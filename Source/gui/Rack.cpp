/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)
*/

#include "Rack.h"

namespace pt::ui
{
    using namespace pt::engine;

    juce::PopupMenu buildModelMenu (const ModelDef* current)
    {
        juce::PopupMenu root;
        root.addItem (1, "- Slot vuoto -", true, current == nullptr);
        root.addSeparator();
        juce::StringArray cats;
        for (int i = 0; i < numModels(); ++i)
            cats.addIfNotAlreadyThere (model (i).category);
        for (const auto& cat : cats)
        {
            juce::PopupMenu sub;
            bool containsCurrent = false;
            for (int i = 0; i < numModels(); ++i)
            {
                const auto& m = model (i);
                if (cat != m.category) continue;
                const bool isCur = &m == current;
                containsCurrent |= isCur;
                juce::String text = juce::String (m.code) + "   " + m.name;
                if (m.inspiredBy != nullptr && *m.inspiredBy)
                    text << "   (stile " << m.inspiredBy << ")";
                sub.addItem (i + 2, text, true, isCur);
            }
            root.addSubMenu (cat, sub, true, nullptr, containsCurrent);
        }
        return root;
    }

    //==============================================================================
    class SlotComponent::IconButton : public juce::Button
    {
    public:
        enum Kind { Left, Right, Zoom, Delete, Power };
        IconButton (Kind k, const juce::String& tip) : juce::Button (tip), kind (k)
        {
            setTooltip (tip);
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
        }
        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            auto r = getLocalBounds().toFloat().reduced (1.0f);
            g.setColour (down ? juce::Colour (0xff3a3b40) : over ? juce::Colour (0xff303136) : juce::Colour (0xff25262a));
            g.fillRoundedRectangle (r, 3.0f);
            auto ic = r.withSizeKeepingCentre (juce::jmin (r.getWidth(), r.getHeight()) * 0.62f, juce::jmin (r.getWidth(), r.getHeight()) * 0.62f);
            juce::Colour col = kind == Delete ? juce::Colour (0xffe06a5a) : juce::Colour (0xffe8e6df);
            if (kind == Power) col = getToggleState() ? juce::Colour (0xffff4a30) : juce::Colour (0xff77787c);
            if (! isEnabled()) col = col.withAlpha (0.3f);
            g.setColour (col);
            juce::Path p;
            switch (kind)
            {
                case Left:  p.addTriangle (ic.getRight(), ic.getY(), ic.getRight(), ic.getBottom(), ic.getX(), ic.getCentreY()); g.fillPath (p); break;
                case Right: p.addTriangle (ic.getX(), ic.getY(), ic.getX(), ic.getBottom(), ic.getRight(), ic.getCentreY()); g.fillPath (p); break;
                case Zoom:
                    g.drawEllipse (ic.withTrimmedRight (ic.getWidth() * 0.3f).withTrimmedBottom (ic.getHeight() * 0.3f), 1.6f);
                    g.drawLine (ic.getX() + ic.getWidth() * 0.62f, ic.getY() + ic.getHeight() * 0.62f, ic.getRight(), ic.getBottom(), 2.0f);
                    break;
                case Delete:
                    g.drawLine (ic.getX(), ic.getY(), ic.getRight(), ic.getBottom(), 2.0f);
                    g.drawLine (ic.getRight(), ic.getY(), ic.getX(), ic.getBottom(), 2.0f);
                    break;
                case Power:
                    p.addCentredArc (ic.getCentreX(), ic.getCentreY(), ic.getWidth() * 0.45f, ic.getHeight() * 0.45f, 0,
                                     juce::degreesToRadians (35.0f), juce::degreesToRadians (325.0f), true);
                    g.strokePath (p, juce::PathStrokeType (1.8f));
                    g.drawLine (ic.getCentreX(), ic.getY(), ic.getCentreX(), ic.getCentreY(), 1.8f);
                    break;
            }
        }
        Kind kind;
    };

    //==============================================================================
    SlotComponent::SlotComponent (Chain& c, int i, juce::Component* popupParent, Callbacks callbacks)
        : chain (c), index (i), cb (std::move (callbacks)), view (c, i, popupParent)
    {
        left = std::make_unique<IconButton> (IconButton::Left, "Sposta a sinistra");
        right = std::make_unique<IconButton> (IconButton::Right, "Sposta a destra");
        zoomBtn = std::make_unique<IconButton> (IconButton::Zoom, "Zoom");
        del = std::make_unique<IconButton> (IconButton::Delete, "Elimina lo slot");
        power = std::make_unique<IconButton> (IconButton::Power, "Acceso / spento");
        for (auto* b : { left.get(), right.get(), zoomBtn.get(), del.get(), power.get() })
            addAndMakeVisible (b);

        left->onClick = [this] { chain.move (index, index - 1); };
        right->onClick = [this] { chain.move (index, index + 1); };
        zoomBtn->onClick = [this] { if (cb.zoom) cb.zoom (index); };
        del->onClick = [this]
        {
            const int idx = index;
            const auto removed = cb.removed;      // copia locale: niente 'this' nella lambda annidata (MSVC)
            juce::MessageManager::callAsync ([removed, idx] { if (removed) removed (idx); });
        };
        power->onClick = [this] { if (auto* s = chain.slot (index)) chain.setEnabled (index, ! s->enabled.load()); };

        selector.setTooltip ("Scegli il pedale per questo slot");
        selector.onChange = [this]
        {
            const int id = selector.getSelectedId();
            if (id <= 0) return;
            const juce::String modelId = id == 1 ? juce::String() : juce::String (model (id - 2).id);
            auto* s = chain.slot (index);
            const juce::String cur = s != nullptr && s->def != nullptr ? juce::String (s->def->id) : juce::String();
            if (modelId != cur)
            {
                const int idx = index;
                auto& ch = chain;
                juce::MessageManager::callAsync ([&ch, idx, modelId] { ch.setModel (idx, modelId); });
            }
        };
        addAndMakeVisible (selector);
        addAndMakeVisible (view);
        refreshHeader();
    }

    SlotComponent::~SlotComponent() = default;

    void SlotComponent::bindTo (int i)
    {
        index = i;
        view.bindTo (i);
        refreshHeader();
        repaint();
    }

    void SlotComponent::refreshHeader()
    {
        auto* s = chain.slot (index);
        const ModelDef* d = s != nullptr ? s->def : nullptr;
        selector.clear (juce::dontSendNotification);
        *selector.getRootMenu() = buildModelMenu (d);
        selector.setSelectedId (d == nullptr ? 1 : (int) (d - &model (0)) + 2, juce::dontSendNotification);
        if (d != nullptr) selector.setText (juce::String (d->code) + "  " + d->name, juce::dontSendNotification);
        else selector.setText ("- vuoto -", juce::dontSendNotification);
        left->setEnabled (index > 0);
        right->setEnabled (index < chain.size() - 1);
        power->setEnabled (d != nullptr);
        power->setToggleState (s != nullptr && s->enabled.load(), juce::dontSendNotification);
        zoomBtn->setEnabled (d != nullptr);
    }

    juce::Rectangle<int> SlotComponent::headerArea() const
    {
        const int row = juce::jlimit (18, 28, getWidth() / 8);
        return getLocalBounds().removeFromTop (row * 2 + 6);
    }

    void SlotComponent::resized()
    {
        auto r = getLocalBounds();
        const int row = juce::jlimit (18, 28, getWidth() / 8);
        auto header = r.removeFromTop (row * 2 + 6).reduced (3, 2);
        auto top = header.removeFromTop (row);
        top.removeFromLeft (juce::jmin (26, getWidth() / 7));    // numero dello slot
        selector.setBounds (top);
        header.removeFromTop (2);
        const int n = 5, w = header.getWidth() / n;
        IconButton* order[] = { left.get(), right.get(), power.get(), zoomBtn.get(), del.get() };
        for (int i = 0; i < n; ++i)
            order[i]->setBounds (header.getX() + i * w, header.getY(), w - 2, header.getHeight());
        view.setBounds (r.reduced (2));
        power->setToggleState (chain.slot (index) != nullptr && chain.slot (index)->enabled.load(), juce::dontSendNotification);
    }

    void SlotComponent::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (juce::Colour (0xff17181b));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (juce::Colour (0x18ffffff));
        g.drawRoundedRectangle (r, 6.0f, 1.0f);
        auto h = headerArea().toFloat().reduced (1.0f);
        g.setColour (juce::Colour (0xff1f2024));
        g.fillRoundedRectangle (h.withHeight (h.getHeight()), 5.0f);
        const int row = juce::jlimit (18, 28, getWidth() / 8);
        g.setColour (juce::Colour (0xffd9b464));
        g.setFont (juce::Font ((float) row * 0.62f, juce::Font::bold));
        g.drawText (juce::String (index + 1), 3, 2, juce::jmin (26, getWidth() / 7), row, juce::Justification::centred);
    }

    void SlotComponent::paintOverChildren (juce::Graphics& g)
    {
        if (dropHover)
        {
            g.setColour (juce::Colour (0xffd9b464));
            g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (2.0f), 6.0f, 3.0f);
        }
    }

    void SlotComponent::mouseDown (const juce::MouseEvent& e)
    {
        if (e.mods.isPopupMenu() && headerArea().contains (e.getPosition()))
            showContextMenu();
    }

    void SlotComponent::showContextMenu()
    {
        auto* s = chain.slot (index);
        const bool hasFx = s != nullptr && s->fx != nullptr;
        juce::PopupMenu m;
        m.addSectionHeader ("Slot " + juce::String (index + 1));
        if (hasFx && s->fx->acceptsFiles())
            m.addItem (1, "Carica risposta all'impulso (.wav)...");
        m.addItem (2, "Duplica lo slot", hasFx && chain.canAdd());
        m.addItem (3, "Inserisci slot vuoto prima", chain.canAdd());
        m.addItem (4, "Inserisci slot vuoto dopo", chain.canAdd());
        m.addItem (5, "Svuota lo slot", hasFx);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&selector), [this] (int r)
        {
            auto* sl = chain.slot (index);
            if (r == 1 && sl != nullptr && sl->fx != nullptr)
            {
                chooser = std::make_unique<juce::FileChooser> ("Risposta all'impulso", juce::File(), "*.wav;*.aif;*.aiff");
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                      [this] (const juce::FileChooser& fc)
                {
                    const auto f = fc.getResult();
                    auto* s2 = chain.slot (index);
                    if (s2 != nullptr && s2->fx != nullptr && f.existsAsFile())
                        s2->fx->loadFile (f.getFullPathName().toStdString());
                });
            }
            else if (r == 2 && sl != nullptr && sl->def != nullptr)
            {
                const int at = chain.insert (index + 1, sl->def->id);
                if (at >= 0)
                    for (int k = 0; k < sl->def->numControls; ++k)
                        chain.setParam (at, k, sl->fx->p (k));
            }
            else if (r == 3) chain.insert (index, {});
            else if (r == 4) chain.insert (index + 1, {});
            else if (r == 5) chain.setModel (index, {});
        });
    }

    void SlotComponent::mouseDrag (const juce::MouseEvent& e)
    {
        if (! headerArea().contains (e.getMouseDownPosition()) || e.getDistanceFromDragStart() < 6) return;
        if (auto* dnd = juce::DragAndDropContainer::findParentDragContainerFor (this))
            if (! dnd->isDragAndDropActive())
            {
                auto snap = createComponentSnapshot (getLocalBounds(), true, 0.5f);
                dnd->startDragging ("slot:" + juce::String (index), this, juce::ScaledImage (snap, 0.5), true);
            }
    }

    bool SlotComponent::isInterestedInDragSource (const SourceDetails& d)
    {
        return d.description.toString().startsWith ("slot:");
    }

    void SlotComponent::itemDropped (const SourceDetails& d)
    {
        dropHover = false;
        const int from = d.description.toString().fromFirstOccurrenceOf ("slot:", false, false).getIntValue();
        const int to = index;
        if (from != to)
        {
            auto& ch = chain;
            juce::MessageManager::callAsync ([&ch, from, to] { ch.move (from, to); });
        }
        repaint();
    }

    //==============================================================================
    ZoomPanel::ZoomPanel (Chain& c, int i, juce::Component* popupParent) : chain (c), index (i), view (c, i, popupParent)
    {
        setWantsKeyboardFocus (true);
        addAndMakeVisible (view);
        title.setFont (juce::Font (24.0f, juce::Font::bold));
        title.setColour (juce::Label::textColourId, juce::Colour (0xffe9e6dc));
        info.setFont (juce::Font (15.0f));
        info.setColour (juce::Label::textColourId, juce::Colour (0xffb9b6ac));
        info.setJustificationType (juce::Justification::topLeft);
        for (auto* b : { &prev, &next, &close }) addAndMakeVisible (b);
        addAndMakeVisible (title);
        addAndMakeVisible (info);
        prev.onClick = [this] { show (juce::jmax (0, index - 1)); };
        next.onClick = [this] { show (juce::jmin (chain.size() - 1, index + 1)); };
        close.onClick = [this] { if (onClose) onClose(); };
        prev.setTooltip ("Pedale precedente");
        next.setTooltip ("Pedale successivo");
        show (i);
    }

    void ZoomPanel::show (int i)
    {
        index = i;
        view.bindTo (i);
        auto* s = chain.slot (i);
        const ModelDef* d = s != nullptr ? s->def : nullptr;
        title.setText (juce::String ("Slot ") + juce::String (i + 1) + "  -  " + (d != nullptr ? juce::String (d->code) + "  " + d->name : juce::String ("vuoto")),
                       juce::dontSendNotification);
        juce::String text;
        if (d != nullptr)
        {
            text << "Categoria: " << d->category << "\n";
            if (d->inspiredBy != nullptr && *d->inspiredBy) text << "Suono di riferimento: " << d->inspiredBy << "\n\n";
            if (d->notes != nullptr) text << juce::String::fromUTF8 (d->notes) << "\n\n";
            text << "Comandi:\n";
            for (int k = 0; k < d->numControls; ++k) text << "  - " << d->controls[k].label << "\n";
            text << "\nDoppio clic su un comando = valore di fabbrica.";
        }
        info.setText (text, juce::dontSendNotification);
        prev.setEnabled (i > 0);
        next.setEnabled (i < chain.size() - 1);
        repaint();
    }

    juce::Rectangle<int> ZoomPanel::card() const { return getLocalBounds().reduced (juce::jmax (20, getWidth() / 20), juce::jmax (16, getHeight() / 22)); }

    void ZoomPanel::paint (juce::Graphics& g)
    {
        g.fillAll (juce::Colours::black.withAlpha (0.7f));
        auto c = card().toFloat();
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff232428), c.getTopLeft(), juce::Colour (0xff111214), c.getBottomLeft(), false));
        g.fillRoundedRectangle (c, 12.0f);
        g.setColour (juce::Colour (0x88d9b464));
        g.drawRoundedRectangle (c.reduced (1.0f), 12.0f, 1.4f);
    }

    void ZoomPanel::resized()
    {
        auto c = card().reduced (20);
        auto top = c.removeFromTop (40);
        close.setBounds (top.removeFromRight (110).reduced (0, 4));
        top.removeFromRight (8);
        next.setBounds (top.removeFromRight (40).reduced (0, 4));
        prev.setBounds (top.removeFromRight (40).reduced (0, 4));
        title.setBounds (top);
        c.removeFromTop (10);
        auto side = c.removeFromRight (juce::jmin (380, c.getWidth() / 3));
        info.setBounds (side.reduced (10, 0));
        view.setBounds (c);
    }

    void ZoomPanel::mouseUp (const juce::MouseEvent& e)
    {
        if (! card().contains (e.getPosition()) && onClose) onClose();
    }

    bool ZoomPanel::keyPressed (const juce::KeyPress& k)
    {
        if (k == juce::KeyPress::escapeKey && onClose) { onClose(); return true; }
        if (k == juce::KeyPress::leftKey) { prev.triggerClick(); return true; }
        if (k == juce::KeyPress::rightKey) { next.triggerClick(); return true; }
        return false;
    }
}
