/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Slot della pedaliera (menu di scelta del pedale, spostamento,
    eliminazione, zoom, trascinamento) e pannello di zoom.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PedalView.h"
#include "NamPanel.h"

namespace pt::ui
{
    /** Menu dei pedali per categorie (id menu = indice del modello + 2; 1 = slot vuoto).
        Lo splitter e' in cima ed e' disattivato se la catena ne ha gia' uno altrove. */
    juce::PopupMenu buildModelMenu (const engine::ModelDef* current, bool splitterAllowed = true);

    //==============================================================================
    class SlotComponent : public juce::Component, public juce::DragAndDropTarget
    {
    public:
        struct Callbacks
        {
            std::function<void (int)> zoom;
            std::function<void (int)> removed;
            std::function<void (int from, int to)> dropped;      // trascinamento su questo slot
        };

        SlotComponent (engine::Chain&, int index, juce::Component* popupParent, Callbacks);
        ~SlotComponent() override;

        void bindTo (int index);
        int getIndex() const { return index; }
        /** Lettera della corsia mostrata accanto al numero ("A", "B" o vuota). */
        void setLaneLabel (const juce::String&);
        /** Bocca di una presa jack in coordinate dello slot (false se lo slot e' vuoto). */
        bool jackPoint (bool output, int line, juce::Point<float>& out) const;
        float pedalScale() const { return view.imageScale(); }
        /** Riquadro del pedale disegnato (coordinate dello slot), vuoto se lo slot non ha un pedale. */
        juce::Rectangle<float> pedalBounds() const;
        /** Presa A "tipica" (pedale compatto) per uno slot con questi limiti, in coordinate dello slot. */
        static juce::Point<float> nominalJack (juce::Rectangle<int> slotBounds, bool output, int line = 0);
        static float nominalScale (juce::Rectangle<int> slotBounds);

        void paint (juce::Graphics&) override;
        void paintOverChildren (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;

        bool isInterestedInDragSource (const SourceDetails&) override;
        void itemDragEnter (const SourceDetails&) override { dropHover = true; repaint(); }
        void itemDragExit (const SourceDetails&) override { dropHover = false; repaint(); }
        void itemDropped (const SourceDetails&) override;

    private:
        class IconButton;
        void refreshHeader();
        juce::Rectangle<int> headerArea() const;
        /** Slot vicino nella stessa corsia (dir -1 / +1), -1 se non c'e'. */
        int neighbour (int dir) const;
        int numberWidth() const;

        engine::Chain& chain;
        int index;
        Callbacks cb;
        juce::ComboBox selector;
        std::unique_ptr<IconButton> left, right, zoomBtn, del, power;
        PedalView view;
        bool dropHover = false;
        juce::String laneLabel;
        juce::SharedResourcePointer<ThemeManager> themes;
        std::unique_ptr<juce::FileChooser> chooser;
        void showContextMenu();
    };

    //==============================================================================
    /** Retro del pedale o dell'ampli disegnato (normale o replica REAL MOD): immagine "<id>_back.jpg"
        ("r_<id>_back.jpg" per la REAL MOD) cercata per nome nel BinaryData. Non valida se il modello non ha il retro. */
    juce::Image backImageFor (const engine::ModelDef* visual);
    /** Solo autotest e screenshot: sostituisce la ricerca nel BinaryData (nome della risorsa -> immagine);
        una funzione vuota torna al BinaryData. */
    void setBackImageLookup (std::function<juce::Image (const juce::String& resource)>);

    /** Pannello di zoom: un pedale ingrandito, con descrizione e navigazione.
        Se il modello ha l'immagine del retro compare il tasto "Retro" / "Fronte" che gira la vista. */
    class ZoomPanel : public juce::Component, private juce::Timer
    {
    public:
        ZoomPanel (engine::Chain&, int index, juce::Component* popupParent);
        ~ZoomPanel() override;
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseUp (const juce::MouseEvent&) override;
        bool keyPressed (const juce::KeyPress&) override;
        void show (int index);
        int currentIndex() const { return index; }
        std::function<void()> onClose;
        /** Avvia l'allineamento dei pomelli sulla foto (screenshot della guida). */
        void startAlign() { setShowBack (false, false); view.setAlignMode (true); updateAlignButtons(); }

        /** Il modello mostrato ha l'immagine del retro? */
        bool hasBack() const { return backImage.isValid(); }
        bool showingBack() const { return back; }
        bool isFlipping() const { return isTimerRunning(); }
        /** Gira la vista sul retro (true) o sul fronte; animate = breve rotazione. */
        void setShowBack (bool showBack, bool animate = true);
        juce::String flipButtonText() const { return flip.getButtonText(); }
        bool flipButtonVisible() const { return flip.isVisible(); }

    private:
        class BackView;
        void timerCallback() override;
        void applyFlipTransform (float phase);
        std::unique_ptr<BackView> backView;
        juce::TextButton flip { "Retro" };
        juce::Image backImage;
        bool back = false;
        juce::uint32 flipStart = 0;

        juce::Rectangle<int> card() const;
        engine::Chain& chain;
        int index;
        PedalView view;
        juce::TextButton prev { "<" }, next { ">" }, close { "Chiudi" };
        juce::TextButton align { "Allinea pomelli sulla foto" }, cancelAlign { "Annulla" };
        juce::Label alignHint;
        void updateAlignButtons();
        juce::Label title, info;
        std::unique_ptr<NamPanel> nam;          // solo per il NAM-A1A2 Model
        juce::SharedResourcePointer<ThemeManager> themes;
    };
}
