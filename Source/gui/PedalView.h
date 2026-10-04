/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Vista di un singolo pedale: immagine renderizzata + comandi regolabili
    sull'immagine, LED, zona del footswitch, display (accordatore/looper).
    Scala liberamente: la stessa vista serve lo slot e il pannello zoom.
*/

#pragma once

#include <array>
#include <map>
#include <juce_gui_basics/juce_gui_basics.h>
#include "Controls.h"
#include "RealMod.h"
#include "../engine/Chain.h"

namespace pt::ui
{
    class PedalView : public juce::Component, private juce::Timer
    {
    public:
        PedalView (engine::Chain&, int slotIndex, juce::Component* popupParent);
        ~PedalView() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        /** Ricollega la vista allo slot (dopo spostamenti o cambi di pedale). */
        void bindTo (int slotIndex);
        int boundIndex() const { return index; }
        const engine::ModelDef* model() const { return def; }

        /** Bocca di una presa jack in coordinate della vista (uscita = fianco destro; line 0 = A, 1 = B).
            false se lo slot e' vuoto. */
        bool jackPoint (bool output, int line, juce::Point<float>& out) const;
        /** Pixel della vista per pixel logico dell'immagine del pedale. */
        float imageScale() const;
        /** Riquadro dell'immagine del pedale nella vista. */
        juce::Rectangle<float> pedalArea() const { return imageArea(); }

        //==================== REAL MOD: allineamento dei pomelli sulla foto personale
        bool hasPhoto() const { return photoImage.isValid() && def != nullptr && def->bodyW > 0; }
        bool isAligning() const { return aligner != nullptr; }
        void setAlignMode (bool);
        /** Salva nel .json della foto le posizioni allineate; errore o stringa vuota. */
        juce::String saveAlignment();

    private:
        class Canvas;
        class Aligner;
        struct AlignItem { int control = -1; juce::String label; juce::Point<float> centre; float radius = 0; };
        void applyAlignItem (const AlignItem&);
        void setKnobPlacement (int controlIndex, juce::Point<float> target, float radius);
        void canvasRepaint();
        void timerCallback() override;
        void rebuild();
        void placeOnPhoto (KnobControl&, int controlIndex);
        juce::Rectangle<float> imageArea() const;

        engine::Chain& chain;
        juce::Component* popupParent;
        int index = -1;
        std::shared_ptr<engine::Slot> slotRef;   // mantiene vivo lo slot finche' la vista lo usa
        engine::Slot* slot = nullptr;
        const engine::ModelDef* def = nullptr;          // aspetto disegnato (replica reale in REAL MOD)
        const engine::ModelDef* engineDef = nullptr;    // modello dello slot (suono e comandi)
        juce::Image photoImage;                         // foto personale in rilievo in uso (REAL MOD)
        bool photoKnobs = true;
        std::vector<std::pair<juce::String, PhotoKnob>> photoControls;   // pomelli nella foto
        juce::File photoFile;
        std::map<int, KnobControl*> knobComps;          // pomelli creati, per indice del comando
        std::unique_ptr<Aligner> aligner;
        bool photoLed = false;                          // LED CHECK nella foto
        juce::Point<float> photoLedAt;
        juce::Point<float> ledPoint() const;
        juce::SharedResourcePointer<Assets> assets;
        juce::SharedResourcePointer<RealPhotos> photos;
        juce::SharedResourcePointer<ThemeManager> themes;
        std::unique_ptr<Canvas> canvas;
        juce::Image source, scaled;
        juce::Rectangle<int> scaledFor;
        bool lastOn = false;
    public:
        std::array<float, 6> namLevels {};       // livelli dei meter del NAM-A1A2 (thread dei messaggi)
    };
}
