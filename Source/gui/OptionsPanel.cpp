/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)
*/

#include "OptionsPanel.h"
#include "Assets.h"
#include "Cables.h"
#include "../engine/Model.h"

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
                cables::drawCable (g, c, themes->cable().body, themes->cable().sheen);
                cables::drawPlug (g, c.a, 1.0f, c.scale, 0);
                cables::drawPlug (g, c.b, -1.0f, c.scale, 0);
            }
        }

        int index;
        juce::Image preview;
        juce::SharedResourcePointer<ThemeManager> themes;
    };

    //==============================================================================
    OptionsPanel::OptionsPanel()
    {
        setWantsKeyboardFocus (true);
        for (auto* l : { &title, &themeLabel, &cableLabel, &colourLabel })
        {
            l->setInterceptsMouseClicks (false, false);
            addAndMakeVisible (l);
        }
        title.setText ("Opzioni", juce::dontSendNotification);
        title.setFont (juce::Font (26.0f, juce::Font::bold));
        themeLabel.setText ("Tema della pedaliera", juce::dontSendNotification);
        cableLabel.setText ("Cavi jack", juce::dontSendNotification);
        colourLabel.setText ("Colore dei cavi", juce::dontSendNotification);
        for (auto* l : { &themeLabel, &cableLabel }) l->setFont (juce::Font (16.0f, juce::Font::bold));
        colourLabel.setFont (juce::Font (14.0f));

        for (int i = 0; i < (int) allThemes().size(); ++i)
            addAndMakeVisible (tiles.add (new ThemeTile (i)));

        showCables.onClick = [this] { themes->setShowCables (showCables.getToggleState()); };
        showCables.setTooltip ("Disegna i cavi tra i pedali, dal meter INPUT e verso il meter OUTPUT");
        addAndMakeVisible (showCables);
        for (int i = 0; i < (int) cableColours().size(); ++i)
            cableColour.addItem (cableColours()[(size_t) i].name, i + 1);
        cableColour.onChange = [this] { themes->setCableColour (cableColour.getSelectedId() - 1); };
        addAndMakeVisible (cableColour);
        closeButton.onClick = [this] { if (onClose) onClose(); };
        addAndMakeVisible (closeButton);

        themes->addChangeListener (this);
        syncControls();
    }

    OptionsPanel::~OptionsPanel() { themes->removeChangeListener (this); }

    void OptionsPanel::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        for (auto* t : tiles) t->resized();         // le anteprime mostrano il nuovo cavo
        syncControls();
        repaint();
    }

    void OptionsPanel::syncControls()
    {
        const auto& t = themes->current();
        title.setColour (juce::Label::textColourId, t.accent);
        for (auto* l : { &themeLabel, &cableLabel }) l->setColour (juce::Label::textColourId, t.text);
        colourLabel.setColour (juce::Label::textColourId, t.textDim);
        showCables.setToggleState (themes->showCables(), juce::dontSendNotification);
        cableColour.setSelectedId (themes->cableColourIndex() + 1, juce::dontSendNotification);
        cableColour.setEnabled (themes->showCables());
    }

    juce::Rectangle<int> OptionsPanel::card() const
    {
        return getLocalBounds().withSizeKeepingCentre (juce::jmin (1000, getWidth() - 40), juce::jmin (660, getHeight() - 30));
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
        g.drawHorizontalLine (cableLabel.getY() - 8, (float) c.getX() + 24.0f, c.getRight() - 24.0f);
    }

    void OptionsPanel::resized()
    {
        auto c = card().reduced (24, 18);
        title.setBounds (c.removeFromTop (38));
        c.removeFromTop (4);
        themeLabel.setBounds (c.removeFromTop (26));
        c.removeFromTop (6);

        auto bottom = c.removeFromBottom (36);
        closeButton.setBounds (bottom.removeFromRight (120));
        c.removeFromBottom (12);
        auto cablesRow = c.removeFromBottom (34);
        c.removeFromBottom (4);
        cableLabel.setBounds (c.removeFromBottom (26));
        c.removeFromBottom (14);
        showCables.setBounds (cablesRow.removeFromLeft (230));
        cablesRow.removeFromLeft (20);
        colourLabel.setBounds (cablesRow.removeFromLeft (120));
        cableColour.setBounds (cablesRow.removeFromLeft (200).reduced (0, 3));

        const int cols = 3, rows = (tiles.size() + cols - 1) / cols;
        const int tw = c.getWidth() / cols, th = c.getHeight() / rows;
        for (int i = 0; i < tiles.size(); ++i)
            tiles[i]->setBounds (c.getX() + (i % cols) * tw + 5, c.getY() + (i / cols) * th + 5, tw - 10, th - 10);
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
