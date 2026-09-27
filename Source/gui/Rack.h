/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Slot della pedaliera (menu di scelta del pedale, spostamento,
    eliminazione, zoom, trascinamento) e pannello di zoom.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PedalView.h"

namespace pt::ui
{
    /** Menu dei pedali per categorie (id menu = indice del modello + 2; 1 = slot vuoto). */
    juce::PopupMenu buildModelMenu (const engine::ModelDef* current);

    //==============================================================================
    class SlotComponent : public juce::Component, public juce::DragAndDropTarget
    {
    public:
        struct Callbacks
        {
            std::function<void (int)> zoom;
            std::function<void (int)> removed;
            std::function<void()> changed;
        };

        SlotComponent (engine::Chain&, int index, juce::Component* popupParent, Callbacks);
        ~SlotComponent() override;

        void bindTo (int index);
        int getIndex() const { return index; }

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

        engine::Chain& chain;
        int index;
        Callbacks cb;
        juce::ComboBox selector;
        std::unique_ptr<IconButton> left, right, zoomBtn, del, power;
        PedalView view;
        bool dropHover = false;
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
    };
}
