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

namespace
{
    //==============================================================================
    // Autotest del DSP
    //==============================================================================
    struct TestResult { juce::String name; bool ok; juce::String detail; };

    float rmsDb (const juce::AudioBuffer<float>& b, int ch, int start, int len)
    {
        return juce::Decibels::gainToDecibels (b.getRMSLevel (ch, start, len), -200.0f);
    }

    /** Processa un seno e restituisce l'uscita (ultimo secondo, a regime). */
    juce::AudioBuffer<float> render (PedalTrinityProcessor& p, float freq, float amp, double sr = 48000.0,
                                     int block = 256, int seconds = 2)
    {
        p.setPlayConfigDetails (2, 2, sr, block);
        p.prepareToPlay (sr, block);
        const int total = (int) sr * seconds;
        juce::AudioBuffer<float> out (2, total);
        juce::MidiBuffer midi;
        juce::AudioBuffer<float> buf (2, block);
        for (int pos = 0; pos < total; pos += block)
        {
            const int n = juce::jmin (block, total - pos);
            buf.setSize (2, n, false, false, true);
            for (int i = 0; i < n; ++i)
            {
                const float s = amp * std::sin (2.0f * juce::MathConstants<float>::pi * freq * (float) (pos + i) / (float) sr);
                buf.setSample (0, i, s);
                buf.setSample (1, i, s);
            }
            p.processBlock (buf, midi);
            for (int c = 0; c < 2; ++c)
                out.copyFrom (c, pos, buf, c, 0, n);
        }
        return out;
    }

    void setParam (PedalTrinityProcessor& p, const juce::String& id, float value)
    {
        if (auto* param = p.apvts.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
    }

    bool allFinite (const juce::AudioBuffer<float>& b)
    {
        for (int c = 0; c < b.getNumChannels(); ++c)
            for (int i = 0; i < b.getNumSamples(); ++i)
                if (! std::isfinite (b.getSample (c, i)))
                    return false;
        return true;
    }

    int runSelfTest()
    {
        std::vector<TestResult> results;
        auto add = [&] (juce::String n, bool ok, juce::String d) { results.push_back ({ n, ok, d }); };
        const int sr = 48000;

        {   // tutto spento = bypass perfetto
            PedalTrinityProcessor p;
            setParam (p, pt::ids::odOn, 0); setParam (p, pt::ids::distOn, 0); setParam (p, pt::ids::eqOn, 0);
            auto out = render (p, 440.0f, 0.5f);
            float maxErr = 0;
            for (int i = sr; i < out.getNumSamples(); ++i)
            {
                const float ref = 0.5f * std::sin (2.0f * juce::MathConstants<float>::pi * 440.0f * (float) i / (float) sr);
                maxErr = juce::jmax (maxErr, std::abs (out.getSample (0, i) - ref));
            }
            add ("Bypass totale trasparente", maxErr < 1.0e-6f, "errore max " + juce::String (maxErr, 9));
        }
        {   // overdrive: saturazione, livello ragionevole, niente NaN
            PedalTrinityProcessor p;
            setParam (p, pt::ids::eqOn, 0);
            setParam (p, pt::ids::odDrive, 10.0f);
            auto out = render (p, 110.0f, 0.3f);
            const float lvl = rmsDb (out, 0, sr, sr);
            add ("Overdrive drive max: uscita finita e ragionevole", allFinite (out) && lvl > -30.0f && lvl < 6.0f,
                 "RMS " + juce::String (lvl, 1) + " dBFS");
            setParam (p, pt::ids::odDrive, 0.0f);
            auto clean = render (p, 110.0f, 0.3f);
            const float lvlClean = rmsDb (clean, 0, sr, sr);
            add ("Overdrive: il drive aumenta la saturazione", lvl > lvlClean - 3.0f, juce::String (lvlClean, 1) + " -> " + juce::String (lvl, 1) + " dB");
        }
        {   // distorsore modo S e C
            for (int mode = 0; mode < 2; ++mode)
            {
                PedalTrinityProcessor p;
                setParam (p, pt::ids::odOn, 0); setParam (p, pt::ids::eqOn, 0); setParam (p, pt::ids::distOn, 1);
                setParam (p, pt::ids::distMode, (float) mode);
                setParam (p, pt::ids::distGain, 10.0f);
                setParam (p, pt::ids::distLow, 15.0f); setParam (p, pt::ids::distHigh, 15.0f); setParam (p, pt::ids::distMid, 15.0f);
                auto out = render (p, 82.4f, 0.2f);
                const float lvl = rmsDb (out, 0, sr, sr);
                add (juce::String ("Distorsore modo ") + (mode ? "C" : "S") + ", tutto al massimo",
                     allFinite (out) && lvl > -30.0f && lvl < 12.0f, "RMS " + juce::String (lvl, 1) + " dBFS");
            }
        }
        {   // EQ: +15 dB sulla banda a 800 Hz
            PedalTrinityProcessor p;
            setParam (p, pt::ids::odOn, 0); setParam (p, pt::ids::distOn, 0); setParam (p, pt::ids::eqOn, 1);
            auto flat = render (p, 800.0f, 0.1f);
            setParam (p, pt::ids::eqBand (3), 15.0f);
            auto boosted = render (p, 800.0f, 0.1f);
            const float gain = rmsDb (boosted, 0, sr, sr) - rmsDb (flat, 0, sr, sr);
            add ("EQ banda 800 Hz a +15 dB", std::abs (gain - 15.0f) < 0.6f, "guadagno misurato " + juce::String (gain, 2) + " dB");
            setParam (p, pt::ids::eqBand (3), 0.0f);
            setParam (p, pt::ids::eqLevel, -6.0f);
            auto lower = render (p, 800.0f, 0.1f);
            const float lv = rmsDb (lower, 0, sr, sr) - rmsDb (flat, 0, sr, sr);
            add ("EQ level a -6 dB", std::abs (lv + 6.0f) < 0.2f, "misurato " + juce::String (lv, 2) + " dB");
        }
        {   // catena completa a varie frequenze di campionamento e blocchi irregolari
            bool ok = true;
            juce::String info;
            for (double rate : { 44100.0, 48000.0, 96000.0 })
            {
                PedalTrinityProcessor p;
                setParam (p, pt::ids::distOn, 1);
                auto out = render (p, 196.0f, 0.4f, rate, 97, 1);
                ok = ok && allFinite (out);
                info << juce::String (rate / 1000.0, 1) << "k:" << juce::String (rmsDb (out, 0, 0, out.getNumSamples()), 1) << "dB ";
            }
            add ("Catena completa (44.1/48/96 kHz, blocchi da 97)", ok, info);
        }
        {   // mono
            PedalTrinityProcessor p;
            p.setPlayConfigDetails (1, 1, 48000.0, 128);
            juce::AudioProcessor::BusesLayout mono;
            mono.inputBuses.add (juce::AudioChannelSet::mono());
            mono.outputBuses.add (juce::AudioChannelSet::mono());
            const bool layoutOk = p.setBusesLayout (mono);
            p.prepareToPlay (48000.0, 128);
            juce::AudioBuffer<float> b (1, 128);
            juce::MidiBuffer m;
            bool fin = true;
            for (int k = 0; k < 400; ++k)
            {
                for (int i = 0; i < 128; ++i)
                    b.setSample (0, i, 0.3f * std::sin ((float) (k * 128 + i) * 0.05f));
                p.processBlock (b, m);
                fin = fin && allFinite (b);
            }
            add ("Configurazione mono", layoutOk && fin, layoutOk ? "ok" : "layout rifiutato");
        }
        {   // salvataggio / ripristino dello stato
            PedalTrinityProcessor a, b;
            setParam (a, pt::ids::odDrive, 7.3f);
            setParam (a, pt::ids::distMidFreq, 2500.0f);
            juce::MemoryBlock state;
            a.getStateInformation (state);
            b.setStateInformation (state.getData(), (int) state.getSize());
            const float d = b.apvts.getRawParameterValue (pt::ids::odDrive)->load();
            const float f = b.apvts.getRawParameterValue (pt::ids::distMidFreq)->load();
            add ("Salvataggio/ripristino preset", std::abs (d - 7.3f) < 0.01f && std::abs (f - 2500.0f) < 1.0f,
                 "drive " + juce::String (d, 2) + ", mid freq " + juce::String (f, 0));
        }

        int failed = 0;
        std::cout << "\nPedal Trinity " << pt::versionString << " - autotest DSP\n";
        for (auto& r : results)
        {
            std::cout << (r.ok ? "  [OK]    " : "  [FALLITO] ") << r.name << "  (" << r.detail << ")\n";
            failed += r.ok ? 0 : 1;
        }
        std::cout << (failed == 0 ? "Tutti i test superati.\n" : "Alcuni test sono falliti.\n") << std::flush;
        return failed == 0 ? 0 : 1;
    }

    //==============================================================================
    // Screenshot dell'interfaccia (per la guida PDF)
    //==============================================================================
    int runScreenshot (const juce::StringArray& args)
    {
        const int idx = args.indexOf ("--screenshot");
        if (idx < 0 || idx + 1 >= args.size())
            return 2;
        const juce::File outFile = juce::File::getCurrentWorkingDirectory().getChildFile (args[idx + 1].unquoted());

        PedalTrinityProcessor p;
        float scale = 1.0f;
        bool info = false;
        for (int i = 0; i < args.size(); ++i)
        {
            if (args[i] == "--scale" && i + 1 < args.size())
                scale = args[i + 1].getFloatValue();
            else if (args[i] == "--info")
                info = true;
            else if (args[i] == "--set" && i + 1 < args.size())
                setParam (p, args[i + 1].upToFirstOccurrenceOf ("=", false, false),
                          args[i + 1].fromFirstOccurrenceOf ("=", false, false).getFloatValue());
        }

        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        auto* editor = dynamic_cast<PedalTrinityEditor*> (ed.get());
        if (editor == nullptr)
            return 3;
        if (info)
            editor->showInfo (true);
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
