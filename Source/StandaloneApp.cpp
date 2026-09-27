/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Applicazione Standalone personalizzata:
      * finestra con titolo "Pedal Trinity 1.0.0 beta";
      * su Windows, al primo avvio, seleziona i driver ASIO se presenti;
      * ingresso audio attivo di default (e' un effetto per chitarra);
      * riga di comando:
          --selftest                         verifica automatica del DSP
          --screenshot file.png [opzioni]    salva un'immagine dell'interfaccia
              --scale 1.0   --info   --set id=valore (ripetibile)
*/

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <iostream>

#if JucePlugin_Build_Standalone && JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP

#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Version.h"
#include "engine/Circuit.h"

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

        // 3. processore completo e bypass
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

    //==============================================================================
    // Screenshot dell'interfaccia (per la guida PDF)
    //   --screenshot file.png [--scale s] [--size WxH] [--view n] [--first i] [--factory k]
    //   [--zoom slot] [--info] [--chain id,id,...] [--set slot:ctrl=val]
    //==============================================================================
    int runScreenshot (const juce::StringArray& args)
    {
        const int idx = args.indexOf ("--screenshot");
        if (idx < 0 || idx + 1 >= args.size()) return 2;
        const juce::File outFile = juce::File::getCurrentWorkingDirectory().getChildFile (args[idx + 1].unquoted());

        PedalTrinityProcessor p;
        float scale = 1.0f;
        int w = 1280, h = 760, zoomSlot = -1;
        bool info = false;
        for (int i = 0; i < args.size(); ++i)
        {
            const auto next = i + 1 < args.size() ? args[i + 1] : juce::String();
            if (args[i] == "--scale") scale = next.getFloatValue();
            else if (args[i] == "--size") { w = next.upToFirstOccurrenceOf ("x", false, false).getIntValue(); h = next.fromFirstOccurrenceOf ("x", false, false).getIntValue(); }
            else if (args[i] == "--view") p.uiState.setProperty ("view", next.getIntValue(), nullptr);
            else if (args[i] == "--first") p.uiState.setProperty ("first", next.getIntValue(), nullptr);
            else if (args[i] == "--factory") p.presets.loadFactory (next.getIntValue());
            else if (args[i] == "--zoom") zoomSlot = next.getIntValue();
            else if (args[i] == "--info") info = true;
            else if (args[i] == "--chain")
            {
                juce::ValueTree t ("CHAIN");
                for (auto& id : juce::StringArray::fromTokens (next, ",", ""))
                {
                    juce::ValueTree s ("SLOT");
                    s.setProperty ("model", id, nullptr);
                    s.setProperty ("on", true, nullptr);
                    t.appendChild (s, nullptr);
                }
                p.chain.fromValueTree (t);
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
        if (info) editor->showInfo (true);
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
