/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)
*/

#include "MidiPanel.h"

namespace pt::ui
{
    using midi::Mapping;
    using midi::Target;
    using midi::Mode;

    /** Una riga dell'elenco: messaggio, funzione, canale, modo o gamma, elimina. */
    class MidiPanel::Row : public juce::Component
    {
    public:
        Row (midi::MidiManager& m, int i) : manager (m), index (i)
        {
            const auto map = manager.mappings()[(size_t) index];
            source.setText (manager.describeSource (map), juce::dontSendNotification);
            target.setText (manager.describeTarget (map), juce::dontSendNotification);
            for (auto* l : { &source, &target }) { l->setFont (juce::Font (13.5f)); addAndMakeVisible (l); }
            source.setFont (juce::Font (13.5f, juce::Font::bold));

            channel.addItem ("Tutti i canali", 1);
            for (int c = 1; c <= 16; ++c) channel.addItem ("Canale " + juce::String (c), c + 1);
            channel.setSelectedId (map.channel + 1, juce::dontSendNotification);
            channel.setTooltip ("Canale MIDI su cui ascoltare il messaggio");
            channel.onChange = [this] { edit ([this] (Mapping& x) { x.channel = channel.getSelectedId() - 1; }); };
            addAndMakeVisible (channel);

            if (map.isSwitch())
            {
                mode.addItem ("Segue il valore (interruttore a scatto)", 1);
                mode.addItem ("Ogni pressione accende/spegne", 2);
                mode.addItem ("Acceso solo finche' e' premuto", 3);
                mode.setSelectedId ((int) map.mode + 1, juce::dontSendNotification);
                mode.setTooltip ("Come il messaggio comanda l'interruttore");
                mode.onChange = [this] { edit ([this] (Mapping& x) { x.mode = (Mode) (mode.getSelectedId() - 1); }); };
                addAndMakeVisible (mode);
            }
            else if (map.target == Target::SlotControl || map.target == Target::InputGain || map.target == Target::OutputGain)
            {
                for (auto* s : { &lo, &hi })
                {
                    s->setSliderStyle (juce::Slider::LinearHorizontal);
                    s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 20);
                    s->setRange (0.0, 100.0, 1.0);
                    s->setTextValueSuffix ("%");
                    addAndMakeVisible (s);
                }
                lo.setValue (map.lo * 100.0, juce::dontSendNotification);
                hi.setValue (map.hi * 100.0, juce::dontSendNotification);
                lo.setTooltip ("Posizione del comando con il valore MIDI 0");
                hi.setTooltip ("Posizione del comando con il valore MIDI 127 (anche minore del minimo: verso invertito)");
                lo.onDragEnd = [this] { edit ([this] (Mapping& x) { x.lo = (float) lo.getValue() / 100.0f; }); };
                hi.onDragEnd = [this] { edit ([this] (Mapping& x) { x.hi = (float) hi.getValue() / 100.0f; }); };
                lo.onValueChange = [this] { if (! lo.isMouseButtonDown()) edit ([this] (Mapping& x) { x.lo = (float) lo.getValue() / 100.0f; }); };
                hi.onValueChange = [this] { if (! hi.isMouseButtonDown()) edit ([this] (Mapping& x) { x.hi = (float) hi.getValue() / 100.0f; }); };
            }
            remove.setButtonText ("X");
            remove.setTooltip ("Elimina l'assegnazione");
            remove.onClick = [this]
            {
                auto& mgr = manager;
                const int i2 = index;
                juce::MessageManager::callAsync ([&mgr, i2] { mgr.removeMapping (i2); });
            };
            addAndMakeVisible (remove);
        }

        void paint (juce::Graphics& g) override
        {
            const auto& t = themes->current();
            g.setColour ((index % 2 == 0 ? t.panelTop : t.panelBottom).withAlpha (0.9f));
            g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 5.0f);
            source.setColour (juce::Label::textColourId, t.accent);
            target.setColour (juce::Label::textColourId, t.text);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (8, 4);
            remove.setBounds (r.removeFromRight (30).reduced (0, 2));
            r.removeFromRight (8);
            auto extra = r.removeFromRight (juce::jmin (330, r.getWidth() / 3));
            if (mode.isVisible()) mode.setBounds (extra.reduced (0, 2));
            if (lo.isVisible())
            {
                lo.setBounds (extra.removeFromLeft (extra.getWidth() / 2).reduced (2, 2));
                hi.setBounds (extra.reduced (2, 2));
            }
            r.removeFromRight (8);
            channel.setBounds (r.removeFromRight (128).reduced (0, 2));
            r.removeFromRight (8);
            source.setBounds (r.removeFromLeft (juce::jmin (230, r.getWidth() / 2)));
            target.setBounds (r);
        }

    private:
        void edit (std::function<void (Mapping&)> f)
        {
            auto all = manager.mappings();
            if (! juce::isPositiveAndBelow (index, (int) all.size())) return;
            auto m = all[(size_t) index];
            f (m);
            auto& mgr = manager;
            const int i2 = index;
            juce::MessageManager::callAsync ([&mgr, i2, m] { mgr.updateMapping (i2, m); });
        }

        midi::MidiManager& manager;
        int index;
        juce::Label source, target;
        juce::ComboBox channel, mode;
        juce::Slider lo, hi;
        juce::TextButton remove;
        juce::SharedResourcePointer<ThemeManager> themes;
    };

    class MidiPanel::RowList : public juce::Component
    {
    public:
        juce::OwnedArray<Row> rows;
        void resized() override
        {
            for (int i = 0; i < rows.size(); ++i) rows[i]->setBounds (0, i * rowH, getWidth(), rowH - 3);
        }
        static constexpr int rowH = 40;
    };

    //==============================================================================
    MidiPanel::MidiPanel (midi::MidiManager& m) : manager (m)
    {
        setWantsKeyboardFocus (true);
        for (auto* l : { &title, &devLabel, &inLabel, &outLabel, &pluginInfo, &optLabel, &listLabel, &monitor, &learnStatus, &empty })
        {
            l->setInterceptsMouseClicks (false, false);
            addAndMakeVisible (l);
        }
        title.setText ("MIDI: pedaliera esterna", juce::dontSendNotification);
        title.setFont (juce::Font (26.0f, juce::Font::bold));
        devLabel.setText ("Dispositivi MIDI", juce::dontSendNotification);
        optLabel.setText ("Opzioni", juce::dontSendNotification);
        listLabel.setText ("Assegnazioni", juce::dontSendNotification);
        for (auto* l : { &devLabel, &optLabel, &listLabel }) l->setFont (juce::Font (16.0f, juce::Font::bold));
        inLabel.setText ("MIDI IN", juce::dontSendNotification);
        outLabel.setText ("MIDI OUT", juce::dontSendNotification);
        for (auto* l : { &inLabel, &outLabel }) l->setFont (juce::Font (14.0f, juce::Font::bold));
        pluginInfo.setFont (juce::Font (13.0f));
        pluginInfo.setText ("Nel plugin il MIDI passa dalla DAW: manda alla traccia di Pedal Trinity il MIDI della pedaliera "
                            "e, per il ritorno dello stato, collega l'uscita MIDI del plugin alla pedaliera.", juce::dontSendNotification);
        monitor.setFont (juce::Font (13.0f, juce::Font::bold));
        learnStatus.setFont (juce::Font (13.0f));
        learnStatus.setMinimumHorizontalScale (0.75f);
        empty.setFont (juce::Font (14.0f));
        empty.setJustificationType (juce::Justification::centred);
        empty.setText ("Nessuna assegnazione. Premi \"Mappa con il tocco\": tocca un pomello o un footswitch sulla pedaliera "
                       "e poi muovi il comando della pedaliera MIDI.", juce::dontSendNotification);

        output.onChange = [this]
        {
            auto& sd = midi::StandaloneDevices::get();
            if (sd.manager == nullptr) return;
            const int id = output.getSelectedId();
            sd.manager->setDefaultMidiOutputDevice (id >= 2 && id - 2 < outDevices.size() ? outDevices[id - 2].identifier : juce::String());
            if (sd.outputChanged) sd.outputChanged();
        };
        output.setTooltip ("Dispositivo su cui rimandare lo stato (LED della pedaliera MIDI)");
        addAndMakeVisible (output);

        feedback.onClick = [this] { manager.setFeedbackEnabled (feedback.getToggleState()); };
        programChange.onClick = [this] { manager.setProgramChangeSelectsPreset (programChange.getToggleState()); };
        touchLearn.setTooltip ("Chiude il pannello: tocca un comando sulla pedaliera e muovi quello della pedaliera MIDI");
        touchLearn.onClick = [this] { if (onStartTouchLearn) onStartTouchLearn(); };
        auto armGlobal = [this] (Target t) { Mapping m; m.target = t; manager.arm (m); };
        learnNext.onClick = [armGlobal] { armGlobal (Target::PresetNext); };
        learnPrev.onClick = [armGlobal] { armGlobal (Target::PresetPrev); };
        learnBypass.onClick = [armGlobal] { armGlobal (Target::Bypass); };
        learnNext.setTooltip ("Premi poi il footswitch MIDI che deve caricare il preset successivo");
        learnPrev.setTooltip ("Premi poi il footswitch MIDI che deve caricare il preset precedente");
        learnBypass.setTooltip ("Premi poi il footswitch MIDI che deve escludere tutta la pedaliera");
        clearAll.onClick = [this]
        {
            juce::Component::SafePointer<MidiPanel> safe (this);
            juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::QuestionIcon)
                                              .withTitle ("Cancella le assegnazioni MIDI")
                                              .withMessage ("Eliminare tutte le assegnazioni MIDI di questo progetto?")
                                              .withButton ("Elimina tutte").withButton ("Annulla").withAssociatedComponent (this),
                                          [safe] (int r) { if (safe != nullptr && r == 1) safe->manager.clearMappings(); });
        };
        closeButton.onClick = [this] { if (onClose) onClose(); };
        for (auto* c : std::initializer_list<juce::Component*> { &feedback, &programChange, &touchLearn, &learnNext, &learnPrev, &learnBypass, &clearAll, &closeButton })
            addAndMakeVisible (c);

        rows = std::make_unique<RowList>();
        rowView.setViewedComponent (rows.get(), false);
        rowView.setScrollBarsShown (true, false);
        addAndMakeVisible (rowView);

        rebuildDevices();
        rebuildRows();
        syncControls();
        manager.addChangeListener (this);
        startTimerHz (4);
    }

    MidiPanel::~MidiPanel()
    {
        manager.removeChangeListener (this);
        // la mappatura avviata con i tasti "Impara" non resta in sospeso
        if (manager.hasArmed() && ! manager.isLearning()) manager.setLearning (false);
    }

    void MidiPanel::rebuildDevices()
    {
        inputs.clear();
        output.clear (juce::dontSendNotification);
        auto& sd = midi::StandaloneDevices::get();
        const bool standalone = sd.manager != nullptr;
        pluginInfo.setVisible (! standalone);
        inLabel.setVisible (standalone);
        outLabel.setVisible (standalone);
        output.setVisible (standalone);
        if (! standalone) return;
        for (const auto& d : juce::MidiInput::getAvailableDevices())
        {
            auto* b = inputs.add (new juce::ToggleButton (d.name));
            b->setToggleState (sd.manager->isMidiInputDeviceEnabled (d.identifier), juce::dontSendNotification);
            const auto id = d.identifier;
            b->onClick = [b, id] { if (auto* dm = midi::StandaloneDevices::get().manager) dm->setMidiInputDeviceEnabled (id, b->getToggleState()); };
            addAndMakeVisible (b);
        }
        if (inputs.isEmpty())
        {
            auto* b = inputs.add (new juce::ToggleButton ("(nessun dispositivo MIDI IN collegato)"));
            b->setEnabled (false);
            addAndMakeVisible (b);
        }
        outDevices = juce::MidiOutput::getAvailableDevices();
        output.addItem ("Nessuno", 1);
        int sel = 1;
        for (int i = 0; i < outDevices.size(); ++i)
        {
            output.addItem (outDevices[i].name, i + 2);
            if (outDevices[i].identifier == sd.manager->getDefaultMidiOutputIdentifier()) sel = i + 2;
        }
        output.setSelectedId (sel, juce::dontSendNotification);
        resized();
    }

    void MidiPanel::rebuildRows()
    {
        const auto all = manager.mappings();
        rows->rows.clear();
        for (int i = 0; i < (int) all.size(); ++i)
            rows->addAndMakeVisible (rows->rows.add (new Row (manager, i)));
        shownVersion = manager.version();
        empty.setVisible (all.empty());
        rows->setSize (juce::jmax (100, rowView.getWidth() - rowView.getScrollBarThickness() - 2), (int) all.size() * RowList::rowH);
        rows->resized();
    }

    void MidiPanel::syncControls()
    {
        feedback.setToggleState (manager.feedbackEnabled(), juce::dontSendNotification);
        programChange.setToggleState (manager.programChangeSelectsPreset(), juce::dontSendNotification);
        const auto last = manager.lastMessage();
        monitor.setText ("Ultimo messaggio ricevuto:  " + (last.isEmpty() ? juce::String ("nessuno") : last), juce::dontSendNotification);
        learnStatus.setText (manager.hasArmed() ? manager.learnStatus() : juce::String(), juce::dontSendNotification);
        const auto& t = themes->current();
        title.setColour (juce::Label::textColourId, t.accent);
        for (auto* l : { &devLabel, &optLabel, &listLabel, &inLabel, &outLabel }) l->setColour (juce::Label::textColourId, t.text);
        for (auto* l : { &pluginInfo, &empty }) l->setColour (juce::Label::textColourId, t.textDim);
        monitor.setColour (juce::Label::textColourId, t.accent);
        learnStatus.setColour (juce::Label::textColourId, juce::Colour (0xff4cc26a));
    }

    void MidiPanel::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        // le righe si ricostruiscono solo quando cambia l'elenco delle assegnazioni
        if (manager.version() != shownVersion) rebuildRows();
        syncControls();
    }

    void MidiPanel::timerCallback() { syncControls(); }

    juce::Rectangle<int> MidiPanel::card() const
    {
        return getLocalBounds().withSizeKeepingCentre (juce::jmin (1040, getWidth() - 40), juce::jmin (760, getHeight() - 20));
    }

    void MidiPanel::paint (juce::Graphics& g)
    {
        const auto& t = themes->current();
        g.fillAll (juce::Colours::black.withAlpha (0.62f));
        const auto c = card().toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRoundedRectangle (c.translated (0, 6).expanded (4), 14.0f);
        g.setGradientFill (juce::ColourGradient (t.panelTop.brighter (0.06f), c.getTopLeft(), t.panelBottom, c.getBottomLeft(), false));
        g.fillRoundedRectangle (c, 12.0f);
        g.setColour (t.accent.withAlpha (0.55f));
        g.drawRoundedRectangle (c.reduced (1.0f), 12.0f, 1.4f);
        g.setColour (t.text.withAlpha (0.12f));
        for (auto* l : { &optLabel, &listLabel })
            g.drawHorizontalLine (l->getY() - 6, c.getX() + 24.0f, c.getRight() - 24.0f);
    }

    void MidiPanel::resized()
    {
        auto c = card().reduced (24, 16);
        title.setBounds (c.removeFromTop (36));
        c.removeFromTop (4);
        auto bottom = c.removeFromBottom (34);
        closeButton.setBounds (bottom.removeFromRight (120));
        clearAll.setBounds (bottom.removeFromLeft (140));
        c.removeFromBottom (8);

        devLabel.setBounds (c.removeFromTop (24));
        if (pluginInfo.isVisible())
            pluginInfo.setBounds (c.removeFromTop (40));
        else
        {
            auto row = c.removeFromTop (30);
            outLabel.setBounds (row.removeFromLeft (80));
            output.setBounds (row.removeFromLeft (320).reduced (0, 2));
            c.removeFromTop (4);
            auto in = c.removeFromTop (juce::jmin (90, 26 * ((inputs.size() + 1) / 2)));
            inLabel.setBounds (in.removeFromLeft (80).withHeight (26));
            const int w = in.getWidth() / 2;
            for (int i = 0; i < inputs.size(); ++i)
                inputs[i]->setBounds (in.getX() + (i % 2) * w, in.getY() + (i / 2) * 26, w - 8, 24);
        }
        c.removeFromTop (14);
        optLabel.setBounds (c.removeFromTop (24));
        feedback.setBounds (c.removeFromTop (26));
        programChange.setBounds (c.removeFromTop (26));
        c.removeFromTop (14);
        listLabel.setBounds (c.removeFromTop (24));
        auto buttons = c.removeFromTop (32);
        for (auto* b : { &touchLearn, &learnNext, &learnPrev, &learnBypass })
        {
            b->setBounds (buttons.removeFromLeft (b == &touchLearn ? 170 : 140).reduced (0, 2));
            buttons.removeFromLeft (8);
        }
        monitor.setBounds (buttons);
        learnStatus.setBounds (c.removeFromTop (22));
        c.removeFromTop (4);
        rowView.setBounds (c);
        empty.setBounds (c);
        rows->setSize (juce::jmax (100, c.getWidth() - rowView.getScrollBarThickness() - 2), rows->rows.size() * RowList::rowH);
    }

    void MidiPanel::mouseUp (const juce::MouseEvent& e)
    {
        if (! card().contains (e.getPosition()) && onClose) onClose();
    }

    bool MidiPanel::keyPressed (const juce::KeyPress& k)
    {
        if (k == juce::KeyPress::escapeKey && onClose) { onClose(); return true; }
        return false;
    }

    //==============================================================================
    MidiLearnBar::MidiLearnBar (midi::MidiManager& m) : manager (m)
    {
        list.onClick = [this] { if (onShowList) onShowList(); };
        done.onClick = [this] { if (onDone) onDone(); };
        list.setTooltip ("Elenco delle assegnazioni MIDI, dispositivi e opzioni");
        done.setTooltip ("Termina la mappatura MIDI");
        addAndMakeVisible (list);
        addAndMakeVisible (done);
        manager.addChangeListener (this);
        startTimerHz (3);
    }

    MidiLearnBar::~MidiLearnBar() { manager.removeChangeListener (this); }

    void MidiLearnBar::paint (juce::Graphics& g)
    {
        const auto& t = themes->current();
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (juce::Colour (0xf0101114));
        g.fillRoundedRectangle (r, 8.0f);
        const auto green = juce::Colour (0xff4cc26a);
        g.setColour (manager.hasArmed() ? green : t.accent);
        g.drawRoundedRectangle (r, 8.0f, 2.0f);
        // spia lampeggiante della mappatura
        const bool blink = (juce::Time::getMillisecondCounter() / 450) % 2 == 0;
        g.setColour ((manager.hasArmed() ? green : t.accent).withAlpha (blink ? 1.0f : 0.35f));
        g.fillEllipse (14.0f, r.getCentreY() - 6.0f, 12.0f, 12.0f);
        g.setColour (t.text);
        g.setFont (juce::Font (14.0f, juce::Font::bold));
        auto text = getLocalBounds().withTrimmedLeft (36).withTrimmedRight (list.getWidth() + done.getWidth() + 30);
        g.drawFittedText ("MAPPATURA MIDI  -  " + manager.learnStatus(), text, juce::Justification::centredLeft, 2, 0.8f);
    }

    void MidiLearnBar::resized()
    {
        auto r = getLocalBounds().reduced (8, 6);
        done.setBounds (r.removeFromRight (80));
        r.removeFromRight (8);
        list.setBounds (r.removeFromRight (140));
    }
}
