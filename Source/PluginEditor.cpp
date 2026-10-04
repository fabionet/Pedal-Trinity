/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)
*/

#include "PluginEditor.h"
#include "Version.h"

using namespace pt;
using namespace pt::ui;

namespace
{
    /** Tasto OPZIONI: ingranaggio disegnato. */
    /** Tasto REAL MOD PEDALBOARD: acceso trasforma la pedaliera nelle repliche dei pedali reali,
        ripremuto torna ai pedali originali Pedal Trinity (il suono non cambia). */
    class RealModButton : public juce::Button
    {
    public:
        RealModButton() : juce::Button ("REAL MOD PEDALBOARD")
        {
            setClickingTogglesState (true);
            setTooltip ("REAL MOD PEDALBOARD: mostra le repliche dei pedali reali con i loro nomi "
                        "(o le tue foto della lista scelta nelle Opzioni). Ripremi per tornare ai pedali originali.");
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
        }
        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            const bool on = getToggleState();
            auto r = getLocalBounds().toFloat().reduced (0.5f);
            const auto accent = findColour (juce::TextButton::textColourOffId);
            if (on)
            {
                g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe8402e), r.getTopLeft(), juce::Colour (0xff8a1810), r.getBottomLeft(), false));
                g.fillRoundedRectangle (r, 5.0f);
                g.setColour (juce::Colour (0xffffd0a0).withAlpha (over ? 0.95f : 0.7f));
                g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.2f);
            }
            else
                getLookAndFeel().drawButtonBackground (g, *this, findColour (juce::TextButton::buttonColourId), over, down);
            g.setColour (on ? juce::Colours::white : accent);
            auto t = r.reduced (4.0f, 2.0f);
            g.setFont (juce::Font (13.5f, juce::Font::bold));
            g.drawFittedText ("REAL MOD", t.removeFromTop (t.getHeight() * 0.55f).toNearestInt(), juce::Justification::centredBottom, 1);
            g.setFont (juce::Font (9.5f, juce::Font::bold));
            g.drawFittedText ("PEDALBOARD", t.toNearestInt(), juce::Justification::centredTop, 1, 0.8f);
        }
    };

    class GearButton : public juce::Button
    {
    public:
        GearButton() : juce::Button ("Opzioni")
        {
            setTooltip ("Opzioni: tema della pedaliera e cavi");
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
        }
        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            getLookAndFeel().drawButtonBackground (g, *this, findColour (juce::TextButton::buttonColourId), over, down);
            const auto r = getLocalBounds().toFloat().withSizeKeepingCentre (18.0f, 18.0f);
            const auto c = r.getCentre();
            juce::Path gear;
            const int teeth = 8;
            for (int i = 0; i < teeth * 2; ++i)
            {
                const float a = juce::MathConstants<float>::twoPi * (float) i / (float) (teeth * 2);
                const float rad = (i % 2 == 0 ? 9.0f : 7.0f);
                const float a0 = a - 0.18f, a1 = a + 0.18f;
                const juce::Point<float> p0 (c.x + rad * std::sin (a0), c.y - rad * std::cos (a0)), p1 (c.x + rad * std::sin (a1), c.y - rad * std::cos (a1));
                if (i == 0) gear.startNewSubPath (p0); else gear.lineTo (p0);
                gear.lineTo (p1);
            }
            gear.closeSubPath();
            gear.addEllipse (r.withSizeKeepingCentre (7.0f, 7.0f));
            gear.setUsingNonZeroWinding (false);
            g.setColour (findColour (juce::TextButton::textColourOffId));
            g.fillPath (gear);
        }
    };

    constexpr int toolbarH = 50;
    constexpr int views[4] = { 3, 6, 9, 18 };
}

const juce::Array<juce::Point<int>>& PedalTrinityEditor::sizePresets()
{
    static const juce::Array<juce::Point<int>> s = []
    {
        juce::Array<juce::Point<int>> a;
        for (auto p : { juce::Point<int> (1280, 760), juce::Point<int> (1440, 810), juce::Point<int> (1600, 900),
                        juce::Point<int> (1920, 1080), juce::Point<int> (2560, 1440) })
            a.add (p);
        return a;
    }();
    return s;
}

PedalTrinityEditor::PedalTrinityEditor (PedalTrinityProcessor& p) : AudioProcessorEditor (p), processor (p)
{
    setLookAndFeel (&lookAndFeel);

    logo.setText ("PEDAL TRINITY", juce::dontSendNotification);
    logo.setFont (juce::Font (22.0f, juce::Font::bold));
    logo.setColour (juce::Label::textColourId, juce::Colour (0xffd9b464));
    addAndMakeVisible (logo);

    presetButton.setTooltip ("Preset: esporta senza i percorsi locali dei file NAM/IR; per usarli su un altro computer ricarica i file");
    presetButton.onClick = [this] { showPresetMenu(); };
    addAndMakeVisible (presetButton);

    firstButton.setTooltip ("Pagina precedente");
    prevButton.setTooltip ("Indietro di 3 pedali");
    nextButton.setTooltip ("Avanti di 3 pedali");
    lastButton.setTooltip ("Pagina successiva");
    firstButton.onClick = [this] { scrollBy (-board->pageLength()); };
    prevButton.onClick = [this] { scrollBy (-3); };
    nextButton.onClick = [this] { scrollBy (3); };
    lastButton.onClick = [this] { scrollBy (board->pageLength()); };
    for (auto* b : { &firstButton, &prevButton, &nextButton, &lastButton }) addAndMakeVisible (b);

    pageLabel.setJustificationType (juce::Justification::centred);
    pageLabel.setColour (juce::Label::textColourId, juce::Colour (0xffd9b464));
    pageLabel.setFont (juce::Font (14.0f, juce::Font::bold));
    addAndMakeVisible (pageLabel);

    for (int i = 0; i < 4; ++i)
    {
        auto& b = viewButtons[i];
        b.setButtonText (juce::String (views[i]));
        b.setClickingTogglesState (false);
        b.setTooltip ("Mostra " + juce::String (views[i]) + " pedali alla volta");
        b.onClick = [this, i] { setView (views[i]); };
        addAndMakeVisible (b);
    }

    addButton.setTooltip ("Aggiungi uno slot (fino a 100)");
    addButton.onClick = [this] { addPedal(); };
    addAndMakeVisible (addButton);

    zoomOut.setTooltip ("Riduci (fino a 1280x760)");
    zoomIn.setTooltip ("Ingrandisci (fino a 2560x1440)");
    zoomOut.onClick = [this] { zoomStep (-1); };
    zoomIn.onClick = [this] { zoomStep (1); };
    addAndMakeVisible (zoomOut);
    addAndMakeVisible (zoomIn);
    zoomLabel.setJustificationType (juce::Justification::centred);
    zoomLabel.setColour (juce::Label::textColourId, juce::Colour (0xffb9b6ac));
    zoomLabel.setFont (juce::Font (13.0f));
    addAndMakeVisible (zoomLabel);

    infoButton.setTooltip ("Informazioni, licenza e guida");
    infoButton.onClick = [this] { showInfo (true); };
    addAndMakeVisible (infoButton);
    realButton = std::make_unique<RealModButton>();
    realButton->setToggleState (themes->realMode(), juce::dontSendNotification);
    realButton->onClick = [this] { themes->setRealMode (realButton->getToggleState()); };
    addAndMakeVisible (*realButton);

    midiButton.setClickingTogglesState (true);
    midiButton.setTooltip ("Mappatura MIDI: tocca un pomello o un footswitch e muovi il comando della pedaliera MIDI "
                           "(impostazioni e assegnazioni: \"Assegnazioni...\" nella barra della mappatura o Opzioni -> MIDI)");
    midiButton.onClick = [this] { setMidiLearn (midiButton.getToggleState()); };
    addAndMakeVisible (midiButton);

    optionsButton = std::make_unique<GearButton>();
    optionsButton->onClick = [this] { showOptions (true); };
    addAndMakeVisible (*optionsButton);

    pt::ui::Pedalboard::Callbacks bcb;
    bcb.zoom = [this] (int i) { showZoom (i); };
    bcb.scroll = [this] (int delta) { scrollBy (delta); };
    board = std::make_unique<pt::ui::Pedalboard> (processor, this, bcb);
    addAndMakeVisible (*board);

    // stato dell'interfaccia salvato nel progetto
    view = (int) processor.uiState.getProperty ("view", 3);
    if (std::find (std::begin (views), std::end (views), view) == std::end (views)) view = 3;
    first = (int) processor.uiState.getProperty ("first", 0);
    const int w = (int) processor.uiState.getProperty ("w", 1280), h = (int) processor.uiState.getProperty ("h", 760);

    processor.chain.addChangeListener (this);
    processor.midi.addChangeListener (this);
    themes->addChangeListener (this);
    applyTheme();

    setResizable (true, true);
    setResizeLimits (1280, 760, 2560, 1440);
    setSize (juce::jlimit (1280, 2560, w), juce::jlimit (760, 1440, h));
    refresh();
}

PedalTrinityEditor::~PedalTrinityEditor()
{
    saveUiState();
    processor.midi.setLearning (false);
    processor.midi.removeChangeListener (this);
    learnBar.reset();
    midiPanel.reset();
    processor.chain.removeChangeListener (this);
    themes->removeChangeListener (this);
    optionsPanel.reset();
    board.reset();
    zoomPanel.reset();
    infoPanel.reset();
    setLookAndFeel (nullptr);
}

void PedalTrinityEditor::saveUiState()
{
    processor.uiState.setProperty ("view", view, nullptr);
    processor.uiState.setProperty ("first", first, nullptr);
    processor.uiState.setProperty ("w", getWidth(), nullptr);
    processor.uiState.setProperty ("h", getHeight(), nullptr);
}

void PedalTrinityEditor::paint (juce::Graphics& g)
{
    const auto& t = themes->current();
    g.fillAll (t.window);
    auto bar = getLocalBounds().removeFromTop (toolbarH).toFloat();
    g.setGradientFill (juce::ColourGradient (t.barTop, bar.getTopLeft(), t.barBottom, bar.getBottomLeft(), false));
    g.fillRect (bar);
    g.setColour (t.accent.withAlpha (0.4f));
    g.fillRect (bar.removeFromBottom (1.5f));
    g.setColour (t.textDim);
    g.setFont (juce::Font (11.0f, juce::Font::bold));
    g.drawText (juce::String ("by ") + pt::author + "  v" + pt::versionString, 18, 30, 220, 16, juce::Justification::left);
}

void PedalTrinityEditor::applyTheme()
{
    const auto& t = themes->current();
    lookAndFeel.applyTheme (t);
    if (realButton) realButton->setToggleState (themes->realMode(), juce::dontSendNotification);
    logo.setColour (juce::Label::textColourId, t.accent);
    pageLabel.setColour (juce::Label::textColourId, t.accent);
    zoomLabel.setColour (juce::Label::textColourId, t.textDim);
    sendLookAndFeelChange();
    if (board) board->themeChanged();
    repaint();
}

juce::Rectangle<int> PedalTrinityEditor::rackArea() const
{
    return getLocalBounds().withTrimmedTop (toolbarH).reduced (8, 8);
}

void PedalTrinityEditor::resized()
{
    auto bar = getLocalBounds().removeFromTop (toolbarH).reduced (10, 6);
    // larghezze compatte: a 1280 px (minimo) tutti i tasti restano visibili
    const bool wide = getWidth() >= 1500;
    logo.setBounds (bar.getX() + 6, 2, 184, 30);
    bar.removeFromLeft (192);
    presetButton.setBounds (bar.removeFromLeft (wide ? 240 : 156).reduced (0, 3));
    bar.removeFromLeft (wide ? 10 : 6);
    realButton->setBounds (bar.removeFromLeft (wide ? 118 : 96).reduced (0, 3));
    bar.removeFromLeft (wide ? 10 : 6);

    const int gap = wide ? 10 : 6;
    auto right = bar.removeFromRight (juce::jmin (bar.getWidth() - 290, wide ? 610 : 530));
    infoButton.setBounds (right.removeFromRight (60).reduced (0, 3));
    right.removeFromRight (6);
    optionsButton->setBounds (right.removeFromRight (36).reduced (0, 3));
    right.removeFromRight (6);
    midiButton.setBounds (right.removeFromRight (wide ? 54 : 46).reduced (0, 3));
    right.removeFromRight (gap);
    zoomIn.setBounds (right.removeFromRight (30).reduced (0, 3));
    zoomLabel.setBounds (right.removeFromRight (wide ? 90 : 64));
    zoomOut.setBounds (right.removeFromRight (30).reduced (0, 3));
    right.removeFromRight (gap);
    addButton.setBounds (right.removeFromRight (wide ? 96 : 80).reduced (0, 3));
    right.removeFromRight (gap);
    for (int i = 3; i >= 0; --i) viewButtons[i].setBounds (right.removeFromRight (34).reduced (1, 3));

    bar.removeFromRight (6);
    firstButton.setBounds (bar.removeFromLeft (36).reduced (0, 3));
    prevButton.setBounds (bar.removeFromLeft (32).reduced (0, 3));
    lastButton.setBounds (bar.removeFromRight (36).reduced (0, 3));
    nextButton.setBounds (bar.removeFromRight (32).reduced (0, 3));
    pageLabel.setBounds (bar);

    zoomLabel.setText (juce::String (getWidth()) + " x " + juce::String (getHeight()), juce::dontSendNotification);
    board->setBounds (rackArea());
    board->setPage (first, view);
    if (zoomPanel) zoomPanel->setBounds (getLocalBounds());
    if (infoPanel) infoPanel->setBounds (getLocalBounds());
    if (optionsPanel) optionsPanel->setBounds (getLocalBounds());
    if (midiPanel) midiPanel->setBounds (getLocalBounds());
    if (learnBar) learnBar->setBounds (rackArea().removeFromTop (44).reduced (60, 0));
    saveUiState();
}

//==============================================================================
void PedalTrinityEditor::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &themes.getObject()) applyTheme();
    else if (source == &processor.midi)
    {
        // la mappatura puo' essere chiusa anche da altrove (pannello MIDI)
        if (midiButton.getToggleState() != processor.midi.isLearning()) setMidiLearn (processor.midi.isLearning());
    }
    else refresh();
}

void PedalTrinityEditor::refresh()
{
    const int n = processor.chain.size();
    const int total = board->contentLength(), per = board->pageLength();
    first = juce::jlimit (0, juce::jmax (0, total - 1), first);
    board->setPage (first, view);

    const bool dual = board->isDual();
    const int last = juce::jmin (total, first + per);
    const juce::String unit = dual ? "Colonne " : "Slot ";
    pageLabel.setText (n == 0 ? juce::String ("Nessun pedale: premi + Pedale")
                              : unit + juce::String (first + 1) + " - " + juce::String (last) + " di " + juce::String (total)
                                    + "   (" + juce::String (n) + "/100)",
                       juce::dontSendNotification);
    // i tasti di pagina compaiono solo quando la catena non sta tutta nella vista
    const bool paged = total > per || first > 0;
    for (auto* b : { &firstButton, &prevButton, &nextButton, &lastButton }) b->setVisible (paged);
    prevButton.setEnabled (first > 0);
    firstButton.setEnabled (first > 0);
    nextButton.setEnabled (first + per < total);
    lastButton.setEnabled (first + per < total);
    addButton.setEnabled (processor.chain.canAdd());
    for (int i = 0; i < 4; ++i) viewButtons[i].setToggleState (views[i] == view, juce::dontSendNotification);
    presetButton.setButtonText ("Preset: " + processor.presets.currentName());
    if (zoomPanel) zoomPanel->show (juce::jlimit (0, juce::jmax (0, n - 1), zoomPanel->currentIndex()));
    saveUiState();
}

void PedalTrinityEditor::setView (int v)
{
    view = v;
    board->setPage (first, view);
    const int total = board->contentLength(), per = board->pageLength();
    if (first + per > total) first = juce::jmax (0, total - per);
    refresh();
}

void PedalTrinityEditor::scrollBy (int delta)
{
    const int total = board->contentLength(), per = board->pageLength();
    first = juce::jlimit (0, juce::jmax (0, total - 1), first + delta);
    if (delta > 0 && first + per > total) first = juce::jmax (0, total - per);
    refresh();
}

void PedalTrinityEditor::addPedal()
{
    // in DUAL il tasto aggiunge alla corsia A; la corsia B ha il suo "+" sulla pedaliera
    const int idx = processor.chain.insert (-1, {}, 0);
    if (idx < 0) return;
    const int unit = board->unitOfSlot (idx), per = board->pageLength();
    if (unit >= first + per || unit < first) first = juce::jmax (0, unit - per + 1);
    refresh();
}

void PedalTrinityEditor::zoomStep (int dir)
{
    const auto& s = sizePresets();
    const int w = getWidth();
    juce::Point<int> target = dir > 0 ? s.getLast() : s.getFirst();
    if (dir > 0) { for (auto& p : s) if (p.x > w + 4) { target = p; break; } }
    else         { for (int i = s.size() - 1; i >= 0; --i) if (s[i].x < w - 4) { target = s[i]; break; } }
    setSize (target.x, target.y);
}

//==============================================================================
void PedalTrinityEditor::showPresetMenu()
{
    juce::PopupMenu m, factory, user;
    const auto names = PresetManager::factoryNames();
    for (int i = 0; i < names.size(); ++i) factory.addItem (1000 + i, names[i]);
    const auto files = processor.presets.userPresets();
    for (int i = 0; i < files.size(); ++i) user.addItem (2000 + i, files[i].getFileNameWithoutExtension());
    if (files.isEmpty()) user.addItem (-1, "(nessun preset salvato)", false);

    m.addSectionHeader ("Preset");
    m.addSubMenu ("Fabbrica", factory);
    m.addSubMenu ("I miei preset", user);
    m.addSeparator();
    m.addItem (1, "Salva...");
    m.addItem (2, "Esporta su file...");
    m.addItem (3, "Importa da file...");
    m.addItem (4, "Elimina un preset...", ! files.isEmpty());
    m.addSeparator();
    m.addItem (5, "Nuova pedaliera vuota");
    m.addItem (6, "Apri la cartella dei preset");

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetButton), [this, files] (int r)
    {
        auto& pm = processor.presets;
        if (r >= 1000 && r < 2000) pm.loadFactory (r - 1000);
        else if (r >= 2000) pm.load (files[r - 2000]);
        else if (r == 1)
        {
            auto* w = new juce::AlertWindow ("Salva preset", "Nome del preset:", juce::MessageBoxIconType::NoIcon, this);
            w->addTextEditor ("name", pm.currentName());
            w->addButton ("Salva", 1, juce::KeyPress (juce::KeyPress::returnKey));
            w->addButton ("Annulla", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int res)
            {
                if (res == 1) processor.presets.save (w->getTextEditorContents ("name"));
                refresh();
            }), true);
            return;
        }
        else if (r == 2 || r == 3)
        {
            const bool save = r == 2;
            chooser = std::make_unique<juce::FileChooser> (save ? "Esporta preset (senza percorsi locali NAM/IR)" : "Importa preset",
                                                           PresetManager::folder(), "*.ptpreset");
            const auto flags = save ? (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting)
                                    : juce::FileBrowserComponent::openMode;
            chooser->launchAsync (flags | juce::FileBrowserComponent::canSelectFiles, [this, save] (const juce::FileChooser& fc)
            {
                const auto f = fc.getResult();
                if (f == juce::File()) return;
                if (save) processor.presets.saveTo (f.withFileExtension ("ptpreset"));
                else processor.presets.load (f);
                refresh();
            });
            return;
        }
        else if (r == 4)
        {
            juce::PopupMenu del;
            for (int i = 0; i < files.size(); ++i) del.addItem (i + 1, files[i].getFileNameWithoutExtension());
            del.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetButton), [this, files] (int d)
            {
                if (d > 0) processor.presets.remove (files[d - 1]);
                refresh();
            });
            return;
        }
        else if (r == 5) { pm.initialise(); first = 0; }
        else if (r == 6) PresetManager::folder().startAsProcess();
        refresh();
    });
}

//==============================================================================
void PedalTrinityEditor::showZoom (int slotIndex)
{
    zoomPanel = std::make_unique<ZoomPanel> (processor.chain, slotIndex, this);
    const juce::Component::SafePointer<PedalTrinityEditor> safeThis (this);
    zoomPanel->onClose = [safeThis]
    {
        juce::MessageManager::callAsync ([safeThis] { if (safeThis != nullptr) safeThis->zoomPanel.reset(); });
    };
    zoomPanel->setBounds (getLocalBounds());
    addAndMakeVisible (*zoomPanel);
    zoomPanel->grabKeyboardFocus();
}

void PedalTrinityEditor::showInfo (bool shouldShow)
{
    if (! shouldShow) { infoPanel.reset(); return; }
    infoPanel = std::make_unique<InfoPanel>();
    const juce::Component::SafePointer<PedalTrinityEditor> safeThis (this);
    infoPanel->onClose = [safeThis]
    {
        juce::MessageManager::callAsync ([safeThis] { if (safeThis != nullptr) safeThis->showInfo (false); });
    };
    infoPanel->setBounds (getLocalBounds());
    addAndMakeVisible (*infoPanel);
    infoPanel->grabKeyboardFocus();
}

void PedalTrinityEditor::showMidi (bool shouldShow)
{
    if (! shouldShow) { midiPanel.reset(); return; }
    midiPanel = std::make_unique<pt::ui::MidiPanel> (processor.midi);
    const juce::Component::SafePointer<PedalTrinityEditor> safeThis (this);
    midiPanel->onClose = [safeThis]
    {
        juce::MessageManager::callAsync ([safeThis] { if (safeThis != nullptr) safeThis->showMidi (false); });
    };
    midiPanel->onStartTouchLearn = [safeThis]
    {
        juce::MessageManager::callAsync ([safeThis] { if (safeThis != nullptr) { safeThis->showMidi (false); safeThis->setMidiLearn (true); } });
    };
    midiPanel->setBounds (getLocalBounds());
    addAndMakeVisible (*midiPanel);
    midiPanel->grabKeyboardFocus();
}

void PedalTrinityEditor::setMidiLearn (bool on)
{
    midiButton.setToggleState (on, juce::dontSendNotification);
    if (processor.midi.isLearning() != on) processor.midi.setLearning (on);
    if (! on) { learnBar.reset(); return; }
    if (learnBar == nullptr)
    {
        learnBar = std::make_unique<pt::ui::MidiLearnBar> (processor.midi);
        const juce::Component::SafePointer<PedalTrinityEditor> safeThis (this);
        learnBar->onDone = [safeThis] { juce::MessageManager::callAsync ([safeThis] { if (safeThis != nullptr) safeThis->setMidiLearn (false); }); };
        learnBar->onShowList = [safeThis] { juce::MessageManager::callAsync ([safeThis] { if (safeThis != nullptr) safeThis->showMidi (true); }); };
        addAndMakeVisible (*learnBar);
    }
    learnBar->setBounds (rackArea().removeFromTop (44).reduced (60, 0));
    learnBar->toFront (false);
}

void PedalTrinityEditor::showOptions (bool shouldShow)
{
    if (! shouldShow) { optionsPanel.reset(); return; }
    optionsPanel = std::make_unique<pt::ui::OptionsPanel>();
    const juce::Component::SafePointer<PedalTrinityEditor> safeThis (this);
    optionsPanel->onMidi = [safeThis]
    {
        juce::MessageManager::callAsync ([safeThis] { if (safeThis != nullptr) { safeThis->showOptions (false); safeThis->showMidi (true); } });
    };
    optionsPanel->onClose = [safeThis]
    {
        juce::MessageManager::callAsync ([safeThis] { if (safeThis != nullptr) safeThis->showOptions (false); });
    };
    optionsPanel->setBounds (getLocalBounds());
    addAndMakeVisible (*optionsPanel);
    optionsPanel->grabKeyboardFocus();
}
