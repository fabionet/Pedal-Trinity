/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Controlli "fotografici": ogni componente disegna i fotogrammi 3D
    renderizzati e reagisce solo sulla zona reale del pomello/cursore.
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Assets.h"

namespace pt::ui
{
    /** LookAndFeel per il fumetto con il valore e per i cursori. */
    class PedalLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        PedalLookAndFeel();
        void drawBubble (juce::Graphics&, juce::BubbleComponent&, const juce::Point<float>& tip,
                         const juce::Rectangle<float>& body) override;
        juce::Font getSliderPopupFont (juce::Slider&) override;
        int getSliderPopupPlacement (juce::Slider&) override;
        int getSliderThumbRadius (juce::Slider&) override;
    };

    //==============================================================================
    /** Pomello rotativo 3D (filmstrip di 64 fotogrammi). */
    class FilmstripKnob : public juce::Slider
    {
    public:
        FilmstripKnob (const Assets&, const Knob& layout, float innerExclusionRadius = 0.0f);

        void paint (juce::Graphics&) override;
        bool hitTest (int x, int y) override;

    private:
        const Assets& assets;
        Knob k;
        float exclusion;
        juce::Point<float> origin;
    };

    //==============================================================================
    /** Cursore verticale del GQ-7. */
    class FaderCap : public juce::Slider
    {
    public:
        FaderCap (const Assets&, const Fader& layout);

        void paint (juce::Graphics&) override;
        bool hitTest (int x, int y) override;
        static int indentPx();

    private:
        juce::Point<float> capPosition();

        const Assets& assets;
        Fader f;
        juce::Point<float> origin;
    };

    //==============================================================================
    /** Interruttore a pedale invisibile sopra la foto (zona cliccabile = sagoma). */
    class FootSwitch : public juce::Button
    {
    public:
        explicit FootSwitch (const Footswitch& layout);
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
        bool hitTest (int x, int y) override;

    private:
        juce::Path shape;
    };

    //==============================================================================
    /** Bagliore del LED quando il pedale e' acceso. */
    class LedGlow : public juce::Component
    {
    public:
        explicit LedGlow (const Led& layout);
        void setOn (bool shouldBeOn);
        void paint (juce::Graphics&) override;

    private:
        Led led;
        bool on = false;
    };

    //==============================================================================
    /** Levetta S / C del distorsore (parametro a scelta con due valori). */
    class ModeToggle : public juce::Component
    {
    public:
        ModeToggle (const Assets&, juce::RangedAudioParameter& param);
        void paint (juce::Graphics&) override;
        void mouseUp (const juce::MouseEvent&) override;
        bool hitTest (int x, int y) override;

    private:
        const Assets& assets;
        juce::ParameterAttachment attachment;
        bool custom = false;
        juce::Point<float> origin;
    };

    //==============================================================================
    /** Tasto rotondo "i" sulla targhetta. */
    class InfoButton : public juce::Button
    {
    public:
        InfoButton();
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
        bool hitTest (int x, int y) override;
    };
}
