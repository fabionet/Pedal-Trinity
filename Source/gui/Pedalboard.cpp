/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)
*/

#include "Pedalboard.h"
#include "../PluginProcessor.h"

namespace pt::ui
{
    using namespace pt::engine;

    namespace
    {
        /** Corsie DUAL: indici degli slot dopo lo splitter nella corsia A e nella B, in ordine. */
        void laneLists (const Chain& chain, int sp, std::vector<int>& a, std::vector<int>& b)
        {
            a.clear(); b.clear();
            for (int i = sp + 1; i < chain.size(); ++i)
                (chain.lane (i) == 1 ? b : a).push_back (i);
        }
    }

    Pedalboard::Pedalboard (PedalTrinityProcessor& p, juce::Component* popup, Callbacks c)
        : processor (p), chain (p.chain), popupParent (popup), cb (std::move (c)),
          inPanel (MeterPanel::Side::Input, p.apvts, p.chain.inputMeter),
          outPanel (MeterPanel::Side::Output, p.apvts, p.outputMeter)
    {
        addAndMakeVisible (inPanel);
        addAndMakeVisible (outPanel);
        addAndMakeVisible (overlay);

        pageLeft.setTooltip ("Pagina precedente della catena");
        pageRight.setTooltip ("Pagina successiva della catena");
        pageLeft.onClick = [this] { if (cb.scroll) cb.scroll (-pageLength()); };
        pageRight.onClick = [this] { if (cb.scroll) cb.scroll (pageLength()); };
        addA.setTooltip ("Aggiungi uno slot in fondo alla catena");
        addB.setTooltip ("Aggiungi uno slot in fondo alla corsia B");
        auto add = [this] (int lane)
        {
            const int at = chain.insert (-1, {}, lane);
            if (at < 0) return;
            const int unit = unitOfSlot (at);
            if (cb.scroll && unit >= first + pageLength()) cb.scroll (unit - (first + pageLength()) + 1);
        };
        addA.onClick = [add] { add (0); };
        addB.onClick = [add] { add (1); };
        for (auto* b : { &pageLeft, &pageRight, &addA, &addB })
            addChildComponent (b);
        themeChanged();
        startTimerHz (30);
    }

    Pedalboard::~Pedalboard()
    {
        stopTimer();
        slotComps.clear();
    }

    //==============================================================================
    bool Pedalboard::isDual() const
    {
        return chain.splitterIndex() >= 0 && chain.splitMode() == SplitMode::Dual;
    }

    int Pedalboard::pageLength() const
    {
        const int c = view == 18 ? 9 : view;
        return isDual() ? c : (view == 18 ? 18 : view);
    }

    int Pedalboard::contentLength() const
    {
        const int n = chain.size();
        if (! isDual()) return n;
        const int sp = chain.splitterIndex();
        std::vector<int> a, b;
        laneLists (chain, sp, a, b);
        return sp + 1 + (int) std::max (a.size(), b.size());
    }

    int Pedalboard::unitOfSlot (int slotIndex) const
    {
        if (! isDual()) return slotIndex;
        const int sp = chain.splitterIndex();
        if (slotIndex <= sp) return slotIndex;
        std::vector<int> a, b;
        laneLists (chain, sp, a, b);
        const auto& l = chain.lane (slotIndex) == 1 ? b : a;
        const auto it = std::find (l.begin(), l.end(), slotIndex);
        return sp + 1 + (int) std::distance (l.begin(), it);
    }

    void Pedalboard::setPage (int f, int v)
    {
        first = juce::jmax (0, f);
        view = v;
        layout();
    }

    void Pedalboard::refresh() { layout(); }

    void Pedalboard::themeChanged()
    {
        const auto& t = themes->current();
        for (auto* b : { &pageLeft, &pageRight, &addA, &addB })
        {
            b->setColour (juce::TextButton::buttonColourId, t.panelTop.withAlpha (0.92f));
            b->setColour (juce::TextButton::textColourOffId, t.accent);
        }
        const auto& c = themes->cable();
        overlay.setStyle (c.body, c.sheen, themes->showCables());
        boardKey = {};
        layout();                    // REAL MOD: gli slot ridisegnano i pedali (repliche o originali)
        repaint();
    }
    void Pedalboard::resized() { layout(); }

    juce::Rectangle<int> Pedalboard::boardArea() const { return cellGrid; }

    SlotComponent* Pedalboard::componentFor (int slotIndex) const
    {
        for (auto* s : slotComps)
            if (s->getIndex() == slotIndex) return s;
        return nullptr;
    }

    float Pedalboard::rowJackY (int row, int line) const
    {
        for (const auto& c : cells)
            if (c.row == row && c.slot != -2)
                return (float) c.bounds.getY() + SlotComponent::nominalJack (c.bounds, false, line).y;
        return (float) cellGrid.getY() + (float) cellGrid.getHeight() * 0.3f;
    }

    //==============================================================================
    void Pedalboard::layout()
    {
        const int n = chain.size();
        splitIndex = chain.splitterIndex();
        dual = isDual();

        auto area = getLocalBounds();
        const int pw = juce::jlimit (76, 112, getWidth() / 15);
        inPanel.setBounds (area.removeFromLeft (pw));
        outPanel.setBounds (area.removeFromRight (pw));
        boardRect = area.reduced (4, 0);
        cellGrid = area.reduced (juce::jmax (22, getWidth() / 60), 0);

        cols = view == 18 ? 9 : view;
        rows = dual ? 2 : (view == 18 ? 2 : 1);
        const int cw = cellGrid.getWidth() / cols, ch = cellGrid.getHeight() / rows;

        cells.clear();
        auto addCell = [&] (int slot, int row, int col, int lane)
        {
            Cell c;
            c.slot = slot; c.row = row; c.col = col; c.lane = lane;
            c.bounds = { cellGrid.getX() + col * cw + 2, cellGrid.getY() + row * ch + 2, cw - 4, ch - 4 };
            cells.push_back (c);
        };
        if (! dual)
        {
            for (int k = 0; k < rows * cols; ++k)
            {
                const int idx = first + k;
                addCell (idx < n ? idx : -1, k / cols, k % cols, idx < n ? chain.lane (idx) : 0);
            }
        }
        else
        {
            std::vector<int> a, b;
            laneLists (chain, splitIndex, a, b);
            for (int c = 0; c < cols; ++c)
            {
                const int col = first + c;
                if (col <= splitIndex)
                {
                    addCell (col, 0, c, 0);
                    addCell (-2, 1, c, 1);
                }
                else
                {
                    const int k = col - splitIndex - 1;
                    addCell (k < (int) a.size() ? a[(size_t) k] : -1, 0, c, 0);
                    addCell (k < (int) b.size() ? b[(size_t) k] : -1, 1, c, 1);
                }
            }
        }

        // componenti degli slot visibili (riusati: si ricollegano agli indici)
        std::vector<const Cell*> used;
        for (const auto& c : cells)
            if (c.slot >= 0) used.push_back (&c);
        while (slotComps.size() > (int) used.size()) slotComps.removeLast();
        for (size_t k = 0; k < used.size(); ++k)
        {
            const Cell& c = *used[k];
            if ((int) k < slotComps.size()) slotComps[(int) k]->bindTo (c.slot);
            else
            {
                SlotComponent::Callbacks scb;
                scb.zoom = [this] (int i) { if (cb.zoom) cb.zoom (i); };
                scb.removed = [this] (int i) { chain.remove (i); };
                scb.dropped = [this] (int from, int to)
                {
                    const int lane = dual && to > splitIndex ? chain.lane (to) : chain.lane (from);
                    chain.moveTo (from, to, lane);
                };
                auto* s = slotComps.add (new SlotComponent (chain, c.slot, popupParent, scb));
                addAndMakeVisible (s);
            }
            auto* s = slotComps[(int) k];
            s->setBounds (c.bounds);
            s->setLaneLabel (dual && c.slot > splitIndex ? (c.lane == 1 ? "B" : "A") : juce::String());
        }

        // "+" nella prima cella libera di ogni fila/corsia
        addA.setVisible (false);
        addB.setVisible (false);
        if (chain.canAdd())
            for (int lane = 0; lane < (dual ? 2 : 1); ++lane)
                for (const auto& c : cells)
                    if (c.slot == -1 && (! dual || c.row == lane))
                    {
                        auto& b = lane == 0 ? addA : addB;
                        const int sz = juce::jlimit (34, 56, c.bounds.getWidth() / 4);
                        b.setBounds (c.bounds.withSizeKeepingCentre (sz, sz).withY (c.bounds.getBottom() - sz - c.bounds.getHeight() / 6));
                        b.setVisible (true);
                        break;
                    }

        // prese dei pannelli allineate alle prese dei pedali
        const float sc = cells.empty() ? 0.6f : SlotComponent::nominalScale (cells.front().bounds);
        int endRow = 0;
        if (! dual && rows == 2)
            endRow = n - first > cols ? 1 : 0;
        inPanel.setSockets (rowJackY (0, 0) - (float) inPanel.getY(), -1.0f, sc);
        const float outA = rowJackY (endRow, 0) - (float) outPanel.getY();
        float outB = -1.0f;
        if (dual) outB = rowJackY (1, 0) - (float) outPanel.getY();
        else if (chain.isSplitActive()) outB = rowJackY (endRow, 1) - (float) outPanel.getY();
        outPanel.setSockets (outA, outB, sc);
        outPanel.setSplitControlsVisible (chain.isSplitActive());
        outPanel.setChannels (chain.isSplitActive() ? 2 : 1);
        inPanel.setChannels (chain.isSplitActive() && splitIndex == 0 ? 2 : 1);

        // tasti di pagina solo se la catena non sta tutta nella vista
        const int btn = 40;
        pageLeft.setVisible (first > 0);
        pageRight.setVisible (first + pageLength() < contentLength());
        pageLeft.setBounds (cellGrid.getX() - btn / 2, cellGrid.getCentreY() - btn / 2, btn, btn);
        pageRight.setBounds (cellGrid.getRight() - btn / 2, cellGrid.getCentreY() - btn / 2, btn, btn);

        overlay.setBounds (getLocalBounds());
        overlay.toFront (false);
        for (auto* b : { &addA, &addB, &pageLeft, &pageRight }) b->toFront (false);
        updateCables();
        repaint();
    }

    //==============================================================================
    bool Pedalboard::endPoint (const End& e, const End& other, juce::Point<float>& p, float& dir, float& scale, int& row) const
    {
        juce::ignoreUnused (other);
        scale = cells.empty() ? 0.6f : SlotComponent::nominalScale (cells.front().bounds);
        switch (e.kind)
        {
            case End::Input:
                if (first > 0) return false;
                p = inPanel.socketPoint (0) + inPanel.getPosition().toFloat();
                dir = 1.0f; row = 0;
                return true;
            case End::Output:
                if (first + pageLength() < contentLength()) return false;
                p = outPanel.socketPoint (e.line) + outPanel.getPosition().toFloat();
                dir = -1.0f;
                row = dual ? e.line : (rows == 2 && chain.size() - first > cols ? 1 : 0);
                return true;
            case End::SlotIn:
            case End::SlotOut:
            {
                auto* comp = componentFor (e.slot);
                if (comp == nullptr) return false;
                if (! comp->jackPoint (e.kind == End::SlotOut, e.line, p)) return false;
                p += comp->getPosition().toFloat();
                dir = e.kind == End::SlotOut ? 1.0f : -1.0f;
                scale = comp->pedalScale();
                for (const auto& c : cells) if (c.slot == e.slot) row = c.row;
                return true;
            }
        }
        return false;
    }

    void Pedalboard::updateCables()
    {
        struct Conn { End a, b; };
        std::vector<Conn> conns;
        const int n = chain.size();
        const auto mode = chain.splitMode();
        End srcA { End::Input }, srcB { End::Input }, laneSrc[2] { { End::Input }, { End::Input } };
        bool hasB = false, splitSeen = false;
        for (int i = 0; i < n; ++i)
        {
            auto* s = chain.slot (i);
            if (s == nullptr || s->fx == nullptr) continue;          // slot vuoto: il cavo lo attraversa
            const End in0 { End::SlotIn, i, 0 }, in1 { End::SlotIn, i, 1 }, out0 { End::SlotOut, i, 0 }, out1 { End::SlotOut, i, 1 };
            if (s->isSplitter())
            {
                conns.push_back ({ srcA, in0 });
                splitSeen = true;
                if (mode == SplitMode::Dual) { laneSrc[0] = out0; laneSrc[1] = out1; }
                else if (mode == SplitMode::Stereo) { srcA = out0; srcB = out1; hasB = true; }
                else { srcA = out0; hasB = false; }
                continue;
            }
            if (splitSeen && mode == SplitMode::Dual)
            {
                const int l = juce::jlimit (0, 1, s->lane.load());
                conns.push_back ({ laneSrc[l], in0 });
                laneSrc[l] = out0;
            }
            else if (splitSeen && mode == SplitMode::Stereo && s->def->stereo)
            {
                conns.push_back ({ srcA, in0 });
                if (hasB) conns.push_back ({ srcB, in1 });
                srcA = out0; srcB = out1; hasB = true;
            }
            else
            {
                conns.push_back ({ srcA, in0 });
                srcA = out0; hasB = false;
            }
        }
        if (splitSeen && mode == SplitMode::Dual)
        {
            conns.push_back ({ laneSrc[0], { End::Output, -1, 0 } });
            conns.push_back ({ laneSrc[1], { End::Output, -1, 1 } });
        }
        else
        {
            conns.push_back ({ srcA, { End::Output, -1, 0 } });
            if (hasB) conns.push_back ({ srcB, { End::Output, -1, 1 } });
        }

        auto unitOf = [this] (const End& e)
        {
            if (e.kind == End::Input) return -1;
            if (e.kind == End::Output) return 1 << 20;
            return unitOfSlot (e.slot);
        };
        auto colOf = [this] (const End& e, int fallback)
        {
            if (e.kind == End::Input) return -1;
            if (e.kind == End::Output) return cols;
            for (const auto& c : cells) if (c.slot == e.slot) return c.col;
            return fallback;
        };
        auto laneLine = [this] (const End& e)
        {
            if (e.kind == End::Output) return e.line;
            if (e.kind == End::Input) return 0;
            if (dual && e.slot > splitIndex) return chain.lane (e.slot);
            return e.line;
        };

        std::vector<Cable> over;
        underCables.clear();
        for (const auto& c : conns)
        {
            juce::Point<float> pa, pb;
            float da = 0, db = 0, sa = 0.6f, sb = 0.6f;
            int ra = 0, rb = 0;
            const bool va = endPoint (c.a, c.b, pa, da, sa, ra);
            const bool vb = endPoint (c.b, c.a, pb, db, sb, rb);
            const int line = juce::jmax (laneLine (c.a) == 1 || c.a.line == 1 ? 1 : 0, laneLine (c.b) == 1 || c.b.line == 1 ? 1 : 0);
            const float left = (float) cellGrid.getX() - 6.0f, right = (float) cellGrid.getRight() + 6.0f;
            if (! va && ! vb)
            {
                // tratto che attraversa tutta la pagina (slot vuoti o pagine intermedie)
                const bool before = unitOf (c.b) < first, after = unitOf (c.a) >= first + pageLength();
                if (before || after) continue;
                const float y = rowJackY (dual ? line : 0, dual ? 0 : c.a.line);
                over.push_back ({ { left, y }, { right, y }, 0.0f, 0.0f, sa, line, false });
                continue;
            }
            if (! va) { pa = { left, pb.y }; da = 0.0f; sa = sb; ra = rb; }
            if (! vb) { pb = { right, pa.y }; db = 0.0f; sb = sa; rb = ra; }
            Cable cab { pa, pb, da, db, (sa + sb) * 0.5f, line, false };
            const int ca = colOf (c.a, -1), cbc = colOf (c.b, cols);
            cab.under = va && vb && ra != rb && std::abs (cbc - ca) > 1;
            cab.laneY = (float) cellGrid.getY() + (float) cellGrid.getHeight() * 0.5f;
            if (cab.under) underCables.push_back (cab);
            over.push_back (cab);        // le spine si disegnano sempre sopra
        }
        overlay.setCables (std::move (over));
    }

    //==============================================================================
    void Pedalboard::paint (juce::Graphics& g)
    {
        const auto& t = themes->current();
        // pedana del tema (in cache: si ridisegna solo se cambiano tema o dimensioni)
        const auto key = t.id + juce::String (boardRect.getWidth()) + "x" + juce::String (boardRect.getHeight());
        if (key != boardKey && ! boardRect.isEmpty())
        {
            const float sc = (float) g.getInternalContext().getPhysicalPixelScaleFactor();
            boardCache = juce::Image (juce::Image::ARGB, juce::roundToInt ((float) boardRect.getWidth() * sc),
                                      juce::roundToInt ((float) boardRect.getHeight() * sc), true);
            juce::Graphics bg (boardCache);
            bg.addTransform (juce::AffineTransform::scale (sc));
            paintBoard (bg, t, boardRect.withZeroOrigin().toFloat(), juce::jlimit (0.7f, 2.2f, (float) getHeight() / 700.0f));
            boardKey = key;
        }
        if (boardCache.isValid())
        {
            g.setOpacity (1.0f);
            g.drawImage (boardCache, boardRect.toFloat());
        }

        // celle libere in coda: sagoma tratteggiata (il cavo ci passa sopra)
        const float dash[] = { 6.0f, 5.0f };
        for (int k = 0; k < (int) cells.size(); ++k)
        {
            const auto& c = cells[(size_t) k];
            if (c.slot != -1) continue;
            auto r = c.bounds.toFloat().reduced (6.0f);
            g.setColour (t.ink.withAlpha (0.06f));
            g.fillRoundedRectangle (r, 8.0f);
            juce::Path p, d;
            p.addRoundedRectangle (r, 8.0f);
            juce::PathStrokeType (1.4f).createDashedStroke (d, p, dash, 2);
            g.setColour (k == dropCell ? t.accent : t.ink.withAlpha (0.55f));
            g.fillPath (d);
            if (k == dropCell)
            {
                g.setColour (t.accent.withAlpha (0.14f));
                g.fillRoundedRectangle (r, 8.0f);
            }
        }
        if (dual)
        {
            // etichette delle corsie su una pastiglia scura: leggibili su qualsiasi pedana
            g.setFont (juce::Font (12.0f, juce::Font::bold));
            for (int row = 0; row < 2; ++row)
            {
                const int y = cellGrid.getY() + row * (cellGrid.getHeight() / 2) + 4;
                const auto pill = juce::Rectangle<float> ((float) cellGrid.getX() - (float) juce::jmax (22, getWidth() / 60) + 2.0f, (float) y, 18.0f, 18.0f);
                g.setColour (t.panelBottom.withAlpha (0.9f));
                g.fillRoundedRectangle (pill, 4.0f);
                g.setColour (row == 0 ? t.accent : juce::Colour (0xffe0554b));
                g.drawText (row == 0 ? "A" : "B", pill, juce::Justification::centred);
            }
        }
        // cavi che tornano a capo passando sotto la pedaliera (coperti dalle intestazioni)
        if (themes->showCables())
            for (const auto& c : underCables)
                cables::drawCable (g, c, themes->cable().body, themes->cable().sheen);
    }

    void Pedalboard::timerCallback()
    {
        inPanel.tick();
        outPanel.tick();
    }

    //==============================================================================
    int Pedalboard::cellIndexAt (juce::Point<int> p) const
    {
        for (int k = 0; k < (int) cells.size(); ++k)
            if (cells[(size_t) k].bounds.contains (p)) return k;
        return -1;
    }

    bool Pedalboard::isInterestedInDragSource (const SourceDetails& d)
    {
        return d.description.toString().startsWith ("slot:");
    }

    void Pedalboard::itemDragMove (const SourceDetails& d)
    {
        const int k = cellIndexAt (d.localPosition);
        const int target = k >= 0 && cells[(size_t) k].slot == -1 ? k : -1;
        if (target != dropCell) { dropCell = target; repaint(); }
    }

    void Pedalboard::itemDragExit (const SourceDetails&)
    {
        dropCell = -1;
        repaint();
    }

    void Pedalboard::itemDropped (const SourceDetails& d)
    {
        const int k = cellIndexAt (d.localPosition);
        dropCell = -1;
        repaint();
        if (k < 0 || cells[(size_t) k].slot != -1) return;
        // rilascio su una cella libera: lo slot va in fondo alla catena (o alla corsia della fila)
        const int from = d.description.toString().fromFirstOccurrenceOf ("slot:", false, false).getIntValue();
        const int lane = dual ? cells[(size_t) k].row : chain.lane (from);
        auto& ch = chain;
        juce::MessageManager::callAsync ([&ch, from, lane] { ch.moveTo (from, ch.size() - 1, lane); });
    }
}
