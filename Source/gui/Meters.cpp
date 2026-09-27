/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)
*/

#include "Meters.h"
#include "Cables.h"

namespace pt::ui
{
    using namespace pt::engine;

    namespace
    {
        constexpr float logicalW = 120.0f;       // larghezza logica del pannello (px dei filmstrip @1x)
        constexpr float minDb = -60.0f, maxDb = 6.0f;

        float dbToProp (float db) { return juce::jlimit (0.0f, 1.0f, (db - minDb) / (maxDb - minDb)); }
    }

    /** Superficie logica scalata che contiene fader e pomelli (come la tela dei pedali). */
    class MeterPanel::Canvas : public juce::Component
    {
    public:
        Canvas() { setInterceptsMouseClicks (false, true); }
    };

    MeterPanel::MeterPanel (Side s, juce::AudioProcessorValueTreeState& state, LevelMeter& m)
        : side (s), apvts (state), meter (m)
    {
        canvas = std::make_unique<Canvas>();
        addAndMakeVisible (*canvas);
        value.setJustificationType (juce::Justification::centred);
        value.setFont (juce::Font (12.0f, juce::Font::bold));
        value.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (value);
        setTooltip (side == Side::Input ? "Ingresso: livello e volume (doppio clic sul fader = 0 dB)"
                                        : "Uscita: livello e volume (doppio clic sul fader = 0 dB)");
    }

    MeterPanel::~MeterPanel()
    {
        fader.reset();
        for (auto& k : knobs) k.reset();
    }

    /** Collega un comando 3D a un parametro dell'host (gesti di automazione compresi). */
    void MeterPanel::bind (BoundControl& b, const juce::String& paramId)
    {
        auto* p = apvts.getParameter (paramId);
        if (p == nullptr) return;
        b.onChange = [p] (float v) { p->setValueNotifyingHost (v); };
        b.readModel = [p] { return p->getValue(); };
        if (auto* sl = dynamic_cast<juce::Slider*> (&b))
        {
            sl->onDragStart = [p] { p->beginChangeGesture(); };
            sl->onDragEnd = [p] { p->endChangeGesture(); };
            sl->setDoubleClickReturnValue (true, p->getDefaultValue());
            sl->textFromValueFunction = [p] (double v) { return p->getText ((float) v, 12) + " " + p->getLabel(); };
            sl->setPopupDisplayEnabled (true, false, getTopLevelComponent());
        }
        b.syncFromModel();
    }

    void MeterPanel::resized()
    {
        const float s = (float) getWidth() / logicalW;          // pixel per pixel logico
        auto r = getLocalBounds();
        r.removeFromTop (24);                                   // titolo
        value.setBounds (r.removeFromBottom (20));
        if (side == Side::Output && splitControls)
            knobArea = r.removeFromBottom (juce::jmin (r.getHeight() / 2, (int) (3 * 92.0f * s)));
        else
            knobArea = {};
        r.removeFromBottom (6);
        meterArea = r;

        canvas->setTransform (juce::AffineTransform::scale (s));
        canvas->setBounds (0, 0, (int) std::ceil (logicalW), (int) std::ceil ((float) getHeight() / s));

        // fader: cappuccio 3D che corre lungo la meta' destra dell'area del meter
        const float top = (float) meterArea.getY() / s + 30.0f, bottom = (float) meterArea.getBottom() / s - 30.0f;
        fader.reset();
        faderDef = {};
        faderDef.label = side == Side::Input ? "INPUT" : "OUTPUT";
        faderDef.role = "gain";
        faderDef.kind = ControlKind::Slider;
        faderDef.strip = sliderCap;
        faderDef.units = Units::Db;
        faderDef.x = 78.0f; faderDef.y = bottom; faderDef.tx = 78.0f; faderDef.ty = top; faderDef.radius = 10.0f;
        fader = std::make_unique<FaderControl> (*assets, faderDef);
        canvas->addAndMakeVisible (*fader);
        bind (*fader, side == Side::Input ? "in_gain" : "out_gain");
        fader->setTooltip (side == Side::Input ? "Volume d'ingresso" : "Volume d'uscita");

        for (auto& k : knobs) k.reset();
        if (! knobArea.isEmpty())
        {
            const auto* ref = findModel ("ds1");                 // proporzioni di un pomello compatto
            const auto& rk = ref->controls[0];
            const char* labels[] = { "BALANCE", "LEFT", "RIGHT" };
            const char* ids[] = { "out_balance", "out_level_l", "out_level_r" };
            const float step = (float) knobArea.getHeight() / s / 3.0f;
            for (int i = 0; i < 3; ++i)
            {
                auto& d = knobDefs[i];
                d = rk;
                d.label = labels[i];
                d.units = i == 0 ? Units::Percent : Units::Db;
                d.x = logicalW * 0.5f;
                d.y = (float) knobArea.getY() / s + step * ((float) i + 0.5f) + 14.0f;
                d.tx = d.x + (rk.tx - rk.x);
                d.ty = d.y + (rk.ty - rk.y);
                knobs[i] = std::make_unique<KnobControl> (*assets, d);
                canvas->addAndMakeVisible (*knobs[i]);
                bind (*knobs[i], ids[i]);
                knobs[i]->setTooltip (i == 0 ? "Bilanciamento d'uscita tra la linea A (L) e la B (R)"
                                             : i == 1 ? "Livello dell'uscita sinistra (linea A)" : "Livello dell'uscita destra (linea B)");
            }
        }
    }

    void MeterPanel::setSplitControlsVisible (bool v)
    {
        if (side != Side::Output || v == splitControls) return;
        splitControls = v;
        resized();
        repaint();
    }

    void MeterPanel::setSockets (float yA, float yB, float scale)
    {
        sockY[0] = yA; sockY[1] = yB; sockScale = scale;
        repaint();
    }

    juce::Point<float> MeterPanel::socketPoint (int line) const
    {
        const float x = side == Side::Input ? (float) getWidth() - 4.0f : 4.0f;
        return { x, sockY[juce::jlimit (0, 1, line)] };
    }

    void MeterPanel::setChannels (int c)
    {
        c = juce::jlimit (1, 2, c);
        if (c != channels) { channels = c; repaint(); }
    }

    void MeterPanel::tick()
    {
        bool changed = false;
        for (int c = 0; c < 2; ++c)
        {
            const float p = meter.take (c);
            const float db = juce::Decibels::gainToDecibels (p, -100.0f);
            const float target = dbToProp (db);
            const float next = target > level[c] ? target : juce::jmax (target, level[c] - 0.025f);   // discesa lenta
            if (std::abs (next - level[c]) > 1e-4f) { level[c] = next; changed = true; }
            if (target >= hold[c]) { hold[c] = target; holdTicks[c] = 45; changed = true; }
            else if (--holdTicks[c] <= 0 && hold[c] > 0.0f) { hold[c] = juce::jmax (0.0f, hold[c] - 0.02f); changed = true; }
        }
        if (fader) fader->syncFromModel();
        for (auto& k : knobs) if (k) k->syncFromModel();
        if (auto* p = apvts.getParameter (side == Side::Input ? "in_gain" : "out_gain"))
            value.setText (p->getCurrentValueAsText() + " dB", juce::dontSendNotification);
        if (changed) repaint (meterArea.expanded (2));
    }

    void MeterPanel::paint (juce::Graphics& g)
    {
        const auto& t = themes->current();
        const auto gold = t.accent, text = t.textDim;
        if (value.findColour (juce::Label::textColourId) != gold) value.setColour (juce::Label::textColourId, gold);
        auto r = getLocalBounds().toFloat();
        g.setGradientFill (juce::ColourGradient (t.panelTop, 0, 0, t.panelBottom, 0, r.getBottom(), false));
        g.fillRoundedRectangle (r.reduced (1.0f), 6.0f);
        g.setColour (juce::Colour (0x22ffffff));
        g.drawRoundedRectangle (r.reduced (1.0f), 6.0f, 1.0f);

        g.setColour (gold);
        g.setFont (juce::Font (13.0f, juce::Font::bold));
        const juce::String title = side == Side::Input ? "INPUT" : "OUTPUT";
        g.drawText (title, getLocalBounds().removeFromTop (24), juce::Justification::centred);
        g.setColour (text);
        g.setFont (juce::Font (10.0f, juce::Font::bold));
        g.drawText (channels > 1 ? (side == Side::Input ? "A / B" : "L / R") : "MONO",
                    getLocalBounds().removeFromTop (38).removeFromBottom (14), juce::Justification::centred);

        // meter: una o due barre a LED continue
        const float s = (float) getWidth() / logicalW;
        auto m = meterArea.toFloat().withTrimmedTop (18.0f).withTrimmedBottom (4.0f);
        const float barW = juce::jmax (5.0f, 13.0f * s);
        const float x0 = m.getX() + 12.0f * s;
        auto yFor = [&m] (float prop) { return m.getBottom() - prop * m.getHeight(); };
        // scala in dB
        g.setFont (juce::Font (9.0f));
        for (float db : { 6.0f, 0.0f, -6.0f, -12.0f, -24.0f, -36.0f, -48.0f, -60.0f })
        {
            const float y = yFor (dbToProp (db));
            g.setColour (juce::Colour (0x30ffffff));
            g.drawHorizontalLine ((int) y, x0 - 3.0f, x0 + barW * 2.0f + 6.0f);
            g.setColour (text.withAlpha (0.8f));
            g.drawText (juce::String ((int) db), juce::Rectangle<float> (x0 + barW * 2.0f + 5.0f, y - 6.0f, 24.0f, 12.0f), juce::Justification::centredLeft);
        }
        for (int c = 0; c < channels; ++c)
        {
            const float bx = x0 + (float) c * (barW + 2.0f);
            const auto bar = juce::Rectangle<float> (bx, m.getY(), channels > 1 ? barW : barW * 2.0f + 2.0f, m.getHeight());
            g.setColour (juce::Colour (0xff0a0a0b));
            g.fillRoundedRectangle (bar, 2.0f);
            const float y = yFor (level[c]);
            juce::ColourGradient grad (juce::Colour (0xffff3b2f), 0, yFor (1.0f), juce::Colour (0xff3ad06a), 0, yFor (0.45f), false);
            grad.addColour (0.12, juce::Colour (0xffffc23a));
            g.setGradientFill (grad);
            g.fillRect (bar.withTop (y).reduced (1.0f, 0.0f));
            if (hold[c] > 0.01f)
            {
                g.setColour (hold[c] > dbToProp (0.0f) ? juce::Colour (0xffff3b2f) : juce::Colour (0xffe9e6dc));
                g.fillRect (bar.getX() + 1.0f, yFor (hold[c]) - 1.0f, bar.getWidth() - 2.0f, 2.0f);
            }
        }
        // guida del fader
        const float gx = 78.0f * s;
        g.setColour (juce::Colour (0xff050506));
        g.fillRoundedRectangle (juce::Rectangle<float> (gx - 2.5f * s - 1.0f, m.getY() + 10.0f, 5.0f * s + 2.0f, m.getHeight() - 20.0f), 2.0f);

        // etichette dei pomelli d'uscita
        if (! knobArea.isEmpty())
        {
            g.setColour (text);
            g.setFont (juce::Font (10.0f, juce::Font::bold));
            const float step = (float) knobArea.getHeight() / 3.0f;
            const char* labels[] = { "BALANCE", "LEFT", "RIGHT" };
            for (int i = 0; i < 3; ++i)
                g.drawText (labels[i], juce::Rectangle<float> (0.0f, (float) knobArea.getY() + step * (float) i, (float) getWidth(), 12.0f),
                            juce::Justification::centred);
        }

        // prese jack verso i pedali
        for (int line = 0; line < 2; ++line)
            if (sockY[line] >= 0.0f)
                cables::drawSocket (g, socketPoint (line).translated (side == Side::Input ? -8.0f * sockScale : 8.0f * sockScale, 0.0f),
                                    sockScale);
    }
}
