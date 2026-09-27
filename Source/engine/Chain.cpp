/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)
*/

#include "Chain.h"

namespace pt::engine
{
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
        dry.setSize (2, maxBlock, false, false, true);
        for (auto& s : model)
        {
            if (s->fx) s->fx->prepare (sr, maxBlock);
            s->fade = s->enabled.load() ? 1.0f : 0.0f;
            s->wasOff = ! s->enabled.load();
        }
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

        if (snap != nullptr)
        {
            const int n = buffer.getNumSamples();
            const int nch = juce::jmin (numChannels, 2, buffer.getNumChannels());
            float* chans[2] = { buffer.getWritePointer (0), nch > 1 ? buffer.getWritePointer (1) : buffer.getWritePointer (0) };
            const float fadeStep = (float) (1.0 / (0.012 * sampleRate));

            for (auto& sp : snap->slots)
            {
                Slot& s = *sp;
                if (s.fx == nullptr) continue;
                const bool on = s.enabled.load (std::memory_order_relaxed);
                if (! on && s.fade <= 0.0f) { s.wasOff = true; continue; }
                if (on && s.wasOff) { s.fx->reset(); s.wasOff = false; }

                if (on && s.fade >= 1.0f)
                {
                    s.fx->process (chans, nch, n);
                    continue;
                }
                // dissolvenza on/off senza click
                for (int c = 0; c < nch; ++c) dry.copyFrom (c, 0, chans[c], n);
                s.fx->process (chans, nch, n);
                float f = s.fade;
                for (int i = 0; i < n; ++i)
                {
                    f = on ? juce::jmin (1.0f, f + fadeStep) : juce::jmax (0.0f, f - fadeStep);
                    for (int c = 0; c < nch; ++c)
                    {
                        const float d = dry.getSample (c, i);
                        chans[c][i] = d + (chans[c][i] - d) * f;
                    }
                }
                s.fade = f;
            }
        }
        inUse.store (nullptr, std::memory_order_release);
    }

    //==============================================================================
    void Chain::publish()
    {
        auto snap = std::make_unique<Snapshot>();
        {
            const juce::ScopedLock sl (modelLock);
            snap->slots = model;
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

    int Chain::insert (int index, const juce::String& modelId)
    {
        auto s = makeSlot (modelId);
        {
            const juce::ScopedLock sl (modelLock);
            if ((int) model.size() >= maxSlots) return -1;
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

    void Chain::setModel (int index, const juce::String& modelId)
    {
        auto s = makeSlot (modelId);
        {
            const juce::ScopedLock sl (modelLock);
            if (! juce::isPositiveAndBelow (index, (int) model.size())) return;
            s->enabled.store (true);
            model[(size_t) index] = s;
        }
        publish();
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
                s->fx->params[control].store (juce::jlimit (0.0f, 1.0f, value));
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
            if (s->fx)
            {
                for (int i = 0; i < s->def->numControls; ++i)
                    st.setProperty ("p" + juce::String (i), s->fx->p (i), nullptr);
                if (! s->fx->loadedFile.empty())
                    st.setProperty ("file", juce::String (s->fx->loadedFile), nullptr);
            }
            t.appendChild (st, nullptr);
        }
        return t;
    }

    void Chain::fromValueTree (const juce::ValueTree& t)
    {
        std::vector<std::shared_ptr<Slot>> fresh;
        for (int i = 0; i < t.getNumChildren() && (int) fresh.size() < maxSlots; ++i)
        {
            const auto st = t.getChild (i);
            if (! st.hasType ("SLOT")) continue;
            auto s = makeSlot (st.getProperty ("model").toString());
            s->enabled.store ((bool) st.getProperty ("on", true));
            if (s->fx)
            {
                for (int k = 0; k < s->def->numControls; ++k)
                    if (st.hasProperty ("p" + juce::String (k)))
                        s->fx->params[k].store ((float) st.getProperty ("p" + juce::String (k)));
                const auto file = st.getProperty ("file").toString();
                if (file.isNotEmpty()) s->fx->loadFile (file.toStdString());
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
