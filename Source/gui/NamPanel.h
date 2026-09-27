/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Pannello NAM nel pannello di zoom del NAM-A1A2 Model (a destra del pedale):
      - canale A e canale B: file del modello (.nam / .namb) e IR (.wav / .aiff),
        con verifica di sicurezza e messaggio di esito; pomello SLIM attivo solo
        se il modello caricato e' slimmable (A2: da Lite a Full);
      - calibrazione come nel plugin NAM: calibra ingresso + livello in dBu,
        uscita Raw / Normalized / Calibrated;
      - noise gate (soglia), tonestack e IR attivabili.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"
#include "Controls.h"
#include "../engine/Chain.h"

namespace pt::engine { class NamEffect; }

namespace pt::ui
{
    class NamPanel : public juce::Component, private juce::Timer
    {
    public:
        NamPanel (engine::Chain&, int slotIndex);
        ~NamPanel() override;
        void bindTo (int slotIndex);
        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        struct ChannelUi;
        class SlimKnob;
        void timerCallback() override;
        engine::NamEffect* effect() const;
        void refresh();
        void pushOptions();
        void choose (int channel, bool model);

        engine::Chain& chain;
        int index;
        std::shared_ptr<engine::Slot> slotRef;
        juce::SharedResourcePointer<ThemeManager> themes;
        std::unique_ptr<ChannelUi> chans[2];
        juce::Label title, calTitle;
        juce::ToggleButton calibrate { "Calibra ingresso" }, gate { "Noise gate" }, eq { "Tonestack (EQ)" }, irOn { "IR attivi" };
        juce::Slider calLevel, gateThreshold;
        juce::Label calLevelLabel, gateLabel, outputLabel;
        juce::ComboBox outputMode;
        std::unique_ptr<juce::FileChooser> chooser;
        bool updating = false;
    };
}
