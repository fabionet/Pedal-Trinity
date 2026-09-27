/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Pannelli laterali INPUT / OUTPUT: meter di picco mono o stereo (secondo
    lo splitter), fader del volume con il cappuccio 3D dei cursori e, in
    uscita, i pomelli BALANCE / LEFT / RIGHT quando lo splitter e' in DUAL
    o STEREO. Le prese jack del pannello sono sul lato rivolto ai pedali.
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Controls.h"
#include "../engine/Chain.h"

namespace pt::ui
{
    class MeterPanel : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        enum class Side { Input, Output };

        MeterPanel (Side, juce::AudioProcessorValueTreeState&, engine::LevelMeter&);
        ~MeterPanel() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        /** Legge i picchi (chiamato dal timer della pedaliera). */
        void tick();
        /** Canali mostrati (1 = mono, 2 = A/B o L/R), secondo lo splitter. */
        void setChannels (int);
        /** Mostra i comandi d'uscita dello splitter (solo pannello OUTPUT). */
        void setSplitControlsVisible (bool);
        /** Altezze (coordinate del pannello) delle prese A e B; B < 0 = assente. */
        void setSockets (float yA, float yB, float scale);
        /** Bocca della presa (coordinate del pannello). */
        juce::Point<float> socketPoint (int line) const;

    private:
        class Canvas;
        void bind (BoundControl&, const juce::String& paramId);

        Side side;
        juce::AudioProcessorValueTreeState& apvts;
        engine::LevelMeter& meter;
        juce::SharedResourcePointer<Assets> assets;
        juce::SharedResourcePointer<ThemeManager> themes;
        engine::ControlDef faderDef {}, knobDefs[3] {};
        std::unique_ptr<Canvas> canvas;
        std::unique_ptr<FaderControl> fader;
        std::unique_ptr<KnobControl> knobs[3];
        juce::Label value;

        float level[2] { 0.0f, 0.0f }, hold[2] { 0.0f, 0.0f };
        int holdTicks[2] { 0, 0 };
        int channels = 1;
        bool splitControls = false;
        float sockY[2] { -1.0f, -1.0f }, sockScale = 1.0f;
        juce::Rectangle<int> meterArea, knobArea;
    };
}
