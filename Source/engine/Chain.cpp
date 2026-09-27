/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)
*/

#include "Chain.h"

namespace pt::engine
{
    namespace
    {
        float peakOf (const float* x, int n) noexcept
        {
            float p = 0.0f;
            for (int i = 0; i < n; ++i) p = std::max (p, std::abs (x[i]));
            return p;
        }

        /** Guadagno che scorre linearmente da 'from' a 'to' lungo il blocco. */
        void ramp (float* x, int n, float from, float to) noexcept
        {
            if (from == to) { juce::FloatVectorOperations::multiply (x, to, n); return; }
            const float step = (to - from) / (float) std::max (1, n);
            for (int i = 0; i < n; ++i) x[i] *= from + step * (float) (i + 1);
        }

        float dbKnob (float v, float lo, float hi) noexcept
        {
            return juce::Decibels::decibelsToGain (lo + (hi - lo) * v);
        }
    }

    Chain::Chain()
    {
        current = std::make_unique<Snapshot>();
        active.store (current.get());
        startTimerHz (10);
    }

    Chain::~Chain()
    {
        stopTimer();
        active.store (nullptr);
    }

    //==============================================================================
    std::shared_ptr<Slot> Chain::makeSlot (const juce::String& modelId) const
    {
        auto s = std::make_shared<Slot>();
        if (modelId.isNotEmpty())
            if (const auto* d = findModel (modelId.toRawUTF8()))
            {
                s->def = d;
                s->fx = createEffect (*d);
                if (sampleRate > 0)
                    s->fx->prepare (sampleRate, blockSize);
            }
        return s;
    }

    void Chain::prepare (double sr, int maxBlock)
    {
        const juce::ScopedLock sl (modelLock);
        sampleRate = sr;
        blockSize = maxBlock;
        lines.setSize (2, maxBlock, false, false, true);
        dry.setSize (2, maxBlock, false, false, true);
        for (auto& s : model)
        {
            if (s->fx) s->fx->prepare (sr, maxBlock);
            s->fade = s->enabled.load() ? 1.0f : 0.0f;
            s->wasOff = ! s->enabled.load();
        }
    }

    void Chain::runSlot (Slot& s, float* const* ch, int nch, int n)
    {
        const bool on = s.enabled.load (std::memory_order_relaxed);
        if (! on && s.fade <= 0.0f) { s.wasOff = true; return; }
        if (on && s.wasOff) { s.fx->reset(); s.wasOff = false; }

        if (on && s.fade >= 1.0f)
        {
            s.fx->process (ch, nch, n);
            return;
        }
        // dissolvenza on/off senza click
        for (int c = 0; c < nch; ++c) dry.copyFrom (c, 0, ch[c], n);
        s.fx->process (ch, nch, n);
        const float fadeStep = (float) (1.0 / (0.012 * sampleRate));
        float f = s.fade;
        for (int i = 0; i < n; ++i)
        {
            f = on ? juce::jmin (1.0f, f + fadeStep) : juce::jmax (0.0f, f - fadeStep);
            for (int c = 0; c < nch; ++c)
            {
                const float d = dry.getSample (c, i);
                ch[c][i] = d + (ch[c][i] - d) * f;
            }
        }
        s.fade = f;
    }

    void Chain::process (juce::AudioBuffer<float>& buffer, int numChannels)
    {
        Snapshot* snap;
        do
        {
            snap = active.load (std::memory_order_acquire);
            inUse.store (snap, std::memory_order_release);
        }
        while (snap != active.load (std::memory_order_acquire));

        const int n = juce::jmin (buffer.getNumSamples(), lines.getNumSamples());
        if (snap != nullptr && n > 0)
        {
            float* a = lines.getWritePointer (0);
            float* b = lines.getWritePointer (1);
            float* lr[2] = { a, b };
            juce::FloatVectorOperations::copy (a, buffer.getReadPointer (0), n);   // sorgente mono: ingresso 1

            const int split = snap->split;
            auto mode = SplitMode::Mono;
            bool twoLines = false;        // B contiene un segnale proprio (dual o tratto stereo)
            bool inMeasured = false;

            for (int i = 0; i < (int) snap->slots.size(); ++i)
            {
                Slot& s = *snap->slots[(size_t) i];
                if (i == split)
                {
                    mode = s.fx != nullptr ? (SplitMode) s.fx->step (0) : SplitMode::Mono;
                    if (mode != SplitMode::Mono)
                    {
                        const float bal = s.fx->p (1) * 2.0f - 1.0f;
                        const float ga = dbKnob (s.fx->p (2), -24.0f, 12.0f) * juce::jmin (1.0f, 1.0f - bal);
                        const float gb = dbKnob (s.fx->p (3), -24.0f, 12.0f) * juce::jmin (1.0f, 1.0f + bal);
                        juce::FloatVectorOperations::copy (b, a, n);
                        ramp (a, n, lastGain[0], ga);
                        ramp (b, n, lastGain[1], gb);
                        lastGain[0] = ga; lastGain[1] = gb;
                        twoLines = true;
                    }
                    if (i == 0)
                    {
                        // splitter nel primo slot: il meter d'ingresso mostra le due mandate
                        inputMeter.channels.store (twoLines ? 2 : 1, std::memory_order_relaxed);
                        inputMeter.push (0, peakOf (a, n));
                        if (twoLines) inputMeter.push (1, peakOf (b, n));
                        inMeasured = true;
                    }
                    continue;
                }
                if (! inMeasured)
                {
                    inputMeter.channels.store (1, std::memory_order_relaxed);
                    inputMeter.push (0, peakOf (a, n));
                    inMeasured = true;
                }
                if (s.fx == nullptr) continue;

                if (split >= 0 && i > split && mode == SplitMode::Dual)
                {
                    float* line[1] = { s.lane.load (std::memory_order_relaxed) == 1 ? b : a };
                    runSlot (s, line, 1, n);
                }
                else if (split >= 0 && i > split && mode == SplitMode::Stereo && s.def->stereo)
                {
                    if (! twoLines) { juce::FloatVectorOperations::copy (b, a, n); twoLines = true; }
                    runSlot (s, lr, 2, n);
                }
                else
                {
                    // tratto mono, oppure pedale mono nella catena stereo: solo la presa A
                    runSlot (s, lr, 1, n);
                    if (mode == SplitMode::Stereo && i > split) twoLines = false;
                }
            }
            if (! inMeasured)
            {
                inputMeter.channels.store (1, std::memory_order_relaxed);
                inputMeter.push (0, peakOf (a, n));
            }

            const bool splitOn = split >= 0 && mode != SplitMode::Mono;
            if (! twoLines) juce::FloatVectorOperations::copy (b, a, n);
            if (splitOn)
            {
                const float bal = outBalance.load (std::memory_order_relaxed);
                const float gl = outGainL.load (std::memory_order_relaxed) * juce::jmin (1.0f, 1.0f - bal);
                const float gr = outGainR.load (std::memory_order_relaxed) * juce::jmin (1.0f, 1.0f + bal);
                ramp (a, n, lastGain[2], gl);
                ramp (b, n, lastGain[3], gr);
                lastGain[2] = gl; lastGain[3] = gr;
            }
            else
            {
                lastGain[0] = lastGain[1] = lastGain[2] = lastGain[3] = 1.0f;
            }
            outputChannels.store (splitOn ? 2 : 1, std::memory_order_relaxed);

            if (numChannels > 1 && buffer.getNumChannels() > 1)
            {
                buffer.copyFrom (0, 0, a, n);
                buffer.copyFrom (1, 0, b, n);
            }
            else
            {
                buffer.copyFrom (0, 0, a, n);
                if (splitOn || twoLines)
                {
                    buffer.addFrom (0, 0, b, n);
                    buffer.applyGain (0, 0, n, 0.5f);
                }
            }
        }
        inUse.store (nullptr, std::memory_order_release);
    }

    //==============================================================================
    int Chain::splitterIndexLocked() const
    {
        for (int i = 0; i < (int) model.size(); ++i)
            if (model[(size_t) i]->isSplitter()) return i;
        return -1;
    }

    void Chain::publish()
    {
        auto snap = std::make_unique<Snapshot>();
        {
            const juce::ScopedLock sl (modelLock);
            snap->slots = model;
            snap->split = splitterIndexLocked();
        }
        {
            const juce::ScopedLock pl (publishLock);
            Snapshot* raw = snap.get();
            retired.push_back (std::move (current));
            current = std::move (snap);
            active.store (raw, std::memory_order_release);
        }
        sendChangeMessage();
    }

    void Chain::timerCallback()
    {
        // libera le istantanee non piu' usate dal thread audio
        const juce::ScopedLock pl (publishLock);
        Snapshot* busy = inUse.load (std::memory_order_acquire);
        retired.erase (std::remove_if (retired.begin(), retired.end(),
                                       [busy] (const std::unique_ptr<Snapshot>& s) { return s.get() != busy; }),
                       retired.end());
        const juce::ScopedLock sl (modelLock);
        for (auto& s : model)
            if (s->fx) s->fx->messageThreadUpdate();
    }

    int Chain::size() const
    {
        const juce::ScopedLock sl (modelLock);
        return (int) model.size();
    }

    Slot* Chain::slot (int index) const
    {
        const juce::ScopedLock sl (modelLock);
        return juce::isPositiveAndBelow (index, (int) model.size()) ? model[(size_t) index].get() : nullptr;
    }

    std::shared_ptr<Slot> Chain::slotRef (int index) const
    {
        const juce::ScopedLock sl (modelLock);
        return juce::isPositiveAndBelow (index, (int) model.size()) ? model[(size_t) index] : nullptr;
    }

    int Chain::splitterIndex() const
    {
        const juce::ScopedLock sl (modelLock);
        return splitterIndexLocked();
    }

    SplitMode Chain::splitMode() const
    {
        const juce::ScopedLock sl (modelLock);
        const int i = splitterIndexLocked();
        if (i < 0 || model[(size_t) i]->fx == nullptr) return SplitMode::Mono;
        return (SplitMode) model[(size_t) i]->fx->step (0);
    }

    bool Chain::canPlaceSplitter (int index) const
    {
        const int i = splitterIndex();
        return i < 0 || i == index;
    }

    int Chain::lane (int index) const
    {
        if (auto* s = slot (index)) return s->lane.load();
        return 0;
    }

    int Chain::insert (int index, const juce::String& modelId, int laneIndex)
    {
        auto s = makeSlot (modelId);
        s->lane.store (juce::jlimit (0, 1, laneIndex));
        {
            const juce::ScopedLock sl (modelLock);
            if ((int) model.size() >= maxSlots) return -1;
            if (s->isSplitter() && splitterIndexLocked() >= 0) return -1;
            if (index < 0 || index > (int) model.size()) index = (int) model.size();
            model.insert (model.begin() + index, s);
        }
        publish();
        return index;
    }

    void Chain::remove (int index)
    {
        {
            const juce::ScopedLock sl (modelLock);
            if (! juce::isPositiveAndBelow (index, (int) model.size())) return;
            model.erase (model.begin() + index);
        }
        publish();
    }

    void Chain::move (int from, int to)
    {
        {
            const juce::ScopedLock sl (modelLock);
            const int n = (int) model.size();
            if (! juce::isPositiveAndBelow (from, n)) return;
            to = juce::jlimit (0, n - 1, to);
            if (from == to) return;
            auto s = model[(size_t) from];
            model.erase (model.begin() + from);
            model.insert (model.begin() + to, s);
        }
        publish();
    }

    void Chain::moveTo (int from, int to, int laneIndex)
    {
        {
            const juce::ScopedLock sl (modelLock);
            const int n = (int) model.size();
            if (! juce::isPositiveAndBelow (from, n)) return;
            to = juce::jlimit (0, n - 1, to);
            auto s = model[(size_t) from];
            s->lane.store (juce::jlimit (0, 1, laneIndex));
            if (from != to)
            {
                model.erase (model.begin() + from);
                model.insert (model.begin() + to, s);
            }
        }
        publish();
    }

    bool Chain::setModel (int index, const juce::String& modelId)
    {
        auto s = makeSlot (modelId);
        {
            const juce::ScopedLock sl (modelLock);
            if (! juce::isPositiveAndBelow (index, (int) model.size())) return false;
            const int sp = splitterIndexLocked();
            if (s->isSplitter() && sp >= 0 && sp != index) return false;
            s->enabled.store (true);
            s->lane.store (model[(size_t) index]->lane.load());
            model[(size_t) index] = s;
        }
        publish();
        return true;
    }

    void Chain::setEnabled (int index, bool on)
    {
        if (auto* s = slot (index)) s->enabled.store (on);
        sendChangeMessage();
    }

    void Chain::setParam (int index, int control, float value)
    {
        if (auto* s = slot (index))
            if (s->fx && juce::isPositiveAndBelow (control, maxControls))
            {
                const bool modeChange = s->isSplitter() && control == 0 && s->fx->p (0) != value;
                s->fx->params[control].store (juce::jlimit (0.0f, 1.0f, value));
                if (modeChange) sendChangeMessage();     // cambia la disposizione della pedaliera
            }
    }

    void Chain::setLane (int index, int laneIndex)
    {
        if (auto* s = slot (index))
        {
            s->lane.store (juce::jlimit (0, 1, laneIndex));
            sendChangeMessage();
        }
    }

    void Chain::clear()
    {
        {
            const juce::ScopedLock sl (modelLock);
            model.clear();
        }
        publish();
    }

    //==============================================================================
    juce::ValueTree Chain::toValueTree() const
    {
        juce::ValueTree t ("CHAIN");
        const juce::ScopedLock sl (modelLock);
        for (auto& s : model)
        {
            juce::ValueTree st ("SLOT");
            st.setProperty ("model", s->def != nullptr ? juce::String (s->def->id) : juce::String(), nullptr);
            st.setProperty ("on", s->enabled.load(), nullptr);
            if (s->lane.load() != 0) st.setProperty ("lane", s->lane.load(), nullptr);
            if (s->fx)
            {
                for (int i = 0; i < s->def->numControls; ++i)
                    st.setProperty ("p" + juce::String (i), s->fx->p (i), nullptr);
                if (! s->fx->loadedFile.empty())
                    st.setProperty ("file", juce::String (s->fx->loadedFile), nullptr);
                const auto extra = s->fx->saveState();
                if (! extra.empty())
                    st.setProperty ("state", juce::String::fromUTF8 (extra.c_str()), nullptr);
            }
            t.appendChild (st, nullptr);
        }
        return t;
    }

    void Chain::fromValueTree (const juce::ValueTree& t)
    {
        std::vector<std::shared_ptr<Slot>> fresh;
        bool haveSplitter = false;
        for (int i = 0; i < t.getNumChildren() && (int) fresh.size() < maxSlots; ++i)
        {
            const auto st = t.getChild (i);
            if (! st.hasType ("SLOT")) continue;
            auto s = makeSlot (st.getProperty ("model").toString());
            if (s->isSplitter())
            {
                if (haveSplitter) s = makeSlot ({});      // al massimo uno splitter
                haveSplitter = true;
            }
            s->enabled.store ((bool) st.getProperty ("on", true));
            s->lane.store (juce::jlimit (0, 1, (int) st.getProperty ("lane", 0)));
            if (s->fx)
            {
                for (int k = 0; k < s->def->numControls; ++k)
                    if (st.hasProperty ("p" + juce::String (k)))
                        s->fx->params[k].store ((float) st.getProperty ("p" + juce::String (k)));
                const auto file = st.getProperty ("file").toString();
                if (file.isNotEmpty()) s->fx->loadFile (file.toStdString());
                const auto extra = st.getProperty ("state").toString();
                if (extra.isNotEmpty()) s->fx->restoreState (extra.toStdString());
                s->fx->reset();
            }
            s->fade = s->enabled.load() ? 1.0f : 0.0f;
            s->wasOff = ! s->enabled.load();
            fresh.push_back (s);
        }
        {
            const juce::ScopedLock sl (modelLock);
            model = std::move (fresh);
        }
        publish();
    }
}
