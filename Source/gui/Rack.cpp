/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)
*/

#include "Rack.h"

namespace pt::ui
{
    using namespace pt::engine;

    juce::PopupMenu buildModelMenu (const ModelDef* current, bool splitterAllowed)
    {
        juce::PopupMenu root;
        root.addItem (1, "- Slot vuoto -", true, current == nullptr);
        for (int i = 0; i < numModels(); ++i)
            if (model (i).family == Family::Splitter)
                root.addItem (i + 2, juce::String (model (i).code) + "   " + model (i).name + "   (divide la catena: mono / dual / stereo)",
                              splitterAllowed, &model (i) == current);
        root.addSeparator();
        juce::StringArray cats;
        for (int i = 0; i < numModels(); ++i)
            if (model (i).family != Family::Splitter)
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
                juce::String text;
                if (const auto* v = visualDef (&m); v != &m)
                    text = juce::String (v->code) + "   " + v->name + "   (replica reale)";     // REAL MOD
                else
                {
                    text = juce::String (m.code) + "   " + m.name;
                    if (m.inspiredBy != nullptr && *m.inspiredBy)
                        text << "   (stile " << m.inspiredBy << ")";
                }
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
            const auto& t = themes->current();
            auto r = getLocalBounds().toFloat().reduced (1.0f);
            g.setColour (down ? t.button.brighter (0.3f) : over ? t.button.brighter (0.15f) : t.button);
            g.fillRoundedRectangle (r, 3.0f);
            auto ic = r.withSizeKeepingCentre (juce::jmin (r.getWidth(), r.getHeight()) * 0.62f, juce::jmin (r.getWidth(), r.getHeight()) * 0.62f);
            juce::Colour col = kind == Delete ? juce::Colour (0xffe06a5a) : t.text;
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
        juce::SharedResourcePointer<ThemeManager> themes;
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

        left->onClick = [this] { const int j = neighbour (-1); if (j >= 0) chain.moveTo (index, j, chain.lane (index)); };
        right->onClick = [this] { const int j = neighbour (1); if (j >= 0) chain.moveTo (index, j, chain.lane (index)); };
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
            else refreshHeader();
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
        *selector.getRootMenu() = buildModelMenu (d, chain.canPlaceSplitter (index));
        selector.setSelectedId (d == nullptr ? 1 : (int) (d - &model (0)) + 2, juce::dontSendNotification);
        if (const auto* v = visualDef (d); v != nullptr) selector.setText (juce::String (v->code) + "  " + v->name, juce::dontSendNotification);
        else selector.setText ("- vuoto -", juce::dontSendNotification);
        left->setEnabled (neighbour (-1) >= 0);
        right->setEnabled (neighbour (1) >= 0);
        power->setEnabled (d != nullptr);
        power->setToggleState (s != nullptr && s->enabled.load(), juce::dontSendNotification);
        zoomBtn->setEnabled (d != nullptr);
    }

    void SlotComponent::setLaneLabel (const juce::String& l)
    {
        if (l == laneLabel) return;
        laneLabel = l;
        resized();
        repaint();
    }

    int SlotComponent::numberWidth() const
    {
        return laneLabel.isEmpty() ? juce::jmin (26, getWidth() / 7) : juce::jmin (46, getWidth() / 5);
    }

    int SlotComponent::neighbour (int dir) const
    {
        const int n = chain.size(), sp = chain.splitterIndex();
        const bool dual = chain.splitMode() == SplitMode::Dual;
        if (! dual || index <= sp)
        {
            const int j = index + dir;
            // in DUAL gli slot prima dello splitter si scambiano solo tra loro e con lo splitter
            if (dual && j > sp && index <= sp) return -1;
            return juce::isPositiveAndBelow (j, n) ? j : -1;
        }
        const int myLane = chain.lane (index);
        for (int j = index + dir; j > sp && j < n; j += dir)
            if (chain.lane (j) == myLane) return j;
        return -1;
    }

    bool SlotComponent::jackPoint (bool output, int line, juce::Point<float>& out) const
    {
        juce::Point<float> p;
        if (! view.jackPoint (output, line, p)) return false;
        out = p + view.getPosition().toFloat();
        return true;
    }

    namespace
    {
        /** Area dell'immagine del pedale dentro uno slot con questi limiti (come PedalView::imageArea). */
        juce::Rectangle<float> nominalImage (juce::Rectangle<int> b, const ModelDef& d)
        {
            const int row = juce::jlimit (18, 28, b.getWidth() / 8);
            auto v = b.withZeroOrigin().withTrimmedTop (row * 2 + 6).reduced (2).toFloat();
            const float s = juce::jmin (v.getWidth() / d.imageW, v.getHeight() / d.imageH);
            return juce::Rectangle<float> (d.imageW * s, d.imageH * s).withCentre (v.getCentre());
        }
    }

    juce::Point<float> SlotComponent::nominalJack (juce::Rectangle<int> b, bool output, int line)
    {
        const auto* d = findModel ("split");                    // pedale compatto con prese A e B
        if (d == nullptr) d = &model (0);
        const auto a = nominalImage (b, *d);
        const float s = a.getWidth() / d->imageW;
        return { a.getX() + (output ? d->jackOutX : d->jackInX) * s, a.getY() + (line == 1 ? d->jackYB : d->jackYA) * s };
    }

    float SlotComponent::nominalScale (juce::Rectangle<int> b)
    {
        const auto* d = findModel ("split");
        if (d == nullptr) d = &model (0);
        return nominalImage (b, *d).getWidth() / d->imageW;
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
        top.removeFromLeft (numberWidth());                      // numero dello slot (e corsia)
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
        // lo slot lascia vedere la pedana del tema: solo un velo leggero e l'intestazione scura
        const auto& t = themes->current();
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (t.ink.withAlpha (0.10f));
        g.drawRoundedRectangle (r, 6.0f, 1.0f);
        auto h = headerArea().toFloat().reduced (1.0f);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (h.translated (0.0f, 2.0f), 5.0f);
        g.setColour (t.header);
        g.fillRoundedRectangle (h, 5.0f);
        const int row = juce::jlimit (18, 28, getWidth() / 8);
        g.setColour (t.accent);
        g.setFont (juce::Font ((float) row * 0.62f, juce::Font::bold));
        const int nw = numberWidth();
        if (laneLabel.isNotEmpty())
        {
            // numero + corsia ("3A"): la lettera ha il colore del cavo della corsia
            const auto box = juce::Rectangle<float> (3.0f, 2.0f, (float) nw, (float) row);
            g.drawText (juce::String (index + 1), box.withTrimmedRight (box.getWidth() * 0.42f), juce::Justification::centredRight);
            const auto badge = box.withTrimmedLeft (box.getWidth() * 0.6f).reduced (1.0f, 3.0f);
            g.setColour (laneLabel == "B" ? juce::Colour (0xffc8322a) : juce::Colour (0xffd9b464));
            g.fillRoundedRectangle (badge, 3.0f);
            g.setColour (juce::Colour (0xff111214));
            g.setFont (juce::Font (badge.getHeight() * 0.85f, juce::Font::bold));
            g.drawText (laneLabel, badge, juce::Justification::centred);
        }
        else
            g.drawText (juce::String (index + 1), 3, 2, nw, row, juce::Justification::centred);
    }

    void SlotComponent::paintOverChildren (juce::Graphics& g)
    {
        if (dropHover)
        {
            g.setColour (themes->current().accent);
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
        const int sp = chain.splitterIndex();
        if (chain.splitMode() == SplitMode::Dual && sp >= 0 && index > sp)
            m.addItem (6, chain.lane (index) == 0 ? "Sposta nella corsia B (uscita destra)" : "Sposta nella corsia A (uscita sinistra)");
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
                const int at = chain.insert (index + 1, sl->def->id, chain.lane (index));
                if (at >= 0)
                {
                    for (int k = 0; k < sl->def->numControls; ++k)
                        chain.setParam (at, k, sl->fx->p (k));
                    auto* copy = chain.slot (at);
                    if (const auto extra = sl->fx->saveState(); ! extra.empty() && copy != nullptr && copy->fx != nullptr)
                        copy->fx->restoreState (extra);      // file NAM/IR e opzioni del pannello
                }
            }
            else if (r == 3) chain.insert (index, {}, chain.lane (index));
            else if (r == 4) chain.insert (index + 1, {}, chain.lane (index));
            else if (r == 5) chain.setModel (index, {});
            else if (r == 6) chain.setLane (index, 1 - chain.lane (index));
        });
    }

    void SlotComponent::mouseDrag (const juce::MouseEvent& e)
    {
        // si trascina dall'intestazione o dal corpo del pedale (i comandi gestiscono i propri trascinamenti)
        if (e.mods.isPopupMenu() || e.getDistanceFromDragStart() < 6) return;
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
        if (from != to && cb.dropped)
        {
            const auto dropped = cb.dropped;
            juce::MessageManager::callAsync ([dropped, from, to] { dropped (from, to); });
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
        const ModelDef* v = visualDef (d);
        title.setText (juce::String ("Slot ") + juce::String (i + 1) + "  -  " + (v != nullptr ? juce::String (v->code) + "  " + v->name : juce::String ("vuoto")),
                       juce::dontSendNotification);
        juce::String text;
        if (d != nullptr)
        {
            text << "Categoria: " << d->category << "\n";
            if (v != d)
                text << "REAL MOD: replica del pedale reale. Suono e comandi del modello Pedal Trinity "
                     << d->code << " " << d->name << ".\n\n";
            else if (d->inspiredBy != nullptr && *d->inspiredBy) text << "Suono di riferimento: " << d->inspiredBy << "\n\n";
            if (d->notes != nullptr) text << juce::String::fromUTF8 (d->notes) << "\n\n";
            text << "Comandi:\n";
            for (int k = 0; k < d->numControls; ++k) text << "  - " << d->controls[k].label << "\n";
            text << "\nDoppio clic su un comando = valore di fabbrica.";
        }
        info.setText (text, juce::dontSendNotification);
        if (d != nullptr && d->family == Family::Nam)
        {
            if (nam == nullptr) { nam = std::make_unique<NamPanel> (chain, i); addAndMakeVisible (*nam); }
            else nam->bindTo (i);
        }
        else nam.reset();
        info.setVisible (nam == nullptr);
        resized();
        prev.setEnabled (i > 0);
        next.setEnabled (i < chain.size() - 1);
        repaint();
    }

    juce::Rectangle<int> ZoomPanel::card() const { return getLocalBounds().reduced (juce::jmax (20, getWidth() / 20), juce::jmax (16, getHeight() / 22)); }

    void ZoomPanel::paint (juce::Graphics& g)
    {
        const auto& t = themes->current();
        if (title.findColour (juce::Label::textColourId) != t.text) title.setColour (juce::Label::textColourId, t.text);
        if (info.findColour (juce::Label::textColourId) != t.textDim) info.setColour (juce::Label::textColourId, t.textDim);
        g.fillAll (juce::Colours::black.withAlpha (0.7f));
        auto c = card().toFloat();
        g.setGradientFill (juce::ColourGradient (t.panelTop.brighter (0.05f), c.getTopLeft(), t.panelBottom, c.getBottomLeft(), false));
        g.fillRoundedRectangle (c, 12.0f);
        g.setColour (t.accent.withAlpha (0.55f));
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
        auto side = c.removeFromRight (nam != nullptr ? juce::jmin (560, c.getWidth() / 2) : juce::jmin (380, c.getWidth() / 3));
        info.setBounds (side.reduced (10, 0));
        if (nam != nullptr) nam->setBounds (side.reduced (4, 0));
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
