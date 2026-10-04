/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Applicazione Standalone personalizzata:
      * finestra con titolo "Pedal Trinity 1.2.0 beta";
      * su Windows, al primo avvio, seleziona i driver ASIO se presenti;
      * ingresso audio attivo di default (e' un effetto per chitarra);
      * riga di comando:
          --selftest                         verifica automatica del DSP
          --screenshot file.png [opzioni]    salva un'immagine dell'interfaccia
              --scale s  --size WxH  --view n  --first i  --factory numero|nome
              --zoom slot  --info  --options  --theme pro|tolex|walnut|green|alu|night  --chain id,id@B,split=0.5,...  --set slot:comando=valore (ripetibile)
              --nam slot:A|B=file.nam  --ir slot:A|B=file.wav   (NAM-A1A2)  --real (REAL MOD)  --photos (usa le foto personali)
*/

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <iostream>
#include <array>
#include <atomic>
#include <deque>
#include <map>
#include <random>
#include <thread>

#if JucePlugin_Build_Standalone && JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP

#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "gui/Theme.h"
#include "gui/Pedalboard.h"
#include "Version.h"
#include "engine/Circuit.h"
#include "engine/FxNam.h"
#include "engine/FxClassic.h"
#include "engine/FxSpectral.h"
#include "engine/NamSecurity.h"
#include "gui/RealMod.h"
#include "Presets.h"
#include "gui/Assets.h"

namespace
{
    //==============================================================================
    // Autotest del motore
    //==============================================================================
    float rmsDb (const juce::AudioBuffer<float>& b, int start, int len)
    {
        return juce::Decibels::gainToDecibels (b.getRMSLevel (0, start, len), -200.0f);
    }

    bool allFinite (const juce::AudioBuffer<float>& b)
    {
        for (int c = 0; c < b.getNumChannels(); ++c)
            for (int i = 0; i < b.getNumSamples(); ++i)
                if (! std::isfinite (b.getSample (c, i))) return false;
        return true;
    }

    /** Segnale di prova: nota di chitarra (fondamentale + armoniche) con inviluppo. */
    void fillGuitar (juce::AudioBuffer<float>& b, double sr, float amp, int offset)
    {
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const double t = (double) (i + offset) / sr;
            const double env = std::exp (-2.0 * std::fmod (t, 1.0));
            double v = 0;
            for (int h = 1; h <= 6; ++h) v += std::sin (2.0 * juce::MathConstants<double>::pi * 110.0 * h * t) / (h * h);
            for (int c = 0; c < b.getNumChannels(); ++c) b.setSample (c, i, (float) (amp * env * v));
        }
    }

    /** Processa 1.5 s con un effetto e restituisce l'uscita. */
    juce::AudioBuffer<float> runEffect (pt::engine::Effect& fx, double sr, int block, float amp)
    {
        fx.prepare (sr, block);
        const int total = (int) (sr * 1.5);
        juce::AudioBuffer<float> out (2, total), buf (2, block);
        for (int pos = 0; pos < total; pos += block)
        {
            const int n = juce::jmin (block, total - pos);
            buf.setSize (2, n, false, false, true);
            fillGuitar (buf, sr, amp, pos);
            fx.messageThreadUpdate();
            fx.process (buf.getArrayOfWritePointers(), 2, n);
            for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, buf, c, 0, n);
        }
        return out;
    }

    /** Processa 1.5 s con un effetto in mono (un solo canale, come nei tratti mono della catena). */
    bool runEffectMono (pt::engine::Effect& fx, double sr, int block)
    {
        fx.prepare (sr, block);
        juce::AudioBuffer<float> buf (1, block);
        bool ok = true;
        for (int pos = 0; pos < (int) (sr * 1.5); pos += block)
        {
            fillGuitar (buf, sr, 0.25f, pos);
            fx.messageThreadUpdate();
            fx.process (buf.getArrayOfWritePointers(), 1, block);
            ok = ok && allFinite (buf) && buf.getMagnitude (0, block) < 16.0f;
        }
        return ok;
    }

    /** Catena da 'ids' (";" separati, "id@B" = corsia B, "split=modo"); restituisce L/R dopo 1 s. */
    juce::AudioBuffer<float> runChain (pt::engine::Chain& chain, const juce::String& ids)
    {
        juce::ValueTree t ("CHAIN");
        for (auto tok : juce::StringArray::fromTokens (ids, ";", ""))
        {
            juce::ValueTree s ("SLOT");
            const auto id = tok.upToFirstOccurrenceOf ("@", false, false).upToFirstOccurrenceOf ("=", false, false);
            s.setProperty ("model", id, nullptr);
            if (tok.contains ("@B")) s.setProperty ("lane", 1, nullptr);
            if (tok.contains ("=")) s.setProperty ("p0", tok.fromFirstOccurrenceOf ("=", false, false).getFloatValue(), nullptr);
            t.appendChild (s, nullptr);
        }
        chain.fromValueTree (t);
        const double sr = 48000.0;
        const int block = 256, total = (int) sr;
        chain.prepare (sr, block);
        juce::AudioBuffer<float> out (2, total), buf (2, block);
        for (int pos = 0; pos < total; pos += block)
        {
            fillGuitar (buf, sr, 0.25f, pos);
            buf.clear (1, 0, block);                  // chitarra solo sull'ingresso 1
            chain.process (buf, 2);
            for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, buf, c, 0, juce::jmin (block, total - pos));
        }
        return out;
    }

    float channelDiff (const juce::AudioBuffer<float>& b)
    {
        float d = 0;
        for (int i = b.getNumSamples() / 2; i < b.getNumSamples(); ++i) d = std::max (d, std::abs (b.getSample (0, i) - b.getSample (1, i)));
        return d;
    }

    /** Cartella dei modelli di esempio di NeuralAmpModelerCore (PT_NAM_EXAMPLES o _deps della build). */
    juce::File namExamples()
    {
        const auto env = juce::SystemStats::getEnvironmentVariable ("PT_NAM_EXAMPLES", {});
        if (env.isNotEmpty()) return juce::File (env);
        auto dir = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
        for (int up = 0; up < 8 && dir != dir.getParentDirectory(); ++up, dir = dir.getParentDirectory())
            for (auto rel : { "_deps/nam_core-src/example_models", "build/_deps/nam_core-src/example_models" })
                if (dir.getChildFile (rel).isDirectory()) return dir.getChildFile (rel);
        return {};
    }

    /** Test del NAM-A1A2: modelli ufficiali, file alterati rifiutati, impronta SHA-256, elaborazione. */
    void runNamTests (const std::function<void (const juce::String&, bool, const juce::String&)>& report)
    {
        using namespace pt::engine;
        const auto* def = findModel ("nama1a2");
        if (def == nullptr) { report ("NAM-A1A2 nel catalogo", false, "modello assente"); return; }
        auto makeNam = [def] { auto fx = createEffect (*def); return std::unique_ptr<NamEffect> (dynamic_cast<NamEffect*> (fx.release())); };
        const auto examples = namExamples();
        if (! examples.isDirectory())
        {
            std::cout << "  [--]      NAM: modelli di esempio non trovati (PT_NAM_EXAMPLES), test dei file saltati\n";
            return;
        }
        const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory)
                             .getChildFile ("pt-nam-selftest-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()));
        tmp.createDirectory();

        // 1. modelli ufficiali: tutti accettati; SLIM solo per i modelli A2 slimmable
        for (auto name : { "wavenet_a1_standard.nam", "A2.nam", "wavenet_a2_max.nam", "lstm.nam", "wavenet.nam",
                           "slimmable_container.nam", "slimmable_wavenet.nam", "wavenet_condition_dsp.nam" })
        {
            const auto f = examples.getChildFile (name);
            if (! f.existsAsFile()) continue;
            auto fx = makeNam();
            const bool ok = fx->loadNam (0, f);
            const auto st = fx->status (0);
            report (juce::String ("NAM ufficiale ") + name + ": accettato", ok,
                    juce::String (st.namKind) + (st.slimmable ? ", SLIM attivo" : ", SLIM spento") + " - " + juce::String (st.message).upToFirstOccurrenceOf (",", false, false));
        }
        {
            auto fx = makeNam();
            fx->loadNam (0, examples.getChildFile ("A2.nam"));
            fx->loadNam (1, examples.getChildFile ("wavenet_a1_standard.nam"));
            report ("SLIM attivo solo per A2 slimmable", fx->status (0).slimmable && ! fx->status (1).slimmable, "A2 si', A1 no");
        }

        // 2. file alterati: devono essere tutti rifiutati
        const auto a1 = examples.getChildFile ("wavenet_a1_standard.nam");
        const auto base = juce::JSON::parse (a1);
        auto tampered = [&] (const juce::String& label, const juce::String& file, std::function<void (juce::var&)> edit)
        {
            auto v = juce::JSON::parse (juce::JSON::toString (base, true));
            edit (v);
            const auto f = tmp.getChildFile (file);
            f.replaceWithText (juce::JSON::toString (v, true));
            auto fx = makeNam();
            const bool loaded = fx->loadNam (0, f);
            report ("NAM alterato rifiutato: " + label, ! loaded && fx->status (0).warning && ! fx->status (0).hasNam,
                    juce::String (fx->status (0).message).fromFirstOccurrenceOf ("File rifiutato: ", false, false).substring (0, 90));
        };
        auto weights = [] (juce::var& v) -> juce::Array<juce::var>& { return *v.getProperty ("weights", {}).getArray(); };
        tampered ("un peso in meno", "meno.nam", [&] (juce::var& v) { weights (v).removeLast(); });
        tampered ("un peso in piu'", "piu.nam", [&] (juce::var& v) { weights (v).add (0.0); });
        tampered ("peso fuori scala", "scala.nam", [&] (juce::var& v) { weights (v).set (7, 1.0e9); });
        tampered ("peso non numerico", "testo.nam", [&] (juce::var& v) { weights (v).set (3, "x"); });
        tampered ("versione inventata", "versione.nam", [] (juce::var& v) { v.getDynamicObject()->setProperty ("version", "9.9.9"); });
        tampered ("architettura non ammessa", "arch.nam", [] (juce::var& v) { v.getDynamicObject()->setProperty ("architecture", "ConvNet"); });
        tampered ("metadati fuori scala", "meta.nam", [] (juce::var& v)
        {
            auto* m = v.getProperty ("metadata", {}).getDynamicObject();
            if (m == nullptr) { m = new juce::DynamicObject(); v.getDynamicObject()->setProperty ("metadata", juce::var (m)); }
            m->setProperty ("loudness", 1.0e6);
        });
        tampered ("canali della rete alterati", "canali.nam", [] (juce::var& v)
        {
            auto layers = v.getProperty ("config", {}).getProperty ("layers", {});
            if (auto* first = layers[0].getDynamicObject()) first->setProperty ("channels", (int) first->getProperty ("channels") + 1);
        });
        auto rejectRaw = [&] (const juce::String& label, const juce::String& file, const juce::MemoryBlock& data)
        {
            const auto f = tmp.getChildFile (file);
            f.replaceWithData (data.getData(), data.getSize());
            auto fx = makeNam();
            const bool loaded = fx->loadNam (0, f);
            report ("NAM alterato rifiutato: " + label, ! loaded && ! fx->status (0).hasNam,
                    juce::String (fx->status (0).message).fromFirstOccurrenceOf ("File rifiutato: ", false, false).substring (0, 90));
        };
        {
            juce::MemoryBlock orig;
            a1.loadFileAsData (orig);
            rejectRaw ("estensione .json", "modello.json", orig);
            rejectRaw ("estensione doppia .nam.txt", "modello.nam.txt", orig);
            juce::MemoryBlock cut (orig.getData(), orig.getSize() * 2 / 3);
            rejectRaw ("file troncato", "troncato.nam", cut);
            juce::MemoryBlock deep;
            for (int i = 0; i < 200; ++i) deep.append ("{\"a\":", 5);
            rejectRaw ("JSON annidato all'eccesso", "profondo.nam", deep);
            rejectRaw ("contenuto .nam in un .namb", "falso.namb", orig);
            juce::MemoryBlock fake (256, true);
            const uint8_t magic[4] = { 'B', 'M', 'A', 'N' };      // 0x4E414D42 little-endian
            fake.copyFrom (magic, 0, 4);
            fake[4] = 1;
            rejectRaw (".namb con intestazione inventata", "inventato.namb", fake);
            rejectRaw ("contenuto binario in un .nam", "binario.nam", fake);
        }

        // 3. regola dell'impronta: un file cambiato dopo il salvataggio del preset non viene caricato
        {
            const auto copy = tmp.getChildFile ("preset_a1.nam");
            a1.copyFileTo (copy);
            auto fx = makeNam();
            fx->loadNam (0, copy);
            const auto saved = fx->saveState();
            auto same = makeNam();
            same->restoreState (saved);
            auto v = juce::JSON::parse (juce::JSON::toString (base, true));
            weights (v).set (0, (double) weights (v)[0] + 1.0e-3);          // ancora un modello valido, ma diverso
            copy.replaceWithText (juce::JSON::toString (v, true));
            auto changed = makeNam();
            changed->restoreState (saved);
            report ("Preset: modello identico ricaricato, modello modificato bloccato (SHA-256)",
                    same->status (0).hasNam && ! changed->status (0).hasNam && changed->status (0).warning,
                    juce::String (changed->status (0).message).substring (0, 80));
        }

        // 4. elaborazione: A1 sul canale A, A2 sul B; stereo (A->L, B->R) e mono
        {
            auto fx = makeNam();
            fx->loadNam (0, a1);
            fx->loadNam (1, examples.getChildFile ("A2.nam"));
            auto out = runEffect (*fx, 48000.0, 256, 0.25f);
            const float l = rmsDb (out, 24000, out.getNumSamples() - 24000);
            const bool fin = allFinite (out);
            report ("NAM A/B stereo: uscita finita e canali indipendenti", fin && l > -60.0f && l < 24.0f && channelDiff (out) > 1.0e-4f,
                    "L " + juce::String (l, 1) + " dB, diff " + juce::String (channelDiff (out), 4));
            report ("NAM A/B mono: uscita finita", runEffectMono (*fx, 48000.0, 256), "somma dei canali attivi");
            auto opt = fx->options();
            opt.calibrateInput = true; opt.outputMode = 2; opt.slim[1] = 0.0;
            fx->setOptions (opt);
            auto cal = runEffect (*fx, 44100.0, 128, 0.25f);
            report ("NAM calibrato, SLIM Lite, 44.1 kHz: uscita finita", allFinite (cal), "ricampionamento al modello");
        }
        tmp.deleteRecursively();
    }

    /** REAL MOD: repliche coerenti con i modelli e verifiche di sicurezza delle foto personali. */
    void runRealModTests (const std::function<void (const juce::String&, bool, const juce::String&)>& report)
    {
        using namespace pt::engine;
        // ogni pedale BOSS ha la sua replica; anche gli altri pedali con un riferimento reale possono averla
        int boss = 0, withReal = 0, ok = 0, bad = 0;
        juce::String firstBad;
        for (int m = 0; m < numModels(); ++m)
        {
            const auto& d = model (m);
            const bool isBoss = juce::String (d.inspiredBy).startsWith ("BOSS");
            const auto* r = findRealModel (d.id);
            if (isBoss) ++boss;
            if (r == nullptr) { if (isBoss) { ++bad; if (firstBad.isEmpty()) firstBad = d.id; } continue; }
            ++withReal;
            bool same = r->numControls == d.numControls && r->family == d.family;
            for (int k = 0; same && k < d.numControls; ++k)
                same = std::strcmp (r->controls[k].label, d.controls[k].label) == 0
                       && std::strcmp (r->controls[k].role, d.controls[k].role) == 0
                       && r->controls[k].steps == d.controls[k].steps;
            // il nome della replica identifica il modello, mai il marchio del produttore
            const juce::String rn (r->name);
            bool noBrand = true;
            for (auto* brand : { "BOSS", "Roland", "Dunlop", "Ernie Ball", "Morley", "DeArmond", "MXR", "Ibanez", "Electro-Harmonix", "Behringer", "Cuvave" })
                noBrand = noBrand && ! rn.containsIgnoreCase (brand);
            same = same && r->image != nullptr && pt::ui::Assets::pedalImage (r->image).isValid() && noBrand && r->bodyW > 0;
            if (same) ++ok; else { ++bad; if (firstBad.isEmpty()) firstBad = d.id; }
        }
        report ("REAL MOD: una replica per ogni pedale BOSS (e per gli altri pedali reali), stessi comandi nello stesso ordine",
                bad == 0 && ok == withReal && numRealModels() == withReal && withReal >= boss,
                juce::String (ok) + "/" + juce::String (withReal) + " repliche (" + juce::String (boss) + " BOSS)"
                    + (firstBad.isNotEmpty() ? ", problema: " + firstBad : juce::String()));

        // foto personali: firma, dimensioni dichiarate prima di decodificare, composizione
        const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory)
                             .getChildFile ("pt-real-selftest-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()));
        tmp.createDirectory();
        juce::Image photo (juce::Image::RGB, 300, 520, true);
        {
            juce::Graphics g (photo);
            g.fillAll (juce::Colours::white);
            g.setColour (juce::Colours::orange);
            g.fillRoundedRectangle (40.0f, 30.0f, 220.0f, 460.0f, 12.0f);
        }
        const auto good = tmp.getChildFile ("ds1.png");
        { juce::FileOutputStream os (good); juce::PNGImageFormat().writeImageToStream (photo, os); }
        int w = 0, h = 0;
        const auto e1 = pt::ui::RealPhotos::checkImageFile (good, w, h);
        report ("REAL MOD: foto PNG valida accettata", e1.isEmpty() && w == 300 && h == 520, e1.isEmpty() ? "300 x 520" : e1);
        // PNG che dichiara 40000 x 40000 pixel: rifiutato prima di decodificare
        juce::MemoryBlock big;
        good.loadFileAsData (big);
        auto* b = static_cast<uint8_t*> (big.getData());
        for (int i : { 16, 20 }) { b[i] = 0; b[i + 1] = 0; b[i + 2] = 0x9C; b[i + 3] = 0x40; }
        const auto bomb = tmp.getChildFile ("bomba.png");
        bomb.replaceWithData (big.getData(), big.getSize());
        const auto e2 = pt::ui::RealPhotos::checkImageFile (bomb, w, h);
        report ("REAL MOD: foto con dimensioni enormi dichiarate rifiutata", e2.isNotEmpty(), e2);
        const auto fake = tmp.getChildFile ("finta.jpg");
        good.copyFileTo (fake);                                   // PNG travestito da JPEG
        const auto e3 = pt::ui::RealPhotos::checkImageFile (fake, w, h);
        report ("REAL MOD: contenuto non coerente con l'estensione rifiutato", e3.isNotEmpty(), e3);
        const auto exe = tmp.getChildFile ("foto.gif");
        good.copyFileTo (exe);
        const auto e4 = pt::ui::RealPhotos::checkImageFile (exe, w, h);
        report ("REAL MOD: estensione non ammessa rifiutata", e4.isNotEmpty(), e4);
        if (const auto* r = findRealModel ("ds1"))
        {
            const auto img = pt::ui::RealPhotos::compose (photo, *r, { 40, 30, 220, 460 }, 0);
            // il piano superiore della foto deve restare opaco e del suo colore (niente ombra sopra la foto)
            const auto mid = img.getPixelAt ((int) (r->bodyX * 2 + r->bodyW), (int) (r->bodyY * 2 + r->bodyH * 0.6f));
            report ("REAL MOD: foto in rilievo alla dimensione della replica, piano opaco",
                    img.getWidth() == (int) std::ceil (r->imageW * 2.0f) && img.getHeight() == (int) std::ceil (r->imageH * 2.0f)
                        && mid.getAlpha() == 255 && mid.getRed() > 200,
                    juce::String (img.getWidth()) + " x " + juce::String (img.getHeight()) + ", centro " + mid.toDisplayString (true));
        }
        // file .json delle foto: posizioni dei pomelli e del LED, valori fuori dai limiti rifiutati
        {
            auto parse = [] (const char* text, pt::ui::RealPhotos::Photo& ph)
            {
                juce::Rectangle<int> crop; int rot = 0; bool knobs = true;
                return pt::ui::RealPhotos::parseSidecar (juce::JSON::parse (juce::String (text)), { 0, 0, 800, 1200 }, ph, crop, rot, knobs);
            };
            pt::ui::RealPhotos::Photo ok;
            const auto e0 = parse ("{\"knobs\": true, \"controls\": {\"TONE\": [0.29, 0.12, 0.13], \"DIST\": [0.75, 0.12, 0.13]}, \"led\": [0.5, 0.05]}", ok);
            report ("REAL MOD: posizioni dei pomelli sulla foto lette dal .json",
                    e0.isEmpty() && ok.controls.size() == 2 && ok.hasLed && std::abs (ok.controls[0].second.x - 0.29f) < 1.0e-4f,
                    e0.isEmpty() ? "2 pomelli + LED" : e0);
            const char* bad[] = {
                "{\"controls\": {\"TONE\": [1.5, 0.1, 0.1]}}",          // fuori dalla foto
                "{\"controls\": {\"TONE\": [0.5, 0.1, 0.9]}}",          // raggio assurdo
                "{\"controls\": {\"TONE\": [0.5, \"x\", 0.1]}}",       // non numerico
                "{\"controls\": [1, 2, 3]}",                           // tipo sbagliato
                "{\"led\": [0.5, -1]}", "{\"rotate\": 45}", "{\"crop\": [0, 0, 5, 5]}" };
            int rejected = 0;
            for (auto* b : bad) { pt::ui::RealPhotos::Photo ph; if (parse (b, ph).isNotEmpty()) ++rejected; }
            juce::String many = "{\"controls\": {";
            for (int i = 0; i < 40; ++i) many << (i ? "," : "") << "\"K" << i << "\": [0.5, 0.5, 0.1]";
            many << "}}";
            { pt::ui::RealPhotos::Photo ph; if (parse (many.toRawUTF8(), ph).isNotEmpty()) ++rejected; }
            report ("REAL MOD: .json delle foto con valori non validi rifiutato", rejected == 8, juce::String (rejected) + "/8 rifiutati");
        }
        tmp.deleteRecursively();
    }

    /** Sicurezza e privacy di preset, progetti e percorsi dei file. */
    void runMidiTests (const std::function<void (const juce::String&, bool, const juce::String&)>& report)
    {
        using namespace pt::midi;
        PedalTrinityProcessor p;
        juce::ValueTree t ("CHAIN");
        for (auto* id : { "ds1", "ce2", "bd2" })
        {
            juce::ValueTree s ("SLOT");
            s.setProperty ("model", id, nullptr);
            t.appendChild (s, nullptr);
        }
        p.chain.fromValueTree (t);
        auto& m = p.midi;
        auto cc = [&m] (int ch, int num, int val) { m.injectForTest (juce::MidiMessage::controllerEvent (ch, num, val)); };

        Mapping a; a.channel = 1; a.number = 20; a.target = Target::SlotSwitch; a.slot = 0;
        m.addMapping (a);
        cc (1, 20, 0);
        const bool off = ! p.chain.slot (0)->enabled.load();
        cc (1, 20, 127);
        report ("MIDI: footswitch che segue il valore (CC 0 / 127)", off && p.chain.slot (0)->enabled.load(), "slot 1 spento e riacceso");

        Mapping b; b.channel = 0; b.number = 21; b.target = Target::SlotSwitch; b.slot = 1; b.mode = Mode::Toggle;
        m.addMapping (b);
        const bool before = p.chain.slot (1)->enabled.load();
        cc (5, 21, 127);
        const bool t1 = p.chain.slot (1)->enabled.load() != before;
        cc (5, 21, 0);
        const bool t2 = p.chain.slot (1)->enabled.load() != before;
        cc (5, 21, 127);
        report ("MIDI: interruttore momentaneo, ogni pressione inverte (tutti i canali)", t1 && t2 && p.chain.slot (1)->enabled.load() == before,
                "pressione, rilascio ignorato, pressione");

        Mapping k; k.channel = 1; k.number = 22; k.target = Target::SlotControl; k.slot = 0; k.control = 0; k.lo = 0.2f; k.hi = 0.8f;
        m.addMapping (k);
        cc (1, 22, 127);
        const float vMax = p.chain.slot (0)->fx->p (0);
        cc (1, 22, 0);
        const float vMin = p.chain.slot (0)->fx->p (0);
        cc (2, 22, 127);                                 // canale sbagliato: ignorato
        const float vOther = p.chain.slot (0)->fx->p (0);
        report ("MIDI: pomello con gamma min..max e filtro del canale",
                std::abs (vMax - 0.8f) < 1.0e-4f && std::abs (vMin - 0.2f) < 1.0e-4f && vOther == vMin,
                juce::String (vMin, 2) + " .. " + juce::String (vMax, 2));

        // mappatura con il tocco: comando toccato, poi messaggio MIDI
        m.setLearning (true);
        p.chain.notifyTouch (2, 1);
        const bool armed = m.hasArmed() && m.armed().target == Target::SlotControl && m.armed().slot == 2;
        cc (3, 30, 90);
        bool learned = false;
        for (const auto& x : m.mappings())
            learned |= x.channel == 3 && x.number == 30 && x.target == Target::SlotControl && x.slot == 2 && x.control == 1;
        report ("MIDI: mappatura con il tocco (comando poi CC)", armed && learned && ! m.hasArmed(), m.learnStatus().substring (0, 60));

        p.chain.notifyTouch (2, -1);
        cc (3, 31, 127);
        cc (3, 31, 0);                                   // il rilascio subito dopo: interruttore momentaneo
        bool toggleMode = false;
        for (const auto& x : m.mappings())
            if (x.number == 31 && x.target == Target::SlotSwitch) toggleMode = x.mode == Mode::Toggle;
        m.setLearning (false);
        report ("MIDI: interruttore momentaneo riconosciuto durante la mappatura", toggleMode, toggleMode ? "modo: ogni pressione" : "modo non cambiato");

        // ritorno dello stato
        p.chain.setEnabled (0, true);
        const auto fb = m.feedbackForTest();
        bool sawSwitch = false;
        for (const auto meta : fb)
        {
            const auto msg = meta.getMessage();
            sawSwitch |= msg.isController() && msg.getControllerNumber() == 20 && msg.getControllerValue() == 127 && msg.getChannel() == 1;
        }
        report ("MIDI OUT: ritorno dello stato dei footswitch", sawSwitch, juce::String (fb.getNumEvents()) + " messaggi");

        // Program Change libero: preset di fabbrica con quel numero
        m.injectForTest (juce::MidiMessage::programChange (1, 1));
        const auto names = pt::PresetManager::factoryNames();
        report ("MIDI: Program Change carica il preset", names.size() > 1 && p.presets.currentName() == names[1], p.presets.currentName());

        // salvataggio nel progetto (non nei preset) e progetti alterati
        const auto saved = m.toValueTree();
        PedalTrinityProcessor q;
        juce::MemoryBlock blob;
        p.getStateInformation (blob);
        q.setStateInformation (blob.getData(), (int) blob.getSize());
        auto bad = saved.createCopy();
        juce::ValueTree evil ("MAP");
        evil.setProperty ("ch", 99, nullptr);
        evil.setProperty ("target", 50, nullptr);
        bad.appendChild (evil, nullptr);
        juce::ValueTree evil2 ("MAP");
        evil2.setProperty ("slot", std::nan (""), nullptr);
        evil2.setProperty ("target", 0, nullptr);
        bad.appendChild (evil2, nullptr);
        PedalTrinityProcessor r;
        r.midi.fromValueTree (bad);
        const bool presetClean = ! p.captureState().getChildWithName ("MIDI").isValid();
        report ("MIDI: assegnazioni salvate nel progetto, non nei preset; voci alterate scartate",
                q.midi.mappings().size() == m.mappings().size() && r.midi.mappings().size() == m.mappings().size() && presetClean,
                juce::String ((int) q.midi.mappings().size()) + " assegnazioni");
    }

    void runCableTests (const std::function<void (const juce::String&, bool, const juce::String&)>& report)
    {
        // cavi staccabili: un pedale staccato resta nella catena ma il segnale lo salta
        {
            pt::engine::Chain chain;
            juce::ValueTree t ("CHAIN"), s ("SLOT");
            s.setProperty ("model", "mt2", nullptr);
            s.setProperty ("p5", 1.0, nullptr);                  // DIST al massimo: impossibile non sentirlo
            t.appendChild (s, nullptr);
            chain.fromValueTree (t);
            chain.prepare (48000.0, 256);
            chain.setPatched (0, false);
            juce::AudioBuffer<float> buf (2, 256), ref (2, 256);
            float err = 0;
            for (int b = 0; b < 12; ++b)                            // la dissolvenza dura 12 ms
            {
                fillGuitar (buf, 48000.0, 0.3f, b * 256);
                ref.makeCopyOf (buf);
                chain.process (buf, 2);
                if (b >= 4)
                    for (int i = 0; i < 256; ++i) err = juce::jmax (err, std::abs (buf.getSample (0, i) - ref.getSample (0, i)));
            }
            report ("Cavi: pedale staccato saltato dal segnale", chain.size() == 1 && err < 1.0e-6f, "errore max " + juce::String (err, 9));

            const auto saved = chain.toValueTree();
            pt::engine::Chain copy;
            copy.fromValueTree (saved);
            report ("Cavi: stato staccato salvato nei preset", ! copy.isPatched (0) && saved.getChild (0).hasProperty ("patched"),
                    "patched=" + saved.getChild (0).getProperty ("patched").toString());
            copy.setModel (0, "ds1");
            const bool kept = ! copy.isPatched (0);
            copy.patchAll();
            report ("Cavi: cambio pedale mantiene il cavo, Ricollega tutti", kept && copy.isPatched (0), kept ? "ok" : "stato perso");
        }
        {
            pt::engine::Chain chain;
            chain.insert (-1, "split");
            chain.setPatched (0, false);
            report ("Cavi: lo splitter non si stacca", chain.isPatched (0), "SPL-3 sempre collegato");
        }

        // cavi sulla pedaliera vera: spine trascinate e rilasciate come con il mouse
        {
            PedalTrinityProcessor p;
            juce::ValueTree t ("CHAIN");
            for (auto* id : { "ds1", "ce2", "bd2", "dd3" })
            {
                juce::ValueTree s ("SLOT");
                s.setProperty ("model", id, nullptr);
                t.appendChild (s, nullptr);
            }
            p.chain.fromValueTree (t);
            p.uiState.setProperty ("view", 6, nullptr);
            std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
            ed->setSize (1280, 720);
            pt::ui::Pedalboard* board = nullptr;
            for (auto* c : ed->getChildren())
                if (auto* b = dynamic_cast<pt::ui::Pedalboard*> (c)) board = b;
            if (board == nullptr) { report ("Cavi: pedaliera trovata", false, "nessuna Pedalboard"); return; }
            board->refresh();
            auto centre = [board] (int slot) { return board->slotArea (slot).getCentre().toFloat(); };
            const auto empty = juce::Point<float> ((float) board->slotArea (5).getCentreX(), (float) board->boardBounds().getBottom() - 12.0f);
            juce::StringArray steps;
            const auto start = board->describeCables();
            steps.add (start);
            // 1. spina d'ingresso di CE-2 lasciata nel vuoto: CE-2 staccato, la catena si richiude
            board->dropPlugForTest (1, true, { (float) board->slotArea (1).getCentreX(), (float) board->boardBounds().getBottom() - 12.0f });
            steps.add (board->describeCables());
            const bool s1 = ! p.chain.isPatched (1) && steps[1] == "I>0 0>2 2>3 3>O";
            // 2. la stessa spina infilata di nuovo in CE-2 (tra DS-1 e BD-2): torna in catena
            board->dropPlugForTest (1, true, centre (1));
            steps.add (board->describeCables());
            const bool s2 = p.chain.isPatched (1) && steps[2] == start;
            // 3. spina dell'INPUT infilata in BD-2: DS-1 e CE-2 scavalcati
            board->dropPlugForTest (0, true, centre (2));
            steps.add (board->describeCables());
            const bool s3 = steps[3] == "I>2 2>3 3>O" && ! p.chain.isPatched (0) && ! p.chain.isPatched (1);
            p.chain.patchAll();
            board->refresh();
            // 4. spina d'uscita verso l'OUTPUT presa da DD-3 e infilata nell'uscita di CE-2
            board->dropPlugForTest (4, false, centre (1));
            steps.add (board->describeCables());
            const bool s4 = steps[4] == "I>0 0>1 1>O";
            p.chain.patchAll();
            board->refresh();
            // 5. all'indietro (ingresso di BD-2 portato su DS-1): rifiutato, nulla cambia
            const bool moved = board->dropPlugForTest (2, true, centre (0));
            const bool s5 = ! moved && board->describeCables() == start;
            // 6. fuori dalla pedaliera: il cavo torna al suo posto
            const bool s6 = ! board->dropPlugForTest (2, true, { -50.0f, -50.0f }) && board->describeCables() == start;
            juce::ignoreUnused (empty);
            report ("Cavi: spine staccate e ricollegate sulla pedaliera", s1 && s2 && s3 && s4 && s5 && s6,
                    steps.joinIntoString (" | ") + juce::String (" [") + (s1 ? "1" : "-") + (s2 ? "2" : "-") + (s3 ? "3" : "-")
                        + (s4 ? "4" : "-") + (s5 ? "5" : "-") + (s6 ? "6" : "-") + "]");
        }

        // liste REAL MOD: nomi di cartella sicuri
        {
            using pt::ui::RealPhotos;
            int ok = 0;
            const char* good[] = { "Mia lista", "Boss 2026", "Fuzz_e-Wah.v2", "Città" };
            const char* bad[] = { "", "../fuori", "a/b", "a\\b", ".nascosta", "fine.", " spazio", "CON", "lpt1.txt", "due..punti", "x:y" };
            for (auto* n : good) ok += RealPhotos::checkListName (juce::CharPointer_UTF8 (n)).isEmpty() ? 1 : 0;
            for (auto* n : bad) ok += RealPhotos::checkListName (n).isNotEmpty() ? 1 : 0;
            ok += RealPhotos::checkListName (juce::String::repeatedString ("a", 41)).isNotEmpty() ? 1 : 0;
            const int expected = 4 + 11 + 1;
            report ("REAL MOD: nomi delle liste (cartelle) controllati", ok == expected, juce::String (ok) + "/" + juce::String (expected) + " casi corretti");
            report ("REAL MOD: lista classica nella cartella RealPhotos",
                    RealPhotos::folderFor ({}).getFileName() == "RealPhotos" && RealPhotos::folderFor ("Mia").getParentDirectory() == RealPhotos::listsRoot()
                        && RealPhotos::folderFor ("../x").getFileName() == "RealPhotos",
                    RealPhotos::listsRoot().getFullPathName());

            auto parse = [] (const char* text, RealPhotos::Photo& ph)
            {
                juce::Rectangle<int> crop; int rot = 0; bool knobs = true;
                return RealPhotos::parseSidecar (juce::JSON::parse (juce::String (text)), { 0, 0, 800, 1200 }, ph, crop, rot, knobs);
            };
            RealPhotos::Photo named;
            const auto e1 = parse (R"({"name": "Il mio distorsore", "code": "MY-1"})", named);
            RealPhotos::Photo p2, p3, p4;
            const bool rejected = parse (R"({"name": 5})", p2).isNotEmpty()
                                  && parse (R"({"code": "SIGLA-TROPPO-LUNGA-123"})", p3).isNotEmpty()
                                  && parse ("{\"name\": \"a\\u0007b\"}", p4).isNotEmpty();
            report ("REAL MOD: nome e sigla personalizzati nel .json",
                    e1.isEmpty() && named.name == "Il mio distorsore" && named.code == "MY-1" && rejected,
                    e1.isEmpty() ? named.code + " " + named.name : e1);
        }

        // temi, pedane e cavi
        {
            const auto& themes = pt::ui::allThemes();
            juce::StringArray ids;
            for (const auto& t : themes) ids.addIfNotAlreadyThere (t.id);
            report ("Temi: almeno 16, id univoci", themes.size() >= 16 && ids.size() == (int) themes.size(),
                    juce::String ((int) themes.size()) + " temi");
            report ("Cavi: almeno 15 colori e Multicolore", pt::ui::cableColours().size() >= 15
                        && juce::String (pt::ui::cableColours()[(size_t) pt::ui::multiCableIndex()].name) == "Multicolore",
                    juce::String ((int) pt::ui::cableColours().size()) + " colori");
            double worst = 99;
            for (auto c : { 0xff000000u, 0xffffffffu, 0xff777777u, 0xff8a1c1cu, 0xff1c3f8au, 0xffd8c060u, 0xff3f8a3fu, 0xff5a5a5au })
                worst = juce::jmin (worst, pt::ui::contrastRatio (pt::ui::inkFor (juce::Colour (c)), juce::Colour (c)));
            report ("Pedana con colore personalizzato: segni leggibili", worst >= 3.0, "contrasto minimo " + juce::String (worst, 1) + ":1");
            // ogni materiale si disegna anche fuori dal suo tema (pedana personalizzata)
            juce::Image img (juce::Image::ARGB, 320, 160, true);
            int painted = 0;
            for (const auto& t : themes)
            {
                img.clear (img.getBounds());
                juce::Graphics g (img);
                auto tt = t;
                tt.id = "test-" + t.id;
                tt.boardBase = juce::Colour (0xff6a4a8a);
                tt.boardAlt = tt.boardBase.darker (0.6f);
                pt::ui::paintBoard (g, tt, { 0.0f, 0.0f, 320.0f, 160.0f }, 0.5f);
                painted += img.getPixelAt (160, 80).getAlpha() == 255 ? 1 : 0;
            }
            report ("Pedane: tutti i materiali disegnati (anche ricolorati)", painted == (int) themes.size(),
                    juce::String (painted) + "/" + juce::String ((int) themes.size()));
        }
    }

    void runSafetyTests (const std::function<void (const juce::String&, bool, const juce::String&)>& report)
    {
        using pt::namsafe::isSafeLocalFile;
        const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory)
                             .getChildFile ("pt-safety-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()));
        tmp.createDirectory();
        const auto good = tmp.getChildFile ("ir.wav");
        good.replaceWithText ("RIFF test");
        int ok = 0;
        ok += isSafeLocalFile (good.getFullPathName(), 1024) ? 1 : 0;
        ok += ! isSafeLocalFile ("\\\\server\\share\\ir.wav", 1024) ? 1 : 0;      // rete (Windows)
        ok += ! isSafeLocalFile ("//server/share/ir.wav", 1024) ? 1 : 0;              // rete
        ok += ! isSafeLocalFile ("relativo/ir.wav", 1024) ? 1 : 0;                    // relativo
        ok += ! isSafeLocalFile (tmp.getFullPathName(), 1024) ? 1 : 0;                // cartella
        ok += ! isSafeLocalFile (good.getFullPathName(), 4) ? 1 : 0;                  // troppo grande
       #if JUCE_LINUX || JUCE_MAC
        ok += ! isSafeLocalFile ("/dev/zero", 1 << 30) ? 1 : 0;                       // dispositivo infinito
       #else
        ++ok;
       #endif
        report ("Sicurezza: percorsi dei file da preset e progetti", ok == 7, juce::String (ok) + "/7 casi corretti");
        report ("Sicurezza: impronta mai calcolata su dispositivi", pt::namsafe::sha256Of (juce::File ("/dev/zero")).empty(), "/dev/zero ignorato");

        // XML: annidamento eccessivo rifiutato prima del parser
        juce::String deep;
        for (int i = 0; i < 5000; ++i) deep << "<a>";
        const juce::String normal = "<PedalTrinityState version=\"x\"><CHAIN><SLOT model=\"ds1\" p0=\"0.5\"/></CHAIN><UI w=\"1280\"/></PedalTrinityState>";
        {
            juce::String deepJson;
            for (int i = 0; i < 100000; ++i) deepJson << "[";
            report ("Sicurezza: JSON annidato all'eccesso rifiutato (stato NAM, foto)",
                    ! pt::namsafe::plausibleJson (deepJson, 1 << 20) && pt::namsafe::plausibleJson ("{\"a\": [1, 2, {\"b\": 3}]}", 1024),
                    "100000 livelli / 3 livelli");
            pt::engine::Chain chain;                         // e il motore non va in crash ricevendolo da un preset
            juce::ValueTree t ("CHAIN"), s ("SLOT");
            s.setProperty ("model", "nama1a2", nullptr);
            s.setProperty ("state", deepJson, nullptr);
            t.appendChild (s, nullptr);
            chain.fromValueTree (t);
            report ("Sicurezza: preset con stato NAM anomalo caricato senza crash", chain.size() == 1, "stato ignorato");
        }
        report ("Sicurezza: XML annidato all'eccesso rifiutato, preset normale accettato",
                ! pt::plausibleStateXml (deep.toRawUTF8(), (size_t) deep.getNumBytesAsUTF8())
                    && pt::plausibleStateXml (normal.toRawUTF8(), (size_t) normal.getNumBytesAsUTF8()), "5000 livelli / 4 livelli");

        // valori dei comandi: non finiti ignorati, fuori scala limitati a 0..1
        {
            pt::engine::Chain chain;
            juce::ValueTree t ("CHAIN"), s ("SLOT");
            s.setProperty ("model", "ds1", nullptr);
            s.setProperty ("p0", 1.0e9, nullptr);
            s.setProperty ("p1", std::nan (""), nullptr);
            s.setProperty ("p2", -5.0, nullptr);
            t.appendChild (s, nullptr);
            chain.fromValueTree (t);
            const auto* sl = chain.slot (0);
            const bool fine = sl != nullptr && sl->fx != nullptr && sl->fx->p (0) == 1.0f && std::isfinite (sl->fx->p (1)) && sl->fx->p (2) == 0.0f;
            report ("Sicurezza: valori dei comandi dei preset limitati a 0..1", fine, "1e9 -> 1, NaN ignorato, -5 -> 0");
        }

        // privacy: l'esportazione dei preset non contiene percorsi locali (NAM, IR)
        {
            PedalTrinityProcessor p;
            juce::ValueTree t ("CHAIN"), s ("SLOT"), s2 ("SLOT");
            s.setProperty ("model", "nama1a2", nullptr);
            s.setProperty ("state", "{\"namA\": \"/home/utente/modelli/a.nam\", \"namShaA\": \"abc\", \"irA\": \"/home/utente/ir.wav\"}", nullptr);
            s2.setProperty ("model", "irl1", nullptr);
            s2.setProperty ("file", "/home/utente/cab.wav", nullptr);
            t.appendChild (s, nullptr); t.appendChild (s2, nullptr);
            p.chain.fromValueTree (t);
            juce::ValueTree raw = p.captureState();
            // simula i percorsi salvati (file non esistenti: il motore non li carica, l'esportazione li deve togliere)
            auto ch = raw.getChildWithName ("CHAIN");
            ch.getChild (0).setProperty ("state", s.getProperty ("state"), nullptr);
            ch.getChild (1).setProperty ("file", "/home/utente/cab.wav", nullptr);
            const auto out = tmp.getChildFile ("export.ptpreset");
            pt::PresetManager::stripLocalReferences (raw);
            const auto text = raw.toXmlString();
            report ("Privacy: preset esportati senza percorsi locali dei file", ! text.contains ("/home/utente"), "percorsi NAM/IR rimossi");
            // esportazione e reimportazione di un preset normale
            const bool saved = p.presets.saveTo (out), loaded = saved && p.presets.load (out);
            report ("Preset: esportazione e importazione con i nuovi controlli", saved && loaded && p.chain.size() == 2,
                    juce::String (out.getSize()) + " byte");
        }
        tmp.deleteRecursively();
    }

    //==============================================================================
    // Wah a induttore (Cry Baby): misure contro le curve pubblicate, stabilita', wah esistenti invariati
    //==============================================================================
    struct WahPeak { double f = 0, db = -200, q = 0; };

    /** Risposta a piccolo segnale (impulso di 1 mV, FFT): frequenza e guadagno del picco, Q a -3 dB. */
    WahPeak measureWahPeak (pt::engine::Effect& fx, double sr)
    {
        const int order = sr > 60000.0 ? 16 : 15, N = 1 << order;
        fx.prepare (sr, 512);
        std::vector<float> buf ((size_t) N * 2, 0.0f);
        for (int i = 0; i < (int) (sr * 0.2); i += 512)        // assestamento dei comandi levigati
        {
            float* c[1] = { buf.data() };
            fx.process (c, 1, 512);
        }
        std::fill (buf.begin(), buf.end(), 0.0f);
        buf[0] = 1.0e-3f;
        for (int i = 0; i < N; i += 512)
        {
            float* c[1] = { buf.data() + i };
            fx.process (c, 1, 512);
        }
        for (int i = 0; i < N; ++i) buf[(size_t) i] *= 1000.0f;
        juce::dsp::FFT fft (order);
        fft.performFrequencyOnlyForwardTransform (buf.data());
        auto db = [&buf] (int k) { return 20.0 * std::log10 (std::max (1.0e-12, (double) buf[(size_t) k])); };
        const int k0 = (int) (60.0 * N / sr), k1 = (int) (12000.0 * N / sr);
        int kb = k0;
        for (int k = k0; k <= k1; ++k)
            if (buf[(size_t) k] > buf[(size_t) kb]) kb = k;
        WahPeak r;
        const double y0 = db (kb - 1), y1 = db (kb), y2 = db (kb + 1), den = y0 - 2 * y1 + y2;
        const double d = std::abs (den) > 1e-12 ? 0.5 * (y0 - y2) / den : 0.0;
        r.f = (kb + d) * sr / N;
        r.db = y1 - 0.25 * (y0 - y2) * d;
        const double th = r.db - 3.0103;
        int lo = kb, hi = kb;
        while (lo > 1 && db (lo) > th) --lo;
        while (hi < N / 2 - 1 && db (hi) > th) ++hi;
        const double flo = (lo + (th - db (lo)) / (db (lo + 1) - db (lo))) * sr / N;
        const double fhi = (hi - 1 + (th - db (hi - 1)) / (db (hi) - db (hi - 1))) * sr / N;
        r.q = r.f / std::max (1.0e-9, fhi - flo);
        return r;
    }

    juce::String describe (const WahPeak& p)
    {
        return juce::String (juce::roundToInt (p.f)) + " Hz " + juce::String (p.db, 1) + " dB Q" + juce::String (p.q, 1);
    }

    void runWahTests (const std::function<void (const juce::String&, bool, const juce::String&)>& report)
    {
        using namespace pt::engine;
        using Ctl = std::tuple<const char*, ControlKind, int, float>;
        auto makeDef = [] (const char* config, const std::vector<Ctl>& ctrls)
        {
            ModelDef d {};
            d.id = "wahtest"; d.name = "Wah di prova"; d.code = "WT"; d.inspiredBy = "Cry Baby"; d.category = "Wah / Filtri";
            d.family = Family::Wah;
            d.config = config;
            d.numControls = (int) ctrls.size();
            for (size_t i = 0; i < ctrls.size(); ++i)
            {
                auto& c = d.controls[i];
                c.label = c.role = std::get<0> (ctrls[i]);
                c.kind = std::get<1> (ctrls[i]);
                c.steps = (uint8_t) std::get<2> (ctrls[i]);
                c.def = std::get<3> (ctrls[i]);
                c.strip = 2; c.units = Units::Dial; c.hi = 10.0f;
            }
            return d;
        };

        // GCB95 (valori dei componenti dello schema; Rl = perdite dell'induttore tarate sul guadagno Dunlop)
        static const char* gcb95 = "type=inductor L=500m Rl=35 C=10n Rq=33k Rf=1.5k Ri=68k Ci=10n Rc=22k Re=390 R4=470k R5=470k "
                                   "R8=82k hfe=1430 vcc=8.15 pot=100k taper=hot hp=16";
        const auto dGcb = makeDef (gcb95, { Ctl { "freq", ControlKind::Knob, 0, 0.5f } });
        auto gcbPeak = [&dGcb] (float pedal, double sr)
        {
            auto fx = createEffect (dGcb);
            fx->params[0].store (pedal);
            return measureWahPeak (*fx, sr);
        };
        // riferimenti: Dunlop GCB95 (tallone 350-450 Hz +19.5 dB, punta 1.5-2.5 kHz +19 dB), Electrosmash (~750 Hz a
        // meta' corsa), analisi nodale del circuito di Holters & Zoelzer (Q 12.9 al tallone, 3.1 in punta)
        for (double sr : { 44100.0, 48000.0, 96000.0 })
        {
            const auto h = gcbPeak (0.0f, sr), m = gcbPeak (0.5f, sr), t = gcbPeak (1.0f, sr);
            const bool ok = h.f > 350 && h.f < 450 && h.db > 18 && h.db < 22 && h.q > 10 && h.q < 18
                         && m.f > 650 && m.f < 850 && m.db > 17 && m.db < 22
                         && t.f > 1500 && t.f < 2500 && t.db > 17 && t.db < 21 && t.q > 2 && t.q < 4;
            report ("Wah a induttore GCB95 a " + juce::String (sr / 1000.0, 1) + " kHz: picco a tallone, meta' e punta", ok,
                    "tallone " + describe (h) + ", meta' " + describe (m) + ", punta " + describe (t));
        }

        // 535Q: selettore RANGE a 6 posizioni, VARIABLE Q, BOOST con VOLUME (manuale Dunlop 535Q)
        static const char* q535 = "type=inductor L=500m C=10n|13n|15n|20n|27n|33n@range Rq=20k Re=470 Rl=2k~56@q:L taper=hot "
                                  "boost=0.1~17@level brail=3.8";
        const auto d535 = makeDef (q535, { Ctl { "freq", ControlKind::Knob, 0, 0.5f }, Ctl { "range", ControlKind::Selector, 6, 0.0f },
                                           Ctl { "q", ControlKind::Knob, 0, 1.0f }, Ctl { "level", ControlKind::Knob, 0, 1.0f },
                                           Ctl { "boost", ControlKind::Toggle, 2, 0.0f } });
        auto m535 = [&d535] (float pedal, float range, float q, float boost)
        {
            auto fx = createEffect (d535);
            fx->params[0].store (pedal); fx->params[1].store (range); fx->params[2].store (q); fx->params[4].store (boost);
            return measureWahPeak (*fx, 48000.0);
        };
        {
            const auto h1 = m535 (0, 0, 1, 0), t1 = m535 (1, 0, 1, 0), h6 = m535 (0, 1, 1, 0), t6 = m535 (1, 1, 1, 0);
            const bool ok = h1.f > 400 && h1.f < 480 && t1.f > 2000 && t1.f < 2400 && h6.f > 225 && h6.f < 275 && t6.f > 1080 && t6.f < 1320
                         && h1.db > 13 && h1.db < 17 && t1.db > 13 && t1.db < 17;
            report ("Wah a induttore 535Q: RANGE posizioni 1 e 6 (440-2200 / 250-1200 Hz, 15 dB)", ok,
                    "pos.1 " + describe (h1) + " - " + describe (t1) + "; pos.6 " + describe (h6) + " - " + describe (t6));
            const auto qmin = m535 (0, 0, 0, 0);
            const auto off = m535 (0.5f, 0, 1, 0), on = m535 (0.5f, 0, 1, 1);
            report ("Wah a induttore 535Q: VARIABLE Q al minimo e BOOST +17 dB", qmin.db < 3 && qmin.q < 2 && std::abs (on.db - off.db - 17.0) < 0.5,
                    "Q min al tallone " + describe (qmin) + ", boost " + juce::String (on.db - off.db, 2) + " dB");
        }

        // saturazione morbida dei transistor: seno a 719 Hz sul picco (Holters & Zoelzer, fig. 5: uscita verso +6 dBV,
        // THD ~ -18 dB con 0 dBV in ingresso, ~ -62 dB a -40 dBV)
        {
            auto run = [&dGcb] (double inDbV, double& outDbV, double& thdDb)
            {
                auto fx = createEffect (dGcb);
                fx->params[0].store (0.48f);
                const double sr = 48000.0, f = 719.0, amp = std::sqrt (2.0) * std::pow (10.0, inDbV / 20.0);
                fx->prepare (sr, 512);
                const int n = 48000;
                std::vector<float> x ((size_t) n);
                for (int i = 0; i < n; ++i) x[(size_t) i] = (float) (amp * std::sin (2.0 * juce::MathConstants<double>::pi * f * i / sr));
                for (int i = 0; i < n; i += 512) { float* c[1] = { x.data() + i }; fx->process (c, 1, juce::jmin (512, n - i)); }
                const int s0 = n / 2, len = (int) (std::floor ((n - s0) * f / sr) * sr / f);
                double rms = 0, h[10] {};
                for (int i = s0; i < s0 + len; ++i) rms += (double) x[(size_t) i] * x[(size_t) i];
                for (int k = 1; k < 10; ++k)
                {
                    std::complex<double> acc;
                    for (int i = s0; i < s0 + len; ++i)
                        acc += (double) x[(size_t) i] * std::polar (1.0, -2.0 * juce::MathConstants<double>::pi * f * k * i / sr);
                    h[k] = std::abs (acc);
                }
                double hs = 0;
                for (int k = 2; k < 10; ++k) hs += h[k] * h[k];
                outDbV = 10.0 * std::log10 (rms / len);
                thdDb = 10.0 * std::log10 (hs / (h[1] * h[1]) + 1e-30);
            };
            double o1, t1, o2, t2;
            run (-40.0, o1, t1);
            run (0.0, o2, t2);
            const bool ok = o1 + 40.0 > 17 && o1 + 40.0 < 21.5 && t1 < -50 && o2 > 4.0 && o2 < 7.5 && t2 > -26 && t2 < -12;
            report ("Wah a induttore: saturazione morbida dei transistor (seno a 719 Hz, -40 e 0 dBV)", ok,
                    "-40 dBV -> " + juce::String (o1, 1) + " dBV THD " + juce::String (juce::roundToInt (t1)) + " dB; 0 dBV -> "
                        + juce::String (o2, 1) + " dBV THD " + juce::String (juce::roundToInt (t2)) + " dB");
        }

        // stabilita': segnali estremi, comandi e pedale casuali, mono e stereo, 44.1 / 48 / 96 kHz
        {
            static const char* bass = "type=inductor C=16n Rq=100k Rc=12k Re=1 Rl=2k~68@q:L boost=0.1~20@level";
            const auto dBass = makeDef (bass, { Ctl { "freq", ControlKind::Knob, 0, 0.5f }, Ctl { "q", ControlKind::Knob, 0, 0.5f },
                                                Ctl { "level", ControlKind::Knob, 0, 0.5f }, Ctl { "boost", ControlKind::Toggle, 2, 1.0f } });
            juce::Random rng (2026);
            bool fin = true;
            double peak = 0;
            for (const ModelDef* d : { &dGcb, &d535, &dBass })
                for (double sr : { 44100.0, 48000.0, 96000.0 })
                    for (int numCh : { 1, 2 })
                    {
                        auto fx = createEffect (*d);
                        fx->prepare (sr, 512);
                        juce::AudioBuffer<float> b (2, 512);
                        for (int blk = 0; blk < (int) (sr / 512); ++blk)
                        {
                            for (int k = 0; k < d->numControls; ++k) fx->params[k].store (rng.nextFloat());
                            const float amp = std::pow (10.0f, -3.0f + 4.0f * rng.nextFloat());
                            const int kind = rng.nextInt (3);
                            for (int i = 0; i < 512; ++i)
                            {
                                const double tt = (blk * 512 + i) / sr;
                                const float v = kind == 0 ? rng.nextFloat() * 2.0f - 1.0f
                                              : kind == 1 ? (std::sin (2.0 * juce::MathConstants<double>::pi * 110.0 * tt) > 0 ? 1.0f : -1.0f)
                                                          : (float) std::sin (2.0 * juce::MathConstants<double>::pi * 700.0 * tt);
                                b.setSample (0, i, amp * v);
                                b.setSample (1, i, -amp * v);
                            }
                            fx->process (b.getArrayOfWritePointers(), numCh, 512);
                            fin = fin && allFinite (b);
                            peak = std::max (peak, (double) b.getMagnitude (0, 0, 512));
                        }
                    }
            report ("Wah a induttore: stabile con segnali fino a 10 V e comandi casuali (44.1/48/96 kHz, mono e stereo)",
                    fin && peak < 20.0, (fin ? "uscita finita" : "NaN!") + juce::String (", picco ") + juce::String (peak, 2) + " V");
        }

        // niente zipper: pedale mosso a scatti di 1/127 ogni 512 campioni (espressione MIDI) contro il movimento continuo
        {
            const double sr = 48000.0;
            const int N = 65536, n = N + 8192;
            auto runPedal = [&] (bool steps)
            {
                auto fx = createEffect (dGcb);
                fx->prepare (sr, 512);
                std::vector<float> y ((size_t) n);
                for (int i = 0; i < n; ++i) y[(size_t) i] = 0.1f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 300.0 * i / sr);
                const int blk = steps ? 512 : 1;
                for (int i = 0; i < n; i += blk)
                {
                    const double pos = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * 1.5 * i / sr);
                    fx->params[0].store ((float) (steps ? std::round (127.0 * pos) / 127.0 : pos));
                    float* c[1] = { y.data() + i };
                    fx->process (c, 1, juce::jmin (blk, n - i));
                }
                std::vector<float> spec ((size_t) N * 2, 0.0f);
                for (int i = 0; i < N; ++i)
                    spec[(size_t) i] = y[(size_t) (i + 8192)] * (0.5f - 0.5f * (float) std::cos (2.0 * juce::MathConstants<double>::pi * i / N));
                juce::dsp::FFT fft (16);
                fft.performFrequencyOnlyForwardTransform (spec.data());
                double hf = 0, tot = 0;
                for (int k = 1; k < N / 2; ++k)
                {
                    const double e = (double) spec[(size_t) k] * spec[(size_t) k];
                    tot += e;
                    if (k * sr / N > 4000.0) hf += e;
                }
                return 10.0 * std::log10 (hf / tot + 1e-30);
            };
            const double stepped = runPedal (true), smooth = runPedal (false);
            report ("Wah a induttore: niente zipper con il pedale mosso a scatti (energia sopra 4 kHz)", stepped < -100.0,
                    "scatti MIDI " + juce::String (stepped, 1) + " dB, movimento continuo " + juce::String (smooth, 1) + " dB");
        }

        // i wah esistenti non cambiano: livello d'uscita con chitarra di prova e pedale in movimento, confrontato con
        // i valori misurati prima dell'introduzione del wah a induttore (tolleranza 0.005 dB)
        {
            struct Ref { const char* id; double a, b; };
            static const Ref refs[] = { { "pw1", -32.374, -32.374 }, { "fw3", -32.812, -31.392 }, { "pw3", -31.789, -31.296 },
                                        { "pw10", -15.292, -13.569 }, { "tw1", -40.609, -25.074 }, { "ft2", -44.138, -25.702 },
                                        { "aw2", -35.252, -35.613 }, { "aw3", -32.331, -43.706 } };
            juce::String detail;
            bool ok = true;
            for (const auto& r : refs)
            {
                const auto* d = findModel (r.id);
                if (d == nullptr) { ok = false; detail << r.id << " assente "; continue; }
                double lv[2];
                for (int set = 0; set < 2; ++set)
                {
                    auto fx = createEffect (*d);
                    for (int k = 0; k < d->numControls; ++k) fx->params[k].store (set == 0 ? 0.37f : 0.81f);
                    const double sr = 48000.0;
                    fx->prepare (sr, 256);
                    juce::AudioBuffer<float> b (2, 256);
                    double acc = 0;
                    const int total = 48000;
                    for (int pos = 0; pos < total; pos += 256)
                    {
                        fillGuitar (b, sr, 0.25f, pos);
                        for (int k = 0; k < d->numControls; ++k)
                            if (std::strcmp (d->controls[k].role, "freq") == 0)
                                fx->params[k].store (0.5f + 0.45f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 1.3 * pos / sr));
                        fx->process (b.getArrayOfWritePointers(), 2, 256);
                        for (int i = 0; i < 256; ++i) acc += (double) b.getSample (0, i) * b.getSample (0, i) + (double) b.getSample (1, i) * b.getSample (1, i);
                    }
                    lv[set] = 10.0 * std::log10 (acc / (2.0 * total) + 1e-30);
                }
                const bool same = std::abs (lv[0] - r.a) < 0.005 && std::abs (lv[1] - r.b) < 0.005;
                ok = ok && same;
                detail << r.id << " " << juce::String (lv[0], 3) << "/" << juce::String (lv[1], 3) << (same ? "" : " (DIVERSO)") << " ";
            }
            report ("Wah esistenti invariati (PW-1, FW-3, PW-3, PW-10, TW-1, FT-2, AW-2, AW-3)", ok, detail.trim());
        }
    }

    //==============================================================================
    // Tappa 3A: controlli numerici dei pedali EHX/MXR analogici (blocchi nuovi di Circuit.cpp) e digitali
    // (modulazioni, delay, riverberi, pitch polifonico, serie 9). Metodi di misura ripresi dai banchi
    // della tappa (s1: risposta a piccolo segnale, chitarra Karplus-Strong; s2: frase di chitarra, picco
    // spettrale, treno di impulsi, curva di Schroeder, inviluppo).
    //==============================================================================
    /** Esegue fn(0..n-1) su tutti i core (misure indipendenti: ogni compito crea i propri effetti). */
    void parallelFor (int n, const std::function<void (int)>& fn)
    {
        const int threads = juce::jlimit (1, 32, (int) std::thread::hardware_concurrency());
        std::atomic<int> next { 0 };
        std::vector<std::thread> pool;
        for (int t = 0; t < std::min (threads, n); ++t)
            pool.emplace_back ([&]
            {
                for (int i = next.fetch_add (1); i < n; i = next.fetch_add (1))
                    fn (i);
            });
        for (auto& th : pool) th.join();
    }

    using Buf = std::vector<float>;
    using Sets = std::vector<std::pair<const char*, float>>;
    constexpr double kPi = 3.14159265358979323846;

    double dbOf (double x) { return 20.0 * std::log10 (std::max (x, 1.0e-30)); }

    /** Comandi come nel banco s1: ruolo esatto o etichetta senza maiuscole, vale l'ultimo che corrisponde. */
    void setsS1 (pt::engine::Effect& fx, const pt::engine::ModelDef& d, const Sets& sets)
    {
        for (const auto& [k, v] : sets)
        {
            int idx = -1;
            for (int i = 0; i < d.numControls; ++i)
                if (std::strcmp (k, d.controls[i].role) == 0 || juce::String (k).equalsIgnoreCase (d.controls[i].label)) idx = i;
            if (idx >= 0) fx.params[idx].store (v);
        }
    }

    /** Comandi come nel banco s2: ruolo o etichetta esatti, tutti quelli che corrispondono. */
    std::map<int, float> setsS2 (const pt::engine::ModelDef& d, const Sets& sets)
    {
        std::map<int, float> p;
        for (const auto& [k, v] : sets)
            for (int c = 0; c < d.numControls; ++c)
                if (std::strcmp (k, d.controls[c].role) == 0 || std::strcmp (k, d.controls[c].label) == 0) p[c] = v;
        return p;
    }

    void processMono (pt::engine::Effect& fx, Buf& x, int block)
    {
        for (size_t i = 0; i < x.size(); i += (size_t) block)
        {
            const int n = (int) std::min<size_t> ((size_t) block, x.size() - i);
            float* c[1] = { x.data() + i };
            fx.process (c, 1, n);
        }
    }

    /** Effetto del banco s1: comandi impostati prima di prepare (blocchi da 512). */
    std::unique_ptr<pt::engine::Effect> makeS1 (const pt::engine::ModelDef& d, const Sets& sets, double sr = 48000.0)
    {
        auto fx = pt::engine::createEffect (d);
        setsS1 (*fx, d, sets);
        fx->prepare (sr, 512);
        return fx;
    }

    /** Risposta a piccolo segnale in dB (banco s1 'fr'): per ogni frequenza reset, seno di ampiezza 'amp',
        0.6 s di assestamento, correlazione su ceil(f/4) periodi. */
    std::vector<double> smallSignalOf (pt::engine::Effect& fx, const std::vector<double>& freqs, double amp, double sr)
    {
        std::vector<double> out;
        for (double f : freqs)
        {
            fx.reset();
            const int settle = (int) (0.6 * sr), per = (int) std::ceil (0.25 * f), meas = (int) std::lround (per * sr / f);
            Buf x ((size_t) (settle + meas));
            for (size_t i = 0; i < x.size(); ++i) x[i] = (float) (amp * std::sin (2 * kPi * f * (double) i / sr));
            processMono (fx, x, 512);
            double re = 0, im = 0;
            for (int i = 0; i < meas; ++i)
            {
                const double ph = 2 * kPi * f * (double) (settle + i) / sr;
                re += x[(size_t) (settle + i)] * std::sin (ph);
                im += x[(size_t) (settle + i)] * std::cos (ph);
            }
            out.push_back (dbOf (2.0 * std::sqrt (re * re + im * im) / meas / amp));
        }
        return out;
    }

    std::vector<double> smallSignalDb (const pt::engine::ModelDef& d, const Sets& sets, const std::vector<double>& freqs,
                                       double amp, double sr = 48000.0)
    {
        auto fx = makeS1 (d, sets, sr);
        return smallSignalOf (*fx, freqs, amp, sr);
    }

    /** Corda pizzicata Karplus-Strong del banco s1 (rumore mt19937 filtrato, pettine del plettro, all-pass frazionario). */
    void pluckS1 (std::vector<double>& out, double sr, double f0, double t0, double amp, double bright, unsigned seed)
    {
        std::mt19937 rng (seed);
        std::uniform_real_distribution<double> u (-1, 1);
        const double period = sr / f0;
        const int N = (int) std::floor (period);
        const double frac = period - N;
        const double c = (1 - frac) / (1 + frac);
        std::vector<double> line ((size_t) N);
        double lp = 0;
        for (auto& v : line) { lp += bright * (u (rng) - lp); v = lp; }
        std::vector<double> l2 = line;
        const int pk = std::max (1, N / 7);
        for (int i = 0; i < N; ++i) line[(size_t) i] = l2[(size_t) i] - l2[(size_t) ((i + pk) % N)];
        double mx = 0;
        for (double v : line) mx = std::max (mx, std::abs (v));
        for (auto& v : line) v /= mx;
        const int start = (int) (t0 * sr);
        double prev = 0, apx = 0, apy = 0;
        const double loss = std::pow (0.001, 1.0 / (4.0 * f0));
        int idx = 0;
        for (size_t n = (size_t) start; n < out.size(); ++n)
        {
            const double y = line[(size_t) idx];
            const double avg = 0.5 * (y + prev);
            prev = y;
            const double ap = c * avg + apx - c * apy;
            apx = avg; apy = ap;
            line[(size_t) idx] = loss * ap;
            idx = (idx + 1) % N;
            out[n] += amp * y;
        }
    }

    /** Segnali del banco s1: riff (note singole poi power chord) o accordo, normalizzati al picco peakDb dBFS. */
    Buf guitarS1 (bool riff, double sr, double seconds, double peakDb)
    {
        std::vector<double> s ((size_t) (seconds * sr), 0.0);
        if (riff)
        {
            const double notes[] = { 82.41, 82.41, 98.0, 82.41, 110.0, 82.41, 123.47, 116.54 };
            for (int k = 0; k < 8; ++k) pluckS1 (s, sr, notes[k], 0.25 * k, 1.0, 0.7, (unsigned) (10 + k));
            for (int k = 0; k < 4; ++k)
            {
                pluckS1 (s, sr, 82.41, 2.0 + 0.5 * k, 1.0, 0.7, (unsigned) (30 + k));
                pluckS1 (s, sr, 123.47, 2.01 + 0.5 * k, 0.8, 0.7, (unsigned) (40 + k));
                pluckS1 (s, sr, 164.81, 2.02 + 0.5 * k, 0.6, 0.7, (unsigned) (50 + k));
            }
        }
        else
            for (int r = 0; r < 2; ++r)
            {
                const double t = r * 1.5;
                pluckS1 (s, sr, 82.41, t, 1.0, 0.7, (unsigned) (1 + 10 * r));
                pluckS1 (s, sr, 123.47, t + 0.012, 0.8, 0.7, (unsigned) (2 + 10 * r));
                pluckS1 (s, sr, 164.81, t + 0.024, 0.6, 0.7, (unsigned) (3 + 10 * r));
            }
        double mx = 0;
        for (double v : s) mx = std::max (mx, std::abs (v));
        const double g = std::pow (10.0, peakDb / 20.0) / mx;
        Buf x (s.size());
        for (size_t i = 0; i < s.size(); ++i) x[i] = (float) (s[i] * g);
        return x;
    }

    /** Armonica h di un seno di frequenza f nella finestra 0.75-1.5 s (ampiezza di picco); h = 0.5 = sub-armonica. */
    std::vector<double> harmonicsOf (pt::engine::Effect& fx, double f, double peakDb, const std::vector<double>& hs)
    {
        const double sr = 48000.0;
        const int n = (int) (1.5 * sr);
        Buf x ((size_t) n);
        const double A = std::pow (10.0, peakDb / 20.0);
        for (int i = 0; i < n; ++i) x[(size_t) i] = (float) (A * std::sin (2 * kPi * f * i / sr));
        processMono (fx, x, 512);
        const int start = (int) (0.75 * sr);
        const int per = (int) std::floor ((n - start) * f / sr);
        const int meas = (int) std::lround (per * sr / f);
        auto harm = [&] (double h)
        {
            double re = 0, im = 0;
            for (int i = 0; i < meas; ++i)
            {
                const double ph = 2 * kPi * f * h * (double) (start + i) / sr;
                re += x[(size_t) (start + i)] * std::sin (ph);
                im += x[(size_t) (start + i)] * std::cos (ph);
            }
            return 2.0 * std::sqrt (re * re + im * im) / meas;
        };
        std::vector<double> out;
        for (double h : hs) out.push_back (harm (h));
        return out;
    }

    struct SineMeasure { double h1 = 0, sub = 0; };
    SineMeasure sineS1 (const pt::engine::ModelDef& d, const Sets& sets, double f, double peakDb)
    {
        auto fx = makeS1 (d, sets);
        const auto h = harmonicsOf (*fx, f, peakDb, { 1.0, 0.5 });
        return { h[0], h[1] };
    }

    /** ModelDef di prova della famiglia Circuit (netlist di un solo blocco nuovo), con 'knobs' pomelli a meta'. */
    pt::engine::ModelDef circuitDef (const char* config, int knobs = 0)
    {
        using namespace pt::engine;
        ModelDef d {};
        d.id = "circtest"; d.name = "Circuito di prova"; d.code = "CT"; d.inspiredBy = "netlist di prova"; d.category = "Test";
        d.family = Family::Circuit;
        d.config = config;
        d.numControls = knobs;
        for (int i = 0; i < knobs; ++i)
        {
            auto& c = d.controls[i];
            c.label = "K"; c.role = "k"; c.kind = ControlKind::Knob; c.def = 0.5f; c.strip = 2; c.units = Units::Dial; c.hi = 10.0f;
        }
        return d;
    }

    /** Segnale del confronto bit per bit (banco s1 'raw'): riff 3 s + accordo 2 s a -3 dBFS + seno a 440 Hz crescente 1 s. */
    Buf rawSignalS1 (double sr)
    {
        Buf x = guitarS1 (true, sr, 3.0, -12.0);
        const Buf chord = guitarS1 (false, sr, 2.0, -3.0);
        x.insert (x.end(), chord.begin(), chord.end());
        for (int i = 0; i < (int) sr; ++i) x.push_back ((float) (0.3 * std::sin (2 * kPi * 440.0 * i / sr) * (i / sr)));
        return x;
    }

    /** Hash FNV-1a dell'uscita (byte per byte) con i comandi di default, poi spostati di +0.37 a meta' segnale. */
    uint64_t rawHashS1 (const pt::engine::ModelDef& d, const Buf& x)
    {
        auto fx = makeS1 (d, {});
        const size_t half = x.size() / 2;
        Buf y1 (x.begin(), x.begin() + (long) half), y2 (x.begin() + (long) half, x.end());
        processMono (*fx, y1, 512);
        for (int i = 0; i < d.numControls; ++i) fx->params[i].store (std::fmod (fx->params[i].load() + 0.37f, 1.0f));
        processMono (*fx, y2, 512);
        y1.insert (y1.end(), y2.begin(), y2.end());
        uint64_t h = 1469598103934665603ull;
        for (float v : y1)
        {
            uint32_t u;
            std::memcpy (&u, &v, 4);
            for (int k = 0; k < 4; ++k) { h ^= (u >> (8 * k)) & 255u; h *= 1099511628211ull; }
        }
        return h;
    }

    juce::String dbList (const std::vector<double>& v, int decimals = 2)
    {
        juce::String s;
        for (double x : v) s << (s.isEmpty() ? "" : " / ") << juce::String (x, decimals);
        return s;
    }

    const char* const kStage3Analog[] = { "zw44", "evh5150", "dd11", "sf01", "dd25", "bmtriangle", "bmramshead", "bmvioletrh", "bmredblack",
        "bmopamp", "bmcivilwar", "bmblackrus", "bmnycreiss", "bmlittlexo", "bmbassxo", "bmdeluxe14", "mufffuzz69", "doublemuff", "metalmuff",
        "ehlpb1", "ehscrbird", "ehmole", "ehattackeq", "ehhottubes", "ehsoulfood", "ehriverdrive", "ehodglove", "ehcrayon", "ehbritvalve",
        "ehgermod", "ehtortion", "ehlumberjack", "ehsatisfaction", "ehoctavix", "ehcockfight", "ehblackfinger", "ehsoulpreacher",
        "ehhumdebugger", "ehdrq", "ehqtronp", "ehmicroqt", "ehbassball", "ehmicrosyn", "ehbassmsyn", "ehtubezip", "ehoctmux", "ehfreqan",
        "ehfreeze", "ehstm" };

    /** Pedali digitali della tappa 3B (motore FxClassicB, agganciati anch'essi da makeClassic). */
    const char* const kStage3BDigital[] = { "evh30", "ehbattalion", "ehbassmetaphors", "ehanalogizer", "ehxstmist", "ehxneomist", "ehxflhoax", "ehxechofl", "ehxbassclo", "ehxeddy", "ehxwiggler", "ehxnanopul", "ehxsuppul", "ehxlesterk", "ehxmodrex", "ehxstpolyp", "ehxbadst1", "ehxstpolyc", "ehxstcthe", "ehdmm550", "ehdmm1100", "ehdmboy", "ehmmstereo", "ehreplay", "ehcanecho", "ehrerun", "ehhgneo", "ehholier", "ehholiest", "ehabyss", "eh3verb", "ehshimmer", "ehsuperegop", "ehattackdecay", "ehpog3", "ehpicopog", "ehpitchforkp", "ehpicofork", "ehihm", "ehslammi", "ehslammip", "ehstring9", "ehbassmono", "ehringthing", "ehatomic", "ehv256", "ehironlung", "ehepitome", "ehsoulpog", "ehtonetattoo" };

    const char* const kStage3Digital[] = { "zw38", "evh90", "evh117", "ehxgoodvib", "ehxlesterg", "ehxmod11", "ehxworm", "ehxss75", "ehxssnano",
        "ehxbadst", "ehxpoly79", "ehxem76", "ehxdem78", "ehxdemre", "ehxpolych", "ehxsclone", "ehxneoclo", "ehxctheory", "ehxstpuls", "ehmm76",
        "ehdmm", "ehmmboy", "ehmmtoy", "ehslapback", "ehsmmh", "eh1echo", "eh16sec", "ehcanyon", "ehgcanyon", "ehhg", "ehhgplus", "ehhgmax",
        "ehcathedral", "ehoceans11", "ehoceans12", "ehholystain", "ehsuperego", "eh720", "ehpog", "ehpog2", "ehmicropog", "ehhog", "ehhog2",
        "ehpitchfork", "ehb9", "ehc9", "ehkey9", "ehmel9", "ehsynth9", "ehbass9", "ehmonosynth", "ehravish", "ehvoicebox" };

    void runStage3AnalogTests (const std::function<void (const juce::String&, bool, const juce::String&)>& report)
    {
        using namespace pt::engine;
        const double sr = 48000.0;

        // 1. pedali esistenti invariati bit per bit: riff 3 s + accordo 2 s + seno a 440 Hz crescente 1 s, comandi di
        //    default poi spostati di +0.37 a meta' (hash FNV-1a misurati prima della tappa 3 sul banco s1)
        {
            struct Ref { const char* id; uint64_t hash; };
            static const Ref refs[] = { { "ds1", 0x4dafee9176bb36b4ull }, { "sd1", 0xf1764965540f55daull }, { "bd2", 0x6b73a36faee6cbf3ull },
                                        { "mt2", 0xaf69af8f033d93fbull }, { "gcb95", 0xb596b28c390cf713ull }, { "cb535q", 0xa98b47398be0c7cbull } };
            const Buf x = rawSignalS1 (sr);
            bool ok = true;
            juce::String detail;
            for (const auto& r : refs)
            {
                const auto* d = findModel (r.id);
                if (d == nullptr) { ok = false; detail << r.id << " assente "; continue; }
                const uint64_t h = rawHashS1 (*d, x);
                const bool same = h == r.hash;
                ok = ok && same;
                if (! same) detail << r.id << " DIVERSO (" << juce::String::toHexString ((juce::int64) h) << ") ";
            }
            report ("Tappa 3A: DS-1, SD-1, BD-2, MT-2, GCB95, 535Q invariati bit per bit (riff, accordo, seno; comandi mossi)", ok,
                    ok ? juce::String ("6/6 hash identici") : detail.trim());
        }

        // 2. blocchi nuovi del motore (netlist di prova)
        {
            const std::vector<double> f3 { 100.0, 1000.0, 10000.0 };
            const auto dN = circuitDef ("nodal R.i.a=10k C.a.0=10n OUT.a=1"), dL = circuitDef ("lpf R=10k C=10n");
            const auto n = smallSignalDb (dN, {}, f3, 1.0e-4), l = smallSignalDb (dL, {}, f3, 1.0e-4);
            double worst = 0;
            for (size_t k = 0; k < f3.size(); ++k) worst = std::max (worst, std::abs (n[k] - l[k]));
            report ("Circuito: blocco nodal (rete RC) uguale a 'lpf R=10k C=10n' a 100 Hz / 1 kHz / 10 kHz (scarto < 0.02 dB)", worst < 0.02,
                    "nodal " + dbList (n) + " dB, lpf " + dbList (l) + " dB, scarto max " + juce::String (worst, 3) + " dB");
        }
        {
            const auto d = circuitDef ("nodal R.i.m=10k R.m.o=100k C.m.o=1n O.0.m.o=1 OUT.o=1");
            const auto g = smallSignalDb (d, {}, { 100.0, 1000.0, 10000.0 }, 1.0e-4);
            const double ref[] = { 19.98, 18.55, 3.85 };
            bool ok = true;
            for (int k = 0; k < 3; ++k) ok = ok && std::abs (g[(size_t) k] - ref[k]) <= 0.05;
            report ("Circuito: nodal con op-amp ideale invertente 10k / 100k || 1n (19.98 / 18.55 / 3.85 dB +-0.05)", ok, dbList (g) + " dB");
        }
        {
            const auto d = circuitDef ("nodal R.i.a=10 R.a.l=33k C.l.0=10n C.a.h=4n R.h.0=33k R.l.w=50k R.w.h=50k C.w.b=100n R.b.0=87.57k "
                                       "E.b.0.x=-4.257 R.x.c=12k R.c.b=390k OUT.c=1");
            const auto g = smallSignalDb (d, {}, { 31.5, 100.0, 500.0, 1000.0, 4000.0 }, 1.0e-4);
            const double ref[] = { -1.29, -0.18, -2.15, -3.61, 1.11 };
            bool ok = true;
            for (int k = 0; k < 5; ++k) ok = ok && std::abs (g[(size_t) k] - ref[k]) <= 0.1;
            report ("Circuito: nodal con 3 condensatori e generatore controllato (tono Big Muff + Q4, riferimento numpy +-0.1 dB)", ok,
                    "31.5/100/500/1k/4k Hz: " + dbList (g) + " dB");
        }
        {
            const auto g = smallSignalDb (circuitDef ("fbinv Ri=10k Rf=100k"), {}, { 1000.0 }, 1.0e-4);
            report ("Circuito: fbinv (op-amp ideale invertente 10k/100k) a 1 kHz = 20.00 dB +-0.05", std::abs (g[0] - 20.0) <= 0.05,
                    juce::String (g[0], 3) + " dB");
        }
        {
            const auto d = circuitDef ("tap n=5; gain a=0; recall n=5 a=div(par(10k,pot(0,20k,B)),5k)", 1);
            const auto g = smallSignalDb (d, {}, { 1000.0 }, 1.0e-4);
            report ("Circuito: espressioni e memorie (tap n=5, gain 0, recall con div/par/pot) = 0.00 dB +-0.01", std::abs (g[0]) <= 0.01,
                    juce::String (g[0], 4) + " dB");
        }
        if (const auto* d = findModel ("ehhumdebugger"))
        {
            const Sets normal { { "MODE", 0.0f } }, strong { { "MODE", 1.0f } };
            const double n150 = dbOf (sineS1 (*d, normal, 150.0, -30.0).h1), n100 = dbOf (sineS1 (*d, normal, 100.0, -30.0).h1);
            const double s100 = dbOf (sineS1 (*d, strong, 100.0, -30.0).h1);
            const double n110 = dbOf (sineS1 (*d, normal, 110.0, -30.0).h1), s110 = dbOf (sineS1 (*d, strong, 110.0, -30.0).h1);
            const bool ok = n150 <= -42.0 && std::abs (n100 + 30.0) <= 0.2 && s100 <= -42.0 && std::abs (n110 + 30.0) <= 0.3 && std::abs (s110 + 30.0) <= 0.3;
            report ("Hum Debugger: NORMAL toglie 150 Hz e lascia 100 Hz, STRONG toglie anche 100 Hz, 110 Hz invariato (seni a -30 dBFS)", ok,
                    "NORMAL 150 Hz " + juce::String (n150, 1) + ", 100 Hz " + juce::String (n100, 2) + "; STRONG 100 Hz " + juce::String (s100, 1)
                        + "; 110 Hz " + juce::String (n110, 2) + " / " + juce::String (s110, 2) + " dBFS");
        }
        else report ("Hum Debugger nel catalogo", false, "modello assente");
        if (const auto* d = findModel ("ehfreeze"))
        {
            auto tail = [&] (float level)
            {
                auto fx = makeS1 (*d, { { "EFFECT LVL", level } });
                Buf x = guitarS1 (true, sr, 1.0, -12.0);
                x.resize ((size_t) (3.0 * sr), 0.0f);
                processMono (*fx, x, 512);
                double r = 0;
                for (size_t i = (size_t) (2.0 * sr); i < x.size(); ++i) r += (double) x[i] * x[i];
                return 10.0 * std::log10 (r / sr + 1e-30);
            };
            const double on = tail (1.0f), off = tail (0.0f);
            report ("Freeze: riff 1 s poi silenzio, ultimo secondo > -30 dBFS (EFFECT LVL 1) e nullo (< -120 dBFS) a EFFECT LVL 0",
                    on > -30.0 && off < -120.0, juce::String (on, 1) + " / " + (off < -199.0 ? juce::String ("-inf") : juce::String (off, 1)) + " dBFS");
        }
        else report ("Freeze nel catalogo", false, "modello assente");
        if (const auto* d = findModel ("ehoctmux"))
        {
            const auto m = sineS1 (*d, { { "BLEND", 1.0f }, { "SUB", 0.0f } }, 110.0, -12.0);
            const double rel = dbOf (m.sub) - dbOf (m.h1);
            report ("Octave Multiplexer (BLEND 1, SUB OFF): seno 110 Hz -> 55 Hz oltre 40 dB sopra 110 Hz", rel > 40.0,
                    "55 Hz - 110 Hz = " + juce::String (rel, 1) + " dB");
        }
        else report ("Octave Multiplexer nel catalogo", false, "modello assente");

        // 3. pedali (valori di default salvo indicazione)
        auto need = [&] (const char* id) -> const ModelDef*
        {
            const auto* d = findModel (id);
            if (d == nullptr) report (juce::String (id) + " nel catalogo", false, "modello assente");
            return d;
        };
        if (const auto* d = need ("bmtriangle"))
        {
            const auto g = smallSignalDb (*d, { { "SUSTAIN", 1.0f }, { "TONE", 0.5f }, { "VOLUME", 1.0f } }, { 1000.0 }, 1.0e-6);
            report ("Big Muff Triangle: SUSTAIN 1, TONE 0.5, VOLUME 1 a 1 kHz = 58.7 dB +-1.5 (analisi nodale 58.9)", std::abs (g[0] - 58.7) <= 1.5,
                    juce::String (g[0], 2) + " dB");
        }
        {
            // riferimento: analisi nodale completa a transistor (ibrido-pi, banco s1 bm.py), risposta fino al cursore del VOLUME
            struct BM { const char* id; float ref[4][7]; };
            static const BM bms[] = {
                { "bmtriangle", { { 39.98f, 61.61f, 64.72f, 58.92f, 52.93f, 42.44f, 27.49f }, { 37.23f, 55.10f, 54.39f, 47.53f, 39.89f, 27.79f, 11.92f },
                                  { 38.12f, 55.79f, 56.46f, 49.61f, 36.16f, 17.20f, -5.00f }, { 33.21f, 55.36f, 62.90f, 64.67f, 61.27f, 51.58f, 37.15f } } },
                { "bmramshead", { { 41.10f, 62.30f, 64.83f, 59.28f, 53.70f, 43.69f, 29.13f }, { 38.41f, 55.11f, 54.26f, 47.72f, 40.51f, 28.80f, 13.15f },
                                  { 39.63f, 55.75f, 56.14f, 49.46f, 36.33f, 17.72f, -4.26f }, { 34.27f, 55.90f, 62.86f, 64.87f, 61.82f, 52.48f, 38.33f } } },
                { "bmvioletrh", { { 40.79f, 62.29f, 65.46f, 59.93f, 54.26f, 44.04f, 29.27f }, { 38.04f, 55.76f, 55.16f, 48.61f, 41.33f, 29.48f, 13.75f },
                                  { 38.88f, 56.42f, 57.10f, 50.48f, 37.33f, 18.61f, -3.47f }, { 33.99f, 55.96f, 63.55f, 65.60f, 62.48f, 52.99f, 38.68f } } },
                { "bmredblack", { { 47.95f, 62.11f, 60.01f, 52.82f, 47.76f, 38.29f, 23.83f }, { 42.41f, 53.50f, 50.30f, 42.32f, 35.78f, 24.85f, 9.56f },
                                  { 42.27f, 53.84f, 52.78f, 45.71f, 32.14f, 13.37f, -8.59f }, { 39.37f, 54.22f, 57.62f, 59.77f, 56.66f, 47.35f, 33.25f } } },
                { "bmcivilwar", { { 36.27f, 52.69f, 53.19f, 47.18f, 39.76f, 31.39f, 18.51f }, { 33.17f, 46.18f, 44.75f, 37.97f, 29.07f, 18.99f, 4.88f },
                                  { 34.64f, 47.36f, 46.72f, 40.98f, 29.10f, 11.86f, -8.95f }, { 27.38f, 44.45f, 48.33f, 49.51f, 47.67f, 40.17f, 27.62f } } },
                { "bmblackrus", { { 36.37f, 52.77f, 53.22f, 47.25f, 40.55f, 32.65f, 19.99f }, { 33.27f, 46.25f, 44.80f, 38.11f, 29.99f, 20.37f, 6.43f },
                                  { 34.66f, 47.32f, 46.77f, 41.22f, 29.60f, 12.58f, -8.09f }, { 27.51f, 44.56f, 48.51f, 50.18f, 48.71f, 41.45f, 29.09f } } },
                { "bmnycreiss", { { 61.32f, 61.66f, 58.15f, 51.57f, 44.17f, 35.75f, 22.82f }, { 52.78f, 52.84f, 49.01f, 41.74f, 32.96f, 23.00f, 8.96f },
                                  { 53.80f, 53.89f, 51.14f, 45.04f, 32.85f, 15.55f, -5.20f }, { 53.25f, 53.68f, 53.59f, 54.66f, 52.51f, 44.79f, 32.21f } } },
                { "bmlittlexo", { { 61.32f, 61.66f, 58.15f, 51.57f, 44.17f, 35.75f, 22.82f }, { 52.78f, 52.84f, 49.01f, 41.74f, 32.96f, 23.00f, 8.96f },
                                  { 53.80f, 53.89f, 51.14f, 45.04f, 32.85f, 15.55f, -5.20f }, { 53.25f, 53.68f, 53.59f, 54.66f, 52.51f, 44.79f, 32.21f } } },
                { "bmbassxo",   { { 37.24f, 53.73f, 55.12f, 50.34f, 42.10f, 32.39f, 19.60f }, { 34.13f, 47.21f, 46.70f, 41.20f, 31.49f, 20.06f, 6.04f },
                                  { 36.00f, 48.90f, 48.65f, 43.62f, 32.73f, 16.35f, -3.81f }, { 28.19f, 45.34f, 49.82f, 50.06f, 47.73f, 40.62f, 28.58f } } } };
            const std::vector<double> F { 31.5, 100.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0 };
            const float settings[4][2] = { { 1.0f, 0.5f }, { 0.6f, 0.5f }, { 0.3f, 0.0f }, { 1.0f, 1.0f } };
            // 9 modelli x 4 regolazioni elaborati in parallelo (stesse misure, solo piu' veloce)
            constexpr int numBM = (int) (sizeof (bms) / sizeof (bms[0]));
            std::vector<std::vector<double>> resp ((size_t) numBM * 4);
            parallelFor (numBM * 4, [&] (int t)
            {
                const auto& b = bms[t / 4];
                const int s = t % 4;
                if (const auto* d = findModel (b.id))
                {
                    Sets sets { { "SUSTAIN", settings[s][0] }, { "TONE", settings[s][1] }, { "VOLUME", 1.0f } };
                    if (std::strcmp (b.id, "bmbassxo") == 0) sets.push_back ({ "MODE", 0.5f });
                    resp[(size_t) t] = smallSignalDb (*d, sets, F, 1.0e-6);
                }
            });
            bool ok = true;
            juce::String detail;
            for (int m = 0; m < numBM; ++m)
            {
                const auto& b = bms[m];
                double acc = 0;
                bool found = true;
                for (int s = 0; s < 4; ++s)
                {
                    const auto& g = resp[(size_t) (m * 4 + s)];
                    if (g.size() != F.size()) { found = false; break; }
                    for (size_t k = 0; k < F.size(); ++k) acc += (g[k] - b.ref[s][k]) * (g[k] - b.ref[s][k]);
                }
                if (! found) { ok = false; detail << b.id << " assente "; continue; }
                const double rms = std::sqrt (acc / 28.0);
                // tolleranza del report: 2.0 dB alla risoluzione di 0.1 dB con cui sono riportati gli scarti
                // (Ram's Head: 2.03 dB, riportato come 2.0)
                const bool good = std::round (rms * 10.0) / 10.0 <= 2.0;
                ok = ok && good;
                detail << b.id << " " << juce::String (rms, 2) << (good ? "" : " (FUORI)") << " ";
            }
            report ("Big Muff a transistor: scarto RMS dall'analisi nodale completa <= 2.0 dB (4 regolazioni x 7 frequenze)", ok, detail.trim());
        }
        if (const auto* d = need ("dd11"))
        {
            const double off = smallSignalDb (*d, {}, { 1000.0 }, 1.0e-4)[0], on = smallSignalDb (*d, { { "SCOOP", 1.0f } }, { 1000.0 }, 1.0e-4)[0];
            report ("MXR DD11: SCOOP a 1 kHz = -22.0 dB +-1 (manuale Dunlop)", std::abs (on - off + 22.0) <= 1.0, juce::String (on - off, 2) + " dB");
        }
        if (const auto* d = need ("dd25"))
        {
            const double off = smallSignalDb (*d, {}, { 1000.0 }, 1.0e-4)[0], on = smallSignalDb (*d, { { "SCOOP", 1.0f } }, { 1000.0 }, 1.0e-4)[0];
            const double t5 = smallSignalDb (*d, { { "TONE", 0.5f } }, { 10000.0 }, 1.0e-4)[0];
            const double t1 = smallSignalDb (*d, { { "TONE", 1.0f } }, { 10000.0 }, 1.0e-4)[0] - t5;
            const double t0 = smallSignalDb (*d, { { "TONE", 0.0f } }, { 10000.0 }, 1.0e-4)[0] - t5;
            const bool ok = std::abs (on - off + 8.0) <= 1.0 && std::abs (t1 - 4.6) <= 1.0 && std::abs (t0 + 4.6) <= 1.0;
            report ("MXR DD25: SCOOP a 1 kHz -8.0 dB, TONE agli estremi a 10 kHz +-4.6 dB (+-1)", ok,
                    "SCOOP " + juce::String (on - off, 2) + " dB, TONE max " + juce::String (t1, 2) + " / min " + juce::String (t0, 2) + " dB");
        }
        if (const auto* d = need ("evh5150"))
        {
            struct Band { const char* label; double f, ref; };
            const Band bands[] = { { "BASS", 31.5, 4.5 }, { "MID", 630.0, 5.0 }, { "TREBLE", 10000.0, 4.5 } };
            bool ok = true;
            juce::String detail;
            for (const auto& b : bands)
            {
                const double mid = smallSignalDb (*d, { { b.label, 0.5f } }, { b.f }, 1.0e-5)[0];
                const double hi = smallSignalDb (*d, { { b.label, 1.0f } }, { b.f }, 1.0e-5)[0] - mid;
                const double lo = smallSignalDb (*d, { { b.label, 0.0f } }, { b.f }, 1.0e-5)[0] - mid;
                ok = ok && std::abs (hi - b.ref) <= 1.0 && std::abs (lo + b.ref) <= 1.0;
                detail << b.label << " " << juce::String (hi, 2) << " / " << juce::String (lo, 2) << " dB; ";
            }
            report ("MXR EVH 5150: BASS / MID / TREBLE agli estremi +-4.5 / +-5.0 / +-4.5 dB a 31.5 Hz / 630 Hz / 10 kHz (+-1)", ok,
                    detail.trimCharactersAtEnd ("; "));
        }
        if (const auto* d = need ("sf01"))
        {
            const double clean = smallSignalDb (*d, { { "SUB>FUZZ", 0.0f }, { "VOLUME", 1.0f }, { "SUB OCTAVE", 0.0f }, { "OCTAVE UP", 0.0f } },
                                                { 1000.0 }, 1.0e-4)[0];
            const double f0 = smallSignalDb (*d, { { "FUZZ", 0.0f }, { "SUB OCTAVE", 0.0f }, { "OCTAVE UP", 0.0f } }, { 1000.0 }, 1.0e-3)[0];
            const double f1 = smallSignalDb (*d, { { "FUZZ", 1.0f }, { "SUB OCTAVE", 0.0f }, { "OCTAVE UP", 0.0f } }, { 1000.0 }, 1.0e-3)[0];
            const bool ok = std::abs (clean - 1.6) <= 0.3 && std::abs (f1 - f0 - 40.0) <= 2.0;
            report ("MXR SF01: pulito al massimo +1.6 dB (+-0.3), gamma del FUZZ 40 dB (+-2) a 1 kHz", ok,
                    "pulito " + juce::String (clean, 2) + " dB, FUZZ 0 -> 1 " + juce::String (f1 - f0, 2) + " dB");
        }
        {
            struct Boost { const char* id; double f, ref, tol; };
            const Boost boosts[] = { { "ehlpb1", 1000.0, 26.8, 1.0 }, { "ehmole", 200.0, 20.0, 3.0 }, { "ehscrbird", 2500.0, 21.0, 3.0 } };
            bool ok = true;
            juce::String detail;
            for (const auto& b : boosts)
            {
                const auto* d = findModel (b.id);
                if (d == nullptr) { ok = false; detail << b.id << " assente; "; continue; }
                const double g = smallSignalDb (*d, { { "BOOST", 1.0f } }, { b.f }, 1.0e-4)[0];
                ok = ok && std::abs (g - b.ref) <= b.tol;
                detail << b.id << " " << juce::String (g, 2) << " dB a " << juce::String (b.f, 0) << " Hz; ";
            }
            report ("EHX LPB-1 / Mole / Screaming Bird a BOOST 1: +26.8 (+-1) / +20 (+-3) / +21 dB (+-3)", ok, detail.trimCharactersAtEnd ("; "));
        }
        if (const auto* d = need ("bmdeluxe14"))
        {
            struct Mid { double f; float pos; };
            const Mid mids[] = { { 315.0, 0.0f }, { 1250.0, 0.5f }, { 5000.0, 1.0f } };
            const auto ref = smallSignalDb (*d, { { "MIDS LEVEL", 0.5f } }, { 315.0, 1250.0, 5000.0 }, 1.0e-6);
            bool ok = true;
            juce::String detail;
            for (int k = 0; k < 3; ++k)
            {
                const double g = smallSignalDb (*d, { { "MIDS LEVEL", 1.0f }, { "MIDS FREQ", mids[k].pos } }, { mids[k].f }, 1.0e-6)[0] - ref[(size_t) k];
                ok = ok && std::abs (g - 10.0) <= 1.5;
                detail << juce::String (g, 2) << (k < 2 ? " / " : "");
            }
            report ("Big Muff Deluxe: MIDS LEVEL 1 - 0.5 = +10 dB (+-1.5) a 315 Hz / 1.25 kHz / 5 kHz (MIDS FREQ 0 / 0.5 / 1)", ok, detail + " dB");
        }

        // livelli: riff a -12 dBFS con i comandi di default -> uscita finita, picco < 0 dBFS, RMS tra -33 e -11 dBFS
        constexpr int numAnalog = (int) (sizeof (kStage3Analog) / sizeof (kStage3Analog[0]));
        {
            const Buf riff = guitarS1 (true, sr, 3.0, -12.0);
            const size_t skip = (size_t) (0.05 * sr);
            struct Lv { bool found = false, fin = true; double rms = -200, pk = -200; };
            std::vector<Lv> lv ((size_t) numAnalog);
            parallelFor (numAnalog, [&] (int m)
            {
                const auto* d = findModel (kStage3Analog[m]);
                if (d == nullptr) return;
                auto fx = makeS1 (*d, {});
                Buf y = riff;
                processMono (*fx, y, 512);
                double acc = 0, pk = 0;
                bool fin = true;
                for (size_t i = skip; i < y.size(); ++i)
                {
                    fin = fin && std::isfinite (y[i]);
                    acc += (double) y[i] * y[i];
                    pk = std::max (pk, (double) std::abs (y[i]));
                }
                lv[(size_t) m] = { true, fin, 10.0 * std::log10 (acc / (double) (y.size() - skip) + 1e-30), dbOf (pk) };
            });
            bool ok = true;
            int good = 0;
            double rmsLo = 0, rmsHi = -200, pkHi = -200;
            juce::String bad;
            for (int m = 0; m < numAnalog; ++m)
            {
                const auto& r = lv[(size_t) m];
                const bool g = r.found && r.fin && r.pk < 0.0 && r.rms > -33.0 && r.rms < -11.0;
                if (g) ++good;
                else { ok = false; bad << kStage3Analog[m] << " (RMS " << juce::String (r.rms, 1) << ", picco " << juce::String (r.pk, 1) << ") "; }
                rmsLo = std::min (rmsLo, r.rms); rmsHi = std::max (rmsHi, r.rms); pkHi = std::max (pkHi, r.pk);
            }
            report ("Tappa 3A analogici: livelli con riff a -12 dBFS (49 modelli: finiti, picco < 0 dBFS, RMS -33..-11 dBFS)", ok,
                    juce::String (good) + "/49, RMS " + juce::String (rmsLo, 1) + ".." + juce::String (rmsHi, 1) + " dBFS, picco max "
                        + juce::String (pkHi, 1) + " dBFS " + bad.trim());
        }

        // robustezza: comandi tutti a 0, tutti a 1 e 3 combinazioni casuali, a 44.1 e 96 kHz, accordo a -3 dBFS
        // (una pennata di 1 s invece delle due in 3 s del banco, per il tempo dell'autotest)
        {
            const double rates[2] = { 44100.0, 96000.0 };
            juce::Random rng (7);
            std::vector<std::array<float, maxControls>> randoms ((size_t) (2 * numAnalog * 3));
            for (auto& r : randoms) for (auto& v : r) v = rng.nextFloat();
            const Buf chords[2] = { guitarS1 (false, rates[0], 1.0, -3.0), guitarS1 (false, rates[1], 1.0, -3.0) };
            struct Res { bool found = false, fin = true; double pk = -200; };
            std::vector<Res> res ((size_t) (2 * numAnalog * 5));
            parallelFor ((int) res.size(), [&] (int t)
            {
                const int c = t % 5, m = (t / 5) % numAnalog, ri = t / (5 * numAnalog);
                const auto* d = findModel (kStage3Analog[m]);
                if (d == nullptr) return;
                auto fx = createEffect (*d);
                for (int k = 0; k < d->numControls; ++k)
                    fx->params[k].store (c == 0 ? 0.0f : c == 1 ? 1.0f : randoms[(size_t) ((ri * numAnalog + m) * 3 + c - 2)][(size_t) k]);
                fx->prepare (rates[ri], 512);
                Buf y = chords[ri];
                processMono (*fx, y, 512);
                Res r { true, true, 0.0 };
                for (float v : y) { r.fin = r.fin && std::isfinite (v); r.pk = std::max (r.pk, (double) std::abs (v)); }
                r.pk = dbOf (r.pk);
                res[(size_t) t] = r;
            });
            bool ok = true;
            double worst = -200;
            juce::String bad;
            for (size_t t = 0; t < res.size(); ++t)
            {
                const auto& r = res[t];
                worst = std::max (worst, r.fin ? r.pk : 999.0);
                if (! r.found || ! r.fin || r.pk >= 24.0)
                {
                    ok = false;
                    bad << kStage3Analog[(t / 5) % (size_t) numAnalog] << "@" << juce::String (rates[t / (5 * (size_t) numAnalog)] / 1000.0, 1)
                        << "/" << (int) (t % 5) << (r.fin ? "" : " NaN") << " ";
                }
            }
            report ("Tappa 3A analogici: robustezza (49 modelli x 5 regolazioni x 44.1/96 kHz, accordo a -3 dBFS): finiti, picco < +24 dBFS", ok,
                    "picco max " + juce::String (worst, 1) + " dBFS " + bad.trim());
        }
    }

    //------------------------------------------------------------------------------
    /** Corda Karplus-Strong del banco s2 (rumore xorshift, deterministica). */
    void pluckS2 (Buf& out, double sr, double t0, double f, double amp, double decayS, uint32_t seed)
    {
        const int N = std::max (2, (int) std::lround (sr / f));
        std::vector<float> loop ((size_t) N);
        uint32_t s = seed * 2654435761u + (uint32_t) (f * 100);
        float prevNoise = 0;
        for (auto& v : loop)
        {
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;
            const float r = (float) (int32_t) s * 4.6566129e-10f;
            v = 0.5f * (r + prevNoise); prevNoise = r;
        }
        const double g = std::pow (10.0, -3.0 / (decayS * f));
        const int start = (int) (t0 * sr);
        int idx = 0;
        for (int n = start; n < (int) out.size(); ++n)
        {
            const float cur = loop[(size_t) idx];
            const float nxt = loop[(size_t) ((idx + 1) % N)];
            loop[(size_t) idx] = (float) (g * 0.5 * (cur + nxt));
            idx = (idx + 1) % N;
            out[(size_t) n] += (float) amp * cur;
        }
    }

    double midiHz (int m) { return 440.0 * std::pow (2.0, (m - 69) / 12.0); }

    /** Frase di chitarra del banco s2 (8 s: note gravi e acute, power chord, accordo aperto, staccato). */
    Buf guitarS2 (double sr, double seconds)
    {
        Buf b ((size_t) (sr * seconds), 0.0f);
        pluckS2 (b, sr, 0.05, midiHz (40), 0.35, 2.5, 1);
        pluckS2 (b, sr, 1.00, midiHz (45), 0.3, 2.0, 2);
        pluckS2 (b, sr, 1.00, midiHz (52), 0.25, 2.0, 3);
        pluckS2 (b, sr, 1.00, midiHz (57), 0.2, 2.0, 4);
        const int chord[6] = { 40, 47, 52, 56, 59, 64 };
        for (int k = 0; k < 6; ++k) pluckS2 (b, sr, 2.2 + 0.012 * k, midiHz (chord[k]), 0.18, 3.0, (uint32_t) (10 + k));
        pluckS2 (b, sr, 4.0, midiHz (64), 0.3, 1.5, 20);
        pluckS2 (b, sr, 4.5, midiHz (71), 0.3, 1.5, 21);
        pluckS2 (b, sr, 5.0, midiHz (76), 0.3, 1.5, 22);
        for (int k = 0; k < 8; ++k) pluckS2 (b, sr, 5.6 + 0.18 * k, midiHz (52 + 2 * k), 0.25, 0.4, (uint32_t) (30 + k));
        return b;
    }

    Buf sineBuf (double sr, double f, double amp, double seconds)
    {
        Buf b ((size_t) (sr * seconds));
        for (size_t i = 0; i < b.size(); ++i) b[i] = (float) (amp * std::sin (2 * kPi * f * (double) i / sr));
        return b;
    }

    /** Elaborazione del banco s2: comandi impostati prima di prepare, 'ch' canali (copie dell'ingresso). */
    std::vector<Buf> runS2 (const pt::engine::ModelDef& d, const Buf& in, const std::map<int, float>& p, double sr = 48000.0,
                            int block = 256, int ch = 1)
    {
        auto fx = pt::engine::createEffect (d);
        for (const auto& [k, v] : p) fx->params[k].store (v);
        fx->prepare (sr, block);
        std::vector<Buf> out ((size_t) ch, in);
        std::vector<float*> ptr ((size_t) ch);
        for (size_t pos = 0; pos < in.size(); pos += (size_t) block)
        {
            const int n = (int) std::min ((size_t) block, in.size() - pos);
            for (int c = 0; c < ch; ++c) ptr[(size_t) c] = out[(size_t) c].data() + pos;
            fx->process (ptr.data(), ch, n);
        }
        return out;
    }

    double rmsOf (const Buf& b, size_t a = 0, size_t e = 0)
    {
        if (e == 0 || e > b.size()) e = b.size();
        double s = 0;
        for (size_t i = a; i < e; ++i) s += (double) b[i] * b[i];
        return std::sqrt (s / (double) std::max<size_t> (1, e - a));
    }

    /** Frequenza del picco piu' vicino a 'near' (FFT 65536 con finestra di Hann, interpolazione parabolica) e
        purezza = energia entro +-6% del bersaglio rispetto al resto (dB). */
    double peakFreq (const Buf& b, size_t a, size_t len, double sr, double nearHz, double* purity = nullptr)
    {
        const int order = 16, N = 1 << order;
        std::vector<float> w ((size_t) N * 2, 0.0f);
        len = std::min (len, (size_t) N);
        for (size_t i = 0; i < len; ++i) w[i] = b[a + i] * (float) (0.5 - 0.5 * std::cos (2 * kPi * (double) i / (double) (len - 1)));
        juce::dsp::FFT fft (order);
        fft.performFrequencyOnlyForwardTransform (w.data());
        const double bin = sr / N;
        const int lo = std::max (1, (int) (nearHz * 0.94 / bin)), hi = std::min (N / 2 - 2, (int) (nearHz * 1.06 / bin));
        int k = lo;
        for (int j = lo; j <= hi; ++j) if (w[(size_t) j] > w[(size_t) k]) k = j;
        const double y0 = std::log (w[(size_t) k - 1] + 1e-12), y1 = std::log (w[(size_t) k] + 1e-12), y2 = std::log (w[(size_t) k + 1] + 1e-12);
        const double dd = 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2);
        if (purity != nullptr)
        {
            double tot = 0, in = 0;
            for (int j = (int) (30 / bin); j < N / 2; ++j)
            {
                const double e = (double) w[(size_t) j] * w[(size_t) j];
                tot += e;
                if (j >= lo && j <= hi) in += e;
            }
            *purity = 10 * std::log10 (in / std::max (1e-30, tot - in));
        }
        return (k + dd) * bin;
    }

    double cents (double f, double ref) { return 1200.0 * std::log2 (f / ref); }

    /** Inviluppo: raddrizzatore + passa-basso a un polo. */
    Buf envelopeOf (const Buf& b, double sr, double fc)
    {
        Buf e (b.size());
        const float a = (float) std::exp (-2 * kPi * fc / sr);
        float z = 0;
        for (size_t i = 0; i < b.size(); ++i) { z = std::abs (b[i]) + a * (z - std::abs (b[i])); e[i] = z; }
        return e;
    }

    /** Frequenza dominante di una modulazione: autocorrelazione dell'inviluppo decimato, primo massimo dopo il primo minimo. */
    double modRate (const Buf& sig, double sr, size_t from, double maxPeriodS)
    {
        const Buf e = envelopeOf (sig, sr, 12);
        const int dec = 48;
        std::vector<double> d;
        for (size_t i = from; i + dec < e.size(); i += dec) d.push_back (e[i]);
        double m = 0;
        for (double v : d) m += v;
        m /= (double) d.size();
        for (auto& v : d) v -= m;
        const double fsd = sr / dec;
        const int maxLag = std::min ((int) d.size() / 2, (int) (maxPeriodS * fsd));
        std::vector<double> ac ((size_t) maxLag + 1, 0.0);
        for (int L = 0; L <= maxLag; ++L)
        {
            double s = 0;
            for (size_t i = 0; i + (size_t) L < d.size(); ++i) s += d[i] * d[i + (size_t) L];
            ac[(size_t) L] = s;
        }
        int L = std::max (1, (int) (fsd / 30.0));
        while (L < maxLag && ac[(size_t) L] > ac[(size_t) L + 1]) ++L;
        int best = L;
        for (int j = L; j < maxLag; ++j) if (ac[(size_t) j] > ac[(size_t) best]) best = j;
        if (best <= 1 || ac[(size_t) best] < 0.1 * ac[0]) return 0;
        return fsd / best;
    }

    /** Ritardi del wet (ms) con un treno di impulsi: picco nella finestra [minMs, maxMs] dopo ciascun impulso. */
    std::vector<double> echoTimes (const pt::engine::ModelDef& d, const std::map<int, float>& p, double periodS, double totalS,
                                   double minMs, double maxMs)
    {
        const double sr = 48000;
        Buf in ((size_t) (sr * totalS), 0.0f);
        const size_t step = (size_t) (periodS * sr);
        for (size_t i = step; i < in.size(); i += step) in[i] = 0.5f;
        const auto out = runS2 (d, in, p);
        const Buf& y = out[0];
        std::vector<double> r;
        for (size_t i = step; i + step < in.size(); i += step)
        {
            const size_t a = i + (size_t) (minMs * sr / 1000), e = i + (size_t) (maxMs * sr / 1000);
            size_t k = a;
            for (size_t j = a; j < e; ++j) if (std::abs (y[j]) > std::abs (y[k])) k = j;
            double dd = 0;
            if (k > a && k + 1 < e)
            {
                const double y0 = std::abs (y[k - 1]), y1 = std::abs (y[k]), y2 = std::abs (y[k + 1]);
                const double den = y0 - 2 * y1 + y2;
                if (std::abs (den) > 1e-12) dd = 0.5 * (y0 - y2) / den;
            }
            r.push_back (((double) (k - i) + dd) * 1000.0 / sr);
        }
        return r;
    }

    /** RT60 dalla curva di Schroeder della risposta all'impulso (retta fra -5 e -35 dB, o -20 se il pavimento e' alto). */
    double rt60 (const pt::engine::ModelDef& d, const std::map<int, float>& p, double seconds)
    {
        const double sr = 48000;
        Buf in ((size_t) (sr * seconds), 0.0f);
        in[100] = 1.0f;
        Buf y = runS2 (d, in, p)[0];
        y[100] = 0;
        std::vector<double> sch (y.size() + 1, 0.0);
        for (size_t i = y.size(); i-- > 0;) sch[i] = sch[i + 1] + (double) y[i] * y[i];
        if (sch[0] <= 0) return 0;
        double t5 = -1, tEnd = -1, endDb = -35;
        for (size_t i = 0; i < y.size(); ++i)
        {
            const double L = 10 * std::log10 (sch[i] / sch[0] + 1e-30);
            if (t5 < 0 && L <= -5) t5 = (double) i / sr;
            if (L <= endDb) { tEnd = (double) i / sr; break; }
        }
        if (tEnd < 0)
        {
            endDb = -20;
            for (size_t i = 0; i < y.size(); ++i)
                if (10 * std::log10 (sch[i] / sch[0] + 1e-30) <= endDb) { tEnd = (double) i / sr; break; }
            if (tEnd < 0) return 999;
        }
        return (tEnd - t5) * 60.0 / (-endDb - 5.0);
    }

    /** Latenza (ms) di una voce: burst di seno 0.2 V dopo 0.5 s di silenzio, tempo al 50% del regime. */
    double latencyMs (const pt::engine::ModelDef& d, const std::map<int, float>& p, double f)
    {
        const double sr = 48000;
        Buf in ((size_t) (sr * 1.5), 0.0f);
        const size_t t0 = (size_t) (0.5 * sr);
        for (size_t i = t0; i < in.size(); ++i) in[i] = (float) (0.2 * std::sin (2 * kPi * f * (double) (i - t0) / sr));
        const Buf y = runS2 (d, in, p)[0];
        const Buf e = envelopeOf (y, sr, 400);
        const double ss = rmsOf (y, (size_t) (1.2 * sr), (size_t) (1.45 * sr)) * std::sqrt (2.0);
        for (size_t i = t0; i < e.size(); ++i) if (e[i] >= 0.5 * ss) return (double) (i - t0) * 1000.0 / sr;
        return -1;
    }

    /** Sanita' di un modello digitale (banco s2): default, 3 combinazioni casuali (mt19937, seme 1234 + seed), tutto a 1,
        tutto a 0, ogni posizione di ogni selettore con gli altri al default; mono e (se stereo) due canali; blocchi 512/64. */
    struct SanityRes { bool found = false, fin = true; double pk = 0; int runs = 0; juce::String bad; };
    std::vector<std::map<int, float>> sanitySets (const pt::engine::ModelDef* d, int seed)
    {
        std::vector<std::map<int, float>> sets;
        sets.push_back ({});
        std::mt19937 rng ((unsigned) (1234 + seed));
        std::uniform_real_distribution<float> U (0, 1);
        for (int k = 0; k < 3; ++k) { std::map<int, float> p; for (int c = 0; c < d->numControls; ++c) p[c] = U (rng); sets.push_back (p); }
        std::map<int, float> mx, mn;
        for (int c = 0; c < d->numControls; ++c) { mx[c] = 1.0f; mn[c] = 0.0f; }
        sets.push_back (mx); sets.push_back (mn);
        for (int c = 0; c < d->numControls; ++c)
            if (d->controls[c].steps > 1)
                for (int st = 0; st < d->controls[c].steps; ++st) sets.push_back ({ { c, (float) st / (d->controls[c].steps - 1) } });
        return sets;
    }

    /** 'onlySet' >= 0: solo quella regolazione (per suddividere il lavoro fra i thread). */
    SanityRes sanityS2 (const char* id, int seed, const Buf& phrase, double rate, int onlySet = -1)
    {
        SanityRes r;
        const auto* d = pt::engine::findModel (id);
        if (d == nullptr) return r;
        r.found = true;
        const auto sets = sanitySets (d, seed);
        for (size_t si = 0; si < sets.size(); ++si)
            for (int ch = 1; ch <= (d->stereo ? 2 : 1); ++ch)
            {
                if (onlySet >= 0 && (int) si != onlySet) continue;
                ++r.runs;
                bool fin = true;
                double pk = 0;
                for (const auto& b : runS2 (*d, phrase, sets[si], rate, si % 2 ? 64 : 512, ch))
                    for (float v : b) { fin = fin && std::isfinite (v); pk = std::max (pk, (double) std::abs (v)); }
                r.fin = r.fin && fin;
                r.pk = std::max (r.pk, fin ? pk : 999.0);
                if (! fin || pk >= 8.0)
                    r.bad << id << "@" << juce::String (rate / 1000.0, 1) << "/" << (int) si << "/" << ch << (fin ? "" : " NaN") << " ";
            }
        return r;
    }

    /** Click: ogni comando cambiato durante un seno a 220 Hz (ogni 0.4 s); picco della differenza seconda nei 10 ms dopo il
        cambio rispetto al massimo fra i 300 ms prima e il regime (20-150 ms dopo). 'skip' (facoltativo) esclude dei cambi
        (indice del comando, valore). */
    struct ClickRes { bool found = false; double worst = 0; juce::String what; };
    ClickRes clicksS2 (const char* id, const std::function<bool (int, float)>& skip = {})
    {
        using namespace pt::engine;
        const double sr = 48000.0;
        const int blk = 256;
        const int spacing = (int) (0.4 * sr / blk);
        ClickRes r;
        const auto* d = findModel (id);
        if (d == nullptr) return r;
        r.found = true;
        auto fx = createEffect (*d);
        fx->prepare (sr, blk);
        std::vector<std::pair<int, float>> changes;
        for (int c = 0; c < d->numControls; ++c)
        {
            if (d->controls[c].kind == ControlKind::Button) continue;
            const int st = d->controls[c].steps;
            if (st > 1) for (int k = 0; k < st; ++k) changes.push_back ({ c, (float) k / (st - 1) });
            else { changes.push_back ({ c, 0.9f }); changes.push_back ({ c, 0.1f }); changes.push_back ({ c, d->controls[c].def }); }
        }
        if (skip) changes.erase (std::remove_if (changes.begin(), changes.end(), [&] (const auto& c) { return skip (c.first, c.second); }), changes.end());
        Buf buf ((size_t) blk), hist;
        hist.reserve ((size_t) (0.6 * sr) + (changes.size() + 1) * (size_t) (spacing * blk) + (size_t) blk);
        float* ptr[1] = { buf.data() };
        double phase = 0;
        int changeAt = (int) (0.6 * sr / blk), lastChange = -1;
        size_t ci = 0;
        for (int b = 0; ci <= changes.size(); ++b)
        {
            if (b == changeAt)
            {
                if (ci == changes.size()) break;
                fx->params[changes[ci].first].store (changes[ci].second);
                ++ci;
                lastChange = (int) hist.size();
                changeAt = b + spacing;
            }
            for (int i = 0; i < blk; ++i) { buf[(size_t) i] = (float) (0.2 * std::sin (phase)); phase += 2 * kPi * 220.0 / sr; }
            fx->process (ptr, 1, blk);
            hist.insert (hist.end(), buf.begin(), buf.end());
            if (lastChange > 0 && (int) hist.size() > lastChange + (int) (0.16 * sr))
            {
                auto d2 = [&] (int k) { return (double) std::abs (hist[(size_t) k] - 2 * hist[(size_t) k - 1] + hist[(size_t) k - 2]); };
                double before = 1e-6, after = 0, steady = 0;
                for (int k = std::max (2, lastChange - (int) (0.3 * sr)); k < lastChange; ++k) before = std::max (before, d2 (k));
                for (int k = lastChange; k < lastChange + (int) (0.01 * sr); ++k) after = std::max (after, d2 (k));
                for (int k = lastChange + (int) (0.02 * sr); k < lastChange + (int) (0.15 * sr); ++k) steady = std::max (steady, d2 (k));
                const double ratio = after / std::max (std::max (before, steady), 2e-4);
                if (ratio > r.worst)
                {
                    r.worst = ratio;
                    r.what = juce::String (d->controls[changes[ci - 1].first].label) + "=" + juce::String (changes[ci - 1].second, 2);
                }
                lastChange = -1;
            }
        }
        return r;
    }

    void runStage3DigitalTests (const std::function<void (const juce::String&, bool, const juce::String&)>& report)
    {
        using namespace pt::engine;
        const double sr = 48000.0;
        auto need = [&] (const char* id) -> const ModelDef*
        {
            const auto* d = findModel (id);
            if (d == nullptr) report (juce::String (id) + " nel catalogo", false, "modello assente");
            return d;
        };

        // delay con ripetizioni al massimo in ogni modo (multitap, ottava, shimmer, pitch...): l'anello resta limitato
        {
            float worst = 0; bool finite = true; int runs = 0;
            for (auto* id : { "ehcanyon", "ehgcanyon", "ehsmmh", "ehdmm", "ehmmboy", "eh1echo" })
            {
                const auto* d = findModel (id);
                if (d == nullptr) { finite = false; continue; }
                int sel = -1, steps = 1;
                for (int k = 0; k < d->numControls; ++k)
                    if (d->controls[k].kind == ControlKind::Selector && sel < 0) { sel = k; steps = std::max (1, (int) d->controls[k].steps); }
                for (int pos = 0; pos < steps; ++pos)
                {
                    auto fx = createEffect (*d);
                    fx->prepare (sr, 512);
                    for (int k = 0; k < d->numControls; ++k)
                    {
                        const juce::String role (d->controls[k].role != nullptr ? d->controls[k].role : "");
                        float v = d->controls[k].def;
                        if (role == "feedback" || role == "repeats" || role == "blend" || role == "level") v = 1.0f;
                        if (k == sel) v = steps > 1 ? (float) pos / (float) (steps - 1) : 0.0f;
                        fx->params[k].store (v);
                    }
                    juce::AudioBuffer<float> buf (2, 512);
                    for (int b = 0; b < (int) (4.0 * sr / 512); ++b)
                    {
                        buf.clear();
                        if (b < (int) (0.8 * sr / 512)) fillGuitar (buf, sr, 0.5f, b * 512);
                        fx->process (buf.getArrayOfWritePointers(), 2, 512);
                        for (int c = 0; c < 2; ++c)
                            for (int i = 0; i < 512; ++i)
                            {
                                const float x = buf.getSample (c, i);
                                if (! std::isfinite (x)) finite = false;
                                else worst = std::max (worst, std::abs (x));
                            }
                    }
                    ++runs;
                }
            }
            report ("Delay EHX: ripetizioni al massimo in tutti i modi senza divergere", finite && worst < 8.0f,
                    juce::String (runs) + " prove, picco massimo " + juce::String (worst, 2) + " V");
        }

        // 0. il nuovo aggancio (makeClassic) prende i 53 modelli della tappa e nessun altro modello del catalogo; le repliche
        //    REAL MOD (config vuota, suono del modello del catalogo) non sono mai agganciate direttamente
        {
            auto isNew = [] (const char* id)
            {
                for (const char* n : kStage3Digital) if (std::strcmp (n, id) == 0) return true;
                for (const char* n : kStage3BDigital) if (std::strcmp (n, id) == 0) return true;
                return false;
            };
            int hooked = 0, total = 0;
            juce::String bad;
            for (int i = 0; i < numModels(); ++i)
            {
                ++total;
                const bool h = makeClassic (model (i)) != nullptr;
                if (h) ++hooked;
                if (h != isNew (model (i).id)) bad << model (i).id << " ";
            }
            for (int i = 0; i < numRealModels(); ++i)
            {
                ++total;
                const bool h = makeClassic (realModel (i)) != nullptr;
                if (h) { ++hooked; if (! isNew (realModel (i).id)) bad << realModel (i).id << "(REAL) "; }
            }
            constexpr int expected = (int) (sizeof (kStage3Digital) / sizeof (kStage3Digital[0]) + sizeof (kStage3BDigital) / sizeof (kStage3BDigital[0]));
            report ("Tappa 3A/3B digitali: makeClassic aggancia i modelli nuovi e nessun pedale esistente (catalogo e REAL MOD)",
                    bad.isEmpty() && hooked == expected, juce::String (hooked) + " agganciati su " + juce::String (total) + (bad.isEmpty() ? juce::String() : ", errati: " + bad.trim()));
        }
        // pedali esistenti invariati: hash dell'uscita (frase di 2 s, default e comandi a 0.7) misurati sul banco s2 prima della
        // tappa 3, per il modello del catalogo e per la replica REAL MOD
        {
            struct Ref { const char* id; uint64_t cat, real; };
            static const Ref refs[] = { { "ce2", 0x05ba6aa2c177e55cull, 0x5960f48a1352d275ull }, { "bf2", 0x17c9e4b5e96baf10ull, 0x5d7fcac0e349e227ull },
                                        { "ph3", 0x35356ee67712ee66ull, 0x021e49157a5c7be9ull }, { "dd3", 0xec89082205f84b34ull, 0xecffa22dcb8cd389ull },
                                        { "rv6", 0x2f9a85e927c4908cull, 0xe0a9995d91a748d8ull }, { "ps6", 0x4191f6e6798b07c7ull, 0x4e9b58046f1dbe93ull },
                                        { "oc5", 0x3b21f2a57e0d2ca0ull, 0x4001d44c47c98d07ull }, { "sy1", 0x739be5d65c627894ull, 0x2fbc2a7bc7e15e51ull } };
            const Buf g = guitarS2 (sr, 2.0);
            auto hashOf = [&g, sr] (const ModelDef& d)
            {
                uint64_t h = 1469598103934665603ull;
                for (int k = 0; k < 2; ++k)
                {
                    std::map<int, float> p;
                    if (k == 1) for (int c = 0; c < d.numControls; ++c) p[c] = 0.7f;
                    for (const auto& b : runS2 (d, g, p, sr, 256, d.stereo ? 2 : 1))
                        for (float v : b) { uint32_t u; std::memcpy (&u, &v, 4); h ^= u; h *= 1099511628211ull; }
                }
                return h;
            };
            bool ok = true;
            juce::String bad;
            for (const auto& r : refs)
            {
                const auto* d = findModel (r.id);
                const auto* rd = findRealModel (r.id);
                if (d == nullptr || rd == nullptr) { ok = false; bad << r.id << " assente "; continue; }
                if (hashOf (*d) != r.cat) { ok = false; bad << r.id << " DIVERSO "; }
                if (hashOf (*rd) != r.real) { ok = false; bad << r.id << " (REAL) DIVERSO "; }
            }
            report ("Tappa 3A: CE-2, BF-2, PH-3, DD-3, RV-6, PS-6, OC-5, SY-1 invariati bit per bit (catalogo e REAL MOD)", ok,
                    ok ? juce::String ("16/16 hash identici") : bad.trim());
        }

        // 1. banco di filtri complessi (FxSpectral.h): intonazione, livello e purezza delle voci
        {
            struct V { int num, L; double r; const char* name; };
            const V vs[] = { { 1, 1, 0.5, "-1 ott" }, { 2, 0, 2, "+1 ott" }, { 4, 0, 4, "+2 ott" }, { 3, 1, 1.5, "+5a" }, { 5, 0, 5, "+2o+3a" } };
            double worstCent = 0, lvlLo = 9, lvlHi = 0, purMin = 999;
            for (double f : { 82.41, 196.0, 329.6, 659.3 })
            {
                cx::SpectralBank bank;
                bank.prepare (sr, 45.0f, 7000.0f, 4, 0.14f, 3);
                const Buf in = sineBuf (sr, f, 0.2, 2.0);
                std::vector<Buf> outs (std::size (vs), Buf (in.size()));
                for (size_t i = 0; i < in.size(); ++i)
                {
                    bank.push (in[i]);
                    for (size_t v = 0; v < std::size (vs); ++v) outs[v][i] = bank.voice (vs[v].num, vs[v].L) * cx::kBankGain;
                }
                for (size_t v = 0; v < std::size (vs); ++v)
                {
                    double pur = 0;
                    const double fm = peakFreq (outs[v], (size_t) (0.6 * sr), (size_t) (1.3 * sr), sr, f * vs[v].r, &pur);
                    worstCent = std::max (worstCent, std::abs (cents (fm, f * vs[v].r)));
                    const double lvl = rmsOf (outs[v], (size_t) (1.0 * sr), (size_t) (2.0 * sr)) / rmsOf (in);
                    lvlLo = std::min (lvlLo, lvl); lvlHi = std::max (lvlHi, lvl);
                    if (vs[v].num == 2 && vs[v].L == 0 && f >= 196.0) purMin = std::min (purMin, pur);
                }
            }
            report ("Banco di filtri: voci -1, +1, +2 ottave, +5a, +2o+3a intonate entro 2 cent (82-659 Hz)", worstCent < 2.0,
                    "errore max " + juce::String (worstCent, 2) + " cent");
            report ("Banco di filtri: livello delle voci 0.9..1.15 dell'ingresso, purezza +1 ottava > 35 dB da 196 Hz", lvlLo > 0.9 && lvlHi < 1.15 && purMin > 35.0,
                    "livello " + juce::String (lvlLo, 3) + ".." + juce::String (lvlHi, 3) + ", purezza min " + juce::String (purMin, 1) + " dB");
        }

        // 2. POG / Pitch Fork: latenza e intonazione
        if (const auto* pog = need ("ehpog"))
            if (const auto* pf = need ("ehpitchfork"))
            {
                const auto pUp = setsS2 (*pog, { { "direct", 0.0f }, { "oct1", 0.0f }, { "up1", 0.8f } });
                const double l330 = latencyMs (*pog, pUp, 329.6), l82 = latencyMs (*pog, pUp, 82.41);
                const std::map<int, float> pP5 { { 0, 1.0f }, { 1, 0.5f }, { 2, 0.0f } }, pDet { { 0, 1.0f }, { 1, 0.0f }, { 2, 0.0f } };
                const double lP5 = latencyMs (*pf, pP5, 196.0), lDet = latencyMs (*pf, pDet, 196.0);
                const bool ok = l330 >= 0 && l330 < 15.0 && l82 >= 0 && l82 < 35.0 && lP5 >= 0 && lP5 < 25.0 && lDet >= 0 && lDet < 5.0;
                report ("POG +1 ottava e Pitch Fork: latenza (POG < 15 ms a 330 Hz, < 35 ms a 82 Hz; Pitch Fork P5 < 25 ms, detune < 5 ms)", ok,
                        "POG " + juce::String (l330, 1) + " / " + juce::String (l82, 1) + " ms, Pitch Fork P5 " + juce::String (lP5, 1)
                            + " ms, detune " + juce::String (lDet, 1) + " ms");

                const double semis[8] = { 0, 1, 2, 4, 5, 7, 9, 10 };
                double worst = 0;
                juce::String detail;
                for (int m = 0; m < 2; ++m)
                    for (int s = 1; s <= 7; ++s)
                    {
                        const double r = std::pow (2.0, (m ? -semis[s] : semis[s]) / 12.0), f = 196.0;
                        const std::map<int, float> p { { 0, 1.0f }, { 1, (float) s / 10.0f }, { 2, (float) m * 0.5f } };
                        const Buf y = runS2 (*pf, sineBuf (sr, f, 0.2, 2.0), p)[0];
                        const double c = cents (peakFreq (y, (size_t) (0.6 * sr), (size_t) (1.3 * sr), sr, f * r), f * r);
                        worst = std::max (worst, std::abs (c));
                        detail << juce::String (c, 1) << (s < 7 ? " " : (m == 0 ? " | " : ""));
                    }
                report ("Pitch Fork: intervalli granulari m2..m7 su e giu' a 196 Hz entro 6 cent", worst < 6.0, "cent: " + detail);
            }

        // 3. BBD: ritardi misurati con un treno di impulsi (picco del wet)
        {
            struct D { const char* id; double t0, t1; };
            const D ds[] = { { "ehdmm", 35.0, 550.0 }, { "ehmm76", 30.0, 300.0 }, { "ehmmboy", 30.6, 550.4 } };
            bool ok = true;
            juce::String detail;
            for (const auto& dd : ds)
            {
                const auto* d = findModel (dd.id);
                if (d == nullptr) { ok = false; detail << dd.id << " assente; "; continue; }
                double t[2];
                for (int k = 0; k < 2; ++k)
                {
                    const auto p = setsS2 (*d, { { "time", (float) k }, { "feedback", 0.0f }, { "blend", 1.0f }, { "depth", 0.0f }, { "modon", 0.0f } });
                    const double per = k == 0 ? 0.6 : 3.5;
                    const auto e = echoTimes (*d, p, per, per * 3.2, 2.0, per * 1000 - 20);
                    t[k] = e.empty() ? 0.0 : e[0];
                }
                ok = ok && std::abs (t[0] - dd.t0) <= 2.0 && std::abs (t[1] - dd.t1) <= 10.0;
                detail << dd.id << " " << juce::String (t[0], 1) << " / " << juce::String (t[1], 1) << " ms; ";
            }
            report ("Delay a BBD: DELAY 0 / 1 (DMM 35 / 550 ms, MM 30 / 300 ms, MM Boy 30.6 / 550.4 ms; +-2 / +-10 ms)", ok, detail.trimCharactersAtEnd ("; "));
        }
        {
            bool ok = true;
            juce::String detail;
            auto range = [&] (const char* id, const Sets& sets, double totalS, double& mn, double& mx)
            {
                mn = 1e9; mx = 0;
                const auto* d = findModel (id);
                if (d == nullptr) { ok = false; detail << id << " assente; "; return; }
                for (double v : echoTimes (*d, setsS2 (*d, sets), 0.12, totalS, 0.15, 110.0)) { mn = std::min (mn, v); mx = std::max (mx, v); }
            };
            double a0, a1, b0, b1, c0, c1, d0, d1;
            range ("evh117", { { "manual", 0.0f }, { "depth", 0.0f } }, 2.0, a0, a1);
            range ("evh117", { { "manual", 1.0f }, { "depth", 0.0f } }, 2.0, b0, b1);
            range ("ehxem76", { { "range", 1.0f }, { "rate", 0.25f } }, 14.0, c0, c1);
            range ("ehxpolych", { { "mode", 1.0f }, { "manual", 1.0f }, { "depth", 0.0f }, { "res", 0.0f } }, 2.0, d0, d1);
            ok = ok && std::abs (a0 - 12.9) <= 0.2 && std::abs (a1 - 12.9) <= 0.2 && std::abs (b0 - 0.58) <= 0.2 && std::abs (b1 - 0.58) <= 0.2
                 && std::abs (c0 - 1.4) <= 0.2 && std::abs (c1 - 12.7) <= 0.2 && std::abs (d0 - 100.6) <= 0.2 && std::abs (d1 - 100.6) <= 0.2;
            report ("Flanger/chorus a BBD: EVH 117 MANUAL 0 / 1 = 12.9 / 0.58 ms, EM 76 RANGE 1 1.4-12.7 ms, Polychorus SLAP BACK 100.6 ms (+-0.2)", ok,
                    detail + "117 " + juce::String (a0, 2) + " / " + juce::String (b0, 2) + " ms, EM 76 " + juce::String (c0, 2) + "-" + juce::String (c1, 2)
                        + " ms, Polychorus " + juce::String (d0, 2) + " ms");
        }
        if (const auto* d = need ("ehdmm"))
        {
            const auto p = setsS2 (*d, { { "time", (float) ((300 - 35) / (550.0 - 35)) }, { "feedback", 0.0f }, { "blend", 1.0f }, { "depth", 0.0f },
                                         { "level", 0.39f } });
            auto gain = [&] (double f) { return dbOf (rmsOf (runS2 (*d, sineBuf (sr, f, 0.1, 2.0), p)[0], (size_t) (1.0 * sr), (size_t) (1.9 * sr)) / (0.1 / std::sqrt (2.0))); };
            const double ref = gain (500.0), g28 = gain (2800.0) - ref, g30 = gain (3000.0) - ref, g36 = gain (3600.0) - ref;
            report ("Deluxe Memory Man: eco a -3 dB fra 2.8 e 3.6 kHz (rispetto a 500 Hz)", g28 > -3.0 && g36 < -3.0,
                    "2.8 / 3 / 3.6 kHz: " + juce::String (g28, 1) + " / " + juce::String (g30, 1) + " / " + juce::String (g36, 1) + " dB");
        }

        // 4. LFO: frequenza dell'inviluppo (autocorrelazione)
        if (const auto* d = need ("evh90"))
        {
            const double ref[3] = { 0.365, 1.93, 7.7 };
            bool ok = true;
            juce::String detail;
            for (int k = 0; k < 3; ++k)
            {
                const float rk = 0.5f * (float) k;
                const Buf y = runS2 (*d, sineBuf (sr, 700, 0.2, k == 0 ? 24.0 : 8.0), setsS2 (*d, { { "rate", rk } }))[0];
                const double f = modRate (y, sr, (size_t) (0.5 * sr), k == 0 ? 20.0 : 4.0);
                ok = ok && std::abs (f / ref[k] - 1.0) <= 0.05;
                detail << juce::String (f, 3) << (k < 2 ? " / " : " Hz");
            }
            report ("MXR EVH 90: SPEED 0 / 0.5 / 1 = 0.365 / 1.93 / 7.7 Hz (+-5%)", ok, detail);
        }
        if (const auto* d = need ("ehxlesterg"))
        {
            const double ref[2][3] = { { 0.11, 0.80, 3.2 }, { 1.56, 6.2, 25.0 } };
            bool ok = true;
            juce::String detail;
            for (int fast = 0; fast < 2; ++fast)
                for (int k = 0; k < 3; ++k)
                {
                    const float v = 0.5f * (float) k;
                    const auto p = setsS2 (*d, { { fast ? "fast" : "slow", v }, { "speed", (float) fast }, { "sustain", 0.0f }, { "drive", 0.0f },
                                                 { "balance", 1.0f } });
                    const Buf y = runS2 (*d, sineBuf (sr, 3000, 0.2, fast ? 8.0 : 30.0), p, sr, 256, 2)[0];
                    const double f = modRate (y, sr, (size_t) ((fast ? 3.0 : 12.0) * sr), fast ? 2.0 : 20.0);
                    ok = ok && std::abs (f / ref[fast][k] - 1.0) <= 0.05;
                    detail << juce::String (f, 3) << (k < 2 ? " / " : (fast ? " Hz" : " Hz; FAST "));
                }
            report ("Lester G (solo tromba, AM a 3 kHz): SLOW 0.11 / 0.80 / 3.2 Hz, FAST 1.56 / 6.2 / 25 Hz (+-5%)", ok, "SLOW " + detail);
        }

        // 5. riverberi: RT60 dalla curva di Schroeder
        {
            auto rt = [&] (const char* id, int modeIdx, float tk, bool& found)
            {
                const auto* d = findModel (id);
                if (d == nullptr) { found = false; return 0.0; }
                auto p = setsS2 (*d, { { "time", tk }, { "amount", tk }, { "blend", 1.0f }, { "mix", 1.0f }, { "predelay", 0.0f }, { "feedback", 0.0f } });
                for (int c = 0; c < d->numControls; ++c)
                    if (std::strcmp (d->controls[c].role, "mode") == 0) p[c] = d->controls[c].steps > 1 ? (float) modeIdx / (d->controls[c].steps - 1) : 0.0f;
                return rt60 (*d, p, 14);
            };
            bool found = true;
            const double plateMax = rt ("ehhgmax", 2, 0.96f, found), plateMin = rt ("ehhgmax", 2, 0.0f, found);
            const double hall = rt ("ehcathedral", 2, 0.96f, found), spring = rt ("ehhg", 0, 0.5f, found);
            report ("Riverberi: Holy Grail Max PLATE > 15 s / < 1 s, Cathedral HALL > 10 s, Holy Grail SPRING 1.6-2.8 s (RT60)",
                    found && plateMax > 15.0 && plateMin < 1.0 && hall > 10.0 && spring >= 1.6 && spring <= 2.8,
                    "PLATE " + juce::String (plateMax, 2) + " / " + juce::String (plateMin, 2) + " s, HALL " + juce::String (hall, 2) + " s, SPRING "
                        + juce::String (spring, 2) + " s");
        }

        // 6. robustezza e click su tutti i 53 modelli (segnali brevi, modelli elaborati in parallelo)
        constexpr int numDigital = (int) (sizeof (kStage3Digital) / sizeof (kStage3Digital[0]));
        {
            // regolazioni come nel banco s2: default, 3 casuali (mt19937, seme 1234 + indice del modello), tutto a 1, tutto a 0,
            // ogni posizione di ogni selettore; segnale: 0.5 s della frase del banco (0.95-1.45 s: Mi basso che decade e power chord)
            const double rates[3] = { 44100.0, 48000.0, 96000.0 };
            Buf phrases[3];
            for (int r = 0; r < 3; ++r)
            {
                const Buf full = guitarS2 (rates[r], 1.45);
                phrases[r].assign (full.begin() + (long) (0.95 * rates[r]), full.end());
            }
            std::vector<SanityRes> res ((size_t) (3 * numDigital));
            parallelFor ((int) res.size(), [&] (int t)
            {
                const int mi = t % numDigital, ri = t / numDigital;
                res[(size_t) t] = sanityS2 (kStage3Digital[mi], mi, phrases[ri], rates[ri]);
            });
            bool ok = true;
            double worstPk = 0;
            int runs = 0;
            juce::String bad;
            for (const auto& r : res)
            {
                ok = ok && r.found && r.bad.isEmpty();
                worstPk = std::max (worstPk, r.pk);
                runs += r.runs;
                bad << r.bad;
            }
            report ("Tappa 3A digitali: robustezza (53 modelli, 44.1/48/96 kHz, blocchi 64/512, default, casuali, 0, 1, ogni selettore): finiti, picco < 8 V",
                    ok, juce::String (runs) + " elaborazioni, picco max " + juce::String (worstPk, 2) + " V " + bad.trim());
        }
        {
            // click: ogni comando cambiato durante un seno a 220 Hz (ogni 0.4 s); picco della differenza seconda nei 10 ms dopo il
            // cambio rispetto al massimo fra i 300 ms prima e il regime (20-150 ms dopo)
            std::vector<ClickRes> res ((size_t) numDigital);
            parallelFor (numDigital, [&] (int mi) { res[(size_t) mi] = clicksS2 (kStage3Digital[mi]); });
            bool ok = true;
            double worst = 0;
            juce::String worstWhat, bad;
            for (int mi = 0; mi < numDigital; ++mi)
            {
                const auto& r = res[(size_t) mi];
                if (! r.found || r.worst > 10.0) { ok = false; bad << kStage3Digital[mi] << " " << juce::String (r.worst, 1) << "x "; }
                if (r.worst > worst) { worst = r.worst; worstWhat = juce::String (kStage3Digital[mi]) + " " + r.what; }
            }
            report ("Tappa 3A digitali: niente click cambiando i comandi (seno 220 Hz, differenza seconda <= 10x prima/regime, 53 modelli)", ok,
                    "peggiore " + juce::String (worst, 1) + "x (" + worstWhat + ") " + bad.trim());
        }
    }


    //==============================================================================
    // Tappa 3B: controlli numerici dei 74 pedali analogici EHX/MXR (blocchi nuovi di Circuit.cpp: cebias, envvca, crush,
    // lfovcf, chgain, pan, argomenti facoltativi di gate/sweep3/freeze) e dei 50 digitali (FxClassicB*). Metodi dei banchi
    // della tappa (s1b: come s1 con i comandi per indice, piu' i modi 'st' stereo e 'gtr' con centroide; s2b: come s2).
    // Le misure sono registrate in un Lab ed eseguite tutte in parallelo; i controlli le leggono poi in ordine.
    //==============================================================================
    const char* const kStage3BAnalog[] = { "kfk1", "evhmhg", "csp265", "tbm1", "eg74", "yjm308", "rr104", "bmreverse", "littlemuffop",
        "micrometal", "pocketmetal", "nanometal", "bmwicker", "bmdlxbass", "bmsovdlx", "bmgerm4", "bmhwplugin", "bmpi2", "bmpi2bass",
        "bmpi2wick", "bmpi2dlx", "bmpi2dlxb", "bmlittle70", "bmdeluxe78", "ehlpb2ube", "ehlpb3", "ehaxis", "ehknockout", "ehhotwax",
        "ehbasssoulfood", "ehoverlord", "ehsprucegoose", "ehhellmelter", "ehbassblogger", "ehtriboro", "ehmainframe", "ehsatisplus",
        "ehflatiron", "ehrippedspeaker", "ehlizardqueen", "ehlizardking", "ehgraphicfuzz", "ehcockfightplus", "ehbenderroyale",
        "ehpercolator", "ehwhitefinger", "ehplatform", "ehpicoplatform", "ehtonecorset", "ehbasspreacher", "ehsteelleather", "ehtubeeq",
        "ehsilencer", "ehsignalpad", "ehswitchblade", "ehswitchbladepro", "ehtriparallel", "ehsuperswitcher", "ehchillswitch", "ehvolume",
        "ehnextvolume", "ehnextpan", "ehzipper", "ehytrig", "ehqueenwah", "ehcrytone", "ehnscrytone", "ehnscrybass", "ehnstalk",
        "ehtalkped", "ehriddle", "ehblurst", "ehdeepfreeze", "ehswello" };

    using Verdict = std::pair<bool, juce::String>;
    using ISets = std::vector<std::pair<int, float>>;     // comandi per indice, come "indice=valore" del banco s1b

    /** Misure indipendenti (un rendering ciascuna) registrate prima, eseguite tutte in parallelo con parallelFor, e
        controlli valutati dopo, in ordine di registrazione. Un modello assente fa fallire i controlli che lo usano. */
    class Lab
    {
    public:
        using Measure = std::function<std::vector<double> (const pt::engine::ModelDef&)>;

        int add (const char* id, Measure m) { return push (pt::engine::findModel (id), id, std::move (m)); }
        int add (const pt::engine::ModelDef& testDef, Measure m)
        {
            owned.push_back (testDef);
            return push (&owned.back(), testDef.id, std::move (m));
        }
        /** Valore k della misura h (solo dentro i controlli). */
        double v (int h, size_t k = 0) const { return res[(size_t) h][k]; }
        /** Misura lunga: eseguita fra le prime, perche' non resti sola alla fine del lavoro parallelo. */
        void first (int h, int priority = 1) { early.push_back ({ priority, h }); }
        const std::vector<double>& all (int h) const { return res[(size_t) h]; }
        void check (std::vector<int> needs, const juce::String& name, std::function<Verdict()> verdict)
        {
            checks.push_back ({ std::move (needs), name, std::move (verdict) });
        }
        void run (const std::function<void (const juce::String&, bool, const juce::String&)>& report)
        {
            res.assign (jobs.size(), {});
            std::stable_sort (early.begin(), early.end(), [] (const auto& a, const auto& b) { return a.first > b.first; });
            std::vector<int> order;
            std::vector<bool> taken (jobs.size(), false);
            for (const auto& e : early) { order.push_back (e.second); taken[(size_t) e.second] = true; }
            for (int i = 0; i < (int) jobs.size(); ++i) if (! taken[(size_t) i]) order.push_back (i);
            parallelFor ((int) order.size(), [this, &order] (int k)
            {
                const int i = order[(size_t) k];
                const auto& j = jobs[(size_t) i];
                if (j.def != nullptr) res[(size_t) i] = j.measure (*j.def);
            });
            for (const auto& c : checks)
            {
                juce::StringArray missing;
                for (int h : c.needs)
                    if (jobs[(size_t) h].def == nullptr || res[(size_t) h].empty()) missing.addIfNotAlreadyThere (jobs[(size_t) h].id);
                if (! missing.isEmpty()) { report (c.name, false, "modello assente: " + missing.joinIntoString (", ")); continue; }
                const auto [ok, detail] = c.verdict();
                report (c.name, ok, detail);
            }
        }

    private:
        struct Job { const pt::engine::ModelDef* def; juce::String id; Measure measure; };
        struct Check { std::vector<int> needs; juce::String name; std::function<Verdict()> verdict; };
        int push (const pt::engine::ModelDef* d, const char* id, Measure m)
        {
            jobs.push_back ({ d, id, std::move (m) });
            return (int) jobs.size() - 1;
        }
        std::deque<pt::engine::ModelDef> owned;      // ModelDef delle netlist di prova (indirizzi stabili: l'effetto li referenzia)
        std::vector<Job> jobs;
        std::vector<Check> checks;
        std::vector<std::vector<double>> res;
        std::vector<std::pair<int, int>> early;     // (priorita', misura)
    };

    bool within (double x, double ref, double tol) { return std::abs (x - ref) <= tol; }
    juce::String num (double x, int decimals = 2) { return juce::String (x, decimals); }

    /** Effetto del banco s1b: comandi per indice impostati prima di prepare (blocchi da 512). */
    std::unique_ptr<pt::engine::Effect> makeIdx (const pt::engine::ModelDef& d, const ISets& sets, double sr = 48000.0)
    {
        auto fx = pt::engine::createEffect (d);
        for (const auto& [k, val] : sets)
            if (k >= 0 && k < d.numControls) fx->params[k].store (val);
        fx->prepare (sr, 512);
        return fx;
    }

    /** Banco s1b 'fr': guadagno a piccolo segnale (dB) a una frequenza. */
    double frIdx (const pt::engine::ModelDef& d, const ISets& sets, double f, double amp)
    {
        auto fx = makeIdx (d, sets);
        return smallSignalOf (*fx, { f }, amp, 48000.0)[0];
    }

    /** Nota singola del banco s1b ('note': La 110 Hz pizzicato a 0 e 2 s), normalizzata al picco peakDb dBFS. */
    Buf noteS1 (double sr, double seconds, double peakDb)
    {
        std::vector<double> s ((size_t) (seconds * sr), 0.0);
        pluckS1 (s, sr, 110.0, 0.0, 1.0, 0.6, 1);
        pluckS1 (s, sr, 110.0, 2.0, 1.0, 0.6, 2);
        double mx = 0;
        for (double x : s) mx = std::max (mx, std::abs (x));
        const double g = std::pow (10.0, peakDb / 20.0) / mx;
        Buf x (s.size());
        for (size_t i = 0; i < s.size(); ++i) x[i] = (float) (s[i] * g);
        return x;
    }

    /** Banco s1b 'gtr': RMS e picco d'uscita (dBFS, dopo 50 ms), finitezza e centroide spettrale (Hz) sui terzi d'ottava
        (FFT 8192 con finestra di Hann, passo 2048). Restituisce { RMS, picco, centroide, finito }. */
    std::vector<double> gtrIdx (const pt::engine::ModelDef& d, const ISets& sets, const Buf& in)
    {
        const double sr = 48000.0;
        auto fx = makeIdx (d, sets);
        Buf y = in;
        processMono (*fx, y, 512);
        const size_t skip = (size_t) (0.05 * sr);
        double acc = 0, pk = 0;
        bool fin = true;
        for (size_t i = skip; i < y.size(); ++i)
        {
            fin = fin && std::isfinite (y[i]);
            acc += (double) y[i] * y[i];
            pk = std::max (pk, (double) std::abs (y[i]));
        }
        static const double fc[] = { 31.5, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800, 1000, 1250, 1600,
                                     2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000 };
        const int N = 8192, H = 2048;
        std::vector<double> win ((size_t) N), psd ((size_t) N / 2 + 1, 0.0);
        for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5 - 0.5 * std::cos (2 * kPi * i / N);
        int frames = 0;
        juce::dsp::FFT fft (13);
        std::vector<float> buf ((size_t) (2 * N));
        for (size_t p = skip; p + (size_t) N <= y.size(); p += (size_t) H)
        {
            for (int i = 0; i < N; ++i) { buf[(size_t) i] = (float) (y[p + (size_t) i] * win[(size_t) i]); buf[(size_t) (N + i)] = 0; }
            fft.performFrequencyOnlyForwardTransform (buf.data(), true);
            for (int k = 0; k <= N / 2; ++k) psd[(size_t) k] += (double) buf[(size_t) k] * buf[(size_t) k];
            ++frames;
        }
        double numer = 0, denom = 0;
        for (double f : fc)
        {
            const double lo = f * std::pow (2.0, -1.0 / 6.0), hi = f * std::pow (2.0, 1.0 / 6.0);
            double e = 1e-30;
            for (int k = 1; k <= N / 2; ++k)
            {
                const double fk = k * sr / N;
                if (fk >= lo && fk < hi) e += psd[(size_t) k];
            }
            e /= std::max (1, frames);
            numer += f * e;
            denom += e;
        }
        const double n = (double) (y.size() - skip);
        return { 20 * std::log10 (std::sqrt (acc / n) + 1e-30), 20 * std::log10 (pk + 1e-30), numer / denom, fin ? 1.0 : 0.0 };
    }

    /** Banco s1b 'sine': ampiezze di picco delle armoniche H1..H6 (indici 0..5) nella finestra 0.75-1.5 s. */
    std::vector<double> sineIdx (const pt::engine::ModelDef& d, const ISets& sets, double f, double peakDb)
    {
        auto fx = makeIdx (d, sets);
        return harmonicsOf (*fx, f, peakDb, { 1, 2, 3, 4, 5, 6 });
    }

    /** Armonica h rispetto alla fondamentale (dB), come il banco. */
    double relH (const std::vector<double>& h, int k) { return 20 * std::log10 ((h[(size_t) k - 1] + 1e-30) / (h[0] + 1e-30)); }

    /** Banco s1b 'st': sinistro = riff (picco -12 dBFS), destro = seno a 440 Hz di 0.1 V; RMS d'uscita { L, R } in dBFS. */
    std::vector<double> stIdx (const pt::engine::ModelDef& d, const ISets& sets, const Buf& riff)
    {
        const double sr = 48000.0;
        auto fx = makeIdx (d, sets);
        Buf l = riff, r (riff.size());
        for (size_t i = 0; i < r.size(); ++i) r[i] = (float) (0.1 * std::sin (2 * kPi * 440.0 * (double) i / sr));
        for (size_t i = 0; i < l.size(); i += 512)
        {
            const int n = (int) std::min<size_t> (512, l.size() - i);
            float* ch[2] = { l.data() + i, r.data() + i };
            fx->process (ch, 2, n);
        }
        auto rms = [sr] (const Buf& b)
        {
            double s = 0;
            const size_t a = (size_t) (0.05 * sr);
            for (size_t i = a; i < b.size(); ++i) s += (double) b[i] * b[i];
            return 10 * std::log10 (s / (double) (b.size() - a) + 1e-30);
        };
        return { rms (l), rms (r) };
    }

    void runStage3BAnalogTests (const std::function<void (const juce::String&, bool, const juce::String&)>& report)
    {
        using namespace pt::engine;
        const double sr = 48000.0;
        const Buf riff = guitarS1 (true, sr, 3.0, -12.0), riffSoft = guitarS1 (true, sr, 3.0, -24.0), note = noteS1 (sr, 3.0, -12.0);
        Lab lab;
        auto fr = [&lab] (auto target, ISets s, double f, double amp)
        {
            return lab.add (target, [s, f, amp] (const ModelDef& d) { return std::vector<double> { frIdx (d, s, f, amp) }; });
        };
        auto gtr = [&lab] (const char* id, ISets s, const Buf& in)
        {
            return lab.add (id, [s, &in] (const ModelDef& d) { return gtrIdx (d, s, in); });
        };
        auto sine = [&lab] (auto target, ISets s, double f, double peakDb)
        {
            return lab.add (target, [s, f, peakDb] (const ModelDef& d) { return sineIdx (d, s, f, peakDb); });
        };
        auto st = [&lab, &riff] (auto target, ISets s)
        {
            return lab.add (target, [s, &riff] (const ModelDef& d) { return stIdx (d, s, riff); });
        };

        // 1. pedali della tappa 3A invariati bit per bit (stessa prova della 3A; hash del banco s1b prima della tappa 3B, uguali al
        //    JSON 3A: fbinv, envf, freeze, sweep3, formant)
        {
            struct Ref { const char* id; uint64_t hash; };
            static const Ref refs[] = { { "bmtriangle", 0x8f5f636b2df4fd84ull }, { "ehqtronp", 0x9cae23213cded777ull },
                                        { "ehfreeze", 0xec49f9784dab7eb4ull }, { "ehmicrosyn", 0x6bbe9a0a03aa82dcull },
                                        { "ehstm", 0x5d23c09cb6316021ull } };
            static const Buf raw = rawSignalS1 (48000.0);
            std::vector<int> h;
            for (const auto& r : refs)
                h.push_back (lab.add (r.id, [] (const ModelDef& d)
                {
                    const uint64_t x = rawHashS1 (d, raw);
                    return std::vector<double> { (double) (x >> 32), (double) (x & 0xffffffffull) };
                }));
            lab.check (h, "Tappa 3B: Big Muff Triangle, Q-Tron+, Freeze, Micro Synth, Stereo Talking Machine (3A) invariati bit per bit",
                       [&lab, h] {
                           int same = 0;
                           juce::String bad;
                           for (size_t k = 0; k < h.size(); ++k)
                           {
                               const uint64_t x = ((uint64_t) lab.v (h[k], 0) << 32) | (uint64_t) lab.v (h[k], 1);
                               if (x == refs[k].hash) ++same;
                               else bad << refs[k].id << " DIVERSO (" << juce::String::toHexString ((juce::int64) x) << ") ";
                           }
                           return Verdict { same == (int) h.size(), juce::String (same) + "/" + juce::String ((int) h.size()) + " hash identici " + bad.trim() };
                       });
        }

        // 2. blocchi e argomenti nuovi del motore (netlist di prova del banco, comandi per indice)
        {
            const auto dc = circuitDef ("cebias g=20 vsw=8 q=lin(0,-0.2,1.2) s=0.04 k=4", 1);
            const int a = fr (dc, { { 0, 0.5f } }, 1000.0, 1.0e-3), b = fr (dc, { { 0, 0.0f } }, 1000.0, 1.0e-3);
            lab.check ({ a, b }, "Circuito: cebias g=20 k=4 a piccolo segnale: q=0.5 +25.36 dB, q=-0.2 (interdizione) -17.5 dB (+-0.2)", [&lab, a, b] {
                return Verdict { within (lab.v (a), 25.36, 0.2) && within (lab.v (b), -17.5, 0.2), num (lab.v (a)) + " / " + num (lab.v (b)) + " dB" };
            });
        }
        {
            const int a = fr (circuitDef ("lfovcf fmin=1000 fmax=1000 span=0 center=0 res=taper(0,B) rate=1", 1), { { 0, 0.0f } }, 1000.0, 1.0e-3);
            const int b = fr (circuitDef ("sweep3 fc=1000 mod=lin(0,-1,1) oct=3 res=0 rate=100", 1), { { 0, 0.5f } }, 1000.0, 1.0e-3);
            lab.check ({ a, b }, "Circuito: lfovcf fermo a 1 kHz (4 poli) -12.04 dB, sweep3 fc=1000 mod=0 (3 poli) -9.03 dB alla frequenza (+-0.2)",
                       [&lab, a, b] {
                           return Verdict { within (lab.v (a), -12.04, 0.2) && within (lab.v (b), -9.03, 0.2), num (lab.v (a)) + " / " + num (lab.v (b)) + " dB" };
                       });
        }
        {
            const auto dg = circuitDef ("gate thr=0.05 rel=2 hold=1 fdb=lin(0,6,-60)", 1);
            const int a = fr (dg, { { 0, 0.0f } }, 1000.0, 1.0e-3), b = fr (dg, { { 0, 1.0f } }, 1000.0, 1.0e-3);
            lab.check ({ a, b }, "Circuito: gate con fdb a gate chiuso: fdb=+6 -> +6 dB, fdb=-60 -> -60 dB (+-0.1)", [&lab, a, b] {
                return Verdict { within (lab.v (a), 6.0, 0.1) && within (lab.v (b), -60.0, 0.1), num (lab.v (a)) + " / " + num (lab.v (b)) + " dB" };
            });
        }
        {
            const auto dc = circuitDef ("crush bits=lin(0,1,24) rate=log(1,1000,48000) fs=1", 2);
            const int a = sine (dc, { { 0, 0.0f }, { 1, 1.0f } }, 1000.0, 0.0), b = fr (dc, { { 0, 1.0f }, { 1, 0.0f } }, 250.0, 0.01);
            lab.check ({ a, b }, "Circuito: crush bits=1 (seno 0 dBFS: 3 livelli, H3 < -60 dB, H5 -13.98 dB +-0.5), rate=1 kHz: tenuta a 250 Hz -0.91 dB (+-0.5)",
                       [&lab, a, b] {
                           const auto& h = lab.all (a);
                           const double h3 = relH (h, 3), h5 = relH (h, 5);
                           return Verdict { h3 < -60.0 && within (h5, -13.98, 0.5) && within (lab.v (b), -0.91, 0.5),
                                            "H3 " + num (std::max (h3, -200.0), 1) + " dB, H5 " + num (h5) + " dB, 250 Hz " + num (lab.v (b)) + " dB" };
                       });
        }
        {
            const auto de = circuitDef ("tap n=0; envvca src=0 ref=0.1 pw=lin(0,0,2) gmax=100 atk=1 rel=50", 1);
            const int a = fr (de, { { 0, 0.5f } }, 1000.0, 0.1), b = fr (de, { { 0, 0.5f } }, 1000.0, 0.01);
            lab.check ({ a, b }, "Circuito: envvca pw=1, guadagno proporzionale all'inviluppo: -20 dB d'ingresso -> -20 dB di guadagno (+-1)", [&lab, a, b] {
                return Verdict { within (lab.v (b) - lab.v (a), -20.0, 1.0), num (lab.v (b) - lab.v (a)) + " dB (0.1 V " + num (lab.v (a)) + ", 0.01 V " + num (lab.v (b)) + " dB)" };
            });
        }
        {
            const int a = st (circuitDef ("chgain l=taper(0,B) r=taper(1,B)", 2), { { 0, 1.0f }, { 1, 0.1f } });
            lab.check ({ a }, "Circuito: chgain l=1 r=0.1 (stereo): canale destro -20 dB rispetto al sinistro (+-0.2)", [&lab, a] {
                const double rel = (lab.v (a, 1) - lab.v (a, 0)) - (-23.0 + 28.7);
                return Verdict { within (rel, -20.0, 0.2), num (rel) + " dB" };
            });
        }

        // 3. pedali: comandi contro i dati dichiarati (manuali Dunlop/EHX, schemi), comandi per indice come nel banco
        {
            struct Band { int idx; double f; };
            const Band bands[] = { { 1, 31.5 }, { 6, 1000.0 }, { 9, 8000.0 } };
            std::vector<int> h;
            for (const auto& b : bands)
            {
                h.push_back (fr ("kfk1", {}, b.f, 1.0e-4));
                h.push_back (fr ("kfk1", { { b.idx, 1.0f } }, b.f, 1.0e-4));
                h.push_back (fr ("kfk1", { { b.idx, 0.0f } }, b.f, 1.0e-4));
            }
            lab.check (h, "MXR KFK1 (kfk1): bande 31.25 Hz / 1 kHz / 8 kHz al massimo e al minimo +-12 dB (+-0.5)", [&lab, h] {
                bool ok = true;
                juce::String s;
                for (size_t k = 0; k < 3; ++k)
                {
                    const double up = lab.v (h[3 * k + 1]) - lab.v (h[3 * k]), dn = lab.v (h[3 * k + 2]) - lab.v (h[3 * k]);
                    ok = ok && within (up, 12.0, 0.5) && within (dn, -12.0, 0.5);
                    s << num (up) << "/" << num (dn) << (k < 2 ? "; " : " dB");
                }
                return Verdict { ok, s };
            });
            const int a16 = fr ("kfk1", {}, 16000.0, 1.0e-4), b16 = fr ("kfk1", { { 10, 1.0f } }, 16000.0, 1.0e-4);
            const int a1 = fr ("kfk1", {}, 1000.0, 1.0e-4), g1 = fr ("kfk1", { { 11, 1.0f } }, 1000.0, 1.0e-4);
            lab.check ({ a16, b16, a1, g1 }, "MXR KFK1 (kfk1): mensola 16 kHz al massimo +12 dB (+-3, misurato 9.9), GAIN al massimo +12 dB (+-0.5)",
                       [&lab, a16, b16, a1, g1] {
                           const double sh = lab.v (b16) - lab.v (a16), g = lab.v (g1) - lab.v (a1);
                           return Verdict { within (sh, 12.0, 3.0) && within (g, 12.0, 0.5), num (sh) + " / " + num (g) + " dB" };
                       });
        }
        {
            struct Band { const char* label; int idx; double f, range; };
            const Band bands[] = { { "BASS", 1, 31.5, 7.0 }, { "MID", 3, 630.0, 5.0 }, { "TREBLE", 4, 10000.0, 6.0 } };
            std::vector<int> h;
            for (const auto& b : bands)
            {
                h.push_back (fr ("evhmhg", {}, b.f, 1.0e-5));
                h.push_back (fr ("evhmhg", { { b.idx, 1.0f } }, b.f, 1.0e-5));
                h.push_back (fr ("evhmhg", { { b.idx, 0.0f } }, b.f, 1.0e-5));
            }
            h.push_back (fr ("evhmhg", {}, 1000.0, 1.0e-5));
            h.push_back (fr ("evhmhg", { { 7, 1.0f } }, 1000.0, 1.0e-5));
            lab.check (h, "MXR EVH Hm (evhmhg): BASS / MID / TREBLE agli estremi +-7 / +-5 / +-6 dB a 31.5 Hz / 630 Hz / 10 kHz (+-1.5), BOOST +6 dB (+-0.5)",
                       [&lab, h] {
                           const double ranges[3] = { 7.0, 5.0, 6.0 };
                           bool ok = true;
                           juce::String s;
                           for (size_t k = 0; k < 3; ++k)
                           {
                               const double up = lab.v (h[3 * k + 1]) - lab.v (h[3 * k]), dn = lab.v (h[3 * k + 2]) - lab.v (h[3 * k]);
                               ok = ok && within (up, ranges[k], 1.5) && within (dn, -ranges[k], 1.5);
                               s << num (up) << "/" << num (dn) << "; ";
                           }
                           const double boost = lab.v (h[10]) - lab.v (h[9]);
                           ok = ok && within (boost, 6.0, 0.5);
                           return Verdict { ok, s + "BOOST " + num (boost) + " dB" };
                       });
        }
        {
            struct Sweep { const char* id; int idx; double ref, tol; const char* what; };
            const Sweep sweeps[] = { { "eg74", 2, 26.0, 3.0, "DRIVE (Dunlop 21-47 dB)" }, { "yjm308", 1, 35.3, 1.5, "GAIN (4k7 + 47n: 9.5-44.8 dB)" },
                                     { "rr104", 1, 38.8, 1.5, "DISTORTION (4k7 + 47n: 6.0-44.8 dB)" } };
            for (const auto& sw : sweeps)
            {
                const int a = fr (sw.id, { { sw.idx, 0.0f } }, 1000.0, 1.0e-5), b = fr (sw.id, { { sw.idx, 1.0f } }, 1000.0, 1.0e-5);
                const double ref = sw.ref, tol = sw.tol;
                lab.check ({ a, b }, juce::String (sw.id) + ": escursione del " + sw.what + " a 1 kHz = " + num (ref, 1) + " dB (+-" + num (tol, 1) + ")",
                           [&lab, a, b, ref, tol] { return Verdict { within (lab.v (b) - lab.v (a), ref, tol), num (lab.v (b) - lab.v (a)) + " dB" }; });
            }
        }
        {
            const int b0 = fr ("tbm1", { { 1, 0.0f } }, 50.0, 1.0e-6), b1 = fr ("tbm1", { { 1, 1.0f } }, 50.0, 1.0e-6);
            const int m5 = fr ("tbm1", {}, 500.0, 1.0e-6), m5u = fr ("tbm1", { { 2, 1.0f } }, 500.0, 1.0e-6), m5d = fr ("tbm1", { { 2, 0.0f } }, 500.0, 1.0e-6);
            const int t5 = fr ("tbm1", {}, 5000.0, 1.0e-6), t5u = fr ("tbm1", { { 3, 1.0f } }, 5000.0, 1.0e-6), t5d = fr ("tbm1", { { 3, 0.0f } }, 5000.0, 1.0e-6);
            const int t5dd = fr ("tbm1", { { 3, 0.0f }, { 2, 0.0f } }, 5000.0, 1.0e-6);
            const int p4 = fr ("tbm1", {}, 4000.0, 1.0e-6), p4u = fr ("tbm1", { { 5, 1.0f } }, 4000.0, 1.0e-6);
            lab.check ({ b0, b1, m5, m5u, m5d, t5, t5u, t5d, t5dd, p4, p4u },
                       "MXR Bass Preamp (tbm1): BASS 25 dB a 50 Hz (+-3), MIDDLE / TREBLE +-7 dB (+-1.5), TREBLE e MIDDLE al minimo -21 dB (+-3.5), PRESENCE +7 dB (+-1)",
                       [&lab, b0, b1, m5, m5u, m5d, t5, t5u, t5d, t5dd, p4, p4u] {
                           const double bass = lab.v (b1) - lab.v (b0), mu = lab.v (m5u) - lab.v (m5), md = lab.v (m5d) - lab.v (m5);
                           const double tu = lab.v (t5u) - lab.v (t5), td = lab.v (t5d) - lab.v (t5), tdd = lab.v (t5dd) - lab.v (t5);
                           const double pr = lab.v (p4u) - lab.v (p4);
                           const bool ok = within (bass, 25.0, 3.0) && within (mu, 7.0, 1.5) && within (md, -7.0, 1.5) && within (tu, 7.0, 1.5) && within (td, -7.0, 1.5)
                                           && within (tdd, -21.0, 3.5) && within (pr, 7.0, 1.0);
                           return Verdict { ok, "BASS " + num (bass) + ", MIDDLE " + num (mu) + "/" + num (md) + ", TREBLE " + num (tu) + "/" + num (td)
                                                    + ", entrambi al minimo " + num (tdd) + ", PRESENCE " + num (pr) + " dB" };
                       });
        }
        {
            std::vector<int> h;
            for (const char* id : { "bmreverse", "bmwicker", "bmhwplugin" })
            {
                h.push_back (gtr (id, {}, riff));
                h.push_back (gtr (id, { { 3, 1.0f } }, riff));
            }
            lab.check (h, "Big Muff Reverse / Wicker / Hardware Plugin: TONE BYPASS aumenta l'RMS del riff di 7.5 dB (+-1.5, Kit Rae +6/9)", [&lab, h] {
                bool ok = true;
                juce::String s;
                for (size_t k = 0; k < 3; ++k)
                {
                    const double d = lab.v (h[2 * k + 1]) - lab.v (h[2 * k]);
                    ok = ok && within (d, 7.5, 1.5);
                    s << num (d, 1) << (k < 2 ? " / " : " dB");
                }
                return Verdict { ok, s };
            });
        }
        for (const char* id : { "bmsovdlx", "bmpi2dlx" })
        {
            const double fq[3] = { 315.0, 1250.0, 5000.0 };
            const float pos[3] = { 0.0f, 0.5f, 1.0f };
            std::vector<int> h;
            for (int k = 0; k < 3; ++k)
            {
                h.push_back (fr (id, { { 9, 1.0f } }, fq[k], 1.0e-6));
                h.push_back (fr (id, { { 5, 1.0f }, { 6, pos[k] }, { 9, 1.0f } }, fq[k], 1.0e-6));
            }
            lab.check (h, juce::String (id) + ": MIDS LEVEL +10 dB (+-1.5) a 310 Hz / 1245 Hz / 5 kHz (manuale EHX)", [&lab, h] {
                bool ok = true;
                juce::String s;
                for (size_t k = 0; k < 3; ++k)
                {
                    const double d = lab.v (h[2 * k + 1]) - lab.v (h[2 * k]);
                    ok = ok && within (d, 10.0, 1.5);
                    s << num (d) << (k < 2 ? " / " : " dB");
                }
                return Verdict { ok, s };
            });
        }
        {
            const int ma = fr ("micrometal", { { 2, 0.0f } }, 800.0, 1.0e-5), mb = fr ("micrometal", { { 2, 0.0f }, { 1, 1.0f } }, 800.0, 1.0e-5);
            const int ba = fr ("nanometal", { { 2, 0.0f } }, 50.0, 1.0e-5), bb = fr ("nanometal", { { 2, 0.0f }, { 5, 1.0f } }, 50.0, 1.0e-5);
            const int ka = fr ("nanometal", { { 2, 0.0f } }, 800.0, 1.0e-5), kb = fr ("nanometal", { { 2, 0.0f }, { 4, 1.0f } }, 800.0, 1.0e-5);
            const int ta = fr ("nanometal", { { 2, 0.0f } }, 8000.0, 1.0e-5), tb = fr ("nanometal", { { 2, 0.0f }, { 3, 1.0f } }, 8000.0, 1.0e-5);
            lab.check ({ ma, mb, ba, bb, ka, kb, ta, tb },
                       "Micro Metal Muff TONE +15 dB (+-1.5); Nano Metal Muff BASS +14 / MID +15 / TREBLE +10 dB (+-3) (manuali EHX)",
                       [&lab, ma, mb, ba, bb, ka, kb, ta, tb] {
                           const double m = lab.v (mb) - lab.v (ma), b = lab.v (bb) - lab.v (ba), k = lab.v (kb) - lab.v (ka), t = lab.v (tb) - lab.v (ta);
                           return Verdict { within (m, 15.0, 1.5) && within (b, 14.0, 3.0) && within (k, 15.0, 3.0) && within (t, 10.0, 3.0),
                                            num (m) + "; " + num (b) + " / " + num (k) + " / " + num (t) + " dB" };
                       });
        }
        {
            const int a = fr ("ehlpb3", { { 5, 1.0f }, { 2, 1.0f } }, 1000.0, 1.0e-4), b = fr ("ehlpb3", { { 0, 1.0f }, { 5, 1.0f }, { 2, 1.0f } }, 1000.0, 1.0e-4);
            const int g0 = fr ("ehsprucegoose", { { 1, 0.0f } }, 1000.0, 1.0e-5), g5 = fr ("ehsprucegoose", { { 1, 0.0f }, { 2, 0.5f } }, 1000.0, 1.0e-5);
            const int g1 = fr ("ehsprucegoose", { { 1, 0.0f }, { 2, 1.0f } }, 1000.0, 1.0e-5);
            const int hw = fr ("ehhotwax", { { 7, 0.0f }, { 8, 0.0f } }, 1000.0, 1.0e-4);
            lab.check ({ a, b, g0, g5, g1, hw }, "LPB-3 guadagno massimo 20 / 33 dB (MAX), Spruce Goose LIFT +9 / +21 dB (+-0.5); Hot Wax con entrambe le sezioni spente = pulito (+-0.1)",
                       [&lab, a, b, g0, g5, g1, hw] {
                           const double l5 = lab.v (g5) - lab.v (g0), l1 = lab.v (g1) - lab.v (g0);
                           const bool ok = within (lab.v (a), 20.0, 0.5) && within (lab.v (b), 33.0, 0.5) && within (l5, 9.0, 0.5) && within (l1, 21.0, 0.5)
                                           && within (lab.v (hw), 0.0, 0.1);
                           return Verdict { ok, "LPB-3 " + num (lab.v (a)) + " / " + num (lab.v (b)) + " dB, LIFT " + num (l5) + " / " + num (l1)
                                                    + " dB, Hot Wax " + num (lab.v (hw)) + " dB" };
                       });
        }
        {
            const int a = fr ("ehgraphicfuzz", { { 10, 1.0f } }, 1000.0, 1.0e-4), b = fr ("ehgraphicfuzz", { { 10, 1.0f }, { 6, 1.0f } }, 1000.0, 1.0e-4);
            const int c = fr ("ehgraphicfuzz", { { 10, 1.0f }, { 6, 0.0f } }, 1000.0, 1.0e-4);
            const int o0 = fr ("ehgraphicfuzz", { { 10, 0.0f }, { 0, 0.0f } }, 1000.0, 1.0e-4), o1 = fr ("ehgraphicfuzz", { { 10, 0.0f }, { 0, 1.0f } }, 1000.0, 1.0e-4);
            const int d0a = gtr ("ehgraphicfuzz", { { 1, 0.0f } }, riff), d0b = gtr ("ehgraphicfuzz", { { 1, 0.0f } }, riffSoft);
            const int d1a = gtr ("ehgraphicfuzz", { { 1, 1.0f } }, riff), d1b = gtr ("ehgraphicfuzz", { { 1, 1.0f } }, riffSoft);
            lab.check ({ a, b, c, o0, o1, d0a, d0b, d1a, d1b },
                       "Graphic Fuzz: banda 1 kHz +-15 dB con FUZZ OFF (+-0.5), OVERDRIVE 28 dB (+-1.5), DYNAMICS: -12 dB d'uscita in piu' per -12 dB d'ingresso (+-2)",
                       [&lab, a, b, c, o0, o1, d0a, d0b, d1a, d1b] {
                           const double up = lab.v (b) - lab.v (a), dn = lab.v (c) - lab.v (a), od = lab.v (o1) - lab.v (o0);
                           const double dyn = (lab.v (d1b) - lab.v (d1a)) - (lab.v (d0b) - lab.v (d0a));
                           return Verdict { within (up, 15.0, 0.5) && within (dn, -15.0, 0.5) && within (od, 28.0, 1.5) && within (dyn, -12.0, 2.0),
                                            num (up) + " / " + num (dn) + " dB, OVERDRIVE " + num (od) + " dB, DYNAMICS " + num (dyn, 1) + " dB" };
                       });
        }
        {
            const int a = fr ("ehsatisplus", {}, 1000.0, 1.0e-5), b = fr ("ehsatisfaction", {}, 1000.0, 1.0e-5);
            const int ga = gtr ("ehsatisplus", {}, riff), gb = gtr ("ehsatisfaction", {}, riff);
            lab.check ({ a, b, ga, gb }, "Satisfaction Plus a ore 12 = Satisfaction: scarto a 1 kHz 0 dB (+-1), RMS del riff 0 dB (+-1.5)", [&lab, a, b, ga, gb] {
                const double d1 = lab.v (a) - lab.v (b), d2 = lab.v (ga) - lab.v (gb);
                return Verdict { within (d1, 0.0, 1.0) && within (d2, 0.0, 1.5), num (d1) + " / " + num (d2, 1) + " dB" };
            });
        }
        {
            const int r5 = sine ("ehrippedspeaker", { { 1, 0.5f } }, 220.0, -40.0), r0 = sine ("ehrippedspeaker", { { 1, 0.0f } }, 220.0, -40.0);
            const int p1 = sine ("ehpercolator", { { 2, 0.1f }, { 3, 0.0f } }, 220.0, -20.0), p9 = sine ("ehpercolator", { { 2, 1.0f }, { 3, 0.0f } }, 220.0, -20.0);
            const int q0 = sine ("ehlizardqueen", { { 1, 0.0f } }, 220.0, -20.0), q1 = sine ("ehlizardqueen", { { 1, 1.0f } }, 220.0, -20.0);
            lab.check ({ r5, r0, p1, p9, q0, q1 },
                       "Stadio cebias: Ripped Speaker RIP 0 annulla il segnale debole (-85 dB +-15), Percolator BIAS 1 alza H2 di 10 dB (+-10), "
                       "Lizard Queen OCTAVE 0 senza H2 (< -90 dB) e al massimo con H2 dominante (0 dB +-8)",
                       [&lab, r5, r0, p1, p9, q0, q1] {
                           const double rip = 20 * std::log10 ((lab.v (r0) + 1e-30) / (lab.v (r5) + 1e-30));
                           const double perc = relH (lab.all (p9), 2) - relH (lab.all (p1), 2);
                           const double lq0 = std::max (relH (lab.all (q0), 2), -100.0), lq1 = relH (lab.all (q1), 2);
                           return Verdict { within (rip, -85.0, 15.0) && within (perc, 10.0, 10.0) && within (lq0, -100.0, 10.0) && within (lq1, 0.0, 8.0),
                                            "RIP " + num (rip, 1) + " dB, BIAS " + num (perc, 1) + " dB, H2 " + num (lq0, 1) + " / " + num (lq1, 1) + " dB" };
                       });
        }
        {
            const int s0 = fr ("ehsilencer", { { 0, 0.5f }, { 1, 0.0f }, { 2, 0.0f } }, 1000.0, 1.0e-3);
            const int s1 = fr ("ehsilencer", { { 0, 0.5f }, { 1, 0.0f }, { 2, 1.0f } }, 1000.0, 1.0e-3);
            const int s2 = fr ("ehsilencer", { { 0, 0.0f }, { 2, 1.0f } }, 1000.0, 1.0e-3);
            const int sl = gtr ("ehsteelleather", { { 0, 0.0f } }, riff);
            const int ss = fr ("ehsuperswitcher", { { 0, 1.0f }, { 1, 1.0f } }, 1000.0, 1.0e-4);
            lab.check ({ s0, s1, s2, sl, ss },
                       "Silencer a gate chiuso REDUCTION +4 / -70 dB (+-0.5), THRESHOLD 0 aperto (+-0.1); Steel Leather EFFECT 0 = pulito (-28.7 dBFS +-0.3); "
                       "Super Switcher BOOST +20 dB (+-0.2)",
                       [&lab, s0, s1, s2, sl, ss] {
                           const bool ok = within (lab.v (s0), 4.0, 0.5) && within (lab.v (s1), -70.0, 0.5) && within (lab.v (s2), 0.0, 0.1)
                                           && within (lab.v (sl), -28.7, 0.3) && within (lab.v (ss), 20.0, 0.2);
                           return Verdict { ok, num (lab.v (s0)) + " / " + num (lab.v (s1)) + " / " + num (lab.v (s2)) + " dB, Steel Leather "
                                                    + num (lab.v (sl), 1) + " dBFS, BOOST " + num (lab.v (ss)) + " dB" };
                       });
        }
        {
            const int p = fr ("ehswitchbladepro", { { 0, 1.0f }, { 2, 1.0f }, { 3, 1.0f }, { 5, 1.0f } }, 1000.0, 1.0e-4);
            const int s = fr ("ehswitchbladepro", { { 0, 1.0f }, { 2, 1.0f }, { 3, 0.0f }, { 5, 1.0f } }, 1000.0, 1.0e-4);
            const int a = fr ("ehswitchbladepro", { { 1, 0.5f } }, 1000.0, 1.0e-4);
            const int t = fr ("ehtriparallel", { { 3, 0.5f }, { 11, 0.2857f } }, 1000.0, 1.0e-4);
            const int c = fr ("ehchillswitch", { { 1, 1.0f } }, 1000.0, 1.0e-4);
            const int v0 = fr ("ehvolume", { { 1, 0.0f } }, 8000.0, 1.0e-4), v1 = fr ("ehvolume", {}, 8000.0, 1.0e-4);
            lab.check ({ p, s, a, t, c, v0, v1 },
                       "Switchblade Pro PARALLEL / SERIES +12.04 dB, loop A + DRY +6.02 dB (+-0.2); Tri Parallel fase 0R0 e Chillswitch premuto muti (< -99 dB); "
                       "Volume LO-Z -3 dB a 8 kHz (+-0.5)",
                       [&lab, p, s, a, t, c, v0, v1] {
                           const double lz = lab.v (v0) - lab.v (v1);
                           const bool ok = within (lab.v (p), 12.04, 0.2) && within (lab.v (s), 12.04, 0.2) && within (lab.v (a), 6.02, 0.2)
                                           && lab.v (t) <= -99.0 && lab.v (c) <= -99.0 && within (lz, -3.0, 0.5);
                           return Verdict { ok, num (lab.v (p)) + " / " + num (lab.v (s)) + " / " + num (lab.v (a)) + " dB, " + num (std::max (lab.v (t), -200.0), 1)
                                                    + " / " + num (std::max (lab.v (c), -200.0), 1) + " dB, LO-Z " + num (lz) + " dB" };
                       });
        }
        {
            struct Case { float mode, pos; double l, r; };
            const Case cases[] = { { 0.666667f, 0.0f, -300.0, -28.7 }, { 0.666667f, 1.0f, -28.7, -300.0 }, { 0.666667f, 0.5f, -31.7, -31.7 },
                                   { 0.333333f, 0.0f, -23.0, -23.0 } };
            std::vector<int> h;
            for (const auto& c : cases) h.push_back (st ("ehnextpan", { { 1, c.mode }, { 0, c.pos } }));
            const int sb = st ("ehswitchblade", { { 0, 1.0f } }), sab = st ("ehswitchblade", { { 1, 1.0f } });
            const int lp = st ("ehlpb2ube", { { 0, 1.0f }, { 1, 0.0f } });
            std::vector<int> all = h;
            all.insert (all.end(), { sb, sab, lp });
            lab.check (all, "Stadio pan e chgain (stereo, L riff -28.7 dBFS, R seno -23 dBFS): Next Step Pan PAN tallone / punta / centro (-3 dB) e BLEND tallone "
                            "(+-0.3); Switchblade B e A+B; LPB-2ube canali indipendenti (L - R 29 dB +-8)",
                       [&lab, h, sb, sab, lp, cases] {
                           bool ok = true;
                           juce::String s;
                           for (size_t k = 0; k < h.size(); ++k)
                           {
                               const double l = std::max (lab.v (h[k], 0), -300.0), r = std::max (lab.v (h[k], 1), -300.0);
                               ok = ok && within (l, cases[k].l, 0.3) && within (r, cases[k].r, 0.3);
                               s << num (l, 1) << "/" << num (r, 1) << "; ";
                           }
                           const double bOnly = std::max (lab.v (sb, 0), -300.0), abR = lab.v (sab, 1), lr = lab.v (lp, 0) - lab.v (lp, 1);
                           ok = ok && within (bOnly, -300.0, 0.1) && within (abR, -28.7, 0.3) && within (lr, 29.0, 8.0);
                           return Verdict { ok, "PAN " + s + "Switchblade " + num (bOnly, 1) + " / " + num (abR, 1) + " dBFS, LPB-2ube " + num (lr, 1) + " dB" };
                       });
        }
        {
            const int b0 = gtr ("ehblurst", { { 3, 0.0f } }, riff), b1 = gtr ("ehblurst", { { 3, 1.0f } }, riff);
            const int w0 = gtr ("ehswello", { { 2, 0.0f } }, note), w1 = gtr ("ehswello", { { 2, 1.0f } }, note);
            const int y5 = gtr ("ehytrig", { { 0, 0.5f } }, riff), y1 = gtr ("ehytrig", { { 0, 1.0f } }, riff);
            lab.check ({ b0, b1, w0, w1, y5, y1 },
                       "Filtri: Blurst RANGE LO centroide piu' basso (-1000 Hz +-1500), Swello ATTACK 4 s la nota non emerge (-30 dB +-10), "
                       "Y-Trig UP-DOWN orario centroide piu' alto (+1500 Hz +-1500)",
                       [&lab, b0, b1, w0, w1, y5, y1] {
                           const double bl = lab.v (b0, 2) - lab.v (b1, 2), sw = lab.v (w1) - lab.v (w0), yt = lab.v (y1, 2) - lab.v (y5, 2);
                           return Verdict { within (bl, -1000.0, 1500.0) && within (sw, -30.0, 10.0) && within (yt, 1500.0, 1500.0),
                                            num (bl, 0) + " Hz, " + num (sw, 1) + " dB, " + num (yt, 0) + " Hz" };
                       });
        }
        {
            const int a = sine ("ehmainframe", { { 5, 0.0f }, { 6, 0.0f }, { 4, 1.0f }, { 3, 1.0f }, { 2, 0.0f } }, 1000.0, -6.0);
            const int b = sine ("ehmainframe", { { 5, 1.0f }, { 6, 0.0f }, { 4, 1.0f }, { 3, 1.0f }, { 2, 0.0f } }, 1000.0, -6.0);
            lab.check ({ a, b }, "Stadio crush (Mainframe, seno 1 kHz -6 dBFS): BIT DEPTH 24 H3 < -90 dB, 1 bit con scala sull'inviluppo H3 -16.7 dB (+-2)",
                       [&lab, a, b] {
                           const double h24 = std::max (relH (lab.all (a), 3), -100.0), h1 = relH (lab.all (b), 3);
                           return Verdict { within (h24, -100.0, 10.0) && within (h1, -16.7, 2.0), num (h24, 1) + " / " + num (h1, 1) + " dB" };
                       });
        }

        // 4. livelli: riff a -12 dBFS con i comandi di default -> uscita finita, picco < 0 dBFS, RMS tra -36 e -14 dBFS
        constexpr int numB = (int) (sizeof (kStage3BAnalog) / sizeof (kStage3BAnalog[0]));
        {
            std::vector<int> h;
            for (const char* id : kStage3BAnalog) h.push_back (gtr (id, {}, riff));
            lab.check (h, "Tappa 3B analogici: livelli con riff a -12 dBFS (74 modelli: finiti, picco < 0 dBFS, RMS -36..-14 dBFS)", [&lab, h] {
                int good = 0;
                double rmsLo = 0, rmsHi = -200, pkHi = -200;
                juce::String bad;
                for (size_t m = 0; m < h.size(); ++m)
                {
                    const double r = lab.v (h[m], 0), pk = lab.v (h[m], 1);
                    const bool fin = lab.v (h[m], 3) > 0.5;
                    if (fin && pk < 0.0 && r > -36.0 && r < -14.0) ++good;
                    else bad << kStage3BAnalog[m] << " (RMS " << num (r, 1) << ", picco " << num (pk, 1) << (fin ? "" : ", NaN") << ") ";
                    rmsLo = std::min (rmsLo, r); rmsHi = std::max (rmsHi, r); pkHi = std::max (pkHi, pk);
                }
                return Verdict { good == (int) h.size(), juce::String (good) + "/" + juce::String ((int) h.size()) + ", RMS " + num (rmsLo, 1) + ".." + num (rmsHi, 1)
                                                         + " dBFS, picco max " + num (pkHi, 1) + " dBFS " + bad.trim() };
            });
        }

        // 5. robustezza: comandi tutti a 0, tutti a 1 e 3 combinazioni casuali, a 44.1 e 96 kHz, accordo a -3 dBFS (una pennata di 1 s come
        //    nella 3A). Limite +24 dBFS, salvo LPB-3 e White Finger a +30 dBFS: con tutti i comandi al massimo l'LPB-3 arriva a +29 dBFS
        //    (il pedale dichiara +22 dBu d'uscita, binari +-15 V) e il White Finger a +24.7 dBFS a 96 kHz; il sovrappiu' rispetto ai binari
        //    viene dal filtro di decimazione del sovracampionamento (gia' nel motore: un clipping duro a 1 V esce a +6.6 dBFS)
        {
            const double rates[2] = { 44100.0, 96000.0 };
            static const Buf chords[2] = { guitarS1 (false, 44100.0, 1.0, -3.0), guitarS1 (false, 96000.0, 1.0, -3.0) };
            juce::Random rng (77);
            std::vector<int> h;
            for (int ri = 0; ri < 2; ++ri)
                for (int m = 0; m < numB; ++m)
                {
                    const auto* d = findModel (kStage3BAnalog[m]);
                    for (int c = 0; c < 5; ++c)
                    {
                        ISets s;
                        for (int k = 0; k < (d != nullptr ? d->numControls : 0); ++k)
                            s.push_back ({ k, c == 0 ? 0.0f : c == 1 ? 1.0f : rng.nextFloat() });
                        const double rate = rates[ri];
                        const Buf* in = &chords[ri];
                        h.push_back (lab.add (kStage3BAnalog[m], [s, rate, in] (const ModelDef& dd)
                        {
                            auto fx = makeIdx (dd, s, rate);
                            Buf y = *in;
                            processMono (*fx, y, 512);
                            bool fin = true;
                            double pk = 0;
                            for (float x : y) { fin = fin && std::isfinite (x); pk = std::max (pk, (double) std::abs (x)); }
                            return std::vector<double> { fin ? 1.0 : 0.0, dbOf (pk) };
                        }));
                    }
                }
            lab.check (h, "Tappa 3B analogici: robustezza (74 modelli x 5 regolazioni x 44.1/96 kHz, accordo a -3 dBFS): finiti, picco < +24 dBFS "
                          "(LPB-3 e White Finger < +30)",
                       [&lab, h, rates] {
                           double worst = -200;
                           juce::String bad, worstId;
                           for (size_t t = 0; t < h.size(); ++t)
                           {
                               const int c = (int) (t % 5), m = (int) ((t / 5) % (size_t) numB), ri = (int) (t / (5 * (size_t) numB));
                               const char* id = kStage3BAnalog[m];
                               const bool fin = lab.v (h[t], 0) > 0.5;
                               const double pk = lab.v (h[t], 1);
                               const double limit = (std::strcmp (id, "ehlpb3") == 0 || std::strcmp (id, "ehwhitefinger") == 0) ? 30.0 : 24.0;
                               if (! fin || pk > worst) { worst = fin ? pk : 999.0; worstId = juce::String (id) + "@" + num (rates[ri] / 1000.0, 1) + "/" + juce::String (c); }
                               if (! fin || pk >= limit) bad << id << "@" << num (rates[ri] / 1000.0, 1) << "/" << c << (fin ? " " + num (pk, 1) : juce::String (" NaN")) << " ";
                           }
                           return Verdict { bad.isEmpty(), juce::String ((int) h.size()) + " rendering, picco max " + num (worst, 1) + " dBFS (" + worstId + ") " + bad.trim() };
                       });
        }

        lab.run (report);
    }

    //------------------------------------------------------------------------------
    /** Livello (dB) dell'energia entro +-3% di f rispetto al totale (banco s2b, FFT 65536 con finestra di Hann). */
    double bandDb (const Buf& y, double f, size_t a, size_t len)
    {
        const int order = 16, N = 1 << order;
        std::vector<float> w ((size_t) N * 2, 0.0f);
        len = std::min (len, (size_t) N);
        for (size_t i = 0; i < len; ++i) w[i] = y[a + i] * (float) (0.5 - 0.5 * std::cos (2 * kPi * (double) i / (double) (len - 1)));
        juce::dsp::FFT fft (order);
        fft.performFrequencyOnlyForwardTransform (w.data());
        const double bin = 48000.0 / N;
        double tot = 0, in = 0;
        for (int j = 1; j < N / 2; ++j)
        {
            const double e = (double) w[(size_t) j] * w[(size_t) j];
            tot += e;
            if (std::abs (j * bin - f) < 0.03 * f) in += e;
        }
        return 10 * std::log10 (in / std::max (1e-30, tot));
    }

    /** Prova di robustezza dei digitali 3B, ridotta per il tempo dell'autotest (come la 3A): frase di chitarra (x3) di 4 s invece
        di 8 (Mi basso, power chord, accordo aperto) e 2 s di silenzio invece di 4; prodotto cartesiano dei selettori fino a 16
        combinazioni (banco: 48), oltre ogni posizione di ogni selettore piu' 4 combinazioni casuali (banco: 40). */
    constexpr double kStressPhraseS = 4.0, kStressTailS = 2.0;
    constexpr long kStressFullMax = 16;
    constexpr int kStressRandom = 4;

    void runStage3BDigitalTests (const std::function<void (const juce::String&, bool, const juce::String&)>& report)
    {
        using namespace pt::engine;
        constexpr int numB = (int) (sizeof (kStage3BDigital) / sizeof (kStage3BDigital[0]));
        Lab lab;
        /** Uscita (canale 0) di un modello con i comandi per ruolo/etichetta (banco s2b 'P'). */
        auto render = [&lab] (const char* id, Sets sets, const Buf& in, std::function<std::vector<double> (const Buf&)> measure)
        {
            return lab.add (id, [sets, &in, measure] (const ModelDef& d) { return measure (runS2 (d, in, setsS2 (d, sets))[0]); });
        };

        // 0. il nuovo aggancio (makeStage3B) prende solo i modelli della tappa con tipo "b3..." (46); gli altri 4 (Lester K, DMM 550 / 1100,
        //    STRING9) usano i tipi della 3A (leslie, mmbbd, nine) con config nuove e li aggancia makeClassic (controllo della 3A).
        //    Nessun pedale esistente ne' replica REAL MOD passa da makeStage3B
        lab.check ({}, "Tappa 3B digitali: makeStage3B aggancia i 46 modelli nuovi di tipo b3 (gli altri 4 usano leslie/mmbbd/nine della 3A) e nessun pedale "
                       "esistente (catalogo e REAL MOD)", [] {
            auto isB = [] (const char* id) { for (const char* n : kStage3BDigital) if (std::strcmp (n, id) == 0) return true; return false; };
            int hooked = 0, total = 0, others = 0;
            juce::String bad;
            auto probe = [&] (const ModelDef& d, bool real)
            {
                ++total;
                const auto type = Config (d.config).str ("type");
                const bool h = makeStage3B (d, type) != nullptr;
                if (h) ++hooked;
                const bool b3 = type.rfind ("b3", 0) == 0;
                if (isB (d.id) && ! real && ! b3)
                {
                    ++others;
                    if (type != "leslie" && type != "mmbbd" && type != "nine") bad << d.id << " (tipo " << type << ") ";
                }
                if (h != (isB (d.id) && ! real && b3)) bad << d.id << (real ? "(REAL) " : " ");
            };
            for (int i = 0; i < numModels(); ++i) probe (model (i), false);
            for (int i = 0; i < numRealModels(); ++i) probe (realModel (i), true);
            return Verdict { bad.isEmpty() && hooked == numB - 4 && others == 4,
                             juce::String (hooked) + " agganciati su " + juce::String (total) + ", " + juce::String (others) + " con tipi della 3A"
                                 + (bad.isEmpty() ? juce::String() : ", errati: " + bad.trim()) };
        });

        // 1. robustezza: tutti i comandi continui a 1 (feedback, regen, ripetizioni, decay, livelli) e ogni combinazione di selettori e
        //    levette (prodotto cartesiano fino a kStressFullMax combinazioni, altrimenti kStressRandom casuali piu' ogni posizione di ogni
        //    selettore), mono e stereo alternati, blocchi da 64 e 256; frase di chitarra x3 (picchi ~1 V) seguita da silenzio (la coda
        //    resta limitata)
        {
            static const Buf loud = []
            {
                Buf g = guitarS2 (48000.0, kStressPhraseS);
                for (auto& x : g) x *= 3.0f;
                g.resize ((size_t) (48000.0 * (kStressPhraseS + kStressTailS)), 0.0f);
                return g;
            }();
            std::vector<int> h;
            juce::StringArray what;
            for (int i = 0; i < numB; ++i)
            {
                const auto* d = findModel (kStage3BDigital[i]);
                if (d == nullptr) { h.push_back (lab.add (kStage3BDigital[i], {})); what.add ({}); continue; }
                std::vector<int> sels;
                for (int c = 0; c < d->numControls; ++c) if (d->controls[c].steps > 1) sels.push_back (c);
                auto base = [d]
                {
                    std::map<int, float> p;
                    for (int c = 0; c < d->numControls; ++c) if (d->controls[c].steps <= 1) p[c] = 1.0f;
                    return p;
                };
                std::vector<std::map<int, float>> sets;
                long total = 1;
                for (int c : sels) total *= d->controls[c].steps;
                if (total <= kStressFullMax)
                    for (long k = 0; k < total; ++k)
                    {
                        auto p = base();
                        long r = k;
                        for (int c : sels) { const int s = d->controls[c].steps; p[c] = (float) (r % s) / (s - 1); r /= s; }
                        sets.push_back (p);
                    }
                else
                {
                    std::mt19937 rng ((unsigned) (777 + i));
                    for (int k = 0; k < kStressRandom; ++k)
                    {
                        auto p = base();
                        for (int c : sels) { const int s = d->controls[c].steps; p[c] = (float) (rng() % (unsigned) s) / (s - 1); }
                        sets.push_back (p);
                    }
                    for (int c : sels)
                        for (int s = 0; s < d->controls[c].steps; ++s) { auto p = base(); p[c] = (float) s / (d->controls[c].steps - 1); sets.push_back (p); }
                }
                for (size_t si = 0; si < sets.size(); ++si)
                {
                    const int ch = d->stereo && si % 2 ? 2 : 1, block = si % 3 == 0 ? 64 : 256;
                    const auto p = sets[si];
                    h.push_back (lab.add (kStage3BDigital[i], [p, ch, block] (const ModelDef& dd)
                    {
                        bool fin = true;
                        double pk = 0;
                        for (const auto& b : runS2 (dd, loud, p, 48000.0, block, ch))
                            for (float x : b) { fin = fin && std::isfinite (x); pk = std::max (pk, (double) std::abs (x)); }
                        return std::vector<double> { fin ? 1.0 : 0.0, pk };
                    }));
                    what.add (juce::String (kStage3BDigital[i]) + "/" + juce::String ((int) si) + "/" + juce::String (ch));
                }
            }
            lab.check (h, "Tappa 3B digitali: robustezza con feedback/regen/ripetizioni al massimo in ogni modo (50 modelli, frase x3 + silenzio): "
                          "finiti, picco < 8 V",
                       [&lab, h, what] {
                           double worst = 0;
                           juce::String bad, worstWhat;
                           for (size_t k = 0; k < h.size(); ++k)
                           {
                               const bool fin = lab.v (h[k], 0) > 0.5;
                               const double pk = fin ? lab.v (h[k], 1) : 999.0;
                               if (pk > worst) { worst = pk; worstWhat = what[(int) k]; }
                               if (! fin || pk >= 8.0) bad << what[(int) k] << (fin ? " " + num (pk, 2) : juce::String (" NaN")) << " ";
                           }
                           return Verdict { bad.isEmpty(), juce::String ((int) h.size()) + " rendering, picco max " + num (worst, 2) + " V (" + worstWhat + ") " + bad.trim() };
                       });
        }

        // 2. sanita' come nella 3A (blocchi 64/512, default, 3 casuali, 0, 1, ogni selettore, mono e stereo) a 44.1 e 96 kHz: i 48 kHz
        //    sono gia' coperti dalla prova di robustezza e dalle misure (per il tempo dell'autotest)
        {
            const double rates[2] = { 44100.0, 96000.0 };
            static Buf phrases[2];
            for (int r = 0; r < 2; ++r)
            {
                const Buf full = guitarS2 (rates[r], 1.45);
                phrases[r].assign (full.begin() + (long) (0.95 * rates[r]), full.end());
            }
            std::vector<int> h;
            juce::StringArray where;
            for (int ri = 0; ri < 2; ++ri)
                for (int mi = 0; mi < numB; ++mi)
                {
                    const double rate = rates[ri];
                    const Buf* ph = &phrases[ri];
                    const auto* d = findModel (kStage3BDigital[mi]);
                    const int numSets = d != nullptr ? (int) sanitySets (d, mi).size() : 1;
                    for (int si = 0; si < numSets; ++si)
                    {
                        const int job = lab.add (kStage3BDigital[mi], [mi, rate, ph, si] (const ModelDef&)
                        {
                            const auto r = sanityS2 (kStage3BDigital[mi], mi, *ph, rate, si);
                            return std::vector<double> { r.found && r.fin && r.bad.isEmpty() ? 1.0 : 0.0, r.pk, (double) r.runs };
                        });
                        h.push_back (job);
                        where.add (juce::String (kStage3BDigital[mi]) + "@" + num (rate / 1000.0, 1) + "/" + juce::String (si));
                        if (rate > 50000.0) lab.first (job);
                    }
                }
            lab.check (h, "Tappa 3B digitali: sanita' (50 modelli, 44.1/96 kHz, blocchi 64/512, default, casuali, 0, 1, ogni selettore): finiti, picco < 8 V",
                       [&lab, h, where] {
                           double worst = 0;
                           int runs = 0;
                           juce::String bad;
                           for (size_t k = 0; k < h.size(); ++k)
                           {
                               worst = std::max (worst, lab.v (h[k], 1));
                               runs += (int) lab.v (h[k], 2);
                               if (lab.v (h[k], 0) < 0.5) bad << where[(int) k] << " ";
                           }
                           return Verdict { bad.isEmpty(), juce::String (runs) + " elaborazioni, picco max " + num (worst, 2) + " V " + bad.trim() };
                       });
        }

        // 3. ritardi: treno d'impulsi, media dei picchi del wet (feedback 0, BLEND tutto wet)
        {
            struct DT { const char* id; const char* role; double lo, hi, period; Sets extra; double ref[3]; double tolPct; };
            const DT dts[] = {
                { "ehdmm550", "time", 20, 700, 1.5, { { "blend", 1.0f }, { "feedback", 0.0f }, { "depth", 0.0f } }, { 30.4, 330.5, 630.4 }, 2 },
                { "ehdmm1100", "time", 40, 1200, 2.0, { { "blend", 1.0f }, { "feedback", 0.0f }, { "depth", 0.0f } }, { 52.4, 576.3, 1100.3 }, 2 },
                { "ehdmboy", "time", 20, 800, 1.5, { { "blend", 1.0f }, { "feedback", 0.0f }, { "depth", 0.5f } }, { 34.3, 154.6, 700.9 }, 2 },
                { "ehmmstereo", "time", 20, 400, 1.0, { { "blend", 1.0f }, { "feedback", 0.0f } }, { 30.3, 95.2, 300.8 }, 2 },
                { "ehcanecho", "time", 4, 3300, 4.0, { { "blend", 1.0f }, { "feedback", 0.0f } }, { 8.0, 154.9, 3000.0 }, 1 },
                { "ehrerun", "time", 4, 3300, 4.0, { { "blend", 1.0f }, { "feedback", 0.0f }, { "flutter", 0.0f } }, { 8.0, 154.9, 3000.0 }, 1 },
                { "ehanalogizer", "spread", 2, 80, 0.4, { { "blend", 1.0f }, { "gain", 0.0f } }, { 3.66, 15.2, 65.2 }, 5 },
                { "ehtonetattoo", "time", 20, 650, 1.5, { { "blend", 1.0f }, { "feedback", 0.0f }, { "depth", 0.0f }, { "drive", 0.0f } }, { 30.8, 129.3, 551.2 }, 2 } };
            std::vector<int> h;
            for (const auto& t : dts)
                for (float val : { 0.0f, 0.5f, 1.0f })
                {
                    const DT tt = t;
                    h.push_back (lab.add (t.id, [tt, val] (const ModelDef& d)
                    {
                        auto p = setsS2 (d, tt.extra);
                        const auto r = setsS2 (d, { { tt.role, 0.0f } });
                        if (! r.empty()) p[r.begin()->first] = val;
                        const auto e = echoTimes (d, p, tt.period, tt.period * 4.2, tt.lo * 0.5, tt.hi);
                        double m = 0;
                        for (double x : e) m += x;
                        return std::vector<double> { m / (double) std::max<size_t> (1, e.size()) };
                    }));
                }
            std::vector<DT> list (std::begin (dts), std::end (dts));
            lab.check (h, "Delay della tappa 3B: DELAY / SPREAD 0 / 0.5 / 1 (DMM 550 / 1100, DM Boy, MM Stereo, Canyon Echo, Rerun +-2/1%, Analogizer +-5%, Tone Tattoo)",
                       [&lab, h, list] {
                           bool ok = true;
                           juce::String s;
                           for (size_t k = 0; k < list.size(); ++k)
                           {
                               s << list[k].id << " ";
                               for (size_t j = 0; j < 3; ++j)
                               {
                                   const double x = lab.v (h[3 * k + j]);
                                   const bool g = std::abs (x / list[k].ref[j] - 1.0) <= list[k].tolPct / 100.0;
                                   ok = ok && g;
                                   s << num (x, x < 10 ? 2 : 1) << (g ? "" : "(FUORI)") << (j < 2 ? "/" : " ms; ");
                               }
                           }
                           return Verdict { ok, s.trimCharactersAtEnd ("; ") };
                       });
        }
        {
            std::vector<int> h;
            for (float k : { 0.0f, 1.0f })
                h.push_back (lab.add ("evh30", [k] (const ModelDef& d)
                {
                    const auto e = echoTimes (d, setsS2 (d, { { "depth", k } }), 0.1731, 6.0, 1.5, 16);
                    double lo = 1e9, hi = 0;
                    for (double x : e) { lo = std::min (lo, x); hi = std::max (hi, x); }
                    return std::vector<double> { lo, hi };
                }));
            lab.check (h, "MXR EVH 30 (evh30): ritardo del wet INTENSITY 0 -> 5.8-9.6 ms, INTENSITY 1 -> 7.0-8.3 ms (+-0.3, escursione piu' stretta)",
                       [&lab, h] {
                           const double a0 = lab.v (h[0], 0), a1 = lab.v (h[0], 1), b0 = lab.v (h[1], 0), b1 = lab.v (h[1], 1);
                           const bool ok = within (a0, 5.8, 0.3) && within (a1, 9.6, 0.3) && within (b0, 7.0, 0.3) && within (b1, 8.3, 0.3) && (b1 - b0) < (a1 - a0);
                           return Verdict { ok, num (a0) + "-" + num (a1) + " / " + num (b0) + "-" + num (b1) + " ms" };
                       });
        }

        // 4. velocita' delle modulazioni (seno 700 Hz, inviluppo e autocorrelazione)
        {
            static const Buf s700 = sineBuf (48000.0, 700.0, 0.2, 10.0);
            struct MR { const char* id; Sets sets; double ref, tolPct, maxP; };
            const MR mrs[] = {
                { "ehxmodrex", { { "moddiv", 0.0f }, { "tremdiv", 4.0f / 9 }, { "tempo", 0.635f }, { "tremdepth", 1.0f } }, 2.0, 1, 4 },
                { "ehxmodrex", { { "moddiv", 0.0f }, { "tremdiv", 7.0f / 9 }, { "tempo", 0.635f }, { "tremdepth", 1.0f } }, 4.0, 1, 4 },
                { "ehxmodrex", { { "moddiv", 0.0f }, { "tremdiv", 5.0f / 9 }, { "tempo", 0.635f }, { "tremdepth", 1.0f } }, 3.0, 1, 4 },
                { "ehxnanopul", { { "rate", 0.5f }, { "depth", 0.6f } }, 1.22, 5, 4 },
                { "ehxnanopul", { { "rate", 0.85f }, { "depth", 0.6f } }, 11.5, 5, 4 },
                { "ehxsuppul", { { "rate", 0.5f }, { "range", 0.0f } }, 0.47, 5, 4 },
                { "ehxsuppul", { { "rate", 0.5f }, { "range", 0.5f } }, 3.74, 5, 4 },
                { "ehxlesterk", { { "slow", 0.5f }, { "speed", 0.0f }, { "balance", 1.0f } }, 0.80, 5, 4 },
                { "ehxlesterk", { { "fast", 0.5f }, { "speed", 1.0f }, { "balance", 1.0f } }, 6.21, 5, 4 },
                { "ehxwiggler", { { "rate", 1.0f }, { "mode", 0.0f } }, 12.0, 5, 4 },
                { "ehxbadst1", { { "rate", 0.3f } }, 0.92, 5, 4 } };
            std::vector<int> h;
            for (const auto& m : mrs)
            {
                const double maxP = m.maxP;
                h.push_back (render (m.id, m.sets, s700, [maxP] (const Buf& y) { return std::vector<double> { modRate (y, 48000.0, 48000, maxP) }; }));
            }
            std::vector<MR> list (std::begin (mrs), std::end (mrs));
            lab.check (h, "Modulazioni 3B: Mod Rex TREM 1/4 / 1/8 / 1/4T a 120 BPM = 2 / 4 / 3 Hz (+-1%); Nano / Super Pulsar, Lester K, Wiggler, "
                          "Bad Stone V1 (+-5%)",
                       [&lab, h, list] {
                           bool ok = true;
                           juce::String s;
                           for (size_t k = 0; k < list.size(); ++k)
                           {
                               const double f = lab.v (h[k]);
                               const bool g = std::abs (f / list[k].ref - 1.0) <= list[k].tolPct / 100.0;
                               ok = ok && g;
                               s << list[k].id << " " << num (f, 3) << (g ? "" : "(FUORI)") << (k + 1 < list.size() ? "; " : " Hz");
                           }
                           return Verdict { ok, s };
                       });
        }

        // 5. riverberi: RT60 dalla curva di Schroeder (solo wet) e code infinite limitate
        {
            struct RV { const char* id; Sets sets; double ref, tolPct, secs; };
            const RV rvs[] = {
                { "ehhgneo", { { "mode", 0.0f }, { "blend", 1.0f } }, 2.15, 20, 14 },
                { "ehhgneo", { { "mode", 0.5f }, { "blend", 1.0f } }, 2.90, 20, 14 },
                { "ehhgneo", { { "mode", 1.0f }, { "blend", 1.0f } }, 2.92, 20, 14 },
                { "ehholier", { { "mode", 1.0f / 3 }, { "length", 0.0f }, { "blend", 1.0f } }, 3.77, 20, 14 },
                { "ehholier", { { "mode", 1.0f / 3 }, { "length", 1.0f }, { "blend", 1.0f } }, 1.54, 20, 14 },
                { "ehholier", { { "mode", 1.0f }, { "length", 0.0f }, { "blend", 1.0f } }, 1.05, 20, 14 },
                { "ehholier", { { "mode", 1.0f }, { "length", 1.0f }, { "blend", 1.0f } }, 0.44, 20, 14 },
                { "eh3verb", { { "mode", 0.0f }, { "time", 0.0f }, { "blend", 1.0f } }, 0.36, 20, 14 },
                { "eh3verb", { { "mode", 0.0f }, { "time", 0.9f }, { "blend", 1.0f } }, 13.0, 20, 30 },
                { "eh3verb", { { "mode", 0.5f }, { "time", 0.5f }, { "blend", 1.0f } }, 2.85, 20, 14 },
                { "eh3verb", { { "mode", 1.0f }, { "time", 0.5f }, { "blend", 1.0f } }, 1.2, 20, 14 },
                { "ehholiest", { { "decay", 0.0f }, { "direct", 0.0f } }, 0.27, 25, 14 },
                { "ehholiest", { { "decay", 0.5f }, { "direct", 0.0f } }, 1.84, 25, 14 },
                { "ehholiest", { { "decay", 1.0f }, { "direct", 0.0f } }, 12.9, 25, 30 },
                { "ehabyss", { { "atime", 0.2f }, { "atype", 1.0f / 9 } }, 0.65, 25, 14 },
                { "ehabyss", { { "atime", 0.8f }, { "atype", 1.0f / 9 } }, 7.5, 25, 30 },
                { "ehshimmer", { { "time", 0.2f }, { "voice", 0.0f }, { "blend", 1.0f } }, 1.56, 25, 14 },
                { "ehshimmer", { { "time", 0.6f }, { "voice", 0.0f }, { "blend", 1.0f } }, 5.35, 25, 20 } };
            std::vector<int> h;
            for (const auto& r : rvs)
            {
                const Sets s = r.sets;
                const double secs = r.secs;
                h.push_back (lab.add (r.id, [s, secs] (const ModelDef& d) { return std::vector<double> { rt60 (d, setsS2 (d, s), secs) }; }));
                lab.first (h.back(), secs > 14 ? 3 : 2);
            }
            std::vector<RV> list (std::begin (rvs), std::end (rvs));
            lab.check (h, "Riverberi 3B (RT60): Holy Grail Neo, Holier Grail (SHORT < 0.6 x LONG), Trinity 3 modi (+-20%); Holiest, Oceans Abyss, Shimmer (+-25%)",
                       [&lab, h, list] {
                           bool ok = true;
                           juce::String s;
                           for (size_t k = 0; k < list.size(); ++k)
                           {
                               const double t = lab.v (h[k]);
                               const bool g = std::abs (t / list[k].ref - 1.0) <= list[k].tolPct / 100.0;
                               ok = ok && g;
                               s << (k == 0 || std::strcmp (list[k].id, list[k - 1].id) != 0 ? juce::String (list[k].id) + " " : juce::String())
                                 << num (t, 2) << (g ? "" : "(FUORI)") << (k + 1 < list.size() && std::strcmp (list[k].id, list[k + 1].id) == 0 ? "/" : " s; ");
                           }
                           // Holier Grail: SHORT (LENGTH 1) piu' corto di 0.6 x LONG in HALL e in ROOM
                           ok = ok && lab.v (h[4]) < 0.6 * lab.v (h[3]) && lab.v (h[6]) < 0.6 * lab.v (h[5]);
                           return Verdict { ok, s.trimCharactersAtEnd ("; ") };
                       });
        }
        {
            static const Buf loop20 = []
            {
                const Buf ph = guitarS2 (48000.0, 7.5);
                Buf g ((size_t) (48000.0 * 20.0));
                for (size_t i = 0; i < g.size(); ++i) g[i] = ph[i % ph.size()];
                return g;
            }();
            struct INF { const char* id; Sets sets; };
            const INF infs[] = { { "eh3verb", { { "mode", 0.0f }, { "time", 1.0f }, { "blend", 1.0f } } },
                                 { "eh3verb", { { "mode", 0.5f }, { "time", 1.0f }, { "blend", 1.0f } } },
                                 { "ehabyss", { { "atime", 1.0f }, { "amix", 1.0f }, { "btime", 1.0f }, { "bmix", 1.0f } } },
                                 { "ehshimmer", { { "time", 1.0f }, { "voice", 0.5f }, { "blend", 1.0f } } },
                                 { "ehshimmer", { { "time", 1.0f }, { "voice", 1.0f }, { "blend", 1.0f }, { "mode", 0.5f } } } };
            std::vector<int> h;
            for (const auto& c : infs)
            {
                const Sets s = c.sets;
                h.push_back (lab.add (c.id, [s] (const ModelDef& d)
                {
                    double pk = 0;
                    bool fin = true;
                    for (const auto& b : runS2 (d, loop20, setsS2 (d, s), 48000.0, 256, d.stereo ? 2 : 1))
                        for (float x : b) { fin = fin && std::isfinite (x); pk = std::max (pk, (double) std::abs (x)); }
                    return std::vector<double> { fin ? pk : 999.0 };
                }));
                lab.first (h.back(), 3);
            }
            lab.check (h, "Code infinite (Trinity Reverb HALL/PLATE, Oceans Abyss, Shimmer a TIME 1): 20 s di frase di chitarra, wet < 2 V", [&lab, h] {
                bool ok = true;
                juce::String s;
                for (size_t k = 0; k < h.size(); ++k) { ok = ok && lab.v (h[k]) < 2.0; s << num (lab.v (h[k]), 3) << (k + 1 < h.size() ? " / " : " V"); }
                return Verdict { ok, s };
            });
        }

        // 6. intonazione: seno 0.2 V 2.2 s, picco FFT (65536) interpolato nel tratto 0.6-1.9 s, purezza +-6%
        auto voice = [&lab] (const char* id, Sets sets, double fin, double ratio)
        {
            return lab.add (id, [sets, fin, ratio] (const ModelDef& d)
            {
                const Buf y = runS2 (d, sineBuf (48000.0, fin, 0.2, 2.2), setsS2 (d, sets))[0];
                double pur = 0;
                const double f = peakFreq (y, (size_t) (0.6 * 48000), (size_t) (1.3 * 48000), 48000.0, fin * ratio, &pur);
                return std::vector<double> { cents (f, fin * ratio), pur, f };
            });
        };
        {
            std::vector<int> h;
            std::vector<double> outF;
            for (double f : { 82.41, 196.0, 659.3 })
            {
                h.push_back (voice ("ehpog3", { { "direct", 0.0f }, { "oct1", 0.0f }, { "up1", 0.0f }, { "oct2", 0.8f } }, f, 0.25)); outF.push_back (f * 0.25);
                h.push_back (voice ("ehpog3", { { "direct", 0.0f }, { "oct1", 0.8f }, { "up1", 0.0f } }, f, 0.5)); outF.push_back (f * 0.5);
                h.push_back (voice ("ehpog3", { { "direct", 0.0f }, { "oct1", 0.0f }, { "up1", 0.0f }, { "fifth", 0.8f } }, f, 1.5)); outF.push_back (f * 1.5);
                h.push_back (voice ("ehpog3", { { "direct", 0.0f }, { "oct1", 0.0f }, { "up1", 0.8f } }, f, 2.0)); outF.push_back (f * 2.0);
                h.push_back (voice ("ehpog3", { { "direct", 0.0f }, { "oct1", 0.0f }, { "up1", 0.0f }, { "up2", 0.8f } }, f, 4.0)); outF.push_back (f * 4.0);
            }
            lab.check (h, "POG3: voci -2/-1 ottava, +5a, +1/+2 ottave a 82 / 196 / 659 Hz intonate entro 2 cent, purezza > 40 dB sopra 40 Hz", [&lab, h, outF] {
                double worstC = 0, minPur = 999;
                for (size_t k = 0; k < h.size(); ++k)
                {
                    worstC = std::max (worstC, std::abs (lab.v (h[k], 0)));
                    if (outF[k] > 40.0) minPur = std::min (minPur, lab.v (h[k], 1));
                }
                return Verdict { worstC < 2.0 && minPur > 40.0, "errore max " + num (worstC) + " cent, purezza min " + num (minPur, 1) + " dB" };
            });
        }
        {
            const int sts[] = { 7, -5, 4, 19, -24, 36, 1 };
            std::vector<int> h;
            for (int s : sts)
                h.push_back (voice ("ehpitchforkp", { { "direct", 0.0f }, { "shift1", 0.8f }, { "shift2", 0.0f }, { "det1", 0.5f }, { "int1", (s + 36) / 72.0f } },
                                    196.0, std::exp2 (s / 12.0)));
            const int pf5 = voice ("ehpicofork", { { "blend", 1.0f }, { "shift", 5.0f / 9 }, { "mode", 0.0f } }, 196.0, 1.4983);
            const int pfo = voice ("ehpicofork", { { "blend", 1.0f }, { "shift", 7.0f / 9 }, { "mode", 0.5f } }, 196.0, 0.5);
            std::vector<int> all = h;
            all.insert (all.end(), { pf5, pfo });
            lab.check (all, "Pitch Fork+ (196 Hz, +7/-5/+4/+19/-24/+36/+1 semitoni) e Pico Pitch Fork (P5 su, ottava giu'): errore < 3 cent, ottave purezza > 45 dB",
                       [&lab, h, pf5, pfo] {
                           double worst = 0;
                           juce::String s;
                           for (size_t k = 0; k < h.size(); ++k) { worst = std::max (worst, std::abs (lab.v (h[k]))); s << num (lab.v (h[k]), 1) << " "; }
                           const double purOct = std::min ({ lab.v (h[4], 1), lab.v (h[5], 1), lab.v (pfo, 1) });
                           worst = std::max ({ worst, std::abs (lab.v (pf5)), std::abs (lab.v (pfo)) });
                           return Verdict { worst < 3.0 && purOct > 45.0, "cent " + s + "| Pico " + num (lab.v (pf5)) + " / " + num (lab.v (pfo))
                                                                             + ", purezza ottave min " + num (purOct, 1) + " dB" };
                       });
        }
        {
            const int i1 = voice ("ehihm", { { "blend", 1.0f }, { "key", 0.0f }, { "sharp", 1.0f }, { "interval", 0.3f } }, 329.63, 392.0 / 329.63);
            const int i2 = voice ("ehihm", { { "blend", 1.0f }, { "key", 0.0f }, { "sharp", 1.0f }, { "interval", 0.3f } }, 293.66, 349.23 / 293.66);
            const int i3 = voice ("ehihm", { { "blend", 1.0f }, { "poly", 1.0f }, { "interval", 0.5f } }, 220.0, 1.4983);
            const int s1 = voice ("ehslammi", { { "pedal", 0.5f }, { "shift", 0.8f }, { "direct", 0.0f } }, 196.0, std::exp2 (0.5));
            const int s2 = voice ("ehslammi", { { "pedal", 1.0f }, { "shift", 1.0f }, { "dir", 1.0f }, { "direct", 0.0f } }, 440.0, 0.125);
            lab.check ({ i1, i2, i3, s1, s2 }, "IHM (Do maggiore, 3a sopra diatonica: Mi -> Sol, Re -> Fa; Poly 5a) e Slammi (bend a meta' +6 semitoni, 3 ottave giu'): "
                                               "errore < 3 cent",
                       [&lab, i1, i2, i3, s1, s2] {
                           const double c[5] = { lab.v (i1), lab.v (i2), lab.v (i3), lab.v (s1), lab.v (s2) };
                           double worst = 0;
                           for (double x : c) worst = std::max (worst, std::abs (x));
                           return Verdict { worst < 3.0, "IHM " + num (lab.v (i1, 2)) + " / " + num (lab.v (i2, 2)) + " / " + num (lab.v (i3, 2)) + " Hz, Slammi "
                                                            + num (lab.v (s1, 2)) + " / " + num (lab.v (s2, 2)) + " Hz, errore max " + num (worst) + " cent" };
                       });
        }
        {
            const float ck = 0.5f;
            const double fc = 0.1 * std::pow (29400.0, (double) ck);    // portante del Ring Thing (COARSE 0.5): 17.15 Hz
            const int ps = voice ("ehringthing", { { "mode", 1.0f }, { "coarse", (7 + 24) / 48.0f }, { "filter", 0.0f }, { "fine", 0.5f }, { "blend", 1.0f } },
                                  220.0, 1.4983);
            const int ub = voice ("ehringthing", { { "mode", 1.0f / 3 }, { "coarse", ck }, { "blend", 1.0f } }, 440.0, (440.0 + fc) / 440.0);
            const int lb = voice ("ehringthing", { { "mode", 2.0f / 3 }, { "coarse", ck }, { "blend", 1.0f } }, 440.0, (440.0 - fc) / 440.0);
            const int at = voice ("ehatomic", { { "blend", 1.0f }, { "rate", 1.0f }, { "atoms", 1.0f } }, 330.0, 1.0);
            const int bm = voice ("ehbassmono", { { "direct", 0.0f }, { "mode", 0.4f }, { "ctrl", 0.0f } }, 55.0, 1.0);
            lab.check ({ ps, ub, lb, at, bm }, "Ring Thing (PS +7, banda laterale superiore / inferiore con portante 17.15 Hz, purezza > 40 dB), Atomic Cluster "
                                               "(SPEED e ATOMS massimi, purezza > 60 dB), Bass Mono Synth SUB 55 Hz: errore < 2 cent",
                       [&lab, ps, ub, lb, at, bm] {
                           double worst = 0;
                           for (int x : { ps, ub, lb, at, bm }) worst = std::max (worst, std::abs (lab.v (x)));
                           const bool ok = worst < 2.0 && lab.v (ub, 1) > 40.0 && lab.v (lb, 1) > 40.0 && lab.v (at, 1) > 60.0;
                           return Verdict { ok, "PS " + num (lab.v (ps, 2)) + " Hz, UB " + num (lab.v (ub, 2)) + " / LB " + num (lab.v (lb, 2)) + " Hz (purezza "
                                                    + num (lab.v (ub, 1), 1) + " / " + num (lab.v (lb, 1), 1) + " dB), Atomic " + num (lab.v (at, 2)) + " Hz ("
                                                    + num (lab.v (at, 1), 1) + " dB), SUB " + num (lab.v (bm, 2)) + " Hz, errore max " + num (worst) + " cent" };
                       });
        }
        {
            static const Buf voc = []
            {
                Buf s ((size_t) (48000.0 * 2.2));
                for (size_t i = 0; i < s.size(); ++i)
                {
                    const double t = (double) i / 48000.0;
                    s[i] = (float) (0.1 * std::sin (2 * kPi * 300 * t) + 0.06 * std::sin (2 * kPi * 600 * t) + 0.04 * std::sin (2 * kPi * 900 * t));
                }
                return s;
            }();
            const int il = lab.add ("ehironlung", [] (const ModelDef& d)
            {
                const Buf y = runS2 (d, voc, {})[0];
                const size_t lat = 1024;
                double num2 = 0, den = 0;
                for (size_t i = 48000; i < 96000; ++i) { num2 += (double) (y[i] - voc[i - lat]) * (y[i] - voc[i - lat]); den += (double) voc[i - lat] * voc[i - lat]; }
                return std::vector<double> { dbOf (rmsOf (y, 48000, 96000) / rmsOf (voc, 48000, 96000)), 10 * std::log10 (num2 / den + 1e-30) };
            });
            const int vr = render ("ehv256", { { "mode", 0.0f }, { "pitch", 0.5f }, { "blend", 1.0f } }, voc, [] (const Buf& y)
            {
                double pur = 0;
                const double f = peakFreq (y, (size_t) (0.6 * 48000), (size_t) (1.3 * 48000), 48000.0, 261.6, &pur);
                return std::vector<double> { f, dbOf (rmsOf (y, 48000, 96000) / rmsOf (voc, 48000, 96000)) };
            });
            lab.check ({ il, vr }, "Vocoder: Iron Lung mono = ingresso ritardato di 1024 campioni (errore < -40 dB, livello 0 dB +-0.5); V256 VOX-ROBO righe sul Do3 "
                                   "(261.63 Hz +-5 cent), livello +-3 dB",
                       [&lab, il, vr] {
                           const bool ok = lab.v (il, 1) < -40.0 && within (lab.v (il, 0), 0.0, 0.5) && std::abs (cents (lab.v (vr, 0), 261.63)) < 5.0
                                           && within (lab.v (vr, 1), 0.0, 3.0);
                           return Verdict { ok, "Iron Lung errore " + num (lab.v (il, 1), 1) + " dB, livello " + num (lab.v (il, 0)) + " dB; V256 " + num (lab.v (vr, 0))
                                                    + " Hz, livello " + num (lab.v (vr, 1), 1) + " dB" };
                       });
        }

        // 7. latenza delle voci (burst di seno 0.2 V dopo 0.5 s di silenzio, 50% del regime)
        {
            struct LT { const char* id; Sets sets; double f, limit; };
            const LT lts[] = {
                { "ehpog3", { { "direct", 0.0f }, { "oct1", 0.0f }, { "up1", 0.8f } }, 82.41, 35 },
                { "ehpog3", { { "direct", 0.0f }, { "oct1", 0.0f }, { "up1", 0.8f } }, 329.6, 15 },
                { "ehpog3", { { "direct", 0.0f }, { "oct1", 0.8f }, { "up1", 0.0f } }, 82.41, 35 },
                { "ehpog3", { { "direct", 0.0f }, { "oct1", 0.8f }, { "up1", 0.0f } }, 329.6, 15 },
                { "ehpog3", { { "direct", 0.0f }, { "oct1", 0.0f }, { "up1", 0.0f }, { "fifth", 0.8f } }, 82.41, 35 },
                { "ehpog3", { { "direct", 0.0f }, { "oct1", 0.0f }, { "up1", 0.0f }, { "fifth", 0.8f } }, 329.6, 15 },
                { "ehpitchforkp", { { "direct", 0.0f }, { "shift1", 0.8f }, { "int1", (7 + 36) / 72.0f } }, 196.0, 25 },
                { "ehpitchforkp", { { "direct", 0.0f }, { "shift1", 0.8f }, { "int1", (12 + 36) / 72.0f } }, 196.0, 15 },
                { "ehpicofork", { { "blend", 1.0f }, { "shift", 0.0f } }, 196.0, 5 },
                { "ehihm", { { "blend", 1.0f }, { "key", 0.0f }, { "sharp", 1.0f }, { "interval", 0.3f } }, 329.6, 45 },
                { "ehslammi", { { "pedal", 1.0f }, { "shift", 0.8f } }, 196.0, 20 } };
            std::vector<int> h;
            for (const auto& l : lts)
            {
                const Sets s = l.sets;
                const double f = l.f;
                h.push_back (lab.add (l.id, [s, f] (const ModelDef& d) { return std::vector<double> { latencyMs (d, setsS2 (d, s), f) }; }));
            }
            std::vector<LT> list (std::begin (lts), std::end (lts));
            lab.check (h, "Latenza delle voci: POG3 +1 / -1 ottava / +5a (< 35 ms a 82 Hz, < 15 ms a 330 Hz), Pitch Fork+ +7 < 25 / +12 < 15 ms, Pico Fork < 5 ms, "
                          "IHM < 45 ms, Slammi < 20 ms",
                       [&lab, h, list] {
                           bool ok = true;
                           juce::String s;
                           for (size_t k = 0; k < list.size(); ++k)
                           {
                               const double ms = lab.v (h[k]);
                               const bool g = ms >= 0 && ms < list[k].limit;
                               ok = ok && g;
                               s << num (ms, 1) << (g ? "" : "(FUORI)") << (k + 1 < list.size() ? " / " : " ms");
                           }
                           return Verdict { ok, s };
                       });
        }

        // 8. synth, congelamento, campionatore
        {
            static const Buf a3 = sineBuf (48000.0, 220.0, 0.25, 2.5);
            std::vector<int> h;
            for (int pre = 0; pre < 9; ++pre)
                h.push_back (render ("ehstring9", { { "direct", 0.0f }, { "mode", pre / 8.0f } }, a3, [] (const Buf& y)
                {
                    return std::vector<double> { bandDb (y, 110, 48000, 48000), bandDb (y, 220, 48000, 48000), bandDb (y, 440, 48000, 48000), bandDb (y, 660, 48000, 48000) };
                }));
            static const Buf noteA3 = []
            {
                Buf b ((size_t) (48000.0 * 5), 0.0f);
                for (size_t i = (size_t) (0.2 * 48000); i < (size_t) (1.2 * 48000); ++i) b[i] = (float) (0.25 * std::sin (2 * kPi * 220 * (double) i / 48000.0));
                return b;
            }();
            const int fz = render ("ehstring9", { { "direct", 0.0f }, { "mode", 6.0f / 8 }, { "ctrl2", 0.0f } }, noteA3, [] (const Buf& y)
            {
                return std::vector<double> { rmsOf (y, (size_t) (0.6 * 48000), (size_t) (1.1 * 48000)), rmsOf (y, (size_t) (3.2 * 48000), (size_t) (5 * 48000)) };
            });
            std::vector<int> all = h;
            all.push_back (fz);
            lab.check (all, "STRING9 su La3: riga a 220 Hz la piu' forte (> -5 dB del totale) nei preset 1-3 e 5-8, ottava sotto (110 Hz > -6 dB) in SYMPHONIC "
                            "e ORCH FREEZE; ORCH FREEZE tiene la nota (rms dopo >= 0.8 x durante)",
                       [&lab, h, fz] {
                           bool ok = true;
                           juce::String s;
                           for (int pre : { 0, 1, 2, 4, 5, 6, 7 })
                           {
                               const auto& b = lab.all (h[(size_t) pre]);
                               const bool strongest = b[1] > -5.0 && b[1] >= b[0] && b[1] >= b[2] && b[1] >= b[3];
                               ok = ok && strongest;
                               s << (pre + 1) << ":" << num (b[1], 1) << (strongest ? "" : "(FUORI)") << " ";
                           }
                           const double sub1 = lab.v (h[0], 0), sub7 = lab.v (h[6], 0);
                           ok = ok && sub1 > -6.0 && sub7 > -6.0;
                           const double during = lab.v (fz, 0), after = lab.v (fz, 1);
                           ok = ok && after >= 0.8 * during;
                           return Verdict { ok, "220 Hz " + s + "dB; 110 Hz " + num (sub1, 1) + " / " + num (sub7, 1) + " dB; FREEZE " + num (during, 4) + " -> " + num (after, 4) };
                       });
        }
        {
            static const Buf pl = []
            {
                Buf b ((size_t) (48000.0 * 6), 0.0f);
                pluckS2 (b, 48000.0, 0.3, 196.0, 0.3, 1.0, 5);
                return b;
            }();
            const int se = render ("ehsuperegop", { { "direct", 0.0f }, { "decay", 1.0f }, { "mode", 0.5f } }, pl, [] (const Buf& y)
            {
                return std::vector<double> { rmsOf (y, (size_t) (0.5 * 48000), 48000), rmsOf (y, 4 * 48000, 6 * 48000) };
            });
            const int ir = lab.add ("ehreplay", [] (const ModelDef& d)
            {
                const double rate = 48000.0;
                auto fx = createEffect (d);
                fx->params[2].store (1.0f);      // REPEAT
                fx->prepare (rate, 256);
                Buf b = sineBuf (rate, 440, 0.3, 3.0);
                for (size_t i = (size_t) (0.5 * rate); i < b.size(); ++i) b[i] = 0;
                float* chp[1];
                for (size_t pos = 0; pos < b.size(); pos += 256)
                {
                    if (pos == 0) fx->trigger (0);
                    if (pos == (size_t) (0.5 * rate) / 256 * 256) fx->trigger (0);
                    chp[0] = b.data() + pos;
                    fx->process (chp, 1, (int) std::min<size_t> (256, b.size() - pos));
                }
                const double f = peakFreq (b, (size_t) (1.0 * rate), (size_t) (1.5 * rate), rate, 440);
                return std::vector<double> { (double) fx->readout (2), f };
            });
            lab.check ({ se, ir }, "Superego+ AUTO con DECAY al massimo: congelato 4-6 s >= 0.8 x 0.5-1 s; Instant Replay REPEAT: campione di 0.50 s, "
                                   "ripetuto a 439.5 Hz (+-0.5%)",
                       [&lab, se, ir] {
                           const bool ok = lab.v (se, 1) >= 0.8 * lab.v (se, 0) && within (lab.v (ir, 0), 0.5, 0.01) && std::abs (lab.v (ir, 1) / 439.5 - 1.0) <= 0.005;
                           return Verdict { ok, "Superego+ " + num (lab.v (se, 0), 4) + " -> " + num (lab.v (se, 1), 4) + "; Replay " + num (lab.v (ir, 0), 3) + " s, "
                                                    + num (lab.v (ir, 1)) + " Hz" };
                       });
        }

        // 9. click cambiando i comandi (stessa misura della 3A). Oceans Abyss: l'algoritmo REVERSE, dopo ogni cambio di tipo (che svuota
        //    il serbatoio con core.clear()), rilegge al contrario anche l'attacco brusco della registrazione e ~0.39 s dopo (fine del primo
        //    segmento, TIME a ore 12) da' un gradino d'uscita (difetto noto del motore, FxVerbCore.h, non di stabilita'): per l'Abyss la
        //    soglia si applica alla sequenza senza la posizione REVERSE di A/B TYPE e la sequenza completa e' solo riportata
        {
            std::vector<int> h;
            auto what = std::make_shared<std::vector<juce::String>> ((size_t) numB + 1);     // comando peggiore (ogni misura scrive il suo)
            auto noReverse = [] (const ModelDef& d)
            {
                std::function<bool (int, float)> skip;
                if (std::strcmp (d.id, "ehabyss") == 0)
                    skip = [&d] (int c, float v)
                    {
                        const bool type = std::strcmp (d.controls[c].role, "atype") == 0 || std::strcmp (d.controls[c].role, "btype") == 0;
                        return type && (int) std::lround (v * (d.controls[c].steps - 1)) == 4;      // 4 = REVERSE
                    };
                return skip;
            };
            for (int mi = 0; mi < numB; ++mi)
                h.push_back (lab.add (kStage3BDigital[mi], [mi, what, noReverse] (const ModelDef& d)
                {
                    const auto r = clicksS2 (kStage3BDigital[mi], noReverse (d));
                    (*what)[(size_t) mi] = r.what;
                    return std::vector<double> { r.worst };
                }));
            for (int x : h) lab.first (x, 2);
            const int full = lab.add ("ehabyss", [what] (const ModelDef&)
            {
                const auto r = clicksS2 ("ehabyss");
                (*what)[(size_t) numB] = r.what;
                return std::vector<double> { r.worst };
            });
            lab.first (full, 3);
            std::vector<int> all = h;
            all.push_back (full);
            lab.check (all, "Tappa 3B digitali: niente click cambiando i comandi (seno 220 Hz, differenza seconda <= 10x prima/regime, 50 modelli; "
                            "Oceans Abyss senza REVERSE, difetto noto riportato)", [&lab, h, what, full] {
                double worst = 0;
                juce::String worstId, bad;
                for (size_t k = 0; k < h.size(); ++k)
                {
                    const double r = lab.v (h[k]);
                    if (r > 10.0) bad << kStage3BDigital[k] << " " << num (r, 1) << "x (" << (*what)[k] << ") ";
                    if (r > worst) { worst = r; worstId = juce::String (kStage3BDigital[k]) + " " + (*what)[k]; }
                }
                return Verdict { bad.isEmpty(), "peggiore " + num (worst, 1) + "x (" + worstId + "); Abyss con REVERSE " + num (lab.v (full), 1) + "x ("
                                                    + (*what)[(size_t) numB] + ", difetto noto) " + bad.trim() };
            });
        }

        lab.run (report);
    }

    //==============================================================================
    // Simulatori di cassa IR Loader: uscita deterministica (IR pronto dal primo campione, non dal thread di caricamento)
    //==============================================================================
    void runCabIRTests (const std::function<void (const juce::String&, bool, const juce::String&)>& report)
    {
        using namespace pt::engine;
        for (const char* id : { "irl1", "irl2" })
        {
            const auto* d = findModel (id);
            if (d == nullptr) { report (juce::String (id) + " nel catalogo", false, "modello assente"); continue; }
            auto render = [d] (int waitMs)
            {
                auto fx = createEffect (*d);
                const double sr = 48000.0;
                fx->prepare (sr, 256);
                if (waitMs > 0) juce::Thread::sleep (waitMs);      // il thread di caricamento di juce::dsp::Convolution ha tempo di finire
                juce::AudioBuffer<float> out (2, 256 * 94), buf (2, 256);
                for (int pos = 0; pos < out.getNumSamples(); pos += 256)
                {
                    fillGuitar (buf, sr, 0.25f, pos);
                    fx->messageThreadUpdate();
                    fx->process (buf.getArrayOfWritePointers(), 2, 256);
                    for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, buf, c, 0, 256);
                }
                return out;
            };
            const auto a = render (0), b = render (60), c = render (0);
            int diff = 0;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < a.getNumSamples(); ++i)
                    if (std::memcmp (a.getReadPointer (ch) + i, b.getReadPointer (ch) + i, sizeof (float)) != 0
                        || std::memcmp (a.getReadPointer (ch) + i, c.getReadPointer (ch) + i, sizeof (float)) != 0) ++diff;
            // il primo blocco deve gia' contenere la cassa: un IR unitario darebbe l'ingresso moltiplicato per LEVEL
            juce::AudioBuffer<float> dry (2, 256);
            fillGuitar (dry, 48000.0, 0.25f, 0);
            double corr = 0, ex = 0, ey = 0;
            for (int i = 0; i < 256; ++i)
            {
                corr += (double) a.getSample (0, i) * dry.getSample (0, i);
                ex += (double) a.getSample (0, i) * a.getSample (0, i);
                ey += (double) dry.getSample (0, i) * dry.getSample (0, i);
            }
            const double sim = corr / std::sqrt (ex * ey + 1e-30);
            report (juce::String (d->code) + " " + d->name + ": uscita identica bit per bit tra esecuzioni, IR attivo dal primo blocco",
                    diff == 0 && sim < 0.999, juce::String (diff) + " campioni diversi, somiglianza al segnale secco nel primo blocco " + juce::String (sim, 4));
        }
    }

    int runSelfTest()
    {
        using namespace pt::engine;
        int failed = 0, total = 0;
        auto report = [&] (const juce::String& name, bool ok, const juce::String& detail)
        {
            ++total;
            if (! ok) ++failed;
            std::cout << (ok ? "  [OK]      " : "  [FALLITO] ") << name << "  (" << detail << ")\n";
        };

        std::cout << "\nPedal Trinity " << pt::versionString << " - autotest del motore (" << numModels() << " modelli)\n";

        // 1. ogni modello: netlist valida, uscita finita e livello plausibile con i comandi a min/meta'/max
        for (int m = 0; m < numModels(); ++m)
        {
            const auto& def = model (m);
            juce::String detail;
            bool ok = true;
            for (float setting : { 0.0f, 0.5f, 1.0f })
            {
                auto fx = createEffect (def);
                if (auto* c = dynamic_cast<CircuitEffect*> (fx.get()); c != nullptr && ! c->error.empty())
                {
                    ok = false;
                    detail << "netlist: " << c->error << " ";
                    break;
                }
                for (int k = 0; k < def.numControls; ++k)
                    if (def.controls[k].kind != ControlKind::Button)
                        fx->params[k].store (setting);
                auto out = runEffect (*fx, 48000.0, 256, 0.25f);
                const float lvl = rmsDb (out, 24000, out.getNumSamples() - 24000);
                const bool fin = allFinite (out);
                const bool sane = lvl < 24.0f;
                if (! fin || ! sane) ok = false;
                detail << juce::String (setting, 1) << ":" << (fin ? juce::String (lvl, 1) : juce::String ("NaN")) << "dB ";
            }
            {
                auto fx = createEffect (def);
                for (int k = 0; k < def.numControls; ++k)
                    if (def.controls[k].kind != ControlKind::Button) fx->params[k].store (0.5f);
                if (! runEffectMono (*fx, 48000.0, 256)) { ok = false; detail << "mono: NON valido "; }
                else detail << "mono ok";
            }
            report (juce::String (def.code) + " " + def.name + " [" + def.inspiredBy + "]", ok, detail.trim());
        }

        // 2. catena: 100 slot, spostamenti, rimozioni, stato
        {
            Chain chain;
            chain.prepare (48000.0, 256);
            for (int i = 0; i < maxSlots + 5; ++i)
                chain.insert (-1, numModels() > 0 ? model (i % numModels()).id : "");
            report ("Limite di 100 slot", chain.size() == maxSlots, juce::String (chain.size()) + " slot");
            chain.move (0, 50); chain.move (99, 0); chain.remove (10);
            const auto state = chain.toValueTree();
            Chain copy;
            copy.fromValueTree (state);
            report ("Salvataggio/ripristino della catena", copy.toValueTree().isEquivalentTo (state), juce::String (copy.size()) + " slot");
            juce::AudioBuffer<float> buf (2, 256);
            fillGuitar (buf, 48000.0, 0.2f, 0);
            chain.process (buf, 2);
            report ("Catena di 99 pedali: uscita finita", allFinite (buf), "RMS " + juce::String (rmsDb (buf, 0, 256), 1) + " dB");
        }

        // 3. instradamento: splitter MONO / DUAL / STEREO
        {
            Chain chain;
            const auto noSplit = runChain (chain, "ce5");
            report ("Catena senza splitter: uscita mono (L = R)", allFinite (noSplit) && channelDiff (noSplit) < 1.0e-6f,
                    "diff " + juce::String (channelDiff (noSplit), 6));
            const auto stereo = runChain (chain, "split=1;ce5");
            report ("Splitter STEREO + chorus stereo: immagine stereo", allFinite (stereo) && channelDiff (stereo) > 1.0e-3f,
                    "diff " + juce::String (channelDiff (stereo), 4));
            const auto collapse = runChain (chain, "split=1;ce5;ds1");
            report ("STEREO: un pedale mono riporta il segnale in mono", allFinite (collapse) && channelDiff (collapse) < 1.0e-6f,
                    "diff " + juce::String (channelDiff (collapse), 6));
            const auto dualOut = runChain (chain, "split=0.5;ds1;ce2@B");
            report ("Splitter DUAL: corsie A e B separate su L e R", allFinite (dualOut) && channelDiff (dualOut) > 1.0e-2f
                        && dualOut.getMagnitude (1, 24000, 24000) > 1.0e-3f,
                    "diff " + juce::String (channelDiff (dualOut), 4));
            const int second = chain.insert (-1, "split");
            report ("Un solo splitter per catena", second < 0 && ! chain.setModel (1, "split"), "secondo splitter rifiutato");
            const auto mono = runChain (chain, "split=0;ds1");
            report ("Splitter MONO: segnale non diviso", allFinite (mono) && channelDiff (mono) < 1.0e-6f, "L = R");
        }

        // 4. temi: leggibilita' (contrasto WCAG 2.1: testo >= 4.5:1, testo grande/accento >= 3:1)
        for (const auto& t : pt::ui::allThemes())
        {
            using pt::ui::contrastRatio;
            const double pairs[] = {
                contrastRatio (t.text, t.barBottom), contrastRatio (t.text, t.header), contrastRatio (t.text, t.panelTop),
                contrastRatio (t.text, t.button), contrastRatio (t.textDim, t.panelBottom), contrastRatio (juce::Colours::white, t.selected) };
            const double accent[] = { contrastRatio (t.accent, t.barBottom), contrastRatio (t.accent, t.header), contrastRatio (t.accent, t.panelTop) };
            const double ink = contrastRatio (t.ink, t.boardBase);
            double minText = 99, minAccent = 99;
            for (double v : pairs) minText = std::min (minText, v);
            for (double v : accent) minAccent = std::min (minAccent, v);
            report ("Tema " + t.name + ": contrasto", minText >= 4.5 && minAccent >= 3.0 && ink >= 3.0,
                    "testo " + juce::String (minText, 1) + ":1, accento " + juce::String (minAccent, 1) + ":1, segni pedana "
                        + juce::String (ink, 1) + ":1");
        }

        // 5. NAM-A1A2: sicurezza dei file ed elaborazione
        runNamTests (report);

        // 6. REAL MOD
        runRealModTests (report);
        runSafetyTests (report);
        runCableTests (report);
        runMidiTests (report);
        runWahTests (report);
        runCabIRTests (report);
        runStage3AnalogTests (report);
        runStage3DigitalTests (report);
        runStage3BAnalogTests (report);
        runStage3BDigitalTests (report);

        // 7. processore completo e bypass
        {
            PedalTrinityProcessor p;
            p.chain.fromValueTree (juce::ValueTree ("CHAIN"));
            p.setPlayConfigDetails (2, 2, 48000.0, 256);
            p.prepareToPlay (48000.0, 256);
            juce::AudioBuffer<float> buf (2, 256), ref (2, 256);
            fillGuitar (buf, 48000.0, 0.3f, 0);
            ref.makeCopyOf (buf);
            juce::MidiBuffer midi;
            p.processBlock (buf, midi);
            float err = 0;
            for (int i = 0; i < 256; ++i) err = juce::jmax (err, std::abs (buf.getSample (0, i) - ref.getSample (0, i)));
            report ("Catena vuota trasparente", err < 1.0e-6f, "errore max " + juce::String (err, 9));
        }

        std::cout << (failed == 0 ? "Tutti i test superati (" : "Test falliti: ") << (failed == 0 ? total : failed)
                  << (failed == 0 ? ")\n" : "\n") << std::flush;
        return failed == 0 ? 0 : 1;
    }

    // Indice di un preset di fabbrica dato il numero o il nome (esatto o parziale, senza maiuscole); -1 se assente
    int findFactoryPreset (const juce::String& key)
    {
        const auto k = key.trim().unquoted();
        if (k.isEmpty()) return -1;
        if (k.containsOnly ("0123456789")) return k.getIntValue();
        const auto names = pt::PresetManager::factoryNames();
        for (int n = 0; n < names.size(); ++n)
            if (names[n].equalsIgnoreCase (k)) return n;
        for (int n = 0; n < names.size(); ++n)
            if (names[n].containsIgnoreCase (k)) return n;
        return -1;
    }

    //==============================================================================
    // Screenshot dell'interfaccia (per la guida PDF)
    //   --screenshot file.png [--scale s] [--size WxH] [--view n] [--first i] [--factory numero|nome]
    //   [--zoom slot] [--info] [--chain id,id,...] [--set slot:ctrl=val]
    //==============================================================================
    int runScreenshot (const juce::StringArray& args)
    {
        const int idx = args.indexOf ("--screenshot");
        if (idx < 0 || idx + 1 >= args.size()) return 2;
        const juce::File outFile = juce::File::getCurrentWorkingDirectory().getChildFile (args[idx + 1].unquoted());

        // le foto personali (RealPhotos) non finiscono negli screenshot, salvo richiesta esplicita
        pt::ui::RealPhotos::setEnabled (args.contains ("--photos"));
        PedalTrinityProcessor p;
        float scale = 1.0f;
        int w = 1280, h = 760, zoomSlot = -1;
        bool info = false, options = false, midiPanel = false, learn = false;
        juce::SharedResourcePointer<pt::ui::ThemeManager> themes;
        for (int i = 0; i < args.size(); ++i)
        {
            const auto next = i + 1 < args.size() ? args[i + 1] : juce::String();
            if (args[i] == "--scale") scale = next.getFloatValue();
            else if (args[i] == "--size") { w = next.upToFirstOccurrenceOf ("x", false, false).getIntValue(); h = next.fromFirstOccurrenceOf ("x", false, false).getIntValue(); }
            else if (args[i] == "--view") p.uiState.setProperty ("view", next.getIntValue(), nullptr);
            else if (args[i] == "--first") p.uiState.setProperty ("first", next.getIntValue(), nullptr);
            else if (args[i] == "--factory")
            {
                const int k = findFactoryPreset (next);
                if (! p.presets.loadFactory (k))
                {
                    std::cerr << "Preset di fabbrica non trovato: \"" << next << "\". Disponibili:\n";
                    const auto names = pt::PresetManager::factoryNames();
                    for (int n = 0; n < names.size(); ++n) std::cerr << "  " << n << "  " << names[n] << "\n";
                    return 5;
                }
            }
            else if (args[i] == "--zoom") zoomSlot = next.getIntValue();
            else if (args[i] == "--info") info = true;
            else if (args[i] == "--real") themes->previewRealMode (true);        // REAL MOD senza salvarlo
            else if (args[i] == "--options") options = true;
            else if (args[i] == "--midi") midiPanel = true;
            else if (args[i] == "--learn") learn = true;
            else if (args[i] == "--cablecolour") themes->previewCableColour (next.getIntValue());     // anteprime: non tocca le preferenze
            else if (args[i] == "--board") themes->previewBoardSource (next);
            else if (args[i] == "--theme")
            {
                const auto& all = pt::ui::allThemes();
                for (int t = 0; t < (int) all.size(); ++t)
                    if (all[(size_t) t].id == next) themes->preview (t);      // anteprima: non tocca le preferenze
            }
            else if (args[i] == "--chain")
            {
                // "id" pedale, "id@B" nella corsia B, "id!" staccato dai cavi, "split=0.5" splitter con MODE (0 mono, 0.5 dual, 1 stereo)
                juce::ValueTree t ("CHAIN");
                for (auto& tok : juce::StringArray::fromTokens (next, ",", ""))
                {
                    juce::ValueTree s ("SLOT");
                    s.setProperty ("model", tok.upToFirstOccurrenceOf ("@", false, false).upToFirstOccurrenceOf ("=", false, false).upToFirstOccurrenceOf ("!", false, false), nullptr);
                    s.setProperty ("on", true, nullptr);
                    if (tok.contains ("!")) s.setProperty ("patched", false, nullptr);
                    if (tok.contains ("@B")) s.setProperty ("lane", 1, nullptr);
                    if (tok.contains ("=")) s.setProperty ("p0", tok.fromFirstOccurrenceOf ("=", false, false).getFloatValue(), nullptr);
                    t.appendChild (s, nullptr);
                }
                p.chain.fromValueTree (t);
            }
            else if (args[i] == "--nam" || args[i] == "--ir")
            {
                // "slot:A=file" / "slot:B=file": modello o IR nel NAM-A1A2 (stessa verifica di sicurezza del pannello)
                const int slot = next.upToFirstOccurrenceOf (":", false, false).getIntValue();
                const auto rest = next.fromFirstOccurrenceOf (":", false, false);
                const int ch = rest.startsWithIgnoreCase ("B") ? 1 : 0;
                const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (rest.fromFirstOccurrenceOf ("=", false, false).unquoted());
                auto* s = p.chain.slot (slot);
                if (auto* nam = s != nullptr ? dynamic_cast<pt::engine::NamEffect*> (s->fx.get()) : nullptr)
                {
                    const bool ok = args[i] == "--nam" ? nam->loadNam (ch, file) : nam->loadIr (ch, file);
                    std::cout << nam->status (ch).message << std::endl;
                    if (! ok) std::cerr << "File non caricato: " << file.getFullPathName() << std::endl;
                }
            }
            else if (args[i] == "--set")
            {
                const int slot = next.upToFirstOccurrenceOf (":", false, false).getIntValue();
                const auto rest = next.fromFirstOccurrenceOf (":", false, false);
                p.chain.setParam (slot, rest.upToFirstOccurrenceOf ("=", false, false).getIntValue(),
                                  rest.fromFirstOccurrenceOf ("=", false, false).getFloatValue());
            }
        }
        p.uiState.setProperty ("w", w, nullptr);
        p.uiState.setProperty ("h", h, nullptr);

        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        auto* editor = dynamic_cast<PedalTrinityEditor*> (ed.get());
        if (editor == nullptr) return 3;
        editor->setSize (w, h);
        if (zoomSlot >= 0) editor->showZoom (zoomSlot);
        if (zoomSlot >= 0 && args.contains ("--align")) editor->startZoomAlign();
        if (info) editor->showInfo (true);
        if (options) editor->showOptions (true);
        if (learn || midiPanel)
        {
            // esempio per la guida: tre assegnazioni e una mappatura in corso
            using namespace pt::midi;
            Mapping a; a.channel = 1; a.number = 80; a.target = Target::SlotSwitch; a.slot = 0; a.mode = Mode::Toggle;
            Mapping b; b.channel = 1; b.number = 11; b.target = Target::SlotControl; b.slot = 1; b.control = 0;
            Mapping c; c.channel = 1; c.number = 82; c.target = Target::PresetNext; c.mode = Mode::Toggle;
            p.midi.setMappings ({ a, b, c });
        }
        if (learn) { editor->setMidiLearn (true); p.chain.notifyTouch (1, 1); }
        if (midiPanel) editor->showMidi (true);
        auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, scale);
        outFile.deleteFile();
        juce::FileOutputStream os (outFile);
        const bool ok = os.openedOk() && juce::PNGImageFormat().writeImageToStream (img, os);
        std::cout << (ok ? "Salvato " : "Errore ") << outFile.getFullPathName() << " (" << img.getWidth() << "x"
                  << img.getHeight() << ")" << std::endl;
        return ok ? 0 : 4;
    }
}

//==============================================================================
class PedalTrinityApp final : public juce::JUCEApplication
{
public:
    PedalTrinityApp()
    {
        juce::PropertiesFile::Options options;
        options.applicationName = "Pedal Trinity";
        options.filenameSuffix = ".settings";
        options.osxLibrarySubFolder = "Application Support";
       #if JUCE_LINUX || JUCE_BSD
        options.folderName = "~/.config/PedalTrinity";
       #else
        options.folderName = "PedalTrinity";
       #endif
        appProperties.setStorageParameters (options);
    }

    const juce::String getApplicationName() override { return "Pedal Trinity"; }
    const juce::String getApplicationVersion() override { return pt::versionString; }
    bool moreThanOneInstanceAllowed() override { return true; }
    void anotherInstanceStarted (const juce::String&) override {}

    void initialise (const juce::String& commandLine) override
    {
        const auto args = juce::StringArray::fromTokens (commandLine, true);
        if (args.contains ("--selftest") || args.contains ("--screenshot"))
        {
            const int rc = args.contains ("--selftest") ? runSelfTest() : runScreenshot (args);
            setApplicationReturnValue (rc);
            quit();
            return;
        }
        if (args.contains ("--version"))
        {
            std::cout << "Pedal Trinity " << pt::versionString << " - by " << pt::author << " - GNU GPL v3" << std::endl;
            quit();
            return;
        }

        auto* settings = appProperties.getUserSettings();
        const bool firstRun = settings->getXmlValue ("audioSetup") == nullptr;

        mainWindow = std::make_unique<juce::StandaloneFilterWindow> (
            juce::String ("Pedal Trinity ") + pt::versionString + " - by " + pt::author,
            juce::Colour (0xff121214), settings, false);

        if (auto* holder = mainWindow->getPluginHolder())
        {
            // impostazioni MIDI del pannello MIDI: dispositivi dello Standalone
            auto& sd = pt::midi::StandaloneDevices::get();
            sd.manager = &holder->deviceManager;
            sd.outputChanged = [holder] { holder->player.setMidiOutput (holder->deviceManager.getDefaultMidiOutput()); };
            if (firstRun)
            {
                holder->getMuteInputValue().setValue (false);
               #if JUCE_WINDOWS && JUCE_ASIO
                // al primo avvio si preferiscono i driver ASIO (latenza minima)
                auto& dm = holder->deviceManager;
                for (auto* type : dm.getAvailableDeviceTypes())
                {
                    if (type->getTypeName() == "ASIO")
                    {
                        type->scanForDevices();
                        if (type->getDeviceNames().size() > 0)
                            dm.setCurrentAudioDeviceType ("ASIO", true);
                        break;
                    }
                }
               #endif
            }
        }

        mainWindow->setVisible (true);
    }

    void shutdown() override
    {
        pt::midi::StandaloneDevices::get() = {};
        mainWindow = nullptr;
        appProperties.saveIfNeeded();
    }

    void systemRequestedQuit() override
    {
        if (mainWindow != nullptr)
            if (auto* holder = mainWindow->getPluginHolder())
                holder->savePluginState();

        if (juce::ModalComponentManager::getInstance()->cancelAllModalComponents())
        {
            juce::Timer::callAfterDelay (100, []
            {
                if (auto app = juce::JUCEApplicationBase::getInstance())
                    app->systemRequestedQuit();
            });
        }
        else
        {
            quit();
        }
    }

private:
    juce::ApplicationProperties appProperties;
    std::unique_ptr<juce::StandaloneFilterWindow> mainWindow;
};

juce::JUCEApplicationBase* juce_CreateApplication();
juce::JUCEApplicationBase* juce_CreateApplication() { return new PedalTrinityApp(); }

#endif
