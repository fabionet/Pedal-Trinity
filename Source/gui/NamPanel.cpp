/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)
*/

#include "NamPanel.h"
#include "../engine/FxNam.h"

namespace pt::ui
{
    using namespace pt::engine;

    /** Pomello SLIM disegnato: da A2-Lite (tutto a sinistra) ad A2-Full (tutto a destra). */
    class NamPanel::SlimKnob : public juce::Slider
    {
    public:
        SlimKnob()
        {
            setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
            setRotaryParameters (juce::degreesToRadians (-150.0f), juce::degreesToRadians (150.0f), true);
            setRange (0.0, 1.0, 0.0);
            setMouseDragSensitivity (200);
            setDoubleClickReturnValue (true, 1.0);
            setPopupDisplayEnabled (true, true, nullptr);
            setMouseCursor (juce::MouseCursor::DraggingHandCursor);
            setTitle ("SLIM");
            textFromValueFunction = [] (double v)
            {
                return juce::String (v < 0.5 ? "A2-Lite (" : "A2-Full (") + juce::String ((int) std::lround (v * 100)) + "%)";
            };
        }
        void setActive (bool a) { setEnabled (a); setAlpha (a ? 1.0f : 0.35f); }

        void paint (juce::Graphics& g) override
        {
            const auto& t = themes->current();
            auto r = getLocalBounds().toFloat().reduced (4.0f);
            const float d = juce::jmin (r.getWidth(), r.getHeight());
            r = r.withSizeKeepingCentre (d, d);
            const auto c = r.getCentre();
            const float a0 = juce::degreesToRadians (-150.0f), a1 = juce::degreesToRadians (150.0f);
            const float ang = a0 + (float) getValue() * (a1 - a0);
            juce::Path arc;
            arc.addCentredArc (c.x, c.y, d * 0.5f - 2.0f, d * 0.5f - 2.0f, 0.0f, a0, a1, true);
            g.setColour (t.textDim.withAlpha (0.35f));
            g.strokePath (arc, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            juce::Path val;
            val.addCentredArc (c.x, c.y, d * 0.5f - 2.0f, d * 0.5f - 2.0f, 0.0f, a0, ang, true);
            g.setColour (juce::Colour (0xffd8322a));
            g.strokePath (val, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            const auto body = r.reduced (d * 0.16f);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a4a4e), body.getTopLeft(), juce::Colour (0xff141416), body.getBottomRight(), false));
            g.fillEllipse (body);
            g.setColour (juce::Colours::black.withAlpha (0.6f));
            g.drawEllipse (body, 1.2f);
            const float rr = body.getWidth() * 0.5f;
            g.setColour (juce::Colours::white.withAlpha (0.9f));
            g.drawLine (c.x + std::sin (ang) * rr * 0.25f, c.y - std::cos (ang) * rr * 0.25f,
                        c.x + std::sin (ang) * rr * 0.85f, c.y - std::cos (ang) * rr * 0.85f, 2.4f);
        }
    private:
        juce::SharedResourcePointer<ThemeManager> themes;
    };

    struct NamPanel::ChannelUi
    {
        juce::Label title, namFile, irFile, status, slimLabel;
        juce::TextButton loadNam { "Carica NAM..." }, clearNam { "X" }, loadIr { "Carica IR..." }, clearIr { "X" };
        SlimKnob slim;
    };

    NamPanel::NamPanel (Chain& c, int i) : chain (c), index (i)
    {
        title.setText ("Modelli NAM e IR", juce::dontSendNotification);
        title.setFont (juce::Font (18.0f, juce::Font::bold));
        addAndMakeVisible (title);
        for (int k = 0; k < 2; ++k)
        {
            auto& u = *(chans[k] = std::make_unique<ChannelUi>());
            u.title.setText (k == 0 ? "Canale A" : "Canale B", juce::dontSendNotification);
            u.title.setFont (juce::Font (16.0f, juce::Font::bold));
            for (auto* l : { &u.namFile, &u.irFile }) { l->setFont (juce::Font (13.0f)); l->setMinimumHorizontalScale (0.7f); }
            u.status.setFont (juce::Font (12.0f));
            u.status.setJustificationType (juce::Justification::topLeft);
            u.slimLabel.setText ("SLIM", juce::dontSendNotification);
            u.slimLabel.setFont (juce::Font (11.0f, juce::Font::bold));
            u.slimLabel.setJustificationType (juce::Justification::centred);
            u.loadNam.setTooltip ("Carica un modello .nam (A1, A2, LSTM) o .namb");
            u.loadIr.setTooltip ("Carica una risposta all'impulso .wav / .aiff");
            u.clearNam.setTooltip ("Togli il modello");
            u.clearIr.setTooltip ("Togli l'IR");
            u.loadNam.onClick = [this, k] { choose (k, true); };
            u.loadIr.onClick = [this, k] { choose (k, false); };
            u.clearNam.onClick = [this, k] { if (auto* e = effect()) e->clearNam (k); refresh(); };
            u.clearIr.onClick = [this, k] { if (auto* e = effect()) e->clearIr (k); refresh(); };
            u.slim.onValueChange = [this] { pushOptions(); };
            u.slim.setTooltip ("SLIM: sceglie la rete del modello A2 (Lite a sinistra, Full a destra). Attivo solo con modelli slimmable.");
            for (auto* comp : std::initializer_list<juce::Component*> { &u.title, &u.namFile, &u.irFile, &u.status, &u.slimLabel,
                                                                        &u.loadNam, &u.clearNam, &u.loadIr, &u.clearIr, &u.slim })
                addAndMakeVisible (comp);
        }

        calTitle.setText ("Calibrazione e opzioni (come nel plugin NAM)", juce::dontSendNotification);
        calTitle.setFont (juce::Font (15.0f, juce::Font::bold));
        addAndMakeVisible (calTitle);
        for (auto* s : { &calLevel, &gateThreshold })
        {
            s->setSliderStyle (juce::Slider::LinearHorizontal);
            s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
            s->onValueChange = [this] { pushOptions(); };
            addAndMakeVisible (s);
        }
        calLevel.setRange (-60.0, 60.0, 0.1);
        calLevel.setTextValueSuffix (" dBu");
        calLevel.setDoubleClickReturnValue (true, 12.0);
        calLevel.setTooltip ("Livello d'ingresso della scheda audio in dBu per 0 dBFS (usato dalla calibrazione)");
        gateThreshold.setRange (-100.0, 0.0, 0.1);
        gateThreshold.setTextValueSuffix (" dB");
        gateThreshold.setDoubleClickReturnValue (true, -80.0);
        calLevelLabel.setText ("Livello d'ingresso", juce::dontSendNotification);
        gateLabel.setText ("Soglia del gate", juce::dontSendNotification);
        outputLabel.setText ("Modo d'uscita", juce::dontSendNotification);
        for (auto* l : { &calLevelLabel, &gateLabel, &outputLabel }) { l->setFont (juce::Font (13.0f)); addAndMakeVisible (l); }
        outputMode.addItem ("Raw", 1);
        outputMode.addItem ("Normalized (-18 dB)", 2);
        outputMode.addItem ("Calibrated", 3);
        outputMode.setTooltip ("Raw: livello del modello; Normalized: loudness a -18 dB; Calibrated: livello d'uscita reale dell'ampli");
        outputMode.onChange = [this] { pushOptions(); };
        addAndMakeVisible (outputMode);
        calibrate.setTooltip ("Porta il segnale al livello d'ingresso con cui e' stato catturato il modello");
        for (auto* b : { &calibrate, &gate, &eq, &irOn })
        {
            b->onClick = [this] { pushOptions(); };
            addAndMakeVisible (b);
        }
        bindTo (i);
        startTimerHz (4);
    }

    NamPanel::~NamPanel() { stopTimer(); }

    void NamPanel::bindTo (int i)
    {
        index = i;
        slotRef = chain.slotRef (i);
        refresh();
    }

    NamEffect* NamPanel::effect() const
    {
        return slotRef != nullptr ? dynamic_cast<NamEffect*> (slotRef->fx.get()) : nullptr;
    }

    void NamPanel::refresh()
    {
        auto* e = effect();
        if (e == nullptr) return;
        const auto& t = themes->current();
        updating = true;
        title.setColour (juce::Label::textColourId, t.accent);
        calTitle.setColour (juce::Label::textColourId, t.accent);
        const auto opt = e->options();
        for (int k = 0; k < 2; ++k)
        {
            auto& u = *chans[k];
            const auto st = e->status (k);
            u.title.setColour (juce::Label::textColourId, t.text);
            for (auto* l : { &u.namFile, &u.irFile, &u.slimLabel }) l->setColour (juce::Label::textColourId, t.textDim);
            u.namFile.setText (st.hasNam ? "NAM: " + juce::String (st.namFile) + "  (" + juce::String (st.namKind) + ")" : juce::String ("NAM: nessuno"),
                               juce::dontSendNotification);
            u.irFile.setText (st.hasIr ? "IR: " + juce::String (st.irFile) : juce::String ("IR: nessuno"), juce::dontSendNotification);
            u.status.setText (juce::String (st.message), juce::dontSendNotification);
            u.status.setColour (juce::Label::textColourId, st.warning ? juce::Colour (0xffff6a5a) : t.textDim);
            u.clearNam.setEnabled (st.hasNam);
            u.clearIr.setEnabled (st.hasIr);
            u.slim.setActive (st.hasNam && st.slimmable);          // SLIM solo con file slimmable (A2)
            u.slim.setValue (opt.slim[k], juce::dontSendNotification);
        }
        calibrate.setToggleState (opt.calibrateInput, juce::dontSendNotification);
        calLevel.setValue (opt.calibrationLevel, juce::dontSendNotification);
        calLevel.setEnabled (opt.calibrateInput || opt.outputMode == 2);
        calLevel.setAlpha (calLevel.isEnabled() ? 1.0f : 0.4f);
        outputMode.setSelectedId (opt.outputMode + 1, juce::dontSendNotification);
        gate.setToggleState (opt.noiseGate, juce::dontSendNotification);
        gateThreshold.setValue (opt.gateThreshold, juce::dontSendNotification);
        gateThreshold.setEnabled (opt.noiseGate);
        gateThreshold.setAlpha (opt.noiseGate ? 1.0f : 0.4f);
        eq.setToggleState (opt.toneStack, juce::dontSendNotification);
        irOn.setToggleState (opt.irEnabled, juce::dontSendNotification);
        updating = false;
        repaint();
    }

    void NamPanel::pushOptions()
    {
        auto* e = effect();
        if (e == nullptr || updating) return;
        auto o = e->options();
        o.calibrateInput = calibrate.getToggleState();
        o.calibrationLevel = calLevel.getValue();
        o.outputMode = juce::jlimit (0, 2, outputMode.getSelectedId() - 1);
        o.noiseGate = gate.getToggleState();
        o.gateThreshold = gateThreshold.getValue();
        o.toneStack = eq.getToggleState();
        o.irEnabled = irOn.getToggleState();
        for (int k = 0; k < 2; ++k) o.slim[k] = chans[k]->slim.getValue();
        e->setOptions (o);
        refresh();
    }

    void NamPanel::choose (int k, bool model)
    {
        chooser = std::make_unique<juce::FileChooser> (model ? "Modello NAM (.nam / .namb)" : "Risposta all'impulso (.wav / .aiff)",
                                                       juce::File(), model ? NamEffect::modelWildcard() : NamEffect::irWildcard());
        const juce::Component::SafePointer<NamPanel> safe (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [safe, k, model] (const juce::FileChooser& fc)
        {
            if (safe == nullptr) return;
            const auto f = fc.getResult();
            if (f == juce::File()) return;
            if (auto* e = safe->effect())
            {
                juce::MouseCursor::showWaitCursor();
                if (model) e->loadNam (k, f); else e->loadIr (k, f);
                juce::MouseCursor::hideWaitCursor();
            }
            safe->refresh();
        });
    }

    void NamPanel::timerCallback() { refresh(); }

    void NamPanel::paint (juce::Graphics& g)
    {
        const auto& t = themes->current();
        for (int k = 0; k < 2; ++k)
        {
            auto r = chans[k]->title.getBounds().getUnion (chans[k]->status.getBounds()).expanded (6, 4).toFloat();
            g.setColour (t.panelBottom.withAlpha (0.7f));
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour ((k == 0 ? t.accent : juce::Colour (0xffc8322a)).withAlpha (0.6f));
            g.fillRect (r.withWidth (3.0f));
        }
    }

    void NamPanel::resized()
    {
        auto r = getLocalBounds().reduced (6);
        title.setBounds (r.removeFromTop (26));
        r.removeFromTop (4);
        const int chH = juce::jmin (160, (r.getHeight() - 190) / 2);
        for (int k = 0; k < 2; ++k)
        {
            auto& u = *chans[k];
            auto box = r.removeFromTop (chH).reduced (6, 4);
            r.removeFromTop (8);
            auto knob = box.removeFromRight (juce::jmin (100, box.getWidth() / 4));
            u.slimLabel.setBounds (knob.removeFromTop (16));
            u.slim.setBounds (knob.removeFromTop (juce::jmin (knob.getHeight(), 84)));
            u.title.setBounds (box.removeFromTop (22));
            auto row = box.removeFromTop (28);
            u.clearNam.setBounds (row.removeFromRight (28).reduced (0, 2));
            row.removeFromRight (4);
            u.loadNam.setBounds (row.removeFromRight (juce::jmin (120, row.getWidth() / 2)).reduced (0, 2));
            u.namFile.setBounds (row);
            box.removeFromTop (2);
            row = box.removeFromTop (28);
            u.clearIr.setBounds (row.removeFromRight (28).reduced (0, 2));
            row.removeFromRight (4);
            u.loadIr.setBounds (row.removeFromRight (juce::jmin (120, row.getWidth() / 2)).reduced (0, 2));
            u.irFile.setBounds (row);
            u.status.setBounds (box.withHeight (juce::jmin (box.getHeight(), 60)));
        }
        calTitle.setBounds (r.removeFromTop (24));
        auto line = [&r] { return r.removeFromTop (28); };
        auto a = line();
        calibrate.setBounds (a.removeFromLeft (a.getWidth() / 2));
        gate.setBounds (a);
        a = line();
        calLevelLabel.setBounds (a.removeFromLeft (130));
        calLevel.setBounds (a);
        a = line();
        outputLabel.setBounds (a.removeFromLeft (130));
        outputMode.setBounds (a.reduced (0, 2));
        a = line();
        gateLabel.setBounds (a.removeFromLeft (130));
        gateThreshold.setBounds (a);
        a = line();
        eq.setBounds (a.removeFromLeft (a.getWidth() / 2));
        irOn.setBounds (a);
    }
}
