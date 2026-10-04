/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)
*/

#include "MidiMap.h"
#include "PluginProcessor.h"

namespace pt::midi
{
    namespace
    {
        constexpr const char* inId = "in_gain";
        constexpr const char* outId = "out_gain";
        constexpr const char* bypassId = "bypass_all";

        int roundValue (float v01) { return juce::jlimit (0, 127, juce::roundToInt (v01 * 127.0f)); }
    }

    MidiManager::MidiManager (PedalTrinityProcessor& p) : processor (p)
    {
        current = std::make_unique<Table>();
        active.store (current.get());
        for (auto* id : { inId, outId, bypassId })
            processor.apvts.addParameterListener (id, this);
        processor.chain.onUserTouch = [this] (int slot, int control)
        {
            if (! learning) return;
            Mapping t;
            t.target = control < 0 ? Target::SlotSwitch : Target::SlotControl;
            t.slot = slot;
            t.control = control;
            arm (t);
        };
        startTimerHz (30);
    }

    MidiManager::~MidiManager()
    {
        stopTimer();
        processor.chain.onUserTouch = nullptr;
        for (auto* id : { inId, outId, bypassId })
            processor.apvts.removeParameterListener (id, this);
        active.store (nullptr);
    }

    //==============================================================================
    bool MidiManager::matches (const Mapping& m, const Event& e) noexcept
    {
        if (m.channel != 0 && m.channel != e.channel) return false;
        if ((int) m.source != (int) e.kind) return false;
        return m.source == Source::Program ? m.number == e.number || m.number < 0 : m.number == e.number;
    }

    void MidiManager::processBlock (juce::MidiBuffer& midi) noexcept
    {
        Table* t;
        do
        {
            t = active.load (std::memory_order_acquire);
            inUse.store (t, std::memory_order_release);
        }
        while (t != active.load (std::memory_order_acquire));

        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            Event e;
            if (msg.isController()) { e.kind = 0; e.number = (uint8_t) msg.getControllerNumber(); e.value = (uint8_t) msg.getControllerValue(); }
            else if (msg.isNoteOn()) { e.kind = 1; e.number = (uint8_t) msg.getNoteNumber(); e.value = 127; }
            else if (msg.isNoteOff()) { e.kind = 1; e.number = (uint8_t) msg.getNoteNumber(); e.value = 0; }
            else if (msg.isProgramChange()) { e.kind = 2; e.number = (uint8_t) msg.getProgramChangeNumber(); e.value = 127; }
            else continue;
            e.channel = (uint8_t) juce::jlimit (1, 16, msg.getChannel());

            // al thread dei messaggi: monitor, mappatura, funzioni globali
            if (inFifo.getFreeSpace() > 0)
            {
                const auto w = inFifo.write (1);
                if (w.blockSize1 > 0) inEvents[w.startIndex1] = e;
            }

            if (t == nullptr) continue;
            for (int i = 0; i < t->count; ++i)
            {
                const auto& m = t->m[i];
                if (! matches (m, e)) continue;
                const bool press = e.value >= 64;
                if (m.target == Target::SlotSwitch)
                {
                    if (m.mode == Mode::Follow && e.kind != 2) processor.chain.applyFromAudio (m.slot, 0, -1, press ? 1.0f : 0.0f);
                    else if (m.mode == Mode::Momentary) processor.chain.applyFromAudio (m.slot, 1, -1, 0.0f);
                    else if (press) processor.chain.applyFromAudio (m.slot, 1, -1, 0.0f);
                    slotsChanged.store (true, std::memory_order_relaxed);
                }
                else if (m.target == Target::SlotControl)
                {
                    processor.chain.applyFromAudio (m.slot, 2, m.control, m.lo + (m.hi - m.lo) * (float) e.value / 127.0f);
                }
            }
        }
        inUse.store (nullptr, std::memory_order_release);

        // il MIDI in ingresso non prosegue (niente anelli con la pedaliera esterna): esce solo il ritorno dello stato
        midi.clear();
        const int ready = outFifo.getNumReady();
        if (ready > 0)
        {
            const auto r = outFifo.read (ready);
            for (int k = 0; k < r.blockSize1; ++k) midi.addEvent (outEvents[r.startIndex1 + k], 0);
            for (int k = 0; k < r.blockSize2; ++k) midi.addEvent (outEvents[r.startIndex2 + k], 0);
        }
    }

    //==============================================================================
    void MidiManager::publish()
    {
        auto t = std::make_unique<Table>();
        t->count = juce::jmin ((int) list.size(), maxMappings);
        for (int i = 0; i < t->count; ++i) t->m[i] = list[(size_t) i];
        t->pcPresets = pcPresets;
        retired.push_back (std::move (current));
        current = std::move (t);
        active.store (current.get(), std::memory_order_release);
        lastSent.assign (list.size(), -1);
        forceFeedback = true;
        ++listVersion;
        sendChangeMessage();
    }

    void MidiManager::setMappings (std::vector<Mapping> m)
    {
        if ((int) m.size() > maxMappings) m.resize ((size_t) maxMappings);
        list = std::move (m);
        publish();
    }

    void MidiManager::addMapping (const Mapping& m)
    {
        list.erase (std::remove_if (list.begin(), list.end(), [&m] (const Mapping& o) { return o.sameSource (m) || o.sameTarget (m); }), list.end());
        if ((int) list.size() >= maxMappings) return;
        list.push_back (m);
        publish();
    }

    void MidiManager::removeMapping (int index)
    {
        if (! juce::isPositiveAndBelow (index, (int) list.size())) return;
        list.erase (list.begin() + index);
        publish();
    }

    void MidiManager::updateMapping (int index, const Mapping& m)
    {
        if (! juce::isPositiveAndBelow (index, (int) list.size())) return;
        list[(size_t) index] = m;
        publish();
    }

    void MidiManager::setFeedbackEnabled (bool b)
    {
        feedback = b;
        forceFeedback = true;
        sendChangeMessage();
    }

    //==============================================================================
    void MidiManager::setLearning (bool b)
    {
        learning = b;
        armedValid = false;
        status = b ? "Tocca un pomello, un footswitch o il fader INPUT/OUTPUT, poi muovi il comando della pedaliera MIDI"
                   : juce::String();
        sendChangeMessage();
    }

    void MidiManager::arm (const Mapping& target)
    {
        armedTarget = target;
        armedValid = true;
        learning = true;
        status = "In attesa del comando MIDI per: " + describeTarget (target) + "  (premi un footswitch o muovi un pedale/pomello MIDI)";
        sendChangeMessage();
    }

    void MidiManager::parameterChanged (const juce::String& id, float)
    {
        // chiamata anche dal thread audio (automazione): la mappatura la riprende il timer del thread dei messaggi
        if (learning) pendingParam.store (id == inId ? (int) Target::InputGain : id == outId ? (int) Target::OutputGain : (int) Target::Bypass);
    }

    void MidiManager::handleLearn (const Event& e)
    {
        // interruttore appena imparato che manda anche il rilascio: e' momentaneo -> ogni pressione inverte
        if (learnedIndex >= 0 && juce::isPositiveAndBelow (learnedIndex, (int) list.size())
            && juce::Time::getMillisecondCounter() - learnedAt < 1500)
        {
            auto& m = list[(size_t) learnedIndex];
            if ((int) m.source == e.kind && m.number == e.number && m.channel == e.channel)
            {
                if (m.isSwitch() && m.mode == Mode::Follow && e.value < 64 && e.kind != 2)
                {
                    m.mode = Mode::Toggle;
                    publish();
                    status = "Assegnato: " + describeSource (m) + "  ->  " + describeTarget (m) + "  (interruttore momentaneo: ogni pressione accende/spegne)";
                }
                return;                                  // stesso comando appena imparato: non e' una nuova assegnazione
            }
        }
        if (! learning || ! armedValid) return;
        if (e.kind == 2 && armedTarget.target == Target::SlotControl) return;    // un Program Change non regola un pomello
        Mapping m = armedTarget;
        m.channel = e.channel;
        m.source = (Source) e.kind;
        m.number = e.number;
        m.mode = Mode::Follow;
        addMapping (m);
        learnedIndex = (int) list.size() - 1;
        learnedAt = juce::Time::getMillisecondCounter();
        armedValid = false;
        status = "Assegnato: " + describeSource (m) + "  ->  " + describeTarget (m) + ".  Tocca un altro comando per continuare.";
        sendChangeMessage();
    }

    void MidiManager::handleGlobal (const Mapping& m, const Event& e)
    {
        const bool press = e.value >= 64;
        const float v = m.lo + (m.hi - m.lo) * (float) e.value / 127.0f;
        auto setParam = [this] (const char* id, float norm)
        {
            if (auto* p = processor.apvts.getParameter (id)) p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, norm));
            pendingParam.store (-1);                     // cambiato dal MIDI, non toccato a mano
        };
        switch (m.target)
        {
            case Target::InputGain:  setParam (inId, v); break;
            case Target::OutputGain: setParam (outId, v); break;
            case Target::Bypass:
            {
                auto* p = processor.apvts.getParameter (bypassId);
                if (p == nullptr) break;
                const bool on = p->getValue() > 0.5f;
                if (m.mode == Mode::Follow && e.kind != 2) setParam (bypassId, press ? 1.0f : 0.0f);
                else if (m.mode == Mode::Momentary || press) setParam (bypassId, on ? 0.0f : 1.0f);
                break;
            }
            case Target::PresetNext: if (press) stepPreset (1); break;
            case Target::PresetPrev: if (press) stepPreset (-1); break;
            default: break;
        }
    }

    void MidiManager::stepPreset (int delta)
    {
        const int total = PresetManager::factoryNames().size() + processor.presets.userPresets().size();
        if (total <= 0) return;
        presetIndex = ((presetIndex < 0 ? (delta > 0 ? -1 : 0) : presetIndex) + delta + total) % total;
        loadPresetIndex (presetIndex);
    }

    void MidiManager::loadPresetIndex (int index)
    {
        const int nf = PresetManager::factoryNames().size();
        const auto user = processor.presets.userPresets();
        if (index < 0 || index >= nf + user.size()) return;
        presetIndex = index;
        if (index < nf) processor.presets.loadFactory (index);
        else processor.presets.load (user[index - nf]);
    }

    void MidiManager::drainEvents()
    {
        const int ready = inFifo.getNumReady();
        if (ready <= 0) return;
        const auto r = inFifo.read (ready);
        auto handle = [this] (const Event& e)
        {
            static const char* kinds[] = { "CC", "Nota", "Program Change" };
            lastText = "Can. " + juce::String (e.channel) + "  " + kinds[e.kind] + " "
                       + (e.kind == 1 ? noteName (e.number) : juce::String (e.number))
                       + (e.kind == 0 ? "  valore " + juce::String (e.value) : e.kind == 1 ? (e.value > 0 ? "  on" : "  off") : juce::String());
            const bool learnedNow = learning && armedValid;
            handleLearn (e);
            if (learnedNow) return;                      // il messaggio usato per imparare non comanda nulla
            bool mapped = false;
            for (const auto& m : list)
                if (matches (m, e))
                {
                    mapped = true;
                    if (m.target != Target::SlotSwitch && m.target != Target::SlotControl) handleGlobal (m, e);
                }
            if (! mapped && e.kind == 2 && pcPresets) loadPresetIndex (e.number);    // Program Change libero: preset n
        };
        for (int k = 0; k < r.blockSize1; ++k) handle (inEvents[r.startIndex1 + k]);
        for (int k = 0; k < r.blockSize2; ++k) handle (inEvents[r.startIndex2 + k]);
        sendChangeMessage();
    }

    void MidiManager::sendFeedback (bool force)
    {
        if (! feedback) return;
        if (lastSent.size() != list.size()) lastSent.assign (list.size(), -1);
        for (size_t i = 0; i < list.size(); ++i)
        {
            const auto& m = list[i];
            if (m.source == Source::Program) continue;
            float v = -1.0f;
            switch (m.target)
            {
                case Target::SlotSwitch: v = processor.chain.readForMidi (m.slot, -1); break;
                case Target::SlotControl:
                {
                    const float p = processor.chain.readForMidi (m.slot, m.control);
                    if (p >= 0.0f) v = std::abs (m.hi - m.lo) > 1.0e-4f ? juce::jlimit (0.0f, 1.0f, (p - m.lo) / (m.hi - m.lo)) : 0.0f;
                    break;
                }
                case Target::InputGain:  if (auto* p = processor.apvts.getParameter (inId)) v = (p->getValue() - m.lo) / juce::jmax (1.0e-4f, m.hi - m.lo); break;
                case Target::OutputGain: if (auto* p = processor.apvts.getParameter (outId)) v = (p->getValue() - m.lo) / juce::jmax (1.0e-4f, m.hi - m.lo); break;
                case Target::Bypass:     if (auto* p = processor.apvts.getParameter (bypassId)) v = p->getValue(); break;
                default: break;
            }
            if (v < 0.0f) continue;
            const int value = roundValue (juce::jlimit (0.0f, 1.0f, v));
            if (! force && lastSent[i] == value) continue;
            lastSent[i] = value;
            const int ch = m.channel == 0 ? 1 : m.channel;
            const auto msg = m.source == Source::CC ? juce::MidiMessage::controllerEvent (ch, m.number, value)
                                                    : (value >= 64 ? juce::MidiMessage::noteOn (ch, m.number, (juce::uint8) 127)
                                                                   : juce::MidiMessage::noteOff (ch, m.number));
            if (outFifo.getFreeSpace() > 0)
            {
                const auto w = outFifo.write (1);
                if (w.blockSize1 > 0) outEvents[w.startIndex1] = msg;
            }
        }
    }

    void MidiManager::timerCallback()
    {
        // istantanee della tabella non piu' usate dal thread audio
        Table* busy = inUse.load (std::memory_order_acquire);
        retired.erase (std::remove_if (retired.begin(), retired.end(), [busy] (const std::unique_ptr<Table>& t) { return t.get() != busy; }),
                       retired.end());
        if (const int pt = pendingParam.exchange (-1); pt >= 0 && learning && ! suppressParamLearn)
        {
            Mapping t;
            t.target = (Target) pt;
            arm (t);
        }
        drainEvents();
        if (slotsChanged.exchange (false)) processor.chain.sendChangeMessage();     // footswitch azionati dal MIDI
        sendFeedback (forceFeedback);
        forceFeedback = false;
    }

    void MidiManager::injectForTest (const juce::MidiMessage& msg)
    {
        juce::MidiBuffer b;
        b.addEvent (msg, 0);
        processBlock (b);
        drainEvents();
    }

    juce::MidiBuffer MidiManager::feedbackForTest()
    {
        sendFeedback (true);
        juce::MidiBuffer b;
        processBlock (b);
        return b;
    }

    //==============================================================================
    juce::ValueTree MidiManager::toValueTree() const
    {
        juce::ValueTree t ("MIDI");
        t.setProperty ("feedback", feedback, nullptr);
        t.setProperty ("pcPresets", pcPresets, nullptr);
        for (const auto& m : list)
        {
            juce::ValueTree c ("MAP");
            c.setProperty ("ch", m.channel, nullptr);
            c.setProperty ("src", (int) m.source, nullptr);
            c.setProperty ("num", m.number, nullptr);
            c.setProperty ("target", (int) m.target, nullptr);
            c.setProperty ("slot", m.slot, nullptr);
            c.setProperty ("ctl", m.control, nullptr);
            c.setProperty ("mode", (int) m.mode, nullptr);
            c.setProperty ("lo", m.lo, nullptr);
            c.setProperty ("hi", m.hi, nullptr);
            t.appendChild (c, nullptr);
        }
        return t;
    }

    void MidiManager::fromValueTree (const juce::ValueTree& t)
    {
        // progetti ricevuti da altri: ogni campo controllato, le assegnazioni non valide sono scartate
        std::vector<Mapping> fresh;
        if (t.isValid())
        {
            feedback = (bool) t.getProperty ("feedback", true);
            pcPresets = (bool) t.getProperty ("pcPresets", true);
            for (int i = 0; i < t.getNumChildren() && (int) fresh.size() < maxMappings; ++i)
            {
                const auto c = t.getChild (i);
                if (! c.hasType ("MAP")) continue;
                // dal file di progetto i valori arrivano come testo: solo numeri veri, entro i limiti
                auto value = [&c] (const char* k, double def)
                {
                    if (! c.hasProperty (k)) return def;
                    const auto v = c.getProperty (k);
                    if (v.isInt() || v.isInt64() || v.isDouble()) return (double) v;
                    const auto text = v.toString().trim();
                    if (text.isEmpty() || text.length() > 24 || ! text.containsOnly ("+-.0123456789eE")) return std::nan ("");
                    return text.getDoubleValue();
                };
                auto num = [&value] (const char* k, int lo, int hi, int def)
                {
                    const double d = value (k, def);
                    return std::isfinite (d) && d >= lo && d <= hi && d == std::floor (d) ? (int) d : -9999;
                };
                auto real = [&value] (const char* k, float def)
                {
                    const double d = value (k, def);
                    return std::isfinite (d) ? (float) juce::jlimit (0.0, 1.0, d) : def;
                };
                Mapping m;
                m.channel = num ("ch", 0, 16, 0);
                const int src = num ("src", 0, 2, 0), target = num ("target", 0, 6, 0), mode = num ("mode", 0, 2, 0);
                m.number = num ("num", 0, 127, 0);
                m.slot = num ("slot", -1, 99, -1);
                m.control = num ("ctl", -1, 11, -1);
                if (m.channel < 0 || src < 0 || target < 0 || mode < 0 || m.number < 0 || m.slot < -1 || m.control < -1) continue;
                m.source = (Source) src; m.target = (Target) target; m.mode = (Mode) mode;
                if ((m.target == Target::SlotSwitch || m.target == Target::SlotControl) && m.slot < 0) continue;
                if (m.target == Target::SlotControl && m.control < 0) continue;
                m.lo = real ("lo", 0.0f); m.hi = real ("hi", 1.0f);
                fresh.push_back (m);
            }
        }
        else { feedback = true; pcPresets = true; }
        setMappings (std::move (fresh));
    }

    //==============================================================================
    juce::String MidiManager::noteName (int note)
    {
        static const char* n[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return juce::String (n[note % 12]) + juce::String (note / 12 - 1) + " (" + juce::String (note) + ")";
    }

    juce::String MidiManager::describeSource (const Mapping& m) const
    {
        const juce::String ch = m.channel == 0 ? juce::String ("Tutti i canali") : "Can. " + juce::String (m.channel);
        switch (m.source)
        {
            case Source::CC:      return ch + "  CC " + juce::String (m.number);
            case Source::Note:    return ch + "  Nota " + noteName (m.number);
            case Source::Program: return ch + "  Program " + juce::String (m.number);
        }
        return ch;
    }

    juce::String MidiManager::describeTarget (const Mapping& m) const
    {
        switch (m.target)
        {
            case Target::InputGain:  return "INPUT (volume d'ingresso)";
            case Target::OutputGain: return "OUTPUT (volume d'uscita)";
            case Target::Bypass:     return "Bypass generale";
            case Target::PresetNext: return "Preset successivo";
            case Target::PresetPrev: return "Preset precedente";
            case Target::SlotSwitch:
            case Target::SlotControl:
            {
                juce::String s = "Slot " + juce::String (m.slot + 1);
                const auto* sl = processor.chain.slot (m.slot);
                const auto* d = sl != nullptr ? sl->def : nullptr;
                if (d == nullptr) return s + "  (vuoto)";
                s << "  " << d->code;
                if (m.target == Target::SlotSwitch) return s + "  footswitch (acceso/spento)";
                if (juce::isPositiveAndBelow (m.control, d->numControls)) return s + "  " + d->controls[m.control].label;
                return s + "  comando " + juce::String (m.control + 1);
            }
        }
        return {};
    }
}
