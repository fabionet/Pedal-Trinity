/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)
*/

#include "Cables.h"

namespace pt::ui
{
    namespace cables
    {
        namespace
        {
            juce::Path cablePath (const Cable& c)
            {
                const float s = c.scale;
                const auto a = c.a.translated (c.dirA * plugLength * s, 0.0f);
                const auto b = c.b.translated (c.dirB * plugLength * s, 0.0f);
                const float dist = a.getDistanceFrom (b);
                juce::Path p;
                p.startNewSubPath (a);
                if (c.under)
                {
                    // ritorno tra due file: scende lungo il margine, corre nello spazio tra le file
                    // (sotto la pedaliera) e risale dall'altro lato fino alla presa
                    const float r = juce::jmax (14.0f, 30.0f * s);
                    const float xa = a.x + c.dirA * r, xb = b.x + c.dirB * r, y = c.laneY;
                    p.quadraticTo (xa, a.y, xa, a.y + (y > a.y ? r : -r));
                    p.lineTo (xa, y + (y > a.y ? -r : r));
                    p.quadraticTo (xa, y, xa + (xb > xa ? r : -r), y);
                    p.lineTo (xb + (xb > xa ? -r : r), y);
                    p.quadraticTo (xb, y, xb, y + (b.y > y ? r : -r));
                    p.lineTo (xb, b.y + (b.y > y ? -r : r));
                    p.quadraticTo (xb, b.y, b.x, b.y);
                    return p;
                }
                // tratto che cede sotto il proprio peso (piu' lungo = piu' pancia, con un limite)
                const float sag = juce::jmin (dist * 0.16f, 70.0f * s + 20.0f) + 6.0f * s;
                const float k = juce::jmin (dist * 0.35f, 120.0f * s + 20.0f);
                const float dA = c.dirA != 0.0f ? c.dirA : (b.x >= a.x ? 1.0f : -1.0f);
                const float dB = c.dirB != 0.0f ? c.dirB : (a.x >= b.x ? 1.0f : -1.0f);
                p.cubicTo (a.translated (dA * k, sag), b.translated (dB * k, sag), b);
                return p;
            }

            juce::Colour ringColour (int line) { return line == 1 ? juce::Colour (0xffc8322a) : juce::Colour (0xffd9b464); }
        }

        void drawCable (juce::Graphics& g, const Cable& c, juce::Colour body, juce::Colour sheen)
        {
            const float w = juce::jmax (3.0f, 11.0f * c.scale);
            const auto path = cablePath (c);
            // ombra sulla moquette
            g.setColour (juce::Colours::black.withAlpha (0.38f));
            g.strokePath (path, juce::PathStrokeType (w * 1.15f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                          juce::AffineTransform::translation (w * 0.25f, w * 0.55f));
            // gomma
            g.setColour (body);
            g.strokePath (path, juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (sheen);
            g.strokePath (path, juce::PathStrokeType (w * 0.62f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            // riflesso lungo il cavo
            g.setColour (juce::Colours::white.withAlpha (0.13f));
            g.strokePath (path, juce::PathStrokeType (w * 0.22f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                          juce::AffineTransform::translation (0.0f, -w * 0.2f));
        }

        void drawPlug (juce::Graphics& g, juce::Point<float> jack, float dir, float s, int line)
        {
            if (dir == 0.0f) return;
            // coordinate lungo la spina: u = distanza dalla presa, v = verticale
            auto rect = [&] (float u0, float u1, float h) {
                const float x0 = jack.x + dir * u0 * s, x1 = jack.x + dir * u1 * s;
                return juce::Rectangle<float> (juce::jmin (x0, x1), jack.y - h * s * 0.5f, std::abs (x1 - x0), h * s);
            };
            // ombra
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillRoundedRectangle (rect (0, 36, 24).translated (1.5f * s, 5.0f * s), 5.0f * s);

            // colletto cromato nella presa
            const auto collar = rect (-2, 10, 17);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff2f2f0), 0, collar.getY(), juce::Colour (0xff6c6e72), 0, collar.getBottom(), false));
            g.fillRect (collar);
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            for (float u : { 3.0f, 6.5f })
            {
                const auto r = rect (u, u + 0.8f, 17);
                g.fillRect (r);
            }
            // corpo in plastica nera
            const auto body = rect (9, 36, 25);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a4b50), 0, body.getY(), juce::Colour (0xff0e0e10), 0, body.getBottom(), false));
            g.fillRoundedRectangle (body, 5.0f * s);
            g.setColour (juce::Colours::white.withAlpha (0.10f));
            g.fillRoundedRectangle (body.withHeight (body.getHeight() * 0.3f).translated (0, 1.5f * s), 3.0f * s);
            // anello colorato A / B
            g.setColour (ringColour (line));
            g.fillRect (rect (13, 16.5f, 25.5f));
            // guaina che si restringe verso il cavo
            juce::Path boot;
            const float x0 = jack.x + dir * 35.0f * s, x1 = jack.x + dir * plugLength * s;
            boot.startNewSubPath (x0, jack.y - 9.0f * s);
            boot.lineTo (x1, jack.y - 5.5f * s);
            boot.lineTo (x1, jack.y + 5.5f * s);
            boot.lineTo (x0, jack.y + 9.0f * s);
            boot.closeSubPath();
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff38393e), 0, jack.y - 9.0f * s, juce::Colour (0xff0c0c0e), 0, jack.y + 9.0f * s, false));
            g.fillPath (boot);
        }

        void drawSocket (juce::Graphics& g, juce::Point<float> p, float s)
        {
            juce::Path hex;
            const float r = 12.0f * s;
            for (int i = 0; i < 6; ++i)
            {
                const float a = juce::MathConstants<float>::pi / 3.0f * (float) i + juce::MathConstants<float>::pi / 6.0f;
                const juce::Point<float> q (p.x + r * std::cos (a), p.y + r * std::sin (a));
                if (i == 0) hex.startNewSubPath (q); else hex.lineTo (q);
            }
            hex.closeSubPath();
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd8d9dc), p.x - r, p.y - r, juce::Colour (0xff5a5c61), p.x + r, p.y + r, false));
            g.fillPath (hex);
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.strokePath (hex, juce::PathStrokeType (1.0f));
            g.setColour (juce::Colour (0xff0a0a0b));
            g.fillEllipse (juce::Rectangle<float> (r * 0.95f, r * 0.95f).withCentre (p));
        }
    }

    CableOverlay::CableOverlay()
    {
        setInterceptsMouseClicks (true, false);          // solo sulle spine (hitTest)
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        setTooltip ("Trascina la spina: infilala in un altro pedale per continuare la catena, "
                    "lasciala nel vuoto per staccare il pedale");
    }

    void CableOverlay::setCables (std::vector<Cable> c)
    {
        if (drag >= 0) return;                           // la catena si aggiorna al rilascio
        list = std::move (c);
        repaint();
    }

    juce::Rectangle<float> CableOverlay::plugArea (const Cable& c, bool endB) const
    {
        const float dir = endB ? c.dirB : c.dirA;
        if (dir == 0.0f || c.id < 0) return {};
        const auto j = endB ? c.b : c.a;
        const float s = c.scale, len = cables::plugLength * s, h = juce::jmax (16.0f, 30.0f * s);
        return { dir > 0 ? j.x - 2.0f * s : j.x - len, j.y - h * 0.5f, len + 2.0f * s, h };
    }

    bool CableOverlay::hitTest (int x, int y)
    {
        if (! visible) return false;
        if (drag >= 0) return true;
        const juce::Point<float> p ((float) x, (float) y);
        for (const auto& c : list)
            if (plugArea (c, false).contains (p) || plugArea (c, true).contains (p)) return true;
        return false;
    }

    void CableOverlay::mouseDown (const juce::MouseEvent& e)
    {
        const auto p = e.position;
        drag = -1;
        // la spina piu' in alto nella pila (disegnata per ultima) vince
        for (int i = (int) list.size() - 1; i >= 0 && drag < 0; --i)
            for (bool endB : { true, false })
                if (plugArea (list[(size_t) i], endB).contains (p))
                {
                    drag = i; dragB = endB;
                    grabOffset = p - (endB ? list[(size_t) i].b : list[(size_t) i].a);
                    break;
                }
        if (drag < 0) return;
        mouse = p;
        target = {};
        if (onGrab) onGrab (list[(size_t) drag].id, dragB);
        repaint();
    }

    void CableOverlay::mouseDrag (const juce::MouseEvent& e)
    {
        if (drag < 0) return;
        mouse = e.position;
        target = onMove ? onMove (list[(size_t) drag].id, dragB, mouse) : Target {};
        repaint();
    }

    void CableOverlay::mouseUp (const juce::MouseEvent& e)
    {
        if (drag < 0) return;
        const int id = list[(size_t) drag].id;
        const bool endB = dragB;
        const bool moved = e.getDistanceFromDragStart() > 4;
        drag = -1;
        target = {};
        repaint();
        if (onDrop) onDrop (id, endB, e.position, moved);
    }

    void CableOverlay::paint (juce::Graphics& g)
    {
        if (! visible) return;
        auto colours = [this] (const Cable& c, juce::Colour& b, juce::Colour& s)
        {
            b = c.body.isTransparent() ? body : c.body;
            s = c.body.isTransparent() ? sheen : c.sheen;
        };
        // cavo tenuto in mano: dall'estremita' ancora infilata fino alla spina sotto il puntatore
        Cable held;
        if (drag >= 0)
        {
            held = list[(size_t) drag];
            held.under = false;
            const auto tip = mouse - grabOffset;
            if (dragB) held.b = tip; else held.a = tip;
        }
        for (int i = 0; i < (int) list.size(); ++i)
        {
            const auto& c = i == drag ? held : list[(size_t) i];
            if (c.under) continue;
            juce::Colour b, s;
            colours (c, b, s);
            cables::drawCable (g, c, b, s);
        }
        for (int i = 0; i < (int) list.size(); ++i)
        {
            const auto& c = i == drag ? held : list[(size_t) i];
            cables::drawPlug (g, c.a, c.dirA, c.scale, c.line);
            cables::drawPlug (g, c.b, c.dirB, c.scale, c.line);
        }
        if (drag >= 0 && target.any)
        {
            const auto col = target.ok ? juce::Colour (0xff4cc26a) : juce::Colour (0xffe0554b);
            if (! target.area.isEmpty())
            {
                g.setColour (col.withAlpha (0.16f));
                g.fillRoundedRectangle (target.area, 8.0f);
                g.setColour (col);
                g.drawRoundedRectangle (target.area, 8.0f, 2.5f);
            }
            if (target.hint.isNotEmpty())
            {
                const juce::Font f (13.0f, juce::Font::bold);
                const float w = juce::jmin ((float) getWidth() - 16.0f, (float) f.getStringWidth (target.hint) + 20.0f);
                auto r = juce::Rectangle<float> (w, 24.0f).withCentre (mouse.translated (0.0f, -34.0f));
                r = r.withX (juce::jlimit (8.0f, juce::jmax (8.0f, (float) getWidth() - w - 8.0f), r.getX()))
                     .withY (juce::jmax (4.0f, r.getY()));
                g.setColour (juce::Colour (0xee141518));
                g.fillRoundedRectangle (r, 6.0f);
                g.setColour (col);
                g.drawRoundedRectangle (r, 6.0f, 1.2f);
                g.setColour (juce::Colours::white);
                g.setFont (f);
                g.drawFittedText (target.hint, r.toNearestInt().reduced (8, 0), juce::Justification::centred, 1, 0.8f);
            }
        }
    }
}
