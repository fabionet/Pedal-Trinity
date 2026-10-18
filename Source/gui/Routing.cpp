/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)
*/

#include "Routing.h"

namespace pt::ui::routing
{
    using namespace pt::engine;

    bool isDual (const Chain& chain)
    {
        return chain.splitterIndex() >= 0 && chain.splitMode() == SplitMode::Dual;
    }

    std::vector<Conn> connections (const Chain& chain)
    {
        std::vector<Conn> conns;
        const int n = chain.size();
        const auto mode = chain.splitMode();
        End srcA { End::Input }, srcB { End::Input }, laneSrc[2] { { End::Input }, { End::Input } };
        bool hasB = false, splitSeen = false;
        for (int i = 0; i < n; ++i)
        {
            auto* s = chain.slot (i);
            if (s == nullptr || s->fx == nullptr) continue;          // slot vuoto: il cavo lo attraversa
            if (! s->patched.load() && ! s->isSplitter()) continue;  // pedale staccato: il cavo lo scavalca
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
        return conns;
    }

    juce::String describe (const std::vector<Conn>& conns)
    {
        auto end = [] (const End& e) { return e.kind == End::Input ? juce::String ("I") : e.kind == End::Output ? juce::String ("O") : juce::String (e.slot); };
        juce::StringArray out;
        for (const auto& c : conns) out.add (end (c.a) + ">" + end (c.b));
        return out.joinIntoString (" ");
    }

    int segmentOfSlot (const Chain& chain, int i)
    {
        const int sp = chain.splitterIndex();
        return isDual (chain) && sp >= 0 && i > sp ? 1 + chain.lane (i) : 0;
    }

    int segmentOf (const Chain& chain, const End& e)
    {
        const bool dual = isDual (chain);
        switch (e.kind)
        {
            case End::Input:   return 0;
            case End::Output:  return dual ? 1 + e.line : 0;
            case End::SlotIn:  return segmentOfSlot (chain, e.slot);
            case End::SlotOut: return dual && e.slot == chain.splitterIndex() ? 1 + e.line : segmentOfSlot (chain, e.slot);
        }
        return 0;
    }

    Replug resolve (const Chain& chain, const Conn& c, bool destEnd, const Target& t, const juce::String& outsideHint)
    {
        Replug r;
        const int n = chain.size();
        auto pos = [n] (const End& e) { return e.kind == End::Input ? -1 : e.kind == End::Output ? n : e.slot; };
        const End& moving = destEnd ? c.b : c.a;
        const int seg = segmentOf (chain, moving);
        const int lo0 = pos (c.a), hi0 = pos (c.b);

        // pedali collegati che il cavo smetterebbe di attraversare tra lo e hi (esclusi), nello stesso tratto
        auto between = [&] (int lo, int hi, std::vector<int>& out)
        {
            for (int k = lo + 1; k < hi; ++k)
            {
                auto* s = chain.slot (k);
                if (s == nullptr || s->fx == nullptr || ! s->patched.load() || segmentOfSlot (chain, k) != seg) continue;
                if (s->isSplitter()) return false;           // lo splitter non si scavalca
                out.push_back (k);
            }
            return true;
        };

        auto kind = t.kind;
        if (kind == Target::Slot)
        {
            const int y = t.slot;
            auto* s = chain.slot (y);
            if (s == nullptr || s->fx == nullptr) kind = Target::Empty;      // slot vuoto: come la pedana libera
            else
            {
                const End& fixed = destEnd ? c.a : c.b;
                if (y == moving.slot && moving.kind != End::Input && moving.kind != End::Output)
                    { r.hint = "Rilascia altrove per staccarlo"; return r; }
                if (fixed.kind != End::Input && fixed.kind != End::Output && y == fixed.slot)
                    { r.hint = "Un pedale non si collega a se stesso"; return r; }
                if (segmentOfSlot (chain, y) != seg && ! (s->isSplitter() && seg == 0))
                    { r.hint = "Corsia diversa: sposta il pedale nella corsia del cavo"; return r; }
                r.kind = Replug::Plug;
                if (destEnd)
                {
                    if (y <= lo0) { r.kind = Replug::Cancel; r.hint = "Il segnale va da sinistra a destra: il pedale e' prima del cavo"; return r; }
                    if (! between (lo0, y, r.unpatch)) { r.kind = Replug::Cancel; r.unpatch.clear(); r.hint = "Lo splitter non si puo' scavalcare"; return r; }
                }
                else
                {
                    if (y >= hi0) { r.kind = Replug::Cancel; r.hint = "Il segnale va da sinistra a destra: il pedale e' dopo il cavo"; return r; }
                    if (! between (y, hi0, r.unpatch)) { r.kind = Replug::Cancel; r.unpatch.clear(); r.hint = "Lo splitter non si puo' scavalcare"; return r; }
                }
                r.patch = s->patched.load() ? -1 : y;
                r.ok = true;
                const auto name = juce::String (s->def->code);
                r.hint = (destEnd ? "Ingresso di " : "Uscita di ") + name
                       + (r.unpatch.empty() ? juce::String() : "  (scavalca " + juce::String ((int) r.unpatch.size()) + (r.unpatch.size() == 1 ? " pedale)" : " pedali)"));
                return r;
            }
        }
        if (kind == Target::InputPanel || kind == Target::OutputPanel)
        {
            // la spina d'ingresso (destinazione) va nell'OUTPUT, quella d'uscita (sorgente) nell'INPUT
            if ((kind == Target::OutputPanel) != destEnd)
            {
                r.hint = destEnd ? "Questa spina entra in un pedale o nell'OUTPUT, non nell'INPUT"
                                 : "Questa spina esce da un pedale o dall'INPUT, non dall'OUTPUT";
                return r;
            }
            const bool right = destEnd ? (moving.kind == End::Output) : (moving.kind == End::Input);
            if (right) { r.hint = "Gia' collegato qui"; return r; }
            r.kind = Replug::Plug;
            if (! between (destEnd ? lo0 : -1, destEnd ? n : hi0, r.unpatch))
                { r.hint = "Lo splitter non si puo' scavalcare"; r.kind = Replug::Cancel; r.unpatch.clear(); return r; }
            r.ok = true;
            r.hint = juce::String (destEnd ? "OUTPUT" : "INPUT") + "  (scavalca " + juce::String ((int) r.unpatch.size())
                   + (r.unpatch.size() == 1 ? " pedale)" : " pedali)");
            return r;
        }
        if (kind == Target::Outside) { r.hint = outsideHint; return r; }

        // spazio libero: si stacca il pedale a quell'estremita' (la catena si richiude da sola)
        if (moving.kind == End::Input || moving.kind == End::Output)
            { r.hint = juce::String ("Il cavo dell'") + (moving.kind == End::Input ? "INPUT" : "OUTPUT") + " non si stacca"; return r; }
        auto* s = chain.slot (moving.slot);
        if (s == nullptr || s->isSplitter()) { r.hint = "Lo splitter non si stacca"; return r; }
        r.kind = Replug::Unplug;
        r.ok = true;
        r.unpatch.push_back (moving.slot);
        r.hint = "Rilascia per staccare " + juce::String (s->def != nullptr ? s->def->code : "") + " dalla catena";
        return r;
    }

    bool apply (Chain& chain, const Replug& r)
    {
        if (! r.ok || r.kind == Replug::Cancel) return false;
        for (int k : r.unpatch) chain.setPatched (k, false);
        if (r.patch >= 0) chain.setPatched (r.patch, true);
        return ! r.unpatch.empty() || r.patch >= 0;
    }
}

//==============================================================================
namespace pt::ui
{
    namespace
    {
        bool sameSlot (const std::weak_ptr<engine::Slot>& a, const std::shared_ptr<engine::Slot>& b)
        {
            return b != nullptr && ! a.owner_before (b) && ! b.owner_before (a);
        }
        bool plausible (float v) { return std::isfinite (v) && std::abs (v) < 1.0e6f; }
    }

    RouteLayout::RouteLayout (engine::Chain& c) : chain (c)
    {
        chain.addChangeListener (this);
        sync();
    }

    RouteLayout::~RouteLayout() { chain.removeChangeListener (this); }

    void RouteLayout::sync()
    {
        const juce::ScopedLock sl (lock);
        syncLocked();
    }

    void RouteLayout::syncLocked()
    {
        const int n = chain.size();
        std::vector<std::shared_ptr<engine::Slot>> cur ((size_t) n);
        for (int i = 0; i < n; ++i) cur[(size_t) i] = chain.slotRef (i);

        bool same = (int) items.size() == n;
        for (int i = 0; same && i < n; ++i) same = sameSlot (items[(size_t) i].id, cur[(size_t) i]);
        if (same) return;

        std::vector<Item> next ((size_t) n);
        std::vector<bool> used (items.size(), false);
        std::vector<bool> found ((size_t) n, false);
        int matched = 0;
        for (int i = 0; i < n; ++i)
            for (size_t j = 0; j < items.size(); ++j)
                if (! used[j] && sameSlot (items[j].id, cur[(size_t) i]))
                {
                    next[(size_t) i] = items[j];
                    used[j] = found[(size_t) i] = true;
                    ++matched;
                    break;
                }
        // slot sostituito sul posto (cambio di pedale): conserva la posizione di quell'indice;
        // catena interamente nuova (preset, progetto): disposizione automatica
        const bool wholeNew = matched == 0 && items.size() > 1;
        for (int i = 0; i < n; ++i)
        {
            if (! found[(size_t) i] && ! wholeNew && i < (int) items.size() && ! used[(size_t) i])
            {
                next[(size_t) i].set = items[(size_t) i].set;
                next[(size_t) i].p = items[(size_t) i].p;
                used[(size_t) i] = true;
            }
            next[(size_t) i].id = cur[(size_t) i];
        }
        items = std::move (next);
        ++rev;
    }

    bool RouteLayout::slotPos (int index, juce::Point<float>& out) const
    {
        const juce::ScopedLock sl (lock);
        const_cast<RouteLayout*> (this)->syncLocked();
        if (! juce::isPositiveAndBelow (index, (int) items.size()) || ! items[(size_t) index].set) return false;
        out = items[(size_t) index].p;
        return true;
    }

    void RouteLayout::setSlotPos (int index, juce::Point<float> p)
    {
        const juce::ScopedLock sl (lock);
        syncLocked();
        if (! juce::isPositiveAndBelow (index, (int) items.size()) || ! plausible (p.x) || ! plausible (p.y)) return;
        items[(size_t) index].set = true;
        items[(size_t) index].p = p;
        ++rev;
    }

    bool RouteLayout::ioPos (int which, juce::Point<float>& out) const
    {
        const juce::ScopedLock sl (lock);
        if (! juce::isPositiveAndBelow (which, 2) || ! ioSet[which]) return false;
        out = io[which];
        return true;
    }

    void RouteLayout::setIoPos (int which, juce::Point<float> p)
    {
        const juce::ScopedLock sl (lock);
        if (! juce::isPositiveAndBelow (which, 2) || ! plausible (p.x) || ! plausible (p.y)) return;
        ioSet[which] = true;
        io[which] = p;
        ++rev;
    }

    void RouteLayout::clearPositions()
    {
        const juce::ScopedLock sl (lock);
        syncLocked();
        for (auto& it : items) it.set = false;
        ioSet[0] = ioSet[1] = false;
        ++rev;
    }

    int RouteLayout::numPlaced() const
    {
        const juce::ScopedLock sl (lock);
        const_cast<RouteLayout*> (this)->syncLocked();
        int k = ioSet[0] + ioSet[1];
        for (const auto& it : items) k += it.set ? 1 : 0;
        return k;
    }

    float RouteLayout::zoom() const { const juce::ScopedLock sl (lock); return viewZoom; }
    juce::Point<float> RouteLayout::viewOrigin() const { const juce::ScopedLock sl (lock); return origin; }

    void RouteLayout::setView (float z, juce::Point<float> o)
    {
        const juce::ScopedLock sl (lock);
        if (std::isfinite (z)) viewZoom = z <= 0.0f ? 0.0f : juce::jlimit (0.1f, 4.0f, z);
        if (plausible (o.x) && plausible (o.y)) origin = o;
    }

    juce::ValueTree RouteLayout::toValueTree() const
    {
        const juce::ScopedLock sl (lock);
        const_cast<RouteLayout*> (this)->syncLocked();     // indici allineati alla catena che si salva
        juce::ValueTree t ("ROUTEMAP");
        t.setProperty ("zoom", viewZoom, nullptr);
        t.setProperty ("vx", origin.x, nullptr);
        t.setProperty ("vy", origin.y, nullptr);
        const char* names[2][2] = { { "ix", "iy" }, { "ox", "oy" } };
        for (int w = 0; w < 2; ++w)
            if (ioSet[w])
            {
                t.setProperty (names[w][0], io[w].x, nullptr);
                t.setProperty (names[w][1], io[w].y, nullptr);
            }
        for (int i = 0; i < (int) items.size(); ++i)
            if (items[(size_t) i].set)
            {
                juce::ValueTree n ("NODE");
                n.setProperty ("i", i, nullptr);
                n.setProperty ("x", items[(size_t) i].p.x, nullptr);
                n.setProperty ("y", items[(size_t) i].p.y, nullptr);
                t.appendChild (n, nullptr);
            }
        return t;
    }

    void RouteLayout::fromValueTree (const juce::ValueTree& t)
    {
        const juce::ScopedLock sl (lock);
        items.clear();
        syncLocked();                                       // identita' degli slot della catena appena caricata
        for (auto& it : items) it.set = false;
        ioSet[0] = ioSet[1] = false;
        viewZoom = 0.0f;
        origin = {};
        ++rev;
        if (! t.isValid() || ! t.hasType ("ROUTEMAP")) return;

        auto num = [] (const juce::var& v, float& out)
        {
            if (! (v.isDouble() || v.isInt() || v.isInt64() || v.isString())) return false;
            out = (float) (double) v;
            return plausible (out);
        };
        float z = 0, x = 0, y = 0;
        if (num (t.getProperty ("zoom"), z) && z > 0.0f) viewZoom = juce::jlimit (0.1f, 4.0f, z);
        if (num (t.getProperty ("vx"), x) && num (t.getProperty ("vy"), y)) origin = { x, y };
        const char* names[2][2] = { { "ix", "iy" }, { "ox", "oy" } };
        for (int w = 0; w < 2; ++w)
            if (num (t.getProperty (names[w][0]), x) && num (t.getProperty (names[w][1]), y))
            {
                ioSet[w] = true;
                io[w] = { x, y };
            }
        for (int k = 0; k < t.getNumChildren() && k < 4 * engine::maxSlots; ++k)
        {
            const auto n = t.getChild (k);
            if (! n.hasType ("NODE")) continue;
            float fi = -1;
            if (! num (n.getProperty ("i"), fi) || ! num (n.getProperty ("x"), x) || ! num (n.getProperty ("y"), y)) continue;
            const int i = (int) fi;
            if ((float) i != fi || ! juce::isPositiveAndBelow (i, (int) items.size())) continue;
            items[(size_t) i].set = true;
            items[(size_t) i].p = { x, y };
        }
    }
}
