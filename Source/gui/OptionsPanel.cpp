/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)
*/

#include "OptionsPanel.h"
#include "Assets.h"
#include "Cables.h"
#include "../engine/Model.h"
#include <juce_gui_extra/juce_gui_extra.h>

namespace pt::ui
{
    /** Anteprima di un tema: la sua pedana con due pedali collegati da un cavo. */
    class OptionsPanel::ThemeTile : public juce::Button
    {
    public:
        ThemeTile (int i) : juce::Button (allThemes()[(size_t) i].name), index (i)
        {
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
            setTooltip (allThemes()[(size_t) i].description);
            onClick = [this] { themes->select (index); };
        }

        void resized() override { preview = {}; }

        void paintButton (juce::Graphics& g, bool over, bool) override
        {
            const auto& t = allThemes()[(size_t) index];
            const auto& cur = themes->current();
            const bool selected = themes->currentIndex() == index;
            auto r = getLocalBounds().toFloat().reduced (3.0f);
            auto img = r.removeFromTop (r.getHeight() * 0.64f);
            if (! preview.isValid()) renderPreview (t, img.getSmallestIntegerContainer());
            g.drawImage (preview, img);

            // nome e descrizione su fondo scuro (leggibili qualunque sia la pedana)
            g.setColour (cur.panelBottom);
            g.fillRoundedRectangle (r.withTrimmedTop (-4.0f), 6.0f);
            g.setColour (selected ? cur.accent : cur.text);
            g.setFont (juce::Font (15.0f, juce::Font::bold));
            g.drawText (t.name, r.removeFromTop (22.0f).reduced (8.0f, 0.0f), juce::Justification::centredLeft);
            g.setColour (cur.textDim);
            g.setFont (juce::Font (12.0f));
            g.drawFittedText (t.description, r.reduced (8.0f, 2.0f).toNearestInt(), juce::Justification::topLeft, 3);

            auto frame = getLocalBounds().toFloat().reduced (1.5f);
            g.setColour (selected ? cur.accent : over ? cur.text.withAlpha (0.5f) : cur.text.withAlpha (0.15f));
            g.drawRoundedRectangle (frame, 8.0f, selected ? 3.0f : 1.2f);
            if (selected)
            {
                // spunta del tema in uso
                const auto badge = juce::Rectangle<float> (frame.getRight() - 30.0f, frame.getY() + 8.0f, 22.0f, 22.0f);
                g.setColour (cur.accent);
                g.fillEllipse (badge);
                juce::Path tick;
                tick.startNewSubPath (badge.getX() + 6.0f, badge.getCentreY());
                tick.lineTo (badge.getCentreX() - 1.0f, badge.getBottom() - 6.0f);
                tick.lineTo (badge.getRight() - 5.0f, badge.getY() + 6.0f);
                g.setColour (cur.panelBottom);
                g.strokePath (tick, juce::PathStrokeType (2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
        }

    private:
        void renderPreview (const Theme& t, juce::Rectangle<int> area)
        {
            const float sc = 2.0f;               // anteprima nitida anche su schermi HiDPI
            preview = juce::Image (juce::Image::ARGB, juce::jmax (1, (int) (area.getWidth() * sc)), juce::jmax (1, (int) (area.getHeight() * sc)), true);
            juce::Graphics g (preview);
            g.addTransform (juce::AffineTransform::scale (sc));
            const auto a = area.withZeroOrigin().toFloat();
            juce::Path clip;
            clip.addRoundedRectangle (a, 7.0f);
            g.reduceClipRegion (clip);
            paintBoard (g, t, a, 0.45f);
            g.setOpacity (1.0f);                // le immagini usano l'opacita' del colore corrente
            // due pedali veri appoggiati sulla pedana, collegati da un cavo
            const char* ids[] = { "ds1", "ce2" };
            juce::Point<float> jacks[2][2];
            for (int k = 0; k < 2; ++k)
            {
                const auto* d = engine::findModel (ids[k]);
                if (d == nullptr || d->image == nullptr) continue;
                const auto img = Assets::pedalImage (d->image);
                const float h = a.getHeight() * 0.86f, w = h * d->imageW / d->imageH;
                const auto pr = juce::Rectangle<float> (w, h).withCentre ({ a.getWidth() * (k == 0 ? 0.3f : 0.7f), a.getCentreY() + 2.0f });
                g.drawImage (img, pr);
                const float s = w / d->imageW;
                jacks[k][0] = { pr.getX() + d->jackInX * s, pr.getY() + d->jackYA * s };
                jacks[k][1] = { pr.getX() + d->jackOutX * s, pr.getY() + d->jackYA * s };
            }
            if (themes->showCables())
            {
                Cable c { jacks[0][1], jacks[1][0], 1.0f, -1.0f, a.getHeight() * 0.86f / 550.0f, 0, false };
                const auto& col = themes->cableFor (0);
                cables::drawCable (g, c, col.body, col.sheen);
                cables::drawPlug (g, c.a, 1.0f, c.scale, 0);
                cables::drawPlug (g, c.b, -1.0f, c.scale, 0);
            }
        }

        int index;
        juce::Image preview;
        juce::SharedResourcePointer<ThemeManager> themes;
    };

    //==============================================================================
    /** Griglia delle anteprime dei temi (scorre in verticale quando i temi non ci stanno). */
    class OptionsPanel::TileGrid : public juce::Component
    {
    public:
        TileGrid()
        {
            for (int i = 0; i < (int) allThemes().size(); ++i)
                addAndMakeVisible (tiles.add (new ThemeTile (i)));
        }
        /** Altezza necessaria per una larghezza data (4 colonne, o 3 se lo spazio e' poco). */
        int heightFor (int width) const
        {
            const int cols = width >= 760 ? 4 : 3;
            const int rows = (tiles.size() + cols - 1) / cols;
            return rows * tileHeight (width, cols);
        }
        void resized() override
        {
            const int cols = getWidth() >= 760 ? 4 : 3;
            const int tw = getWidth() / cols, th = tileHeight (getWidth(), cols);
            for (int i = 0; i < tiles.size(); ++i)
                tiles[i]->setBounds ((i % cols) * tw + 5, (i / cols) * th + 5, tw - 10, th - 10);
        }
        void refreshPreviews() { for (auto* t : tiles) t->resized(); repaint(); }

    private:
        static int tileHeight (int width, int cols) { return juce::jlimit (130, 190, (int) ((float) width / (float) cols * 0.66f)); }
        juce::OwnedArray<ThemeTile> tiles;
    };

    namespace
    {
        /** Selettore del colore della pedana: applica il colore mentre lo si sceglie. */
        class TintSelector : public juce::ColourSelector, private juce::ChangeListener
        {
        public:
            explicit TintSelector (juce::Colour start)
                : juce::ColourSelector (juce::ColourSelector::showColourspace | juce::ColourSelector::showSliders | juce::ColourSelector::showColourAtTop)
            {
                setCurrentColour (start, juce::dontSendNotification);
                addChangeListener (this);
                setSize (320, 300);
            }
            ~TintSelector() override { removeChangeListener (this); }

        private:
            void changeListenerCallback (juce::ChangeBroadcaster*) override { themes->setBoardTint (getCurrentColour().withAlpha (1.0f)); }
            juce::SharedResourcePointer<ThemeManager> themes;
        };
    }

    //==============================================================================
    OptionsPanel::OptionsPanel()
    {
        setWantsKeyboardFocus (true);
        for (auto* l : { &title, &themeLabel, &boardLabel, &cableLabel, &colourLabel, &cableInfo, &realLabel, &realInfo, &listLabel, &midiLabel, &midiInfo })
        {
            l->setInterceptsMouseClicks (false, false);
            addAndMakeVisible (l);
        }
        title.setText ("Opzioni", juce::dontSendNotification);
        title.setFont (juce::Font (26.0f, juce::Font::bold));
        themeLabel.setText ("Tema della pedaliera (" + juce::String ((int) allThemes().size()) + ")", juce::dontSendNotification);
        boardLabel.setText ("Pedana", juce::dontSendNotification);
        cableLabel.setText ("Cavi jack", juce::dontSendNotification);
        colourLabel.setText ("Colore", juce::dontSendNotification);
        listLabel.setText ("Lista", juce::dontSendNotification);
        for (auto* l : { &themeLabel, &boardLabel, &cableLabel }) l->setFont (juce::Font (16.0f, juce::Font::bold));
        for (auto* l : { &colourLabel, &listLabel }) l->setFont (juce::Font (14.0f));
        cableInfo.setText ("Trascina una spina su un altro pedale per continuare la catena; nel vuoto stacca il pedale.",
                           juce::dontSendNotification);
        cableInfo.setFont (juce::Font (12.5f));
        cableInfo.setMinimumHorizontalScale (0.75f);

        grid = std::make_unique<TileGrid>();
        tileView.setViewedComponent (grid.get(), false);
        tileView.setScrollBarsShown (true, false);
        tileView.setScrollBarThickness (10);
        addAndMakeVisible (tileView);

        // pedana: materiale di un tema, immagine propria e colore
        boardSource.addItem ("Quella del tema", 1);
        boardSource.addSectionHeading ("Materiale di un altro tema");
        for (int i = 0; i < (int) allThemes().size(); ++i)
            boardSource.addItem (allThemes()[(size_t) i].name, i + 2);
        boardSource.addSeparator();
        boardSource.addItem ("Immagine personale", 1000);
        boardSource.setTooltip ("Materiale della pedana: quello del tema scelto, quello di un altro tema o una tua immagine");
        boardSource.onChange = [this]
        {
            const int id = boardSource.getSelectedId();
            if (id == 1000)
            {
                if (themes->boardImage().isValid()) themes->setBoardSource ("image");
                else boardImage.triggerClick();
            }
            else if (id >= 2) themes->setBoardSource (allThemes()[(size_t) (id - 2)].id);
            else themes->setBoardSource ({});
            syncControls();
        };
        boardImage.setTooltip ("Scegli una foto o un'immagine (.jpg/.png, massimo 25 MB e 8000 x 8000 pixel) come pedana");
        boardImage.onClick = [this]
        {
            chooser = std::make_unique<juce::FileChooser> ("Immagine della pedana", themes->boardImageFile().getParentDirectory(), "*.jpg;*.jpeg;*.png");
            juce::Component::SafePointer<OptionsPanel> safe (this);
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe] (const juce::FileChooser& fc)
            {
                if (safe == nullptr) return;
                const auto f = fc.getResult();
                if (f != juce::File())
                    if (const auto err = safe->themes->setBoardImage (f); err.isNotEmpty())
                        safe->showError ("Immagine della pedana", f.getFileName() + ": " + err);
                safe->syncControls();
            });
        };
        boardColour.setTooltip ("Colore personalizzato del materiale della pedana");
        boardColour.onClick = [this] { chooseBoardColour(); };
        boardReset.setTooltip ("Torna ai colori originali del materiale");
        boardReset.onClick = [this] { themes->setBoardTint (juce::Colours::transparentBlack); };
        for (auto* c : std::initializer_list<juce::Component*> { &boardSource, &boardImage, &boardColour, &boardReset })
            addAndMakeVisible (c);

        showCables.onClick = [this] { themes->setShowCables (showCables.getToggleState()); };
        showCables.setTooltip ("Disegna i cavi tra i pedali, dal meter INPUT e verso il meter OUTPUT");
        addAndMakeVisible (showCables);
        for (int i = 0; i < (int) cableColours().size(); ++i)
            cableColour.addItem (cableColours()[(size_t) i].name, i + 1);
        cableColour.setTooltip ("Colore dei cavi (Multicolore: ogni cavo ha un colore diverso)");
        cableColour.onChange = [this] { themes->setCableColour (cableColour.getSelectedId() - 1); };
        addAndMakeVisible (cableColour);

        // REAL MOD e liste di pedali
        realLabel.setText ("REAL MOD", juce::dontSendNotification);
        realLabel.setFont (juce::Font (16.0f, juce::Font::bold));
        realInfo.setFont (juce::Font (12.5f));
        realInfo.setMinimumHorizontalScale (0.75f);
        realMode.onClick = [this] { themes->setRealMode (realMode.getToggleState()); };
        realMode.setTooltip ("Mostra le repliche dei pedali reali con i loro nomi (il suono non cambia)");
        realList.setTooltip ("Lista di pedali REAL MOD: la classica (repliche BOSS) o una tua lista personalizzata, ognuna nella sua cartella");
        realList.onChange = [this]
        {
            const int id = realList.getSelectedId();
            themes->setRealList (id >= 2 && id - 2 < listNames.size() ? listNames[id - 2] : juce::String());
            photos->reload();
            themes->sendChangeMessage();
        };
        newList.setTooltip ("Crea una lista vuota: i pedali mostrano le repliche finche' non metti le tue foto nella sua cartella");
        newList.onClick = [this]
        {
            askName ("Nuova lista REAL MOD", "Nome della lista (diventa il nome della cartella):", {}, [this] (const juce::String& n)
            {
                if (const auto err = RealPhotos::createList (n); err.isNotEmpty()) { showError ("Nuova lista", err); return; }
                themes->setRealList (n);
                photos->reload();
                fillLists();
            });
        };
        copyList.setTooltip ("Crea una nuova lista con una copia di foto, nomi e allineamenti della lista in uso");
        copyList.onClick = [this]
        {
            const auto cur = themes->realList();
            askName ("Duplica la lista", "Nome della copia:", (cur.isEmpty() ? juce::String ("Classica") : cur) + " copia", [this, cur] (const juce::String& n)
            {
                if (const auto err = RealPhotos::createList (n, &cur); err.isNotEmpty()) { showError ("Duplica la lista", err); return; }
                themes->setRealList (n);
                photos->reload();
                fillLists();
            });
        };
        renameList.setTooltip ("Rinomina la lista personalizzata in uso (e la sua cartella)");
        renameList.onClick = [this]
        {
            const auto cur = themes->realList();
            if (cur.isEmpty()) return;
            askName ("Rinomina la lista", "Nuovo nome:", cur, [this, cur] (const juce::String& n)
            {
                if (n == cur) return;
                if (const auto err = RealPhotos::renameList (cur, n); err.isNotEmpty()) { showError ("Rinomina la lista", err); return; }
                themes->setRealList (n);
                photos->reload();
                fillLists();
            });
        };
        deleteList.setTooltip ("Sposta nel cestino la cartella della lista personalizzata in uso (si puo' recuperare)");
        deleteList.onClick = [this]
        {
            const auto cur = themes->realList();
            if (cur.isEmpty()) return;
            juce::Component::SafePointer<OptionsPanel> safe (this);
            juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                              .withIconType (juce::MessageBoxIconType::QuestionIcon)
                                              .withTitle ("Elimina la lista")
                                              .withMessage ("Spostare nel cestino la lista \"" + cur + "\" con tutte le sue foto?\n"
                                                            + RealPhotos::folderFor (cur).getFullPathName())
                                              .withButton ("Sposta nel cestino")
                                              .withButton ("Annulla")
                                              .withAssociatedComponent (this),
                                          [safe, cur] (int r)
            {
                if (safe == nullptr || r != 1) return;
                if (const auto err = RealPhotos::deleteList (cur); err.isNotEmpty()) { safe->showError ("Elimina la lista", err); return; }
                safe->themes->setRealList ({});
                safe->photos->reload();
                safe->fillLists();
            });
        };
        openPhotos.onClick = [] { RealPhotos::folder().startAsProcess(); };
        reloadPhotos.setTooltip ("Rilegge le foto dopo averle aggiunte o cambiate nella cartella");
        reloadPhotos.onClick = [this] { photos->reload(); themes->sendChangeMessage(); };
        for (auto* b : std::initializer_list<juce::Component*> { &realMode, &realList, &newList, &copyList, &renameList, &deleteList, &openPhotos, &reloadPhotos })
            addAndMakeVisible (b);
        midiLabel.setText ("MIDI", juce::dontSendNotification);
        midiLabel.setFont (juce::Font (16.0f, juce::Font::bold));
        midiInfo.setText ("Pedaliera MIDI esterna: dispositivi MIDI IN/OUT, assegnazioni delle funzioni dei pedali, ritorno dello stato.",
                          juce::dontSendNotification);
        midiInfo.setFont (juce::Font (12.5f));
        midiInfo.setMinimumHorizontalScale (0.75f);
        midiButton.onClick = [this] { if (onMidi) onMidi(); };
        addAndMakeVisible (midiButton);
        closeButton.onClick = [this] { if (onClose) onClose(); };
        addAndMakeVisible (closeButton);

        themes->addChangeListener (this);
        fillLists();
    }

    OptionsPanel::~OptionsPanel() { themes->removeChangeListener (this); }

    void OptionsPanel::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        grid->refreshPreviews();                    // le anteprime mostrano il nuovo cavo
        syncControls();
        repaint();
    }

    void OptionsPanel::fillLists()
    {
        listNames = RealPhotos::lists();
        realList.clear (juce::dontSendNotification);
        realList.addItem ("Classica (repliche BOSS)", 1);
        for (int i = 0; i < listNames.size(); ++i)
            realList.addItem (listNames[i], i + 2);
        syncControls();
    }

    void OptionsPanel::syncControls()
    {
        const auto& t = themes->current();
        title.setColour (juce::Label::textColourId, t.accent);
        for (auto* l : { &themeLabel, &boardLabel, &cableLabel, &realLabel, &midiLabel }) l->setColour (juce::Label::textColourId, t.text);
        for (auto* l : { &colourLabel, &cableInfo, &realInfo, &listLabel, &midiInfo }) l->setColour (juce::Label::textColourId, t.textDim);
        realMode.setToggleState (themes->realMode(), juce::dontSendNotification);
        showCables.setToggleState (themes->showCables(), juce::dontSendNotification);
        cableColour.setSelectedId (themes->cableColourIndex() + 1, juce::dontSendNotification);
        cableColour.setEnabled (themes->showCables());

        const auto src = themes->boardSource();
        int sid = 1;
        if (src == "image") sid = themes->boardImage().isValid() ? 1000 : 1;
        else for (int i = 0; i < (int) allThemes().size(); ++i) if (allThemes()[(size_t) i].id == src) sid = i + 2;
        boardSource.setSelectedId (sid, juce::dontSendNotification);
        boardColour.setEnabled (sid != 1000);
        boardReset.setEnabled (sid != 1000 && ! themes->boardTint().isTransparent());

        const auto cur = themes->realList();
        const int idx = listNames.indexOf (cur);
        realList.setSelectedId (cur.isEmpty() || idx < 0 ? 1 : idx + 2, juce::dontSendNotification);
        renameList.setEnabled (cur.isNotEmpty());
        deleteList.setEnabled (cur.isNotEmpty());
        const auto dir = RealPhotos::folder();
        openPhotos.setTooltip (dir.getFullPathName());
        realInfo.setText (juce::String ("Foto tue, dall'alto: <id>.jpg o <SIGLA>.jpg (es. DS-1.jpg) nella cartella della lista ")
                              + "(elenco in ELENCO_FOTO.txt); i pedali senza foto mostrano la replica. Non fanno parte del programma.",
                          juce::dontSendNotification);
    }

    void OptionsPanel::askName (const juce::String& heading, const juce::String& message, const juce::String& initial,
                                std::function<void (const juce::String&)> done)
    {
        auto* w = new juce::AlertWindow (heading, message, juce::MessageBoxIconType::NoIcon, this);
        w->addTextEditor ("name", initial);
        w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("Annulla", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<OptionsPanel> safe (this);
        w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w, done] (int r)
        {
            if (safe == nullptr || r != 1) return;
            done (w->getTextEditorContents ("name").trim());
        }), true);
    }

    void OptionsPanel::showError (const juce::String& heading, const juce::String& message)
    {
        juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                          .withIconType (juce::MessageBoxIconType::WarningIcon)
                                          .withTitle (heading)
                                          .withMessage (message)
                                          .withButton ("OK")
                                          .withAssociatedComponent (this),
                                      nullptr);
    }

    void OptionsPanel::chooseBoardColour()
    {
        const auto start = themes->boardTint().isTransparent() ? themes->boardTheme().boardBase : themes->boardTint();
        juce::CallOutBox::launchAsynchronously (std::make_unique<TintSelector> (start), boardColour.getScreenBounds(), nullptr);
    }

    juce::Rectangle<int> OptionsPanel::card() const
    {
        return getLocalBounds().withSizeKeepingCentre (juce::jmin (1040, getWidth() - 40), juce::jmin (780, getHeight() - 20));
    }

    void OptionsPanel::paint (juce::Graphics& g)
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
        for (auto* l : { &boardLabel, &cableLabel, &realLabel, &midiLabel })
            g.drawHorizontalLine (l->getY() - 6, (float) c.getX() + 24.0f, c.getRight() - 24.0f);
    }

    void OptionsPanel::resized()
    {
        auto c = card().reduced (24, 16);
        title.setBounds (c.removeFromTop (36));
        themeLabel.setBounds (c.removeFromTop (24));
        c.removeFromTop (4);

        auto bottom = c.removeFromBottom (34);
        closeButton.setBounds (bottom.removeFromRight (120));
        c.removeFromBottom (8);

        // MIDI
        auto midiRow = c.removeFromBottom (30);
        c.removeFromBottom (2);
        midiLabel.setBounds (c.removeFromBottom (24));
        c.removeFromBottom (10);
        midiButton.setBounds (midiRow.removeFromLeft (300).reduced (0, 1));
        midiRow.removeFromLeft (14);
        midiInfo.setBounds (midiRow);

        // REAL MOD: interruttore e lista, poi i comandi delle liste e la nota
        auto realButtons = c.removeFromBottom (30);
        c.removeFromBottom (4);
        auto realRow = c.removeFromBottom (30);
        realInfo.setBounds (realButtons.removeFromRight (juce::jmax (0, realButtons.getWidth() - 650)));
        for (auto* b : { &newList, &copyList, &renameList, &deleteList, &openPhotos, &reloadPhotos })
        {
            b->setBounds (realButtons.removeFromLeft (b == &openPhotos || b == &reloadPhotos ? 112 : 96).reduced (0, 1));
            realButtons.removeFromLeft (6);
        }
        c.removeFromBottom (2);
        realLabel.setBounds (c.removeFromBottom (24));
        c.removeFromBottom (10);
        realMode.setBounds (realRow.removeFromLeft (230));
        realRow.removeFromLeft (10);
        listLabel.setBounds (realRow.removeFromLeft (44));
        realList.setBounds (realRow.removeFromLeft (300).reduced (0, 2));
        if (realInfo.getWidth() < 200)                       // finestra stretta: la nota va accanto alla lista
            realInfo.setBounds (realRow.reduced (10, 0));

        // cavi
        auto cablesRow = c.removeFromBottom (32);
        c.removeFromBottom (2);
        cableLabel.setBounds (c.removeFromBottom (24));
        c.removeFromBottom (10);
        showCables.setBounds (cablesRow.removeFromLeft (190));
        colourLabel.setBounds (cablesRow.removeFromLeft (56));
        cableColour.setBounds (cablesRow.removeFromLeft (190).reduced (0, 2));
        cablesRow.removeFromLeft (14);
        cableInfo.setBounds (cablesRow);

        // pedana
        auto boardRow = c.removeFromBottom (32);
        c.removeFromBottom (2);
        boardLabel.setBounds (c.removeFromBottom (24));
        c.removeFromBottom (10);
        boardSource.setBounds (boardRow.removeFromLeft (260).reduced (0, 2));
        boardRow.removeFromLeft (8);
        for (auto* b : { &boardImage, &boardColour, &boardReset })
        {
            b->setBounds (boardRow.removeFromLeft (b == &boardReset ? 150 : 110).reduced (0, 2));
            boardRow.removeFromLeft (6);
        }

        // temi (scorrono se non ci stanno)
        tileView.setBounds (c);
        const int w = c.getWidth() - tileView.getScrollBarThickness() - 2;
        grid->setSize (w, grid->heightFor (w));
    }

    void OptionsPanel::mouseUp (const juce::MouseEvent& e)
    {
        if (! card().contains (e.getPosition()) && onClose) onClose();
    }

    bool OptionsPanel::keyPressed (const juce::KeyPress& k)
    {
        if (k == juce::KeyPress::escapeKey && onClose) { onClose(); return true; }
        return false;
    }
}
