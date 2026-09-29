/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)
*/

#include "InfoPanel.h"
#include "Theme.h"
#include "Assets.h"
#include "../Version.h"
#include "../engine/Model.h"

namespace pt::ui
{
    namespace
    {
        /** Pedali del catalogo (lo splitter SPL-3 e' un modulo di instradamento, non un pedale). */
        int pedalCount()
        {
            int n = 0;
            for (int i = 0; i < pt::engine::numModels(); ++i)
                if (pt::engine::model (i).family != pt::engine::Family::Splitter) ++n;
            return n;
        }
    }

    namespace
    {
        // colori del tema corrente (letti alla creazione del pannello)
        juce::Colour gold, text, dim, cardTop, cardBottom, primaryButton, plainButton;

        void loadTheme()
        {
            juce::SharedResourcePointer<ThemeManager> tm;
            const auto& t = tm->current();
            gold = t.accent; text = t.text; dim = t.textDim;
            cardTop = t.panelTop.brighter (0.06f); cardBottom = t.panelBottom;
            primaryButton = t.selected; plainButton = t.button.brighter (0.08f);
        }

        void styleEditor (juce::TextEditor& e, float size)
        {
            e.setMultiLine (true, true);
            e.setReadOnly (true);
            e.setCaretVisible (false);
            e.setScrollbarsShown (true);
            e.setFont (juce::Font (size));
            e.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0x44000000));
            e.setColour (juce::TextEditor::outlineColourId, juce::Colour (0x22ffffff));
            e.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colour (0x44ffffff));
            e.setColour (juce::TextEditor::textColourId, text);
        }

        void styleButton (juce::TextButton& b, bool primary)
        {
            b.setColour (juce::TextButton::buttonColourId, primary ? primaryButton : plainButton);
            b.setColour (juce::TextButton::buttonOnColourId, primaryButton.brighter (0.2f));
            b.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
            b.setMouseCursor (juce::MouseCursor::PointingHandCursor);
        }
    }

    InfoPanel::InfoPanel()
    {
        loadTheme();
        setWantsKeyboardFocus (true);

        title.setText (pt::productName, juce::dontSendNotification);
        title.setFont (juce::Font (34.0f, juce::Font::bold));
        title.setColour (juce::Label::textColourId, text);
        addAndMakeVisible (title);

        subtitle.setText (juce::String ("Versione ") + pt::versionString + juce::String::fromUTF8 ("   \xe2\x80\xa2   by ")
                              + pt::author, juce::dontSendNotification);
        subtitle.setFont (juce::Font (16.0f, juce::Font::bold));
        subtitle.setColour (juce::Label::textColourId, gold);
        addAndMakeVisible (subtitle);

        styleEditor (body, 14.5f);
        body.setText (juce::String (juce::CharPointer_UTF8 (
            "Pedaliera virtuale: fino a 100 pedali in catena, scelti da un catalogo di ")) + juce::String (pedalCount())
            + juce::String (juce::CharPointer_UTF8 (" modelli ispirati al catalogo BOSS (e al Tube Screamer), "
            "con emulazione dei circuiti reali stadio per stadio, BBD a clock e modelli digitali dalle specifiche. "
            "Lo splitter SPL-3 divide la catena in due linee mono (dual) o in una catena stereo; i cavi jack "
            "e i meter INPUT/OUTPUT mostrano il percorso del segnale.\n"
            "Formati: VST3, LV2 e Standalone (Linux e Windows, driver ASIO su Windows). Preset salvabili ed esportabili.\n\n"
            "LICENZA: GNU General Public License versione 3 (GPL-3.0-or-later).\n"
            "Copyright \xc2\xa9 2026 FabioNET.\n"
            "Questo programma \xc3\xa8 software libero: puoi ridistribuirlo e/o modificarlo secondo i termini "
            "della GNU GPL pubblicata dalla Free Software Foundation, versione 3 o (a tua scelta) successiva. "
            "\xc3\x88 distribuito nella speranza che sia utile, ma SENZA ALCUNA GARANZIA; senza neppure la garanzia "
            "implicita di COMMERCIABILIT\xc3\x80 o IDONEIT\xc3\x80 PER UNO SCOPO PARTICOLARE. "
            "Il testo completo della licenza \xc3\xa8 riportato qui sotto.\n\n"
            "CREDITI: realizzato in C++ con JUCE 7 (GPLv3). Il pedale NAM-A1A2 usa NeuralAmpModelerCore, "
            "AudioDSPTools e parti di NeuralAmpModelerPlugin di Steven Atkinson, il lettore .namb di TONE3000 "
            "e nlohmann/json (licenza MIT), Eigen (MPL 2.0): testi completi in fondo alla licenza. VST\xc2\xae \xc3\xa8 un marchio di Steinberg Media "
            "Technologies GmbH. ASIO \xc3\xa8 un marchio e software di Steinberg Media Technologies GmbH. "
            "LV2 \xc2\xa9 lv2plug.in (licenza ISC).\n\n"
            "MARCHI: BOSS, Roland e le sigle dei pedali BOSS sono marchi di Roland Corporation; Ibanez e Tube Screamer "
            "di Hoshino Gakki; Fender di Fender Musical Instruments. Pedal Trinity \xc3\xa8 un progetto indipendente, "
            "non affiliato n\xc3\xa9 approvato: i modelli hanno nomi e sigle originali, i riferimenti indicano solo il "
            "suono di riferimento. Le immagini dei pedali sono render 3D originali. In modalit\xc3\xa0 REAL MOD le repliche, "
            "modellate da zero e senza loghi, riportano sigla e nome del pedale reale solo per identificarlo; le foto "
            "personali restano sul computer dell'utente.")));
        addAndMakeVisible (body);

        styleEditor (license, 12.5f);
        license.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain));
        license.setText (Assets::resourceAsString ("license_gpl3_txt") + "\n\n\n" + Assets::resourceAsString ("LICENSENAM_txt"));
        addAndMakeVisible (license);

        styleButton (guideButton, true);
        styleButton (webButton, false);
        styleButton (closeButton, false);
        guideButton.onClick = [] { openGuide(); };
        webButton.onClick = [] { juce::URL (pt::homepage).launchInDefaultBrowser(); };
        closeButton.onClick = [this] { if (onClose) onClose(); };
        addAndMakeVisible (guideButton);
        addAndMakeVisible (webButton);
        addAndMakeVisible (closeButton);
    }

    juce::Rectangle<int> InfoPanel::card() const
    {
        return getLocalBounds().withSizeKeepingCentre (juce::jmin (780, getWidth() - 40),
                                                        juce::jmin (560, getHeight() - 30));
    }

    void InfoPanel::paint (juce::Graphics& g)
    {
        g.fillAll (juce::Colours::black.withAlpha (0.62f));
        const auto c = card().toFloat();

        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRoundedRectangle (c.translated (0, 6).expanded (4), 14.0f);
        g.setGradientFill (juce::ColourGradient (cardTop, c.getTopLeft(), cardBottom, c.getBottomLeft(), false));
        g.fillRoundedRectangle (c, 12.0f);
        g.setColour (gold.withAlpha (0.55f));
        g.drawRoundedRectangle (c.reduced (1.0f), 12.0f, 1.4f);

        // tre "LED" colorati come i pedali
        const juce::Colour pedalColours[] = { juce::Colour (0xff2f8a44), juce::Colour (0xff222226), juce::Colour (0xffc7cad0) };
        for (int i = 0; i < 3; ++i)
        {
            auto r = juce::Rectangle<float> (14.0f, 14.0f).withCentre ({ c.getRight() - 90.0f + (float) i * 24.0f, c.getY() + 40.0f });
            g.setColour (pedalColours[i]);
            g.fillEllipse (r);
            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.drawEllipse (r, 1.0f);
        }

        g.setColour (dim);
        g.setFont (juce::Font (12.0f));
        g.drawText ("Licenza completa (GNU GPL v3)", license.getBounds().translated (0, -18).withHeight (16),
                    juce::Justification::centredLeft);
    }

    void InfoPanel::resized()
    {
        auto c = card().reduced (26, 20);
        auto top = c.removeFromTop (72);
        title.setBounds (top.removeFromTop (42));
        subtitle.setBounds (top);

        auto buttons = c.removeFromBottom (36);
        closeButton.setBounds (buttons.removeFromRight (120));
        buttons.removeFromRight (10);
        webButton.setBounds (buttons.removeFromRight (170));
        guideButton.setBounds (buttons.removeFromLeft (200));
        c.removeFromBottom (14);

        body.setBounds (c.removeFromTop (juce::roundToInt (c.getHeight() * 0.52f)));
        c.removeFromTop (24);
        license.setBounds (c);
    }

    void InfoPanel::mouseUp (const juce::MouseEvent& e)
    {
        if (! card().contains (e.getPosition()) && onClose)
            onClose();
    }

    bool InfoPanel::keyPressed (const juce::KeyPress& k)
    {
        if (k == juce::KeyPress::escapeKey && onClose)
        {
            onClose();
            return true;
        }
        return false;
    }

    bool InfoPanel::openGuide()
    {
        const auto data = Assets::resourceAsBlock ("PedalTrinity_Guida_pdf");
        if (data.getSize() == 0)
            return false;

        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("PedalTrinity");
        dir.createDirectory();
        auto pdf = dir.getChildFile ("PedalTrinity_Guida_v1.1.0-beta.pdf");
        if (pdf.getSize() != (juce::int64) data.getSize())
            pdf.replaceWithData (data.getData(), data.getSize());
        return pdf.startAsProcess();
    }
}
