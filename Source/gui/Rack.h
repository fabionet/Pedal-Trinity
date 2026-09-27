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
    /** Pannello di zoom: un pedale ingrandito, con descrizione e navigazione. */
    class ZoomPanel : public juce::Component
    {
    public:
        ZoomPanel (engine::Chain&, int index, juce::Component* popupParent);
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseUp (const juce::MouseEvent&) override;
        bool keyPressed (const juce::KeyPress&) override;
        void show (int index);
        int currentIndex() const { return index; }
        std::function<void()> onClose;

    private:
        juce::Rectangle<int> card() const;
        engine::Chain& chain;
        int index;
        PedalView view;
        juce::TextButton prev { "<" }, next { ">" }, close { "Chiudi" };
        juce::Label title, info;
        std::unique_ptr<NamPanel> nam;          // solo per il NAM-A1A2 Model
        juce::SharedResourcePointer<ThemeManager> themes;
    };
}
