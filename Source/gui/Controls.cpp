/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)
*/

#include "Controls.h"

namespace pt::ui
{
    using namespace pt::engine;

    juce::String formatControlValue (const ControlDef& c, double x)
    {
        const juce::String label (c.label);
        if (c.choices != nullptr && *c.choices != 0)
        {
            auto items = juce::StringArray::fromTokens (c.choices, "|", "");
            const int steps = juce::jmax (1, (int) c.steps, items.size());
            const int idx = juce::jlimit (0, items.size() - 1, juce::roundToInt (x * (steps - 1)));
            return label + "  " + items[idx];
        }
        const double v = c.lo + (c.hi - c.lo) * x;
        switch (c.units)
        {
            case Units::Db:       return label + "  " + (v > 0 ? "+" : "") + juce::String (v, 1) + " dB";
            case Units::Hz:
            {
                const double f = c.lo * std::pow (c.hi / juce::jmax (1.0f, c.lo), x);
                return label + "  " + (f >= 1000 ? juce::String (f / 1000.0, 2) + " kHz" : juce::String (juce::roundToInt (f)) + " Hz");
            }
            case Units::Ms:       return label + "  " + juce::String (c.lo * std::pow (c.hi / juce::jmax (0.1f, c.lo), x), 0) + " ms";
            case Units::Percent:  return label + "  " + juce::String (juce::roundToInt (x * 100)) + " %";
            case Units::Semitone: return label + "  " + (v > 0 ? "+" : "") + juce::String (juce::roundToInt (v)) + " st";
            default:              return label + "  " + juce::String (x * 10.0, 1);
        }
    }

    //==============================================================================
    PedalLookAndFeel::PedalLookAndFeel()
    {
        setColour (juce::BubbleComponent::backgroundColourId, juce::Colour (0xee141416));
        setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff1c1d20));
        setColour (juce::PopupMenu::textColourId, juce::Colour (0xffe8e6df));
        setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff3d6b45));
        setColour (juce::PopupMenu::headerTextColourId, juce::Colour (0xffd9b464));
        setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff202124));
        setColour (juce::ComboBox::textColourId, juce::Colour (0xffe8e6df));
        setColour (juce::ComboBox::outlineColourId, juce::Colour (0x33ffffff));
        setColour (juce::ComboBox::arrowColourId, juce::Colour (0xffd9b464));
        setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a2b2f));
        setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff3d6b45));
        setColour (juce::TextButton::textColourOffId, juce::Colour (0xffe8e6df));
        setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xf0141416));
        setColour (juce::TooltipWindow::textColourId, juce::Colours::white);
        setColour (juce::AlertWindow::backgroundColourId, juce::Colour (0xff1c1d20));
        setColour (juce::AlertWindow::textColourId, juce::Colour (0xffe8e6df));
        setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff121315));
        setColour (juce::TextEditor::textColourId, juce::Colour (0xffe8e6df));
    }

    void PedalLookAndFeel::applyTheme (const Theme& t)
    {
        setColour (juce::BubbleComponent::backgroundColourId, t.panelBottom.withAlpha (0.94f));
        setColour (juce::PopupMenu::backgroundColourId, t.panelTop);
        setColour (juce::PopupMenu::textColourId, t.text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, t.selected);
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
        setColour (juce::PopupMenu::headerTextColourId, t.accent);
        setColour (juce::ComboBox::backgroundColourId, t.button);
        setColour (juce::ComboBox::textColourId, t.text);
        setColour (juce::ComboBox::outlineColourId, t.text.withAlpha (0.2f));
        setColour (juce::ComboBox::arrowColourId, t.accent);
        setColour (juce::TextButton::buttonColourId, t.button);
        setColour (juce::TextButton::buttonOnColourId, t.selected);
        setColour (juce::TextButton::textColourOffId, t.text);
        setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        setColour (juce::ToggleButton::textColourId, t.text);
        setColour (juce::ToggleButton::tickColourId, t.accent);
        setColour (juce::ToggleButton::tickDisabledColourId, t.textDim);
        setColour (juce::TooltipWindow::backgroundColourId, t.panelBottom.withAlpha (0.96f));
        setColour (juce::TooltipWindow::textColourId, t.text);
        setColour (juce::AlertWindow::backgroundColourId, t.panelTop);
        setColour (juce::AlertWindow::textColourId, t.text);
        setColour (juce::TextEditor::backgroundColourId, t.panelBottom);
        setColour (juce::TextEditor::textColourId, t.text);
        setColour (juce::Label::textColourId, t.text);
    }

    void PedalLookAndFeel::drawBubble (juce::Graphics& g, juce::BubbleComponent&, const juce::Point<float>&,
                                       const juce::Rectangle<float>& body)
    {
        g.setColour (findColour (juce::BubbleComponent::backgroundColourId));
        g.fillRoundedRectangle (body, 5.0f);
        g.setColour (juce::Colour (0x55ffffff));
        g.drawRoundedRectangle (body.reduced (0.5f), 5.0f, 1.0f);
    }

    juce::Font PedalLookAndFeel::getSliderPopupFont (juce::Slider&) { return juce::Font (14.0f, juce::Font::bold); }
    int PedalLookAndFeel::getSliderPopupPlacement (juce::Slider&)  { return juce::BubbleComponent::above; }
    int PedalLookAndFeel::getSliderThumbRadius (juce::Slider& s)
    {
        if (dynamic_cast<FaderControl*> (&s) != nullptr) return FaderControl::indentPx();
        return LookAndFeel_V4::getSliderThumbRadius (s);
    }

    void PedalLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
    {
        g.fillAll (findColour (juce::PopupMenu::backgroundColourId));
        g.setColour (juce::Colour (0x22ffffff));
        g.drawRect (0, 0, w, h);
    }

    juce::Font PedalLookAndFeel::getPopupMenuFont() { return juce::Font (15.0f); }
    juce::Font PedalLookAndFeel::getComboBoxFont (juce::ComboBox& b)
    {
        return juce::Font (juce::jlimit (11.0f, 15.0f, (float) b.getHeight() * 0.62f), juce::Font::bold);
    }

    void PedalLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
    {
        const auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
        g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (box.findColour (juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
        juce::Path arrow;
        const float ax = (float) w - 11.0f, ay = (float) h * 0.5f;
        arrow.addTriangle (ax - 4, ay - 2, ax + 4, ay - 2, ax, ay + 3);
        g.setColour (box.findColour (juce::ComboBox::arrowColourId));
        g.fillPath (arrow);
    }

    void PedalLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& base, bool over, bool down)
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        auto c = b.getToggleState() ? b.findColour (juce::TextButton::buttonOnColourId) : base;
        if (down) c = c.brighter (0.25f); else if (over) c = c.brighter (0.12f);
        g.setGradientFill (juce::ColourGradient (c.brighter (0.08f), r.getTopLeft(), c.darker (0.2f), r.getBottomLeft(), false));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (juce::Colour (0x30ffffff));
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
    }

    //==============================================================================
    KnobControl::KnobControl (const Assets& a, const ControlDef& cd) : assets (a), c (cd)
    {
        setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        setRotaryParameters (juce::degreesToRadians (-150.0f), juce::degreesToRadians (150.0f), true);
        const bool stepped = (c.kind == ControlKind::Selector) && c.steps > 1;
        setRange (0.0, 1.0, stepped ? 1.0 / (c.steps - 1) : 0.0);
        setMouseDragSensitivity (stepped ? 90 : 200);
        setDoubleClickReturnValue (true, c.def);
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        setTitle (c.label);
        textFromValueFunction = [this] (double v) { return formatControlValue (c, v); };
        onValueChange = [this] { if (onChange) onChange ((float) juce::Slider::getValue()); };
        const auto r = Assets::frameBounds (c.strip, c.x, c.y).getSmallestIntegerContainer();
        setBounds (r);
        origin = r.getPosition().toFloat();
    }

    void KnobControl::syncFromModel()
    {
        if (readModel && ! isMouseButtonDown())
            setValue (readModel(), juce::dontSendNotification);
    }

    void KnobControl::paint (juce::Graphics& g)
    {
        double prop = juce::Slider::getValue();
        if (c.kind == ControlKind::Selector && c.steps > 1)
            prop = 0.08 + 0.84 * prop;       // i selettori a scatti non usano tutta la corsa
        assets.drawFrame (g, c.strip, Assets::frameForProportion (c.strip, prop), c.x - origin.x, c.y - origin.y);
    }

    bool KnobControl::hitTest (int x, int y)
    {
        const juce::Point<float> ctr ((c.x + c.tx) * 0.5f - origin.x, (c.y + c.ty) * 0.5f - origin.y);
        const float d = ctr.getDistanceFrom ({ (float) x, (float) y });
        return d <= c.radius * 1.15f && d >= exclusion;
    }

    //==============================================================================
    int FaderControl::indentPx()
    {
        const auto& s = strips[sliderCap];
        return (int) std::ceil (juce::jmax (s.anchorY, (float) s.frameH - s.anchorY) / renderScale) + 1;
    }

    FaderControl::FaderControl (const Assets& a, const ControlDef& cd) : assets (a), c (cd)
    {
        setSliderStyle (juce::Slider::LinearVertical);
        setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        setRange (0.0, 1.0);
        setSliderSnapsToMousePosition (false);
        setMouseDragSensitivity (160);
        setDoubleClickReturnValue (true, c.def);
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        setTitle (c.label);
        textFromValueFunction = [this] (double v) { return formatControlValue (c, v); };
        onValueChange = [this] { if (onChange) onChange ((float) juce::Slider::getValue()); };
        const auto& s = strips[sliderCap];
        const int indent = indentPx();
        const float halfW = juce::jmax (s.anchorX, (float) s.frameW - s.anchorX) / renderScale + 1.0f;
        // c.y = ancoraggio al minimo, c.ty = ancoraggio al massimo
        const int top = (int) std::floor (c.ty) - indent, bottom = (int) std::ceil (c.y) + indent;
        setBounds ((int) std::floor (c.x - halfW), top, (int) std::ceil (halfW * 2.0f), bottom - top);
        origin = getPosition().toFloat();
    }

    void FaderControl::syncFromModel()
    {
        if (readModel && ! isMouseButtonDown())
            setValue (readModel(), juce::dontSendNotification);
    }

    void FaderControl::paint (juce::Graphics& g)
    {
        const double prop = juce::Slider::getValue();
        assets.drawFrame (g, sliderCap, 0, c.x - origin.x, (float) (c.y + (c.ty - c.y) * prop) - origin.y);
    }

    bool FaderControl::hitTest (int x, int y)
    {
        return std::abs ((float) x - (c.x - origin.x)) <= 11.0f && y >= 0 && y < getHeight();
    }

    //==============================================================================
    ToggleControl::ToggleControl (const Assets& a, const ControlDef& cd) : assets (a), c (cd)
    {
        const auto r = Assets::frameBounds (toggle, c.x, c.y).getSmallestIntegerContainer();
        setBounds (r);
        origin = r.getPosition().toFloat();
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTooltip (formatControlValue (c, 0.0) + "  /  " + formatControlValue (c, 1.0));
    }

    void ToggleControl::syncFromModel()
    {
        if (readModel)
        {
            const bool s = readModel() > 0.5f;
            if (s != state) { state = s; repaint(); }
        }
    }

    void ToggleControl::paint (juce::Graphics& g)
    {
        assets.drawFrame (g, toggle, state ? 1 : 0, c.x - origin.x, c.y - origin.y);
    }

    void ToggleControl::mouseUp (const juce::MouseEvent& e)
    {
        if (! e.mouseWasClicked()) return;
        state = ! state;
        repaint();
        if (onChange) onChange (state ? 1.0f : 0.0f);
    }

    bool ToggleControl::hitTest (int x, int y)
    {
        const juce::Point<float> ctr (c.x - origin.x, c.y - origin.y - 8.0f);
        return ctr.getDistanceFrom ({ (float) x, (float) y }) < 16.0f;
    }

    //==============================================================================
    PushControl::PushControl (const ControlDef& cd) : juce::Button (cd.label), c (cd)
    {
        const float r = juce::jmax (8.0f, c.radius);
        setBounds (juce::Rectangle<float> (c.x - r, c.y - r, r * 2, r * 2).getSmallestIntegerContainer());
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        onClick = [this] { if (onPress) onPress(); };
        setTooltip (c.label);
    }

    void PushControl::paintButton (juce::Graphics& g, bool over, bool down)
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd8d9dc), r.getTopLeft(), juce::Colour (0xff6d6f74), r.getBottomRight(), false));
        g.fillEllipse (r);
        if (down || over)
        {
            g.setColour (down ? juce::Colour (0x55000000) : juce::Colour (0x22ffffff));
            g.fillEllipse (r);
        }
        g.setColour (juce::Colour (0xff2a2b2e));
        g.drawEllipse (r, 1.2f);
        // icona sul cappuccio (il nome del comando e' serigrafato sotto il tasto)
        const juce::String role (c.role != nullptr ? c.role : "");
        const auto ic = r.reduced (r.getWidth() * 0.3f);
        juce::Path p;
        if (role == "stop")
            p.addRectangle (ic.reduced (ic.getWidth() * 0.08f));
        else if (role == "clear")
        {
            p.addLineSegment ({ ic.getTopLeft(), ic.getBottomRight() }, ic.getWidth() * 0.2f);
            p.addLineSegment ({ ic.getTopRight(), ic.getBottomLeft() }, ic.getWidth() * 0.2f);
        }
        else if (role == "undo")
        {
            p.addCentredArc (ic.getCentreX(), ic.getCentreY(), ic.getWidth() * 0.45f, ic.getHeight() * 0.45f, 0.0f,
                             -2.4f, 1.6f, true);
            p = [&] { juce::Path s; juce::PathStrokeType (ic.getWidth() * 0.16f).createStrokedPath (s, p); return s; }();
            const juce::Point<float> tip (ic.getCentreX() - ic.getWidth() * 0.34f, ic.getCentreY() - ic.getHeight() * 0.3f);
            p.addTriangle (tip.translated (-ic.getWidth() * 0.2f, -ic.getWidth() * 0.05f), tip.translated (ic.getWidth() * 0.18f, -ic.getWidth() * 0.12f),
                           tip.translated (0.0f, ic.getWidth() * 0.26f));
        }
        else
        {
            const float d = ic.getHeight() * 0.62f;       // cerchio (REC) + triangolo (PLAY)
            p.addEllipse (ic.getX() - d * 0.15f, ic.getCentreY() - d * 0.5f, d, d);
            const float x0 = ic.getX() + d * 1.0f, h = d * 0.5f;
            p.addTriangle (x0, ic.getCentreY() - h, x0, ic.getCentreY() + h, x0 + d * 0.85f, ic.getCentreY());
        }
        g.setColour (role == "stop" || role == "clear" || role == "undo" ? juce::Colour (0xff2a2b2e) : juce::Colour (0xffb3202a));
        g.fillPath (p);
    }

    //==============================================================================
    FootZone::FootZone (const float* xs, const float* ys) : juce::Button ("footswitch")
    {
        juce::Path p;
        p.startNewSubPath (xs[0], ys[0]);
        for (int i = 1; i < 4; ++i) p.lineTo (xs[i], ys[i]);
        p.closeSubPath();
        const auto b = p.getBounds().getSmallestIntegerContainer().expanded (2);
        setBounds (b);
        p.applyTransform (juce::AffineTransform::translation ((float) -b.getX(), (float) -b.getY()));
        shape = p.createPathWithRoundedCorners (8.0f);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTooltip ("On / Off");
    }

    void FootZone::paintButton (juce::Graphics& g, bool over, bool down)
    {
        if (down)      { g.setColour (juce::Colours::black.withAlpha (0.28f)); g.fillPath (shape); }
        else if (over) { g.setColour (juce::Colours::white.withAlpha (0.045f)); g.fillPath (shape); }
    }

    bool FootZone::hitTest (int x, int y) { return shape.contains ((float) x, (float) y); }
}
