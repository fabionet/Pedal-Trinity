/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)
*/

#include "RoutingMap.h"
#include "Assets.h"
#include "Cables.h"
#include "RealMod.h"

namespace pt::ui
{
    using namespace pt::engine;
    using routing::End;

    namespace
    {
        // misure sulla mappa (px a zoom 1)
        constexpr float nodeW = 150.0f, ioW = 112.0f, nodeH = 184.0f;
        constexpr float stepX = 200.0f, stepY = 246.0f, gridMargin = 40.0f;
        constexpr float headerH = 28.0f, imageTop = 32.0f, imageBottom = 138.0f;
        constexpr float portYA = 72.0f, portYB = 112.0f;
        constexpr float minZoom = 0.2f, maxZoom = 2.0f, readableZoom = 0.7f;

        const juce::Colour laneA (0xffd9b464), laneB (0xffc8322a);
        const juce::Colour okColour (0xff4cc26a), badColour (0xffe0554b);
        const juce::Colour noticeBg (0xf0141518);

        juce::String codeOf (const ModelDef* engineDef)
        {
            return engineDef != nullptr ? visualCode (engineDef) : juce::String();
        }
    }

    RoutingMap::RoutingMap (Chain& c, RouteLayout& l) : chain (c), layoutStore (l)
    {
        setWantsKeyboardFocus (true);
        setOpaque (false);

        title.setText ("Mappa di Routing", juce::dontSendNotification);
        title.setFont (juce::Font (22.0f, juce::Font::bold));
        help.setText ("Trascina un oggetto per spostarlo e una spina per staccare o ricollegare un cavo (stesse regole della pedaliera). "
                      "Doppio clic = zoom del pedale, clic sul LED = acceso/spento, rotellina = scorri, Ctrl + rotellina = zoom, Esc = chiudi.",
                      juce::dontSendNotification);
        help.setFont (juce::Font (13.0f));
        help.setMinimumHorizontalScale (0.75f);
        help.setJustificationType (juce::Justification::centredLeft);
        zoomLabel.setJustificationType (juce::Justification::centred);
        zoomLabel.setFont (juce::Font (13.0f, juce::Font::bold));
        zoomLabel.setMinimumHorizontalScale (0.7f);
        for (auto* lab : { &title, &help, &zoomLabel }) { lab->setInterceptsMouseClicks (false, false); addAndMakeVisible (lab); }

        closeButton.setTooltip ("Chiude la mappa (Esc)");
        arrangeButton.setTooltip ("Dimentica le posizioni scelte e ridispone gli oggetti in righe leggibili");
        fitButton.setTooltip ("Adatta lo zoom per vedere tutta la catena (tasto 0)");
        zoomOutButton.setTooltip ("Riduci (Ctrl + rotellina, tasto -)");
        zoomInButton.setTooltip ("Ingrandisci (Ctrl + rotellina, tasto +)");
        closeButton.onClick = [this] { if (onClose) onClose(); };
        arrangeButton.onClick = [this] { autoArrange(); };
        fitButton.onClick = [this] { fitAll (false); };
        zoomOutButton.onClick = [this] { setZoom (zoom / 1.2f, canvasArea().getCentre()); };
        zoomInButton.onClick = [this] { setZoom (zoom * 1.2f, canvasArea().getCentre()); };
        for (auto* b : { &closeButton, &arrangeButton, &fitButton, &zoomOutButton, &zoomInButton })
        {
            b->setWantsKeyboardFocus (false);
            addAndMakeVisible (b);
        }
        for (auto* sb : { &hbar, &vbar })
        {
            sb->setAutoHide (true);
            sb->addListener (this);
            addAndMakeVisible (sb);
        }

        // vista salvata nel progetto; alla prima apertura si adatta alla catena (in resized)
        zoom = layoutStore.zoom();
        origin = layoutStore.viewOrigin();
        needFit = zoom <= 0.0f;
        if (needFit) zoom = 1.0f;
        chain.addChangeListener (this);
        themes->addChangeListener (this);
        refresh();
        startTimerHz (10);
    }

    RoutingMap::~RoutingMap()
    {
        stopTimer();
        storeView();
        chain.removeChangeListener (this);
        themes->removeChangeListener (this);
    }

    //==============================================================================
    juce::Rectangle<int> RoutingMap::card() const
    {
        return getLocalBounds().reduced (juce::jmax (12, getWidth() / 90), juce::jmax (10, getHeight() / 70));
    }

    juce::Point<float> RoutingMap::toScreen (juce::Point<float> w) const
    {
        return canvas.getPosition().toFloat() + (w - origin) * zoom;
    }

    juce::Point<float> RoutingMap::toWorld (juce::Point<float> s) const
    {
        return origin + (s - canvas.getPosition().toFloat()) / zoom;
    }

    juce::Rectangle<float> RoutingMap::toScreen (juce::Rectangle<float> w) const
    {
        const auto tl = toScreen (w.getTopLeft());
        return { tl.x, tl.y, w.getWidth() * zoom, w.getHeight() * zoom };
    }

    const RoutingMap::Node* RoutingMap::nodeFor (int slot) const
    {
        for (const auto& n : nodes) if (n.slot == slot) return &n;
        return nullptr;
    }

    RoutingMap::Node* RoutingMap::nodeFor (int slot)
    {
        for (auto& n : nodes) if (n.slot == slot) return &n;
        return nullptr;
    }

    juce::Rectangle<float> RoutingMap::nodeWorldBounds (int slot) const
    {
        if (auto* n = nodeFor (slot)) return n->world;
        return {};
    }

    juce::Point<float> RoutingMap::nodeScreenCentre (int slot) const
    {
        if (auto* n = nodeFor (slot)) return toScreen (n->world).getCentre();
        return {};
    }

    juce::Rectangle<float> RoutingMap::contentBounds() const
    {
        juce::Rectangle<float> r;
        for (const auto& n : nodes) r = r.isEmpty() ? n.world : r.getUnion (n.world);
        return r.expanded (gridMargin);
    }

    //==============================================================================
    void RoutingMap::changeListenerCallback (juce::ChangeBroadcaster* src)
    {
        if (src == &themes.getObject()) thumbs.clear();      // REAL MOD o tema: altre miniature
        refresh();
    }

    void RoutingMap::refresh()
    {
        const auto before = routing::describe (conns);
        layoutStore.sync();
        conns = routing::connections (chain);
        if (drag == Drag::Plug && routing::describe (conns) != before) drag = Drag::None;   // i cavi sono cambiati altrove
        computeLayout();
        updateScrollBars();
        repaint();
    }

    void RoutingMap::computeLayout()
    {
        const int dragged = drag == Drag::Node && juce::isPositiveAndBelow (dragNode, (int) nodes.size()) ? nodes[(size_t) dragNode].slot : 1000000;
        nodes.clear();
        const int n = chain.size(), sp = chain.splitterIndex();
        const bool dual = routing::isDual (chain);
        const float cw = canvas.getWidth() > 0 ? (float) canvas.getWidth() : 1200.0f;
        columns = juce::jlimit (3, 12, (int) (cw / (stepX * 0.75f)) - 2);
        const int C = columns;
        auto grid = [] (float col, float row) { return juce::Point<float> (gridMargin + col * stepX, gridMargin + row * stepY); };

        std::vector<int> pre, a, b;
        for (int i = 0; i < n; ++i)
        {
            auto* s = chain.slot (i);
            if (s == nullptr || s->fx == nullptr) continue;          // slot vuoto: il cavo lo attraversa, non e' un oggetto
            if (! dual || i <= sp) pre.push_back (i);
            else (chain.lane (i) == 1 ? b : a).push_back (i);
        }
        std::map<int, juce::Point<float>> autoPos;
        juce::Point<float> outPos;
        int lastCol = 0, lastRow = 0;
        for (int k = 0; k < (int) pre.size(); ++k)
        {
            lastCol = 1 + k % C; lastRow = k / C;
            autoPos[pre[(size_t) k]] = grid ((float) lastCol, (float) lastRow);
        }
        if (! dual)
            outPos = pre.empty() ? grid (1, 0) : grid ((float) lastCol + 1, (float) lastRow);
        else
        {
            // corsie A e B una sotto l'altra, dalla colonna dopo lo splitter se c'e' spazio
            const int avail = C - lastCol;
            const int start = avail >= 2 ? lastCol + 1 : 1, width = avail >= 2 ? avail : C;
            const int rowA0 = avail >= 2 ? lastRow : lastRow + 1;
            const int rowsA = juce::jmax (1, ((int) a.size() + width - 1) / width);
            const int rowB0 = rowA0 + rowsA;
            for (int k = 0; k < (int) a.size(); ++k) autoPos[a[(size_t) k]] = grid ((float) (start + k % width), (float) (rowA0 + k / width));
            for (int k = 0; k < (int) b.size(); ++k) autoPos[b[(size_t) k]] = grid ((float) (start + k % width), (float) (rowB0 + k / width));
            const int longest = (int) juce::jmax (a.size(), b.size());
            outPos = grid ((float) (start + juce::jmin (width, juce::jmax (1, longest))), (float) (rowA0 + rowsA - 1 + rowB0) * 0.5f);
        }

        auto addIo = [this] (int which, juce::Point<float> autoTopLeft)
        {
            Node nd;
            nd.slot = which == 0 ? inputNode : outputNode;
            juce::Point<float> p;
            if (! layoutStore.ioPos (which, p)) p = autoTopLeft.translated ((nodeW - ioW) * 0.5f, 0.0f);
            nd.world = { p.x, p.y, ioW, nodeH };
            nodes.push_back (nd);
        };
        addIo (0, grid (0, 0));
        for (const auto& [slot, ap] : autoPos)
        {
            Node nd;
            nd.slot = slot;
            auto* s = chain.slot (slot);
            nd.def = s->def;
            nd.vis = visualDef (s->def);
            nd.lane = dual && slot > sp ? chain.lane (slot) : -1;
            nd.outB = s->isSplitter() && chain.isSplitActive();
            juce::Point<float> p;
            if (! layoutStore.slotPos (slot, p)) p = ap;
            nd.world = { p.x, p.y, nodeW, nodeH };
            nodes.push_back (nd);
        }
        addIo (1, outPos);
        // prese B in uso (pedali stereo dopo lo splitter STEREO, OUTPUT L/R)
        for (const auto& c : conns)
            for (const auto* e : { &c.a, &c.b })
                if (e->line == 1)
                    if (auto* nd = nodeFor (e->kind == End::Input ? inputNode : e->kind == End::Output ? outputNode : e->slot))
                        (e->kind == End::SlotOut || e->kind == End::Input ? nd->outB : nd->inB) = true;
        // l'oggetto trascinato resta in cima
        for (size_t k = 0; k < nodes.size(); ++k)
            if (nodes[k].slot == dragged)
            {
                std::rotate (nodes.begin() + (long) k, nodes.begin() + (long) k + 1, nodes.end());
                dragNode = (int) nodes.size() - 1;
            }
    }

    void RoutingMap::autoArrange()
    {
        layoutStore.clearPositions();
        refresh();
        fitAll (true);
        showNotice ("Disposizione automatica", true);
    }

    //==============================================================================
    void RoutingMap::setZoom (float z, juce::Point<float> around)
    {
        z = juce::jlimit (minZoom, maxZoom, z);
        const auto w = toWorld (around);
        zoom = z;
        origin = w - (around - canvas.getPosition().toFloat()) / zoom;
        updateScrollBars();
        storeView();
        repaint();
    }

    void RoutingMap::fitAll (bool limitForReading)
    {
        if (canvas.isEmpty()) return;
        const auto cb = contentBounds();
        float z = juce::jmin ((float) canvas.getWidth() / juce::jmax (1.0f, cb.getWidth()), (float) canvas.getHeight() / juce::jmax (1.0f, cb.getHeight()));
        z = limitForReading ? juce::jlimit (readableZoom, 1.4f, z) : juce::jlimit (minZoom, maxZoom, z);
        zoom = z;
        const float vw = (float) canvas.getWidth() / zoom, vh = (float) canvas.getHeight() / zoom;
        // la catena ci sta: centrata; altrimenti si parte dall'INPUT (in alto a sinistra)
        origin = { cb.getWidth() <= vw ? cb.getCentreX() - vw * 0.5f : cb.getX(),
                   cb.getHeight() <= vh ? cb.getCentreY() - vh * 0.5f : cb.getY() };
        updateScrollBars();
        storeView();
        repaint();
    }

    void RoutingMap::scrollToNode (int slot)
    {
        auto* nd = nodeFor (slot);
        if (nd == nullptr || canvas.isEmpty()) return;
        const float vw = (float) canvas.getWidth() / zoom, vh = (float) canvas.getHeight() / zoom;
        origin = nd->world.getCentre() - juce::Point<float> (vw * 0.5f, vh * 0.5f);
        updateScrollBars();
        storeView();
        repaint();
    }

    void RoutingMap::storeView() { layoutStore.setView (zoom, origin); }

    void RoutingMap::updateScrollBars()
    {
        zoomLabel.setText (juce::String (juce::roundToInt (zoom * 100.0f)) + "%", juce::dontSendNotification);
        if (canvas.isEmpty()) return;
        const float vw = (float) canvas.getWidth() / zoom, vh = (float) canvas.getHeight() / zoom;
        const auto view = juce::Rectangle<float> (origin.x, origin.y, vw, vh);
        const auto total = contentBounds().getUnion (view);
        hbar.setRangeLimits (total.getX(), total.getRight(), juce::dontSendNotification);
        hbar.setCurrentRange (origin.x, vw, juce::dontSendNotification);
        vbar.setRangeLimits (total.getY(), total.getBottom(), juce::dontSendNotification);
        vbar.setCurrentRange (origin.y, vh, juce::dontSendNotification);
    }

    void RoutingMap::scrollBarMoved (juce::ScrollBar* sb, double start)
    {
        if (sb == &hbar) origin.x = (float) start;
        else origin.y = (float) start;
        storeView();
        repaint();
    }

    //==============================================================================
    void RoutingMap::resized()
    {
        auto c = card().reduced (18, 12);
        auto top = c.removeFromTop (36);
        closeButton.setBounds (top.removeFromRight (100).reduced (0, 3));
        top.removeFromRight (10);
        fitButton.setBounds (top.removeFromRight (80).reduced (0, 3));
        top.removeFromRight (6);
        zoomInButton.setBounds (top.removeFromRight (32).reduced (0, 3));
        zoomLabel.setBounds (top.removeFromRight (56));
        zoomOutButton.setBounds (top.removeFromRight (32).reduced (0, 3));
        top.removeFromRight (10);
        arrangeButton.setBounds (top.removeFromRight (190).reduced (0, 3));
        top.removeFromRight (10);
        title.setBounds (top);
        help.setBounds (c.removeFromTop (22));
        c.removeFromTop (8);
        const int sb = 12;
        auto bottom = c.removeFromBottom (sb);
        vbar.setBounds (c.removeFromRight (sb));
        hbar.setBounds (bottom.withTrimmedRight (sb));
        canvas = c;
        computeLayout();
        if (needFit && ! canvas.isEmpty()) { needFit = false; fitAll (true); }   // prima apertura: catena leggibile, dall'INPUT
        updateScrollBars();
    }

    //==============================================================================
    juce::Point<float> RoutingMap::portPoint (const Node& nd, bool output, int line) const
    {
        const auto& r = nd.world;
        return toScreen (juce::Point<float> (output ? r.getRight() : r.getX(), r.getY() + (line == 1 ? portYB : portYA)));
    }

    bool RoutingMap::endPoint (const End& e, juce::Point<float>& p) const
    {
        const Node* nd = nullptr;
        bool output = false;
        switch (e.kind)
        {
            case End::Input:   nd = nodeFor (inputNode); output = true; break;
            case End::Output:  nd = nodeFor (outputNode); break;
            case End::SlotIn:  nd = nodeFor (e.slot); break;
            case End::SlotOut: nd = nodeFor (e.slot); output = true; break;
        }
        if (nd == nullptr) return false;
        p = portPoint (*nd, output, e.kind == End::Input ? 0 : e.line);
        return true;
    }

    juce::Rectangle<float> RoutingMap::plugArea (int ci, bool destEnd) const
    {
        juce::Point<float> j;
        if (! juce::isPositiveAndBelow (ci, (int) conns.size()) || ! endPoint (destEnd ? conns[(size_t) ci].b : conns[(size_t) ci].a, j)) return {};
        const float s = plugScale(), len = juce::jmax (16.0f, cables::plugLength * s), h = juce::jmax (14.0f, 30.0f * s);
        return destEnd ? juce::Rectangle<float> (j.x - len, j.y - h * 0.5f, len + 3.0f, h)
                       : juce::Rectangle<float> (j.x - 3.0f, j.y - h * 0.5f, len + 3.0f, h);
    }

    bool RoutingMap::plugAt (juce::Point<float> p, int& ci, bool& destEnd) const
    {
        for (int i = (int) conns.size() - 1; i >= 0; --i)
            for (bool dest : { true, false })
                if (plugArea (i, dest).contains (p)) { ci = i; destEnd = dest; return true; }
        return false;
    }

    juce::Rectangle<float> RoutingMap::ledArea (const Node& nd) const
    {
        const auto r = toScreen (juce::Rectangle<float> (nd.world.getRight() - 26.0f, nd.world.getY() + 5.0f, 18.0f, 18.0f));
        return r.withSizeKeepingCentre (juce::jmax (16.0f, r.getWidth()), juce::jmax (16.0f, r.getHeight()));
    }

    int RoutingMap::nodeIndexAt (juce::Point<float> screen, float margin) const
    {
        if (! canvas.toFloat().contains (screen)) return -1;
        const auto w = toWorld (screen);
        for (int k = (int) nodes.size() - 1; k >= 0; --k)
            if (nodes[(size_t) k].world.expanded (margin).contains (w)) return k;
        return -1;
    }

    routing::Target RoutingMap::targetAt (juce::Point<float> p, juce::Rectangle<float>* area) const
    {
        routing::Target t;
        if (! canvas.toFloat().contains (p)) { t.kind = routing::Target::Outside; return t; }
        const int k = nodeIndexAt (p, 12.0f);
        if (k < 0) { t.kind = routing::Target::Empty; return t; }
        const auto& nd = nodes[(size_t) k];
        if (area != nullptr) *area = toScreen (nd.world).expanded (4.0f);
        if (nd.slot == inputNode) t.kind = routing::Target::InputPanel;
        else if (nd.slot == outputNode) t.kind = routing::Target::OutputPanel;
        else { t.kind = routing::Target::Slot; t.slot = nd.slot; }
        return t;
    }

    juce::Point<float> RoutingMap::emptyScreenPoint() const
    {
        // un punto del piano lontano dagli oggetti: si cerca su una griglia fitta della vista
        for (float fy = 0.97f; fy > 0.02f; fy -= 0.03f)
            for (float fx = 0.5f; fx < 1.0f; fx += 0.03f)
                for (float sx : { fx, 1.0f - fx })
                {
                    const juce::Point<float> p ((float) canvas.getX() + (float) canvas.getWidth() * sx, (float) canvas.getY() + (float) canvas.getHeight() * fy);
                    if (nodeIndexAt (p, 40.0f) < 0) return p;
                }
        return canvas.toFloat().getBottomRight() - juce::Point<float> (3.0f, 3.0f);
    }

    //==============================================================================
    bool RoutingMap::dropPlug (int ci, bool destEnd, juce::Point<float> p)
    {
        if (! juce::isPositiveAndBelow (ci, (int) conns.size())) return false;
        const auto t = targetAt (p, nullptr);
        const auto r = routing::resolve (chain, conns[(size_t) ci], destEnd, t, "Fuori dalla mappa: il cavo torna al suo posto");
        const bool changed = routing::apply (chain, r);
        if (changed)
        {
            if (r.kind == routing::Replug::Unplug)
            {
                auto* s = chain.slot (r.unpatch.empty() ? -1 : r.unpatch.front());
                showNotice (codeOf (s != nullptr ? s->def : nullptr) + " staccato dalla catena (STACCATO): il cavo si richiude da solo", true);
            }
            else showNotice ("Collegato: " + r.hint, true);
        }
        else if (r.hint.isNotEmpty())
            showNotice ("Collegamento non ammesso: " + r.hint + ". Il cavo torna dov'era.", false);
        refresh();             // subito, senza aspettare il messaggio della catena (anche la pedaliera si aggiorna)
        return changed;
    }

    bool RoutingMap::dropPlugForTest (int ci, bool destEnd, juce::Point<float> p)
    {
        return dropPlug (ci, destEnd, p);
    }

    void RoutingMap::dragNodeForTest (int slot, juce::Point<float> to)
    {
        auto* nd = nodeFor (slot);
        if (nd == nullptr) return;
        const auto r = toScreen (nd->world);
        const auto from = juce::Point<float> (r.getCentreX(), r.getY() + r.getHeight() * 0.62f);     // corpo, lontano da LED e prese
        beginDrag (from, {});
        continueDrag (to, from);
        endDrag (to, true);
    }

    void RoutingMap::clickLedForTest (int slot)
    {
        if (auto* nd = nodeFor (slot))
        {
            const auto p = ledArea (*nd).getCentre();
            beginDrag (p, {});
            endDrag (p, false);
        }
    }

    void RoutingMap::showNotice (const juce::String& text, bool ok)
    {
        toast.text = text;
        toast.ok = ok;
        toast.until = juce::Time::getMillisecondCounter() + (ok ? 2500u : 4500u);
        repaint();
    }

    //==============================================================================
    void RoutingMap::beginDrag (juce::Point<float> p, const juce::ModifierKeys& mods)
    {
        drag = Drag::None;
        mouse = p;
        if (! canvas.toFloat().contains (p) || mods.isPopupMenu()) return;
        int ci = -1; bool dest = false;
        if (plugAt (p, ci, dest))
        {
            drag = Drag::Plug;
            dragConn = ci;
            dragDest = dest;
            juce::Point<float> j;
            endPoint (dest ? conns[(size_t) ci].b : conns[(size_t) ci].a, j);
            grabOffset = p - j;
            hoverValid = false;
            repaint();
            return;
        }
        const int k = nodeIndexAt (p, 0.0f);
        if (k >= 0)
        {
            const auto& nd = nodes[(size_t) k];
            if (nd.slot >= 0 && ledArea (nd).contains (p))
            {
                if (auto* s = chain.slot (nd.slot))
                {
                    chain.setEnabled (nd.slot, ! s->enabled.load());
                    chain.notifyTouch (nd.slot, -1);
                }
                repaint();
                return;
            }
            // l'oggetto preso va in cima
            std::rotate (nodes.begin() + k, nodes.begin() + k + 1, nodes.end());
            drag = Drag::Node;
            dragNode = (int) nodes.size() - 1;
            grabOffset = toWorld (p) - nodes.back().world.getTopLeft();
            repaint();
            return;
        }
        drag = Drag::Pan;
        panStart = origin;
    }

    void RoutingMap::continueDrag (juce::Point<float> p, juce::Point<float> start)
    {
        mouse = p;
        if (drag == Drag::Plug)
        {
            hoverArea = {};
            const auto t = targetAt (p, &hoverArea);
            hover = routing::resolve (chain, conns[(size_t) dragConn], dragDest, t, "Fuori dalla mappa: il cavo torna al suo posto");
            hoverValid = true;
            repaint();
        }
        else if (drag == Drag::Node && juce::isPositiveAndBelow (dragNode, (int) nodes.size()))
        {
            auto& nd = nodes[(size_t) dragNode];
            const auto tl = toWorld (p) - grabOffset;
            nd.world.setPosition (tl);
            if (nd.slot >= 0) layoutStore.setSlotPos (nd.slot, tl);
            else layoutStore.setIoPos (nd.slot == inputNode ? 0 : 1, tl);
            updateScrollBars();
            repaint();
        }
        else if (drag == Drag::Pan)
        {
            origin = panStart - (p - start) / zoom;
            updateScrollBars();
            repaint();
        }
    }

    void RoutingMap::endDrag (juce::Point<float> p, bool moved)
    {
        const auto d = drag;
        drag = Drag::None;
        hoverValid = false;
        if (d == Drag::Plug)
        {
            if (moved) dropPlug (dragConn, dragDest, p);
            repaint();
        }
        else if (d == Drag::Pan) storeView();
        else if (d == Drag::Node) repaint();
    }

    void RoutingMap::mouseDown (const juce::MouseEvent& e) { grabKeyboardFocus(); beginDrag (e.position, e.mods); }
    void RoutingMap::mouseDrag (const juce::MouseEvent& e) { continueDrag (e.position, e.mouseDownPosition); }

    void RoutingMap::mouseUp (const juce::MouseEvent& e)
    {
        const bool wasNone = drag == Drag::None;
        endDrag (e.position, e.getDistanceFromDragStart() > 4);
        if (wasNone && ! card().contains (e.getPosition()) && onClose) onClose();
    }

    void RoutingMap::mouseDoubleClick (const juce::MouseEvent& e)
    {
        const int k = nodeIndexAt (e.position, 0.0f);
        if (k < 0) return;
        const auto& nd = nodes[(size_t) k];
        if (nd.slot >= 0 && ! ledArea (nd).contains (e.position) && onZoom) onZoom (nd.slot);
    }

    void RoutingMap::mouseMove (const juce::MouseEvent& e)
    {
        int ci = -1; bool dest = false;
        const int k = nodeIndexAt (e.position, 0.0f);
        const bool onPlug = plugAt (e.position, ci, dest);
        const bool onLed = k >= 0 && nodes[(size_t) k].slot >= 0 && ledArea (nodes[(size_t) k]).contains (e.position);
        setMouseCursor (onPlug ? juce::MouseCursor::DraggingHandCursor : onLed ? juce::MouseCursor::PointingHandCursor
                        : k >= 0 ? juce::MouseCursor::UpDownLeftRightResizeCursor : juce::MouseCursor::NormalCursor);
        const int hn = k >= 0 ? nodes[(size_t) k].slot : -1000;
        if (hn != hoverNode) { hoverNode = hn; repaint(); }
    }

    void RoutingMap::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
    {
        if (e.mods.isCommandDown() || e.mods.isCtrlDown())
        {
            setZoom (zoom * (w.deltaY > 0 ? 1.12f : 1.0f / 1.12f), e.position);
            return;
        }
        float dx = w.deltaX, dy = w.deltaY;
        if (e.mods.isShiftDown() && dx == 0.0f) std::swap (dx, dy);
        origin -= juce::Point<float> (dx, dy) * (600.0f / zoom);
        updateScrollBars();
        storeView();
        repaint();
    }

    void RoutingMap::mouseMagnify (const juce::MouseEvent& e, float s) { setZoom (zoom * s, e.position); }

    bool RoutingMap::keyPressed (const juce::KeyPress& k)
    {
        if (k == juce::KeyPress::escapeKey)
        {
            if (drag != Drag::None) { drag = Drag::None; hoverValid = false; repaint(); return true; }
            if (onClose) onClose();
            return true;
        }
        const auto ch = k.getTextCharacter();
        if (ch == '+' || ch == '=') { setZoom (zoom * 1.2f, canvasArea().getCentre()); return true; }
        if (ch == '-') { setZoom (zoom / 1.2f, canvasArea().getCentre()); return true; }
        if (ch == '0') { fitAll (false); return true; }
        const float step = 80.0f / zoom;
        juce::Point<float> d;
        if (k == juce::KeyPress::leftKey) d = { -step, 0 };
        else if (k == juce::KeyPress::rightKey) d = { step, 0 };
        else if (k == juce::KeyPress::upKey) d = { 0, -step };
        else if (k == juce::KeyPress::downKey) d = { 0, step };
        else return false;
        origin += d;
        updateScrollBars();
        storeView();
        repaint();
        return true;
    }

    //==============================================================================
    void RoutingMap::timerCallback()
    {
        // LED e cavi possono cambiare anche da MIDI o dal pannello di zoom senza messaggi della catena
        juce::String key;
        key.preallocateBytes ((size_t) nodes.size() * 2 + 8);
        for (const auto& nd : nodes)
            if (nd.slot >= 0)
                if (auto* s = chain.slot (nd.slot))
                    key << (s->enabled.load() ? '1' : '0') << (s->patched.load() ? 'p' : 's');
        if (toast.text.isNotEmpty() && juce::Time::getMillisecondCounter() > toast.until)
        {
            toast.text = {};
            repaint();
        }
        if (key != stateKey) { stateKey = key; repaint(); }
    }

    juce::Image RoutingMap::thumbnailFor (const ModelDef* vis)
    {
        if (vis == nullptr || vis->image == nullptr) return {};
        const juce::String key (vis->image);
        if (auto it = thumbs.find (key); it != thumbs.end()) return it->second;
        auto img = Assets::pedalImage (vis->image);
        if (img.isValid())
        {
            // miniatura in alta qualita' una volta sola (240 px di altezza: nitida fino a zoom 2 su schermi HiDPI)
            const float s = juce::jmin (1.0f, 240.0f / (float) img.getHeight());
            if (s < 1.0f)
                img = img.rescaled (juce::jmax (1, juce::roundToInt ((float) img.getWidth() * s)), juce::jmax (1, juce::roundToInt ((float) img.getHeight() * s)),
                                    juce::Graphics::highResamplingQuality);
        }
        thumbs[key] = img;
        return img;
    }

    double RoutingMap::minContrast (const Theme& t)
    {
        // testi degli oggetti (fondo = intestazione), titolo e guida (pannello), tasti, avvisi (fondo scuro fisso),
        // lettera della corsia sulla sua pastiglia
        const double v[] = { contrastRatio (t.text, t.header), contrastRatio (t.text, t.panelTop), contrastRatio (t.text, t.panelBottom),
                             contrastRatio (t.text, t.button), contrastRatio (juce::Colours::white, noticeBg.withAlpha (1.0f)),
                             contrastRatio (juce::Colour (0xff111214), laneA), contrastRatio (juce::Colours::white, laneB.darker (0.25f)) };
        double m = 99;
        for (double x : v) m = juce::jmin (m, x);
        return m;
    }

    //==============================================================================
    void RoutingMap::paint (juce::Graphics& g)
    {
        const auto& t = themes->current();
        for (auto* lab : { &title, &help, &zoomLabel })
            if (lab->findColour (juce::Label::textColourId) != t.text) lab->setColour (juce::Label::textColourId, t.text);
        for (auto* sb : { &hbar, &vbar })
            if (sb->findColour (juce::ScrollBar::thumbColourId) != t.text.withAlpha (0.35f))
            {
                sb->setColour (juce::ScrollBar::thumbColourId, t.text.withAlpha (0.35f));
                sb->setColour (juce::ScrollBar::trackColourId, juce::Colours::transparentBlack);
            }

        g.fillAll (juce::Colours::black.withAlpha (0.62f));
        const auto c = card().toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRoundedRectangle (c.translated (0, 6).expanded (4), 14.0f);
        g.setGradientFill (juce::ColourGradient (t.panelTop.brighter (0.06f), c.getTopLeft(), t.panelBottom, c.getBottomLeft(), false));
        g.fillRoundedRectangle (c, 12.0f);
        g.setColour (t.accent.withAlpha (0.55f));
        g.drawRoundedRectangle (c.reduced (1.0f), 12.0f, 1.4f);

        // piano della mappa: fondo scuro del tema con una griglia di punti
        const auto cv = canvas.toFloat();
        g.setColour (t.window.interpolatedWith (t.panelBottom, 0.5f));
        g.fillRoundedRectangle (cv, 6.0f);
        {
            juce::Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (canvas);
            const float step = 40.0f * zoom;
            if (step >= 8.0f)
            {
                g.setColour (t.text.withAlpha (0.07f));
                const float x0 = cv.getX() - std::fmod (origin.x * zoom, step) - step, y0 = cv.getY() - std::fmod (origin.y * zoom, step) - step;
                const float d = juce::jmax (1.2f, 1.6f * zoom);
                for (float y = y0; y < cv.getBottom(); y += step)
                    for (float x = x0; x < cv.getRight(); x += step)
                        g.fillEllipse (x - d * 0.5f, y - d * 0.5f, d, d);
            }
            paintCables (g, t);
            for (const auto& nd : nodes)
                if (toScreen (nd.world).intersects (cv.expanded (40.0f)))
                    paintNode (g, nd, t);
            // spine sopra gli oggetti
            const float s = plugScale();
            for (int i = 0; i < (int) conns.size(); ++i)
            {
                if (drag == Drag::Plug && i == dragConn) continue;
                const auto& cn = conns[(size_t) i];
                const int line = (cn.a.line == 1 || cn.b.line == 1 || routing::segmentOf (chain, cn.b) == 2) ? 1 : 0;
                juce::Point<float> pa, pb;
                if (endPoint (cn.a, pa)) cables::drawPlug (g, pa, 1.0f, s, line);
                if (endPoint (cn.b, pb)) cables::drawPlug (g, pb, -1.0f, s, line);
            }
            // cavo tenuto in mano
            if (drag == Drag::Plug && juce::isPositiveAndBelow (dragConn, (int) conns.size()))
            {
                const auto& cn = conns[(size_t) dragConn];
                const int line = (cn.a.line == 1 || cn.b.line == 1 || routing::segmentOf (chain, cn.b) == 2) ? 1 : 0;
                juce::Point<float> pa, pb;
                endPoint (cn.a, pa);
                endPoint (cn.b, pb);
                (dragDest ? pb : pa) = mouse - grabOffset;
                const auto& col = themes->cableFor (dragConn);
                Cable cab { pa, pb, 1.0f, -1.0f, s, line, false, 0.0f, dragConn, col.body, col.sheen };
                if (hoverValid && (hover.kind != routing::Replug::Cancel || hover.hint.isNotEmpty()) && ! hoverArea.isEmpty())
                    {
                        const auto hc = hover.ok ? okColour : badColour;
                        g.setColour (hc.withAlpha (0.16f));
                        g.fillRoundedRectangle (hoverArea, 8.0f);
                        g.setColour (hc);
                        g.drawRoundedRectangle (hoverArea, 8.0f, 2.5f);
                    }
                cables::drawCable (g, cab, col.body, col.sheen);
                cables::drawPlug (g, pa, 1.0f, s, line);
                cables::drawPlug (g, pb, -1.0f, s, line);
            }
        }

        auto pill = [&g, this] (const juce::String& text, juce::Point<float> centre, juce::Colour border)
        {
            const juce::Font f (14.0f, juce::Font::bold);
            const float w = juce::jmin ((float) canvas.getWidth() - 16.0f, (float) f.getStringWidth (text) + 24.0f);
            auto r = juce::Rectangle<float> (w, 28.0f).withCentre (centre);
            r = r.withX (juce::jlimit ((float) canvas.getX() + 8.0f, juce::jmax ((float) canvas.getX() + 8.0f, (float) canvas.getRight() - w - 8.0f), r.getX()))
                 .withY (juce::jlimit ((float) canvas.getY() + 4.0f, juce::jmax ((float) canvas.getY() + 4.0f, (float) canvas.getBottom() - 32.0f), r.getY()));
            g.setColour (noticeBg);
            g.fillRoundedRectangle (r, 7.0f);
            g.setColour (border);
            g.drawRoundedRectangle (r, 7.0f, 1.5f);
            g.setColour (juce::Colours::white);
            g.setFont (f);
            g.drawFittedText (text, r.toNearestInt().reduced (10, 0), juce::Justification::centred, 1, 0.75f);
        };
        if (drag == Drag::Plug && hoverValid && hover.hint.isNotEmpty())
            pill (hover.hint, mouse.translated (0.0f, -36.0f), hover.ok ? okColour : badColour);
        if (toast.text.isNotEmpty())
            pill (toast.text, { cv.getCentreX(), cv.getBottom() - 30.0f }, toast.ok ? okColour : badColour);
    }

    void RoutingMap::paintCables (juce::Graphics& g, const Theme&)
    {
        const float s = plugScale();
        for (int i = 0; i < (int) conns.size(); ++i)
        {
            if (drag == Drag::Plug && i == dragConn) continue;
            const auto& cn = conns[(size_t) i];
            juce::Point<float> pa, pb;
            if (! endPoint (cn.a, pa) || ! endPoint (cn.b, pb)) continue;
            const int line = (cn.a.line == 1 || cn.b.line == 1 || routing::segmentOf (chain, cn.b) == 2) ? 1 : 0;
            const auto& col = themes->cableFor (i);
            Cable cab { pa, pb, 1.0f, -1.0f, s, line, false, 0.0f, i, col.body, col.sheen };
            // cavo che torna indietro (a capo) o che attraverserebbe altri oggetti: corre negli spazi tra
            // righe e colonne (scende accanto alla presa, passa nello spazio tra le righe, risale alla presa)
            const auto* na = nodeFor (cn.a.kind == End::Input ? inputNode : cn.a.slot);
            const auto* nb = nodeFor (cn.b.kind == End::Output ? outputNode : cn.b.slot);
            bool crosses = pb.x < pa.x - 2.0f;
            if (! crosses)
            {
                const juce::Line<float> straight (pa, pb);
                for (const auto& nd : nodes)
                    if (&nd != na && &nd != nb && toScreen (nd.world).reduced (2.0f).intersects (straight)) { crosses = true; break; }
            }
            if (crosses && na != nullptr && nb != nullptr)
            {
                const float half = (stepY - nodeH) * 0.5f * zoom;
                const auto ra = toScreen (na->world), rb = toScreen (nb->world);
                cab.under = true;
                if (std::abs (pb.y - pa.y) <= nodeH * zoom * 0.5f) cab.laneY = juce::jmax (ra.getBottom(), rb.getBottom()) + half;   // stessa riga: sotto
                else if (pb.y > pa.y) cab.laneY = rb.getY() - half;      // verso il basso: nello spazio sopra la riga d'arrivo
                else cab.laneY = ra.getY() - half;                       // verso l'alto: nello spazio sopra la riga di partenza
            }
            const auto bounds = juce::Rectangle<float> (pa, pb).expanded (60.0f * zoom + 40.0f).getUnion (
                juce::Rectangle<float> (pa.x, cab.laneY, 1.0f, 1.0f));
            if (! bounds.intersects (canvas.toFloat())) continue;
            cables::drawCable (g, cab, col.body, col.sheen);
        }
    }

    void RoutingMap::paintNode (juce::Graphics& g, const Node& nd, const Theme& t)
    {
        const auto r = toScreen (nd.world);
        const float z = zoom;
        const bool io = nd.slot < 0;
        const bool hot = nd.slot == hoverNode || (drag == Drag::Node && &nd == &nodes.back());
        auto* s = io ? nullptr : chain.slot (nd.slot);
        const bool unpatched = s != nullptr && ! s->patched.load() && ! s->isSplitter();
        const bool on = s != nullptr && s->enabled.load() && ! unpatched;

        // ombra, corpo, bordo (corsia o accento quando il mouse e' sopra)
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (r.translated (2.0f * z, 4.0f * z), 9.0f * z);
        g.setGradientFill (juce::ColourGradient (t.header.brighter (0.10f), r.getTopLeft(), t.header, r.getBottomLeft(), false));
        g.fillRoundedRectangle (r, 9.0f * z);
        const auto border = hot ? t.accent : nd.lane == 1 ? laneB.withAlpha (0.8f) : nd.lane == 0 ? laneA.withAlpha (0.65f) : t.text.withAlpha (0.25f);
        g.setColour (border);
        g.drawRoundedRectangle (r.reduced (0.5f), 9.0f * z, hot ? 2.4f : 1.3f);

        // prese (dado esagonale) sui fianchi: ingresso a sinistra, uscita a destra
        const float ss = 0.55f * z;
        for (int line = 0; line < 2; ++line)
        {
            if (nd.slot != inputNode && (line == 0 || nd.inB)) cables::drawSocket (g, portPoint (nd, false, line), ss);
            if (nd.slot != outputNode && (line == 0 || nd.outB)) cables::drawSocket (g, portPoint (nd, true, line), ss);
        }
        auto font = [z] (float px, bool bold) { return juce::Font (juce::jmax (6.0f, px * z), bold ? juce::Font::bold : juce::Font::plain); };

        if (io)
        {
            const bool in = nd.slot == inputNode;
            g.setColour (t.text);
            g.setFont (font (17.0f, true));
            g.drawFittedText (in ? "INPUT" : "OUTPUT", r.withHeight (40.0f * z).translated (0, 16.0f * z).toNearestInt(), juce::Justification::centred, 1, 0.7f);
            g.setFont (font (12.0f, false));
            const bool two = in ? nd.outB : nd.inB;
            const juce::String sub = in ? "chitarra" : (two ? (routing::isDual (chain) ? "A = L   B = R" : "stereo L / R") : "mono L + R");
            g.drawFittedText (sub, r.withHeight (22.0f * z).translated (0, 50.0f * z).toNearestInt(), juce::Justification::centred, 1, 0.7f);
            // lettere delle prese
            g.setFont (font (11.0f, true));
            for (int line = 0; line < (two ? 2 : 1); ++line)
            {
                const auto p = portPoint (nd, in, line);
                const auto lr = juce::Rectangle<float> (16.0f * z, 14.0f * z).withCentre (p.translated (in ? -18.0f * z : 18.0f * z, 0.0f));
                g.setColour (line == 1 ? laneB : laneA);
                g.drawText (line == 1 ? "B" : "A", lr, juce::Justification::centred);
            }
            g.setColour (t.text.withAlpha (0.5f));
            const auto jack = r.withTrimmedTop (100.0f * z).withSizeKeepingCentre (34.0f * z, 34.0f * z);
            g.drawEllipse (jack, 2.0f * z);
            g.fillEllipse (jack.withSizeKeepingCentre (12.0f * z, 12.0f * z));
            return;
        }

        // intestazione: numero dello slot (e corsia), LED
        auto head = r.withHeight (headerH * z);
        g.setColour (t.text);
        g.setFont (font (15.0f, true));
        const auto numBox = juce::Rectangle<float> (r.getX() + 8.0f * z, r.getY() + 4.0f * z, 34.0f * z, 20.0f * z);
        g.drawText (juce::String (nd.slot + 1), numBox, juce::Justification::centredLeft);
        if (nd.lane >= 0)
        {
            const auto badge = juce::Rectangle<float> (numBox.getX() + (float) font (15.0f, true).getStringWidth (juce::String (nd.slot + 1)) + 4.0f * z,
                                                       numBox.getY() + 2.0f * z, 18.0f * z, 16.0f * z);
            g.setColour (nd.lane == 1 ? laneB.darker (0.25f) : laneA);
            g.fillRoundedRectangle (badge, 3.0f * z);
            g.setColour (nd.lane == 1 ? juce::Colours::white : juce::Colour (0xff111214));
            g.setFont (font (12.0f, true));
            g.drawText (nd.lane == 1 ? "B" : "A", badge, juce::Justification::centred);
        }
        // LED di stato (cliccabile)
        const auto led = toScreen (juce::Rectangle<float> (nd.world.getRight() - 26.0f, nd.world.getY() + 5.0f, 18.0f, 18.0f));
        if (on)
        {
            juce::ColourGradient halo (juce::Colour (0x88ff2a14), led.getCentre(), juce::Colour (0x00ff2a14), led.getCentre().translated (led.getWidth(), 0), true);
            g.setGradientFill (halo);
            g.fillEllipse (led.expanded (led.getWidth() * 0.5f));
        }
        g.setColour (juce::Colour (0xff0a0a0b));
        g.fillEllipse (led);
        auto core = led.reduced (led.getWidth() * 0.18f);
        g.setGradientFill (juce::ColourGradient (on ? juce::Colour (0xfffff0e0) : juce::Colour (0xff5a2a26), core.getCentre().translated (-core.getWidth() * 0.15f, -core.getHeight() * 0.2f),
                                                 on ? juce::Colour (0xffff2a10) : juce::Colour (0xff2a1210), core.getBottomRight(), true));
        g.fillEllipse (core);
        juce::ignoreUnused (head);

        // miniatura del pedale
        const auto imgArea = toScreen (juce::Rectangle<float> (nd.world.getX() + 10.0f, nd.world.getY() + imageTop, nodeW - 20.0f, imageBottom - imageTop));
        juce::Rectangle<float> drawn = imgArea;
        const auto thumb = thumbnailFor (nd.vis);
        if (thumb.isValid())
        {
            const float sc = juce::jmin (imgArea.getWidth() / (float) thumb.getWidth(), imgArea.getHeight() / (float) thumb.getHeight());
            drawn = juce::Rectangle<float> ((float) thumb.getWidth() * sc, (float) thumb.getHeight() * sc).withCentre (imgArea.getCentre());
            g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);
            g.setOpacity (1.0f);
            g.drawImage (thumb, drawn);
        }
        else if (nd.vis != nullptr)
        {
            const auto body = juce::Colour (nd.vis->colour).withAlpha (1.0f);
            drawn = imgArea.withSizeKeepingCentre (imgArea.getHeight() * 0.62f, imgArea.getHeight());
            g.setColour (body);
            g.fillRoundedRectangle (drawn, 5.0f * z);
        }
        if (s != nullptr && ! s->enabled.load())
        {
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillRoundedRectangle (drawn, 4.0f * z);
        }
        if (unpatched)
        {
            g.setColour (juce::Colours::black.withAlpha (0.45f));
            g.fillRoundedRectangle (drawn, 4.0f * z);
            const auto badge = juce::Rectangle<float> ((nodeW - 24.0f) * z, 22.0f * z).withCentre (imgArea.getCentre());
            g.setColour (t.header.withAlpha (0.95f));
            g.fillRoundedRectangle (badge, 5.0f * z);
            g.setColour (badColour);
            g.drawRoundedRectangle (badge, 5.0f * z, 1.5f);
            g.setColour (t.text);
            g.setFont (font (13.0f, true));
            g.drawText ("STACCATO", badge, juce::Justification::centred);
        }

        // sigla e nome (in REAL MOD quelli della replica)
        juce::String code = codeOf (nd.def), name = nd.def != nullptr ? visualName (nd.def) : juce::String();
        if (s != nullptr && s->isSplitter())
        {
            const auto m = chain.splitMode();
            name = juce::String ("Splitter ") + (m == SplitMode::Dual ? "DUAL" : m == SplitMode::Stereo ? "STEREO" : "MONO");
        }
        g.setColour (t.text);
        g.setFont (font (14.0f, true));
        g.drawFittedText (code, toScreen (juce::Rectangle<float> (nd.world.getX() + 6.0f, nd.world.getY() + imageBottom + 2.0f, nodeW - 12.0f, 20.0f)).toNearestInt(),
                          juce::Justification::centred, 1, 0.7f);
        g.setFont (font (12.0f, false));
        g.drawFittedText (name, toScreen (juce::Rectangle<float> (nd.world.getX() + 6.0f, nd.world.getY() + imageBottom + 21.0f, nodeW - 12.0f, 20.0f)).toNearestInt(),
                          juce::Justification::centred, 1, 0.7f);
    }
}
