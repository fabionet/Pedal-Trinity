/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Comandi "fotografici" generici, posizionati in coordinate logiche
    dell'immagine del pedale: ogni componente disegna i fotogrammi 3D e
    risponde solo sulla zona reale del comando.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Assets.h"
#include "Theme.h"
#include "../engine/Model.h"

namespace pt::ui
{
    /** Testo mostrato nel fumetto per un valore normalizzato. */
    juce::String formatControlValue (const engine::ControlDef&, double normalised);

    class PedalLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        PedalLookAndFeel();
        /** Colori di pulsanti, menu, combobox e fumetti dal tema. */
        void applyTheme (const Theme&);
        void drawBubble (juce::Graphics&, juce::BubbleComponent&, const juce::Point<float>& tip,
                         const juce::Rectangle<float>& body) override;
        juce::Font getSliderPopupFont (juce::Slider&) override;
        int getSliderPopupPlacement (juce::Slider&) override;
        int getSliderThumbRadius (juce::Slider&) override;
        void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
        juce::Font getPopupMenuFont() override;
        void drawComboBox (juce::Graphics&, int w, int h, bool down, int, int, int, int, juce::ComboBox&) override;
        juce::Font getComboBoxFont (juce::ComboBox&) override;
        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    };

    /** Interfaccia comune: collegamento al parametro dello slot. */
    struct BoundControl
    {
        std::function<void (float)> onChange;
        std::function<float()> readModel;
        virtual ~BoundControl() = default;
        virtual void syncFromModel() = 0;
    };

    //==============================================================================
    class KnobControl : public juce::Slider, public BoundControl
    {
    public:
        KnobControl (const Assets&, const engine::ControlDef&);
        void paint (juce::Graphics&) override;
        bool hitTest (int x, int y) override;
        void syncFromModel() override;
        void setExclusion (float r) { exclusion = r; }

    private:
        const Assets& assets;
        const engine::ControlDef& c;
        juce::Point<float> origin;
        float exclusion = 0;
    };

    //==============================================================================
    class FaderControl : public juce::Slider, public BoundControl
    {
    public:
        FaderControl (const Assets&, const engine::ControlDef&);
        void paint (juce::Graphics&) override;
        bool hitTest (int x, int y) override;
        void syncFromModel() override;
        static int indentPx();

    private:
        const Assets& assets;
        const engine::ControlDef& c;
        juce::Point<float> origin;
    };

    //==============================================================================
    class ToggleControl : public juce::Component, public BoundControl, public juce::SettableTooltipClient
    {
    public:
        ToggleControl (const Assets&, const engine::ControlDef&);
        void paint (juce::Graphics&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void syncFromModel() override;
        bool hitTest (int x, int y) override;

    private:
        const Assets& assets;
        const engine::ControlDef& c;
        juce::Point<float> origin;
        bool state = false;
    };

    //==============================================================================
    /** Pulsante disegnato (looper: REC/PLAY, STOP, CLEAR, UNDO). */
    class PushControl : public juce::Button, public BoundControl
    {
    public:
        explicit PushControl (const engine::ControlDef&);
        void paintButton (juce::Graphics&, bool over, bool down) override;
        void syncFromModel() override {}
        std::function<void()> onPress;
    private:
        const engine::ControlDef& c;
    };

    //==============================================================================
    /** Zona del pedale/footswitch (sagoma quadrilatera) che accende/spegne. */
    class FootZone : public juce::Button
    {
    public:
        FootZone (const float* xs, const float* ys);
        void paintButton (juce::Graphics&, bool over, bool down) override;
        bool hitTest (int x, int y) override;
    private:
        juce::Path shape;
    };
}
