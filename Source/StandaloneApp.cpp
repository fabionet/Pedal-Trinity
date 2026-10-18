/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Applicazione Standalone personalizzata:
      * finestra con titolo "Pedal Trinity 1.3.1 beta";
      * su Windows, al primo avvio, seleziona i driver ASIO se presenti;
      * ingresso audio attivo di default (e' un effetto per chitarra);
      * riga di comando:
          --selftest                         verifica automatica del DSP
          --screenshot file.png [opzioni]    salva un'immagine dell'interfaccia
              --scale s  --size WxH  --view n  --first i  --factory numero|nome
              --zoom slot  --info  --options  --theme pro|tolex|walnut|green|alu|night  --chain id,id@B,split=0.5,...  --set slot:comando=valore (ripetibile)
              --nam slot:A|B=file.nam  --ir slot:A|B=file.wav   (NAM-A1A2)  --real (REAL MOD)  --photos (usa le foto personali)
              --routing (Mappa di Routing)  --routezoom z  --routemove slot:x,y (ripetibile; slot I/O = in/out)
              --back (con --zoom: mostra il retro)  --backimage file.jpg (immagine di prova del retro, solo screenshot)
*/

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <iostream>
#include <array>
#include <atomic>
#include <chrono>
#include <ctime>
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
#include "gui/RoutingMap.h"
#include "gui/Rack.h"
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
            for (auto* brand : { "BOSS", "Roland", "Dunlop", "Ernie Ball", "Morley", "DeArmond", "MXR", "Ibanez", "Electro-Harmonix", "Behringer", "Cuvave", "M-VAVE", "MVAVE" })
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

    //==============================================================================
    // Mappa di Routing e retro degli ampli (preparazione)
    //==============================================================================
    /** Catena da una descrizione "id,id@B,id!,split=0.5,-" (- = slot vuoto, ! = staccato, @B = corsia B). */
    juce::ValueTree routeChainSpec (const juce::String& spec)
    {
        juce::ValueTree t ("CHAIN");
        for (auto& tok : juce::StringArray::fromTokens (spec, ",", ""))
        {
            juce::ValueTree s ("SLOT");
            const auto id = tok.upToFirstOccurrenceOf ("@", false, false).upToFirstOccurrenceOf ("=", false, false).upToFirstOccurrenceOf ("!", false, false);
            s.setProperty ("model", id == "-" ? juce::String() : id, nullptr);
            s.setProperty ("on", true, nullptr);
            if (tok.contains ("!")) s.setProperty ("patched", false, nullptr);
            if (tok.contains ("@B")) s.setProperty ("lane", 1, nullptr);
            if (tok.contains ("=")) s.setProperty ("p0", tok.fromFirstOccurrenceOf ("=", false, false).getFloatValue(), nullptr);
            t.appendChild (s, nullptr);
        }
        return t;
    }

    void runRoutingTests (const std::function<void (const juce::String&, bool, const juce::String&)>& report)
    {
        using pt::ui::RoutingMap;
        auto findBoard = [] (juce::Component& ed) -> pt::ui::Pedalboard*
        {
            for (auto* c : ed.getChildren())
                if (auto* b = dynamic_cast<pt::ui::Pedalboard*> (c)) return b;
            return nullptr;
        };
        auto patchMask = [] (const pt::engine::Chain& ch)
        {
            juce::String m;
            for (int i = 0; i < ch.size(); ++i) m << (ch.isPatched (i) ? '1' : '0');
            return m;
        };

        // 1. ogni spina, ogni estremita', ogni bersaglio: la mappa fa esattamente cio' che fa la pedaliera
        {
            const char* specs[] = { "ds1,ce2!,-,bd2,dd3,mt2", "ds1,split=0.5,dd3,rv6@B,bd2,mt2@B,ce2!", "ds1,split=1,ce2,dd3,bd2" };
            int cases = 0, same = 0, changed = 0, refused = 0;
            juce::String firstDiff;
            for (auto* spec : specs)
            {
                PedalTrinityProcessor p;
                p.chain.fromValueTree (routeChainSpec (spec));
                p.uiState.setProperty ("view", 18, nullptr);
                std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
                ed->setSize (1920, 1080);
                auto* editor = dynamic_cast<PedalTrinityEditor*> (ed.get());
                auto* board = findBoard (*ed);
                if (editor == nullptr || board == nullptr) { report ("Mappa di Routing: pedaliera ed editor", false, "non trovati"); return; }
                board->refresh();
                editor->showRouting (true);
                auto* map = editor->routingMap();
                map->fitAll (false);
                const auto initial = patchMask (p.chain);
                auto restore = [&]
                {
                    for (int i = 0; i < p.chain.size(); ++i) p.chain.setPatched (i, initial[i] == '1');
                    board->refresh();
                    map->refresh();
                };
                if (board->describeCables() != map->describeCables())
                {
                    report ("Mappa di Routing: stessi cavi della pedaliera", false, board->describeCables() + " / " + map->describeCables());
                    return;
                }
                // bersagli corrispondenti: pedali, INPUT, OUTPUT, spazio libero, fuori
                struct T { juce::Point<float> b, m; juce::String name; };
                std::vector<T> targets;
                for (int i = 0; i < p.chain.size(); ++i)
                    if (p.chain.slot (i)->fx != nullptr)
                        targets.push_back ({ board->slotArea (i).getCentre().toFloat(), map->nodeScreenCentre (i), "slot " + juce::String (i) });
                targets.push_back ({ board->inputPanelArea().getCentre().toFloat(), map->nodeScreenCentre (RoutingMap::inputNode), "INPUT" });
                targets.push_back ({ board->outputPanelArea().getCentre().toFloat(), map->nodeScreenCentre (RoutingMap::outputNode), "OUTPUT" });
                targets.push_back ({ board->emptyPointForTest(), map->emptyScreenPoint(), "vuoto" });
                targets.push_back ({ { -50.0f, -50.0f }, { 3.0f, 3.0f }, "fuori" });
                const int nConn = map->numCables();
                for (int c = 0; c < nConn; ++c)
                    for (bool dest : { false, true })
                        for (const auto& t : targets)
                        {
                            restore();
                            const bool rb = board->dropPlugForTest (c, dest, t.b);
                            const auto sb = board->describeCables() + " " + patchMask (p.chain);
                            restore();
                            const bool rm = map->dropPlugForTest (c, dest, t.m);
                            const auto sm = map->describeCables() + " " + patchMask (p.chain);
                            ++cases;
                            if (rb == rm && sb == sm) ++same;
                            else if (firstDiff.isEmpty())
                                firstDiff = juce::String (spec) + " cavo " + juce::String (c) + (dest ? " dest " : " sorg ") + t.name + ": " + sb + " / " + sm;
                            changed += rm ? 1 : 0;
                            refused += (! rm && map->lastNoticeWasError()) ? 1 : 0;
                        }
                restore();
            }
            report ("Mappa di Routing: replug dalla mappa uguale al replug della pedaliera (ogni cavo, estremita' e bersaglio; singola, DUAL, STEREO)",
                    cases > 100 && same == cases && changed > 10 && refused > 10,
                    juce::String (same) + "/" + juce::String (cases) + " casi uguali, " + juce::String (changed) + " cambi, "
                        + juce::String (refused) + " rifiuti con avviso" + (firstDiff.isEmpty() ? juce::String() : " | " + firstDiff));
        }

        // 2. rifiuto con avviso chiaro, sincronia mappa <-> pedaliera, LED, Esc
        {
            PedalTrinityProcessor p;
            p.chain.fromValueTree (routeChainSpec ("ds1,ce2,bd2,dd3"));
            p.uiState.setProperty ("view", 6, nullptr);
            std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
            ed->setSize (1280, 760);
            auto* editor = dynamic_cast<PedalTrinityEditor*> (ed.get());
            auto* board = findBoard (*ed);
            editor->showRouting (true);
            auto* map = editor->routingMap();
            const auto start = map->describeCables();
            // ingresso di BD-2 (cavo 2) portato all'indietro su DS-1: rifiutato, il cavo torna dov'era
            const bool moved = map->dropPlugForTest (2, true, map->nodeScreenCentre (0));
            const bool refusedOk = ! moved && map->describeCables() == start && map->lastNoticeWasError()
                                   && map->lastNotice().contains ("non ammesso") && map->lastNotice().contains ("sinistra a destra");
            report ("Mappa di Routing: ricollegamento non ammesso, il cavo torna dov'era con un avviso", refusedOk, map->lastNotice());

            // la mappa stacca CE-2: la pedaliera lo vede subito (stessa catena), e viceversa
            map->dropPlugForTest (1, true, map->emptyScreenPoint());
            p.chain.dispatchPendingMessages();
            const bool m2b = ! p.chain.isPatched (1) && board->describeCables() == map->describeCables() && board->describeCables() == "I>0 0>2 2>3 3>O";
            board->dropPlugForTest (0, true, board->slotArea (1).getCentre().toFloat());      // INPUT infilato in CE-2: torna in catena, DS-1 scavalcato
            p.chain.dispatchPendingMessages();
            const bool b2m = map->describeCables() == board->describeCables() && map->describeCables() == "I>1 1>2 2>3 3>O";
            map->clickLedForTest (2);
            p.chain.dispatchPendingMessages();
            const bool led = ! p.chain.slot (2)->enabled.load();
            map->clickLedForTest (2);
            const bool led2 = p.chain.slot (2)->enabled.load();
            bool closed = false;
            map->onClose = [&closed] { closed = true; };
            map->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
            report ("Mappa di Routing: cambi dalla mappa subito sulla pedaliera e viceversa, LED cliccabile, Esc chiude",
                    m2b && b2m && led && led2 && closed,
                    juce::String (m2b ? "mappa->pedaliera " : "- ") + (b2m ? "pedaliera->mappa " : "- ") + (led && led2 ? "LED " : "- ") + (closed ? "Esc" : "-")
                        + " | " + map->describeCables());
        }

        // 3. posizioni salvate nello stato del progetto (non nei preset) e ricaricate; progetti vecchi = automatica
        {
            PedalTrinityProcessor p;
            p.chain.fromValueTree (routeChainSpec ("ds1,ce2,bd2,dd3,mt2"));
            p.routeLayout.fromValueTree ({});
            std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
            ed->setSize (1280, 760);
            auto* editor = dynamic_cast<PedalTrinityEditor*> (ed.get());
            editor->showRouting (true);
            auto* map = editor->routingMap();
            const auto before = map->nodeWorldBounds (2);
            const auto target = map->nodeScreenCentre (4).translated (0.0f, 260.0f * map->getZoom());
            map->dragNodeForTest (2, target);
            map->dragNodeForTest (RoutingMap::inputNode, map->nodeScreenCentre (RoutingMap::inputNode).translated (0.0f, 300.0f * map->getZoom()));
            map->setZoom (0.9f, map->nodeScreenCentre (0));
            juce::Point<float> p2, pin;
            const bool stored = p.routeLayout.slotPos (2, p2) && p.routeLayout.ioPos (0, pin) && p.routeLayout.numPlaced() == 2
                                && p2 != before.getTopLeft() && map->nodeWorldBounds (2).getTopLeft() == p2;
            editor->showRouting (false);
            juce::MemoryBlock blob;
            p.getStateInformation (blob);
            PedalTrinityProcessor q;
            q.setStateInformation (blob.getData(), (int) blob.getSize());
            juce::Point<float> q2, qin;
            const bool reloaded = q.routeLayout.slotPos (2, q2) && q.routeLayout.ioPos (0, qin) && q2.getDistanceFrom (p2) < 0.01f
                                  && qin.getDistanceFrom (pin) < 0.01f && std::abs (q.routeLayout.zoom() - 0.9f) < 0.001f && q.routeLayout.numPlaced() == 2;
            // la mappa riaperta mostra l'oggetto dove era stato lasciato
            std::unique_ptr<juce::AudioProcessorEditor> ed2 (q.createEditor());
            ed2->setSize (1280, 760);
            auto* editor2 = dynamic_cast<PedalTrinityEditor*> (ed2.get());
            editor2->showRouting (true);
            const bool reopened = editor2->routingMap()->nodeWorldBounds (2).getTopLeft().getDistanceFrom (p2) < 0.01f
                                  && std::abs (editor2->routingMap()->getZoom() - 0.9f) < 0.001f;
            // preset: formato invariato (nessun nodo della mappa); progetto vecchio senza mappa: disposizione automatica
            const bool presetClean = ! p.captureState().getChildWithName ("ROUTEMAP").isValid();
            juce::MemoryBlock old;
            if (auto xml = p.captureState().createXml()) juce::AudioProcessor::copyXmlToBinary (*xml, old);
            PedalTrinityProcessor r;
            r.setStateInformation (old.getData(), (int) old.getSize());
            const bool oldProject = r.routeLayout.numPlaced() == 0 && r.routeLayout.zoom() == 0.0f && r.chain.size() == 5;
            report ("Mappa di Routing: posizioni e zoom salvati nel progetto e ricaricati; preset invariati; progetti vecchi in disposizione automatica",
                    stored && reloaded && reopened && presetClean && oldProject,
                    juce::String (stored ? "salvate " : "- ") + (reloaded ? "ricaricate " : "- ") + (reopened ? "riaperte " : "- ")
                        + (presetClean ? "preset puliti " : "- ") + (oldProject ? "progetto vecchio" : "-")
                        + "  slot 3 in " + juce::String (q2.x, 1) + "," + juce::String (q2.y, 1));

            // le posizioni seguono gli slot: spostamenti, inserimenti, cambio di pedale, rimozione
            auto& ch = q.chain;
            auto& lay = q.routeLayout;
            juce::Point<float> t0;
            ch.move (2, 0);
            const bool mv = lay.slotPos (0, t0) && t0.getDistanceFrom (p2) < 0.01f && ! lay.slotPos (2, t0);
            ch.insert (0, "ce2");
            const bool ins = lay.slotPos (1, t0) && t0.getDistanceFrom (p2) < 0.01f && ! lay.slotPos (0, t0);
            ch.setModel (1, "ds1");
            const bool mdl = lay.slotPos (1, t0) && t0.getDistanceFrom (p2) < 0.01f;
            ch.remove (1);
            const bool rem = lay.numPlaced() == 1;                       // resta solo l'INPUT
            // nodo alterato in un progetto: valori non validi scartati
            juce::ValueTree bad ("ROUTEMAP");
            bad.setProperty ("zoom", std::nan (""), nullptr);
            for (auto v : { std::nan (""), 1.0e30, 3.0 })
            {
                juce::ValueTree n ("NODE");
                n.setProperty ("i", v == 3.0 ? 99.0 : 1.0, nullptr);
                n.setProperty ("x", v, nullptr);
                n.setProperty ("y", 10.0, nullptr);
                bad.appendChild (n, nullptr);
            }
            lay.fromValueTree (bad);
            const bool safe = lay.numPlaced() == 0 && lay.zoom() == 0.0f;
            report ("Mappa di Routing: le posizioni seguono gli slot (sposta, inserisci, cambia pedale, rimuovi); valori alterati scartati",
                    mv && ins && mdl && rem && safe,
                    juce::String (mv ? "sposta " : "- ") + (ins ? "inserisci " : "- ") + (mdl ? "cambio " : "- ") + (rem ? "rimuovi " : "- ") + (safe ? "alterati" : "-"));
        }

        // 4. mappa con 100 slot e splitter DUAL
        {
            juce::StringArray tok;
            const char* ids[] = { "ds1", "ce2", "bd2", "dd3", "rv6", "mt2", "ph3", "cs3", "gq7", "od3" };
            for (int i = 0; i < 100; ++i)
                tok.add (i == 40 ? juce::String ("split=0.5") : juce::String (ids[i % 10]) + (i > 40 && i % 2 == 0 ? "@B" : ""));
            PedalTrinityProcessor p;
            p.chain.fromValueTree (routeChainSpec (tok.joinIntoString (",")));
            std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
            ed->setSize (1280, 760);
            auto* editor = dynamic_cast<PedalTrinityEditor*> (ed.get());
            auto* board = findBoard (*ed);
            editor->showRouting (true);
            auto* map = editor->routingMap();
            int overlaps = 0;
            bool finite = true;
            for (int i = -2; i < 100; ++i)
            {
                const auto a = map->nodeWorldBounds (i);
                finite = finite && ! a.isEmpty() && std::isfinite (a.getX()) && std::isfinite (a.getY());
                for (int j = i + 1; j < 100; ++j)
                    overlaps += a.intersects (map->nodeWorldBounds (j)) ? 1 : 0;
            }
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
            const bool counts = map->numNodes() == 102 && map->numCables() == (int) pt::ui::routing::connections (p.chain).size()
                                && map->describeCables() == board->describeCables();
            const bool readable = map->getZoom() >= 0.69f;
            // uno slot della corsia B staccato dalla mappa (dopo averlo portato in vista), e uno che prova a cambiare corsia
            map->scrollToNode (60);
            int connB = -1, connA = -1;
            const auto conns = pt::ui::routing::connections (p.chain);
            for (int c = 0; c < (int) conns.size(); ++c)
            {
                if (conns[(size_t) c].b.kind == pt::ui::routing::End::SlotIn && conns[(size_t) c].b.slot == 60) connA = c;      // 60: corsia A
                if (conns[(size_t) c].b.kind == pt::ui::routing::End::SlotIn && conns[(size_t) c].b.slot == 61) connB = c;      // 61: corsia B
            }
            const bool laneRefused = ! map->dropPlugForTest (connB, true, map->nodeScreenCentre (62)) && map->lastNotice().contains ("Corsia diversa");
            map->scrollToNode (60);
            const bool unplug = map->dropPlugForTest (connA, true, map->emptyScreenPoint()) && ! p.chain.isPatched (60);
            p.chain.dispatchPendingMessages();
            const bool synced = board->describeCables() == map->describeCables();
            map->scrollToNode (99);
            map->dragNodeForTest (99, map->nodeScreenCentre (99).translated (40.0f, 60.0f));
            const int placed = p.routeLayout.numPlaced();
            map->autoArrange();
            report ("Mappa di Routing: 100 slot con splitter DUAL (102 oggetti senza sovrapposizioni, cavi, corsie, disegno, disposizione automatica)",
                    counts && overlaps == 0 && finite && readable && img.isValid() && ms < 3000.0 && laneRefused && unplug && synced && placed == 1
                        && p.routeLayout.numPlaced() == 0,
                    juce::String (map->numNodes()) + " oggetti, " + juce::String (map->numCables()) + " cavi, " + juce::String (overlaps) + " sovrapposizioni, zoom "
                        + juce::String (map->getZoom(), 2) + ", disegno " + juce::String (ms, 0) + " ms" + (laneRefused ? ", corsia rifiutata" : ", corsia ??")
                        + (unplug ? ", staccato" : ", stacco ??") + (synced ? ", pedaliera allineata" : ", pedaliera ??")
                        + ", spostati " + juce::String (placed) + " -> " + juce::String (p.routeLayout.numPlaced()) + (finite ? "" : ", posizioni non valide"));
        }

        // 5. tasto "Mappa di Routing" nella barra: visibile e senza sovrapposizioni da 1280x760 a 2560x1440
        {
            PedalTrinityProcessor p;
            p.chain.fromValueTree (routeChainSpec ("ds1,ce2,bd2,dd3,rv6,mt2,ph3,cs3,gq7,od3,ds1,ce2"));
            p.uiState.setProperty ("first", 3, nullptr);
            std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
            int bad = 0, sizes = 0;
            juce::String detail;
            for (auto sz : PedalTrinityEditor::sizePresets())
            {
                ed->setSize (sz.x, sz.y);
                ++sizes;
                juce::Array<juce::Component*> bar;
                juce::Component* mapButton = nullptr;
                for (auto* c : ed->getChildren())
                {
                    if (! c->isVisible() || c->getBounds().getBottom() > 50 || c->getWidth() <= 0) continue;
                    bar.add (c);
                    if (c->getName() == "Mappa di Routing") mapButton = c;
                }
                bool okSize = mapButton != nullptr && mapButton->getWidth() >= 70 && ed->getLocalBounds().contains (mapButton->getBounds());
                for (int i = 0; i < bar.size(); ++i)
                {
                    okSize = okSize && bar[i]->getX() >= 0 && bar[i]->getRight() <= sz.x;
                    if (auto* b = dynamic_cast<juce::Button*> (bar[i])) okSize = okSize && b->getWidth() >= 26;
                    for (int j = i + 1; j < bar.size(); ++j)
                        if (bar[i]->getBounds().intersects (bar[j]->getBounds()))
                        {
                            okSize = false;
                            detail << sz.x << ": " << bar[i]->getName() << "/" << bar[j]->getName() << " ";
                        }
                }
                if (! okSize) { ++bad; if (detail.isEmpty()) detail << sz.x << ": tasto della mappa assente o stretto "; }
            }
            report ("Barra: tasto \"Mappa di Routing\" visibile, nessuna sovrapposizione da 1280x760 a 2560x1440",
                    bad == 0 && sizes == 5, juce::String (sizes - bad) + "/" + juce::String (sizes) + " misure " + detail);
        }

        // 6. leggibilita' della mappa con ogni tema (contrasto WCAG >= 4.5:1)
        {
            double worst = 99;
            juce::String worstName;
            for (const auto& t : pt::ui::allThemes())
                if (const double c = RoutingMap::minContrast (t); c < worst) { worst = c; worstName = t.name; }
            report ("Mappa di Routing: testi leggibili con tutti i temi (WCAG >= 4.5:1)", worst >= 4.5,
                    "minimo " + juce::String (worst, 1) + ":1 (" + worstName + ")");
        }

        // 7. retro degli ampli: ricerca per nome nel BinaryData, tasto Retro/Fronte nello zoom
        {
            using pt::ui::Assets;
            const bool names = Assets::backResourceFor ("ds1_jpg") == "ds1_back_jpg" && Assets::backResourceFor ("r_ds1_jpg") == "r_ds1_back_jpg"
                               && Assets::backResourceFor ("ds1_back_jpg").isEmpty() && Assets::backResourceFor ("ds1_png").isEmpty()
                               && Assets::backResourceFor (nullptr).isEmpty() && Assets::backResourceFor ("_jpg").isEmpty();
            int withBack = 0;
            for (int i = 0; i < pt::engine::numModels(); ++i)
                withBack += pt::ui::backImageFor (&pt::engine::model (i)).isValid() ? 1 : 0;
            const bool binary = Assets::hasResource ("ds1_jpg") && ! Assets::hasResource ("ds1_back_jpg") && ! Assets::hasResource ("");
            report ("Retro degli ampli: nome della risorsa del retro (<id>_back.jpg, r_<id>_back.jpg) cercato nel BinaryData",
                    names && binary, juce::String (withBack) + " modelli con il retro nel BinaryData (arriveranno con gli ampli)");

            // immagine di prova creata solo qui (mai nel programma): retro per DS-1 e per la sua replica REAL MOD
            juce::Image test (juce::Image::ARGB, 300, 200, true);
            {
                juce::Graphics g (test);
                g.fillAll (juce::Colour (0xff3a3c40));
                g.setColour (juce::Colours::white);
                g.drawText ("RETRO DI PROVA", test.getBounds(), juce::Justification::centred);
            }
            juce::StringArray asked;
            pt::ui::setBackImageLookup ([test, &asked] (const juce::String& name)
            {
                asked.add (name);
                return name == "ds1_back_jpg" || name == "r_ds1_back_jpg" ? test : juce::Image();
            });
            pt::engine::Chain chain;
            chain.fromValueTree (routeChainSpec ("ds1,ce2"));
            juce::SharedResourcePointer<pt::ui::ThemeManager> themes;
            const bool wasReal = themes->realMode();
            themes->previewRealMode (false);
            pt::ui::ZoomPanel zp (chain, 0, nullptr);
            zp.setSize (1280, 760);
            const bool front = zp.hasBack() && zp.flipButtonVisible() && ! zp.showingBack() && zp.flipButtonText() == "Retro";
            zp.setShowBack (true, false);
            const bool backOn = zp.showingBack() && zp.flipButtonText() == "Fronte";
            zp.setShowBack (false, false);
            const bool backOff = ! zp.showingBack() && zp.flipButtonText() == "Retro";
            zp.setShowBack (true, false);
            zp.show (1);                                           // CE-2: nessun retro, si torna al fronte e il tasto sparisce
            const bool none = ! zp.hasBack() && ! zp.flipButtonVisible() && ! zp.showingBack();
            themes->previewRealMode (true);
            zp.show (0);
            const bool real = zp.hasBack() && asked.contains ("r_ds1_back_jpg");
            themes->previewRealMode (wasReal);
            pt::ui::setBackImageLookup ({});
            const bool restored = ! pt::ui::backImageFor (pt::engine::findModel ("ds1")).isValid();
            report ("Retro degli ampli: tasto Retro/Fronte nello zoom solo se c'e' l'immagine del retro (anche REAL MOD; immagine di prova solo nel test)",
                    front && backOn && backOff && none && real && restored,
                    juce::String (front ? "fronte " : "- ") + (backOn ? "retro " : "- ") + (backOff ? "di nuovo fronte " : "- ") + (none ? "senza retro " : "- ")
                        + (real ? "REAL MOD " : "- ") + (restored ? "BinaryData" : "-"));
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
        // i valori misurati prima dell'introduzione del wah a induttore (tolleranza 0.005 dB). Dalla 1.3.1 il pedale in movimento
        // passa per l'involucro senza click delle famiglie vecchie (due poli da 5 ms, un campione alla volta invece di un
        // valore per blocco da 256): livelli nuovi, scarto dai vecchi 0.04-0.20 dB (prima: pw1 -32.374/-32.374, fw3
        // -32.812/-31.392, pw3 -31.789/-31.296, pw10 -15.292/-13.569, ft2 -44.138/-25.702, aw2 -35.252/-35.613, aw3 -32.331/-43.706;
        // TW-1 senza pedale: invariato)
        {
            struct Ref { const char* id; double a, b; };
            static const Ref refs[] = { { "pw1", -32.414, -32.414 }, { "fw3", -32.845, -31.459 }, { "pw3", -31.833, -31.352 },
                                        { "pw10", -15.368, -13.646 }, { "tw1", -40.609, -25.074 }, { "ft2", -44.079, -25.830 },
                                        { "aw2", -35.161, -35.625 }, { "aw3", -32.529, -43.706 } };
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
            report ("Wah esistenti invariati (PW-1, FW-3, PW-3, PW-10, TW-1, FT-2, AW-2, AW-3; pedale lisciato dalla 1.3.1)", ok, detail.trim());
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

    /** Hash FNV-1a dell'uscita (byte per byte) con i comandi di default, poi spostati di +0.37 a meta' segnale.
        'fixedHalf' (facoltativo): hash della sola prima meta', a comandi fermi. */
    uint64_t rawHashS1 (const pt::engine::ModelDef& d, const Buf& x, uint64_t* fixedHalf = nullptr)
    {
        auto fx = makeS1 (d, {});
        const size_t half = x.size() / 2;
        Buf y1 (x.begin(), x.begin() + (long) half), y2 (x.begin() + (long) half, x.end());
        processMono (*fx, y1, 512);
        auto fnv = [] (const Buf& y)
        {
            uint64_t h = 1469598103934665603ull;
            for (float v : y)
            {
                uint32_t u;
                std::memcpy (&u, &v, 4);
                for (int k = 0; k < 4; ++k) { h ^= (u >> (8 * k)) & 255u; h *= 1099511628211ull; }
            }
            return h;
        };
        if (fixedHalf != nullptr) *fixedHalf = fnv (y1);
        for (int i = 0; i < d.numControls; ++i) fx->params[i].store (std::fmod (fx->params[i].load() + 0.37f, 1.0f));
        processMono (*fx, y2, 512);
        y1.insert (y1.end(), y2.begin(), y2.end());
        return fnv (y1);
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

    /** Le impronte "bit per bit" dei pedali esistenti sono misurate con GCC su Linux x86-64: con un altro compilatore
        (MSVC su Windows) l'aritmetica in virgola mobile differisce negli ultimi bit e il confronto non ha senso. */
   #if JUCE_LINUX && defined (__GNUC__) && ! defined (__clang__) && defined (__x86_64__)
    constexpr bool kBitExactRefs = true;
   #else
    constexpr bool kBitExactRefs = false;
   #endif

    /** Pedali digitali della tappa 3B (motore FxClassicB, agganciati anch'essi da makeClassic). */
    const char* const kStage3BDigital[] = { "evh30", "ehbattalion", "ehbassmetaphors", "ehanalogizer", "ehxstmist", "ehxneomist", "ehxflhoax", "ehxechofl", "ehxbassclo", "ehxeddy", "ehxwiggler", "ehxnanopul", "ehxsuppul", "ehxlesterk", "ehxmodrex", "ehxstpolyp", "ehxbadst1", "ehxstpolyc", "ehxstcthe", "ehdmm550", "ehdmm1100", "ehdmboy", "ehmmstereo", "ehreplay", "ehcanecho", "ehrerun", "ehhgneo", "ehholier", "ehholiest", "ehabyss", "eh3verb", "ehshimmer", "ehsuperegop", "ehattackdecay", "ehpog3", "ehpicopog", "ehpitchforkp", "ehpicofork", "ehihm", "ehslammi", "ehslammip", "ehstring9", "ehbassmono", "ehringthing", "ehatomic", "ehv256", "ehironlung", "ehepitome", "ehsoulpog", "ehtonetattoo" };

    const char* const kStage3Digital[] = { "zw38", "evh90", "evh117", "ehxgoodvib", "ehxlesterg", "ehxmod11", "ehxworm", "ehxss75", "ehxssnano",
        "ehxbadst", "ehxpoly79", "ehxem76", "ehxdem78", "ehxdemre", "ehxpolych", "ehxsclone", "ehxneoclo", "ehxctheory", "ehxstpuls", "ehmm76",
        "ehdmm", "ehmmboy", "ehmmtoy", "ehslapback", "ehsmmh", "eh1echo", "eh16sec", "ehcanyon", "ehgcanyon", "ehhg", "ehhgplus", "ehhgmax",
        "ehcathedral", "ehoceans11", "ehoceans12", "ehholystain", "ehsuperego", "eh720", "ehpog", "ehpog2", "ehmicropog", "ehhog", "ehhog2",
        "ehpitchfork", "ehb9", "ehc9", "ehkey9", "ehmel9", "ehsynth9", "ehbass9", "ehmonosynth", "ehravish", "ehvoicebox" };

    //==============================================================================
    // Tappa 4 (Ibanez, 166 pedali): controlli numerici dei quattro lavori del suono (s1 drive: Circuit.cpp con gli stadi
    // mna, fbclip con gbw/sr/Rg2, triodo a griglia positiva, bbd; s2 dinamica/EQ/filtri/wah: FxStage4Dyn.cpp; s3 modulazioni
    // e delay BBD: FxStage4Mod.cpp; s4 delay/chorus/flanger digitali ADM: FxStage4Time.cpp). Metodi e valori dei banchi della
    // tappa (work/s1..s4/tests.md); misure registrate in un Lab ed eseguite in parallelo, segnali corti per i controlli su
    // tutti i modelli.
    //==============================================================================
    const char* const kStage4Drive[] = { "ib9ts9", "ibtsts808hw", "ibtstsmini", "ibstts5", "ib10msl", "ib10ts10", "ibtsts808hwv2",
        "ibtsts808dx", "ibtsts9dx", "ibtlts7", "ibtstsv808", "ibtsts9b", "ibtsnts", "ib7od855", "ib9st9", "ib10stl", "ib9sd9", "ibtssd9m",
        "ib7od850", "ibtsfzmini", "ibstfz5", "ibstpl5", "ibsttm5", "ibstcm5", "ibstcr5", "ibstmf5", "ibstbn5", "ibstsp5", "ibstcd5",
        "ibtlds7", "ib9sm9", "ibtssmmini", "ib10ms10", "ib10pds1", "ib10pod1", "ib10ds10", "ib10fc10", "ib10mt10", "ib10lm7", "ib10ss10",
        "ib10ss20", "ib10bs10", "ib10ot10", "ibtlsm7", "ibtlsh7", "ibtlfz7", "ibtlpd7", "ibtlsb7", "ibtstk999", "ibtstk999us",
        "ibtstk999od", "ibtstk999ht", "ibtsbb9", "ibtsjd9", "ibtsbtmini", "ibtsptpre", "ibxec20", "ib759", "ibtsjemini" };
    const char* const kStage4Dyn[] = { "ib7cp830", "ib7cp835", "ib9cp9", "ib10cp10", "ib10bp10", "ibstcp5", "ibstbp5", "ibtstc999",
        "ibxec30", "ib7ge601", "ib9ge9", "ib10ge10", "ib10be10", "ib7ge300", "ib7pq401", "ib9pq9", "ib10pql", "ib10ppe1", "ibmpteq",
        "ib7af201", "ib9af9", "ib10afl", "ibtlaw7", "ibstaw5", "ibtllf7", "ib10nb10", "ibmptgate", "ib10wh10", "ibmwh10v3", "ib10wf10",
        "ibtlwd7", "ibmwd7jr", "ibmtwp10", "ibmno93", "ibmno57", "ibmno84", "ibmno58", "ib10vl10", "ibmst810", "ib10ls10", "ibxec50" };
    const char* const kStage4Mod[] = { "ib7pt999", "ib7pt707", "ib7pt909", "ib7fp777", "ib7jl70", "ib7st800", "ib7fl303", "ib7fl305",
        "ib7fl301", "ib7fl301dx", "ib7cs505", "ib7ad80", "ib9fl9", "ib9cs9", "ib9pt9", "ib9ad9", "ib9bc9", "ib10sc10", "ib10tc10",
        "ib10pc10", "ib10ph10", "ib10cd10", "ib10bc10", "ib10csl", "ibstcs5", "ibstfl5", "ibstph5", "ibsttl5", "ibstbc5", "ibtlph7",
        "ibtlpm7", "ibtlap7", "ibmadmini", "ibmcsmini", "ibmphmini", "ibmflmini", "ibmtrmini", "ibmes2", "ibmes3", "ibmaf2", "ibmad99",
        "ibmfl99", "ibmph99", "ibmrc99", "ibmec40", "ibmpt1000" };
    const char* const kStage4Digital[] = { "ib10dl10", "ib10dpl10", "ib10dsc10", "ib10ddl10", "ib10dml10", "ib10dcf10", "ib10ddl20",
        "ib10dml20", "ib10dcl", "ib10ddl", "ib10dfl", "ib10dml", "ib10pdm1", "ib10pdd1", "ibstdl5", "ibstem5", "ibtlcf7", "ibtltc7",
        "ibtlde7", "ibmld303" };

    //==============================================================================
    // Tappa 5 (Behringer e Cuvave/M-VAVE, 134 pedali): controlli numerici dei quattro lavori del suono (s1 drive: Circuit.cpp
    // con lo stadio nuovo octdiv; s2 dinamica/EQ/filtri/wah/synth: FxStage5Dyn.cpp; s3 modulazioni e delay BBD: FxStage5Mod.cpp;
    // s4 digitali, preampli e IR: FxStage5Time.cpp). Metodi e valori dei banchi della tappa (work/s1..s4/tests.md).
    //==============================================================================
    const char* const kStage5Drive[] = { "bhto800", "bhto100", "bhvt911", "bhvt999", "bhod300", "bhod400", "bhbo300", "bhpo300", "bhud300",
        "bhum300", "bhhm300", "bhhd300", "bhsm400", "bhxd300", "bhwd300", "bhdm100", "bhsf300", "bhuz400", "bhfd300", "bhvd1", "bhpb100",
        "bhuo300", "bhbdi21", "bhgdi21", "bhtm300", "bhadi21", "bhbod400", "bhzentara", "bhzentaramini", "bhfuzzbender", "bhoctavia",
        "bhbassbrassmaster", "bhoctavedivider", "cvoverdrivets", "cvoverdriveblues", "cvoverdrivedb", "cvoverdriveamp", "cvdistbritish",
        "cvdistsc1", "cvdistmt1", "cvfuzz", "cvbooster" };
    const char* const kStage5Dyn[] = { "bhcs400", "bhble400", "bhcl9", "bhdc9", "cvcompressor", "bhnr300", "bhig9", "cvnoisegate", "bheq700",
        "bhbeq700", "bhse200", "bham300", "bhsm200", "bhuw300", "bhvb1", "bhbtron3", "bhmeatball", "bhbm11m", "bhbm12", "bhringstinger",
        "bhbm15m", "bhbm17", "bhwahwah", "bhhb01", "bhparapedal", "bhfc600", "bhfcv100", "bhab100", "bhab200" };
    const char* const kStage5Mod[] = { "bhuc200", "bhuf300", "bhup300", "bhut300", "bhtp300", "bhuv300", "bhcc300", "bhco600", "bhhf300",
        "bhvp1", "bhph9", "bhvd400", "bhvm1", "bhbuf300", "bhbuc400", "bhchorussymphony", "bhdualphase", "bhbflanger", "bhpedalflanger",
        "bh69vibe", "bhbm13", "bhbm14m", "bhbm18m", "bhflangenoname", "cvchorus", "cvphaser", "cvtremolo", "cvdelaypd18", "cvclassicdelay" };
    const char* const kStage5Digital[] = { "bhcd400", "bhsf400", "bhsp400", "bhso400", "bhus600", "bhfl600", "bhrm600", "bhdd400", "bhdd600",
        "bhem600", "bhdr600", "bhdr400", "bhrv600", "bhfx600", "bhdw400", "bhfm600", "bham400", "bhbsy600", "cvcubebaby", "cvcubebabybass",
        "cvcubebabyac", "cvminiamp", "cvminiefx", "cvdigdelay", "cvdigpitch", "cvdigreverb", "cvlooper", "cvlooperpro", "cvloop2",
        "cvminiuniverse", "cvelemental", "cvelementalpro", "cvgalaxia", "cvirbox" };
    /** Modelli s2/s3/s4 della tappa 5 che makeClassic non aggancia: WahWah (type=inductor, makeWah), SE200 e VB1 (netlist Circuit), looper. */
    const char* const kStage5NotHooked[] = { "bhwahwah", "bhse200", "bhvb1", "cvlooper", "cvlooperpro", "cvloop2" };


    void runStage3AnalogTests (const std::function<void (const juce::String&, bool, const juce::String&)>& report)
    {
        using namespace pt::engine;
        const double sr = 48000.0;

        // 1. pedali esistenti invariati bit per bit: riff 3 s + accordo 2 s + seno a 440 Hz crescente 1 s, comandi di
        //    default poi spostati di +0.37 a meta'. 'fixed' = prima meta' (comandi fermi): hash misurati prima della tappa 3 sul
        //    banco s1, mai cambiati; 'hash' = segnale intero: dalla 1.2.7 i comandi del motore Circuit si muovono senza scatti
        //    (lisciatura per sotto-blocchi, dissolvenze dei selettori), quindi la parte dopo lo spostamento ha impronte nuove
        //    (DS-1, SD-1, BD-2, MT-2); dalla 1.3.1 anche il BOOST del 535Q (wah a induttore, involucro senza click delle famiglie
        //    vecchie) passa per una dissolvenza: 'hash' nuovo, 'fixed' invariato (GCB95 senza levette: invariato)
        {
            struct Ref { const char* id; uint64_t fixed, hash; };
            static const Ref refs[] = { { "ds1", 0x7edafc3b1e613077ull, 0xae0a3f3ff8388e33ull }, { "sd1", 0x4c903f272789401dull, 0x83cedf5bfcc97beeull },
                                        { "bd2", 0x2346b27b6ea0948full, 0xdd87015347914ccfull }, { "mt2", 0xc92c8b58fe1dc55full, 0xa4cfa044ec26ddb8ull },
                                        { "gcb95", 0x3d724d79eefef810ull, 0xb596b28c390cf713ull }, { "cb535q", 0x33a59d947203c0a2ull, 0xd492a77db7111091ull } };
            const Buf x = rawSignalS1 (sr);
            bool ok = true;
            juce::String detail;
            for (const auto& r : refs)
            {
                const auto* d = findModel (r.id);
                if (d == nullptr) { ok = false; detail << r.id << " assente "; continue; }
                uint64_t f = 0;
                const uint64_t h = rawHashS1 (*d, x, &f);
                const bool same = h == r.hash && f == r.fixed;
                ok = ok && same;
                if (f != r.fixed) detail << r.id << " DIVERSO a comandi fermi (" << juce::String::toHexString ((juce::int64) f) << ") ";
                if (h != r.hash) detail << r.id << " DIVERSO (" << juce::String::toHexString ((juce::int64) h) << ") ";
            }
            if (! kBitExactRefs)
                report ("Tappa 3A: DS-1, SD-1, BD-2, MT-2, GCB95, 535Q invariati bit per bit", true,
                        "impronte di riferimento misurate con GCC su Linux: confronto non applicabile a questo compilatore");
            else
            report ("Tappa 3A: DS-1, SD-1, BD-2, MT-2, GCB95, 535Q invariati bit per bit (riff, accordo, seno; comandi fermi, poi mossi)", ok,
                    ok ? juce::String ("6/6 hash identici (comandi fermi e mossi)") : detail.trim());
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
            // tappa 4: tutti i modelli s2/s3/s4 hanno tipi "i4..." (makeStage4) tranne i wah a induttore No.93 e No.57 (type=inductor,
            // makeWah); i 59 drive della s1 sono netlist Circuit
            auto isNew = [] (const char* id)
            {
                for (const char* n : kStage3Digital) if (std::strcmp (n, id) == 0) return true;
                for (const char* n : kStage3BDigital) if (std::strcmp (n, id) == 0) return true;
                if (std::strcmp (id, "ibmno93") == 0 || std::strcmp (id, "ibmno57") == 0) return false;
                for (const char* n : kStage4Dyn) if (std::strcmp (n, id) == 0) return true;
                for (const char* n : kStage4Mod) if (std::strcmp (n, id) == 0) return true;
                for (const char* n : kStage4Digital) if (std::strcmp (n, id) == 0) return true;
                // tappa 5: tipi "i5..." (makeStage5) e i cloni sui tipi i4ross / otaphase / jfetphase; restano fuori il wah a induttore
                // (WahWah, makeWah), le netlist Circuit (SE200, VB1 e i 42 drive) e i looper Cuvave (famiglia Looper senza type=)
                for (const char* n : kStage5NotHooked) if (std::strcmp (n, id) == 0) return false;
                for (const char* n : kStage5Dyn) if (std::strcmp (n, id) == 0) return true;
                for (const char* n : kStage5Mod) if (std::strcmp (n, id) == 0) return true;
                for (const char* n : kStage5Digital) if (std::strcmp (n, id) == 0) return true;
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
            constexpr int expected = (int) (sizeof (kStage3Digital) / sizeof (kStage3Digital[0]) + sizeof (kStage3BDigital) / sizeof (kStage3BDigital[0])
                                            + sizeof (kStage4Dyn) / sizeof (kStage4Dyn[0]) + sizeof (kStage4Mod) / sizeof (kStage4Mod[0])
                                            + sizeof (kStage4Digital) / sizeof (kStage4Digital[0])) - 2
                                     + (int) (sizeof (kStage5Dyn) / sizeof (kStage5Dyn[0]) + sizeof (kStage5Mod) / sizeof (kStage5Mod[0])
                                             + sizeof (kStage5Digital) / sizeof (kStage5Digital[0]) - sizeof (kStage5NotHooked) / sizeof (kStage5NotHooked[0]));
            report ("Tappa 3A/3B/4/5: makeClassic aggancia i modelli nuovi (53 della 3A, 50 della 3B, 105 'i4' della tappa 4, 86 della tappa 5: 80 'i5' e 6 "
                    "cloni sui tipi i4ross / otaphase / jfetphase) e nessun pedale esistente "
                    "(catalogo e REAL MOD)",
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

        // 1. pedali della tappa 3A invariati bit per bit (stessa prova della 3A; a comandi fermi hash del banco s1b prima della
        //    tappa 3B, uguali al JSON 3A: fbinv, envf, freeze, sweep3, formant; con i comandi mossi hash della 1.2.7, vedi la 3A)
        {
            struct Ref { const char* id; uint64_t fixed, hash; };
            static const Ref refs[] = { { "bmtriangle", 0xe24e02c0d41b273cull, 0x8e7cd77dfbd686abull }, { "ehqtronp", 0x19fd6c43f0f99724ull, 0x019d235d72e9ce37ull },
                                        { "ehfreeze", 0xe730543b76a7212aull, 0x6309a599c75964a2ull }, { "ehmicrosyn", 0xfccd6258c5d77740ull, 0x2a8598688845bcd4ull },
                                        { "ehstm", 0x5f40190409ef17c7ull, 0xc7cb59c7ddf3d468ull } };
            static const Buf raw = rawSignalS1 (48000.0);
            std::vector<int> h;
            for (const auto& r : refs)
                h.push_back (lab.add (r.id, [] (const ModelDef& d)
                {
                    uint64_t f = 0;
                    const uint64_t x = rawHashS1 (d, raw, &f);
                    return std::vector<double> { (double) (x >> 32), (double) (x & 0xffffffffull), (double) (f >> 32), (double) (f & 0xffffffffull) };
                }));
            lab.check (h, "Tappa 3B: Big Muff Triangle, Q-Tron+, Freeze, Micro Synth, Stereo Talking Machine (3A) invariati bit per bit",
                       [&lab, h] {
                           int same = 0;
                           juce::String bad;
                           for (size_t k = 0; k < h.size(); ++k)
                           {
                               const uint64_t x = ((uint64_t) lab.v (h[k], 0) << 32) | (uint64_t) lab.v (h[k], 1);
                               const uint64_t f = ((uint64_t) lab.v (h[k], 2) << 32) | (uint64_t) lab.v (h[k], 3);
                               if (x == refs[k].hash && f == refs[k].fixed) ++same;
                               if (f != refs[k].fixed) bad << refs[k].id << " DIVERSO a comandi fermi (" << juce::String::toHexString ((juce::int64) f) << ") ";
                               if (x != refs[k].hash) bad << refs[k].id << " DIVERSO (" << juce::String::toHexString ((juce::int64) x) << ") ";
                           }
                           if (! kBitExactRefs)
                               return Verdict { true, "impronte di riferimento misurate con GCC su Linux: confronto non applicabile a questo compilatore" };
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

        // 9. click cambiando i comandi (stessa misura della 3A), Oceans Abyss compreso il REVERSE di A/B TYPE: il cambio di tipo
        //    svuota il serbatoio (core.clear()) e la registrazione riparte con un attacco di 30 ms, niente gradino a fine segmento
        {
            std::vector<int> h;
            auto what = std::make_shared<std::vector<juce::String>> ((size_t) numB);     // comando peggiore (ogni misura scrive il suo)
            for (int mi = 0; mi < numB; ++mi)
                h.push_back (lab.add (kStage3BDigital[mi], [mi, what] (const ModelDef&)
                {
                    const auto r = clicksS2 (kStage3BDigital[mi]);
                    (*what)[(size_t) mi] = r.what;
                    return std::vector<double> { r.worst };
                }));
            for (int x : h) lab.first (x, 2);
            lab.check (h, "Tappa 3B digitali: niente click cambiando i comandi (seno 220 Hz, differenza seconda <= 10x prima/regime, 50 modelli; "
                          "Oceans Abyss anche con il REVERSE)", [&lab, h, what] {
                double worst = 0, abyss = 0;
                juce::String worstId, bad;
                for (size_t k = 0; k < h.size(); ++k)
                {
                    const double r = lab.v (h[k]);
                    if (r > 10.0) bad << kStage3BDigital[k] << " " << num (r, 1) << "x (" << (*what)[k] << ") ";
                    if (r > worst) { worst = r; worstId = juce::String (kStage3BDigital[k]) + " " + (*what)[k]; }
                    if (std::strcmp (kStage3BDigital[k], "ehabyss") == 0) abyss = r;
                }
                return Verdict { bad.isEmpty(), "peggiore " + num (worst, 1) + "x (" + worstId + "); Oceans Abyss " + num (abyss, 1) + "x " + bad.trim() };
            });
        }

        lab.run (report);
    }

    //==============================================================================
    // Simulatori di cassa IR Loader: uscita deterministica (IR pronto dal primo campione, non dal thread di caricamento)
    //==============================================================================
    template <size_t N> constexpr int countOf (const char* const (&)[N]) { return (int) N; }

    /** Copia di un modello "type=..." con chiavi aggiunte in fondo alla config (le chiavi ripetute sostituiscono le
        precedenti): solo il ramo ritardato ("mix=0,1"), senza rumore e compander ("noise=0 comp=0"), gate aperto ("gate=0").
        Le stringhe restano valide fino alla fine del programma (registrazione solo dal thread principale). */
    pt::engine::ModelDef withConfig (const pt::engine::ModelDef& d, const char* extra)
    {
        static std::deque<std::string> pool;
        pool.push_back (std::string (d.config != nullptr ? d.config : "") + " " + extra);
        auto c = d;
        c.config = pool.back().c_str();
        return c;
    }

    /** Misura su un modello del catalogo con la config modificata (modello assente: la misura fallisce come le altre). */
    int addPatched (Lab& lab, const char* id, const char* extra, Lab::Measure m)
    {
        if (const auto* d = pt::engine::findModel (id)) return lab.add (withConfig (*d, extra), std::move (m));
        return lab.add (id, std::move (m));
    }

    uint64_t fnvBytes (const Buf& x, uint64_t h = 1469598103934665603ull)
    {
        for (float v : x)
        {
            uint32_t u;
            std::memcpy (&u, &v, 4);
            for (int k = 0; k < 4; ++k) { h ^= (u >> (8 * k)) & 255u; h *= 1099511628211ull; }
        }
        return h;
    }

    /** Elaborazione stereo a blocchi da 256 dei banchi s3/s4; 'perBlock' (facoltativo) cambia i comandi prima del blocco. */
    void runStereo256 (pt::engine::Effect& fx, Buf& l, Buf& r, const std::function<void (size_t)>& perBlock = {}, int numCh = 2)
    {
        for (size_t i = 0; i < l.size(); i += 256)
        {
            const int n = (int) std::min<size_t> (256, l.size() - i);
            if (perBlock) perBlock (i);
            float* ch[2] = { l.data() + i, r.data() + i };
            fx.process (ch, numCh, n);
        }
    }

    /** Effetto dei banchi s3/s4: prepare a 48 kHz / 256, poi i comandi per indice (lisciati a partire dal default, come nei
        banchi: le misure aspettano l'assestamento). */
    std::unique_ptr<pt::engine::Effect> makeAfter (const pt::engine::ModelDef& d, const std::map<int, float>& p)
    {
        auto fx = pt::engine::createEffect (d);
        fx->prepare (48000.0, 256);
        for (const auto& [k, v] : p) if (k >= 0 && k < d.numControls) fx->params[k].store (v);
        return fx;
    }

    /** Chitarra sintetica del banco s3 (Karplus-Strong, accordi e note singole ogni 0.75 s, ~0.3 V di picco). */
    Buf guitarS3 (double seconds, unsigned seed = 1)
    {
        const double SR = 48000.0;
        const int n = (int) (seconds * SR);
        Buf out ((size_t) n, 0.0f);
        uint32_t s = seed * 2654435761u + 12345u;
        auto rnd = [&s] { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) (int32_t) s * 4.6566129e-10f; };
        const float notes[][3] = { { 82.41f, 123.47f, 164.81f }, { 110.0f, 164.81f, 220.0f }, { 146.83f, 220.0f, 293.66f },
                                   { 196.0f, 246.94f, 329.63f }, { 329.63f, 392.0f, 493.88f }, { 659.25f, 0, 0 }, { 440.0f, 0, 0 } };
        const int numEvents = (int) (seconds / 0.75);
        for (int e = 0; e < numEvents; ++e)
        {
            const int start = (int) (e * 0.75 * SR);
            const auto& ch = notes[e % 7];
            for (int v = 0; v < 3; ++v)
            {
                if (ch[v] <= 0) continue;
                const int period = (int) std::lround (SR / ch[v]);
                Buf b ((size_t) period);
                for (auto& x : b) x = rnd() * 0.5f;
                float last = 0;
                const int st = start + v * 300;
                const int len = std::min (n - st, (int) (2.5 * SR));
                for (int i = 0; i < len; ++i)
                {
                    const size_t k = (size_t) (i % period);
                    const float y = b[k];
                    const float nv = 0.4985f * (y + last);
                    last = y;
                    b[k] = nv;
                    out[(size_t) (st + i)] += 0.35f * y;
                }
            }
        }
        return out;
    }

    /** Frase di chitarra del banco s4 (corde pizzicate ogni 0.35 s con plettrata, in volt). */
    Buf guitarS4 (double seconds, uint32_t seed = 1)
    {
        const double SR = 48000.0;
        const int n = (int) (seconds * SR);
        Buf y ((size_t) n, 0.0f);
        static const float notes[] = { 82.4f, 110.0f, 146.8f, 196.0f, 246.9f, 329.6f, 196.0f, 220.0f, 293.7f, 392.0f, 164.8f, 123.5f };
        uint32_t s = seed;
        auto rnd = [&s] { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) (int32_t) s * 4.6566129e-10f; };
        int k = 0;
        for (double t0 = 0.0; t0 < seconds - 0.2; t0 += 0.35, ++k)
        {
            const float f = notes[k % 12];
            const int L = std::max (2, (int) std::lround (SR / f));
            Buf b ((size_t) L);
            for (auto& v : b) v = rnd();
            for (int j = 1; j < L; ++j) b[(size_t) j] = 0.6f * b[(size_t) j] + 0.4f * b[(size_t) j - 1];
            const int start = (int) (t0 * SR);
            const float amp = 0.35f * (k % 3 == 0 ? 1.0f : 0.6f);
            int p = 0;
            for (int i = start; i < n && i < start + (int) (1.4 * SR); ++i)
            {
                const float v = b[(size_t) p];
                const int q = (p + 1) % L;
                b[(size_t) p] = 0.996f * 0.5f * (v + b[(size_t) q]);
                p = q;
                y[(size_t) i] += amp * v;
            }
        }
        return y;
    }

    /** Indice del primo comando con quel ruolo (banco s4), -1 se assente. */
    int roleIndex (const pt::engine::ModelDef& d, const char* role)
    {
        for (int i = 0; i < d.numControls; ++i) if (std::strcmp (d.controls[i].role, role) == 0) return i;
        return -1;
    }
    void setRoleS4 (pt::engine::Effect& fx, const char* role, float v) { const int i = roleIndex (fx.def, role); if (i >= 0) fx.params[i].store (v); }
    void setStepS4 (pt::engine::Effect& fx, const char* role, int k)
    {
        const int i = roleIndex (fx.def, role);
        if (i < 0) return;
        const int s = std::max (1, (int) fx.def.controls[i].steps);
        fx.params[i].store (s > 1 ? (float) k / (float) (s - 1) : 0.0f);
    }
    int stepsOfRole (const pt::engine::ModelDef& d, const char* role)
    {
        const int i = roleIndex (d, role);
        return i < 0 ? 1 : std::max (1, (int) d.controls[i].steps);
    }

    /** Tempo di CPU del thread corrente (s): misura del costo dentro il lavoro parallelo. */
    double threadCpuSeconds()
    {
       #if JUCE_LINUX || JUCE_MAC
        timespec ts {};
        clock_gettime (CLOCK_THREAD_CPUTIME_ID, &ts);
        return (double) ts.tv_sec + 1.0e-9 * (double) ts.tv_nsec;
       #else
        return juce::Time::getMillisecondCounterHiRes() * 0.001;
       #endif
    }

    /** Passaggi per la media con isteresi (+-h/2) del banco s3: conta i semiperiodi senza i tremolii della misura. */
    int hystCross (const std::vector<double>& v, double mean, double h)
    {
        int c = 0, state = 0;
        for (double x : v)
        {
            if (x > mean + 0.5 * h && state != 1) { if (state != 0) ++c; state = 1; }
            else if (x < mean - 0.5 * h && state != -1) { if (state != 0) ++c; state = -1; }
        }
        return c;
    }

    /** Ampiezza e fase (a sin + b cos) di una sinusoide nota di frequenza f su y[a..e) (minimi quadrati). */
    std::pair<double, double> sineFit (const Buf& y, double f, size_t a, size_t e, double sr = 48000.0)
    {
        double ss = 0, sc = 0, cc = 0, ys = 0, yc = 0;
        for (size_t i = a; i < e; ++i)
        {
            const double ph = 2 * kPi * f * (double) i / sr, s = std::sin (ph), c = std::cos (ph);
            ss += s * s; sc += s * c; cc += c * c; ys += y[i] * s; yc += y[i] * c;
        }
        const double det = ss * cc - sc * sc;
        return { (ys * cc - yc * sc) / det, (yc * ss - ys * sc) / det };
    }

    /** Picchi di risonanza a piccolo segnale (impulso di 1 mV, FFT 32768 dopo 0.2 s di assestamento): i 'count' massimi locali
        piu' alti fra 60 Hz e 12 kHz (in ordine di frequenza), ciascuno come { Hz, dB }. Per i wah a due risonanze (TWP10). */
    std::vector<std::pair<double, double>> resonancePeaks (pt::engine::Effect& fx, int count)
    {
        const double sr = 48000.0;
        const int order = 15, N = 1 << order;
        fx.prepare (sr, 512);
        std::vector<float> buf ((size_t) N * 2, 0.0f);
        for (int i = 0; i < (int) (sr * 0.2); i += 512) { float* c[1] = { buf.data() }; fx.process (c, 1, 512); }
        std::fill (buf.begin(), buf.end(), 0.0f);
        buf[0] = 1.0e-3f;
        for (int i = 0; i < N; i += 512) { float* c[1] = { buf.data() + i }; fx.process (c, 1, 512); }
        for (int i = 0; i < N; ++i) buf[(size_t) i] *= 1000.0f;
        juce::dsp::FFT fft (order);
        fft.performFrequencyOnlyForwardTransform (buf.data());
        auto db = [&buf] (int k) { return 20.0 * std::log10 (std::max (1.0e-12, (double) buf[(size_t) k])); };
        const int k0 = (int) (60.0 * N / sr), k1 = (int) (12000.0 * N / sr);
        std::vector<std::pair<double, double>> peaks;
        for (int k = k0; k <= k1; ++k)
        {
            // massimo locale su +-6% (niente increspature)
            const int w = std::max (2, (int) (0.06 * k));
            bool top = true;
            for (int j = std::max (1, k - w); j <= k + w && top; ++j) if (j != k && buf[(size_t) j] > buf[(size_t) k]) top = false;
            if (! top) continue;
            const double y0 = db (k - 1), y1 = db (k), y2 = db (k + 1), den = y0 - 2 * y1 + y2;
            const double dd = std::abs (den) > 1e-12 ? 0.5 * (y0 - y2) / den : 0.0;
            peaks.push_back ({ (k + dd) * sr / N, y1 - 0.25 * (y0 - y2) * dd });
        }
        std::sort (peaks.begin(), peaks.end(), [] (const auto& a, const auto& b) { return a.second > b.second; });
        if ((int) peaks.size() > count) peaks.resize ((size_t) count);
        std::sort (peaks.begin(), peaks.end());
        return peaks;
    }

    /** Comando con un click oltre la soglia del gruppo, gia' analizzato e segnalato (vedi il commento al punto d'uso): escluso dalla
        misura comune e controllato da solo con un tetto proprio, cosi' non puo' peggiorare. */
    struct ClickException { const char* id; const char* label; double limit; };

    /** Controlli comuni a un gruppo della tappa 4: click cambiando i comandi (clicksS2, come 3A/3B) e determinismo (due istanze
        nuove: uscita identica bit per bit; con 'needReset' anche la prima istanza dopo reset(); frase stereo di 1 s del banco s4). */
    void addClickAndDeterminism (Lab& lab, const char* const* ids, int n, const juce::String& group, double clickLimit, bool needReset,
                                 const std::vector<ClickException>& exceptions = {})
    {
        std::vector<int> hc, hd, hx;
        auto what = std::make_shared<std::vector<juce::String>> ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            const char* id = ids[i];
            std::vector<juce::String> exLabels;
            for (const auto& x : exceptions) if (std::strcmp (x.id, id) == 0) exLabels.push_back (x.label);
            hc.push_back (lab.add (id, [id, what, i, exLabels] (const pt::engine::ModelDef& d)
            {
                const auto r = clicksS2 (id, [&d, &exLabels] (int c, float) {
                    for (const auto& l : exLabels) if (l == d.controls[c].label) return true;
                    return false;
                });
                (*what)[(size_t) i] = r.what;
                return std::vector<double> { r.found ? r.worst : 1e9 };
            }));
            lab.first (hc.back());
            hd.push_back (lab.add (id, [] (const pt::engine::ModelDef& d)
            {
                static const Buf g = guitarS4 (1.0, 9);
                uint64_t h[3];
                auto a = makeAfter (d, {}), b = makeAfter (d, {});
                for (int k = 0; k < 3; ++k)
                {
                    auto& fx = k == 2 ? *b : *a;
                    if (k == 1) fx.reset();
                    Buf l = g, r = g;
                    for (auto& x : r) x *= 0.8f;
                    runStereo256 (fx, l, r);
                    h[k] = fnvBytes (r, fnvBytes (l));
                }
                return std::vector<double> { h[0] == h[2] ? 1.0 : 0.0, h[0] == h[1] ? 1.0 : 0.0 };
            }));
        }
        for (const auto& x : exceptions)
        {
            const juce::String label (x.label);
            hx.push_back (lab.add (x.id, [x, label] (const pt::engine::ModelDef& d)
            {
                return std::vector<double> { clicksS2 (x.id, [&d, &label] (int c, float) { return label != d.controls[c].label; }).worst };
            }));
            lab.first (hx.back());
        }
        juce::String exText;
        for (const auto& x : exceptions) exText << (exText.isEmpty() ? "; segnalati a parte: " : ", ") << x.id << " " << x.label << " <= " << juce::String (x.limit, 0) << "x";
        auto needs = hc;
        needs.insert (needs.end(), hx.begin(), hx.end());
        lab.check (needs, group + ": niente click cambiando i comandi (seno 220 Hz, differenza seconda <= " + juce::String (clickLimit, 0) + "x prima/regime, "
                              + juce::String (n) + " modelli" + exText + ")",
                   [&lab, hc, hx, ids, n, clickLimit, what, exceptions] {
                       double worst = 0;
                       juce::String worstWhat, bad, ex;
                       for (int i = 0; i < n; ++i)
                       {
                           const double w = lab.v (hc[(size_t) i]);
                           if (w > clickLimit) bad << ids[i] << " " << num (w, 1) << "x (" << (*what)[(size_t) i] << ") ";
                           if (w > worst) { worst = w; worstWhat = " (" + juce::String (ids[i]) + " " + (*what)[(size_t) i] + ")"; }
                       }
                       for (size_t k = 0; k < exceptions.size(); ++k)
                       {
                           const double w = lab.v (hx[k]);
                           ex << exceptions[k].id << " " << exceptions[k].label << " " << num (w, 1) << "x; ";
                           if (w > exceptions[k].limit) bad << exceptions[k].id << " " << exceptions[k].label << " " << num (w, 1) << "x ";
                       }
                       return Verdict { bad.isEmpty(), "peggiore " + num (worst, 1) + "x" + worstWhat + (ex.isEmpty() ? juce::String() : "; " + ex.trimCharactersAtEnd ("; "))
                                                         + " " + bad.trim() };
                   });
        lab.check (hd, group + ": determinismo (due istanze nuove: uscita identica bit per bit" + (needReset ? juce::String (", anche dopo reset()") : juce::String()) + ", "
                              + juce::String (n) + " modelli)",
                   [&lab, hd, ids, n, needReset] {
                       juce::String bad;
                       int ok = 0, okReset = 0;
                       for (int i = 0; i < n; ++i)
                       {
                           const bool same = lab.v (hd[(size_t) i], 0) > 0.5, sameReset = lab.v (hd[(size_t) i], 1) > 0.5;
                           if (same) ++ok; else bad << ids[i] << " ";
                           if (sameReset) ++okReset; else if (needReset) bad << ids[i] << " (reset) ";
                       }
                       return Verdict { ok == n && (! needReset || okReset == n), juce::String (ok) + "/" + juce::String (n) + " identici, dopo reset() "
                                                                                    + juce::String (okReset) + "/" + juce::String (n) + " " + bad.trim() };
                   });
    }

    /** Tappa 4, lavoro s1: overdrive, distorsioni, fuzz e multi analogici Ibanez (Circuit.cpp). Metodi del banco s1 (comandi
        per ruolo/etichetta prima di prepare, blocchi da 512, risposta a piccolo segnale, riff Karplus-Strong, armoniche). */
    void runStage4DriveTests (const std::function<void (const juce::String&, bool, const juce::String&)>& report)
    {
        using namespace pt::engine;
        constexpr int numDrive = countOf (kStage4Drive);
        Lab lab;
        auto fr = [&lab] (const char* id, Sets sets, std::vector<double> freqs, double amp = 1.0e-4)
        {
            return lab.add (id, [sets, freqs, amp] (const ModelDef& d) { return smallSignalDb (d, sets, freqs, amp); });
        };
        auto frDef = [&lab] (const ModelDef& def, Sets sets, std::vector<double> freqs, double amp = 1.0e-4)
        {
            return lab.add (def, [sets, freqs, amp] (const ModelDef& d) { return smallSignalDb (d, sets, freqs, amp); });
        };
        static const Buf riff = guitarS1 (true, 48000.0, 3.0, -12.0);
        /** Riff a -12 dBFS: { RMS dBFS, picco dBFS, finito, x tempo reale (CPU del thread) } (RMS e picco dopo 50 ms). */
        auto riffOf = [] (const ModelDef& d, const std::function<void (Effect&)>& setup)
        {
            auto fx = createEffect (d);
            setup (*fx);
            fx->prepare (48000.0, 512);
            Buf y = riff;
            const double t0 = threadCpuSeconds();
            processMono (*fx, y, 512);
            const double cpu = threadCpuSeconds() - t0;
            const size_t skip = (size_t) (0.05 * 48000.0);
            double acc = 0, pk = 0;
            bool fin = true;
            for (size_t i = skip; i < y.size(); ++i)
            {
                fin = fin && std::isfinite (y[i]);
                acc += (double) y[i] * y[i];
                pk = std::max (pk, (double) std::abs (y[i]));
            }
            return std::vector<double> { 10.0 * std::log10 (acc / (double) (y.size() - skip) + 1e-30), dbOf (pk), fin ? 1.0 : 0.0,
                                         3.0 / std::max (1.0e-6, cpu) };
        };
        auto riffSets = [&lab, riffOf] (const char* id, Sets sets)
        {
            return lab.add (id, [riffOf, sets] (const ModelDef& d) { return riffOf (d, [&] (Effect& fx) { setsS1 (fx, d, sets); }); });
        };
        auto harm = [&lab] (const char* id, Sets sets, std::vector<double> hs)
        {
            return lab.add (id, [sets, hs] (const ModelDef& d) { auto fx = makeS1 (d, sets); return harmonicsOf (*fx, 220.0, -20.0, hs); });
        };

        // 1. pedali esistenti invariati bit per bit dopo le modifiche della tappa 4 a Circuit.cpp (fbclip, triodo, stadi nuovi):
        //    banco s1 'raw', impronte misurate con GCC su Linux x86-64 (il banco ha confrontato tutti i 190 modelli Circuit).
        //    'fixed' = prima meta' a comandi fermi (invariata dalla tappa 4); 'hash' = segnale intero con i comandi mossi, nuovo
        //    dalla 1.2.7 (comandi senza scatti; il banco della 1.2.7 ha confrontato a comandi fermi tutti i 672 modelli)
        {
            struct Ref { const char* id; uint64_t fixed, hash; };
            static const Ref refs[] = { { "ed9", 0x5a9b673524c55b7eull, 0xa0db19c13960768dull }, { "hm2", 0xb9650518c5ae4865ull, 0x7ca0e7af09f6df15ull },
                                        { "ds2", 0x6a0c81c9e25a17a3ull, 0x9d703e6507803b33ull }, { "bmramshead", 0x819df799488b6b5full, 0xa3b0c7312a14f40dull },
                                        { "metalmuff", 0xde23ddf28496fd25ull, 0x63834e3f03f90469ull },
                                        { "ehbritvalve", 0xc553564d983ef672ull, 0xaa9d2210cf1d5947ull }, { "ehtortion", 0x1feba8352382348aull, 0x9d1cd18ba70b16acull } };
            static const Buf raw = rawSignalS1 (48000.0);
            std::vector<int> h;
            for (const auto& r : refs)
                h.push_back (lab.add (r.id, [] (const ModelDef& d)
                {
                    uint64_t f = 0;
                    const uint64_t v = rawHashS1 (d, raw, &f);
                    return std::vector<double> { (double) (uint32_t) (v >> 32), (double) (uint32_t) v, (double) (uint32_t) (f >> 32), (double) (uint32_t) f };
                }));
            for (int k : h) lab.first (k);
            lab.check (h, "Tappa 4: ED-9, HM-2, DS-2, Ram's Head, Metal Muff, British Valve, Tortion invariati bit per bit (Circuit.cpp)", [&lab, h] {
                if (! kBitExactRefs) return Verdict { true, "impronte di riferimento misurate con GCC su Linux: confronto non applicabile a questo compilatore" };
                juce::String bad;
                for (size_t k = 0; k < h.size(); ++k)
                {
                    const uint64_t v = ((uint64_t) lab.v (h[k], 0) << 32) | (uint64_t) lab.v (h[k], 1);
                    const uint64_t f = ((uint64_t) lab.v (h[k], 2) << 32) | (uint64_t) lab.v (h[k], 3);
                    if (f != refs[k].fixed) bad << refs[k].id << " DIVERSO a comandi fermi (" << juce::String::toHexString ((juce::int64) f) << ") ";
                    if (v != refs[k].hash) bad << refs[k].id << " DIVERSO (" << juce::String::toHexString ((juce::int64) v) << ") ";
                }
                return Verdict { bad.isEmpty(), bad.isEmpty() ? juce::String ((int) h.size()) + "/" + juce::String ((int) h.size()) + " hash identici (comandi fermi e mossi)" : bad.trim() };
            });
        }

        // 2. stadi nuovi del motore (netlist di prova)
        {
            const std::vector<double> f3 { 100.0, 1000.0, 10000.0 };
            const int a = frDef (circuitDef ("mna R.i.a=10k C.a.0=10n OUT.a=1"), {}, f3), b = frDef (circuitDef ("lpf R=10k C=10n"), {}, f3);
            lab.check ({ a, b }, "Circuito: mna (rete RC) uguale a 'lpf R=10k C=10n' a 100 Hz / 1 kHz / 10 kHz (scarto < 0.02 dB)", [&lab, a, b] {
                double worst = 0;
                for (size_t k = 0; k < 3; ++k) worst = std::max (worst, std::abs (lab.v (a, k) - lab.v (b, k)));
                return Verdict { worst < 0.02, "mna " + dbList (lab.all (a)) + " dB, scarto max " + num (worst, 3) + " dB" };
            });
        }
        {
            const std::vector<double> f6 { 100.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0 };
            const int a = frDef (circuitDef ("mna R.i.x=1k C.x.0=220n R.x.w=pot(0,20k,B) R.w.m=potr(0,20k,B) R.w.c=220 C.c.0=220n O.x.m.o=1 R.o.m=1k OUT.o=1", 1),
                                 { { "k", 0.3f } }, f6);
            const int b = frDef (circuitDef ("tonets R7=1k C4=220n P=20k t=taper(0,B) Rw=220 Cw=220n Rf=1k Cf=0", 1), { { "k", 0.3f } }, f6);
            lab.check ({ a, b }, "Circuito: mna con op-amp e potenziometro uguale a 'tonets' (TONE 0.3, 100 Hz - 8 kHz, scarto < 0.02 dB)", [&lab, a, b] {
                double worst = 0;
                for (size_t k = 0; k < 6; ++k) worst = std::max (worst, std::abs (lab.v (a, k) - lab.v (b, k)));
                return Verdict { worst < 0.02, "mna " + dbList (lab.all (a)) + " dB, scarto max " + num (worst, 3) + " dB" };
            });
        }
        {
            const int a = frDef (circuitDef ("mna R.i.a=1k L.a.b=4.7 C.b.c=470n R.c.0=1k OUT.c=1"), {}, { 100.0 });
            lab.check ({ a }, "Circuito: mna con induttanza (RLC serie risonante a 107 Hz) a 100 Hz = -6.2 dB +-0.3", [&lab, a] {
                return Verdict { within (lab.v (a), -6.2, 0.3), num (lab.v (a)) + " dB" };
            });
        }
        {
            const int a = frDef (circuitDef ("fbclip Rg=1k Cg=0 Rf=1M Cf=0 up=0 dn=0 gbw=3M sr=100 rail=10"), {}, { 1000.0, 8000.0 }, 1.0e-5);
            const int b = frDef (circuitDef ("opni Rg=1k Cg=0 Rf=1M Cf=0"), {}, { 1000.0, 8000.0 }, 1.0e-5);
            lab.check ({ a, b }, "Circuito: op-amp reale fbclip gbw=3M (guadagno 1001): 59.5 dB a 1 kHz (+-0.2), 8 kHz - 1 kHz = -8.9 dB (+-0.6); "
                                 "opni ideale 60.0 dB piatto (+-0.05)", [&lab, a, b] {
                const double g1 = lab.v (a, 0), d8 = lab.v (a, 1) - g1, i1 = lab.v (b, 0), id8 = lab.v (b, 1) - i1;
                return Verdict { within (g1, 59.5, 0.2) && within (d8, -8.9, 0.6) && within (i1, 60.0, 0.05) && std::abs (id8) <= 0.05,
                                 "fbclip " + num (g1) + " / " + num (d8) + " dB, opni " + num (i1) + " / " + num (id8) + " dB" };
            });
        }
        {
            // ampiezza della fondamentale (0.75-1.5 s): triangolo +-0.5 V -> 8 / pi^2 x 0.5 = 0.405 V (il picco dei campioni, ~0.48 V,
            // contiene anche le armoniche del triangolo ripiegate sotto Nyquist)
            const int a = lab.add (circuitDef ("fbclip Rg=1k Cg=0 Rf=1k Cf=0 up=0 dn=0 gbw=100M sr=0.02 rail=10"), [] (const ModelDef& d)
            {
                auto fx = makeS1 (d, {});
                return harmonicsOf (*fx, 10000.0, 0.0, { 1.0 });
            });
            lab.check ({ a }, "Circuito: slew rate fbclip sr=0.02 V/us, seno 10 kHz 0 dBFS: triangolo +-0.5 V, fondamentale 0.41 V (+-0.05)", [&lab, a] {
                return Verdict { within (lab.v (a), 0.41, 0.05), num (lab.v (a), 3) + " V" };
            });
        }
        {
            const std::vector<double> f3 { 100.0, 1000.0, 8000.0 };
            const int a = frDef (circuitDef ("fbclip Rg=47 Cg=2.2u Rg2=560 Cg2=4.7u Rf=10k Cf=0 up=0 dn=0 gbw=1000M sr=1000 rail=10"), {}, f3, 1.0e-5);
            const int b = frDef (circuitDef ("mna O.i.n.o=1 R.o.n=10k R.n.a=47 C.a.0=2.2u R.n.b=560 C.b.0=4.7u OUT.o=1"), {}, f3, 1.0e-5);
            lab.check ({ a, b }, "Circuito: fbclip con il secondo ramo Rg2/Cg2 uguale all'analisi mna (100 Hz / 1 kHz / 8 kHz, scarto < 0.05 dB)", [&lab, a, b] {
                double worst = 0;
                for (size_t k = 0; k < 3; ++k) worst = std::max (worst, std::abs (lab.v (a, k) - lab.v (b, k)));
                return Verdict { worst < 0.05, "fbclip " + dbList (lab.all (a)) + " dB, scarto max " + num (worst, 3) + " dB" };
            });
        }
        {
            const int a = lab.add (circuitDef ("fbclip Rg=47 Cg=2.2u Rf=30k Cf=100p up=0 dn=0 gbw=10M sr=13 rail=4.0"), [] (const ModelDef& d)
            {
                auto fx = makeS1 (d, {});
                return harmonicsOf (*fx, 220.0, -20.0, { 1.0, 2.0 });
            });
            const int t = harm ("ib9ts9", {}, { 1.0, 2.0 });
            const int n = harm ("ibtsnts", {}, { 1.0, 2.0 });
            lab.check ({ a, t }, "Circuito: fbclip senza diodi ai binari e TS9 con diodi simmetrici: H2/H1 < -100 dB (seno 220 Hz -20 dBFS)", [&lab, a, t] {
                const double h = dbOf (lab.v (a, 1) / lab.v (a, 0)), ht = dbOf (lab.v (t, 1) / lab.v (t, 0));
                return Verdict { h < -100.0 && ht < -100.0, "fbclip " + num (h, 1) + " dB, TS9 " + num (ht, 1) + " dB" };
            });
            lab.check ({ n }, "NTS (triodo Nutube a griglia positiva): H2/H1 = -14 dB (-20..-8) col seno 220 Hz a -20 dBFS", [&lab, n] {
                const double h = dbOf (lab.v (n, 1) / lab.v (n, 0));
                return Verdict { h > -20.0 && h < -8.0, num (h, 1) + " dB" };
            });
        }
        {
            const int a = lab.add (circuitDef ("bbd ms=10 mod=0 rate=0 fb=0 bw=20000 max=20"), [] (const ModelDef& d)
            {
                auto fx = makeS1 (d, {});
                Buf x = sineBuf (48000.0, 25.0, 0.1, 1.5);
                processMono (*fx, x, 512);
                const auto [s, c] = sineFit (x, 25.0, (size_t) (0.5 * 48000), x.size());
                double ms = std::atan2 (-c, s) / (2 * kPi * 25.0) * 1000.0;
                if (ms < 0) ms += 40.0;
                return std::vector<double> { ms };
            });
            lab.check ({ a }, "Circuito: stadio bbd ms=10 (seno 25 Hz, fase ai minimi quadrati): ritardo 10.1 ms (+-0.2)", [&lab, a] {
                return Verdict { within (lab.v (a), 10.1, 0.2), num (lab.v (a), 3) + " ms" };
            });
        }

        // 3. livelli dei 59 modelli: riff a -12 dBFS con i comandi di default (finito, picco < 0 dBFS, RMS -36..-11 dBFS) e con i
        //    comandi tutti al massimo / al minimo, selettori e levette all'ultima / prima posizione (finito, picco < +18 dBFS);
        //    costo: i tre modelli piu' lenti nel lavoro parallelo, rimisurati da soli, sopra 3 x tempo reale in mono
        {
            std::vector<int> hDef, hMax, hMin;
            for (int m = 0; m < numDrive; ++m)
            {
                const char* id = kStage4Drive[m];
                hDef.push_back (lab.add (id, [riffOf] (const ModelDef& d) { return riffOf (d, [] (Effect&) {}); }));
                for (float v : { 1.0f, 0.0f })
                    (v > 0.5f ? hMax : hMin).push_back (lab.add (id, [riffOf, v] (const ModelDef& d)
                    {
                        return riffOf (d, [&] (Effect& fx) { for (int k = 0; k < d.numControls; ++k) fx.params[k].store (v); });
                    }));
            }
            lab.check (hDef, "Tappa 4 drive: livelli con riff a -12 dBFS (59 modelli: finiti, picco < 0 dBFS, RMS -36..-11 dBFS)", [&lab, hDef] {
                int good = 0;
                double lo = 0, hi = -200, pk = -200;
                juce::String bad;
                for (int m = 0; m < numDrive; ++m)
                {
                    const double rms = lab.v (hDef[(size_t) m], 0), p = lab.v (hDef[(size_t) m], 1);
                    const bool g = lab.v (hDef[(size_t) m], 2) > 0.5 && p < 0.0 && rms > -36.0 && rms < -11.0;
                    if (g) ++good; else bad << kStage4Drive[m] << " (RMS " << num (rms, 1) << ", picco " << num (p, 1) << ") ";
                    lo = std::min (lo, rms); hi = std::max (hi, rms); pk = std::max (pk, p);
                }
                return Verdict { good == numDrive, juce::String (good) + "/59, RMS " + num (lo, 1) + ".." + num (hi, 1) + " dBFS, picco max " + num (pk, 1)
                                                       + " dBFS " + bad.trim() };
            });
            std::vector<int> hExt = hMax;
            hExt.insert (hExt.end(), hMin.begin(), hMin.end());
            lab.check (hExt, "Tappa 4 drive: comandi tutti al massimo e tutti al minimo (59 modelli, riff a -12 dBFS): finiti, picco < +18 dBFS", [&lab, hMax, hMin] {
                double worst = -200;
                juce::String bad, worstId;
                for (int m = 0; m < numDrive; ++m)
                    for (int s = 0; s < 2; ++s)
                    {
                        const int h = (s == 0 ? hMax : hMin)[(size_t) m];
                        const bool fin = lab.v (h, 2) > 0.5;
                        const double p = fin ? lab.v (h, 1) : 999.0;
                        if (p > worst) { worst = p; worstId = juce::String (kStage4Drive[m]) + (s == 0 ? " max" : " min"); }
                        if (! fin || p >= 18.0) bad << kStage4Drive[m] << (s == 0 ? "/max" : "/min") << (fin ? "" : " NaN") << " ";
                    }
                return Verdict { bad.isEmpty(), "picco max " + num (worst, 1) + " dBFS (" + worstId + ") " + bad.trim() };
            });
            lab.check (hDef, "Tappa 4 drive: costo, i 3 modelli piu' lenti (riff mono 3 s, rimisurati da soli) sopra 3 x tempo reale", [&lab, hDef] {
                std::vector<std::pair<double, int>> speed;
                for (int m = 0; m < numDrive; ++m) speed.push_back ({ lab.v (hDef[(size_t) m], 3), m });
                std::sort (speed.begin(), speed.end());
                bool ok = true;
                juce::String detail;
                for (int k = 0; k < 3; ++k)
                {
                    const auto* d = findModel (kStage4Drive[speed[(size_t) k].second]);
                    auto fx = makeS1 (*d, {});
                    Buf y = riff;
                    const auto t0 = std::chrono::steady_clock::now();
                    processMono (*fx, y, 512);
                    const double x = 3.0 / std::max (1.0e-6, std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count());
                    ok = ok && x > 3.0;
                    detail << d->id << " " << num (x, 1) << "x ";
                }
                return Verdict { ok, detail.trim() };
            });
        }

        // 4. famiglia Tube Screamer: differenze misurabili (risposta a piccolo segnale a 1e-4 V, riff, armoniche)
        {
            const std::vector<double> f6 { 100.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0 };
            const int ts808 = fr ("ibtsts808hw", {}, f6), ts9 = fr ("ib9ts9", {}, f6), ts5 = fr ("ibstts5", {}, f6), ed9 = fr ("ed9", {}, f6);
            const int msl = fr ("ib10msl", {}, f6);
            const int r808 = riffSets ("ibtsts808hw", {}), r9 = riffSets ("ib9ts9", {}), r5 = riffSets ("ibstts5", {});
            auto worstDiff = [&lab] (int a, int b) { double w = 0; for (size_t k = 0; k < 6; ++k) w = std::max (w, std::abs (lab.v (a, k) - lab.v (b, k))); return w; };
            lab.check ({ ts808, ts9, r808, r9 }, "Tube Screamer: TS808HW e TS9 quasi identici (100 Hz - 8 kHz e RMS del riff entro 0.1 dB)", [&lab, worstDiff, ts808, ts9, r808, r9] {
                const double w = worstDiff (ts808, ts9), dr = std::abs (lab.v (r808) - lab.v (r9));
                return Verdict { w < 0.1 && dr < 0.1, "scarto max " + num (w, 3) + " dB, riff " + num (dr, 3) + " dB" };
            });
            // risposte relative a 1 kHz, come la tabella del banco (a 1 kHz l'uscita caricata 100 ohm / 10 k toglie 0.06 dB)
            lab.check ({ ts808, ed9 }, "Tube Screamer: ED-9 (op-amp ideale) e TS808HW (op-amp reale, uscita caricata), risposta relativa a 1 kHz entro 0.35 dB "
                                       "(100 Hz - 8 kHz)", [&lab, ts808, ed9] {
                double w = 0;
                for (size_t k = 0; k < 6; ++k) w = std::max (w, std::abs ((lab.v (ts808, k) - lab.v (ts808, 2)) - (lab.v (ed9, k) - lab.v (ed9, 2))));
                return Verdict { w < 0.35, "scarto max " + num (w, 3) + " dB (8 kHz " + num ((lab.v (ts808, 5) - lab.v (ts808, 2)) - (lab.v (ed9, 5) - lab.v (ed9, 2)), 2)
                                               + " dB), 1 kHz " + num (lab.v (ts808, 2) - lab.v (ed9, 2), 3) + " dB" };
            });
            lab.check ({ ts5, ts9, r5, r9 }, "Tube Screamer: TS5 = TS9 (stesso circuito, comandi in altro ordine; entro 0.01 dB)", [&lab, worstDiff, ts5, ts9, r5, r9] {
                const double w = worstDiff (ts5, ts9), dr = std::abs (lab.v (r5) - lab.v (r9));
                return Verdict { w < 0.01 && dr < 0.01, "scarto max " + num (w, 4) + " dB, riff " + num (dr, 4) + " dB" };
            });
            lab.check ({ msl, ts9 }, "Tube Screamer: MSL (82n) contro TS9 (47n), 100 Hz - 1 kHz = -8.0 / -11.3 dB (+-0.5)", [&lab, msl, ts9] {
                const double m = lab.v (msl, 0) - lab.v (msl, 2), t = lab.v (ts9, 0) - lab.v (ts9, 2);
                return Verdict { within (m, -8.0, 0.5) && within (t, -11.3, 0.5), "MSL " + num (m) + " dB, TS9 " + num (t) + " dB" };
            });
        }
        {
            std::vector<int> h;
            for (float mode : { 0.0f, 1.0f / 3.0f, 2.0f / 3.0f, 1.0f }) h.push_back (riffSets ("ibtsts9dx", { { "MODE", mode } }));
            lab.check (h, "TS9DX: RMS del riff nei modi TS9 / + / HOT / TURBO = -34.6 / -28.1 / -25.2 / -22.4 dBFS (+-1.5), crescente", [&lab, h] {
                const double ref[] = { -34.6, -28.1, -25.2, -22.4 };
                bool ok = true;
                for (size_t k = 0; k < 4; ++k) ok = ok && within (lab.v (h[k]), ref[k], 1.5) && (k == 0 || lab.v (h[k]) > lab.v (h[k - 1]));
                return Verdict { ok, num (lab.v (h[0]), 1) + " / " + num (lab.v (h[1]), 1) + " / " + num (lab.v (h[2]), 1) + " / " + num (lab.v (h[3]), 1) + " dBFS" };
            });
            const int turbo = harm ("ibtsts9dx", { { "MODE", 1.0f } }, { 1.0, 3.0 }), ts9m = harm ("ibtsts9dx", { { "MODE", 0.0f } }, { 1.0, 3.0 });
            lab.check ({ turbo, ts9m }, "TS9DX: TURBO senza diodi quasi pulito, H3/H1 -61 dB (+-5) contro -17 dB del modo TS9 (+-5; seno 220 Hz -20 dBFS)", [&lab, turbo, ts9m] {
                const double a = dbOf (lab.v (turbo, 1) / lab.v (turbo, 0)), b = dbOf (lab.v (ts9m, 1) / lab.v (ts9m, 0));
                return Verdict { within (a, -61.0, 5.0) && within (b, -17.0, 5.0), "TURBO " + num (a, 1) + " dB, TS9 " + num (b, 1) + " dB" };
            });
        }
        {
            const int hot = fr ("ibtlts7", { { "MODE", 1.0f } }, { 1000.0 }), nrm = fr ("ibtlts7", { { "MODE", 0.0f } }, { 1000.0 });
            lab.check ({ hot, nrm }, "TS7: MODE HOT - TS9 a 1 kHz = +7.6 dB (+-1)", [&lab, hot, nrm] {
                const double d = lab.v (hot) - lab.v (nrm);
                return Verdict { within (d, 7.6, 1.0), num (d) + " dB" };
            });
        }
        {
            const int on = riffSets ("ibtsts808dx", { { "BOOST", 1.0f }, { "PRE/POST", 1.0f } }), off = riffSets ("ibtsts808dx", { { "BOOST", 0.0f }, { "PRE/POST", 1.0f } });
            lab.check ({ on, off }, "TS808DX: booster POST al massimo +20 dB sull'RMS del riff (+-2)", [&lab, on, off] {
                const double d = lab.v (on) - lab.v (off);
                return Verdict { within (d, 20.0, 2.0), num (d) + " dB (" + num (lab.v (off), 1) + " -> " + num (lab.v (on), 1) + " dBFS)" };
            });
            auto all = [&lab, riffOf] (float volt)
            {
                return lab.add ("ibtsts808dx", [riffOf, volt] (const ModelDef& d)
                {
                    return riffOf (d, [&] (Effect& fx) { for (int k = 0; k < d.numControls; ++k) fx.params[k].store (1.0f); setsS1 (fx, d, { { "18V/9V", volt } }); });
                });
            };
            const int v18 = all (1.0f), v9 = all (0.0f);
            lab.check ({ v18, v9 }, "TS808DX: tutti al massimo, 18 V contro 9 V: picco piu' alto di oltre 2.5 dB (binari +-8.4 / +-4 V; atteso +3.9)", [&lab, v18, v9] {
                const double d = lab.v (v18, 1) - lab.v (v9, 1);
                return Verdict { d > 2.5, num (d) + " dB (" + num (lab.v (v9, 1), 1) + " -> " + num (lab.v (v18, 1), 1) + " dBFS)" };
            });
        }
        {
            const int s1 = fr ("ibtstsv808", { { "SAT", 1.0f }, { "OVERDRIVE", 0.5f } }, { 1000.0 }), s0 = fr ("ibtstsv808", { { "SAT", 0.0f }, { "OVERDRIVE", 0.5f } }, { 1000.0 });
            lab.check ({ s1, s0 }, "TSV808: SAT 1 - SAT 0 a 1 kHz = +13.4 dB (+-1) con OVERDRIVE a meta'", [&lab, s1, s0] {
                const double d = lab.v (s1) - lab.v (s0);
                return Verdict { within (d, 13.4, 1.0), num (d) + " dB" };
            });
            const int asym = harm ("ibtstsv808", { { "DIODE", 2.0f / 3.0f } }, { 1.0, 2.0 }), sym = harm ("ibtstsv808", { { "DIODE", 0.0f } }, { 1.0, 2.0 });
            lab.check ({ asym, sym }, "TSV808: diodi ASYM 1 H2/H1 -19 dB (+-4), SYM 1 < -100 dB (seno 220 Hz -20 dBFS)", [&lab, asym, sym] {
                const double a = dbOf (lab.v (asym, 1) / lab.v (asym, 0)), s = dbOf (lab.v (sym, 1) / lab.v (sym, 0));
                return Verdict { within (a, -19.0, 4.0) && s < -100.0, "ASYM " + num (a, 1) + " dB, SYM " + num (s, 1) + " dB" };
            });
        }
        {
            std::vector<int> h;
            for (float mix : { 0.0f, 0.5f, 1.0f }) h.push_back (riffSets ("ibtsnts", { { "MIX", mix } }));
            lab.check (h, "NTS: RMS del riff con MIX 0 / 0.5 / 1 = -25.4 / -22.7 / -20.5 dBFS (+-1.5)", [&lab, h] {
                const double ref[] = { -25.4, -22.7, -20.5 };
                bool ok = true;
                for (size_t k = 0; k < 3; ++k) ok = ok && within (lab.v (h[k]), ref[k], 1.5);
                return Verdict { ok, num (lab.v (h[0]), 1) + " / " + num (lab.v (h[1]), 1) + " / " + num (lab.v (h[2]), 1) + " dBFS" };
            });
        }

        // 5. comandi di tono e di voce (circuito dagli schemi)
        {
            const std::vector<double> f3 { 100.0, 1000.0, 8000.0 };
            const int t0 = fr ("ib7od850", { { "SUSTAIN", 0.0f }, { "TONE", 0.0f } }, f3), t1 = fr ("ib7od850", { { "SUSTAIN", 0.0f }, { "TONE", 1.0f } }, f3);
            lab.check ({ t0, t1 }, "OD-850: col TONE i bassi restano fermi (100 Hz - 1 kHz +3.1 / +0.4 dB) e gli acuti salgono (8 kHz - 1 kHz -31.6 / -21.9 dB; +-1)", [&lab, t0, t1] {
                const double b0 = lab.v (t0, 0) - lab.v (t0, 1), b1 = lab.v (t1, 0) - lab.v (t1, 1), h0 = lab.v (t0, 2) - lab.v (t0, 1), h1 = lab.v (t1, 2) - lab.v (t1, 1);
                return Verdict { within (b0, 3.1, 1.0) && within (b1, 0.4, 1.0) && within (h0, -31.6, 1.0) && within (h1, -21.9, 1.0),
                                 "bassi " + num (b0) + " / " + num (b1) + " dB, acuti " + num (h0) + " / " + num (h1) + " dB" };
            });
        }
        {
            const int t0 = fr ("ibtlds7", { { "DRIVE", 0.0f }, { "TONE", 0.0f } }, { 1000.0, 8000.0 }), t1 = fr ("ibtlds7", { { "DRIVE", 0.0f }, { "TONE", 1.0f } }, { 1000.0, 8000.0 });
            lab.check ({ t0, t1 }, "DS7: TONE a giratori, 8 kHz - 1 kHz = -16.6 / +7.2 dB a TONE 0 / 1 (+-1)", [&lab, t0, t1] {
                const double a = lab.v (t0, 1) - lab.v (t0, 0), b = lab.v (t1, 1) - lab.v (t1, 0);
                return Verdict { within (a, -16.6, 1.0) && within (b, 7.2, 1.0), num (a) + " / " + num (b) + " dB" };
            });
        }
        {
            const std::vector<double> f3 { 100.0, 1000.0, 2000.0 };
            const int m0 = fr ("ib9st9", { { "DRIVE", 0.0f }, { "MID BOOST", 0.0f } }, f3), m1 = fr ("ib9st9", { { "DRIVE", 0.0f }, { "MID BOOST", 1.0f } }, f3);
            lab.check ({ m0, m1 }, "ST9: MID BOOST 0 / 1, 2 kHz - 1 kHz = -2.0 / +2.1 dB, 100 Hz - 1 kHz = -8.5 / -16.0 dB (+-1)", [&lab, m0, m1] {
                const double a = lab.v (m0, 2) - lab.v (m0, 1), b = lab.v (m1, 2) - lab.v (m1, 1), c = lab.v (m0, 0) - lab.v (m0, 1), e = lab.v (m1, 0) - lab.v (m1, 1);
                return Verdict { within (a, -2.0, 1.0) && within (b, 2.1, 1.0) && within (c, -8.5, 1.0) && within (e, -16.0, 1.0),
                                 "2 kHz " + num (a) + " / " + num (b) + " dB, 100 Hz " + num (c) + " / " + num (e) + " dB" };
            });
        }
        {
            const int mid = fr ("ibtssd9m", { { "MIDS", 1.0f } }, { 1000.0 }), scoop = fr ("ibtssd9m", { { "MIDS", 0.0f } }, { 1000.0 });
            lab.check ({ mid, scoop }, "SD9M: MIDS MID - SCOOP a 1 kHz = +4.6 dB (+-1; 100n -> 33n)", [&lab, mid, scoop] {
                const double d = lab.v (mid) - lab.v (scoop);
                return Verdict { within (d, 4.6, 1.0), num (d) + " dB" };
            });
        }

        // click: fino alla 1.2.6 il motore Circuit aggiornava i comandi a blocchi (lisciatura 0.35 per blocco di 512 campioni,
        // selettori a scatto: 74 pedali Circuit oltre 10x, fino a 2271x, e qui una soglia di 100x). Dalla 1.2.7 i pomelli sono
        // lisciati da due poli e ricalcolati a sotto-blocchi di 16 campioni con i coefficienti in rampa, i selettori passano con
        // una dissolvenza di 15 ms: soglia normale di 10x (peggiore dei drive della tappa 4: 1.4x)
        addClickAndDeterminism (lab, kStage4Drive, numDrive, "Tappa 4 drive", 10.0, false);

        // tutti i modelli del motore Circuit (famiglie Circuit e AmpSim con netlist, 249): click <= 10x e determinismo. Il piu'
        // difficile e' SENS dello Stereo Talking Machine (bipolare: attraversando il centro la posizione di riposo del filtro
        // passa da una vocale all'altra e con lo sweep saturo le formanti correvano per tutta la gamma in pochi ms, 287x): con un
        // comando in movimento la posizione delle formanti insegue il valore con un polo di 6 ms (8x)
        {
            static const std::vector<const char*> circuitIds = [] {
                std::vector<const char*> v;
                for (int i = 0; i < numModels(); ++i)
                {
                    const auto& d = model (i);
                    if ((d.family == Family::Circuit || d.family == Family::AmpSim) && ! (d.config != nullptr && std::strncmp (d.config, "type=", 5) == 0))
                        v.push_back (d.id);
                }
                return v;
            }();
            addClickAndDeterminism (lab, circuitIds.data(), (int) circuitIds.size(), "Motore Circuit (tutti i modelli)", 10.0, false);
        }
        lab.run (report);
    }

    /** Tappa 4, lavoro s2: compressori, EQ, filtri, wah, volume e utility (FxStage4Dyn.cpp). Metodi del banco s2 (comandi per
        ruolo/etichetta prima di prepare). */
    void runStage4DynTests (const std::function<void (const juce::String&, bool, const juce::String&)>& report)
    {
        using namespace pt::engine;
        constexpr int numDyn = countOf (kStage4Dyn);
        Lab lab;
        /** Effetto del banco s2: comandi per ruolo/etichetta (setsS2) prima di prepare, blocchi da 'block'. */
        auto makeS2 = [] (const ModelDef& d, const Sets& sets, int block = 512)
        {
            auto fx = createEffect (d);
            for (const auto& [k, v] : setsS2 (d, sets)) fx->params[k].store (v);
            fx->prepare (48000.0, block);
            return fx;
        };
        /** Risposta del banco s2 ('response'): effetto nuovo per ogni frequenza, 0.5 s di assestamento, blocchi da 256. */
        auto respS2 = [makeS2] (const ModelDef& d, const Sets& sets, const std::vector<double>& freqs, double amp)
        {
            std::vector<double> out;
            for (double f : freqs)
            {
                auto fx = makeS2 (d, sets, 256);
                const double sr = 48000.0;
                const int settle = (int) (0.5 * sr), per = (int) std::ceil (0.25 * f) + 2, meas = (int) std::lround (per * sr / f);
                Buf x ((size_t) (settle + meas));
                for (size_t i = 0; i < x.size(); ++i) x[i] = (float) (amp * std::sin (2 * kPi * f * (double) i / sr));
                processMono (*fx, x, 256);
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
        };
        auto resp = [&lab, respS2] (const char* id, Sets sets, std::vector<double> freqs, double amp = 0.01)
        {
            return lab.add (id, [respS2, sets, freqs, amp] (const ModelDef& d) { return respS2 (d, sets, freqs, amp); });
        };
        /** Picco principale a piccolo segnale (impulso di 1 mV, FFT): { Hz, dB }. */
        auto peak = [&lab] (const char* id, Sets sets)
        {
            return lab.add (id, [sets] (const ModelDef& d)
            {
                auto fx = createEffect (d);
                for (const auto& [k, v] : setsS2 (d, sets)) fx->params[k].store (v);
                const auto p = measureWahPeak (*fx, 48000.0);
                return std::vector<double> { p.f, p.db };
            });
        };
        auto peak2 = [&lab] (const char* id, Sets sets)
        {
            return lab.add (id, [sets] (const ModelDef& d)
            {
                auto fx = createEffect (d);
                for (const auto& [k, v] : setsS2 (d, sets)) fx->params[k].store (v);
                const auto p = resonancePeaks (*fx, 2);
                std::vector<double> r;
                for (const auto& [f, g] : p) { r.push_back (f); r.push_back (g); }
                while (r.size() < 4) r.push_back (0.0);
                return r;
            });
        };
        auto fq = [] (double f, double ref) { return std::abs (f / ref - 1.0) <= 0.06; };
        auto hz = [] (double f) { return juce::String (juce::roundToInt (f)); };

        // 1. pedali esistenti invariati bit per bit (FxFilters, FxDynamics, FxUtility): banco s1 'raw' come la 3A (GCB95 e 535Q sono gia'
        //    controllati dalla 3A). 'fixed' = prima meta' a comandi fermi, invariata dalla tappa 4; 'hash' = segnale intero: dalla 1.3.1 le
        //    famiglie vecchie hanno l'involucro senza click (Effects.cpp), quindi la parte dopo lo spostamento dei comandi ha impronte nuove
        //    (FV-500H e FV60, famiglia Volume senza involucro: invariate). Impronte del banco ~/.config/PedalTrinity/work131/bench
        {
            struct Ref { const char* id; uint64_t fixed, hash; };
            static const Ref refs[] = {
                { "cs3", 0x70e0f050fc09bf80ull, 0x99de31d19387f6d4ull }, { "cp1x", 0xd0a80903950f504full, 0xe6f39b961ec6c3f5ull },
                { "gq7", 0xad6ff35c46abd26cull, 0x8c22ad1b7170c34full }, { "eq200", 0xad6ff35c46abd26cull, 0x9043d94c3a6d442dull },
                { "aw3", 0x30cad961679d4f46ull, 0x17239d4a5bb9bdb1ull }, { "fv500h", 0x6c802cf5c813b57cull, 0x37efb2ce81bff398ull },
                { "ge7b", 0xad6ff35c46abd26cull, 0x56bd0d497567baebull }, { "pq4", 0xad6ff35c46abd26cull, 0x2b4358b81a850756ull },
                { "lmb3", 0xbf2bac6783cabccbull, 0x35dc63df4e390eddull }, { "ehsoulpreacher", 0x6f10e9a4616bb9b5ull, 0x69076385c2133acdull },
                { "ehbasspreacher", 0x8bc981c3d09e2b6bull, 0x68bc45a1740deaecull }, { "mc404", 0x383307e741b9b288ull, 0x527ba7c5a09b362eull },
                { "dd95fw", 0x94b33e04874df812ull, 0x5e620130c64421edull }, { "fv60", 0x6c802cf5c813b57cull, 0x38a9d37e24b9a79bull } };
            static const Buf raw = rawSignalS1 (48000.0);
            std::vector<int> h;
            for (const auto& r : refs)
                h.push_back (lab.add (r.id, [] (const ModelDef& d)
                {
                    uint64_t f = 0;
                    const uint64_t v = rawHashS1 (d, raw, &f);
                    return std::vector<double> { (double) (uint32_t) (v >> 32), (double) (uint32_t) v, (double) (uint32_t) (f >> 32), (double) (uint32_t) f };
                }));
            lab.check (h, "Tappa 4: CS-3, CP-1X, GE-7, EQ-200, AW-3, FV-500H, GE-7B, PQ-4, LMB-3, Soul/Bass Preacher, MC404, DD95FW, FV60 invariati bit per bit "
                          "(comandi fermi; mossi: impronte 1.3.1)",
                       [&lab, h] {
                if (! kBitExactRefs) return Verdict { true, "impronte di riferimento misurate con GCC su Linux: confronto non applicabile a questo compilatore" };
                juce::String bad;
                for (size_t k = 0; k < h.size(); ++k)
                {
                    const uint64_t v = ((uint64_t) lab.v (h[k], 0) << 32) | (uint64_t) lab.v (h[k], 1);
                    const uint64_t f = ((uint64_t) lab.v (h[k], 2) << 32) | (uint64_t) lab.v (h[k], 3);
                    if (f != refs[k].fixed) bad << refs[k].id << " DIVERSO a comandi fermi (" << juce::String::toHexString ((juce::int64) f) << ") ";
                    if (v != refs[k].hash) bad << refs[k].id << " DIVERSO (" << juce::String::toHexString ((juce::int64) v) << ") ";
                }
                return Verdict { bad.isEmpty(), bad.isEmpty() ? juce::String ((int) h.size()) + "/" + juce::String ((int) h.size()) + " hash identici (comandi fermi e mossi)" : bad.trim() };
            });
        }

        // 2. livelli dei 41 modelli: riff a -12 dBFS con i comandi di default (finito, picco fra -30 e -3 dBFS, RMS -42..-14 dBFS e
        //    entro 1.5 dB dai valori del banco, 2 dB per i compressori); robustezza: riff + accordo a -6 / -3 dBFS ripetuti per 3 s con
        //    comandi casuali (mt19937 seme 7, 30% dei comandi ogni 1024 campioni; banco: 10 s), poi stereo a blocchi irregolari
        {
            struct Ref { const char* id; double pk, rms; };
            static const Ref refs[] = {
                { "ib7cp830", -8.42, -26.30 }, { "ib7cp835", -9.71, -22.93 }, { "ib9cp9", -9.82, -25.83 }, { "ib10cp10", -11.78, -26.93 },
                { "ib10bp10", -11.68, -26.99 }, { "ibstcp5", -11.78, -26.93 }, { "ibstbp5", -11.68, -26.99 }, { "ibtstc999", -14.82, -30.86 },
                { "ibxec30", -9.29, -25.25 }, { "ib7ge601", -12.00, -28.72 }, { "ib9ge9", -12.00, -28.72 }, { "ib10ge10", -12.00, -28.72 },
                { "ib10be10", -12.00, -28.72 }, { "ib7ge300", -12.00, -28.72 }, { "ib7pq401", -11.99, -28.78 }, { "ib9pq9", -11.99, -28.79 },
                { "ib10pql", -11.99, -28.79 }, { "ib10ppe1", -12.00, -28.72 }, { "ibmpteq", -12.00, -28.72 }, { "ib7af201", -17.77, -34.37 },
                { "ib9af9", -17.78, -34.45 }, { "ib10afl", -17.78, -34.45 }, { "ibtlaw7", -16.46, -32.04 }, { "ibstaw5", -9.40, -24.02 },
                { "ibtllf7", -17.82, -31.60 }, { "ib10nb10", -11.87, -28.88 }, { "ibmptgate", -12.00, -28.72 }, { "ib10wh10", -15.50, -29.63 },
                { "ibmwh10v3", -15.50, -29.63 }, { "ib10wf10", -12.90, -26.97 }, { "ibtlwd7", -15.12, -29.45 }, { "ibmwd7jr", -15.45, -29.59 },
                { "ibmtwp10", -10.32, -25.08 }, { "ibmno93", -9.08, -22.88 }, { "ibmno57", -9.08, -22.88 }, { "ibmno84", -7.93, -18.21 },
                { "ibmno58", -4.99, -15.59 }, { "ib10vl10", -12.00, -28.72 }, { "ibmst810", -15.01, -31.73 }, { "ib10ls10", -12.00, -28.72 },
                { "ibxec50", -11.88, -25.81 } };
            static_assert (sizeof (refs) / sizeof (refs[0]) == (size_t) numDyn, "un valore per ogni modello s2");
            static const Buf riff = guitarS1 (true, 48000.0, 3.0, -12.0);
            static const Buf stress = []
            {
                Buf x = guitarS1 (true, 48000.0, 3.0, -6.0);
                const Buf ch = guitarS1 (false, 48000.0, 2.0, -3.0);
                x.insert (x.end(), ch.begin(), ch.end());
                x.resize ((size_t) (3.0 * 48000.0));
                return x;
            }();
            std::vector<int> hl, hs;
            for (const auto& r : refs)
            {
                hl.push_back (lab.add (r.id, [makeS2] (const ModelDef& d)
                {
                    auto fx = makeS2 (d, {});
                    Buf y = riff;
                    processMono (*fx, y, 512);
                    double acc = 0, pk = 0;
                    bool fin = true;
                    const size_t skip = (size_t) (0.05 * 48000.0);
                    for (size_t i = skip; i < y.size(); ++i) { fin = fin && std::isfinite (y[i]); acc += (double) y[i] * y[i]; pk = std::max (pk, (double) std::abs (y[i])); }
                    return std::vector<double> { dbOf (pk), 10.0 * std::log10 (acc / (double) (y.size() - skip) + 1e-30), fin ? 1.0 : 0.0 };
                }));
                hs.push_back (lab.add (r.id, [makeS2] (const ModelDef& d)
                {
                    std::mt19937 rng (7);
                    std::uniform_real_distribution<float> u (0, 1);
                    auto fx = makeS2 (d, {}, 1024);
                    Buf y = stress;
                    double pk = 0;
                    bool fin = true;
                    for (size_t i = 0; i + 1024 <= y.size(); i += 1024)
                    {
                        for (int c = 0; c < d.numControls; ++c) if (u (rng) < 0.3f) fx->params[c].store (u (rng));
                        float* p[1] = { y.data() + i };
                        fx->process (p, 1, 1024);
                        for (int k = 0; k < 1024; ++k) { fin = fin && std::isfinite (y[i + (size_t) k]); pk = std::max (pk, (double) std::abs (y[i + (size_t) k])); }
                    }
                    auto fx2 = makeS2 (d, {}, 512);
                    Buf l = riff, rr = riff;
                    for (size_t i = 0; i < l.size();)
                    {
                        const int m = (int) std::min<size_t> ((size_t) (1 + (int) (u (rng) * 511)), l.size() - i);
                        float* p[2] = { l.data() + i, rr.data() + i };
                        fx2->process (p, 2, m);
                        for (int k = 0; k < m; ++k) fin = fin && std::isfinite (l[i + (size_t) k]) && std::isfinite (rr[i + (size_t) k]);
                        i += (size_t) m;
                    }
                    return std::vector<double> { dbOf (pk), fin ? 1.0 : 0.0 };
                }));
            }
            lab.check (hl, "Tappa 4 dinamica/EQ/filtri: livelli con riff a -12 dBFS (41 modelli: finiti, picco -30..-3 dBFS, RMS -42..-14 dBFS, "
                           "entro 1.5 dB dal banco, compressori 2 dB)", [&lab, hl] {
                int good = 0;
                double worst = 0;
                juce::String bad;
                for (int m = 0; m < numDyn; ++m)
                {
                    const double pk = lab.v (hl[(size_t) m], 0), rms = lab.v (hl[(size_t) m], 1);
                    const auto* d = findModel (refs[m].id);
                    const double tol = d != nullptr && d->family == Family::Compressor ? 2.0 : 1.5;
                    const double dev = std::max (std::abs (pk - refs[m].pk), std::abs (rms - refs[m].rms));
                    worst = std::max (worst, dev);
                    const bool g = lab.v (hl[(size_t) m], 2) > 0.5 && pk < -3.0 && pk > -30.0 && rms > -42.0 && rms < -14.0 && dev <= tol;
                    if (g) ++good; else bad << refs[m].id << " (picco " << num (pk, 1) << ", RMS " << num (rms, 1) << ") ";
                }
                return Verdict { good == numDyn, juce::String (good) + "/41, scarto max dal banco " + num (worst) + " dB " + bad.trim() };
            });
            lab.check (hs, "Tappa 4 dinamica/EQ/filtri: robustezza (riff + accordo a -6/-3 dBFS, comandi casuali ogni 1024 campioni; stereo a blocchi "
                           "irregolari): finiti, picco < +12 dBFS", [&lab, hs] {
                double worst = -200;
                juce::String bad, worstId;
                for (int m = 0; m < numDyn; ++m)
                {
                    const bool fin = lab.v (hs[(size_t) m], 1) > 0.5;
                    const double pk = fin ? lab.v (hs[(size_t) m], 0) : 999.0;
                    if (pk > worst) { worst = pk; worstId = refs[m].id; }
                    if (! fin || pk >= 12.0) bad << refs[m].id << (fin ? "" : " NaN") << " ";
                }
                return Verdict { bad.isEmpty(), "picco max " + num (worst, 1) + " dBFS (" + worstId + ") " + bad.trim() };
            });
        }

        // 3. wah a pedale: picco a piccolo segnale (frequenze +-6%, guadagni +-1 dB)
        {
            const int h0 = peak ("ib10wh10", { { "PEDAL", 0.0f } }), h5 = peak ("ib10wh10", { { "PEDAL", 0.5f } }), h1 = peak ("ib10wh10", { { "PEDAL", 1.0f } });
            const int b0 = peak ("ib10wh10", { { "PEDAL", 0.0f }, { "RANGE", 1.0f } }), b1 = peak ("ib10wh10", { { "PEDAL", 1.0f }, { "RANGE", 1.0f } });
            const int v5 = peak ("ibmwh10v3", { { "PEDAL", 0.5f } });
            lab.check ({ h0, h5, h1, b0, b1, v5 }, "WH10 (DEPTH 0.5): GUITAR 307 / 826 / 2357 Hz (+8.8 / +8.5 dB in punta e tallone), BASS 153 / 1187 Hz; "
                                                   "WH10V3 uguale", [&lab, fq, hz, h0, h5, h1, b0, b1, v5] {
                const bool ok = fq (lab.v (h0), 307) && fq (lab.v (h5), 826) && fq (lab.v (h1), 2357) && within (lab.v (h0, 1), 8.8, 1.0)
                             && within (lab.v (h1, 1), 8.5, 1.0) && fq (lab.v (b0), 153) && fq (lab.v (b1), 1187)
                             && std::abs (lab.v (v5) / lab.v (h5) - 1.0) < 0.01 && std::abs (lab.v (v5, 1) - lab.v (h5, 1)) < 0.1;
                return Verdict { ok, "GUITAR " + hz (lab.v (h0)) + " (" + num (lab.v (h0, 1), 1) + " dB) / " + hz (lab.v (h5)) + " / " + hz (lab.v (h1))
                                         + " (" + num (lab.v (h1, 1), 1) + " dB), BASS " + hz (lab.v (b0)) + " / " + hz (lab.v (b1)) + ", V3 " + hz (lab.v (v5)) + " Hz" };
            });
        }
        {
            const Sets foot { { "MODE", 0.0f }, { "RANGE", 0.0f }, { "FINE TUNE", 0.5f } };
            auto with = [foot] (Sets s) { Sets r = foot; r.insert (r.end(), s.begin(), s.end()); return r; };
            const int a = peak ("ibtlwd7", with ({ { "PEDAL", 0.25f } })), b = peak ("ibtlwd7", with ({ { "PEDAL", 0.5f } })), c = peak ("ibtlwd7", with ({ { "PEDAL", 1.0f } }));
            const int f1 = peak ("ibtlwd7", with ({ { "PEDAL", 1.0f }, { "FINE TUNE", 1.0f } })), f0 = peak ("ibtlwd7", with ({ { "PEDAL", 1.0f }, { "FINE TUNE", 0.0f } }));
            const int l0 = peak ("ibtlwd7", with ({ { "PEDAL", 0.25f }, { "RANGE", 1.0f } })), l1 = peak ("ibtlwd7", with ({ { "PEDAL", 1.0f }, { "RANGE", 1.0f } }));
            // guadagno in punta: 15.71 dB nelle misure grezze del banco (measure.log); il "+9.7 dB" di tests.md e' un refuso
            lab.check ({ a, b, c, f1, f0, l0, l1 }, "WD7 (FOOT SW, NORMAL): pedale 0.25 / 0.5 / 1 = 435 / 617 / 1568 Hz (+15.7 dB), FINE 1 / 0 in punta 1762 / 1043 Hz, "
                                                    "RANGE LOW 192 .. 654 Hz", [&lab, fq, hz, a, b, c, f1, f0, l0, l1] {
                const bool ok = fq (lab.v (a), 435) && fq (lab.v (b), 617) && fq (lab.v (c), 1568) && within (lab.v (c, 1), 15.7, 1.0)
                             && fq (lab.v (f1), 1762) && fq (lab.v (f0), 1043) && fq (lab.v (l0), 192) && fq (lab.v (l1), 654);
                return Verdict { ok, hz (lab.v (a)) + " / " + hz (lab.v (b)) + " / " + hz (lab.v (c)) + " Hz (" + num (lab.v (c, 1), 1) + " dB), FINE "
                                         + hz (lab.v (f1)) + " / " + hz (lab.v (f0)) + ", LOW " + hz (lab.v (l0)) + " .. " + hz (lab.v (l1)) + " Hz" };
            });
        }
        {
            // accensione automatica: pedale a 0.6 per 1 s poi al tallone; l'effetto (readout 1) si spegne dopo OFF DELAY
            auto offTime = [&lab] (const char* id, Sets sets)
            {
                return lab.add (id, [sets] (const ModelDef& d)
                {
                    auto fx = createEffect (d);
                    for (const auto& [k, v] : setsS2 (d, sets)) fx->params[k].store (v);
                    fx->prepare (48000.0, 480);
                    fx->params[0].store (0.6f);
                    const double sr = 48000.0;
                    Buf x ((size_t) (4 * sr));
                    for (size_t i = 0; i < x.size(); ++i) x[i] = (float) (0.1 * std::sin (2 * kPi * 440 * (double) i / sr));
                    double off = -1, onBefore = 0;
                    for (size_t i = 0; i < x.size(); i += 48)
                    {
                        if (i == (size_t) sr) { onBefore = fx->readout (1); fx->params[0].store (0.0f); }
                        float* p[1] = { x.data() + i };
                        fx->process (p, 1, 48);
                        if (i >= (size_t) sr && off < 0 && fx->readout (1) < 0.5f) off = (double) (i + 48 - (size_t) sr) / sr;
                    }
                    return std::vector<double> { onBefore, off };
                });
            };
            const int a0 = offTime ("ibtlwd7", { { "MODE", 1.0f }, { "OFF DELAY", 0.0f } }), a5 = offTime ("ibtlwd7", { { "MODE", 1.0f }, { "OFF DELAY", 0.5f } });
            const int a1 = offTime ("ibtlwd7", { { "MODE", 1.0f }, { "OFF DELAY", 1.0f } }), ft = offTime ("ibtlwd7", { { "MODE", 0.0f }, { "OFF DELAY", 0.0f } });
            const int jr = offTime ("ibmwd7jr", { { "OFF DELAY", 0.5f } });
            lab.check ({ a0, a5, a1, ft, jr }, "WD7 AUTO SW: al tallone si spegne dopo OFF DELAY 0 / 0.5 / 1 (< 60 ms / 0.25-0.35 s / 2.3-2.7 s); FOOT SW resta acceso; "
                                               "WD7JR automatico", [&lab, a0, a5, a1, ft, jr] {
                auto t = [&lab] (int h) { return lab.v (h, 1); };
                const bool ok = lab.v (a0) > 0.5 && t (a0) >= 0 && t (a0) < 0.06 && t (a5) >= 0.25 && t (a5) <= 0.35 && t (a1) >= 2.3 && t (a1) <= 2.7
                             && lab.v (ft) > 0.5 && t (ft) < 0 && t (jr) >= 0.25 && t (jr) <= 0.35;
                auto s = [&] (int h) { return t (h) < 0 ? juce::String ("acceso") : num (t (h), 3) + " s"; };
                return Verdict { ok, s (a0) + " / " + s (a5) + " / " + s (a1) + ", FOOT SW " + s (ft) + ", WD7JR " + s (jr) };
            });
        }
        {
            const int h0 = peak2 ("ibmtwp10", { { "PEDAL", 0.0f }, { "TONE", 0.0f } }), h1 = peak2 ("ibmtwp10", { { "PEDAL", 1.0f }, { "TONE", 0.0f } });
            const int l0 = peak2 ("ibmtwp10", { { "PEDAL", 0.0f }, { "TONE", 1.0f } }), l1 = peak2 ("ibmtwp10", { { "PEDAL", 1.0f }, { "TONE", 1.0f } });
            lab.check ({ h0, h1, l0, l1 }, "TWP10 (BALANCE 0.5): due picchi, HIGH 307 + 654 / 984 + 3155 Hz, LOW 229 + 461 / 735 + 2098 Hz (tallone / punta, +-6%)",
                       [&lab, fq, hz, h0, h1, l0, l1] {
                auto two = [&] (int h, double a, double b) { return fq (lab.v (h, 0), a) && fq (lab.v (h, 2), b); };
                auto s = [&] (int h) { return hz (lab.v (h, 0)) + " + " + hz (lab.v (h, 2)); };
                return Verdict { two (h0, 307, 654) && two (h1, 984, 3155) && two (l0, 229, 461) && two (l1, 735, 2098),
                                 "HIGH " + s (h0) + " / " + s (h1) + ", LOW " + s (l0) + " / " + s (l1) + " Hz" };
            });
        }
        {
            const int a0 = peak ("ibmno93", { { "PEDAL", 0.0f } }), a1 = peak ("ibmno93", { { "PEDAL", 1.0f } });
            const int b0 = peak ("ibmno57", { { "PEDAL", 0.0f } }), b1 = peak ("ibmno57", { { "PEDAL", 1.0f } });
            // picchi grezzi del banco (measure.log, griglia di 6%): 18.2 dB al tallone, 17.35 in punta ("+17 dB" di tests.md e' arrotondato)
            lab.check ({ a0, a1, b0, b1 }, "No.93 / No.57 (wah a induttore 620 mH): 387 -> 1979 Hz, picco +18.2 / +17.4 dB (+-1)", [&lab, fq, hz, a0, a1, b0, b1] {
                bool ok = true;
                for (int k : { a0, b0 }) ok = ok && fq (lab.v (k), 387) && within (lab.v (k, 1), 18.2, 1.0);
                for (int k : { a1, b1 }) ok = ok && fq (lab.v (k), 1979) && within (lab.v (k, 1), 17.35, 1.0);
                return Verdict { ok, "No.93 " + hz (lab.v (a0)) + " (" + num (lab.v (a0, 1), 1) + " dB) -> " + hz (lab.v (a1)) + " (" + num (lab.v (a1, 1), 1)
                                         + " dB), No.57 " + hz (lab.v (b0)) + " -> " + hz (lab.v (b1)) + " Hz" };
            });
        }

        // 4. EQ (piccolo segnale 0.01 V; guadagni +-1 dB, frequenze +-6%)
        {
            const std::vector<double> fl { 50.0, 100.0, 300.0, 1000.0, 3000.0, 10000.0 };
            const int flat = resp ("ib10pql", {}, fl);
            const int lo = resp ("ib10pql", { { "LOW", 1.0f } }, { 75.0 }), hi = resp ("ib10pql", { { "HIGH", 1.0f } }, { 5600.0 });
            const int mid = resp ("ib10pql", { { "MID", 1.0f } }, { 503.0 });
            const int pm = peak ("ib10pql", { { "MID", 1.0f } }), p0 = peak ("ib10pql", { { "MID", 1.0f }, { "FREQUENCY", 0.0f } });
            const int p1 = peak ("ib10pql", { { "MID", 1.0f }, { "FREQUENCY", 1.0f } });
            lab.check ({ flat, lo, hi, mid, pm, p0, p1 }, "PQL: piatto (+-0.2 dB) a meta', LOW +14.5 dB a 75 Hz, HIGH +15.0 dB a 5.6 kHz, MID +13.7 dB a 503 Hz, "
                                                          "FREQUENCY 0 / 1 = 81 Hz / 5.9 kHz", [&lab, fq, hz, flat, lo, hi, mid, pm, p0, p1] {
                double w = 0;
                for (double g : lab.all (flat)) w = std::max (w, std::abs (g));
                const bool ok = w <= 0.2 && within (lab.v (lo), 14.5, 1.0) && within (lab.v (hi), 15.0, 1.0) && within (lab.v (mid), 13.7, 1.0)
                             && fq (lab.v (pm), 503) && fq (lab.v (p0), 81) && fq (lab.v (p1), 5900);
                return Verdict { ok, "piatto +-" + num (w) + " dB, LOW " + num (lab.v (lo)) + ", HIGH " + num (lab.v (hi)) + ", MID " + num (lab.v (mid)) + " dB a "
                                         + hz (lab.v (pm)) + " Hz, FREQUENCY " + hz (lab.v (p0)) + " / " + hz (lab.v (p1)) + " Hz" };
            });
        }
        {
            const int mx = resp ("ib10ge10", { { "800", 1.0f } }, { 800.0 }), mn = resp ("ib10ge10", { { "800", 0.0f } }, { 800.0 });
            const int q3 = resp ("ib10ge10", { { "800", 0.75f } }, { 800.0 }), lv = resp ("ib10ge10", { { "LEVEL", 1.0f } }, { 1000.0 });
            const int g9x = resp ("ib9ge9", { { "800", 1.0f } }, { 800.0 }), g9n = resp ("ib9ge9", { { "800", 0.0f } }, { 800.0 });
            const int g6x = resp ("ib7ge601", { { "800", 1.0f } }, { 800.0 }), g6n = resp ("ib7ge601", { { "800", 0.0f } }, { 800.0 });
            const int bex = resp ("ib10be10", { { "500", 1.0f } }, { 500.0 }), ben = resp ("ib10be10", { { "500", 0.0f } }, { 500.0 });
            const int g3l = resp ("ib7ge300", { { "LOW", 1.0f } }, { 100.0 }), g3m = resp ("ib7ge300", { { "MID", 1.0f } }, { 600.0 });
            const int g3h = resp ("ib7ge300", { { "HI", 1.0f } }, { 1500.0 });
            lab.check ({ mx, mn, q3, lv, g9x, g9n, g6x, g6n, bex, ben, g3l, g3m, g3h },
                       "EQ grafici: GE10 / GE9 800 Hz +-15.7 dB (3/4 di corsa +5.7, LEVEL +15), GE-601 +-12.3 dB, BE10 500 Hz +-15.8 dB, GE-300 +12.5 / +12.8 / +12.1 dB "
                       "a 100 / 600 / 1500 Hz (+-1)", [&lab, mx, mn, q3, lv, g9x, g9n, g6x, g6n, bex, ben, g3l, g3m, g3h] {
                auto v = [&lab] (int h) { return lab.v (h); };
                const bool ok = within (v (mx), 15.7, 1.0) && within (v (mn), -15.7, 1.0) && within (v (q3), 5.7, 1.0) && within (v (lv), 15.0, 1.0)
                             && within (v (g9x), 15.7, 1.0) && within (v (g9n), -15.7, 1.0) && within (v (g6x), 12.3, 1.0) && within (v (g6n), -12.3, 1.0)
                             && within (v (bex), 15.8, 1.0) && within (v (ben), -15.8, 1.0) && within (v (g3l), 12.5, 1.0) && within (v (g3m), 12.8, 1.0)
                             && within (v (g3h), 12.1, 1.0);
                return Verdict { ok, "GE10 " + num (v (mx), 1) + " / " + num (v (mn), 1) + " / " + num (v (q3), 1) + ", LEVEL " + num (v (lv), 1) + "; GE9 " + num (v (g9x), 1)
                                         + " / " + num (v (g9n), 1) + "; GE-601 " + num (v (g6x), 1) + " / " + num (v (g6n), 1) + "; BE10 " + num (v (bex), 1) + " / "
                                         + num (v (ben), 1) + "; GE-300 " + num (v (g3l), 1) + " / " + num (v (g3m), 1) + " / " + num (v (g3h), 1) + " dB" };
            });
        }

        // 5. compressori: seno 1 kHz, 2.5 s per livello, picco degli ultimi 200 ms; attacco: gradino -40 -> -10 dBFS, costante = 1/e della
        //    sovraelongazione in dB (risoluzione 0.5 ms), rilascio (entro 1 dB) dopo il ritorno a -40 dBFS
        {
            auto level = [&lab, makeS2] (const char* id, Sets sets, double inDb)
            {
                return lab.add (id, [makeS2, sets, inDb] (const ModelDef& d)
                {
                    auto fx = makeS2 (d, sets);
                    Buf x = sineBuf (48000.0, 1000.0, std::pow (10.0, inDb / 20.0), 2.5);
                    processMono (*fx, x, 512);
                    double pk = 0;
                    for (size_t i = x.size() - 9600; i < x.size(); ++i) pk = std::max (pk, (double) std::abs (x[i]));
                    return std::vector<double> { dbOf (pk) };
                });
            };
            auto attack = [&lab, makeS2] (const char* id, Sets sets)
            {
                return lab.add (id, [makeS2, sets] (const ModelDef& d)
                {
                    const double sr = 48000.0;
                    auto fx = makeS2 (d, sets);
                    const int n = (int) (3 * sr);
                    Buf x ((size_t) n);
                    for (int i = 0; i < n; ++i)
                        x[(size_t) i] = (float) (std::pow (10.0, (i >= sr && i < 2 * sr ? -10.0 : -40.0) / 20) * std::sin (2 * kPi * 1000 * i / sr));
                    processMono (*fx, x, 512);
                    auto env = [&x] (int i) { double m = 0; for (int k = 0; k < 48; ++k) m = std::max (m, (double) std::abs (x[(size_t) (i + k)])); return dbOf (m); };
                    const double fin = env ((int) (2 * sr) - 960), finR = env (n - 960), ov = env ((int) sr) - fin;
                    int t63 = (int) sr;
                    while (t63 < 2 * sr - 48 && env (t63) - fin > ov / 2.718) t63 += 24;
                    int tr = (int) (2 * sr);
                    while (tr < n - 48 && std::abs (env (tr) - finR) > 1.0) tr += 48;
                    return std::vector<double> { (t63 - sr) / sr * 1000.0, (tr - 2 * sr) / sr * 1000.0 };
                });
            };
            const int c40 = level ("ib7cp835", {}, -40), c20 = level ("ib7cp835", {}, -20), c0 = level ("ib7cp835", {}, 0), c30 = level ("ib7cp835", {}, -30);
            const int cat = attack ("ib7cp835", {});
            lab.check ({ c40, c20, c0, c30, cat }, "CP-835 (Ross): -40 / -20 / 0 dBFS -> -24.6 / -15.8 / -15.6 dBFS (+-1.5), uscita entro 1.5 dB da -30 a 0 dBFS, "
                                                   "attacco 5 ms, rilascio 0.8 s (+-20%)", [&lab, c40, c20, c0, c30, cat] {
                const double span = std::abs (lab.v (c0) - lab.v (c30));
                const bool ok = within (lab.v (c40), -24.6, 1.5) && within (lab.v (c20), -15.8, 1.5) && within (lab.v (c0), -15.6, 1.5) && span <= 1.5
                             && within (lab.v (cat, 0), 5.0, 1.0) && within (lab.v (cat, 1), 800.0, 160.0);
                return Verdict { ok, num (lab.v (c40), 1) + " / " + num (lab.v (c20), 1) + " / " + num (lab.v (c0), 1) + " dBFS, -30..0 " + num (span) + " dB, attacco "
                                         + num (lab.v (cat, 0), 1) + " ms, rilascio " + num (lab.v (cat, 1), 0) + " ms" };
            });
            struct Cv { const char* id; double in, out; };
            static const Cv cvs[] = { { "ib9cp9", -30, -21.8 }, { "ib9cp9", 0, -18.1 }, { "ib10cp10", -30, -25.3 }, { "ib10cp10", 0, -18.2 },
                                      { "ibstcp5", -30, -25.3 }, { "ibstcp5", 0, -18.2 }, { "ib10bp10", -30, -25.3 }, { "ib10bp10", 0, -18.2 },
                                      { "ibstbp5", -30, -25.3 }, { "ibstbp5", 0, -18.2 }, { "ibxec30", 0, -17.8 } };
            std::vector<int> hc;
            for (const auto& c : cvs) hc.push_back (level (c.id, {}, c.in));
            lab.check (hc, "CP9 / CP10 / CP5 / BP10 / BP5 / EC30: curva statica a -30 e 0 dBFS (CP9 -21.8 / -18.1, CP10-BP5 -25.3 / -18.2, EC30 0 -> -17.8 dBFS; +-1.5)",
                       [&lab, hc] {
                bool ok = true;
                juce::String detail;
                for (size_t k = 0; k < hc.size(); ++k)
                {
                    const double v = lab.v (hc[k]);
                    ok = ok && within (v, cvs[k].out, 1.5);
                    if (k % 2 == 0) detail << cvs[k].id << " " << num (v, 1); else detail << " / " << num (v, 1) << "; ";
                }
                return Verdict { ok, detail.trimCharactersAtEnd ("; ") };
            });
            struct At { const char* id; double a0, a1; };
            static const At ats[] = { { "ib9cp9", 7.0, 20.0 }, { "ib10cp10", 8.0, 21.5 }, { "ibstcp5", 8.0, 21.5 }, { "ib10bp10", 4.0, 10.0 }, { "ibstbp5", 4.0, 10.0 } };
            std::vector<int> ha;
            for (const auto& a : ats) { ha.push_back (attack (a.id, { { "ATTACK", 0.0f } })); ha.push_back (attack (a.id, { { "ATTACK", 1.0f } })); }
            lab.check (ha, "Compressori: costante d'attacco con ATTACK 0 / 1 (gradino -40 -> -10 dBFS): CP9 7 / 20 ms, CP10 e CP5 8 / 21.5 ms, BP10 e BP5 4 / 10 ms "
                           "(+-15% + 0.5 ms)", [&lab, ha] {
                bool ok = true;
                juce::String detail;
                for (size_t k = 0; k < std::size (ats); ++k)
                {
                    const double a = lab.v (ha[2 * k]), b = lab.v (ha[2 * k + 1]);
                    ok = ok && std::abs (a - ats[k].a0) <= 0.15 * ats[k].a0 + 0.5 && std::abs (b - ats[k].a1) <= 0.15 * ats[k].a1 + 0.5;
                    detail << ats[k].id << " " << num (a, 1) << " / " << num (b, 1) << "; ";
                }
                return Verdict { ok, detail.trimCharactersAtEnd ("; ") + " ms" };
            });
            const int t40 = level ("ibtstc999", {}, -40), t20 = level ("ibtstc999", {}, -20), t0 = level ("ibtstc999", {}, 0);
            lab.check ({ t40, t20, t0 }, "TC999: -40 / -20 / 0 dBFS -> -35.1 / -22.9 / -13.7 dBFS (compressione dolce ~2:1, +-2)", [&lab, t40, t20, t0] {
                const bool ok = within (lab.v (t40), -35.1, 2.0) && within (lab.v (t20), -22.9, 2.0) && within (lab.v (t0), -13.7, 2.0);
                return Verdict { ok, num (lab.v (t40), 1) + " / " + num (lab.v (t20), 1) + " / " + num (lab.v (t0), 1) + " dBFS" };
            });
            const int o40 = level ("ib7cp830", { { "SUSTAIN", 1.0f } }, -40), o20 = level ("ib7cp830", { { "SUSTAIN", 1.0f } }, -20);
            lab.check ({ o40, o20 }, "CP-830 (fotocellula): con SUSTAIN 1 l'uscita da -40 a -20 dBFS varia meno di 6 dB (regime dopo 2 s)", [&lab, o40, o20] {
                const double d = lab.v (o20) - lab.v (o40);
                return Verdict { std::abs (d) < 6.0, num (lab.v (o40), 1) + " -> " + num (lab.v (o20), 1) + " dBFS (" + num (d, 1) + " dB)" };
            });
        }

        // click: fino alla 1.2.6 PPE1 HIGH (18.5x: biquad ricalcolati ogni 8 campioni, una mensola di +-15 dB a 5.6 kHz lasciava
        // un gradino a ogni ricalcolo) e TC999 SUSTAIN (11.8x: guadagno del ramo amplificato a gradini ogni 16 campioni) avevano
        // una soglia propria; dalla 1.2.7 coefficienti e guadagno scorrono campione per campione fra un ricalcolo e l'altro
        // (1.1x / 1.0x), quindi nessuna eccezione. Il reset() di AF-201, AF9, AFL, AW7 e AW5 non riporta l'inviluppo allo stato
        // iniziale (innocuo: reset() si usa riaccendendo il pedale)
        addClickAndDeterminism (lab, kStage4Dyn, numDyn, "Tappa 4 dinamica/EQ/filtri", 10.0, false);
        lab.run (report);
    }

    //------------------------------------------------------------------------------
    // Misure del banco s3 (stereo, ingresso uguale sui due canali, blocchi da 256, comandi per indice dopo prepare)
    //------------------------------------------------------------------------------
    /** 'ir': assestamento 1.5 s in silenzio, impulso di 0.05 V; { ritardo del picco dopo 20 campioni (ms), -3 dB del ramo
        ritardato sopra il suo massimo (Hz) }. */
    std::vector<double> bbdImpulse (const pt::engine::ModelDef& d, const std::map<int, float>& p)
    {
        const double SR = 48000.0;
        auto fx = makeAfter (d, p);
        Buf l ((size_t) (SR * 1.5), 0.0f), r = l;
        runStereo256 (*fx, l, r);
        const int n = (int) (SR * 1.2);
        l.assign ((size_t) n, 0.0f); r = l;
        l[0] = r[0] = 0.05f;
        runStereo256 (*fx, l, r);
        int best = 20;
        float bv = 0;
        for (int i = 20; i < n; ++i) if (std::abs (l[(size_t) i]) > bv) { bv = std::abs (l[(size_t) i]); best = i; }
        std::vector<float> w (16384, 0.0f);
        const int a = std::max (20, best - 24);
        for (int i = 0; i < 2048 && a + i < n; ++i) w[(size_t) i] = l[(size_t) (a + i)];
        juce::dsp::FFT fft (13);
        fft.performFrequencyOnlyForwardTransform (w.data());
        float pk = 0;
        int kpk = 0;
        for (int k = 3; k < 4096; ++k) if (w[(size_t) k] > pk) { pk = w[(size_t) k]; kpk = k; }
        int k3 = kpk;
        while (k3 < 4095 && w[(size_t) k3] > pk * 0.7071f) ++k3;
        return { best * 1000.0 / SR, k3 * SR / 8192.0 };
    }

    /** 'track': treno d'impulsi da 0.5 V ogni 30 ms per 10 s dopo 1 s di silenzio; { ritardo min, max (ms), LFO (Hz) }. */
    std::vector<double> bbdTrack (const pt::engine::ModelDef& d, const std::map<int, float>& p)
    {
        const double SR = 48000.0, seconds = 10.0, stepMs = 30.0;
        auto fx = makeAfter (d, p);
        Buf l ((size_t) SR, 0.0f), r = l;
        runStereo256 (*fx, l, r);
        const int n = (int) (seconds * SR), step = (int) (stepMs * 0.001 * SR);
        l.assign ((size_t) n, 0.0f); r = l;
        for (int i = 0; i < n; i += step) l[(size_t) i] = r[(size_t) i] = 0.5f;
        runStereo256 (*fx, l, r);
        std::vector<double> dl;
        for (int i = 0; i + step <= n; i += step)
        {
            int best = -1;
            float bv = 0;
            for (int j = 12; j < step - 2; ++j) if (std::abs (l[(size_t) (i + j)]) > bv) { bv = std::abs (l[(size_t) (i + j)]); best = j; }
            dl.push_back (best * 1000.0 / SR);
        }
        double lo = 1e9, hi = 0, mean = 0;
        for (double v : dl) { lo = std::min (lo, v); hi = std::max (hi, v); mean += v; }
        mean /= (double) dl.size();
        const int cross = hystCross (dl, mean, 0.3 * (hi - lo));
        return { lo, hi, cross / 2.0 / ((double) dl.size() * stepMs * 0.001) };
    }

    /** 'notch': impulso ogni 60 ms (dopo 1 s), notch = minimo locale 12 dB sotto il massimo in [f/2, 2f] (FFT 4096);
        { primo notch min, max (Hz), numero massimo di notch, LFO (Hz), terzo notch dell'ultima finestra (Hz, 0 se assente) }. */
    std::vector<double> phaserNotches (const pt::engine::ModelDef& d, const std::map<int, float>& p, double seconds)
    {
        const double SR = 48000.0;
        auto fx = makeAfter (d, p);
        Buf l ((size_t) SR, 0.0f), r = l;
        runStereo256 (*fx, l, r);
        const int n = (int) (seconds * SR), step = (int) (0.06 * SR);
        l.assign ((size_t) n, 0.0f); r = l;
        for (int i = 0; i < n; i += step) l[(size_t) i] = r[(size_t) i] = 0.5f;
        runStereo256 (*fx, l, r);
        double nlo = 1e9, nhi = 0, third = 0;
        std::vector<double> first;
        int maxNotches = 0;
        juce::dsp::FFT fft (12);
        for (int i = 0; i + step <= n; i += step)
        {
            std::vector<float> w (8192, 0.0f);
            for (int j = 0; j < step && j < 4096; ++j) w[(size_t) j] = l[(size_t) (i + j)];
            fft.performFrequencyOnlyForwardTransform (w.data());
            std::vector<double> nf;
            for (int k = 2; k < 2000; ++k)
                if (w[(size_t) k] < w[(size_t) k - 1] && w[(size_t) k] <= w[(size_t) k + 1])
                {
                    float loc = 0;
                    for (int j = std::max (1, k / 2); j < std::min (2047, k * 2 + 2); ++j) loc = std::max (loc, w[(size_t) j]);
                    if (dbOf (w[(size_t) k] / std::max (loc, 1.0e-30f)) < -12) nf.push_back (k * SR / 4096);
                }
            maxNotches = std::max (maxNotches, (int) nf.size());
            third = nf.size() >= 3 ? nf[2] : 0.0;
            if (! nf.empty()) { nlo = std::min (nlo, nf.front()); nhi = std::max (nhi, nf.front()); first.push_back (nf.front()); }
            else first.push_back (0);
        }
        double mean = 0;
        int cnt = 0;
        for (double v : first) if (v > 0) { mean += std::log (v); ++cnt; }
        mean = cnt ? std::exp (mean / cnt) : 0;
        std::vector<double> lg;
        for (double v : first) lg.push_back (v > 0 ? std::log (v) : std::log (mean));
        const int cross = cnt ? hystCross (lg, std::log (mean), 0.3 * std::log (nhi / nlo)) : 0;
        return { nlo, nhi, (double) maxNotches, cross / 2.0 / ((double) first.size() * 0.06), third };
    }

    /** 'fb': frase del banco s3 (6 s) + silenzio fino a 14 s; { picco (V), coda 12-14 s (dBV), finito }. */
    std::vector<double> bbdFeedback (const pt::engine::ModelDef& d, const std::map<int, float>& p)
    {
        static const Buf g = guitarS3 (6.0, 5);
        auto fx = makeAfter (d, p);
        Buf l = g;
        l.resize ((size_t) (48000.0 * 14), 0.0f);
        Buf r = l;
        runStereo256 (*fx, l, r);
        double pk = 0;
        bool fin = true;
        for (float v : l) { fin = fin && std::isfinite (v); pk = std::max (pk, (double) std::abs (v)); }
        return { pk, dbOf (rmsOf (l, (size_t) (48000.0 * 12), l.size())), fin ? 1.0 : 0.0, dbOf (rmsOf (l, (size_t) (48000.0 * 6), (size_t) (48000.0 * 8))) };
    }

    /** 'am': seno a 440 Hz di 0.3 V per 10 s, inviluppo RMS su 5 ms (da 1 s); { freq L, profondita' L (dB), escursione di L^2 + R^2 (dB) }. */
    std::vector<double> tremoloAm (const pt::engine::ModelDef& d, const std::map<int, float>& p)
    {
        const double SR = 48000.0;
        auto fx = makeAfter (d, p);
        const int n = (int) (SR * 10);
        Buf l = sineBuf (SR, 440.0, 0.3, 10.0), r = l;
        runStereo256 (*fx, l, r);
        const int win = (int) (SR * 0.005);
        std::vector<double> e, pw;
        for (int i = (int) SR; i + win <= n; i += win)
        {
            const double a = rmsOf (l, (size_t) i, (size_t) (i + win)), b = rmsOf (r, (size_t) i, (size_t) (i + win));
            e.push_back (a);
            pw.push_back (a * a + b * b);
        }
        double lo = 1e9, hi = 0, mean = 0, plo = 1e9, phi = 0;
        for (double v : e) { lo = std::min (lo, v); hi = std::max (hi, v); mean += v; }
        for (double v : pw) { plo = std::min (plo, v); phi = std::max (phi, v); }
        mean /= (double) e.size();
        const int cross = hystCross (e, mean, 0.3 * (hi - lo));
        return { cross / 2.0 / ((double) e.size() * 0.005), dbOf (hi / std::max (lo, 1e-9)), 10.0 * std::log10 (phi / std::max (plo, 1e-30)) };
    }

    /** Tappa 4, lavoro s3: phaser, chorus, flanger, tremolo e delay BBD (FxStage4Mod.cpp). Metodi del banco s3. */
    void runStage4ModTests (const std::function<void (const juce::String&, bool, const juce::String&)>& report)
    {
        using namespace pt::engine;
        constexpr int numMod = countOf (kStage4Mod);
        Lab lab;
        /** Comandi per etichetta o ruolo (setsS2) di un modello del catalogo, eventualmente con la config modificata. */
        auto at = [&lab] (const char* id, const char* extra, Sets sets, std::function<std::vector<double> (const ModelDef&, const std::map<int, float>&)> m)
        {
            auto f = [sets, m] (const ModelDef& d) { return m (d, setsS2 (d, sets)); };
            return extra != nullptr ? addPatched (lab, id, extra, f) : lab.add (id, f);
        };
        auto tol = [] (double x, double ref, double rel, double abs) { return std::abs (x - ref) <= rel * std::abs (ref) + abs; };

        // 0. pedali esistenti invariati bit per bit (banco s3 'hash': frase di 6 s, fabbrica / tutti 0.8 / tutti 0.2, R = 0.7 L)
        {
            struct Ref { const char* id; uint64_t hash; };
            static const Ref refs[] = { { "ce2", 0x1c265696304c2dfcull }, { "ce1", 0xba7cb4ec4a4198f0ull }, { "bf2", 0xc0f5da7731dbaa2full },
                                        { "ph1", 0x57bfd71565f1db5bull }, { "ph3", 0x50051f3685d3e211ull }, { "dm2", 0xbc24ad3e410c67b0ull },
                                        { "tr2", 0x77647c9d8ed04ee8ull }, { "evh90", 0xc38117b07b98878full }, { "ehxss75", 0x2437960de13072a7ull },
                                        { "ehxem76", 0x1649948fd59db79bull }, { "ehmm76", 0x8519405ebac1118bull }, { "ehdmm", 0xbfb931a230a17f83ull },
                                        { "evh117", 0x64226c87e45b4bffull }, { "zw38", 0x4d812a3368684e9eull }, { "ehxsclone", 0xaf8edbd4a6c5c7a3ull },
                                        { "evh30", 0xea8fd4e1fca4787full }, { "ehxbassclo", 0xa0005ebf0712db27ull }, { "ehxstpolyc", 0x2fd6ebdf5571faffull },
                                        { "ehxbadst1", 0x011bc05506118b9full }, { "ehxpoly79", 0x1031e13bfa7ca007ull } };
            static const Buf g = guitarS3 (6.0);
            std::vector<int> h;
            for (const auto& r : refs)
            {
                h.push_back (lab.add (r.id, [] (const ModelDef& d)
                {
                    uint64_t hv = 1469598103934665603ull;
                    for (int set = 0; set < 3; ++set)
                    {
                        std::map<int, float> p;
                        if (set > 0) for (int k = 0; k < d.numControls; ++k) p[k] = set == 1 ? 0.8f : 0.2f;
                        auto fx = makeAfter (d, p);
                        Buf bl = g, br = g;
                        for (auto& x : br) x *= 0.7f;
                        runStereo256 (*fx, bl, br);
                        hv = fnvBytes (br, fnvBytes (bl, hv));
                    }
                    return std::vector<double> { (double) (uint32_t) (hv >> 32), (double) (uint32_t) hv };
                }));
                lab.first (h.back());
            }
            lab.check (h, "Tappa 4: 20 modulazioni e delay esistenti (CE-2, CE-1, BF-2, PH-1/3, DM-2, TR-2, EVH 90/117/30, EHX, MXR) invariati bit per bit",
                       [&lab, h] {
                if (! kBitExactRefs) return Verdict { true, "impronte di riferimento misurate con GCC su Linux: confronto non applicabile a questo compilatore" };
                juce::String bad;
                for (size_t k = 0; k < h.size(); ++k)
                {
                    const uint64_t v = ((uint64_t) lab.v (h[k], 0) << 32) | (uint64_t) lab.v (h[k], 1);
                    if (v != refs[k].hash) bad << refs[k].id << " DIVERSO (" << juce::String::toHexString ((juce::int64) v) << ") ";
                }
                return Verdict { bad.isEmpty(), bad.isEmpty() ? juce::String ((int) h.size()) + "/" + juce::String ((int) h.size()) + " hash identici" : bad.trim() };
            });
        }

        // 1. robustezza dei 46 modelli (banco s3 'check', frase di 3 s invece di 8): comandi tutti a 1, tutti a 0, casuali ogni 40 blocchi,
        //    casuali con l'ingresso x6 (~2 V di picco), stessa istanza; livelli col riff a -12 dBFS e i comandi di default
        {
            static const Buf g = guitarS3 (3.0, 3);
            static const Buf riff = guitarS1 (true, 48000.0, 3.0, -12.0);
            std::vector<int> h, hl;
            for (int m = 0; m < numMod; ++m)
            {
                h.push_back (lab.add (kStage4Mod[m], [] (const ModelDef& d)
                {
                    auto fx = makeAfter (d, {});
                    double peak = 0;
                    bool finite = true;
                    uint32_t s = 77;
                    auto rnd = [&s] { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) (s & 0xffff) / 65535.0f; };
                    for (int pass = 0; pass < 4; ++pass)
                    {
                        Buf l = g, r = g;
                        const float gain = pass == 3 ? 6.0f : 1.0f;
                        for (auto& x : l) x *= gain;
                        for (auto& x : r) x *= gain;
                        runStereo256 (*fx, l, r, [&] (size_t i)
                        {
                            if (i % (256 * 40) == 0)
                                for (int k = 0; k < d.numControls; ++k) fx->params[k].store (pass == 0 ? 1.0f : pass == 1 ? 0.0f : rnd());
                        });
                        for (size_t i = 0; i < l.size(); ++i)
                        {
                            finite = finite && std::isfinite (l[i]) && std::isfinite (r[i]);
                            peak = std::max (peak, (double) std::max (std::abs (l[i]), std::abs (r[i])));
                        }
                    }
                    return std::vector<double> { finite ? 1.0 : 0.0, peak };
                }));
                hl.push_back (lab.add (kStage4Mod[m], [] (const ModelDef& d)
                {
                    auto fx = makeS1 (d, {});
                    Buf y = riff;
                    processMono (*fx, y, 512);
                    double pk = 0;
                    bool fin = true;
                    for (float v : y) { fin = fin && std::isfinite (v); pk = std::max (pk, (double) std::abs (v)); }
                    return std::vector<double> { fin ? 1.0 : 0.0, dbOf (pk), dbOf (rmsOf (y, (size_t) (0.05 * 48000.0))) };
                }));
            }
            lab.check (h, "Tappa 4 modulazioni: robustezza (46 modelli, comandi a 1 / 0 / casuali, ingresso fino a ~2 V): finiti, picco < 8 V", [&lab, h] {
                double worst = 0;
                juce::String bad, worstId;
                for (int m = 0; m < numMod; ++m)
                {
                    const bool fin = lab.v (h[(size_t) m], 0) > 0.5;
                    const double pk = fin ? lab.v (h[(size_t) m], 1) : 999.0;
                    if (pk > worst) { worst = pk; worstId = kStage4Mod[m]; }
                    if (! fin || pk >= 8.0) bad << kStage4Mod[m] << (fin ? "" : " NaN") << " ";
                }
                return Verdict { bad.isEmpty(), "picco max " + num (worst) + " V (" + worstId + ") " + bad.trim() };
            });
            lab.check (hl, "Tappa 4 modulazioni: livelli con riff a -12 dBFS e comandi di default (46 modelli: finiti, picco < 0 dBFS, RMS -40..-20 dBFS)", [&lab, hl] {
                int good = 0;
                double lo = 0, hi = -200, pk = -200;
                juce::String bad;
                for (int m = 0; m < numMod; ++m)
                {
                    const double p = lab.v (hl[(size_t) m], 1), rms = lab.v (hl[(size_t) m], 2);
                    const bool fine = lab.v (hl[(size_t) m], 0) > 0.5 && p < 0.0 && rms > -40.0 && rms < -20.0;
                    if (fine) ++good; else bad << kStage4Mod[m] << " (RMS " << num (rms, 1) << ", picco " << num (p, 1) << ") ";
                    lo = std::min (lo, rms); hi = std::max (hi, rms); pk = std::max (pk, p);
                }
                return Verdict { good == numMod, juce::String (good) + "/46, RMS " + num (lo, 1) + ".." + num (hi, 1) + " dBFS, picco max " + num (pk, 1) + " dBFS " + bad.trim() };
            });
        }

        // 2. ritardo dei BBD (impulso, comandi assestati 1.5 s, retroazione e profondita' 0; +-4% + 0.25 ms) e banda del ramo ritardato
        //    (senza rumore e compander, come il banco)
        {
            struct D { const char* id; const char* extra; Sets sets; double ms; };
            const D ds[] = {
                { "ib9fl9", "noise=0 gate=0", { { "REGEN", 0.0f }, { "WIDTH", 0.0f }, { "DELAY TIME", 0.0f } }, 1.0 },
                { "ib9fl9", "noise=0 gate=0", { { "REGEN", 0.0f }, { "WIDTH", 0.0f }, { "DELAY TIME", 1.0f } }, 12.8 },
                { "ib9ad9", "noise=0 comp=0", { { "DELAY TIME", 0.0f }, { "REPEAT", 0.0f } }, 10.0 },
                { "ib9ad9", "noise=0 comp=0", { { "DELAY TIME", 1.0f }, { "REPEAT", 0.0f } }, 300.0 },
                { "ib10cd10", "noise=0 comp=0", { { "D-TIME", 0.0f }, { "REPEAT", 0.0f } }, 20.0 },
                { "ib10cd10", "noise=0 comp=0", { { "D-TIME", 1.0f }, { "REPEAT", 0.0f } }, 300.0 },
                { "ibmadmini", "noise=0 comp=0", { { "DELAY TIME", 0.0f }, { "REPEAT", 0.0f } }, 20.0 },
                { "ibmadmini", "noise=0 comp=0", { { "DELAY TIME", 1.0f }, { "REPEAT", 0.0f } }, 600.0 },
                { "ibmes2", "noise=0 comp=0", { { "DELAY TIME", 0.0f }, { "FEEDBACK", 0.0f } }, 30.0 },
                { "ibmes2", "noise=0 comp=0", { { "DELAY TIME", 1.0f }, { "FEEDBACK", 0.0f } }, 1000.0 },
                { "ibmes3", "noise=0 comp=0", { { "DELAY TIME", 0.3f }, { "FEEDBACK", 0.0f }, { "ANA/DIG", 0.0f } }, 90.4 },
                { "ibmes3", "noise=0 comp=0", { { "DELAY TIME", 0.3f }, { "FEEDBACK", 0.0f }, { "ANA/DIG", 1.0f } }, 118.7 },
                { "ib10sc10", "noise=0 gate=0", { { "WIDTH", 0.0f }, { "D-TIME", 0.5f } }, 2.68 },
                { "ib9cs9", "noise=0 gate=0", { { "WIDTH", 0.0f } }, 5.22 } };
            std::vector<int> h;
            for (const auto& x : ds) h.push_back (at (x.id, x.extra, x.sets, bbdImpulse));
            std::vector<D> copy (std::begin (ds), std::end (ds));
            lab.check (h, "BBD: ritardo del ramo ritardato (FL9 1 / 12.8, AD9 10 / 300, CD10 20 / 300, AD MINI 20 / 600, ES2 30 / 1000, ES3 0.3 ANALOG / DIGITAL "
                          "90.4 / 118.7, SC10 2.68, CS9 5.22 ms; +-4% + 0.25 ms)", [&lab, h, copy, tol] {
                bool ok = true;
                juce::String detail;
                for (size_t k = 0; k < h.size(); ++k)
                {
                    const double v = lab.v (h[k]);
                    const bool g = tol (v, copy[k].ms, 0.04, 0.25);
                    ok = ok && g;
                    detail << copy[k].id << " " << num (v) << (g ? "" : " (FUORI)") << "; ";
                }
                return Verdict { ok, detail.trimCharactersAtEnd ("; ") + " ms" };
            });
            struct B { int idx; double hz, rel; };
            const B bs[] = { { 0, 6200.0, 0.10 }, { 13, 5700.0, 0.10 }, { 2, 2300.0, 0.12 }, { 3, 2150.0, 0.12 }, { 7, 1000.0, 0.15 } };
            std::vector<int> hb;
            for (const auto& b : bs) hb.push_back (h[(size_t) b.idx]);
            std::vector<B> bcopy (std::begin (bs), std::end (bs));
            lab.check (hb, "BBD: banda -3 dB del ramo ritardato (FL9 6.2 kHz +-10%, CS9 5.7 kHz +-10%, AD9 10 / 300 ms 2.3 / 2.15 kHz +-12%, AD MINI 600 ms 1.0 kHz +-15%)",
                       [&lab, h, bcopy, copy] {
                bool ok = true;
                juce::String detail;
                for (const auto& b : bcopy)
                {
                    const double v = lab.v (h[(size_t) b.idx], 1);
                    ok = ok && std::abs (v / b.hz - 1.0) <= b.rel;
                    detail << copy[(size_t) b.idx].id << " " << juce::String (juce::roundToInt (v)) << " Hz; ";
                }
                return Verdict { ok, detail.trimCharactersAtEnd ("; ") };
            });
        }

        // 3. escursione e frequenza dell'LFO (treno d'impulsi ogni 30 ms; estremi +-6% + 0.25 ms, LFO +-8%)
        {
            struct T { const char* id; Sets sets; double lo, hi, hz; };
            const T ts[] = { { "ib9cs9", { { "SPEED", 0.5f }, { "WIDTH", 1.0f } }, 3.3, 8.5, 0.95 },
                             { "ib10sc10", { { "SPEED", 0.5f }, { "WIDTH", 1.0f } }, 1.6, 4.9, 1.90 },
                             { "ib9bc9", { { "SPEED A", 0.5f }, { "WIDTH A", 1.0f }, { "WIDTH B", 0.0f } }, 4.7, 7.8, 2.53 },
                             { "ib7fl301", { { "SPEED", 0.5f }, { "WIDTH", 1.0f }, { "REGEN", 0.0f } }, 2.6, 11.5, 0.74 },
                             { "ibmaf2", { { "SPEED", 0.5f }, { "RANGE", 1.0f }, { "ENHANCE", 0.0f } }, 0.6, 14.8, 0.63 } };
            std::vector<int> h;
            for (const auto& t : ts) h.push_back (at (t.id, "gate=0", t.sets, bbdTrack));
            std::vector<T> copy (std::begin (ts), std::end (ts));
            lab.check (h, "BBD: escursione del ritardo e LFO (CS9 3.3-8.5 ms 0.95 Hz, SC10 1.6-4.9 ms 1.90 Hz, BC9 4.7-7.8 ms 2.53 Hz, FL301 2.6-11.5 ms 0.74 Hz, "
                          "AF2 0.6-14.8 ms 0.63 Hz)", [&lab, h, copy, tol] {
                bool ok = true;
                juce::String detail;
                for (size_t k = 0; k < h.size(); ++k)
                {
                    const double lo = lab.v (h[k], 0), hi = lab.v (h[k], 1), f = lab.v (h[k], 2);
                    const bool g = tol (lo, copy[k].lo, 0.06, 0.25) && tol (hi, copy[k].hi, 0.06, 0.25) && std::abs (f / copy[k].hz - 1.0) <= 0.08;
                    ok = ok && g;
                    detail << copy[k].id << " " << num (lo) << "-" << num (hi) << " ms " << num (f, 3) << " Hz" << (g ? "" : " (FUORI)") << "; ";
                }
                return Verdict { ok, detail.trimCharactersAtEnd ("; ") };
            });
        }

        // 4. phaser: traiettoria del primo notch, numero di notch, LFO (impulso ogni 60 ms; estremi +-12%, notch esatti, LFO +-8%)
        {
            struct P { const char* id; Sets sets; double seconds, lo, hi; int notches; double hz; };
            const P ps[] = {
                { "ib7pt999", { { "SPEED", 0.0f } }, 22, 94, 1031, 2, 0 },
                { "ib7pt999", { { "SPEED", 0.5f } }, 10, 0, 0, 2, 0.837 },
                { "ib7pt707", { { "SPEED", 0.0f }, { "FEEDBACK", 0.0f } }, 12, 70, 1734, 2, 0 },
                { "ib7pt707", { { "SPEED", 0.5f }, { "FEEDBACK", 0.0f } }, 10, 0, 0, 2, 1.673 },
                { "ib7jl70", { { "SPEED", 0.0f }, { "DEPTH", 0.0f } }, 12, 35, 457, 4, 0 },
                { "ibtlph7", { { "SPEED", 0.5f }, { "DEPTH", 1.0f }, { "FEEDBACK", 0.0f }, { "MODE", 0.0f } }, 10, 35, 281, 2, 0.424 },
                { "ibtlph7", { { "SPEED", 0.0f }, { "DEPTH", 1.0f }, { "FEEDBACK", 0.0f }, { "MODE", 1.0f } }, 60, 23, 188, 3, 0 },
                { "ib10ph10", { { "SPEED", 0.0f }, { "WIDTH", 1.0f }, { "FEEDBACK", 0.0f }, { "MODE", 0.0f } }, 55, 0, 0, 3, 0 },
                { "ib10ph10", { { "SPEED", 0.0f }, { "WIDTH", 1.0f }, { "FEEDBACK", 0.0f }, { "MODE", 1.0f } }, 55, 0, 0, 5, 0 },
                { "ib10ph10", { { "SPEED", 0.5f }, { "WIDTH", 1.0f }, { "FEEDBACK", 0.0f } }, 10, 0, 0, 3, 0.632 },
                { "ibtlpm7", { { "SPEED", 0.0f }, { "DEPTH", 1.0f } }, 2.2 / 0.15, 47, 3902, 1, 0 },        // FEEDBACK bipolare (fbbip): al centro
                { "ibtlpm7", { { "SPEED", 0.5f }, { "DEPTH", 1.0f } }, 10, 0, 0, 1, 0.875 } };
            std::vector<int> h;
            for (const auto& p : ps)
            {
                const double s = p.seconds;
                h.push_back (at (p.id, nullptr, p.sets, [s] (const ModelDef& d, const std::map<int, float>& m) { return phaserNotches (d, m, s); }));
                if (s > 20) lab.first (h.back());
            }
            const int cal = at ("ibtlph7", nullptr, { { "DEPTH", 0.0f }, { "MODE", 1.0f } }, [] (const ModelDef& d, const std::map<int, float>& m) { return phaserNotches (d, m, 3.0); });
            std::vector<P> copy (std::begin (ps), std::end (ps));
            auto hAll = h;
            hAll.push_back (cal);
            lab.check (hAll, "Phaser: primo notch (PT-999 94-1031, PT-707 70-1734, Jetlyzer 35-457, PH7 35-281 / 23-188 Hz), numero di notch (2 / 2 / 4 / 2-3, PH10 3 / 5, "
                             "PM7 1), LFO a meta' corsa (+-8%); PH7 a riposo: terzo notch a 1.4 kHz (+-10%)", [&lab, h, copy, cal] {
                bool ok = true;
                juce::String detail;
                for (size_t k = 0; k < h.size(); ++k)
                {
                    const auto& p = copy[k];
                    const double lo = lab.v (h[k], 0), hi = lab.v (h[k], 1), nn = lab.v (h[k], 2), f = lab.v (h[k], 3);
                    bool g = (int) nn == p.notches;
                    if (p.lo > 0) g = g && std::abs (lo / p.lo - 1.0) <= 0.12 && std::abs (hi / p.hi - 1.0) <= 0.12;
                    if (p.hz > 0) g = g && std::abs (f / p.hz - 1.0) <= 0.08;
                    ok = ok && g;
                    detail << p.id << " ";
                    if (p.lo > 0) detail << juce::roundToInt (lo) << "-" << juce::roundToInt (hi) << " Hz ";
                    detail << (int) nn << "n";
                    if (p.hz > 0) detail << " " << num (f, 3) << " Hz";
                    detail << (g ? "" : " (FUORI)") << "; ";
                }
                const double third = lab.v (cal, 4);
                ok = ok && std::abs (third / 1400.0 - 1.0) <= 0.10;
                return Verdict { ok, detail + "PH7 terzo notch " + juce::String (juce::roundToInt (third)) + " Hz" };
            });
        }

        // 5. retroazione dei delay al massimo (frase 6 s + silenzio fino a 14 s): finiti, picco < 1 V, coda < -90 dBV fino a DELAY TIME 0.3,
        //    < -55 dBV per tutti; ES2 in autooscillazione limitata (coda -35..-15 dBV, picco < 1.5 V); flanger e chorus al massimo coda < -70 dBV
        {
            struct F { const char* id; Sets base; const char* time; };
            const F fs[] = { { "ib9ad9", { { "REPEAT", 1.0f } }, "DELAY TIME" }, { "ib7ad80", { { "REPEAT", 1.0f } }, "DELAY TIME" },
                             { "ib10cd10", { { "REPEAT", 1.0f } }, "D-TIME" }, { "ibmad99", { { "FEEDBACK", 1.0f } }, "DELAY TIME" },
                             { "ibmadmini", { { "REPEAT", 1.0f } }, "DELAY TIME" }, { "ibmes2", { { "FEEDBACK", 1.0f } }, "DELAY TIME" },
                             { "ibmes3", { { "FEEDBACK", 1.0f } }, "DELAY TIME" } };
            const float times[] = { 0.0f, 0.15f, 0.3f, 0.5f, 0.7f, 1.0f };
            std::vector<int> h;
            std::vector<juce::String> what;
            for (const auto& f : fs)
                for (float t : times)
                {
                    Sets s = f.base;
                    s.push_back ({ f.time, t });
                    h.push_back (at (f.id, nullptr, s, bbdFeedback));
                    what.push_back (juce::String (f.id) + "@" + num (t));
                }
            // ES3 DIGITAL: il ritardo digitale non ha le perdite della linea BBD, l'anello resta a fbmax = 0.96 per ripetizione (a 118 ms
            // dopo 6 s di silenzio -18 dB soltanto): niente soglie di coda, la coda deve solo calare (12-14 s sotto 6-8 s)
            std::vector<int> hdig;
            for (float t : times) hdig.push_back (at ("ibmes3", nullptr, { { "FEEDBACK", 1.0f }, { "ANA/DIG", 1.0f }, { "DELAY TIME", t } }, bbdFeedback));
            lab.check (hdig, "ES3 DIGITAL con FEEDBACK al massimo (DELAY TIME 0..1): finito, picco < 1 V, la coda cala (12-14 s sotto 6-8 s)", [&lab, hdig] {
                bool ok = true;
                juce::String detail;
                for (int k : hdig)
                {
                    ok = ok && lab.v (k, 2) > 0.5 && lab.v (k, 0) < 1.0 && lab.v (k, 1) < lab.v (k, 3);
                    detail << num (lab.v (k, 3), 1) << " -> " << num (lab.v (k, 1), 1) << "; ";
                }
                return Verdict { ok, detail.trimCharactersAtEnd ("; ") + " dBV" };
            });
            lab.check (h, "Delay BBD con REPEAT/FEEDBACK al massimo (AD9, AD80, CD10, AD99, AD MINI, ES2, ES3; DELAY TIME 0..1): finiti, picco < 1 V, "
                          "coda < -90 dBV fino a 0.3 e < -55 dBV per tutti", [&lab, h, what] {
                bool ok = true;
                double pk = 0, tailShort = -300, tailAll = -300;
                juce::String bad;
                for (size_t k = 0; k < h.size(); ++k)
                {
                    const double p = lab.v (h[k], 0), tail = lab.v (h[k], 1);
                    const bool shortT = (k % 6) <= 2;
                    const bool g = lab.v (h[k], 2) > 0.5 && p < 1.0 && tail < -55.0 && (! shortT || tail < -90.0);
                    ok = ok && g;
                    pk = std::max (pk, p);
                    (shortT ? tailShort : tailAll) = std::max (shortT ? tailShort : tailAll, tail);
                    if (! g) bad << what[k] << " (" << num (p, 2) << " V, " << num (tail, 1) << " dBV) ";
                }
                return Verdict { ok, "picco max " + num (pk, 3) + " V, coda max " + num (tailShort, 1) + " dBV (<= 0.3) / " + num (tailAll, 1) + " dBV " + bad.trim() };
            });
            const int osc = at ("ibmes2", nullptr, { { "FEEDBACK", 1.0f }, { "OSCILLATE", 1.0f } }, bbdFeedback);
            lab.check ({ osc }, "ES2 OSCILLATE ON: autooscillazione limitata (coda -35..-15 dBV, picco < 1.5 V)", [&lab, osc] {
                const double p = lab.v (osc, 0), t = lab.v (osc, 1);
                return Verdict { lab.v (osc, 2) > 0.5 && p < 1.5 && t > -35.0 && t < -15.0, num (p, 3) + " V, coda " + num (t, 1) + " dBV" };
            });
            struct R { const char* id; Sets sets; };
            const R rs[] = { { "ib7fl303", { { "REGEN", 1.0f } } }, { "ib9fl9", { { "REGEN", 1.0f } } }, { "ibstfl5", { { "REGEN", 1.0f } } },
                             { "ibmfl99", { { "REGEN", 1.0f } } }, { "ibmaf2", { { "ENHANCE", 1.0f }, { "MODE", 1.0f } } },
                             { "ib10pc10", { { "FLANGE", 1.0f }, { "MODE", 1.0f } } }, { "ibmec40", { { "REGEN", 1.0f } } } };
            std::vector<int> hr;
            for (const auto& r : rs) hr.push_back (at (r.id, nullptr, r.sets, bbdFeedback));
            lab.check (hr, "Flanger e chorus con REGEN / FLANGE / ENHANCE al massimo (FL-303, FL9, FL5, FL99, AF2 TAKEOFF, PC10 B, EC40): coda < -70 dBV", [&lab, hr] {
                bool ok = true;
                double worst = -300;
                for (int k : hr) { ok = ok && lab.v (k, 2) > 0.5 && lab.v (k, 1) < -70.0; worst = std::max (worst, lab.v (k, 1)); }
                return Verdict { ok, "coda max " + num (worst, 1) + " dBV" };
            });
        }

        // 6. tremolo e pan (seno 440 Hz 0.3 V, inviluppo RMS su 5 ms)
        {
            const int tl = at ("ibsttl5", nullptr, { { "SPEED", 0.5f }, { "DEPTH", 1.0f } }, tremoloAm);
            const int tm = at ("ibmtrmini", nullptr, { { "SPEED", 0.5f }, { "DEPTH", 1.0f }, { "WAVE", 0.0f } }, tremoloAm);
            const int st = at ("ib7st800", nullptr, { { "SPEED", 0.5f } }, tremoloAm);
            lab.check ({ tl, tm, st }, "Tremolo: TL5 2.68 Hz (+-5%) profondita' > 40 dB, TREMOLO MINI 2.65 Hz > 30 dB; ST-800 1.55 Hz con L^2 + R^2 costante (+-0.5 dB)",
                       [&lab, tl, tm, st] {
                const bool ok = std::abs (lab.v (tl, 0) / 2.68 - 1.0) <= 0.05 && lab.v (tl, 1) > 40.0 && std::abs (lab.v (tm, 0) / 2.65 - 1.0) <= 0.05
                             && lab.v (tm, 1) > 30.0 && std::abs (lab.v (st, 0) / 1.55 - 1.0) <= 0.05 && lab.v (st, 2) <= 1.0;
                return Verdict { ok, "TL5 " + num (lab.v (tl, 0), 3) + " Hz " + num (lab.v (tl, 1), 1) + " dB, MINI " + num (lab.v (tm, 0), 3) + " Hz " + num (lab.v (tm, 1), 1)
                                         + " dB, ST-800 " + num (lab.v (st, 0), 3) + " Hz, potenza +-" + num (0.5 * lab.v (st, 2)) + " dB" };
            });
            auto gain = [&] (float level)
            {
                return at ("ibsttl5", nullptr, { { "DEPTH", 0.0f }, { "LEVEL", level } }, [] (const ModelDef& d, const std::map<int, float>& p)
                {
                    auto fx = makeAfter (d, p);
                    Buf l = sineBuf (48000.0, 440.0, 0.2, 6.0), r = l;
                    runStereo256 (*fx, l, r);
                    return std::vector<double> { dbOf (rmsOf (l, (size_t) (2 * 48000), l.size()) / 0.14142) };
                });
            };
            const int g6 = gain (0.6f), g1 = gain (1.0f);
            lab.check ({ g6, g1 }, "TL5: LEVEL a 0.6 guadagno unitario (+-0.2 dB), al massimo +4 dB (+-0.5)", [&lab, g6, g1] {
                return Verdict { within (lab.v (g6), 0.0, 0.2) && within (lab.v (g1), 4.0, 0.5), num (lab.v (g6)) + " / " + num (lab.v (g1)) + " dB" };
            });
        }

        // 7. uscite stereo
        {
            static const Buf g = guitarS3 (2.0, 4);
            std::vector<int> hc, hd;
            for (const char* id : { "ib9cs9", "ib10sc10", "ib9bc9" })
                hc.push_back (lab.add (id, [] (const ModelDef& d)
                {
                    auto fx = makeAfter (d, {});
                    Buf l = g, r = g;
                    runStereo256 (*fx, l, r);
                    double e = 0, ref = 0;
                    for (size_t i = 0; i < l.size(); ++i)
                    {
                        const double dry = 0.6 * g[i], diff = 0.5 * ((double) l[i] + r[i]) - dry;
                        e += diff * diff;
                        ref += dry * dry;
                    }
                    return std::vector<double> { 10.0 * std::log10 (e / std::max (ref, 1e-30) + 1e-30) };
                }));
            for (const char* id : { "ib9ad9", "ib10cd10", "ibmad99" })
                hd.push_back (lab.add (id, [] (const ModelDef& d)
                {
                    auto fx = makeAfter (d, {});
                    Buf l = g, r = g;
                    runStereo256 (*fx, l, r);
                    return std::vector<double> { std::memcmp (r.data(), g.data(), g.size() * sizeof (float)) == 0 ? 1.0 : 0.0 };
                }));
            // CS-505: con due canali R = diretto (dopo il blocco della continua: residuo rispetto all'ingresso), L = solo chorus
            const int cs = lab.add ("ib7cs505", [] (const ModelDef& d)
            {
                auto fx = makeAfter (d, {});
                Buf l = g, r = g;
                runStereo256 (*fx, l, r);
                double e = 0, ref = 0;
                for (size_t i = 0; i < r.size(); ++i) { e += ((double) r[i] - g[i]) * ((double) r[i] - g[i]); ref += (double) g[i] * g[i]; }
                fx = makeAfter (d, {});                       // impulso su un'istanza nuova, dopo 1 s di silenzio (linea vuota)
                l.assign ((size_t) 48000, 0.0f); r = l;
                runStereo256 (*fx, l, r);
                l.assign (4800, 0.0f); r = l;
                l[0] = r[0] = 0.05f;
                runStereo256 (*fx, l, r);
                double early = 0;
                for (int i = 0; i < 24; ++i) early = std::max (early, (double) std::abs (l[(size_t) i]));
                return std::vector<double> { 10.0 * std::log10 (e / std::max (ref, 1e-30) + 1e-30), early };
            });
            auto all = hc;
            all.insert (all.end(), hd.begin(), hd.end());
            all.push_back (cs);
            lab.check (all, "Uscite stereo: CS9 / SC10 / BC9 (L + R) / 2 = diretto x 0.6 (residuo < -40 dB); AD9 / CD10 / AD99 R = ingresso bit per bit; "
                            "CS-505 R = ingresso (residuo < -40 dB), L solo effetto", [&lab, hc, hd, cs] {
                bool ok = true;
                juce::String detail;
                for (int h : hc) { ok = ok && lab.v (h) < -40.0; detail << num (lab.v (h), 1) << " "; }
                int same = 0;
                for (int h : hd) same += lab.v (h) > 0.5 ? 1 : 0;
                ok = ok && same == 3 && lab.v (cs, 0) < -40.0 && lab.v (cs, 1) < 1.0e-4;
                return Verdict { ok, "residui " + detail.trim() + " dB, R = ingresso " + juce::String (same) + "/3, CS-505 R - ingresso " + num (lab.v (cs, 0), 1)
                                         + " dB, L a ritardo 0 " + num (lab.v (cs, 1), 6) + " V" };
            });
        }

        // 8. rumore a ingresso muto (non pesato, 3 s, dal secondo 1)
        {
            struct N { const char* id; double max; };
            const N ns[] = { { "ib9ad9", -105 }, { "ib10cd10", -105 }, { "ibmadmini", -105 }, { "ibmad99", -105 }, { "ibmes2", -105 }, { "ibmes3", -105 },
                             { "ib7ad80", -105 }, { "ib10sc10", -90 }, { "ib7fl303", -75 }, { "ib9fl9", -95 } };
            std::vector<int> h;
            for (const auto& x : ns)
                h.push_back (lab.add (x.id, [] (const ModelDef& d)
                {
                    auto fx = makeAfter (d, {});
                    Buf l ((size_t) (48000 * 3), 0.0f), r = l;
                    runStereo256 (*fx, l, r);
                    return std::vector<double> { dbOf (rmsOf (l, 48000, l.size())) };
                }));
            std::vector<N> copy (std::begin (ns), std::end (ns));
            lab.check (h, "Rumore a ingresso muto: delay col compander < -105 dBV, chorus MN3207 < -90, FL-303 (SAD1024) < -75, FL9 < -95 dBV", [&lab, h, copy] {
                bool ok = true;
                juce::String detail;
                for (size_t k = 0; k < h.size(); ++k)
                {
                    ok = ok && lab.v (h[k]) < copy[k].max;
                    detail << copy[k].id << " " << num (lab.v (h[k]), 1) << "; ";
                }
                return Verdict { ok, detail.trimCharactersAtEnd ("; ") + " dBV" };
            });
        }

        // reset() non riporta LFO e lisciature allo stato iniziale (innocuo: reset() si usa riaccendendo il pedale): determinismo fra istanze
        addClickAndDeterminism (lab, kStage4Mod, numMod, "Tappa 4 modulazioni", 10.0, false);
        lab.run (report);
    }

    //------------------------------------------------------------------------------
    // Misure del banco s4 (stereo, blocchi da 256, comandi per ruolo dopo prepare; "mix=0,1" = solo il ramo ritardato)
    //------------------------------------------------------------------------------
    struct S4Opt { int range = -1; float time = 0.0f; };

    std::unique_ptr<pt::engine::Effect> makeS4Wet (const pt::engine::ModelDef& d, const S4Opt& o, float depth = 0.0f)
    {
        auto fx = makeAfter (d, {});
        if (o.range >= 0) setStepS4 (*fx, "mode", o.range);
        setRoleS4 (*fx, "time", o.time); setRoleS4 (*fx, "feedback", 0.0f); setRoleS4 (*fx, "depth", depth);
        setRoleS4 (*fx, "level", 1.0f); setRoleS4 (*fx, "balance", 1.0f); setRoleS4 (*fx, "mix", 1.0f);
        return fx;
    }

    /** Ritardo (ms) del ramo ritardato: burst di 1 ms a 1 kHz dopo 0.8 s, picco della correlazione |ingresso x uscita| (canale L). */
    double s4Delay (const pt::engine::ModelDef& d, const S4Opt& o)
    {
        const double SR = 48000.0;
        auto fx = makeS4Wet (d, o);
        setRoleS4 (*fx, "v16", 0.0f); setRoleS4 (*fx, "v3", 0.0f);
        const int pre = (int) (0.8 * SR), len = (int) (3.4 * SR), bl = 48;
        Buf l ((size_t) (pre + len), 0.0f);
        for (int i = 0; i < bl; ++i) l[(size_t) (pre + i)] = 0.4f * std::sin (3.14159f * i / bl) * std::sin (2 * 3.14159f * 1000.0f * i / (float) SR + 0.3f);
        const Buf in = l;
        Buf r = l;
        runStereo256 (*fx, l, r);
        double best = -1;
        int lag = 0;
        for (int L = 1; L < len - bl; ++L)
        {
            double c = 0;
            for (int i = 0; i < bl; ++i) c += in[(size_t) (pre + i)] * l[(size_t) (pre + L + i)];
            c = std::abs (c);
            if (c > best) { best = c; lag = L; }
        }
        return lag * 1000.0 / SR;
    }

    /** Seno di ampiezza 'amp' per 3.8 s, fit sull'ultimo mezzo secondo: { guadagno (dB), SNR (dB) } del ramo ritardato (TONE a 1). */
    std::vector<double> s4Sine (const pt::engine::ModelDef& d, const S4Opt& o, double f, double amp)
    {
        const double SR = 48000.0;
        auto fx = makeS4Wet (d, o);
        setRoleS4 (*fx, "tone", 1.0f);
        Buf l = sineBuf (SR, f, amp, 3.8), r = l;
        runStereo256 (*fx, l, r);
        const size_t a = l.size() - (size_t) (0.5 * SR);
        const auto [A, B] = sineFit (l, f, a, l.size());
        double res = 0, tot = 0;
        for (size_t i = a; i < l.size(); ++i)
        {
            const double ph = 2 * kPi * f * (double) i / SR, fit = A * std::sin (ph) + B * std::cos (ph);
            res += (l[i] - fit) * (l[i] - fit);
            tot += fit * fit;
        }
        return { dbOf (std::sqrt (A * A + B * B) / amp), 10.0 * std::log10 (tot / std::max (1e-30, res)) };
    }

    /** Rumore del ramo ritardato a ingresso nullo (4 s, RMS dell'ultimo secondo, dBV). */
    double s4Noise (const pt::engine::ModelDef& d, const S4Opt& o)
    {
        auto fx = makeS4Wet (d, o);
        setRoleS4 (*fx, "tone", 1.0f);
        Buf l ((size_t) (4 * 48000), 0.0f), r = l;
        runStereo256 (*fx, l, r);
        return dbOf (rmsOf (l, l.size() - 48000, l.size()));
    }

    /** Fase srotolata del ramo ritardato con un seno a 25 Hz (media mobile su un periodo, ogni 240 campioni, da 3 s). */
    std::vector<double> s4Phase (const pt::engine::ModelDef& d, const S4Opt& o, float depth, float rate)
    {
        const double SR = 48000.0, f = 25.0;
        auto fx = makeS4Wet (d, o, depth);
        setRoleS4 (*fx, "rate", rate); setRoleS4 (*fx, "tone", 1.0f); setStepS4 (*fx, "alt", 0);
        const int n = (int) (14.0 * SR), per = (int) (SR / f);
        Buf l = sineBuf (SR, f, 0.2, 14.0), r = l;
        runStereo256 (*fx, l, r);
        std::vector<double> ph;
        double prev = 0, acc = 0;
        for (int p = (int) (3.0 * SR); p + per < n; p += 240)
        {
            double I = 0, Q = 0;
            for (int i = p; i < p + per; ++i)
            {
                const double w = 2.0 * kPi * f * i / SR;
                I += l[(size_t) i] * std::cos (w); Q += l[(size_t) i] * std::sin (w);
            }
            const double a = std::atan2 (I, Q);
            if (! ph.empty())
            {
                double dl = a - prev;
                while (dl > kPi) dl -= 2 * kPi;
                while (dl < -kPi) dl += 2 * kPi;
                acc += dl;
            }
            else acc = a;
            prev = a;
            ph.push_back (acc);
        }
        return ph;
    }

    /** Escursione del ritardo modulato: { min, max (percentili 1% e 99%, ms), LFO (Hz) }. */
    std::vector<double> s4Mod (const pt::engine::ModelDef& d, const S4Opt& o, float depth, float rate)
    {
        const double still = s4Delay (d, o);
        const auto ref = s4Phase (d, o, 0.0f, rate), ph = s4Phase (d, o, depth, rate);
        double r0 = 0, mean = 0;
        for (double v : ref) r0 += v;
        r0 /= (double) ref.size();
        for (double v : ph) mean += v;
        mean /= (double) ph.size();
        const double k = 1000.0 / (2.0 * kPi * 25.0), tw = 2.0 * kPi, m = std::round ((r0 - mean) / tw);
        std::vector<double> track;
        for (double v : ph) track.push_back (still + (r0 - (v + m * tw)) * k);
        auto sorted = track;
        std::sort (sorted.begin(), sorted.end());
        const double lo = sorted[(size_t) (0.01 * (double) sorted.size())], hi = sorted[(size_t) (0.99 * (double) (sorted.size() - 1))];
        const double mid = 0.5 * (lo + hi), hyst = 0.2 * (hi - lo);
        int cross = 0, side = 0;
        for (double v : track)
        {
            if (v > mid + hyst && side <= 0) { if (side < 0) ++cross; side = 1; }
            else if (v < mid - hyst && side >= 0) { if (side > 0) ++cross; side = -1; }
        }
        return { lo, hi, cross / 2.0 / ((double) track.size() * 240.0 / 48000.0) };
    }

    /** Stabilita': retroazione a 1, livello 1, frase del banco s4 di 2 s poi 18 s di silenzio; { picco (V), RMS ultimo secondo (dBV),
        RMS massimo su finestre di 1 s nei primi 3 s (dBV), finito }. */
    std::vector<double> s4Stability (const pt::engine::ModelDef& d, const S4Opt& o, int alt)
    {
        static const Buf g = guitarS4 (2.0);
        auto fx = makeAfter (d, {});
        if (alt >= 0) setStepS4 (*fx, "alt", alt);
        if (o.range >= 0) setStepS4 (*fx, "mode", o.range);
        setRoleS4 (*fx, "time", o.time); setRoleS4 (*fx, "feedback", 1.0f); setRoleS4 (*fx, "level", 1.0f);
        const int n = (int) (20.0 * 48000);
        Buf l = g;
        l.resize ((size_t) n, 0.0f);
        Buf r = l;
        runStereo256 (*fx, l, r);
        double pk = 0;
        bool fin = true;
        for (int i = 0; i < n; ++i)
        {
            fin = fin && std::isfinite (l[(size_t) i]) && std::isfinite (r[(size_t) i]);
            pk = std::max (pk, (double) std::max (std::abs (l[(size_t) i]), std::abs (r[(size_t) i])));
        }
        double early = 0;
        for (int s = 0; s + 48000 <= 3 * 48000; s += 4800) early = std::max (early, rmsOf (l, (size_t) s, (size_t) (s + 48000)));
        return { pk, dbOf (rmsOf (l, (size_t) (n - 48000), (size_t) n)), dbOf (early), fin ? 1.0 : 0.0 };
    }

    /** Gamme dichiarate nella config di un i4adm ("ranges=Nome:minms:maxms:bit[:flag]|...", i4layer: "time=min,max"):
        { minms, maxms, hold }. */
    struct S4Range { double lo, hi; bool hold; };
    std::vector<S4Range> s4Ranges (const pt::engine::ModelDef& d)
    {
        std::vector<S4Range> out;
        const pt::engine::Config cfg (d.config);
        if (cfg.str ("type") == "i4layer")
        {
            const auto t = cfg.list ("time");
            if (t.size() == 2) out.push_back ({ t[0], t[1], false });
            return out;
        }
        for (const auto& r : juce::StringArray::fromTokens (juce::String (cfg.str ("ranges")), "|", ""))
        {
            const auto f = juce::StringArray::fromTokens (r, ":", "");
            if (f.size() >= 4) out.push_back ({ f[1].getDoubleValue(), f[2].getDoubleValue(), f.size() > 4 && f[4].contains ("hold") });
        }
        return out;
    }

    /** Tappa 4, lavoro s4: delay, chorus e flanger digitali ADM e LD303 (FxStage4Time.cpp). Metodi del banco s4. */
    void runStage4DigitalTests (const std::function<void (const juce::String&, bool, const juce::String&)>& report)
    {
        using namespace pt::engine;
        constexpr int numDig = countOf (kStage4Digital);
        Lab lab;

        // 0. pedali esistenti invariati bit per bit (banco s4 'hash': frase di 4 s, poi comandi spostati di +0.37 e frase di 3 s). La prima
        //    impronta (comandi fermi) e' invariata; dalla 1.3.1 la seconda e' nuova per DD-2/3/8/5 e RV-6/5 (famiglie vecchie con
        //    l'involucro senza click: i comandi spostati scorrono e i selettori passano per una dissolvenza)
        {
            struct Ref { const char* id; uint64_t a, b; };
            static const Ref refs[] = {
                { "dd2", 0x590d15b01a380403ull, 0x16e77bd823cda5aaull }, { "dd3", 0x590d15b01a380403ull, 0x16e77bd823cda5aaull },
                { "dd8", 0x92261ccdd28adf26ull, 0xa84adb5c422de4eaull }, { "dd5", 0x014f09a9444e70e3ull, 0xb6528be26e2e4e01ull },
                { "rv6", 0x54957206ce566aa3ull, 0x951f21dfedb1271aull }, { "rv5", 0x8c9485d03f8c184full, 0x576bdf45498fc8fcull },
                { "eh16sec", 0x4134d161fc535ce3ull, 0x677caddc91298e5full }, { "ehcanyon", 0x4289809e604536b6ull, 0xf57051122a5de3cdull },
                { "eh1echo", 0x88666b012b77805dull, 0xc664bcb07ecc79e3ull }, { "ehhgmax", 0x71e1b361f48ef709ull, 0x334c52fa5ca473c6ull },
                { "ehdmm", 0x701dabd49a1c457full, 0xa638f22f4e8bb063ull }, { "ehcanecho", 0xb6f86f1122f524bbull, 0x8b68562fea06b6c3ull },
                { "ehabyss", 0xfb160dc28622a6a2ull, 0x9b820cdc95c6d4ffull }, { "ehmmstereo", 0x4a3659cfb24a92caull, 0x568c7d296d57eb57ull },
                { "ehreplay", 0x1dbd8c954cc91723ull, 0xbf4ac868324f5587ull } };
            static const Buf g1 = guitarS4 (4.0, 3), g2 = guitarS4 (3.0, 5);
            std::vector<int> h;
            for (const auto& r : refs)
            {
                h.push_back (lab.add (r.id, [] (const ModelDef& d)
                {
                    auto fx = makeAfter (d, {});
                    Buf l = g1, rr = g1;
                    for (auto& x : rr) x *= 0.8f;
                    runStereo256 (*fx, l, rr);
                    const uint64_t a = fnvBytes (rr, fnvBytes (l));
                    for (int k = 0; k < d.numControls; ++k) fx->params[k].store (std::fmod (d.controls[k].def + 0.37f, 1.0f));
                    Buf l2 = g2, r2 = g2;
                    runStereo256 (*fx, l2, r2);
                    const uint64_t b = fnvBytes (r2, fnvBytes (l2));
                    return std::vector<double> { (double) (uint32_t) (a >> 32), (double) (uint32_t) a, (double) (uint32_t) (b >> 32), (double) (uint32_t) b };
                }));
                lab.first (h.back());
            }
            lab.check (h, "Tappa 4: DD-2/3/8/5, RV-6/5, 16 Second, Canyon, 1 Echo, Holy Grail Max, DMM, Canyon Echo, Oceans Abyss, Memory Man Stereo, Instant Replay "
                          "invariati bit per bit", [&lab, h] {
                if (! kBitExactRefs) return Verdict { true, "impronte di riferimento misurate con GCC su Linux: confronto non applicabile a questo compilatore" };
                juce::String bad;
                for (size_t k = 0; k < h.size(); ++k)
                {
                    const uint64_t a = ((uint64_t) lab.v (h[k], 0) << 32) | (uint64_t) lab.v (h[k], 1), b = ((uint64_t) lab.v (h[k], 2) << 32) | (uint64_t) lab.v (h[k], 3);
                    if (a != refs[k].a || b != refs[k].b)
                        bad << refs[k].id << " DIVERSO (" << juce::String::toHexString ((juce::int64) a) << " " << juce::String::toHexString ((juce::int64) b) << ") ";
                }
                return Verdict { bad.isEmpty(), bad.isEmpty() ? juce::String ((int) h.size()) + "/" + juce::String ((int) h.size()) + " doppie impronte identiche" : bad.trim() };
            });
        }

        // 1. tempo di ritardo dei 20 modelli: ogni gamma a D-TIME 0 e 1 (la prima anche a 0.5); atteso = estremi dichiarati nella config
        //    (minms, minms * sqrt (max / min), maxms) + 0.05..0.2 ms di filtri, tolleranza 0.25 ms + 1%
        {
            struct Job { const char* id; int range; float t; };
            std::vector<Job> jobs;
            std::vector<int> h;
            for (int m = 0; m < numDig; ++m)
            {
                const auto* d = findModel (kStage4Digital[m]);
                if (d == nullptr) { h.push_back (lab.add (kStage4Digital[m], {})); jobs.push_back ({ kStage4Digital[m], 0, 0 }); continue; }
                const auto rs = s4Ranges (*d);
                const bool multi = stepsOfRole (*d, "mode") > 1 && rs.size() > 1;
                for (int r = 0; r < (int) rs.size(); ++r)
                {
                    if (rs[(size_t) r].hold) continue;
                    for (int ti = 0; ti < 3; ++ti)
                    {
                        if (ti == 1 && r > 0) continue;
                        const float t = 0.5f * (float) ti;
                        const S4Opt o { multi ? r : -1, t };
                        h.push_back (lab.add (withConfig (*d, "mix=0,1"), [o] (const ModelDef& dd) { return std::vector<double> { s4Delay (dd, o) }; }));
                        jobs.push_back ({ kStage4Digital[m], r, t });
                    }
                }
            }
            lab.check (h, "Tappa 4 digitali: tempo di ritardo di ogni gamma a D-TIME 0 / (0.5) / 1 = estremi dichiarati + 0.05..0.2 ms (+-0.25 ms + 1%, "
                          + juce::String ((int) h.size()) + " misure)", [&lab, h, jobs] {
                bool ok = true;
                double worst = 0;
                juce::String bad, ex;
                for (size_t k = 0; k < h.size(); ++k)
                {
                    const auto* d = findModel (jobs[k].id);
                    const auto rs = s4Ranges (*d);
                    const auto& R = rs[(size_t) jobs[k].range];
                    const double decl = R.lo * std::pow (R.hi / R.lo, (double) jobs[k].t), v = lab.v (h[k]), tl = 0.25 + 0.01 * decl;
                    const double dev = v < decl + 0.05 ? decl + 0.05 - v : (v > decl + 0.2 ? v - decl - 0.2 : 0.0);
                    worst = std::max (worst, dev / tl);
                    if (dev > tl) { ok = false; bad << jobs[k].id << "/" << jobs[k].range << "@" << num (jobs[k].t, 1) << " " << num (v) << " ms (" << num (decl) << ") "; }
                    if (std::strcmp (jobs[k].id, "ib10dl10") == 0 || std::strcmp (jobs[k].id, "ibmld303") == 0) ex << jobs[k].id << " " << num (v) << "; ";
                }
                return Verdict { ok, "scarto max " + num (worst * 100.0, 0) + "% della tolleranza; " + ex.trimCharactersAtEnd ("; ") + " ms " + bad.trim() };
            });
        }

        // 2-4. banda del primo eco (gamma di default, D-TIME 0 e 1; relativa a 1 kHz), SNR a 1 kHz e rumore di fondo
        {
            struct Band { const char* id; std::vector<int> h; std::vector<double> f; };
            std::vector<Band> bands;
            std::vector<int> hAll, hSnr, hNoise;
            std::vector<juce::String> noiseIds;
            for (int m = 0; m < numDig; ++m)
            {
                const auto* d = findModel (kStage4Digital[m]);
                if (d == nullptr) { hAll.push_back (lab.add (kStage4Digital[m], {})); continue; }
                const Config cfg (d->config);
                const bool layer = cfg.str ("type") == "i4layer";
                const long bw = std::lround (cfg.num ("bw", 0));
                std::vector<double> freqs { 500.0, 1000.0, 2000.0 };
                if (layer) freqs.push_back (14000.0);
                else if (bw == 7000) { freqs.push_back (7000.0); freqs.push_back (10000.0); }
                else if (bw == 12000) { freqs.push_back (10000.0); freqs.push_back (14000.0); }
                else if (bw == 16000) { freqs.push_back (14000.0); freqs.push_back (18000.0); }
                else { freqs.push_back (7000.0); freqs.push_back (10000.0); }
                const int mi = roleIndex (*d, "mode");
                const int rDef = mi >= 0 ? (int) std::lround (d->controls[mi].def * (stepsOfRole (*d, "mode") - 1)) : -1;
                const auto wet = withConfig (*d, "mix=0,1");
                for (float t : { 0.0f, 1.0f })
                {
                    Band b { kStage4Digital[m], {}, freqs };
                    for (double f : freqs)
                    {
                        b.h.push_back (lab.add (wet, [rDef, t, f] (const ModelDef& dd) { return s4Sine (dd, { rDef, t }, f, 0.1); }));
                        hAll.push_back (b.h.back());
                    }
                    bands.push_back (b);
                    hNoise.push_back (lab.add (wet, [rDef, t] (const ModelDef& dd) { return std::vector<double> { s4Noise (dd, { rDef, t }) }; }));
                    noiseIds.push_back (juce::String (kStage4Digital[m]) + "@" + juce::String ((int) t));
                }
            }
            lab.check (hAll, "Tappa 4 digitali: banda del primo eco (7 kHz: a 7k -2.5..-6 dB, 10k <= -15; 12 kHz: 10k -1..-6, 14k <= -10; 16 kHz: 14k -2.5..-6, 18k <= -10; "
                             "8 kHz: 7k -1..-4, 10k <= -10; LD303 14k >= -3 dB; sotto 3 kHz +-0.5 dB)", [&lab, bands] {
                bool ok = true;
                juce::String bad, ex;
                for (const auto& b : bands)
                {
                    const auto* d = findModel (b.id);
                    const Config cfg (d->config);
                    const bool layer = cfg.str ("type") == "i4layer";
                    const long bw = std::lround (cfg.num ("bw", 0));
                    const double ref = lab.v (b.h[1]);
                    auto rel = [&] (size_t k) { return lab.v (b.h[k]) - ref; };
                    bool g = std::abs (rel (0)) <= 0.5 && std::abs (rel (2)) <= 0.5;
                    if (layer) g = g && rel (3) >= -3.0;
                    else if (bw == 7000) g = g && rel (3) <= -2.5 && rel (3) >= -6.0 && rel (4) <= -15.0;
                    else if (bw == 12000) g = g && rel (3) <= -1.0 && rel (3) >= -6.0 && rel (4) <= -10.0;
                    else if (bw == 16000) g = g && rel (3) <= -2.5 && rel (3) >= -6.0 && rel (4) <= -10.0;
                    else g = g && rel (3) <= -1.0 && rel (3) >= -4.0 && rel (4) <= -10.0;
                    ok = ok && g;
                    juce::String s;
                    s << b.id << " " << juce::roundToInt (b.f[3] / 1000.0) << "k " << num (rel (3), 1);
                    if (b.f.size() > 4) s << " " << juce::roundToInt (b.f[4] / 1000.0) << "k " << num (rel (4), 1);
                    if (! g) bad << s << " (500/2k " << num (rel (0), 1) << "/" << num (rel (2), 1) << ") ";
                    if (std::strcmp (b.id, "ib10dl10") == 0 && ex.isEmpty()) ex = s;
                }
                return Verdict { ok, ex + " dB " + bad.trim() };
            });
            // SNR: valori del banco s4 (measure6.txt, gamma di default) con la tolleranza di +-4 dB del file; il ">= 50 dB per tutti" di
            // tests.md e' arrotondato (DSC10 misurato 49.4 dB anche dal banco)
            lab.check (hAll, "Tappa 4 digitali: SNR a 1 kHz (seno 0.1 V) entro 4 dB dal banco a D-TIME 0 e 1; M50195 (DL10, DPL10, DL5) ed EM5 almeno 10 dB "
                             "peggiori a D-TIME 1 (meno bit al secondo); LD303 >= 100 dB", [&lab, bands] {
                struct Snr { const char* id; double s0, s1; };
                static const Snr refs[] = { { "ib10dl10", 61.8, 39.9 }, { "ib10dpl10", 58.1, 39.9 }, { "ib10dsc10", 49.4, 49.4 }, { "ib10ddl10", 58.7, 40.5 },
                                            { "ib10dml10", 61.3, 39.6 }, { "ib10dcf10", 61.1, 36.5 }, { "ib10ddl20", 59.2, 56.8 }, { "ib10dml20", 59.2, 56.8 },
                                            { "ib10dcl", 62.8, 44.8 }, { "ib10ddl", 61.7, 41.0 }, { "ib10dfl", 62.8, 44.9 }, { "ib10dml", 62.8, 44.8 },
                                            { "ib10pdm1", 59.2, 56.8 }, { "ib10pdd1", 59.2, 56.8 }, { "ibstdl5", 58.1, 39.9 }, { "ibstem5", 58.5, 29.0 },
                                            { "ibtlcf7", 58.4, 40.0 }, { "ibtltc7", 53.5, 53.5 }, { "ibtlde7", 56.0, 50.8 }, { "ibmld303", 146.7, 146.1 } };
                bool ok = true;
                double worst = 0;
                juce::String bad, ex;
                for (size_t k = 0; k + 1 < bands.size(); k += 2)
                {
                    const auto& b0 = bands[k];
                    const double s0 = lab.v (b0.h[1], 1), s1 = lab.v (bands[k + 1].h[1], 1);
                    const Snr* r = nullptr;
                    for (const auto& x : refs) if (std::strcmp (x.id, b0.id) == 0) r = &x;
                    const bool layer = std::strcmp (b0.id, "ibmld303") == 0;
                    const bool clockLaw = std::strcmp (b0.id, "ib10dl10") == 0 || std::strcmp (b0.id, "ib10dpl10") == 0 || std::strcmp (b0.id, "ibstdl5") == 0
                                       || std::strcmp (b0.id, "ibstem5") == 0;
                    bool g = r != nullptr && (layer ? s0 >= 100.0 && s1 >= 100.0 : std::abs (s0 - r->s0) <= 4.0 && std::abs (s1 - r->s1) <= 4.0);
                    if (r != nullptr && ! layer) worst = std::max (worst, std::max (std::abs (s0 - r->s0), std::abs (s1 - r->s1)));
                    if (clockLaw) g = g && s0 - s1 >= 10.0;
                    ok = ok && g;
                    if (! g) bad << b0.id << " " << num (s0, 1) << " / " << num (s1, 1) << " ";
                    if (clockLaw) ex << b0.id << " " << num (s0, 1) << " / " << num (s1, 1) << "; ";
                }
                return Verdict { ok, "scarto max " + num (worst, 1) + " dB; " + ex.trimCharactersAtEnd ("; ") + " dB " + bad.trim() };
            });
            lab.check (hNoise, "Tappa 4 digitali: rumore di fondo del ramo ritardato fra hiss - 2 e hiss + 6 dB (hiss della config; EM5 a D-TIME 1 fino a -80 dBV +2), "
                               "LD303 < -120 dBV", [&lab, hNoise, noiseIds] {
                bool ok = true;
                juce::String bad, ex;
                for (size_t k = 0; k < hNoise.size(); ++k)
                {
                    const auto id = noiseIds[k].upToFirstOccurrenceOf ("@", false, false);
                    const auto* d = findModel (id.toRawUTF8());
                    const double v = lab.v (hNoise[k]), hiss = Config (d->config).num ("hiss", -90);
                    bool g;
                    if (id == "ibmld303") g = v < -120.0;
                    else if (id == "ibstem5" && noiseIds[k].endsWith ("@1")) g = v >= hiss - 2.0 && v <= -78.0;
                    else g = v >= hiss - 2.0 && v <= hiss + 6.0;
                    ok = ok && g;
                    if (! g) bad << noiseIds[k] << " " << num (v, 1) << " (" << num (hiss, 0) << ") ";
                    if (id == "ib10dl10" || id == "ibtlde7" || id == "ibstem5") ex << noiseIds[k] << " " << num (v, 1) << "; ";
                }
                return Verdict { ok, ex.trimCharactersAtEnd ("; ") + " dBV " + bad.trim() };
            });
        }

        // 5. modulazione (gamma di default, D-TIME 0.5, WIDTH 1, SPEED 0.3): estremi +-12% + 0.25 ms (media mobile di 40 ms; 0.25 ms come
        //    la tolleranza dei ritardi, per i ritardi minimi dove pesano i filtri), LFO +-10%; sotto 0.25 Hz (DSC10, CF7) +-25% come indica
        //    tests.md (la finestra di 11 s vede meno di tre periodi)
        {
            struct M { const char* id; double lo, hi, hz, tolHz; };
            const M ms[] = { { "ib10dsc10", 7.07, 14.1, 0.22, 0.25 }, { "ib10dml10", 0.9, 7.2, 0.30, 0.10 }, { "ib10dcf10", 0.25, 2.0, 0, 0 },
                             { "ib10dml20", 4.0, 16.0, 0.32, 0.10 }, { "ib10dcl", 1.0, 8.0, 1.19, 0.10 }, { "ib10dfl", 0.5, 4.0, 0, 0 },
                             { "ib10dml", 8.0, 64.0, 0, 0 }, { "ib10pdm1", 64.0, 256.0, 0, 0 }, { "ibtlcf7", 2.52, 21.8, 0.15, 0.25 },
                             { "ibtltc7", 4.9, 7.35, 0, 0 } };
            std::vector<int> h;
            for (const auto& x : ms)
            {
                const auto* d = findModel (x.id);
                if (d == nullptr) { h.push_back (lab.add (x.id, {})); continue; }
                const int mi = roleIndex (*d, "mode");
                const int rDef = mi >= 0 ? (int) std::lround (d->controls[mi].def * (stepsOfRole (*d, "mode") - 1)) : -1;
                h.push_back (lab.add (withConfig (*d, "mix=0,1"), [rDef] (const ModelDef& dd) { return s4Mod (dd, { rDef, 0.5f }, 1.0f, 0.3f); }));
                lab.first (h.back());
            }
            std::vector<M> copy (std::begin (ms), std::end (ms));
            lab.check (h, "Tappa 4 digitali: escursione della modulazione (DSC10 7.07-14.1, DML10 0.9-7.2, DCF10 0.25-2, DML20 4-16, DCL 1-8, DFL 0.5-4, DML 8-64, PDM1 64-256, "
                          "CF7 2.52-21.8, TC7 4.9-7.35 ms) e LFO", [&lab, h, copy] {
                bool ok = true;
                juce::String detail;
                for (size_t k = 0; k < h.size(); ++k)
                {
                    const auto& x = copy[k];
                    const double lo = lab.v (h[k], 0), hi = lab.v (h[k], 1), f = lab.v (h[k], 2);
                    bool g = std::abs (lo - x.lo) <= 0.12 * x.lo + 0.25 && std::abs (hi - x.hi) <= 0.12 * x.hi + 0.25;
                    if (x.hz > 0) g = g && std::abs (f / x.hz - 1.0) <= x.tolHz;
                    ok = ok && g;
                    detail << x.id << " " << num (lo) << "-" << num (hi);
                    if (x.hz > 0) detail << " " << num (f, 2) << " Hz";
                    detail << (g ? "" : " (FUORI)") << "; ";
                }
                return Verdict { ok, detail.trimCharactersAtEnd ("; ") + " ms" };
            });
        }

        // 6. stabilita' con la retroazione al massimo (gamma minima e massima, D-TIME 0 e 1; DE7 e CF7 anche con la levetta): finiti,
        //    picco <= 1.6 V, nessuna crescita (ultimo secondo <= massimo dei primi 3 s), <= -70 dBV per fbmax < 1 e ritardi <= 160 ms;
        //    ripetizioni infinite dichiarate (DE7 ECHO, EM5, LD303): ultimo secondo <= -10 dBV e picco <= 0.8 V
        {
            struct St { const char* id; S4Opt o; int alt; double delayMs; };
            std::vector<St> runs;
            std::vector<int> h;
            for (int m = 0; m < numDig; ++m)
            {
                const auto* d = findModel (kStage4Digital[m]);
                if (d == nullptr) { h.push_back (lab.add (kStage4Digital[m], {})); runs.push_back ({ kStage4Digital[m], {}, -1, 0 }); continue; }
                if (roleIndex (*d, "feedback") < 0) continue;
                const auto rs = s4Ranges (*d);
                const int nr = stepsOfRole (*d, "mode") > 1 ? (int) rs.size() : 1;
                auto add = [&] (int r, float t, int alt)
                {
                    const S4Opt o { nr > 1 ? r : -1, t };
                    const auto& R = rs[(size_t) std::max (0, r)];
                    h.push_back (lab.add (kStage4Digital[m], [o, alt] (const ModelDef& dd) { return s4Stability (dd, o, alt); }));
                    runs.push_back ({ kStage4Digital[m], o, alt, R.lo * std::pow (R.hi / R.lo, (double) t) });
                };
                for (int r : (nr > 1 ? std::vector<int> { 0, nr - 1 } : std::vector<int> { 0 }))
                    for (float t : { 0.0f, 1.0f }) add (r, t, -1);
                if (roleIndex (*d, "alt") >= 0)
                    for (int al : { 0, 1 })
                        for (float t : { 0.0f, 1.0f }) add (nr - 1, t, al);
            }
            for (int k : h) lab.first (k, 0);
            lab.check (h, "Tappa 4 digitali: retroazione al massimo, 2 s di frase e 18 s di silenzio (" + juce::String ((int) h.size()) + " prove): finiti, picco <= 1.6 V, "
                          "nessuna crescita, <= -70 dBV coi ritardi <= 160 ms; DE7 ECHO, EM5, LD303 <= -10 dBV e <= 0.8 V", [&lab, h, runs] {
                bool ok = true;
                double pk = 0;
                juce::String bad;
                for (size_t k = 0; k < h.size(); ++k)
                {
                    const auto& r = runs[k];
                    const auto* d = findModel (r.id);
                    const Config cfg (d->config);
                    const double p = lab.v (h[k], 0), last = lab.v (h[k], 1), early = lab.v (h[k], 2);
                    const bool infinite = std::strcmp (r.id, "ibstem5") == 0 || std::strcmp (r.id, "ibmld303") == 0
                                       || (std::strcmp (r.id, "ibtlde7") == 0 && r.alt == 0);
                    bool g = lab.v (h[k], 3) > 0.5 && p <= 1.6;
                    if (infinite) g = g && last <= -10.0 && p <= 0.8;      // ripetizioni infinite limitate dai diodi: al posto della crescita
                    else
                    {
                        g = g && last <= early;
                        if (cfg.num ("fbmax", 1.0) < 1.0 && r.delayMs <= 160.0) g = g && last <= -70.0;
                    }
                    ok = ok && g;
                    pk = std::max (pk, p);
                    if (! g) bad << r.id << "/" << r.o.range << "@" << num (r.o.time, 0) << (r.alt >= 0 ? "/alt" + juce::String (r.alt) : juce::String()) << " ("
                                 << num (p, 2) << " V, " << num (last, 1) << " dBV) ";
                }
                return Verdict { ok, "picco max " + num (pk) + " V " + bad.trim() };
            });
        }

        // 7. HOLD del DDL: frase a impulsi in LONG (D-TIME 0.3), poi HOLD con un seno a 300 Hz all'ingresso; il ramo ritardato resta periodico
        //    col periodo del campione congelato (correlazione >= 0.999) e non contiene il seno entrato dopo (< -40 dB)
        {
            const int hd = addPatched (lab, "ib10ddl", "mix=0,1", [] (const ModelDef& d)
            {
                const double SR = 48000.0;
                auto fx = makeAfter (d, {});
                setStepS4 (*fx, "mode", 1); setRoleS4 (*fx, "time", 0.3f); setRoleS4 (*fx, "balance", 1.0f); setRoleS4 (*fx, "feedback", 0.0f);
                const int n = (int) (6 * SR), sw = (int) (1.5 * SR);
                Buf l ((size_t) n), r;
                for (int i = 0; i < n; ++i)
                {
                    const double t = i / SR;
                    l[(size_t) i] = i < sw ? (float) (0.2 * (std::sin (2 * kPi * 110 * t) + std::sin (2 * kPi * 165 * t)) * (std::fmod (t, 0.25) < 0.12 ? 1 : 0.2))
                                           : (float) (0.2 * std::sin (2 * kPi * 300 * t));
                }
                r = l;
                runStereo256 (*fx, l, r, [&] (size_t i) { if ((int) i <= sw && sw < (int) i + 256) setStepS4 (*fx, "mode", 2); });
                const double P = 112.0 * std::pow (900.0 / 112.0, 0.3) * 0.001 * SR;
                double bc = -1;
                const int a = (int) (3.0 * SR), len = (int) (1.0 * SR);
                for (int L = (int) (P * 0.9); L < (int) (P * 1.1); ++L)
                {
                    double c = 0, e1 = 0, e2 = 0;
                    for (int i = a; i < a + len; ++i)
                    {
                        c += (double) l[(size_t) i] * l[(size_t) (i + L)];
                        e1 += (double) l[(size_t) i] * l[(size_t) i];
                        e2 += (double) l[(size_t) (i + L)] * l[(size_t) (i + L)];
                    }
                    bc = std::max (bc, c / std::sqrt (e1 * e2 + 1e-30));
                }
                double I = 0, Q = 0, tot = 0;
                for (int i = a; i < a + len; ++i)
                {
                    const double w = 2 * kPi * 300 * i / SR;
                    I += l[(size_t) i] * std::cos (w); Q += l[(size_t) i] * std::sin (w); tot += (double) l[(size_t) i] * l[(size_t) i];
                }
                const double p300 = 2 * (I * I + Q * Q) / len / len, ptot = tot / len;
                return std::vector<double> { bc, 10 * std::log10 (p300 / ptot + 1e-30), 10 * std::log10 (ptot + 1e-30) };
            });
            lab.check ({ hd }, "DDL HOLD: il campione congelato si ripete (correlazione fra periodi >= 0.999) e il seno entrato dopo non passa (< -40 dB)", [&lab, hd] {
                return Verdict { lab.v (hd, 0) >= 0.999 && lab.v (hd, 1) < -40.0 && lab.v (hd, 2) > -60.0,
                                 "correlazione " + num (lab.v (hd, 0), 5) + ", 300 Hz " + num (lab.v (hd, 1), 1) + " dB, wet " + num (lab.v (hd, 2), 1) + " dBV" };
            });
        }

        // livelli col riff a -12 dBFS e i comandi di default (finiti, picco < 0 dBFS, RMS -40..-20 dBFS)
        {
            static const Buf riff = guitarS1 (true, 48000.0, 3.0, -12.0);
            std::vector<int> hl;
            for (int m = 0; m < numDig; ++m)
                hl.push_back (lab.add (kStage4Digital[m], [] (const ModelDef& d)
                {
                    auto fx = makeS1 (d, {});
                    Buf y = riff;
                    processMono (*fx, y, 512);
                    double pk = 0;
                    bool fin = true;
                    for (float v : y) { fin = fin && std::isfinite (v); pk = std::max (pk, (double) std::abs (v)); }
                    return std::vector<double> { fin ? 1.0 : 0.0, dbOf (pk), dbOf (rmsOf (y, (size_t) (0.05 * 48000.0))) };
                }));
            lab.check (hl, "Tappa 4 digitali: livelli con riff a -12 dBFS e comandi di default (20 modelli: finiti, picco < 0 dBFS, RMS -40..-20 dBFS)", [&lab, hl] {
                int good = 0;
                double lo = 0, hi = -200, pk = -200;
                juce::String bad;
                for (int m = 0; m < numDig; ++m)
                {
                    const double p = lab.v (hl[(size_t) m], 1), rms = lab.v (hl[(size_t) m], 2);
                    const bool g = lab.v (hl[(size_t) m], 0) > 0.5 && p < 0.0 && rms > -40.0 && rms < -20.0;
                    if (g) ++good; else bad << kStage4Digital[m] << " (RMS " << num (rms, 1) << ", picco " << num (p, 1) << ") ";
                    lo = std::min (lo, rms); hi = std::max (hi, rms); pk = std::max (pk, p);
                }
                return Verdict { good == numDig, juce::String (good) + "/20, RMS " + num (lo, 1) + ".." + num (hi, 1) + " dBFS, picco max " + num (pk, 1) + " dBFS " + bad.trim() };
            });
        }

        // click: DE7 MODE ECHO/DELAY (143x) ha una dissolvenza di 10 ms dalla 1.2.6; fino alla 1.2.6 avevano una soglia propria
        // anche LD303 TIME e DML20 WIDTH (16x: lisciatura a un polo, il glissato partiva con la velocita' massima all'istante del
        // cambio) e PDM1 / PDD1 SOUND (15x / 21x: il tempo della memoria nuova cominciava a scorrere mentre l'eco vecchia usciva
        // ancora). Dalla 1.2.7 tempo e larghezza hanno una lisciatura del secondo ordine e i valori di una memoria nuova entrano
        // al cambio, a wet spento: tutti sotto 2x, nessuna eccezione
        addClickAndDeterminism (lab, kStage4Digital, numDig, "Tappa 4 digitali", 10.0, true);
        lab.run (report);
    }

    //------------------------------------------------------------------------------
    // Tappa 5: misure comuni ai quattro gruppi
    //------------------------------------------------------------------------------
    /** Riff del banco s1 a -12 dBFS (comandi impostati da 'setup' prima di prepare, blocchi da 512): { RMS dBFS, picco dBFS,
        finito, x tempo reale (CPU del thread) }, RMS e picco dopo i primi 50 ms. */
    std::vector<double> riffLevelsS5 (const pt::engine::ModelDef& d, const std::function<void (pt::engine::Effect&)>& setup)
    {
        static const Buf riff = guitarS1 (true, 48000.0, 3.0, -12.0);
        auto fx = pt::engine::createEffect (d);
        if (setup) setup (*fx);
        fx->prepare (48000.0, 512);
        Buf y = riff;
        const double t0 = threadCpuSeconds();
        processMono (*fx, y, 512);
        const double cpu = threadCpuSeconds() - t0;
        const size_t skip = (size_t) (0.05 * 48000.0);
        double acc = 0, pk = 0;
        bool fin = true;
        for (size_t i = skip; i < y.size(); ++i)
        {
            fin = fin && std::isfinite (y[i]);
            acc += (double) y[i] * y[i];
            pk = std::max (pk, (double) std::abs (y[i]));
        }
        return { 10.0 * std::log10 (acc / (double) (y.size() - skip) + 1e-30), dbOf (pk), fin ? 1.0 : 0.0, 3.0 / std::max (1.0e-6, cpu) };
    }

    /** Comandi tutti al massimo e tutti al minimo (selettori e levette all'ultima / prima posizione), riff a -12 dBFS: finiti, picco
        < +18 dBFS. Restituisce le misure ai comandi di default (stesso ordine di 'ids') per i controlli dei livelli del gruppo. */
    std::vector<int> addLevelsS5 (Lab& lab, const char* const* ids, int n, const juce::String& group)
    {
        std::vector<int> hDef, hMax, hMin;
        for (int m = 0; m < n; ++m)
        {
            hDef.push_back (lab.add (ids[m], [] (const pt::engine::ModelDef& d) { return riffLevelsS5 (d, {}); }));
            for (float v : { 1.0f, 0.0f })
                (v > 0.5f ? hMax : hMin).push_back (lab.add (ids[m], [v] (const pt::engine::ModelDef& d)
                {
                    return riffLevelsS5 (d, [&d, v] (pt::engine::Effect& fx) { for (int k = 0; k < d.numControls; ++k) fx.params[k].store (v); });
                }));
        }
        std::vector<int> hExt = hMax;
        hExt.insert (hExt.end(), hMin.begin(), hMin.end());
        lab.check (hExt, group + ": comandi tutti al massimo e tutti al minimo (" + juce::String (n) + " modelli, riff a -12 dBFS): finiti, picco < +18 dBFS",
                   [&lab, hMax, hMin, ids, n] {
            double worst = -200;
            juce::String bad, worstId;
            for (int m = 0; m < n; ++m)
                for (int s = 0; s < 2; ++s)
                {
                    const int h = (s == 0 ? hMax : hMin)[(size_t) m];
                    const bool fin = lab.v (h, 2) > 0.5;
                    const double p = fin ? lab.v (h, 1) : 999.0;
                    if (p > worst) { worst = p; worstId = juce::String (ids[m]) + (s == 0 ? " max" : " min"); }
                    if (! fin || p >= 18.0) bad << ids[m] << (s == 0 ? "/max" : "/min") << (fin ? "" : " NaN") << " ";
                }
            return Verdict { bad.isEmpty(), "picco max " + num (worst, 1) + " dBFS (" + worstId + ") " + bad.trim() };
        });
        return hDef;
    }

    /** Controllo dei livelli ai comandi di default: finiti, picco < pkMax dBFS, RMS fra rmsLo e rmsHi dBFS. */
    void checkLevelsS5 (Lab& lab, const std::vector<int>& hDef, const char* const* ids, int n, const juce::String& name, double rmsLo, double rmsHi,
                        double pkMax)
    {
        lab.check (hDef, name, [&lab, hDef, ids, n, rmsLo, rmsHi, pkMax] {
            int good = 0;
            double lo = 0, hi = -200, pk = -200;
            juce::String bad;
            for (int m = 0; m < n; ++m)
            {
                const double rms = lab.v (hDef[(size_t) m], 0), p = lab.v (hDef[(size_t) m], 1);
                const bool g = lab.v (hDef[(size_t) m], 2) > 0.5 && p < pkMax && rms > rmsLo && rms < rmsHi;
                if (g) ++good; else bad << ids[m] << " (RMS " << num (rms, 1) << ", picco " << num (p, 1) << ") ";
                lo = std::min (lo, rms); hi = std::max (hi, rms); pk = std::max (pk, p);
            }
            return Verdict { good == n, juce::String (good) + "/" + juce::String (n) + ", RMS " + num (lo, 1) + ".." + num (hi, 1) + " dBFS, picco max " + num (pk, 1)
                                            + " dBFS " + bad.trim() };
        });
    }

    /** Impronta di un pedale esistente col metodo del banco s1 ('raw', comandi mossi a meta'): { hash alto, basso, fermi alto, basso }. */
    int addRawHashS5 (Lab& lab, const char* id)
    {
        return lab.add (id, [] (const pt::engine::ModelDef& d)
        {
            static const Buf raw = rawSignalS1 (48000.0);
            uint64_t f = 0;
            const uint64_t v = rawHashS1 (d, raw, &f);
            return std::vector<double> { (double) (uint32_t) (v >> 32), (double) (uint32_t) v, (double) (uint32_t) (f >> 32), (double) (uint32_t) f };
        });
    }
    uint64_t hashOfS5 (const Lab& lab, int h, size_t k = 0) { return ((uint64_t) lab.v (h, k) << 32) | (uint64_t) lab.v (h, k + 1); }

    /** Tappa 5, lavoro s1: overdrive, distorsioni, fuzz, boost, ottave e preampli analogici Behringer e Cuvave (Circuit.cpp, stadio
        nuovo octdiv). Metodi del banco s1 (comandi per etichetta o per indice prima di prepare, blocchi da 512). */
    void runStage5DriveTests (const std::function<void (const juce::String&, bool, const juce::String&)>& report)
    {
        using namespace pt::engine;
        constexpr int numDrive = countOf (kStage5Drive);
        Lab lab;
        auto fr = [&lab] (const char* id, Sets sets, std::vector<double> freqs, double amp = 1.0e-4)
        {
            return lab.add (id, [sets, freqs, amp] (const ModelDef& d) { return smallSignalDb (d, sets, freqs, amp); });
        };
        auto frI = [&lab] (const char* id, ISets sets, std::vector<double> freqs, double amp = 1.0e-4)
        {
            return lab.add (id, [sets, freqs, amp] (const ModelDef& d) { auto fx = makeIdx (d, sets); return smallSignalOf (*fx, freqs, amp, 48000.0); });
        };
        auto frDef = [&lab] (const ModelDef& def, std::vector<double> freqs, double amp = 1.0e-4)
        {
            return lab.add (def, [freqs, amp] (const ModelDef& d) { return smallSignalDb (d, {}, freqs, amp); });
        };
        auto riffSets = [&lab] (const char* id, Sets sets)
        {
            return lab.add (id, [sets] (const ModelDef& d) { return riffLevelsS5 (d, [&] (Effect& fx) { setsS1 (fx, d, sets); }); });
        };
        auto harm = [&lab] (const char* id, Sets sets, std::vector<double> hs)
        {
            return lab.add (id, [sets, hs] (const ModelDef& d) { auto fx = makeS1 (d, sets); return harmonicsOf (*fx, 220.0, -20.0, hs); });
        };
        auto rel = [&lab] (int h, size_t k, size_t ref) { return lab.v (h, k) - lab.v (h, ref); };

        // 1. pedali esistenti invariati bit per bit dopo le modifiche della tappa 5 a Circuit.cpp (stadio octdiv): banco s1 'raw',
        //    impronte misurate con GCC su Linux x86-64 (il banco ha confrontato tutte le 249 netlist Circuit/AmpSim del catalogo con il
        //    motore di d691877). 'fixed' = prima meta' a comandi fermi, 'hash' = segnale intero con i comandi mossi (come la 1.2.7)
        {
            struct Ref { const char* id; uint64_t fixed, hash; };
            static const Ref refs[] = {
                { "ed9", 0x5a9b673524c55b7eull, 0xa0db19c13960768dull }, { "ds1", 0x7edafc3b1e613077ull, 0xae0a3f3ff8388e33ull },
                { "sd1", 0x4c903f272789401dull, 0x83cedf5bfcc97beeull }, { "bd2", 0x2346b27b6ea0948full, 0xdd87015347914ccfull },
                { "mt2", 0xc92c8b58fe1dc55full, 0xa4cfa044ec26ddb8ull }, { "hm2", 0xb9650518c5ae4865ull, 0x7ca0e7af09f6df15ull },
                { "ib9ts9", 0xa7f3db737d223227ull, 0x478323d26b3318d6ull }, { "ibtsnts", 0x0620374a8f5b2549ull, 0x437fa69890739c33ull },
                { "bmramshead", 0x819df799488b6b5full, 0xa3b0c7312a14f40dull }, { "ibtstk999", 0xde322f2c9cb483dfull, 0x1cdc2b4b58b35b51ull },
                { "os2", 0xd44b6e3849605cf3ull, 0x58ca2ec8202c17d9ull }, { "ehsoulfood", 0x0f246c0abef56290ull, 0x3d9a40bee796638full } };
            std::vector<int> h;
            for (const auto& r : refs) { h.push_back (addRawHashS5 (lab, r.id)); lab.first (h.back()); }
            lab.check (h, "Tappa 5: ED-9, DS-1, SD-1, BD-2, MT-2, HM-2, TS9, NTS, Ram's Head, TK999, OS-2, Soul Food invariati bit per bit (Circuit.cpp)", [&lab, h] {
                if (! kBitExactRefs) return Verdict { true, "impronte di riferimento misurate con GCC su Linux: confronto non applicabile a questo compilatore" };
                juce::String bad;
                for (size_t k = 0; k < h.size(); ++k)
                {
                    const uint64_t v = hashOfS5 (lab, h[k], 0), f = hashOfS5 (lab, h[k], 2);
                    if (f != refs[k].fixed) bad << refs[k].id << " DIVERSO a comandi fermi (" << juce::String::toHexString ((juce::int64) f) << ") ";
                    if (v != refs[k].hash) bad << refs[k].id << " DIVERSO (" << juce::String::toHexString ((juce::int64) v) << ") ";
                }
                return Verdict { bad.isEmpty(), bad.isEmpty() ? juce::String ((int) h.size()) + "/" + juce::String ((int) h.size()) + " hash identici (comandi fermi e mossi)" : bad.trim() };
            });
        }

        // 2. stadio nuovo octdiv (divisore d'ottava a flip-flop dell'OC-2): netlist di prova
        {
            auto sine = [&lab] (const char* cfg)
            {
                return lab.add (circuitDef (cfg), [] (const ModelDef& d) { auto fx = makeS1 (d, {}); return harmonicsOf (*fx, 220.0, -20.0, { 1.0, 0.5 }); });
            };
            const int a = sine ("octdiv o1=1 o2=0 hys=0.2 thr=0.01"), e = sine ("octdiv o1=0 o2=1 hys=0.2 thr=0.01");
            const int r = lab.add (circuitDef ("octdiv o1=1 o2=0 hys=0.2 thr=0.01"), [] (const ModelDef& d) { return riffLevelsS5 (d, {}); });
            const int c = frDef (circuitDef ("octdiv o1=1 o2=0 hys=0.2 thr=0.01"), { 1000.0 });
            const int g = frDef (circuitDef ("octdiv o1=1 o2=0 hys=0.2 thr=0.01 gate=0"), { 1000.0 });
            lab.check ({ a, e, r, c, g }, "Circuito: octdiv, seno 220 Hz -20 dBFS: fondamentale < -80 dBFS (prima e seconda ottava, resta f/2), RMS del riff -28.70 dBFS "
                                          "(+-0.3, energia conservata), porta chiusa a 1e-4 V (< -100 dB), senza porta 0.00 dB (+-0.05)", [&lab, a, e, r, c, g] {
                const double h1 = dbOf (lab.v (a, 0)), h2 = dbOf (lab.v (e, 0)), sub = dbOf (lab.v (a, 1));
                const bool ok = h1 < -80.0 && h2 < -80.0 && within (lab.v (r, 0), -28.70, 0.3) && lab.v (c) < -100.0 && within (lab.v (g), 0.0, 0.05);
                return Verdict { ok, "H1 " + num (h1, 1) + " / " + num (h2, 1) + " dBFS (f/2 " + num (sub, 1) + " dBFS), riff " + num (lab.v (r, 0)) + " dBFS, porta "
                                         + num (lab.v (c), 1) + " dB, senza porta " + num (lab.v (g), 3) + " dB" };
            });
        }

        // 3. livelli dei 42 modelli: riff a -12 dBFS con i comandi di default (finiti, picco < 0 dBFS, RMS -36..-11 dBFS), comandi
        //    al massimo e al minimo (picco < +18 dBFS); costo: i tre modelli piu' lenti, rimisurati da soli, sopra 3 x tempo reale
        {
            const auto hDef = addLevelsS5 (lab, kStage5Drive, numDrive, "Tappa 5 drive");
            checkLevelsS5 (lab, hDef, kStage5Drive, numDrive, "Tappa 5 drive: livelli con riff a -12 dBFS (42 modelli: finiti, picco < 0 dBFS, RMS -36..-11 dBFS)",
                           -36.0, -11.0, 0.0);
            lab.check (hDef, "Tappa 5 drive: costo, i 3 modelli piu' lenti (riff mono 3 s, rimisurati da soli) sopra 3 x tempo reale", [&lab, hDef] {
                std::vector<std::pair<double, int>> speed;
                for (int m = 0; m < numDrive; ++m) speed.push_back ({ lab.v (hDef[(size_t) m], 3), m });
                std::sort (speed.begin(), speed.end());
                static const Buf riff = guitarS1 (true, 48000.0, 3.0, -12.0);
                bool ok = true;
                juce::String detail;
                for (int k = 0; k < 3; ++k)
                {
                    const auto* d = findModel (kStage5Drive[speed[(size_t) k].second]);
                    auto fx = makeS1 (*d, {});
                    Buf y = riff;
                    const auto t0 = std::chrono::steady_clock::now();
                    processMono (*fx, y, 512);
                    const double x = 3.0 / std::max (1.0e-6, std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count());
                    ok = ok && x > 3.0;
                    detail << d->id << " " << num (x, 1) << "x ";
                }
                return Verdict { ok, detail.trim() };
            });
        }

        // 4. cloni uguali al modello di partenza (comandi rimappati nell'ordine del pedale reale): 3 regolazioni casuali (mt19937, seme 5)
        //    del modello di partenza e le stesse posizioni sui comandi del clone; risposta a 100 Hz - 8 kHz, RMS e picco del riff entro
        //    0.05 dB. L'impedenza d'ingresso dichiarata dal clone (primo stadio "hpf R=500k C=47n" invece di 1M) e' messa anche sul modello
        //    di partenza: e' una differenza voluta (passa-alto a 6.8 Hz invece di 3.4 Hz) e da sola sposta di 2-5 gradi la fase delle note
        //    gravi del riff, abbastanza perche' un raddrizzatore d'ottava (OVERTONE dell'FD300 / DF-2) cambi il picco del riff di 0.07 dB
        {
            /** Modello di partenza col primo stadio "hpf" del clone (stringhe valide fino alla fine del programma). */
            static std::deque<std::string> zinPool;
            auto withZin = [] (const ModelDef& base, const ModelDef& clone)
            {
                const std::string b (base.config), c (clone.config);
                const auto eb = b.find (';'), ec = c.find (';');
                ModelDef out = base;
                if (eb != std::string::npos && ec != std::string::npos && b.rfind ("hpf R=", 0) == 0 && c.rfind ("hpf R=", 0) == 0 && b.substr (0, eb) != c.substr (0, ec))
                {
                    zinPool.push_back (c.substr (0, ec) + b.substr (eb));
                    out.config = zinPool.back().c_str();
                }
                return out;
            };
            struct Clone { const char* id; const char* base; std::vector<int> map; int permIdx; std::vector<int> perm; };
            static const Clone clones[] = {
                { "bhvt999", "ibtstk999", { 0, 4, 2, 1, 3, 5 }, -1, {} }, { "bhod300", "os2", { 0, 1, 3, 2 }, -1, {} },
                { "bhod400", "od3", { 0, 2, 1 }, -1, {} }, { "bhbo300", "bd2", { 0, 1, 2 }, -1, {} }, { "bhpo300", "pw2", { 0, 1, 2, 3 }, -1, {} },
                { "bhud300", "ds2", { 1, 2, 3, 0 }, -1, {} }, { "bhum300", "mt2", { 3, 4, 1, 2, 5, 0 }, -1, {} },
                { "bhhm300", "hm2", { 0, 1, 2, 3 }, -1, {} }, { "bhhd300", "md2", { 0, 2, 1, 4, 3 }, -1, {} }, { "bhsm400", "hm3", { 0, 1, 2, 3 }, -1, {} },
                { "bhxd300", "xt2", { 0, 1, 2, 3 }, -1, {} }, { "bhsf300", "fz2", { 0, 2, 1, 3, 4 }, 4, { 2, 0, 1 } },
                { "bhuz400", "fz3", { 0, 2, 1 }, -1, {} }, { "bhfd300", "df2", { 0, 1, 2, 3 }, -1, {} }, { "bhvd1", "bmnycreiss", { 0, 1, 2 }, -1, {} },
                { "bhbod400", "odb3", { 0, 1, 2, 4, 3 }, -1, {} }, { "cvoverdrivets", "ed9", { 2, 1, 0 }, -1, {} },
                { "cvdistsc1", "ds1", { 1, 0, 2 }, -1, {} }, { "cvfuzz", "fz3", { 0, 1, 2 }, -1, {} }, { "bhzentaramini", "bhzentara", { 0, 1, 2 }, -1, {} } };
            std::mt19937 rng (5);
            std::uniform_real_distribution<double> U (0.0, 1.0);
            std::vector<int> h;
            std::vector<juce::String> what;
            static const std::vector<double> f6 { 100.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0 };
            auto measure = [] (const ModelDef& d, const ISets& s)
            {
                auto fx = makeIdx (d, s);
                auto v = smallSignalOf (*fx, f6, 1.0e-4, 48000.0);
                const auto r = riffLevelsS5 (d, [&s] (Effect& e) { for (const auto& [k, val] : s) e.params[k].store (val); });
                v.push_back (r[0]);
                v.push_back (r[1]);
                return v;
            };
            for (const auto& c : clones)
            {
                const auto* base = findModel (c.base);
                const auto* clone = findModel (c.id);
                for (int trial = 0; trial < 3; ++trial)
                {
                    ISets sb, sc;
                    for (int i = 0; i < (int) c.map.size(); ++i)
                    {
                        const int st = base != nullptr && i < base->numControls ? std::max (1, (int) base->controls[i].steps) : 1;
                        const double u = U (rng);
                        float v = st > 1 ? (float) std::min (st - 1, (int) (u * st)) / (float) (st - 1) : (float) (std::round (u * 1000.0) / 1000.0);
                        sb.push_back ({ i, v });
                        const int j = c.map[(size_t) i];
                        if (j == c.permIdx && clone != nullptr)
                        {
                            const int sn = std::max (2, (int) clone->controls[j].steps);
                            v = (float) c.perm[(size_t) std::lround (v * (sn - 1))] / (float) (sn - 1);
                        }
                        sc.push_back ({ j, v });
                    }
                    if (base != nullptr && clone != nullptr) h.push_back (lab.add (withZin (*base, *clone), [measure, sb] (const ModelDef& d) { return measure (d, sb); }));
                    else h.push_back (lab.add (c.base, [measure, sb] (const ModelDef& d) { return measure (d, sb); }));
                    h.push_back (lab.add (c.id, [measure, sc] (const ModelDef& d) { return measure (d, sc); }));
                    what.push_back (juce::String (c.id) + "<-" + c.base + "#" + juce::String (trial));
                }
            }
            lab.check (h, "Tappa 5: 20 cloni (VT999, OD300, OD400, BO300, PO300, UD300, UM300, HM300, HD300, SM400, XD300, SF300, UZ400, FD300, VD1, BOD400, "
                          "Overdrive-TS, Distortion-SC1, Fuzz, Zentara Mini) uguali al modello di partenza con le stesse regolazioni e la stessa impedenza d'ingresso (100 Hz - 8 kHz, riff; entro 0.05 dB)",
                       [&lab, h, what] {
                static const char* const qn[8] = { "100 Hz", "500 Hz", "1 kHz", "2 kHz", "4 kHz", "8 kHz", "RMS del riff", "picco del riff" };
                double worst = 0;
                juce::String bad, worstWhat;
                for (size_t k = 0; k + 1 < h.size(); k += 2)
                {
                    double w = 0;
                    size_t wq = 0;
                    for (size_t q = 0; q < 8; ++q)
                        if (std::abs (lab.v (h[k], q) - lab.v (h[k + 1], q)) > w) { w = std::abs (lab.v (h[k], q) - lab.v (h[k + 1], q)); wq = q; }
                    if (w > worst) { worst = w; worstWhat = what[k / 2] + ", " + qn[wq]; }
                    if (w >= 0.05) bad << what[k / 2] << " " << qn[wq] << " " << num (w, 3) << " dB ";
                }
                return Verdict { bad.isEmpty(), "scarto max " + num (worst, 3) + " dB (" + worstWhat + ") " + bad.trim() };
            });
        }

        // 5. GDI21 contro lo schema di fabbrica rev. D: valori dell'analisi nodale (tools/art/mna.py, op-amp con GBW 3 MHz, gdicheck.py
        //    del banco) a 50 Hz - 8 kHz per 12 regolazioni (DRIVE, TREBLE, BASS, LEVEL, AMP, MODE, MIC), risposta a 1e-5 V entro 0.5 dB
        {
            struct G { float k[4]; int amp, mode, mic; double db[6]; };
            static const G gs[] = {
                { { 0.4f, 0.5f, 0.5f, 0.5f }, 0, 0, 0, { 1.92, 0.53, -7.83, -15.64, -6.57, -27.39 } },
                { { 0.4f, 0.5f, 0.5f, 0.5f }, 0, 1, 0, { 10.38, 10.32, 2.41, -5.82, 1.03, -22.52 } },
                { { 0.4f, 0.5f, 0.5f, 0.5f }, 0, 2, 0, { 10.38, 10.33, 2.47, -5.30, 4.04, -15.22 } },
                { { 0.4f, 0.5f, 0.5f, 0.5f }, 1, 0, 1, { -0.24, 8.09, 13.42, 11.96, 15.39, -5.60 } },
                { { 0.4f, 0.5f, 0.5f, 0.5f }, 1, 1, 1, { 9.05, 18.55, 23.50, 20.85, 21.96, -0.95 } },
                { { 0.4f, 0.5f, 0.5f, 0.5f }, 1, 2, 1, { 9.05, 18.53, 23.46, 21.39, 25.54, 6.78 } },
                { { 0.4f, 0.5f, 0.5f, 0.5f }, 2, 0, 2, { 20.87, 33.46, 41.51, 36.10, 31.06, 5.35 } },
                { { 0.4f, 0.5f, 0.5f, 0.5f }, 2, 1, 2, { 30.18, 43.95, 51.77, 45.53, 39.07, 11.05 } },
                { { 0.4f, 0.5f, 0.5f, 0.5f }, 2, 2, 2, { 30.18, 43.94, 51.75, 46.15, 42.15, 17.76 } },
                { { 1.0f, 0.0f, 1.0f, 1.0f }, 2, 2, 1, { 53.60, 67.27, 69.67, 55.49, 46.40, 15.38 } },
                { { 0.0f, 1.0f, 0.0f, 0.2f }, 1, 0, 2, { -46.28, -34.14, -21.09, -14.98, -7.43, -20.34 } },
                { { 0.7f, 0.8f, 0.3f, 0.5f }, 0, 1, 1, { 16.57, 17.50, 9.28, -3.05, 11.54, -7.54 } } };
            static const std::vector<double> f6 { 50.0, 100.0, 300.0, 1000.0, 3000.0, 8000.0 };
            std::vector<int> h, ht;
            for (const auto& g : gs)
            {
                const ISets s { { 0, g.k[0] }, { 1, g.k[1] }, { 2, g.k[2] }, { 3, g.k[3] }, { 4, 0.5f * (float) g.amp }, { 5, 0.5f * (float) g.mode }, { 6, 0.5f * (float) g.mic } };
                h.push_back (frI ("bhgdi21", s, f6, 1.0e-5));
                lab.first (h.back());
            }
            // TM300: stesso circuito con i comandi in altro ordine (indici 3, 2, 1, 0, 6, 5, 4) e ingresso 500k: confronto sopra 100 Hz
            static const std::vector<double> t6 { 100.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0 };
            for (int c : { 0, 11 })
            {
                const auto& g = gs[c];
                const float v[7] = { g.k[0], g.k[1], g.k[2], g.k[3], 0.5f * (float) g.amp, 0.5f * (float) g.mode, 0.5f * (float) g.mic };
                static const int order[7] = { 3, 2, 1, 0, 6, 5, 4 };
                ISets sg, st;
                for (int k = 0; k < 7; ++k) { sg.push_back ({ k, v[k] }); st.push_back ({ order[k], v[k] }); }
                ht.push_back (frI ("bhgdi21", sg, t6, 1.0e-5));
                ht.push_back (frI ("bhtm300", st, t6, 1.0e-5));
            }
            lab.check (h, "GDI21: risposta a piccolo segnale uguale allo schema rev. D (analisi nodale, 12 regolazioni, 50 Hz - 8 kHz, entro 0.5 dB); "
                          "guadagni a 1 kHz AMP x MODE TWEED -15.6 / -5.7 / -5.2, BRITISH +12.0 / +20.9 / +21.5, CALIF +36.2 / +45.6 / +46.2 dB (+-0.5); "
                          "TWEED CLEAN 100 Hz - 1 kHz +16.2 dB (+-1)", [&lab, h] {
                double worst = 0;
                juce::String bad, gains;
                for (size_t c = 0; c < h.size(); ++c)
                    for (size_t k = 0; k < 6; ++k)
                    {
                        const double d = std::abs (lab.v (h[c], k) - gs[c].db[k]);
                        worst = std::max (worst, d);
                        if (d > 0.5) bad << "#" << (int) c << "@" << juce::roundToInt (f6[k]) << " " << num (lab.v (h[c], k)) << " (" << num (gs[c].db[k]) << ") ";
                    }
                static const double table[9] = { -15.6, -5.7, -5.2, 12.0, 20.9, 21.5, 36.2, 45.6, 46.2 };
                bool tab = true;
                for (size_t c = 0; c < 9; ++c)
                {
                    tab = tab && within (lab.v (h[c], 3), table[c], 0.5);
                    gains << num (lab.v (h[c], 3), 1) << (c % 3 == 2 ? "; " : " / ");
                }
                const double cr = lab.v (h[0], 1) - lab.v (h[0], 3);
                return Verdict { bad.isEmpty() && tab && within (cr, 16.2, 1.0), "scarto max " + num (worst, 2) + " dB; 1 kHz " + gains + "TWEED 100 Hz - 1 kHz "
                                                                                    + num (cr, 1) + " dB " + bad.trim() };
            });
            lab.check (ht, "TM300 = GDI21 (comandi corrispondenti, 100 Hz - 8 kHz, entro 0.1 dB; differisce solo l'ingresso 500k)", [&lab, ht] {
                double worst = 0;
                for (size_t k = 0; k + 1 < ht.size(); k += 2)
                    for (size_t q = 0; q < 6; ++q) worst = std::max (worst, std::abs (lab.v (ht[k], q) - lab.v (ht[k + 1], q)));
                return Verdict { worst < 0.1, "scarto max " + num (worst, 3) + " dB" };
            });
        }

        // 6. voce dei circuiti nuovi (risposta a 1e-4 V, riff a -12 dBFS, armoniche del seno 220 Hz a -20 dBFS)
        {
            const int sat = harm ("bhgdi21", {}, { 1.0, 2.0, 3.0 }), zen = harm ("bhzentara", {}, { 1.0, 3.0 });
            lab.check ({ sat, zen }, "GDI21 satura senza diodi (BRITISH HI GAIN): H2/H1 < -100 dB (TL074 simmetrico), H3/H1 -14 dB (+-4); Zentara (diodi al germanio) "
                                     "H3/H1 -14 dB (+-4)", [&lab, sat, zen] {
                const double h2 = dbOf (lab.v (sat, 1) / lab.v (sat, 0)), h3 = dbOf (lab.v (sat, 2) / lab.v (sat, 0)), z3 = dbOf (lab.v (zen, 1) / lab.v (zen, 0));
                return Verdict { h2 < -100.0 && within (h3, -14.0, 4.0) && within (z3, -14.0, 4.0),
                                 "GDI21 H2/H1 " + num (h2, 1) + " dB, H3/H1 " + num (h3, 1) + " dB; Zentara H3/H1 " + num (z3, 1) + " dB" };
            });
        }
        {
            const Sets flat { { "BLEND", 0.0f }, { "LEVEL", 1.0f }, { "TREBLE", 0.375f }, { "BASS", 0.375f } };
            const int a = fr ("bhbdi21", flat, { 62.0, 125.0, 250.0, 500.0, 1000.0, 1600.0 });
            const int b = fr ("bhbdi21", { { "BLEND", 1.0f }, { "PRESENCE", 0.0f }, { "DRIVE", 0.0f }, { "LEVEL", 1.0f }, { "TREBLE", 0.375f }, { "BASS", 0.375f } },
                              { 157.0, 793.0 });
            lab.check ({ a, b }, "BDI21: a ore 10:30 piatto da 62 Hz a 1.6 kHz (+6.4..+6.8 dB +-1, variazione < 0.6 dB); BLEND 1 scavo dei medi 157 Hz - 793 Hz +18.1 dB (+-2)",
                       [&lab, a, b] {
                double lo = 1e9, hi = -1e9;
                for (double v : lab.all (a)) { lo = std::min (lo, v); hi = std::max (hi, v); }
                const double sc = lab.v (b, 0) - lab.v (b, 1);
                return Verdict { lo >= 5.4 && hi <= 7.8 && hi - lo < 0.6 && within (sc, 18.1, 2.0),
                                 num (lo) + " .. " + num (hi) + " dB, scavo " + num (sc, 1) + " dB" };
            });
        }
        {
            const int z = fr ("bhzentara", { { "GAIN", 1.0f }, { "OUTPUT", 1.0f }, { "TREBLE", 0.5f } }, { 1000.0 });
            lab.check ({ z }, "Zentara: guadagno massimo a 1 kHz +40.9 dB (+-1)", [&lab, z] { return Verdict { within (lab.v (z), 40.9, 1.0), num (lab.v (z)) + " dB" }; });
        }
        {
            std::vector<int> h;
            for (float r : { 0.0f, 0.5f, 1.0f }) h.push_back (riffSets ("bhuo300", { { "DIRECT", 0.0f }, { "OCT 1", 1.0f }, { "OCT 2", 0.0f }, { "RANGE", r } }));
            const int ss = fr ("bhuo300", {}, { 1000.0 });
            lab.check ({ h[0], h[1], h[2], ss }, "UO300: RANGE HI / MID / LO, RMS del riff (solo OCT 1) -26.5 / -28.1 / -29.5 dBFS (+-1, decrescente); piccolo segnale (porta chiusa, "
                                                 "resta il DIRECT) -3.1 dB a 1 kHz (+-0.1)", [&lab, h, ss] {
                const double r[3] = { lab.v (h[0]), lab.v (h[1]), lab.v (h[2]) };
                const bool ok = within (r[0], -26.5, 1.0) && within (r[1], -28.1, 1.0) && within (r[2], -29.5, 1.0) && r[0] > r[1] && r[1] > r[2]
                             && within (lab.v (ss), -3.1, 0.1);
                return Verdict { ok, num (r[0], 1) + " / " + num (r[1], 1) + " / " + num (r[2], 1) + " dBFS, 1 kHz " + num (lab.v (ss)) + " dB" };
            });
        }
        {
            std::vector<int> h;
            for (float m : { 0.0f, 0.5f, 1.0f }) h.push_back (riffSets ("bhdm100", { { "MODE", m } }));
            lab.check (h, "DM100: modi DS1 / D+ / RAT pareggiati, RMS del riff -28.8 / -26.8 / -26.8 dBFS (+-1.5)", [&lab, h] {
                const double ref[] = { -28.8, -26.8, -26.8 };
                bool ok = true;
                for (size_t k = 0; k < 3; ++k) ok = ok && within (lab.v (h[k]), ref[k], 1.5);
                return Verdict { ok, num (lab.v (h[0]), 1) + " / " + num (lab.v (h[1]), 1) + " / " + num (lab.v (h[2]), 1) + " dBFS" };
            });
        }
        {
            const int rm = riffSets ("bhfuzzbender", { { "ORIG/MOD", 1.0f } }), ro = riffSets ("bhfuzzbender", { { "ORIG/MOD", 0.0f } });
            const int fm = fr ("bhfuzzbender", { { "ORIG/MOD", 1.0f } }, { 100.0, 1000.0 }), fo = fr ("bhfuzzbender", { { "ORIG/MOD", 0.0f } }, { 100.0, 1000.0 });
            lab.check ({ rm, ro, fm, fo }, "Fuzz Bender: MOD - ORIG sul riff +4.7 dB, 100 Hz - 1 kHz MOD -8.1 / ORIG +0.3 dB (+-1)", [&lab, rel, rm, ro, fm, fo] {
                const double d = lab.v (rm) - lab.v (ro), m = rel (fm, 0, 1), o = rel (fo, 0, 1);
                return Verdict { within (d, 4.7, 1.0) && within (m, -8.1, 1.0) && within (o, 0.3, 1.0), num (d) + " dB, MOD " + num (m, 1) + " / ORIG " + num (o, 1) + " dB" };
            });
        }
        {
            const std::vector<double> f3 { 500.0, 1000.0, 2000.0 };
            const int w0 = fr ("bhwd300", { { "GAIN", 0.0f }, { "WARP", 0.0f } }, f3), w1 = fr ("bhwd300", { { "GAIN", 0.0f }, { "WARP", 1.0f } }, f3);
            const int s0 = fr ("bhwd300", { { "SUB", 0.0f } }, { 100.0, 1000.0 }), s1 = fr ("bhwd300", { { "SUB", 1.0f } }, { 100.0, 1000.0 });
            lab.check ({ w0, w1, s0, s1 }, "WD300: WARP 0 / 1 (GAIN 0) 500 Hz - 1 kHz +2.3 / -9.3 dB, 2 kHz - 1 kHz -2.9 / +7.9 dB; SUB ON - OFF a 100 Hz - 1 kHz +8.3 dB (+-1)",
                       [&lab, rel, w0, w1, s0, s1] {
                const double a0 = rel (w0, 0, 1), b0 = rel (w0, 2, 1), a1 = rel (w1, 0, 1), b1 = rel (w1, 2, 1), sub = rel (s0, 0, 1) - rel (s1, 0, 1);
                return Verdict { within (a0, 2.3, 1.0) && within (b0, -2.9, 1.0) && within (a1, -9.3, 1.0) && within (b1, 7.9, 1.0) && within (sub, 8.3, 1.0),
                                 "WARP 0 " + num (a0, 1) + " / " + num (b0, 1) + ", WARP 1 " + num (a1, 1) + " / " + num (b1, 1) + " dB, SUB " + num (sub, 1) + " dB" };
            });
        }
        {
            const int o = harm ("bhoctavia", {}, { 1.0, 2.0 });
            std::vector<int> h;
            for (float m : { 0.0f, 0.5f, 1.0f }) h.push_back (riffSets ("bhoctavedivider", { { "MIX", m } }));
            auto all = h;
            all.push_back (o);
            lab.check (all, "Octavia: l'ottava domina (H2/H1 > +15 dB, seno 220 Hz -20 dBFS); Octave Divider MIX 0 / 0.5 / 1 RMS del riff -28.65 / -31.1 / -27.6 dBFS (+-1.5)",
                       [&lab, h, o] {
                const double h2 = dbOf (lab.v (o, 1) / lab.v (o, 0));
                const double ref[] = { -28.65, -31.1, -27.6 };
                bool ok = h2 > 15.0;
                for (size_t k = 0; k < 3; ++k) ok = ok && within (lab.v (h[k]), ref[k], 1.5);
                return Verdict { ok, "H2/H1 " + num (h2, 1) + " dB, MIX " + num (lab.v (h[0]), 2) + " / " + num (lab.v (h[1]), 1) + " / " + num (lab.v (h[2]), 1) + " dBFS" };
            });
        }
        {
            const std::vector<double> f5 { 100.0, 500.0, 1000.0, 2000.0, 4000.0 };
            const int t = fr ("bhto800", {}, f5), e = fr ("ed9", {}, f5), rt = riffSets ("bhto800", {}), re = riffSets ("ed9", {});
            lab.check ({ t, e, rt, re }, "TO800 (TS808 con 4558 reale, uscita TS9) contro ED-9: 100 Hz - 4 kHz e RMS del riff entro 0.2 dB", [&lab, t, e, rt, re] {
                double w = 0;
                for (size_t k = 0; k < 5; ++k) w = std::max (w, std::abs (lab.v (t, k) - lab.v (e, k)));
                const double dr = std::abs (lab.v (rt) - lab.v (re));
                return Verdict { w < 0.2 && dr < 0.2, "scarto max " + num (w, 3) + " dB, riff " + num (dr, 3) + " dB" };
            });
        }

        // click (metodo dei gruppi precedenti, soglia 10x senza eccezioni: misurati al massimo 3.4x, MIX dell'Octave Divider) e
        // determinismo fra due istanze nuove
        addClickAndDeterminism (lab, kStage5Drive, numDrive, "Tappa 5 drive", 10.0, false);
        lab.run (report);
    }

    /** Tappa 5, lavoro s2: compressori, gate, EQ, filtri, wah, synth, volume e utility (FxStage5Dyn.cpp e famiglie esistenti).
        Metodi del banco s2 (comandi per ruolo/etichetta prima di prepare). */
    void runStage5DynTests (const std::function<void (const juce::String&, bool, const juce::String&)>& report)
    {
        using namespace pt::engine;
        constexpr int numDyn = countOf (kStage5Dyn);
        Lab lab;
        auto makeS2 = [] (const ModelDef& d, const Sets& sets, int block = 512)
        {
            auto fx = createEffect (d);
            for (const auto& [k, v] : setsS2 (d, sets)) fx->params[k].store (v);
            fx->prepare (48000.0, block);
            return fx;
        };
        /** Risposta del banco s2 ('response'): effetto nuovo per ogni frequenza, 0.5 s di assestamento, blocchi da 256. */
        auto resp = [&lab, makeS2] (const char* id, Sets sets, std::vector<double> freqs, double amp = 0.01)
        {
            return lab.add (id, [makeS2, sets, freqs, amp] (const ModelDef& d)
            {
                std::vector<double> out;
                for (double f : freqs)
                {
                    auto fx = makeS2 (d, sets, 256);
                    const double sr = 48000.0;
                    const int settle = (int) (0.5 * sr), per = (int) std::ceil (0.25 * f) + 2, meas = (int) std::lround (per * sr / f);
                    Buf x ((size_t) (settle + meas));
                    for (size_t i = 0; i < x.size(); ++i) x[i] = (float) (amp * std::sin (2 * kPi * f * (double) i / sr));
                    processMono (*fx, x, 256);
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
            });
        };
        /** Picco principale a piccolo segnale (impulso di 1 mV, FFT): { Hz, dB }. */
        auto peak = [&lab] (const char* id, Sets sets)
        {
            return lab.add (id, [sets] (const ModelDef& d)
            {
                auto fx = createEffect (d);
                for (const auto& [k, v] : setsS2 (d, sets)) fx->params[k].store (v);
                const auto p = measureWahPeak (*fx, 48000.0);
                return std::vector<double> { p.f, p.db };
            });
        };
        /** Seno 1 kHz a livello fisso per 2.5 s: picco degli ultimi 200 ms (dBFS). */
        auto level = [&lab, makeS2] (const char* id, Sets sets, double inDb)
        {
            return lab.add (id, [makeS2, sets, inDb] (const ModelDef& d)
            {
                auto fx = makeS2 (d, sets);
                Buf x = sineBuf (48000.0, 1000.0, std::pow (10.0, inDb / 20.0), 2.5);
                processMono (*fx, x, 512);
                double pk = 0;
                for (size_t i = x.size() - 9600; i < x.size(); ++i) pk = std::max (pk, (double) std::abs (x[i]));
                return std::vector<double> { dbOf (pk) };
            });
        };
        /** Gradino -40 -> -10 dBFS (1 kHz): costante d'attacco = 1/e della sovraelongazione in dB, rilascio entro 1 dB (ms). */
        auto attack = [&lab, makeS2] (const char* id, Sets sets)
        {
            return lab.add (id, [makeS2, sets] (const ModelDef& d)
            {
                const double sr = 48000.0;
                auto fx = makeS2 (d, sets);
                const int n = (int) (3 * sr);
                Buf x ((size_t) n);
                for (int i = 0; i < n; ++i)
                    x[(size_t) i] = (float) (std::pow (10.0, (i >= sr && i < 2 * sr ? -10.0 : -40.0) / 20) * std::sin (2 * kPi * 1000 * i / sr));
                processMono (*fx, x, 512);
                auto env = [&x] (int i) { double m = 0; for (int k = 0; k < 48; ++k) m = std::max (m, (double) std::abs (x[(size_t) (i + k)])); return dbOf (m); };
                const double fin = env ((int) (2 * sr) - 960), finR = env (n - 960), ov = env ((int) sr) - fin;
                int t63 = (int) sr;
                while (t63 < 2 * sr - 48 && env (t63) - fin > ov / 2.718) t63 += 24;
                int tr = (int) (2 * sr);
                while (tr < n - 48 && std::abs (env (tr) - finR) > 1.0) tr += 48;
                return std::vector<double> { (t63 - sr) / sr * 1000.0, (tr - 2 * sr) / sr * 1000.0 };
            });
        };
        auto fq = [] (double f, double ref) { return std::abs (f / ref - 1.0) <= 0.06; };
        auto hz = [] (double f) { return juce::String (juce::roundToInt (f)); };

        // 1. pedali esistenti invariati bit per bit (famiglie riusate dai cloni: Compressor, NoiseGate, GraphicEQ, Wah, Volume, SlowGear,
        //    Acoustic, i4ross...): banco s1 'raw' con i comandi mossi a meta' (impronte del banco s2, motore di d691877). 'fixed' = prima
        //    meta' a comandi fermi, invariata; 'hash' = segnale intero, nuovo dalla 1.3.1 per i modelli delle famiglie vecchie con
        //    l'involucro senza click (CS-3, GE-7, AW-3, DB01, AW-2, GEB-7, LMB-3) e per l'AC-2 (modo ENH con le sue risonanze)
        {
            struct Ref { const char* id; uint64_t fixed, hash; };
            static const Ref refs[] = {
                { "cs3", 0x70e0f050fc09bf80ull, 0x99de31d19387f6d4ull }, { "ns2", 0x6a46559e7a871eacull, 0x6d2a220e1f2606cdull },
                { "gq7", 0xad6ff35c46abd26cull, 0x8c22ad1b7170c34full }, { "aw3", 0x30cad961679d4f46ull, 0x17239d4a5bb9bdb1ull },
                { "db01", 0x2cfbf616c023487aull, 0xdcfbc63d70c1a222ull }, { "cm95", 0xc29afcc68ef265afull, 0xddf79b2d624209b4ull },
                { "ib9cp9", 0x0262093f69e029c0ull, 0x3b98bc5f58d192dbull }, { "ibtlwd7", 0x3822843607280daeull, 0xe349826cf3b27e89ull },
                { "ib10wh10", 0x33f905797a39aae8ull, 0xc6330f5d6759a483ull }, { "fv500h", 0x6c802cf5c813b57cull, 0x37efb2ce81bff398ull },
                { "aw2", 0x496855df098fda0cull, 0xe7a3f5e93b8dc66full }, { "geb7", 0xad6ff35c46abd26cull, 0x76d2d5726134eddcull },
                { "lmb3", 0xbf2bac6783cabccbull, 0x35dc63df4e390eddull }, { "sg1", 0x66d346e0ce71a543ull, 0x444d626e621cc584ull },
                { "sp1", 0xade09fee1bcc3dfdull, 0x05bcc99392f77b43ull }, { "ac2", 0x1cc0f54bfc9e31ccull, 0xc1c6eebe8c108790ull },
                { "ehbassball", 0x587ed74c4464352aull, 0x9e71d3ad24f12914ull }, { "ib7cp835", 0x56257d3d483ed850ull, 0x20624c04cbadc54eull },
                { "ibtstc999", 0x1fa09c2212162f3bull, 0x9fa88979c0c410afull }, { "ib10ls10", 0xad6ff35c46abd26cull, 0xc663814a38597cabull } };
            std::vector<int> h;
            for (const auto& r : refs) h.push_back (addRawHashS5 (lab, r.id));
            lab.check (h, "Tappa 5: 20 pedali delle famiglie riusate (CS-3, NS-2, GE-7, AW-3, DB01, CM95, CP9, WD7, WH10, FV-500H, AW-2, GEB-7, LMB-3, SG-1, SP-1, AC-2, "
                          "Bass Ball, CP-835, TC999, LS10) invariati bit per bit", [&lab, h] {
                if (! kBitExactRefs) return Verdict { true, "impronte di riferimento misurate con GCC su Linux: confronto non applicabile a questo compilatore" };
                juce::String bad;
                for (size_t k = 0; k < h.size(); ++k)
                {
                    if (hashOfS5 (lab, h[k], 2) != refs[k].fixed)
                        bad << refs[k].id << " DIVERSO a comandi fermi (" << juce::String::toHexString ((juce::int64) hashOfS5 (lab, h[k], 2)) << ") ";
                    if (hashOfS5 (lab, h[k]) != refs[k].hash) bad << refs[k].id << " DIVERSO (" << juce::String::toHexString ((juce::int64) hashOfS5 (lab, h[k])) << ") ";
                }
                return Verdict { bad.isEmpty(), bad.isEmpty() ? juce::String ((int) h.size()) + "/" + juce::String ((int) h.size()) + " hash identici (comandi fermi e mossi)" : bad.trim() };
            });
        }

        // 2. livelli dei 29 modelli col riff a -12 dBFS (entro 1.5 dB dal banco, compressori 2 dB; picco -16.2..-7.75 dBFS, BLE400 -22.4),
        //    comandi al massimo / al minimo; robustezza: riff + accordo a -6 / -3 dBFS con comandi casuali (seme 7, ogni 1024 campioni),
        //    poi stereo a blocchi irregolari
        {
            struct Ref { const char* id; double pk, rms; };
            static const Ref refs[] = {
                { "bhcs400", -15.30, -30.56 }, { "bhble400", -22.40, -37.05 }, { "bhcl9", -9.82, -25.83 }, { "bhdc9", -9.84, -23.05 },
                { "cvcompressor", -9.82, -25.83 }, { "bhnr300", -12.00, -28.72 }, { "bhig9", -12.00, -28.72 }, { "cvnoisegate", -12.00, -28.72 },
                { "bheq700", -12.00, -28.72 }, { "bhbeq700", -12.00, -28.72 }, { "bhse200", -10.24, -26.41 }, { "bham300", -16.18, -33.34 },
                { "bhsm200", -12.00, -29.29 }, { "bhuw300", -7.75, -23.64 }, { "bhvb1", -10.80, -25.78 }, { "bhbtron3", -16.18, -27.49 },
                { "bhmeatball", -14.71, -30.88 }, { "bhbm11m", -13.66, -27.26 }, { "bhbm12", -10.33, -28.94 }, { "bhringstinger", -8.64, -28.29 },
                { "bhbm15m", -13.69, -29.30 }, { "bhbm17", -13.89, -30.20 }, { "bhwahwah", -12.60, -27.02 }, { "bhhb01", -14.27, -27.81 },
                { "bhparapedal", -8.09, -23.21 }, { "bhfc600", -12.00, -28.72 }, { "bhfcv100", -12.00, -28.72 }, { "bhab100", -11.93, -28.65 },
                { "bhab200", -12.00, -28.72 } };
            static_assert (sizeof (refs) / sizeof (refs[0]) == (size_t) numDyn, "un valore per ogni modello s2");
            const auto hl = addLevelsS5 (lab, kStage5Dyn, numDyn, "Tappa 5 dinamica/EQ/filtri/synth");
            lab.check (hl, "Tappa 5 dinamica/EQ/filtri/synth: livelli con riff a -12 dBFS (29 modelli: finiti, entro 1.5 dB dal banco, compressori 2 dB; picco "
                           "-16.2..-7.75 dBFS, BLE400 -22.4)", [&lab, hl] {
                int good = 0;
                double worst = 0;
                juce::String bad;
                for (int m = 0; m < numDyn; ++m)
                {
                    const double rms = lab.v (hl[(size_t) m], 0), pk = lab.v (hl[(size_t) m], 1);
                    const auto* d = findModel (refs[m].id);
                    const double tol = d != nullptr && d->family == Family::Compressor ? 2.0 : 1.5;
                    const double dev = std::max (std::abs (pk - refs[m].pk), std::abs (rms - refs[m].rms));
                    worst = std::max (worst, dev);
                    if (lab.v (hl[(size_t) m], 2) > 0.5 && dev <= tol) ++good; else bad << refs[m].id << " (picco " << num (pk, 2) << ", RMS " << num (rms, 2) << ") ";
                }
                return Verdict { good == numDyn, juce::String (good) + "/29, scarto max dal banco " + num (worst) + " dB " + bad.trim() };
            });
            static const Buf riff = guitarS1 (true, 48000.0, 3.0, -12.0);
            static const Buf stress = []
            {
                Buf x = guitarS1 (true, 48000.0, 3.0, -6.0);
                const Buf ch = guitarS1 (false, 48000.0, 2.0, -3.0);
                x.insert (x.end(), ch.begin(), ch.end());
                x.resize ((size_t) (3.0 * 48000.0));
                return x;
            }();
            std::vector<int> hs;
            for (int m = 0; m < numDyn; ++m)
                hs.push_back (lab.add (kStage5Dyn[m], [makeS2] (const ModelDef& d)
                {
                    std::mt19937 rng (7);
                    std::uniform_real_distribution<float> u (0, 1);
                    auto fx = makeS2 (d, {}, 1024);
                    Buf y = stress;
                    double pk = 0;
                    bool fin = true;
                    for (size_t i = 0; i + 1024 <= y.size(); i += 1024)
                    {
                        for (int c = 0; c < d.numControls; ++c) if (u (rng) < 0.3f) fx->params[c].store (u (rng));
                        float* p[1] = { y.data() + i };
                        fx->process (p, 1, 1024);
                        for (int k = 0; k < 1024; ++k) { fin = fin && std::isfinite (y[i + (size_t) k]); pk = std::max (pk, (double) std::abs (y[i + (size_t) k])); }
                    }
                    auto fx2 = makeS2 (d, {}, 512);
                    Buf l = riff, rr = riff;
                    for (size_t i = 0; i < l.size();)
                    {
                        const int m = (int) std::min<size_t> ((size_t) (1 + (int) (u (rng) * 511)), l.size() - i);
                        float* p[2] = { l.data() + i, rr.data() + i };
                        fx2->process (p, 2, m);
                        for (int k = 0; k < m; ++k) fin = fin && std::isfinite (l[i + (size_t) k]) && std::isfinite (rr[i + (size_t) k]);
                        i += (size_t) m;
                    }
                    return std::vector<double> { dbOf (pk), fin ? 1.0 : 0.0 };
                }));
            lab.check (hs, "Tappa 5 dinamica/EQ/filtri/synth: robustezza (riff + accordo a -6/-3 dBFS, comandi casuali ogni 1024 campioni; stereo a blocchi "
                           "irregolari): finiti, picco < +12 dBFS", [&lab, hs] {
                double worst = -200;
                juce::String bad, worstId;
                for (int m = 0; m < numDyn; ++m)
                {
                    const bool fin = lab.v (hs[(size_t) m], 1) > 0.5;
                    const double pk = fin ? lab.v (hs[(size_t) m], 0) : 999.0;
                    if (pk > worst) { worst = pk; worstId = kStage5Dyn[m]; }
                    if (! fin || pk >= 12.0) bad << kStage5Dyn[m] << (fin ? "" : " NaN") << " ";
                }
                return Verdict { bad.isEmpty(), "picco max " + num (worst, 1) + " dBFS (" + worstId + ") " + bad.trim() };
            });
            const int fcv = resp ("bhfcv100", { { "PEDAL", 0.5f } }, { 1000.0 });
            lab.check ({ fcv }, "FCV100: VCA in dB lineari, a meta' corsa -35 dB (+-0.5)", [&lab, fcv] { return Verdict { within (lab.v (fcv), -35.0, 0.5), num (lab.v (fcv)) + " dB" }; });
        }

        // 3. compressori (seno 1 kHz, 2.5 s per livello, picco degli ultimi 200 ms; attacco e rilascio col gradino -40 -> -10 dBFS)
        {
            const int d30 = level ("bhdc9", {}, -30), d0 = level ("bhdc9", {}, 0), dat = attack ("bhdc9", {});
            lab.check ({ d30, d0, dat }, "DC9 (Dyna Comp): -30 / 0 dBFS -> -16.5 / -15.7 dBFS (+-1.5), attacco 5 ms (+-1), rilascio 0.8 s (+-20%)", [&lab, d30, d0, dat] {
                const bool ok = within (lab.v (d30), -16.5, 1.5) && within (lab.v (d0), -15.7, 1.5) && within (lab.v (dat, 0), 5.0, 1.0) && within (lab.v (dat, 1), 804.0, 160.0);
                return Verdict { ok, num (lab.v (d30), 2) + " / " + num (lab.v (d0), 2) + " dBFS, attacco " + num (lab.v (dat, 0), 1) + " ms, rilascio " + num (lab.v (dat, 1), 0) + " ms" };
            });
            std::vector<int> h;
            for (const char* id : { "bhcl9", "cvcompressor" })
            {
                h.push_back (level (id, {}, -30));
                h.push_back (level (id, {}, 0));
                h.push_back (attack (id, {}));
            }
            lab.check (h, "CL9 e Compressor Cuvave (Ross): -30 / 0 dBFS -> -21.75 / -18.12 dBFS (+-1.5), attacco 14 ms (+-15% + 0.5 ms), rilascio 0.74 s (+-20%)", [&lab, h] {
                bool ok = true;
                juce::String detail;
                for (size_t k = 0; k < h.size(); k += 3)
                {
                    const double a = lab.v (h[k + 2], 0), r = lab.v (h[k + 2], 1);
                    ok = ok && within (lab.v (h[k]), -21.75, 1.5) && within (lab.v (h[k + 1]), -18.12, 1.5) && std::abs (a - 14.0) <= 0.15 * 14.0 + 0.5
                         && within (r, 744.0, 0.2 * 744.0);
                    detail << num (lab.v (h[k]), 2) << " / " << num (lab.v (h[k + 1]), 2) << " dBFS, " << num (a, 1) << " / " << num (r, 0) << " ms; ";
                }
                return Verdict { ok, detail.trimCharactersAtEnd ("; ") };
            });
        }

        // 4. gate NR300 (soglia di default -56 dBFS con isteresi 6 dB, attenuazione -90 dB): chiuso a -70 dBFS, aperto a -50 dBFS, MUTE
        {
            const int c = level ("bhnr300", {}, -70), o = level ("bhnr300", {}, -50), mu = level ("bhnr300", { { "MODE", 0.0f } }, -20);
            lab.check ({ c, o, mu }, "NR300: gate chiuso a -70 dBFS (uscita < -120 dBFS), aperto a -50 dBFS (-50 +-0.5), MUTE muto a -20 dBFS (< -120 dBFS)", [&lab, c, o, mu] {
                return Verdict { lab.v (c) < -120.0 && within (lab.v (o), -50.0, 0.5) && lab.v (mu) < -120.0,
                                 num (lab.v (c), 1) + " / " + num (lab.v (o), 2) + " / MUTE " + num (lab.v (mu), 1) + " dBFS" };
            });
        }

        // 5. filtri d'inviluppo e synth
        {
            const int bt = peak ("bhbtron3", {}), mb = peak ("bhmeatball", { { "BANDWIDTH", 0.0f }, { "RANGE", 1.0f }, { "INTENS.", 0.0f }, { "MODE", 0.5f } });
            lab.check ({ bt, mb }, "B-Tron III a riposo: picco passa-banda a 210 Hz (+-6%); Meatball statico (BANDWIDTH OFF, RANGE 6): 1280 Hz (+-6%)", [&lab, fq, hz, bt, mb] {
                return Verdict { fq (lab.v (bt), 210.0) && fq (lab.v (mb), 1280.0), "B-Tron " + hz (lab.v (bt)) + " Hz (" + num (lab.v (bt, 1), 1) + " dB), Meatball "
                                                                                        + hz (lab.v (mb)) + " Hz" };
            });
        }
        {
            // BM-12: col silenzio all'ingresso esce la sola portante (feed): frequenza dagli attraversamenti dello zero; LO al minimo
            // (0.6 Hz) dalla modulazione dell'inviluppo di un seno a 1 kHz (|cos|: due volte la portante)
            auto carrier = [&lab] (float lo, float freq)
            {
                return lab.add ("bhbm12", [lo, freq] (const ModelDef& d)
                {
                    const double sr = 48000.0;
                    auto fx = makeAfter (d, {});
                    setsS1 (*fx, d, { { "MODULATOR", lo }, { "FREQUENCY", freq }, { "AMOUNT", 0.0f } });
                    Buf l ((size_t) (2.5 * sr), 0.0f), r = l;
                    runStereo256 (*fx, l, r, {}, 1);
                    int first = -1, last = -1, count = 0;
                    double tf = 0, tl = 0;
                    for (size_t i = (size_t) (0.5 * sr); i + 1 < l.size(); ++i)
                        if (l[i] <= 0.0f && l[i + 1] > 0.0f)
                        {
                            const double t = (double) i + l[i] / (double) (l[i] - l[i + 1]);
                            if (first < 0) { first = (int) i; tf = t; }
                            last = (int) i; tl = t; ++count;
                        }
                    return std::vector<double> { count > 1 ? (count - 1) * sr / (tl - tf) : 0.0 };
                });
            };
            const int h0 = carrier (1.0f, 0.0f), h1 = carrier (1.0f, 1.0f), l1 = carrier (0.0f, 1.0f);
            const int l0 = lab.add ("bhbm12", [] (const ModelDef& d)
            {
                const double sr = 48000.0;
                auto fx = makeAfter (d, {});
                setsS1 (*fx, d, { { "MODULATOR", 0.0f }, { "FREQUENCY", 0.0f }, { "AMOUNT", 0.0f } });
                Buf l = sineBuf (sr, 1000.0, 0.1, 8.0), r = l;
                runStereo256 (*fx, l, r, {}, 1);
                return std::vector<double> { 0.5 * modRate (l, sr, (size_t) (1.0 * sr), 2.5) };
            });
            lab.check ({ h0, h1, l0, l1 }, "BM-12 (ring modulator): portante HI 30 Hz - 4 kHz, LO 0.6 - 80 Hz agli estremi di FREQUENCY (+-6%)", [&lab, h0, h1, l0, l1] {
                auto ok = [] (double f, double ref) { return std::abs (f / ref - 1.0) <= 0.06; };
                return Verdict { ok (lab.v (h0), 30.0) && ok (lab.v (h1), 4000.0) && ok (lab.v (l0), 0.6) && ok (lab.v (l1), 80.0),
                                 "HI " + num (lab.v (h0), 1) + " - " + num (lab.v (h1), 0) + " Hz, LO " + num (lab.v (l0), 3) + " - " + num (lab.v (l1), 1) + " Hz" };
            });
        }

        // 6. wah (picco a piccolo segnale: frequenze +-6%, guadagni +-1 dB)
        {
            const int w0 = peak ("bhwahwah", { { "PEDAL", 0.0f } }), w1 = peak ("bhwahwah", { { "PEDAL", 1.0f } });
            lab.check ({ w0, w1 }, "WahWah (induttore 500 mH): tallone 489 Hz +10.0 dB, punta 1979 Hz +10.5 dB", [&lab, fq, hz, w0, w1] {
                const bool ok = fq (lab.v (w0), 489.0) && fq (lab.v (w1), 1979.0) && within (lab.v (w0, 1), 10.0, 1.0) && within (lab.v (w1, 1), 10.5, 1.0);
                return Verdict { ok, hz (lab.v (w0)) + " Hz (" + num (lab.v (w0, 1), 1) + " dB) -> " + hz (lab.v (w1)) + " Hz (" + num (lab.v (w1, 1), 1) + " dB)" };
            });
        }
        {
            const int a = peak ("bhhb01", { { "PEDAL", 0.03f } }), b = peak ("bhhb01", { { "PEDAL", 1.0f } }), f = peak ("bhhb01", { { "PEDAL", 1.0f }, { "FINE", 1.0f } });
            // accensione automatica: pedale a 0.6 per 1 s poi al tallone; l'effetto (readout 1) si spegne dopo TIMER ADJ (0.4: 0.5 x 12^0.4 = 1.35 s)
            const int off = lab.add ("bhhb01", [] (const ModelDef& d)
            {
                auto fx = createEffect (d);
                fx->prepare (48000.0, 480);
                fx->params[0].store (0.6f);
                const double sr = 48000.0;
                Buf x ((size_t) (4 * sr));
                for (size_t i = 0; i < x.size(); ++i) x[i] = (float) (0.1 * std::sin (2 * kPi * 440 * (double) i / sr));
                double t = -1, onBefore = 0;
                for (size_t i = 0; i < x.size(); i += 48)
                {
                    if (i == (size_t) sr) { onBefore = fx->readout (1); fx->params[0].store (0.0f); }
                    float* p[1] = { x.data() + i };
                    fx->process (p, 1, 48);
                    if (i >= (size_t) sr && t < 0 && fx->readout (1) < 0.5f) t = (double) (i + 48 - (size_t) sr) / sr;
                }
                return std::vector<double> { onBefore, t };
            });
            lab.check ({ a, b, f, off }, "HB01 (Hellbabe): 435 -> 1762 Hz, FINE al massimo in punta 2.2 kHz; al tallone si spegne dopo 1.35 s (+-0.1)", [&lab, fq, hz, a, b, f, off] {
                const bool ok = fq (lab.v (a), 435.0) && fq (lab.v (b), 1762.0) && fq (lab.v (f), 2200.0) && lab.v (off, 0) > 0.5 && within (lab.v (off, 1), 1.35, 0.1);
                return Verdict { ok, hz (lab.v (a)) + " -> " + hz (lab.v (b)) + " Hz, FINE " + hz (lab.v (f)) + " Hz, spento dopo " + num (lab.v (off, 1), 3) + " s" };
            });
        }
        {
            const int a = peak ("bhparapedal", { { "PEDAL", 0.08f } }), b = peak ("bhparapedal", { { "PEDAL", 1.0f } }), c = peak ("bhparapedal", { { "PEDAL", 0.0f } });
            // +18 dB e' il guadagno della risonanza (g=18 della config); all'uscita c'e' anche il livello out=-2 dB: picco misurato +16 dB
            lab.check ({ a, b, c }, "Parapedal: 760 Hz -> 2.2 kHz, picco +16 dB (risonanza +18 dB, uscita -2 dB; +-1), dive bomb a fine corsa fino a 180 Hz",
                       [&lab, fq, hz, a, b, c] {
                const bool ok = fq (lab.v (a), 760.0) && fq (lab.v (b), 2200.0) && fq (lab.v (c), 180.0) && within (lab.v (a, 1), 16.0, 1.0) && within (lab.v (b, 1), 16.0, 1.0);
                return Verdict { ok, hz (lab.v (a)) + " Hz (" + num (lab.v (a, 1), 1) + " dB) -> " + hz (lab.v (b)) + " Hz (" + num (lab.v (b, 1), 1) + " dB), dive "
                                         + hz (lab.v (c)) + " Hz" };
            });
        }

        // click: soglia 10x senza eccezioni (misurati al massimo 6.0x, ENHANCE dell'AM300); i5smooth riusa le famiglie vecchie (che
        // fanno click) con i comandi lisciati. Determinismo fra due istanze nuove; dopo reset() non richiesto (FC600, BLE400 e SM200
        // come i modelli di partenza)
        addClickAndDeterminism (lab, kStage5Dyn, numDyn, "Tappa 5 dinamica/EQ/filtri/synth", 10.0, false);
        lab.run (report);
    }

    /** 'track' del banco s3 della tappa 5 con durata e passo propri: treno d'impulsi da 0.5 V ogni 'stepMs' per 'seconds' dopo 1 s di
        silenzio; { ritardo min, max (ms), LFO (Hz) }. */
    std::vector<double> bbdTrackS5 (const pt::engine::ModelDef& d, const std::map<int, float>& p, double seconds, double stepMs)
    {
        const double SR = 48000.0;
        auto fx = makeAfter (d, p);
        Buf l ((size_t) SR, 0.0f), r = l;
        runStereo256 (*fx, l, r);
        const int n = (int) (seconds * SR), step = (int) (stepMs * 0.001 * SR);
        l.assign ((size_t) n, 0.0f); r = l;
        for (int i = 0; i < n; i += step) l[(size_t) i] = r[(size_t) i] = 0.5f;
        runStereo256 (*fx, l, r);
        std::vector<double> dl;
        for (int i = 0; i + step <= n; i += step)
        {
            int best = -1;
            float bv = 0;
            for (int j = 12; j < step - 2; ++j) if (std::abs (l[(size_t) (i + j)]) > bv) { bv = std::abs (l[(size_t) (i + j)]); best = j; }
            dl.push_back (best * 1000.0 / SR);
        }
        double lo = 1e9, hi = 0, mean = 0;
        for (double v : dl) { lo = std::min (lo, v); hi = std::max (hi, v); mean += v; }
        mean /= (double) dl.size();
        const int cross = hystCross (dl, mean, 0.3 * (hi - lo));
        return { lo, hi, cross / 2.0 / ((double) dl.size() * stepMs * 0.001) };
    }

    /** Tappa 5, lavoro s3: chorus, flanger, phaser, tremolo, vibrato e delay BBD (FxStage5Mod.cpp). Metodi del banco s3 (stereo,
        blocchi da 256, comandi per indice dopo prepare; "noise=0" / "comp=0" aggiunti alla config dei tipi i5 come il banco). */
    void runStage5ModTests (const std::function<void (const juce::String&, bool, const juce::String&)>& report)
    {
        using namespace pt::engine;
        constexpr int numMod = countOf (kStage5Mod);
        Lab lab;
        using PMap = std::map<int, float>;
        using MFn = std::function<std::vector<double> (const ModelDef&, const PMap&)>;
        /** Misura con i comandi per indice, eventualmente con la config modificata. */
        auto at = [&lab] (const char* id, const char* extra, PMap p, MFn m)
        {
            auto f = [p, m] (const ModelDef& d) { return m (d, p); };
            return extra != nullptr ? addPatched (lab, id, extra, f) : lab.add (id, f);
        };
        auto track = [] (double s, double step) { return MFn ([s, step] (const ModelDef& d, const PMap& p) { return bbdTrackS5 (d, p, s, step); }); };
        auto notch = [] (double s) { return MFn ([s] (const ModelDef& d, const PMap& p) { return phaserNotches (d, p, s); }); };
        auto tol = [] (double x, double ref, double rel, double abs) { return std::abs (x - ref) <= rel * std::abs (ref) + abs; };

        // 0. pedali esistenti invariati bit per bit (banco s3 'hash': frase di 6 s, fabbrica / tutti 0.8 / tutti 0.2, R = 0.7 L; impronte del
        //    banco col motore di d691877)
        {
            struct Ref { const char* id; uint64_t hash; };
            static const Ref refs[] = {
                { "ce1", 0xba7cb4ec4a4198f0ull }, { "ce2", 0x1c265696304c2dfcull }, { "ch1", 0x48cd140fe5c5e7dfull }, { "bf2", 0xc0f5da7731dbaa2full },
                { "ph2", 0x0eefe14251cd788dull }, { "tr2", 0x77647c9d8ed04ee8ull }, { "vb2", 0xfe0da55a1de37ca8ull }, { "dm2", 0xbc24ad3e410c67b0ull },
                { "dm3", 0x8de2b453a67e578bull }, { "ehxss75", 0x2437960de13072a7ull }, { "ehdmm", 0xbfb931a230a17f83ull }, { "evh90", 0xc38117b07b98878full },
                { "ib9fl9", 0xfd8da8a9ecce7b7full }, { "ib9ad9", 0xa77208d6d577bd45ull }, { "dc2", 0xb404f5fd59139158ull }, { "ce5", 0xbad717b9b7a9c186ull },
                { "ceb3", 0xa4a259ae26dd61d2ull }, { "hf2", 0xf4ee1b51feab1946ull }, { "bf2b", 0x0d463284736f4f05ull }, { "pn2", 0x332b06897c649079ull },
                { "ehxgoodvib", 0xe0bc728720e3a293ull }, { "ib7pt707", 0x344aaaa433e2b47full }, { "ib10cd10", 0x611c04a2a9f14fd5ull } };
            static const Buf g = guitarS3 (6.0);
            std::vector<int> h;
            for (const auto& r : refs)
            {
                h.push_back (lab.add (r.id, [] (const ModelDef& d)
                {
                    uint64_t hv = 1469598103934665603ull;
                    for (int set = 0; set < 3; ++set)
                    {
                        PMap p;
                        if (set > 0) for (int k = 0; k < d.numControls; ++k) p[k] = set == 1 ? 0.8f : 0.2f;
                        auto fx = makeAfter (d, p);
                        Buf bl = g, br = g;
                        for (auto& x : br) x *= 0.7f;
                        runStereo256 (*fx, bl, br);
                        hv = fnvBytes (br, fnvBytes (bl, hv));
                    }
                    return std::vector<double> { (double) (uint32_t) (hv >> 32), (double) (uint32_t) hv };
                }));
                lab.first (h.back());
            }
            lab.check (h, "Tappa 5: 23 modulazioni e delay esistenti (CE-1/2/5, CH-1, BF-2/2B, PH-2, TR-2, VB-2, DM-2/3, DC-2, CEB-3, HF-2, PN-2, EHX, EVH 90, Ibanez) "
                          "invariati bit per bit", [&lab, h] {
                if (! kBitExactRefs) return Verdict { true, "impronte di riferimento misurate con GCC su Linux: confronto non applicabile a questo compilatore" };
                juce::String bad;
                for (size_t k = 0; k < h.size(); ++k)
                    if (hashOfS5 (lab, h[k]) != refs[k].hash) bad << refs[k].id << " DIVERSO (" << juce::String::toHexString ((juce::int64) hashOfS5 (lab, h[k])) << ") ";
                return Verdict { bad.isEmpty(), bad.isEmpty() ? juce::String ((int) h.size()) + "/" + juce::String ((int) h.size()) + " hash identici" : bad.trim() };
            });
        }

        // 1. robustezza dei 29 modelli (banco s3 'check', frase di 3 s invece di 8): comandi tutti a 1, tutti a 0, casuali ogni 40 blocchi,
        //    casuali con l'ingresso x6 (~2 V di picco), stessa istanza; livelli col riff a -12 dBFS
        {
            static const Buf g = guitarS3 (3.0, 3);
            std::vector<int> h;
            for (int m = 0; m < numMod; ++m)
                h.push_back (lab.add (kStage5Mod[m], [] (const ModelDef& d)
                {
                    auto fx = makeAfter (d, {});
                    double peak = 0;
                    bool finite = true;
                    uint32_t s = 77;
                    auto rnd = [&s] { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) (s & 0xffff) / 65535.0f; };
                    for (int pass = 0; pass < 4; ++pass)
                    {
                        Buf l = g, r = g;
                        const float gain = pass == 3 ? 6.0f : 1.0f;
                        for (auto& x : l) x *= gain;
                        for (auto& x : r) x *= gain;
                        runStereo256 (*fx, l, r, [&] (size_t i)
                        {
                            if (i % (256 * 40) == 0)
                                for (int k = 0; k < d.numControls; ++k) fx->params[k].store (pass == 0 ? 1.0f : pass == 1 ? 0.0f : rnd());
                        });
                        for (size_t i = 0; i < l.size(); ++i)
                        {
                            finite = finite && std::isfinite (l[i]) && std::isfinite (r[i]);
                            peak = std::max (peak, (double) std::max (std::abs (l[i]), std::abs (r[i])));
                        }
                    }
                    return std::vector<double> { finite ? 1.0 : 0.0, peak };
                }));
            lab.check (h, "Tappa 5 modulazioni: robustezza (29 modelli, comandi a 1 / 0 / casuali, ingresso fino a ~2 V): finiti, picco < 8 V", [&lab, h] {
                double worst = 0;
                juce::String bad, worstId;
                for (int m = 0; m < numMod; ++m)
                {
                    const bool fin = lab.v (h[(size_t) m], 0) > 0.5;
                    const double pk = fin ? lab.v (h[(size_t) m], 1) : 999.0;
                    if (pk > worst) { worst = pk; worstId = kStage5Mod[m]; }
                    if (! fin || pk >= 8.0) bad << kStage5Mod[m] << (fin ? "" : " NaN") << " ";
                }
                return Verdict { bad.isEmpty(), "picco max " + num (worst) + " V (" + worstId + ") " + bad.trim() };
            });
            const auto hl = addLevelsS5 (lab, kStage5Mod, numMod, "Tappa 5 modulazioni");
            checkLevelsS5 (lab, hl, kStage5Mod, numMod, "Tappa 5 modulazioni: livelli con riff a -12 dBFS e comandi di default (29 modelli: finiti, picco < 0 dBFS, "
                                                        "RMS -40..-20 dBFS)", -40.0, -20.0, 0.0);
        }

        // 2. ritardi dei BBD: impulso dopo 1.5 s di assestamento (ritardo del picco) o traiettoria con il treno d'impulsi (min / max);
        //    +-4% + 0.25 ms. Bande -3 dB del ramo ritardato (+-10%)
        {
            struct D { const char* id; const char* extra; PMap p; int kind; double s, step, ms; };       // kind 0 impulso, 1 minimo, 2 massimo
            const D ds[] = {
                { "bhuc200", nullptr, { { 2, 0.5f }, { 3, 1.0f } }, 1, 10, 25, 3.52 }, { "bhuc200", nullptr, { { 2, 0.5f }, { 3, 1.0f } }, 2, 10, 25, 6.08 },
                { "bhuf300", "noise=0", { { 1, 0.0f }, { 2, 0.0f }, { 4, 0.0f } }, 0, 0, 0, 1.06 },
                { "bhuf300", "noise=0", { { 1, 1.0f }, { 2, 0.0f }, { 4, 0.0f } }, 0, 0, 0, 12.88 },
                { "bhhf300", "noise=0", { { 0, 0.0f }, { 1, 0.0f }, { 3, 0.0f } }, 0, 0, 0, 0.58 },
                { "bhhf300", "noise=0", { { 0, 1.0f }, { 1, 0.0f }, { 3, 0.0f } }, 0, 0, 0, 6.48 },
                { "bhuv300", nullptr, { { 0, 1.0f }, { 2, 0.5f }, { 3, 1.0f } }, 1, 10, 25, 2.63 }, { "bhuv300", nullptr, { { 0, 1.0f }, { 2, 0.5f }, { 3, 1.0f } }, 2, 10, 25, 6.52 },
                { "bhco600", nullptr, { { 1, 0.5f }, { 3, 1.0f } }, 1, 10, 25, 3.46 }, { "bhco600", nullptr, { { 1, 0.5f }, { 3, 1.0f } }, 2, 10, 25, 6.04 },
                { "bhvd400", "noise=0 comp=0", { { 0, 0.0f }, { 2, 0.0f }, { 1, 1.0f } }, 0, 0, 0, 20.25 },
                { "bhvd400", "noise=0 comp=0", { { 0, 1.0f }, { 2, 0.0f }, { 1, 1.0f } }, 0, 0, 0, 300.35 },
                { "bhvm1", "noise=0 comp=0", { { 3, 0.0f }, { 2, 0.0f }, { 1, 1.0f } }, 0, 0, 0, 35.75 },
                { "bhvm1", "noise=0 comp=0", { { 3, 1.0f }, { 2, 0.0f }, { 1, 1.0f } }, 0, 0, 0, 550.02 },
                { "bhbm14m", "noise=0 comp=0", { { 6, 0.0f }, { 8, 0.0f }, { 2, 1.0f }, { 7, 0.0f } }, 0, 0, 0, 25.29 },
                { "bhbm14m", "noise=0 comp=0", { { 6, 1.0f }, { 8, 0.0f }, { 2, 1.0f }, { 7, 0.0f } }, 0, 0, 0, 501.27 },
                { "bhbm14m", "noise=0 comp=0", { { 6, 1.0f }, { 8, 0.0f }, { 2, 1.0f }, { 7, 1.0f } }, 0, 0, 0, 998.58 },
                { "bhbm18m", "noise=0", { { 6, 0.0f }, { 5, 0.0f }, { 8, 0.5f }, { 7, 0.0f } }, 0, 0, 0, 2.08 },
                { "bhbm18m", "noise=0", { { 6, 1.0f }, { 5, 0.0f }, { 8, 0.5f }, { 7, 0.0f } }, 0, 0, 0, 20.19 },
                { "bhbm18m", "noise=0", { { 6, 0.0f }, { 5, 0.0f }, { 8, 0.5f }, { 7, 1.0f } }, 0, 0, 0, 0.48 },
                { "bhbm18m", "noise=0", { { 6, 1.0f }, { 5, 0.0f }, { 8, 0.5f }, { 7, 1.0f } }, 0, 0, 0, 4.08 },
                { "bhbflanger", "noise=0", { { 0, 0.0f }, { 1, 0.0f }, { 2, 0.0f }, { 5, 1.0f } }, 0, 0, 0, 0.67 },
                { "bhbflanger", "noise=0", { { 0, 1.0f }, { 1, 1.0f }, { 2, 1.0f }, { 5, 1.0f } }, 0, 0, 0, 24.08 },
                { "bhpedalflanger", "noise=0", { { 0, 0.0f }, { 2, 1.0f }, { 1, 1.0f } }, 0, 0, 0, 0.58 },
                { "bhpedalflanger", "noise=0", { { 0, 1.0f }, { 2, 1.0f }, { 1, 1.0f } }, 0, 0, 0, 10.08 },
                { "bhflangenoname", nullptr, { { 2, 0.5f }, { 1, 1.0f }, { 4, 0.5f }, { 5, 1.0f } }, 1, 10, 25, 0.31 },
                { "bhflangenoname", nullptr, { { 2, 0.5f }, { 1, 1.0f }, { 4, 0.5f }, { 5, 1.0f } }, 2, 10, 25, 10.44 },
                { "cvdelaypd18", "noise=0 comp=0", { { 2, 0.0f }, { 0, 0.0f }, { 1, 1.0f } }, 0, 0, 0, 20.19 },
                { "cvdelaypd18", "noise=0 comp=0", { { 2, 1.0f }, { 0, 0.0f }, { 1, 1.0f } }, 0, 0, 0, 530.33 },
                { "cvclassicdelay", "noise=0 comp=0", { { 3, 0.0f }, { 4, 0.0f }, { 5, 1.0f } }, 0, 0, 0, 20.19 },
                { "cvclassicdelay", "noise=0 comp=0", { { 3, 1.0f }, { 4, 0.0f }, { 5, 1.0f } }, 0, 0, 0, 600.42 } };
            std::vector<int> h;
            for (const auto& x : ds)
                h.push_back (at (x.id, x.extra, x.p, x.kind == 0 ? MFn (bbdImpulse) : track (x.s, x.step)));
            std::vector<D> copy (std::begin (ds), std::end (ds));
            lab.check (h, "BBD: ritardi (UC200 3.5-6.1, UF300 1.06-12.9, HF300 0.58-6.5, UV300 2.6-6.5, CO600 3.5-6.0, VD400 20.3 / 300.4, VM1 35.8 / 550, BM-14M 25 / "
                          "501 / 999, BM-18M 2.1-20.2 / 0.48-4.1, B-Flanger 0.67 / 24.1, Pedalflanger 0.58 / 10.1, Flange No Name 0.31-10.4, PD18 20.2 / 530.3, "
                          "Classic Delay 20.2 / 600.4 ms; +-4% + 0.25 ms)", [&lab, h, copy, tol] {
                bool ok = true;
                juce::String detail;
                for (size_t k = 0; k < h.size(); ++k)
                {
                    const double v = lab.v (h[k], copy[k].kind == 2 ? 1 : 0);
                    const bool g = tol (v, copy[k].ms, 0.04, 0.25);
                    ok = ok && g;
                    if (! g || k % 2 == 0) detail << copy[k].id << " " << num (v) << (g ? "" : " (FUORI)") << "; ";
                }
                return Verdict { ok, detail.trimCharactersAtEnd ("; ") + " ms" };
            });
            // bande -3 dB del ramo ritardato (impulsi: UF300 MANUAL 0, VD400 0 / 1, BM-14M SHORT 0 / LONG 1; CO600 a DEPTH 0)
            const int co = at ("bhco600", "noise=0", { { 3, 0.0f } }, bbdImpulse);
            const std::vector<std::pair<int, double>> bands { { h[2], 10020.0 }, { co, 8355.0 }, { h[10], 2443.0 }, { h[11], 2379.0 }, { h[14], 4482.0 }, { h[16], 1248.0 } };
            std::vector<int> hb;
            for (const auto& b : bands) hb.push_back (b.first);
            lab.check (hb, "BBD: banda -3 dB del ramo ritardato (UF300 10 kHz, CO600 8.4 kHz, VD400 2.4 kHz, BM-14M 4.5 kHz SHORT -> 1.25 kHz LONG; +-10%)", [&lab, bands] {
                bool ok = true;
                juce::String detail;
                for (const auto& [hh, ref] : bands)
                {
                    const double v = lab.v (hh, 1);
                    ok = ok && std::abs (v / ref - 1.0) <= 0.10;
                    detail << juce::roundToInt (v) << " ";
                }
                return Verdict { ok, detail.trim() + " Hz" };
            });
        }

        // 3. LFO a meta' corsa contro il valore atteso della legge del comando (+-8%): treno d'impulsi (chorus, flanger), notch (phaser),
        //    inviluppo (tremolo)
        {
            struct L { const char* id; PMap p; int kind; double s, step, hz; };       // kind 0 track, 1 notch, 2 am
            const L ls[] = {
                { "bhuc200", { { 2, 0.5f }, { 3, 1.0f } }, 0, 10, 25, 1.126 }, { "bhuf300", { { 3, 0.5f }, { 2, 1.0f }, { 4, 0.0f } }, 0, 10, 25, 0.791 },
                { "bhup300", { { 1, 0.5f }, { 2, 1.0f }, { 3, 0.0f } }, 1, 12, 0, 0.843 }, { "bhut300", { { 0, 0.5f }, { 2, 1.0f } }, 2, 0, 0, 3.494 },
                { "bhtp300", { { 1, 0.5f }, { 0, 1.0f } }, 2, 0, 0, 0.734 }, { "bhuv300", { { 0, 1.0f }, { 2, 0.5f }, { 3, 1.0f } }, 0, 10, 25, 5.477 },
                { "bhcc300", { { 0, 1.0f }, { 1, 1.0f } }, 0, 10, 30, 2.06 }, { "bhco600", { { 1, 0.5f }, { 3, 1.0f } }, 0, 10, 25, 1.385 },
                { "bhvp1", { { 0, 0.5f } }, 1, 12, 0, 0.894 }, { "bhdualphase", { { 0, 0.5f }, { 3, 1.0f }, { 4, 0.0f }, { 8, 1.0f }, { 9, 0.0f } }, 1, 12, 0, 1.342 },
                { "bhbflanger", { { 1, 1.0f }, { 2, 0.0f }, { 3, 0.3f } }, 0, 12, 30, 0.302 }, { "bhbm18m", { { 4, 0.5f }, { 5, 1.0f }, { 8, 0.5f } }, 0, 10, 25, 1.581 },
                { "bhflangenoname", { { 2, 0.5f }, { 1, 1.0f }, { 4, 0.5f }, { 5, 1.0f } }, 0, 10, 25, 0.775 }, { "cvchorus", { { 2, 0.5f }, { 1, 1.0f } }, 0, 10, 25, 1.342 },
                { "cvtremolo", { { 2, 0.5f }, { 0, 1.0f } }, 2, 0, 0, 3.162 } };
            std::vector<int> h;
            for (const auto& x : ls)
                h.push_back (at (x.id, nullptr, x.p, x.kind == 0 ? track (x.s, x.step) : x.kind == 1 ? notch (x.s) : MFn (tremoloAm)));
            std::vector<L> copy (std::begin (ls), std::end (ls));
            lab.check (h, "LFO a meta' corsa (UC200 1.13, UF300 0.79, UP300 0.84, UT300 3.49, TP300 0.73, UV300 5.48, CC300 tutti i tasti 2.06, CO600 1.39, VP1 0.89, "
                          "Dual-Phase 1.34, B-Flanger 0.30, BM-18M 1.58, Flange No Name 0.78, Chorus 1.34, Classic Tremolo 3.16 Hz; +-8%)", [&lab, h, copy] {
                bool ok = true;
                juce::String detail;
                for (size_t k = 0; k < h.size(); ++k)
                {
                    const double f = lab.v (h[k], copy[k].kind == 0 ? 2 : copy[k].kind == 1 ? 3 : 0);
                    const bool g = std::abs (f / copy[k].hz - 1.0) <= 0.08;
                    ok = ok && g;
                    detail << copy[k].id << " " << num (f, 3) << (g ? "" : " (FUORI)") << "; ";
                }
                return Verdict { ok, detail.trimCharactersAtEnd ("; ") + " Hz" };
            });
        }

        // 4. phaser: traiettoria del primo notch con l'LFO al minimo (impulso ogni 60 ms; estremi +-12%; 23 Hz e' il primo bin della FFT)
        {
            struct P { const char* id; PMap p; double s, lo, hi; };
            const P ps[] = { { "bhup300", { { 1, 0.0f }, { 2, 1.0f }, { 3, 0.0f }, { 0, 0.0f } }, 40, 23, 281 },
                             { "bhup300", { { 1, 0.0f }, { 2, 1.0f }, { 3, 0.0f }, { 0, 1.0f } }, 40, 35, 234 },
                             { "bhvp1", { { 0, 0.0f } }, 40, 59, 1090 }, { "bhph9", { { 0, 0.0f } }, 40, 82, 633 },
                             { "bhdualphase", { { 0, 0.0f }, { 3, 1.0f }, { 4, 0.0f }, { 8, 1.0f }, { 9, 0.0f } }, 40, 23, 1980 },
                             { "bh69vibe", { { 2, 0.0f }, { 1, 1.0f } }, 30, 23, 844 },
                             { "bhbm13", { { 6, 0.0f }, { 5, 1.0f }, { 4, 0.0f } }, 60, 23, 1160 } };
            std::vector<int> h;
            for (const auto& x : ps) { h.push_back (at (x.id, nullptr, x.p, notch (x.s))); lab.first (h.back()); }
            std::vector<P> copy (std::begin (ps), std::end (ps));
            lab.check (h, "Phaser: primo notch (UP300 MODE I 23-281 / II 35-234, VP1 59-1090, PH9 82-633, Dual-Phase 23-1980, 69 Vibe 23-844, BM-13 23-1160 Hz; +-12%)",
                       [&lab, h, copy] {
                bool ok = true;
                juce::String detail;
                for (size_t k = 0; k < h.size(); ++k)
                {
                    const double lo = lab.v (h[k], 0), hi = lab.v (h[k], 1);
                    const bool g = std::abs (lo / copy[k].lo - 1.0) <= 0.12 && std::abs (hi / copy[k].hi - 1.0) <= 0.12;
                    ok = ok && g;
                    detail << copy[k].id << " " << juce::roundToInt (lo) << "-" << juce::roundToInt (hi) << (g ? "" : " (FUORI)") << "; ";
                }
                return Verdict { ok, detail.trimCharactersAtEnd ("; ") + " Hz" };
            });
        }

        // 5. retroazione al massimo (frase 6 s + silenzio fino a 14 s): delay a 6 tempi (VD400 coda <= -111 dBV, VM1 <= -84, PD18 e
        //    Classic Delay <= -60, BM-14M limitato: finito e non crescente), flanger a 3 posizioni del MANUAL / pedale (coda <= -74 dBV)
        {
            struct F { const char* id; int fb, time; double maxTail; };
            const F fs[] = { { "bhvd400", 2, 0, -111.0 }, { "bhvm1", 2, 3, -84.0 }, { "cvdelaypd18", 0, 2, -60.0 }, { "cvclassicdelay", 4, 3, -60.0 }, { "bhbm14m", 8, 6, 0.0 } };
            std::vector<int> h;
            std::vector<juce::String> what;
            std::vector<double> lim;
            for (const auto& f : fs)
                for (float t : { 0.0f, 0.2f, 0.4f, 0.6f, 0.8f, 1.0f })
                {
                    h.push_back (at (f.id, nullptr, { { f.fb, 1.0f }, { f.time, t } }, bbdFeedback));
                    what.push_back (juce::String (f.id) + "@" + num (t, 1));
                    lim.push_back (f.maxTail);
                }
            lab.check (h, "Delay BBD con la retroazione al massimo (VD400, VM1, PD18, Classic Delay, BM-14M; tempo 0..1): finiti, picco < 1 V, coda <= -111 / -84 / -60 / -60 dBV, "
                          "BM-14M limitato e non crescente", [&lab, h, what, lim] {
                bool ok = true;
                double pk = 0;
                juce::String bad, tails;
                for (size_t k = 0; k < h.size(); ++k)
                {
                    const double p = lab.v (h[k], 0), tail = lab.v (h[k], 1), early = lab.v (h[k], 3);
                    const bool g = lab.v (h[k], 2) > 0.5 && p < 1.0 && (lim[k] < 0.0 ? tail <= lim[k] : tail <= early);
                    ok = ok && g;
                    pk = std::max (pk, p);
                    if (k % 6 == 5) tails << what[k].upToFirstOccurrenceOf ("@", false, false) << " " << num (tail, 1) << "; ";
                    if (! g) bad << what[k] << " (" << num (p, 2) << " V, " << num (tail, 1) << " dBV) ";
                }
                return Verdict { ok, "picco max " + num (pk, 3) + " V; coda a tempo 1: " + tails.trimCharactersAtEnd ("; ") + " dBV " + bad.trim() };
            });
            // la coda dei flanger con la risonanza al massimo e' il rumore della linea BBD (noise=...) ripreso dal pettine, non una
            // ripetizione che resta: B-Flanger e Pedalflanger non hanno MANUAL (il pedale e' la velocita' dell'LFO in AUTO / OSC, con
            // l'LFO a 0.1 Hz il pettine resta sui ritardi lunghi e il rumore sale a -71 dBV) e si provano nella posizione di default,
            // come il banco ("res max"); per tutti la coda deve anche non crescere (12-14 s sotto 6-8 s)
            struct R { const char* id; int res, depth, manual; };
            const R rs[] = { { "bhuf300", 4, 2, 1 }, { "bhhf300", 3, 1, 0 }, { "bhbuf300", 0, 2, 3 }, { "bhbflanger", 4, -1, -1 },
                             { "bhpedalflanger", 3, 1, -1 }, { "bhflangenoname", 4, 1, 0 }, { "bhbm18m", 8, 5, 6 } };
            std::vector<int> hr;
            std::vector<juce::String> rw;
            for (const auto& r : rs)
                for (float m : { 0.0f, 0.5f, 1.0f })
                {
                    if (r.manual < 0 && m != 0.5f) continue;
                    PMap p { { r.res, 1.0f } };
                    if (r.manual >= 0) p[r.manual] = m;
                    if (r.depth >= 0) p[r.depth] = 1.0f;
                    hr.push_back (at (r.id, nullptr, p, bbdFeedback));
                    rw.push_back (juce::String (r.id) + (r.manual >= 0 ? "@" + num (m, 1) : juce::String()));
                }
            lab.check (hr, "Flanger con la risonanza al massimo (UF300, HF300, BUF300, Flange No Name, BM-18M a 3 posizioni del MANUAL; B-Flanger e Pedalflanger "
                           "nella posizione di default): finiti, coda <= -74 dBV e non crescente", [&lab, hr, rw] {
                bool ok = true;
                double worst = -300;
                juce::String detail, bad;
                for (size_t k = 0; k < hr.size(); ++k)
                {
                    const double tail = lab.v (hr[k], 1);
                    const bool g = lab.v (hr[k], 2) > 0.5 && tail <= -74.0 && tail <= lab.v (hr[k], 3);
                    ok = ok && g;
                    worst = std::max (worst, tail);
                    detail << num (tail, 1) << " ";
                    if (! g) bad << rw[k] << " ";
                }
                return Verdict { ok, "coda max " + num (worst, 1) + " dBV (" + detail.trim() + ") " + bad.trim() };
            });
        }

        // 6. tremolo (seno 440 Hz 0.3 V, inviluppo RMS su 5 ms, DEPTH 1, RATE 0.5): profondita'
        {
            const int ut = at ("bhut300", nullptr, { { 0, 0.5f }, { 2, 1.0f } }, tremoloAm), tp = at ("bhtp300", nullptr, { { 1, 0.5f }, { 0, 1.0f } }, tremoloAm);
            lab.check ({ ut, tp }, "Tremolo: profondita' UT300 67 dB, TP300 45 dB (+-1.5)", [&lab, ut, tp] {
                return Verdict { within (lab.v (ut, 1), 67.4, 1.5) && within (lab.v (tp, 1), 44.8, 1.5), num (lab.v (ut, 1), 1) + " / " + num (lab.v (tp, 1), 1) + " dB" };
            });
        }

        // click: soglia 10x senza eccezioni (misurati al massimo 1.98x, LFO del BM-18M); determinismo fra due istanze nuove (reset() non
        // riporta LFO e lisciature allo stato iniziale)
        addClickAndDeterminism (lab, kStage5Mod, numMod, "Tappa 5 modulazioni", 10.0, false);
        lab.run (report);
    }

    //------------------------------------------------------------------------------
    // Misure del banco s4 della tappa 5 (stereo, blocchi da 256, comandi per ruolo dopo prepare)
    //------------------------------------------------------------------------------
    void setStepIdxS5 (pt::engine::Effect& fx, int i, int k)
    {
        if (i < 0) return;
        const int s = std::max (1, (int) fx.def.controls[i].steps);
        fx.params[i].store (s > 1 ? (float) k / (float) (s - 1) : 0.0f);
    }

    /** Ritardo (ms) del primo eco: burst di 2 ms a 1 kHz dopo 0.8 s, picco della correlazione con il ramo ritardato (uscita - ingresso). */
    double s5Delay (const pt::engine::ModelDef& d, int mode, float t)
    {
        const double SR = 48000.0;
        auto fx = makeAfter (d, {});
        if (mode >= 0) setStepIdxS5 (*fx, roleIndex (d, "mode"), mode);
        setRoleS4 (*fx, "time", t); setRoleS4 (*fx, "feedback", 0.0f); setRoleS4 (*fx, "mix", 1.0f); setRoleS4 (*fx, "level", 1.0f);
        setRoleS4 (*fx, "mod", 0.0f); setRoleS4 (*fx, "param", 0.0f); setRoleS4 (*fx, "balance", 1.0f);
        const int pre = (int) (0.8 * SR), len = (int) (3.6 * SR), bl = 96;
        Buf l ((size_t) (pre + len), 0.0f);
        for (int i = 0; i < bl; ++i) l[(size_t) (pre + i)] = 0.4f * std::sin (3.14159f * i / bl) * std::sin (2 * 3.14159f * 1000.0f * i / (float) SR + 0.3f);
        const Buf in = l;
        Buf r = l;
        runStereo256 (*fx, l, r);
        double best = -1;
        int lag = 0;
        for (int L = 48; L < len - bl; ++L)
        {
            double c = 0;
            for (int i = 0; i < bl; ++i) c += in[(size_t) (pre + i)] * (l[(size_t) (pre + L + i)] - in[(size_t) (pre + L + i)]);
            c = std::abs (c);
            if (c > best) { best = c; lag = L; }
        }
        return lag * 1000.0 / SR;
    }

    /** Stabilita': retroazione / decadimento al massimo, frase di 2 s poi 16 s di silenzio; { picco (V), RMS ultimo secondo (dBV), finito }. */
    std::vector<double> s5Stability (const pt::engine::ModelDef& d, int mode)
    {
        static const Buf g = guitarS4 (2.0, 4);
        auto fx = makeAfter (d, {});
        if (mode >= 0) setStepIdxS5 (*fx, roleIndex (d, "mode"), mode);
        setRoleS4 (*fx, "time", 1.0f); setRoleS4 (*fx, "feedback", 1.0f); setRoleS4 (*fx, "mix", 1.0f); setRoleS4 (*fx, "decay", 1.0f);
        setRoleS4 (*fx, "mod", 1.0f); setRoleS4 (*fx, "param", 1.0f); setRoleS4 (*fx, "tone", 1.0f);
        setRoleS4 (*fx, "p1", 0.0f); setRoleS4 (*fx, "p2", 1.0f); setRoleS4 (*fx, "p3", 1.0f);
        const int n = (int) (18.0 * 48000);
        Buf l = g;
        l.resize ((size_t) n, 0.0f);
        Buf r = l;
        runStereo256 (*fx, l, r);
        double pk = 0;
        bool fin = true;
        for (int i = 0; i < n; ++i)
        {
            fin = fin && std::isfinite (l[(size_t) i]) && std::isfinite (r[(size_t) i]);
            pk = std::max (pk, (double) std::max (std::abs (l[(size_t) i]), std::abs (r[(size_t) i])));
        }
        return { pk, dbOf (rmsOf (l, (size_t) (n - 48000), (size_t) n)), fin ? 1.0 : 0.0 };
    }

    /** Tempo di riverbero (s): rumore di 0.2 s, decadimento della somma degli RMS su 20 ms da -5 a -25 dB sotto il massimo del primo secondo,
        esteso a 60 dB (risoluzione 0.06 s); 'seconds' di segnale (il banco: 30 s; basta che il decadimento di 25 dB stia dentro). */
    double s5Rt (const pt::engine::ModelDef& d, int mode, float decayK, double seconds)
    {
        const double SR = 48000.0;
        auto fx = makeAfter (d, {});
        if (mode >= 0) setStepIdxS5 (*fx, roleIndex (d, "mode"), mode);
        setRoleS4 (*fx, "decay", decayK); setRoleS4 (*fx, "time", roleIndex (d, "decay") >= 0 ? 0.0f : decayK); setRoleS4 (*fx, "mix", 1.0f);
        setRoleS4 (*fx, "tone", 0.6f); setRoleS4 (*fx, "feedback", 0.0f);
        const int n = (int) (seconds * SR), bl = (int) (0.2 * SR);
        Buf l ((size_t) n, 0.0f);
        uint32_t s = 7;
        for (int i = 0; i < bl; ++i) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; l[(size_t) i] = 0.2f * (float) (int32_t) s * 4.6566129e-10f; }
        Buf r = l;
        runStereo256 (*fx, l, r);
        const int w = (int) (0.02 * SR);
        std::vector<float> e;
        for (int p = bl; p + w < n; p += w)
        {
            const float sum = (float) rmsOf (l, (size_t) p, (size_t) (p + w)) + (float) rmsOf (r, (size_t) p, (size_t) (p + w));
            e.push_back (20.0f * std::log10 (std::max (sum, 1.0e-12f)));
        }
        if (e.empty()) return 0;
        const float e0 = *std::max_element (e.begin(), e.begin() + (long) std::min<size_t> (e.size(), 50));
        int a = -1, b = -1;
        for (size_t k = 0; k < e.size(); ++k)
        {
            if (a < 0 && e[k] <= e0 - 5) a = (int) k;
            if (a >= 0 && e[k] <= e0 - 25) { b = (int) k; break; }
        }
        if (a < 0 || b < 0) return 99;
        return (double) ((float) (b - a) * 0.02f * 3.0f);
    }

    /** Frequenza dell'uscita per un seno a 220 Hz (YIN sull'ultimo mezzo secondo): solo effetto (BALANCE / WET 1, DRY / DIRECT 0). */
    double s5OutFreq (const pt::engine::ModelDef& d, int shift, int mode)
    {
        const double SR = 48000.0, f0 = 220.0;
        auto fx = makeAfter (d, {});
        if (shift >= 0) setStepIdxS5 (*fx, roleIndex (d, "shift"), shift);
        if (mode >= 0) setStepIdxS5 (*fx, roleIndex (d, "mode"), mode);
        setRoleS4 (*fx, "balance", 1.0f); setRoleS4 (*fx, "dry", 0.0f); setRoleS4 (*fx, "wet", 1.0f); setRoleS4 (*fx, "direct", 0.0f);
        const int n = (int) (2.0 * SR);
        Buf l ((size_t) n);
        for (int i = 0; i < n; ++i) l[(size_t) i] = 0.2f * (float) std::sin (2 * 3.14159265 * f0 * i / SR);
        Buf r = l;
        runStereo256 (*fx, l, r);
        const int a = n - (int) (0.6 * SR), W = (int) (0.25 * SR), Lmax = (int) (SR / 30);
        std::vector<double> dd ((size_t) Lmax + 2, 1.0);
        double acc = 0;
        for (int L = 1; L <= Lmax + 1; ++L)
        {
            double sdiff = 0;
            for (int i = a; i < a + W; ++i) { const double e = l[(size_t) i] - l[(size_t) (i + L)]; sdiff += e * e; }
            acc += sdiff;
            dd[(size_t) L] = acc > 0 ? sdiff * L / acc : 1.0;
        }
        int best = -1;
        for (int L = (int) (SR / 2000); L <= Lmax; ++L)
            if (dd[(size_t) L] < 0.15 && dd[(size_t) L] <= dd[(size_t) L - 1] && dd[(size_t) L] <= dd[(size_t) L + 1]) { best = L; break; }
        if (best < 0) return 0.0;
        const double y0 = dd[(size_t) best - 1], y1 = dd[(size_t) best], y2 = dd[(size_t) best + 1], den = y0 - 2 * y1 + y2;
        const double sh = std::abs (den) > 1e-12 ? 0.5 * (y0 - y2) / den : 0.0;
        return (double) (float) (SR / (best + sh));
    }

    /** Tappa 5, lavoro s4: digitali Behringer, preampli/IR e digitali Cuvave/M-VAVE (FxStage5Time.cpp). Metodi del banco s4. */
    void runStage5DigitalTests (const std::function<void (const juce::String&, bool, const juce::String&)>& report)
    {
        using namespace pt::engine;
        constexpr int numDig = countOf (kStage5Digital);
        Lab lab;

        // 0. pedali esistenti invariati bit per bit: le 8 config di partenza dei cloni avvolti (i5wrap), banco s4 'hash' (frase stereo di 2 s,
        //    comandi spostati di +0.37 e frase di 1.5 s, poi 1 s in mono; impronte del banco col motore di d691877). La prima impronta
        //    (comandi fermi) e' invariata; dalla 1.3.1 la seconda e la terza (dopo lo spostamento) sono nuove: involucro senza click delle
        //    famiglie vecchie e, per l'AC-3, i modi con le loro risonanze. I cloni i5wrap usano createEffectRaw() e non cambiano.
        //    AC-3: dalla 1.3.1 il pomello REVERB (default 0.3) suona davvero, quindi cambiano tutte e tre le impronte
        {
            struct Ref { const char* id; uint64_t a, b, c; };
            static const Ref refs[] = {
                { "dc3", 0xfc0f89e3a622bf5full, 0xd67bb19468f68d43ull, 0x0083f230894c6d44ull }, { "bf3", 0xd864318671fb3e9eull, 0x755c050ae929c188ull, 0x0ada0173397964d8ull },
                { "ph3", 0x6eaaf9c72048269bull, 0x3f5f8a15bf0e941eull, 0x19479aa9528c78e9ull }, { "dd3", 0xe871a6ed7c3c5c59ull, 0x637bc50eb7764950ull, 0xb48f8668c8588490ull },
                { "dd5", 0x5046891b5eba4039ull, 0xd3b1b1daf770e4d7ull, 0x5d11fa96f9e26791ull }, { "rv5", 0x1391d68f452bce73ull, 0x48dc6abfc82625bcull, 0xef1bd6551dfc8582ull },
                { "ac3", 0x1ca0de8d22294eb9ull, 0x4bdcee84ebcd996aull, 0xd5a5b370501fc9adull }, { "syb5", 0x9171cfef480ac8ebull, 0xf40dea716089dc13ull, 0xa026590176efa742ull } };
            static const Buf g1 = guitarS4 (2.0, 3), g2 = guitarS4 (1.5, 5), g3 = guitarS4 (1.0, 7);
            std::vector<int> h;
            for (const auto& r : refs)
            {
                h.push_back (lab.add (r.id, [] (const ModelDef& d)
                {
                    auto fx = makeAfter (d, {});
                    Buf l = g1, rr = g1;
                    for (auto& x : rr) x *= 0.8f;
                    runStereo256 (*fx, l, rr);
                    const uint64_t a = fnvBytes (rr, fnvBytes (l));
                    for (int k = 0; k < d.numControls; ++k)
                        if (d.controls[k].kind != ControlKind::Button) fx->params[k].store (std::fmod (d.controls[k].def + 0.37f, 1.0f));
                    Buf l2 = g2, r2 = g2;
                    runStereo256 (*fx, l2, r2);
                    const uint64_t b = fnvBytes (r2, fnvBytes (l2));
                    Buf l3 = g3, r3 = g3;
                    runStereo256 (*fx, l3, r3, {}, 1);
                    const uint64_t c = fnvBytes (r3, fnvBytes (l3));
                    return std::vector<double> { (double) (uint32_t) (a >> 32), (double) (uint32_t) a, (double) (uint32_t) (b >> 32), (double) (uint32_t) b,
                                                 (double) (uint32_t) (c >> 32), (double) (uint32_t) c };
                }));
                lab.first (h.back());
            }
            lab.check (h, "Tappa 5: DC-3, BF-3, PH-3, DD-3, DD-5, RV-5, AC-3, SYB-5 (config di partenza dei cloni digitali) invariati bit per bit", [&lab, h] {
                if (! kBitExactRefs) return Verdict { true, "impronte di riferimento misurate con GCC su Linux: confronto non applicabile a questo compilatore" };
                juce::String bad;
                for (size_t k = 0; k < h.size(); ++k)
                    if (hashOfS5 (lab, h[k], 0) != refs[k].a || hashOfS5 (lab, h[k], 2) != refs[k].b || hashOfS5 (lab, h[k], 4) != refs[k].c)
                        bad << refs[k].id << " DIVERSO (" << juce::String::toHexString ((juce::int64) hashOfS5 (lab, h[k], 0)) << " "
                            << juce::String::toHexString ((juce::int64) hashOfS5 (lab, h[k], 2)) << " " << juce::String::toHexString ((juce::int64) hashOfS5 (lab, h[k], 4)) << ") ";
                return Verdict { bad.isEmpty(), bad.isEmpty() ? juce::String ((int) h.size()) + "/" + juce::String ((int) h.size()) + " triple impronte identiche" : bad.trim() };
            });
        }

        // 1. i cloni avvolti (i5wrap) a comandi fermi danno la stessa uscita della config di partenza (frase di 3 s, bit per bit)
        {
            static std::deque<std::string> pool;
            std::vector<int> h;
            std::vector<juce::String> ids;
            for (int m = 0; m < numDig; ++m)
            {
                const auto* d = findModel (kStage5Digital[m]);
                if (d == nullptr || d->config == nullptr) continue;
                const std::string c (d->config);
                const auto p = c.find ("||");
                if (c.rfind ("type=i5wrap", 0) != 0 || p == std::string::npos) continue;
                std::string ic = c.substr (p + 3);
                for (size_t k; (k = ic.find ("itype=")) != std::string::npos;) ic.replace (k, 6, "type=");
                pool.push_back (ic);
                ModelDef inner = *d;
                inner.config = pool.back().c_str();
                auto run = [] (const ModelDef& dd)
                {
                    static const Buf g = guitarS4 (3.0, 13);
                    auto fx = makeAfter (dd, {});
                    Buf l = g, r = g;
                    runStereo256 (*fx, l, r, {}, dd.stereo ? 2 : 1);
                    const uint64_t v = fnvBytes (r, fnvBytes (l));
                    return std::vector<double> { (double) (uint32_t) (v >> 32), (double) (uint32_t) v };
                };
                h.push_back (lab.add (d->id, run));
                h.push_back (lab.add (inner, run));
                ids.push_back (d->id);
            }
            lab.check (h, "Tappa 5: i cloni avvolti (i5wrap: CD400, SF400, SP400, DD400, DD600, DR600, AM400, BSY600) a comandi fermi identici alla config di partenza",
                       [&lab, h, ids] {
                juce::String bad;
                int same = 0;
                for (size_t k = 0; k + 1 < h.size(); k += 2)
                    if (hashOfS5 (lab, h[k]) == hashOfS5 (lab, h[k + 1])) ++same; else bad << ids[k / 2] << " ";
                return Verdict { bad.isEmpty() && same == 8, juce::String (same) + "/" + juce::String ((int) ids.size()) + " identici " + bad.trim() };
            });
        }

        // 2. sanita' (banco s4 'sanity'): default, 3 regolazioni casuali, tutto a 1 e a 0, ogni posizione di ogni selettore; mono e stereo,
        //    blocchi da 512 e 64, frase di 2.5 s + silenzio fino a 6 s: finiti, picco <= 6.0 V
        {
            std::vector<int> h;
            std::vector<juce::String> what;
            for (int m = 0; m < numDig; ++m)
            {
                const auto* d = findModel (kStage5Digital[m]);
                if (d == nullptr) { h.push_back (lab.add (kStage5Digital[m], {})); what.push_back (kStage5Digital[m]); continue; }
                const auto sets = sanitySets (d, 0);
                for (size_t si = 0; si < sets.size(); ++si)
                {
                    const auto set = sets[si];
                    h.push_back (lab.add (kStage5Digital[m], [set, si] (const ModelDef& dd)
                    {
                        double pk = 0;
                        bool fin = true;
                        for (int ch = 1; ch <= 2; ++ch)
                        {
                            auto fx = createEffect (dd);
                            const int blk = si % 2 ? 64 : 512;
                            fx->prepare (48000.0, blk);
                            for (const auto& [c, v] : set) fx->params[c].store (v);
                            Buf l = guitarS4 (2.5, 11 + (uint32_t) si);
                            l.resize ((size_t) (6.0 * 48000.0), 0.0f);
                            Buf r = l;
                            for (size_t i = 0; i < l.size(); i += (size_t) blk)
                            {
                                const int nn = (int) std::min<size_t> ((size_t) blk, l.size() - i);
                                float* c2[2] = { l.data() + i, r.data() + i };
                                fx->process (c2, ch, nn);
                            }
                            for (size_t k = 0; k < l.size(); ++k)
                            {
                                fin = fin && std::isfinite (l[k]) && std::isfinite (r[k]);
                                pk = std::max (pk, (double) std::max (std::abs (l[k]), ch > 1 ? std::abs (r[k]) : 0.0f));
                            }
                        }
                        return std::vector<double> { fin ? pk : 999.0 };
                    }));
                    what.push_back (juce::String (kStage5Digital[m]) + "/" + juce::String ((int) si));
                }
            }
            lab.check (h, "Tappa 5 digitali: sanita' (34 modelli, " + juce::String ((int) h.size()) + " regolazioni, mono e stereo, blocchi 512 e 64): finiti, picco <= 6.0 V",
                       [&lab, h, what] {
                double worst = 0;
                juce::String bad, worstWhat;
                for (size_t k = 0; k < h.size(); ++k)
                {
                    const double p = lab.v (h[k]);
                    if (p > worst) { worst = p; worstWhat = what[k]; }
                    if (p > 6.0) bad << what[k] << " " << num (p, 2) << " V ";
                }
                return Verdict { bad.isEmpty(), "picco max " + num (worst, 3) + " V (" + worstWhat + ") " + bad.trim() };
            });
        }

        // 3. stabilita' con retroazione / decadimento al massimo (ogni modo dei delay e dei riverberi, tempo 1; 2 s di frase e 16 s di
        //    silenzio): finiti, picco <= 0.86 V
        {
            std::vector<int> h;
            std::vector<juce::String> what;
            for (int m = 0; m < numDig; ++m)
            {
                const auto* d = findModel (kStage5Digital[m]);
                if (d == nullptr || (d->family != Family::DigitalDelay && d->family != Family::Reverb)) continue;
                const int mc = roleIndex (*d, "mode");
                const int nm = mc >= 0 ? std::max (1, (int) d->controls[mc].steps) : 1;
                for (int k = 0; k < nm; ++k)
                {
                    if (mc >= 0 && d->controls[mc].choices != nullptr)
                    {
                        const auto names = juce::StringArray::fromTokens (d->controls[mc].choices, "|", "");
                        if (k < names.size() && names[k] == "HOLD") continue;
                    }
                    h.push_back (lab.add (d->id, [k, mc] (const ModelDef& dd) { return s5Stability (dd, mc >= 0 ? k : -1); }));
                    lab.first (h.back());
                    what.push_back (juce::String (d->id) + "/" + juce::String (k));
                }
            }
            lab.check (h, "Tappa 5 digitali: retroazione / decadimento al massimo in ogni modo dei delay e dei riverberi (" + juce::String ((int) h.size()) + " prove, "
                          "18 s): finiti, picco <= 0.86 V", [&lab, h, what] {
                double worst = 0;
                juce::String bad, worstWhat;
                for (size_t k = 0; k < h.size(); ++k)
                {
                    const bool fin = lab.v (h[k], 2) > 0.5;
                    const double p = fin ? lab.v (h[k], 0) : 999.0;
                    if (p > worst) { worst = p; worstWhat = what[k]; }
                    if (p > 0.86) bad << what[k] << " " << num (p, 2) << " V ";
                }
                return Verdict { bad.isEmpty(), "picco max " + num (worst, 3) + " V (" + worstWhat + ") " + bad.trim() };
            });
        }

        // 4. tempi di ritardo agli estremi del comando (gamma del modo; +-0.25 ms + 1%)
        {
            struct T { const char* id; int mode; double lo, hi; };
            const T ts[] = { { "bhdd400", 0, 325, 1300 }, { "bhdd400", 1, 125, 500 }, { "bhdd400", 2, 50, 200 },
                             { "bhdd600", 0, 1, 50 }, { "bhdd600", 1, 50, 200 }, { "bhdd600", 2, 200, 800 }, { "bhdd600", 3, 800, 2000 },
                             { "bhdr400", 0, 32, 128 }, { "bhdr400", 1, 128, 500 }, { "bhdr400", 2, 500, 2000 },
                             { "cvelemental", 0, 100, 2000 }, { "cvelemental", 2, 100, 3000 }, { "cvelemental", 5, 100, 2500 }, { "cvelemental", 8, 100, 1000 },
                             { "cvdigdelay", 0, 30, 1000 } };
            std::vector<int> h;
            for (const auto& x : ts)
                for (float t : { 0.0f, 1.0f })
                {
                    const int mode = x.mode;
                    h.push_back (lab.add (x.id, [mode, t] (const ModelDef& d) { return std::vector<double> { s5Delay (d, mode, t) }; }));
                }
            std::vector<T> copy (std::begin (ts), std::end (ts));
            lab.check (h, "Tappa 5 digitali: tempi di ritardo (DD400 325-1300 / 125-500 / 50-200, DD600 1-50 / 50-200 / 200-800 / 800-2000, DR400 32-128 / 128-500 / "
                          "500-2000, ELEMENTAL 100-2000 / 3000 / 2500 / 1000, DIG Delay 30-1000 ms; +-0.25 ms + 1%)", [&lab, h, copy] {
                bool ok = true;
                juce::String detail;
                for (size_t k = 0; k < copy.size(); ++k)
                {
                    const double a = lab.v (h[2 * k]), b = lab.v (h[2 * k + 1]);
                    const bool g = std::abs (a - copy[k].lo) <= 0.25 + 0.01 * copy[k].lo && std::abs (b - copy[k].hi) <= 0.25 + 0.01 * copy[k].hi;
                    ok = ok && g;
                    detail << copy[k].id << "/" << copy[k].mode << " " << num (a, 1) << "-" << num (b, 1) << (g ? "" : " (FUORI)") << "; ";
                }
                return Verdict { ok, detail.trimCharactersAtEnd ("; ") + " ms" };
            });
        }

        // 5. intonazione dei pitch (seno 220 Hz, YIN): SO400 ottave esatte (110 / 55 Hz), US600 i 12 intervalli entro 0.02 semitoni,
        //    DIG Pitch i 9 intervalli entro 0.02 semitoni (-3 -> -3.01)
        {
            struct P { const char* id; int shift, mode; double semis; };
            std::vector<P> ps { { "bhso400", -1, 0, -12.0 }, { "bhso400", -1, 1, -24.0 } };
            const double us[] = { -24, -12, -7, -5, -2, -1, 3, 4, 5, 7, 12, 24 }, dg[] = { -12, -7, -3, -1, 1, 3, 5, 7, 12 };
            for (int k = 0; k < 12; ++k) ps.push_back ({ "bhus600", k, 0, us[k] });
            for (int k = 0; k < 9; ++k) ps.push_back ({ "cvdigpitch", k, -1, dg[k] });
            std::vector<int> h;
            for (const auto& p : ps)
            {
                const int sh = p.shift, md = p.mode;
                h.push_back (lab.add (p.id, [sh, md] (const ModelDef& d) { return std::vector<double> { s5OutFreq (d, sh, md) }; }));
            }
            lab.check (h, "Pitch: SO400 220 Hz -> 110.00 / 55.00 Hz, US600 12 intervalli (-24..+24) e DIG Pitch 9 intervalli (-12..+12) entro 0.02 semitoni", [&lab, h, ps] {
                bool ok = true;
                double worst = 0;
                juce::String bad, ex;
                for (size_t k = 0; k < h.size(); ++k)
                {
                    const double f = lab.v (h[k]), st = f > 0 ? 12.0 * std::log2 (f / 220.0) : -99.0, dev = std::abs (st - ps[k].semis);
                    worst = std::max (worst, dev);
                    if (dev > 0.02) { ok = false; bad << ps[k].id << " " << num (ps[k].semis, 0) << ": " << num (st, 3) << " "; }
                }
                ex << "SO400 " << num (lab.v (h[0]), 2) << " / " << num (lab.v (h[1]), 2) << " Hz, DIG Pitch -3: " << num (12.0 * std::log2 (lab.v (h[16]) / 220.0), 2);
                return Verdict { ok, ex + ", scarto max " + num (worst, 3) + " semitoni " + bad.trim() };
            });
        }

        // 6. tempo di riverbero agli estremi del comando (rumore di 0.2 s, decadimento da -5 a -25 dB; +-10% + 0.06 s)
        {
            struct R { const char* id; int mode; float k; double rt; };
            const R rs[] = { { "bhdr600", 0, 0.0f, 0.24 }, { "bhdr600", 0, 1.0f, 6.06 }, { "bhdr400", 7, 0.0f, 0.30 }, { "bhdr400", 9, 1.0f, 5.76 },
                             { "bhrv600", 1, 0.0f, 0.18 }, { "bhrv600", 4, 1.0f, 11.40 }, { "cvdigreverb", 4, 0.0f, 0.18 }, { "cvdigreverb", 3, 1.0f, 9.24 },
                             { "cvminiuniverse", 1, 0.0f, 2.46 }, { "cvminiuniverse", 1, 1.0f, 13.32 }, { "cvminiuniverse", 5, 0.0f, 0.90 },
                             { "cvminiuniverse", 5, 1.0f, 16.32 } };
            std::vector<int> h;
            for (const auto& r : rs)
            {
                const int md = r.mode;
                const float kk = r.k;
                const double secs = std::min (30.0, 4.0 + 0.6 * r.rt);
                h.push_back (lab.add (r.id, [md, kk, secs] (const ModelDef& d) { return std::vector<double> { s5Rt (d, md, kk, secs) }; }));
                if (r.rt > 5.0) lab.first (h.back());
            }
            std::vector<R> copy (std::begin (rs), std::end (rs));
            lab.check (h, "Riverberi: RT60 DR600 HALL 0.24-6.1, DR400 0.3-5.8, RV600 0.18-11.4, DIG Reverb 0.18-9.2, MINI-UNIVERSE HALL 2.5-13.3 e CLOUD 0.9-16.3 s "
                          "(+-10% + 0.06 s)", [&lab, h, copy] {
                bool ok = true;
                juce::String detail;
                for (size_t k = 0; k < h.size(); ++k)
                {
                    const double v = lab.v (h[k]);
                    const bool g = std::abs (v - copy[k].rt) <= 0.1 * copy[k].rt + 0.06;
                    ok = ok && g;
                    detail << copy[k].id << "/" << copy[k].mode << " " << num (v) << (g ? "" : " (FUORI)") << "; ";
                }
                return Verdict { ok, detail.trimCharactersAtEnd ("; ") + " s" };
            });
        }

        // 7. IR generate: IR BOX (32 preset) col livello d'uscita fra -2 e -0.3 dB (frase di 4 s), cabinet delle Cube Baby normalizzati
        //    (media 150 Hz - 2.4 kHz della risposta all'impulso, dai 2 ai 9 cabinet) entro 2 dB
        {
            std::vector<int> hb;
            if (const auto* d = findModel ("cvirbox"))
            {
                const int cc = roleIndex (*d, "cab");
                const int ns = cc >= 0 ? (int) d->controls[cc].steps : 0;
                for (int k = 0; k < ns; ++k)
                    hb.push_back (lab.add ("cvirbox", [k, cc] (const ModelDef& dd)
                    {
                        static const Buf in = guitarS4 (4.0, 21);
                        const double rin = rmsOf (in);
                        auto fx = makeAfter (dd, {});
                        setStepIdxS5 (*fx, cc, k);
                        Buf l = in;
                        l.resize ((size_t) (5.0 * 48000), 0.0f);
                        Buf r = l;
                        runStereo256 (*fx, l, r);
                        return std::vector<double> { dbOf (rmsOf (l, 0, (size_t) (4.0 * 48000)) / rin) };
                    }));
            }
            else hb.push_back (lab.add ("cvirbox", {}));
            // "-2..-0.3 dB" di tests.md sono i valori del banco arrotondati a 0.1 dB (-1.98 / -0.27): +-0.05 dB
            lab.check (hb, "IR BOX: livello d'uscita dei 32 preset fra -2 e -0.3 dB (valori arrotondati a 0.1 dB)", [&lab, hb] {
                double lo = 1e9, hi = -1e9;
                for (int k : hb) { lo = std::min (lo, lab.v (k)); hi = std::max (hi, lab.v (k)); }
                return Verdict { hb.size() == 32 && lo >= -2.05 && hi <= -0.25, juce::String ((int) hb.size()) + " preset, " + num (lo) + " .. " + num (hi) + " dB" };
            });
            std::vector<int> hc;
            std::vector<juce::String> ids;
            for (const char* id : { "cvcubebaby", "cvcubebabybass", "cvcubebabyac" })
            {
                const auto* d = findModel (id);
                const int cc = d != nullptr ? roleIndex (*d, "cab") : -1;
                for (int k = 1; k < 9; ++k)
                {
                    hc.push_back (lab.add (id, [k, cc] (const ModelDef& dd)
                    {
                        auto fx = makeAfter (dd, {});
                        setStepIdxS5 (*fx, cc, k);
                        setRoleS4 (*fx, "gain", 0.0f); setRoleS4 (*fx, "locut", 0.0f); setRoleS4 (*fx, "hicut", 0.0f);
                        setRoleS4 (*fx, "reverb", 0.0f); setRoleS4 (*fx, "mix", 0.0f); setRoleS4 (*fx, "mod", 0.5f); setRoleS4 (*fx, "space", 0.5f);
                        const int n = 16384;
                        Buf l ((size_t) n, 0.0f);
                        l[4096] = 0.05f;
                        Buf r = l;
                        runStereo256 (*fx, l, r, {}, 1);
                        auto mag = [&l, n] (double f)
                        {
                            double re = 0, im = 0;
                            for (int i = 4096; i < n; ++i) { const double w = 2 * kPi * f * (i - 4096) / 48000.0; re += l[(size_t) i] * std::cos (w); im -= l[(size_t) i] * std::sin (w); }
                            return std::sqrt (re * re + im * im);
                        };
                        double ref = 0;
                        for (double f : { 150.0, 300.0, 600.0, 1200.0, 2400.0 }) ref += mag (f);
                        return std::vector<double> { dbOf (ref / 5.0 / 0.05) };
                    }));
                    ids.push_back (juce::String (id) + "/" + juce::String (k + 1));
                }
            }
            lab.check (hc, "Cube Baby / Bass / AC: livello dei cabinet generati (media 150 Hz - 2.4 kHz) entro 2 dB fra i cabinet di ciascun pedale", [&lab, hc, ids] {
                bool ok = true;
                juce::String detail;
                for (size_t p = 0; p < 3; ++p)
                {
                    double lo = 1e9, hi = -1e9;
                    for (size_t k = 0; k < 8; ++k) { lo = std::min (lo, lab.v (hc[p * 8 + k])); hi = std::max (hi, lab.v (hc[p * 8 + k])); }
                    ok = ok && hi - lo <= 2.0;
                    detail << ids[p * 8].upToFirstOccurrenceOf ("/", false, false) << " " << num (lo, 1) << " .. " << num (hi, 1) << " dB; ";
                }
                return Verdict { ok, detail.trimCharactersAtEnd ("; ") };
            });
        }

        // 8. livelli col riff a -12 dBFS e i comandi di default; comandi al massimo e al minimo
        {
            const auto hl = addLevelsS5 (lab, kStage5Digital, numDig, "Tappa 5 digitali");
            // banco s4 ('level'): uscita / ingresso da -12 dB (AM400, come l'AC-3) a +17.1 dB (BSY600, synth a livello fisso come il SYB-5); col riff
            // a -28.65 dBFS di RMS: -40.65..-11.55 dBFS, +-1.5 dB
            checkLevelsS5 (lab, hl, kStage5Digital, numDig, "Tappa 5 digitali: livelli con riff a -12 dBFS e comandi di default (34 modelli: finiti, picco < +6 dBFS, "
                                                            "RMS -42..-10 dBFS come il banco: AM400 -12 dB, BSY600 +17 dB)", -42.0, -10.0, 6.0);
        }

        // click: soglia 10x senza eccezioni (misurati al massimo 4.97x, TYPE dell'ELEMENTAL); determinismo fra due istanze nuove, dopo reset()
        // non richiesto (CD400, SF400, SP400, DR600 e BSY600 come le famiglie di partenza)
        addClickAndDeterminism (lab, kStage5Digital, numDig, "Tappa 5 digitali", 10.0, false);
        lab.run (report);
    }

    /** 1.3.1: involucro senza click delle famiglie della prima versione del motore (Effects.cpp, SmoothFamily), dissolvenze
        del MUTE dei tuner e di LEVEL/MIX degli IR loader, modi separati della famiglia Acoustic. */
    void runSmoothFamilyTests (const std::function<void (const juce::String&, bool, const juce::String&)>& report)
    {
        using namespace pt::engine;
        const int nm = numModels();

        // 1. a comandi fermi l'involucro non cambia niente: uscita identica bit per bit all'effetto della famiglia senza
        //    involucro (createEffectRaw), con i comandi scritti prima di prepare, dopo prepare e prima del primo blocco,
        //    dopo un reset, mono e stereo, blocchi da 64/256/480. Confronto interno (stesso compilatore): vale ovunque
        {
            const Buf riff = guitarS1 (true, 48000.0, 1.0, -12.0), chord = guitarS1 (false, 48000.0, 1.0, -6.0);
            std::vector<int> bad ((size_t) nm, 0);
            parallelFor (nm, [&] (int i)
            {
                const ModelDef& d = model (i);
                if (d.family == Family::Nam) return;
                std::mt19937 rng ((unsigned) (77 + i));
                std::uniform_real_distribution<float> U (0, 1);
                std::vector<float> rnd ((size_t) d.numControls);
                for (auto& v : rnd) v = U (rng);
                auto render = [&] (bool raw, int kase)
                {
                    auto fx = raw ? createEffectRaw (d) : createEffect (d);
                    const int blk = kase == 0 ? 256 : kase == 1 ? 64 : 480;
                    const int nch = kase == 1 ? 1 : 2;
                    if (kase == 1) for (int k = 0; k < d.numControls; ++k) fx->params[k].store (rnd[(size_t) k]);
                    fx->prepare (48000.0, blk);
                    if (kase == 2) for (int k = 0; k < d.numControls; ++k) fx->params[k].store (1.0f - rnd[(size_t) k]);
                    std::vector<Buf> ch ((size_t) nch, riff);
                    if (nch == 2) for (auto& v : ch[1]) v *= 0.7f;
                    auto run = [&] (std::vector<Buf>& c)
                    {
                        std::vector<float*> p (c.size());
                        for (size_t pos = 0; pos < c[0].size(); pos += (size_t) blk)
                        {
                            const int n = (int) std::min ((size_t) blk, c[0].size() - pos);
                            for (size_t q = 0; q < c.size(); ++q) p[q] = c[q].data() + pos;
                            fx->process (p.data(), (int) c.size(), n);
                        }
                    };
                    run (ch);
                    if (kase == 2)
                    {
                        fx->reset();
                        std::vector<Buf> c2 (2, chord);
                        run (c2);
                        ch.insert (ch.end(), c2.begin(), c2.end());
                    }
                    return ch;
                };
                for (int kase = 0; kase < 3; ++kase)
                    if (render (false, kase) != render (true, kase)) bad[(size_t) i] |= 1 << kase;
            });
            juce::String detail;
            int nBad = 0;
            for (int i = 0; i < nm; ++i)
                if (bad[(size_t) i] != 0) { ++nBad; if (nBad <= 6) detail << model (i).id << "(" << bad[(size_t) i] << ") "; }
            report ("Involucro senza click: a comandi fermi uscita identica bit per bit alla famiglia senza involucro (tutti i modelli)",
                    nBad == 0, nBad == 0 ? juce::String (nm) + " modelli, 3 casi ciascuno" : juce::String (nBad) + " diversi: " + detail.trim());
        }

        // 2. click dei comandi su TUTTO il catalogo (clicksS2: ogni pomello a 0.9/0.1/default, ogni posizione di ogni
        //    selettore e levetta): <= 10x senza eccezioni. Fino alla 1.3.0 le famiglie vecchie arrivavano a 2360x (PQ-4),
        //    i tuner a 543x (MUTE) e gli IR loader a 526x (LEVEL)
        {
            std::vector<ClickRes> res ((size_t) nm);
            parallelFor (nm, [&] (int i) { res[(size_t) i] = clicksS2 (model (i).id); });
            std::vector<int> order;
            int over = 0, found = 0;
            for (int i = 0; i < nm; ++i)
            {
                if (! res[(size_t) i].found) continue;
                ++found;
                if (res[(size_t) i].worst > 10.0) ++over;
                order.push_back (i);
            }
            std::sort (order.begin(), order.end(), [&] (int a, int b) { return res[(size_t) a].worst > res[(size_t) b].worst; });
            juce::String detail;
            detail << found << " modelli, oltre 10x: " << over << "; peggiori:";
            for (int k = 0; k < std::min (5, (int) order.size()); ++k)
                detail << " " << model (order[(size_t) k]).id << " " << juce::String (res[(size_t) order[(size_t) k]].worst, 1) << "x ("
                       << res[(size_t) order[(size_t) k]].what << ")";
            report ("Click dei comandi <= 10x su tutto il catalogo (clicksS2)", over == 0 && found == nm, detail);
        }

        // 3. famiglia Acoustic: ogni modo ha le sue risonanze (fino alla 1.3.0 la config si fermava al primo ';' e tutti i
        //    modi suonavano come il primo). Guadagno a 95 / 190 / 700 / 1500 / 3500 Hz con BODY/TOP a meta': i modi sono
        //    tutti diversi fra loro (almeno 0.5 dB a una frequenza) e ENHANCE/BRIGHT ha i 3.5 kHz piu' alti di STANDARD
        {
            auto gainAt = [] (const ModelDef& d, int mode, double f)
            {
                auto fx = createEffect (d);
                for (int k = 0; k < d.numControls; ++k)
                    if (std::strcmp (d.controls[k].role, "mode") == 0)
                        fx->params[k].store (d.controls[k].steps > 1 ? (float) mode / (float) (d.controls[k].steps - 1) : 0.0f);
                fx->prepare (48000.0, 256);
                Buf x (24000);
                for (size_t i = 0; i < x.size(); ++i) x[i] = (float) (0.1 * std::sin (2 * kPi * f * (double) i / 48000.0));
                processMono (*fx, x, 256);
                double e = 0;
                for (size_t i = 12000; i < x.size(); ++i) e += (double) x[i] * x[i];
                return dbOf (std::sqrt (e / 12000.0) / (0.1 / std::sqrt (2.0)));
            };
            bool ok = true;
            juce::String detail;
            for (const char* id : { "ac2", "ac3", "bham400" })
            {
                const auto* d = findModel (id);
                if (d == nullptr) { ok = false; detail << id << " assente "; continue; }
                int modeCtl = -1;
                for (int k = 0; k < d->numControls; ++k) if (std::strcmp (d->controls[k].role, "mode") == 0) modeCtl = k;
                const int nModes = modeCtl >= 0 ? std::max (1, (int) d->controls[modeCtl].steps) : 1;
                static const double fr[5] = { 95.0, 190.0, 700.0, 1500.0, 3500.0 };
                std::vector<std::array<double, 5>> g ((size_t) nModes);
                for (int m = 0; m < nModes; ++m)
                    for (int k = 0; k < 5; ++k) g[(size_t) m][(size_t) k] = gainAt (*d, m, fr[k]);
                for (int a = 0; a < nModes; ++a)
                    for (int b = a + 1; b < nModes; ++b)
                    {
                        double diff = 0;
                        for (int k = 0; k < 5; ++k) diff = std::max (diff, std::abs (g[(size_t) a][(size_t) k] - g[(size_t) b][(size_t) k]));
                        if (diff < 0.5) { ok = false; detail << id << " modi " << a << "/" << b << " uguali "; }
                    }
                // modo ENHANCE/BRIGHT (ac2: 1, ac3: 2, bham400: 3): 3.5 kHz almeno 3 dB sopra STANDARD (modo 0)
                const int enh = std::strcmp (id, "ac2") == 0 ? 1 : std::strcmp (id, "ac3") == 0 ? 2 : 3;
                if (! (enh < nModes && g[(size_t) enh][4] > g[0][4] + 3.0)) { ok = false; detail << id << " ENHANCE non piu' brillante "; }
                detail << id << ":";
                for (int m = 0; m < nModes; ++m)
                {
                    detail << " [";
                    for (int k = 0; k < 5; ++k) detail << (k > 0 ? "/" : "") << juce::String (g[(size_t) m][(size_t) k] - g[0][(size_t) k], 1);
                    detail << "]";
                }
                detail << " dB rispetto al modo 0; ";
            }
            report ("Acoustic (AC-2, AC-3, AM400): ogni modo con le sue risonanze (95 Hz - 3.5 kHz per modo)", ok, detail.trim());
        }

        // AC-3: il pomello REVERB (fino alla 1.3.0 letto ma non usato) aggiunge una coda d'ambiente; a zero il percorso e' quello di prima
        {
            const auto* d = findModel ("ac3");
            bool ok = d != nullptr;
            juce::String detail;
            if (d != nullptr)
            {
                int rc = -1;
                for (int k = 0; k < d->numControls; ++k) if (std::strcmp (d->controls[k].role, "reverb") == 0) rc = k;
                auto tail = [d, rc] (float rv, double& peak)
                {
                    auto fx = createEffect (*d);
                    fx->prepare (48000.0, 256);
                    if (rc >= 0) fx->params[rc].store (rv);
                    Buf x (48000);
                    for (size_t i = 0; i < 14400; ++i) x[i] = (float) (0.2 * std::sin (2 * kPi * 220.0 * (double) i / 48000.0));
                    processMono (*fx, x, 256);
                    double e = 0; peak = 0;
                    for (size_t i = 0; i < x.size(); ++i) peak = std::max (peak, (double) std::abs (x[i]));
                    for (size_t i = 14400 + 2400; i < 14400 + 14400; ++i) e += (double) x[i] * x[i];      // 50-300 ms dopo la fine
                    return dbOf (std::sqrt (e / 12000.0) + 1e-12);
                };
                double p0 = 0, p1 = 0;
                const double t0 = tail (0.0f, p0), t1 = tail (1.0f, p1);
                ok = rc >= 0 && t1 > t0 + 60.0 && t1 > -60.0 && p1 < 2.0 && std::isfinite (t1);    // a zero resta solo la coda dei filtri di risonanza
                detail << "coda 50-300 ms: REVERB 0 " << juce::String (t0, 1) << " dB, REVERB 10 " << juce::String (t1, 1)
                       << " dB (> -60 e almeno 60 dB sopra); picco " << juce::String (p1, 2) << " V";
            }
            report ("AC-3: REVERB aggiunge una coda d'ambiente, a zero nessuna coda", ok, detail);
        }

        // RV-200 REVERSE: alla fine di ogni swell (0.45 s) il guadagno scende in ~3 ms, senza gradino (fino alla 1.3.0 da 1 a 0 in un campione)
        {
            const auto* d = findModel ("rv200");
            bool ok = d != nullptr;
            juce::String detail;
            if (d != nullptr)
            {
                int mc = -1, lc = -1;
                for (int k = 0; k < d->numControls; ++k)
                {
                    if (std::strcmp (d->controls[k].role, "mode") == 0) mc = k;
                    if (std::strcmp (d->controls[k].role, "level") == 0) lc = k;
                }
                auto fx = createEffect (*d);
                fx->prepare (48000.0, 256);
                if (mc >= 0) fx->params[mc].store (1.0f);                 // REVERSE (ultima posizione)
                if (lc >= 0) fx->params[lc].store (1.0f);
                Buf x (96000), dry (96000);
                for (size_t i = 0; i < x.size(); ++i) dry[i] = x[i] = (float) (0.2 * std::sin (2 * kPi * 220.0 * (double) i / 48000.0));
                processMono (*fx, x, 256);
                // solo il riverbero: differenza seconda dell'uscita meno il diretto; gradino = picco rispetto al 99.9 percentile
                std::vector<double> d2;
                for (size_t i = 24002; i < x.size(); ++i)
                {
                    const double w0 = x[i] - dry[i], w1 = x[i - 1] - dry[i - 1], w2 = x[i - 2] - dry[i - 2];
                    d2.push_back (std::abs (w0 - 2 * w1 + w2));
                }
                auto sorted = d2;
                std::sort (sorted.begin(), sorted.end());
                const double p999 = std::max (1e-9, sorted[(size_t) (0.999 * (double) (sorted.size() - 1))]), mx = sorted.back();
                const double ratio = mx / p999;
                ok = mc >= 0 && ratio < 3.0 && std::isfinite (mx);
                detail << "picco della differenza seconda / 99.9 percentile = " << juce::String (ratio, 2) << " (< 3)";
            }
            report ("RV-200 REVERSE: fine dello swell senza gradino", ok, detail);
        }
    }

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
        runRoutingTests (report);
        runMidiTests (report);
        runWahTests (report);
        runCabIRTests (report);
        runStage3AnalogTests (report);
        runStage3DigitalTests (report);
        runStage3BAnalogTests (report);
        runStage3BDigitalTests (report);
        runStage4DriveTests (report);
        runStage4DynTests (report);
        runStage4ModTests (report);
        runStage4DigitalTests (report);
        runStage5DriveTests (report);
        runStage5DynTests (report);
        runStage5ModTests (report);
        runStage5DigitalTests (report);
        runSmoothFamilyTests (report);

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
        bool info = false, options = false, midiPanel = false, learn = false, routingMap = false;
        float routeZoom = 0.0f;
        juce::StringArray routeMoves;
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
            else if (args[i] == "--routing") routingMap = true;
            else if (args[i] == "--routezoom") routeZoom = next.getFloatValue();
            else if (args[i] == "--routemove") routeMoves.add (next);
            else if (args[i] == "--backimage")
            {
                // immagine di prova del retro (non nel programma): vale per qualsiasi modello
                const auto f = juce::File::getCurrentWorkingDirectory().getChildFile (next.unquoted());
                const auto img = juce::ImageFileFormat::loadFrom (f);
                if (! img.isValid()) std::cerr << "Immagine del retro non valida: " << f.getFullPathName() << std::endl;
                else pt::ui::setBackImageLookup ([img] (const juce::String&) { return img; });
            }
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
        if (zoomSlot >= 0 && ! routingMap) editor->showZoom (zoomSlot);
        if (routingMap)
        {
            // posizioni di prova (coordinate della mappa) e zoom, poi la mappa sotto lo zoom eventuale
            for (const auto& m : routeMoves)
            {
                const auto who = m.upToFirstOccurrenceOf (":", false, false).trim();
                const auto xy = m.fromFirstOccurrenceOf (":", false, false);
                const juce::Point<float> pos (xy.upToFirstOccurrenceOf (",", false, false).getFloatValue(), xy.fromFirstOccurrenceOf (",", false, false).getFloatValue());
                if (who == "in") p.routeLayout.setIoPos (0, pos);
                else if (who == "out") p.routeLayout.setIoPos (1, pos);
                else p.routeLayout.setSlotPos (who.getIntValue(), pos);
            }
            if (routeZoom > 0.0f) p.routeLayout.setView (routeZoom, {});
            editor->showRouting (true);
            if (zoomSlot >= 0) editor->showZoom (zoomSlot);       // doppio clic dalla mappa: zoom sopra la mappa
        }
        if (zoomSlot >= 0 && args.contains ("--back"))
            if (auto* z = editor->zoomView()) z->setShowBack (true, false);
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
