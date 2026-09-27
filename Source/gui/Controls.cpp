/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)
*/

#include "Controls.h"

namespace pt::ui
{
    //==============================================================================
    PedalLookAndFeel::PedalLookAndFeel()
    {
        setColour (juce::BubbleComponent::backgroundColourId, juce::Colour (0xee141416));
        setColour (juce::BubbleComponent::outlineColourId, juce::Colour (0x66ffffff));
        setColour (juce::TooltipWindow::textColourId, juce::Colours::white);
    }

    void PedalLookAndFeel::drawBubble (juce::Graphics& g, juce::BubbleComponent&, const juce::Point<float>&,
                                       const juce::Rectangle<float>& body)
    {
        g.setColour (juce::Colour (0xee141416));
        g.fillRoundedRectangle (body, 5.0f);
        g.setColour (juce::Colour (0x55ffffff));
        g.drawRoundedRectangle (body.reduced (0.5f), 5.0f, 1.0f);
    }

    juce::Font PedalLookAndFeel::getSliderPopupFont (juce::Slider&)
    {
        return juce::Font (14.0f, juce::Font::bold);
    }

    int PedalLookAndFeel::getSliderPopupPlacement (juce::Slider&)
    {
        return juce::BubbleComponent::above;
    }

    int PedalLookAndFeel::getSliderThumbRadius (juce::Slider& s)
    {
        if (dynamic_cast<FaderCap*> (&s) != nullptr)
            return FaderCap::indentPx();
        return LookAndFeel_V4::getSliderThumbRadius (s);
    }

    //==============================================================================
    FilmstripKnob::FilmstripKnob (const Assets& a, const Knob& layout, float innerExclusionRadius)
        : assets (a), k (layout), exclusion (innerExclusionRadius)
    {
        setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        setRotaryParameters (juce::degreesToRadians (-150.0f), juce::degreesToRadians (150.0f), true);
        setMouseDragSensitivity (220);
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);

        const auto r = Assets::frameBounds (k.strip, k.ax, k.ay).getSmallestIntegerContainer();
        setBounds (r);
        origin = r.getPosition().toFloat();
    }

    void FilmstripKnob::paint (juce::Graphics& g)
    {
        const double prop = valueToProportionOfLength (getValue());
        assets.drawFrame (g, k.strip, Assets::frameForProportion (k.strip, prop), k.ax - origin.x, k.ay - origin.y);
    }

    bool FilmstripKnob::hitTest (int x, int y)
    {
        // centro visivo = meta' strada tra la base e la sommita' del pomello
        const juce::Point<float> c ((k.ax + k.tx) * 0.5f - origin.x, (k.ay + k.ty) * 0.5f - origin.y);
        const float d = c.getDistanceFrom ({ (float) x, (float) y });
        return d <= k.radius * 1.15f && d >= exclusion;
    }

    //==============================================================================
    int FaderCap::indentPx()
    {
        const auto& s = strips[sliderCap];
        return (int) std::ceil (juce::jmax (s.anchorY, (float) s.frameH - s.anchorY) / renderScale) + 1;
    }

    FaderCap::FaderCap (const Assets& a, const Fader& layout) : assets (a), f (layout)
    {
        setSliderStyle (juce::Slider::LinearVertical);
        setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        setSliderSnapsToMousePosition (false);
        setMouseDragSensitivity (160);
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);

        const auto& s = strips[sliderCap];
        const int indent = indentPx();
        const float halfW = juce::jmax (s.anchorX, (float) s.frameW - s.anchorX) / renderScale + 1.0f;
        const auto top = (int) std::floor (f.yMax) - indent;
        const auto bottom = (int) std::ceil (f.yMin) + indent;
        setBounds ((int) std::floor (f.x - halfW), top, (int) std::ceil (halfW * 2.0f), bottom - top);
        origin = getPosition().toFloat();
    }

    juce::Point<float> FaderCap::capPosition()
    {
        const double prop = valueToProportionOfLength (getValue());
        return { f.x - origin.x, (float) (f.yMin + (f.yMax - f.yMin) * prop) - origin.y };
    }

    void FaderCap::paint (juce::Graphics& g)
    {
        const auto p = capPosition();
        assets.drawFrame (g, sliderCap, 0, p.x, p.y);
    }

    bool FaderCap::hitTest (int x, int y)
    {
        return std::abs ((float) x - (f.x - origin.x)) <= 11.0f && y >= 0 && y < getHeight();
    }

    //==============================================================================
    FootSwitch::FootSwitch (const Footswitch& q) : juce::Button (q.pedal)
    {
        juce::Path p;
        p.startNewSubPath (q.x[0], q.y[0]);
        for (int i = 1; i < 4; ++i)
            p.lineTo (q.x[i], q.y[i]);
        p.closeSubPath();
        const auto b = p.getBounds().getSmallestIntegerContainer().expanded (2);
        setBounds (b);
        p.applyTransform (juce::AffineTransform::translation ((float) -b.getX(), (float) -b.getY()));
        shape = p.createPathWithRoundedCorners (10.0f);
        setClickingTogglesState (true);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTooltip ("On / Off");
    }

    void FootSwitch::paintButton (juce::Graphics& g, bool highlighted, bool down)
    {
        if (down)
        {
            g.setColour (juce::Colours::black.withAlpha (0.28f));
            g.fillPath (shape);
        }
        else if (highlighted)
        {
            g.setColour (juce::Colours::white.withAlpha (0.045f));
            g.fillPath (shape);
        }
    }

    bool FootSwitch::hitTest (int x, int y)
    {
        return shape.contains ((float) x, (float) y);
    }

    //==============================================================================
    LedGlow::LedGlow (const Led& l) : led (l)
    {
        setInterceptsMouseClicks (false, false);
        const float r = led.radius * 7.0f;
        setBounds (juce::Rectangle<float> (led.x - r, led.y - r, r * 2, r * 2).getSmallestIntegerContainer());
    }

    void LedGlow::setOn (bool shouldBeOn)
    {
        if (on != shouldBeOn)
        {
            on = shouldBeOn;
            repaint();
        }
    }

    void LedGlow::paint (juce::Graphics& g)
    {
        if (! on)
            return;
        const juce::Point<float> c (led.x - (float) getX(), led.y - (float) getY());
        const float r = led.radius;

        juce::ColourGradient halo (juce::Colour (0x88ff2a14), c, juce::Colour (0x00ff2a14), c.translated (r * 6.5f, 0), true);
        halo.addColour (0.25, juce::Colour (0x55ff3018));
        g.setGradientFill (halo);
        g.fillEllipse (juce::Rectangle<float> (r * 13.0f, r * 13.0f).withCentre (c));

        juce::ColourGradient core (juce::Colour (0xfffff0e0), c.translated (-r * 0.25f, -r * 0.3f),
                                   juce::Colour (0xffff2a10), c.translated (r, 0), true);
        core.addColour (0.45, juce::Colour (0xffff7050));
        g.setGradientFill (core);
        g.fillEllipse (juce::Rectangle<float> (r * 2.1f, r * 1.9f).withCentre (c));
    }

    //==============================================================================
    ModeToggle::ModeToggle (const Assets& a, juce::RangedAudioParameter& param)
        : assets (a),
          attachment (param, [this] (float v) { custom = v > 0.5f; repaint(); })
    {
        const auto r = Assets::frameBounds (toggle, modeToggleX, modeToggleY)
                           .getUnion (Assets::frameBounds (toggle, modeToggleX, modeToggleY))
                           .getSmallestIntegerContainer();
        setBounds (r);
        origin = r.getPosition().toFloat();
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTitle ("Mode S/C");
        attachment.sendInitialUpdate();
    }

    void ModeToggle::paint (juce::Graphics& g)
    {
        assets.drawFrame (g, toggle, custom ? 1 : 0, modeToggleX - origin.x, modeToggleY - origin.y);
    }

    void ModeToggle::mouseUp (const juce::MouseEvent& e)
    {
        if (e.mouseWasClicked())
            attachment.setValueAsCompleteGesture (custom ? 0.0f : 1.0f);
    }

    bool ModeToggle::hitTest (int x, int y)
    {
        const juce::Point<float> c (modeToggleX - origin.x, modeToggleY - origin.y - 8.0f);
        return c.getDistanceFrom ({ (float) x, (float) y }) < 16.0f;
    }

    //==============================================================================
    InfoButton::InfoButton() : juce::Button ("Info")
    {
        const float r = infoRadius * 1.6f;
        setBounds (juce::Rectangle<float> (infoX - r, infoY - r, r * 2, r * 2).getSmallestIntegerContainer());
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTooltip ("Info / Licenza");
    }

    void InfoButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
    {
        const juce::Point<float> c (infoX - (float) getX(), infoY - (float) getY());
        const float r = infoRadius;
        if (highlighted || down)
        {
            juce::ColourGradient glow (juce::Colour (down ? 0x66ffd28a : 0x44ffd28a), c,
                                       juce::Colour (0x00ffd28a), c.translated (r * 1.6f, 0), true);
            g.setGradientFill (glow);
            g.fillEllipse (juce::Rectangle<float> (r * 3.2f, r * 3.2f).withCentre (c));
        }
        if (down)
        {
            g.setColour (juce::Colours::black.withAlpha (0.2f));
            g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));
        }
    }

    bool InfoButton::hitTest (int x, int y)
    {
        const juce::Point<float> c (infoX - (float) getX(), infoY - (float) getY());
        return c.getDistanceFrom ({ (float) x, (float) y }) <= infoRadius * 1.3f;
    }
}
