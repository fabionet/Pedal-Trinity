/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)
*/

#include "Theme.h"
#include <map>

namespace pt::ui
{
    namespace
    {
        juce::Colour c (uint32_t argb) { return juce::Colour (argb); }
    }

    const std::vector<Theme>& allThemes()
    {
        static const std::vector<Theme> t = {
            { "pro", "Pedana Pro",
              "Guide in alluminio anodizzato nero a lamelle, rivestite di tessuto a strappo, viti a vista.",
              BoardStyle::Rails, c (0xff1b1b1e), c (0xff060607), c (0xff3b3c41), c (0xff948f83),
              c (0xff0e0f11), c (0xff2b2c30), c (0xff18191c), c (0xffd9b464), c (0xffe8e6df), c (0xffb9b6ac),
              c (0xff202125), c (0xff141517), c (0xff1d1e22), c (0xff2a2b2f), c (0xff3d6b45) },
            { "tolex", "Tolex e cromo",
              "Tolex nero goffrato come un amplificatore, profilo crema e angolari cromati.",
              BoardStyle::Tolex, c (0xff161514), c (0xff0b0a09), c (0xffd8c9a6), c (0xffa89d88),
              c (0xff0f0e0d), c (0xff2d2a26), c (0xff1a1816), c (0xffe3d3ad), c (0xffefe9dc), c (0xffbfb6a3),
              c (0xff221f1c), c (0xff151311), c (0xff1f1c19), c (0xff2e2a25), c (0xff6a5635) },
            { "walnut", "Noce e ottone",
              "Tavole di noce venato con finitura a olio e viti in ottone, come una pedaliera artigianale.",
              BoardStyle::Wood, c (0xff5e3c23), c (0xff3a2313), c (0xffc9a15a), c (0xffefdcb8),
              c (0xff120c08), c (0xff3a2618), c (0xff22150d), c (0xffd4a857), c (0xfff1e6d2), c (0xffc8b597),
              c (0xff2a1b11), c (0xff1a100a), c (0xff24170e), c (0xff3a2618), c (0xff7a5226) },
            { "green", "British Green",
              "Tolex verde inglese in tinta con gli amplificatori d'epoca, profilo oro.",
              BoardStyle::Tolex, c (0xff1f3b2b), c (0xff12261a), c (0xffc9a55a), c (0xffd4c9a0),
              c (0xff0b100c), c (0xff1f2e24), c (0xff121b15), c (0xffd2b36a), c (0xffeae7da), c (0xffb6bda9),
              c (0xff18241c), c (0xff0f1712), c (0xff15201a), c (0xff213026), c (0xff2f6b45) },
            { "alu", "Alluminio spazzolato",
              "Lamelle in alluminio naturale spazzolato con fessure scure, accento azzurro acciaio.",
              BoardStyle::BrushedRails, c (0xffa9adb2), c (0xff26292d), c (0xff6f757c), c (0xff202428),
              c (0xff111315), c (0xff2e3236), c (0xff1b1e21), c (0xff6cc7e6), c (0xffe9edf0), c (0xffb3bcc4),
              c (0xff22262a), c (0xff16191c), c (0xff1c2024), c (0xff2c3136), c (0xff2f6f88) },
            { "night", "Studio notte",
              "Moquette scura da studio, minimale e ad alto contrasto (l'aspetto originale).",
              BoardStyle::Carpet, c (0xff1b1c1f), c (0xff0d0d0f), c (0xff2a2b30), c (0xff8a877e),
              c (0xff0d0d0f), c (0xff2b2c30), c (0xff18191c), c (0xffd9b464), c (0xffe8e6df), c (0xffb9b6ac),
              c (0xff202125), c (0xff141517), c (0xff1f2024), c (0xff2a2b2f), c (0xff3d6b45) },
        };
        return t;
    }

    const std::vector<CableColour>& cableColours()
    {
        static const std::vector<CableColour> v = {
            { "Nero", c (0xff141416), c (0xff2a2b30) },
            { "Grafite", c (0xff34363b), c (0xff55585e) },
            { "Rosso vintage", c (0xff4e1411), c (0xff7d2620) },
            { "Blu notte", c (0xff13213a), c (0xff27406a) },
            { "Tweed crema", c (0xff9c8a64), c (0xffcdbd97) },
        };
        return v;
    }

    double contrastRatio (juce::Colour a, juce::Colour b)
    {
        auto lum = [] (juce::Colour x)
        {
            auto ch = [] (juce::uint8 v)
            {
                const double s = v / 255.0;
                return s <= 0.04045 ? s / 12.92 : std::pow ((s + 0.055) / 1.055, 2.4);
            };
            return 0.2126 * ch (x.getRed()) + 0.7152 * ch (x.getGreen()) + 0.0722 * ch (x.getBlue());
        };
        const double la = lum (a), lb = lum (b);
        return (std::max (la, lb) + 0.05) / (std::min (la, lb) + 0.05);
    }

    //==============================================================================
    // texture procedurali ripetibili (senza cuciture)
    namespace
    {
        constexpr int tileSize = 256;

        float hash (int x, int y, int seed)
        {
            uint32_t h = (uint32_t) x * 374761393u + (uint32_t) y * 668265263u + (uint32_t) seed * 2246822519u;
            h = (h ^ (h >> 13)) * 1274126177u;
            return (float) ((h ^ (h >> 16)) & 0xffffff) / (float) 0xffffff;
        }

        /** Rumore a valori interpolato, periodico su 'px' x 'py' celle lungo la tessera. */
        float vnoise (int x, int y, int px, int py, int seed)
        {
            const float fx = (float) x * (float) px / (float) tileSize, fy = (float) y * (float) py / (float) tileSize;
            const int x0 = (int) std::floor (fx), y0 = (int) std::floor (fy);
            float tx = fx - (float) x0, ty = fy - (float) y0;
            tx = tx * tx * (3.0f - 2.0f * tx); ty = ty * ty * (3.0f - 2.0f * ty);
            auto h = [&] (int ix, int iy) { return hash (((ix % px) + px) % px, ((iy % py) + py) % py, seed); };
            const float a = h (x0, y0), b = h (x0 + 1, y0), cc = h (x0, y0 + 1), d = h (x0 + 1, y0 + 1);
            return (a + (b - a) * tx) + ((cc + (d - cc) * tx) - (a + (b - a) * tx)) * ty;
        }

        juce::Colour shade (juce::Colour base, float k)
        {
            return juce::Colour::fromFloatRGBA (juce::jlimit (0.0f, 1.0f, base.getFloatRed() * k),
                                                juce::jlimit (0.0f, 1.0f, base.getFloatGreen() * k),
                                                juce::jlimit (0.0f, 1.0f, base.getFloatBlue() * k), 1.0f);
        }

        enum class Tex { Loop, Brushed, Pebble, Grain, Carpet };

        juce::Image makeTile (Tex kind, juce::Colour base, juce::Colour alt)
        {
            juce::Image img (juce::Image::ARGB, tileSize, tileSize, false);
            juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
            for (int y = 0; y < tileSize; ++y)
                for (int x = 0; x < tileSize; ++x)
                {
                    juce::Colour col;
                    switch (kind)
                    {
                        case Tex::Loop:     // tessuto a strappo: fibre corte e anellini chiari sparsi
                        {
                            const float n = 0.55f * vnoise (x, y, 96, 96, 1) + 0.45f * hash (x, y, 2);
                            float k = 1.0f + (n - 0.5f) * 0.42f;
                            if (hash (x, y, 3) > 0.988f) k += 0.55f;
                            col = shade (base, k);
                            break;
                        }
                        case Tex::Brushed:  // spazzolatura orizzontale
                        {
                            const float n = 0.7f * vnoise (x, y, 4, 160, 4) + 0.3f * hash (x, y, 5);
                            col = shade (base, 1.0f + (n - 0.5f) * 0.16f);
                            break;
                        }
                        case Tex::Pebble:   // tolex goffrato: grani con la sommita' lucida
                        {
                            const float n1 = vnoise (x, y, 64, 64, 6), n2 = vnoise (x, y, 12, 12, 7);
                            float k = 1.0f + (n1 - 0.5f) * 0.55f + (n2 - 0.5f) * 0.18f;
                            if (n1 > 0.72f) k += (n1 - 0.72f) * 1.4f;
                            col = shade (base, k);
                            break;
                        }
                        case Tex::Grain:    // venatura fine del legno lungo x, con pori scuri
                        {
                            const float w = vnoise (x, y, 4, 2, 8) * 1.3f + vnoise (x, y, 16, 8, 9) * 0.35f;
                            const float g = 0.5f + 0.5f * std::sin ((float) y / (float) tileSize * juce::MathConstants<float>::twoPi * 26.0f + w * 2.2f);
                            const float streak = vnoise (x, y, 2, 64, 10);
                            col = alt.interpolatedWith (base, 0.62f + 0.3f * g + 0.08f * streak);
                            float k = 0.97f + hash (x / 4, y, 11) * 0.06f;
                            if (hash (x / 2, y, 12) > 0.992f) k *= 0.7f;          // pori
                            col = shade (col, k);
                            break;
                        }
                        case Tex::Carpet:
                        {
                            const float n = 0.5f * vnoise (x, y, 128, 128, 11) + 0.5f * hash (x, y, 12);
                            col = shade (base, 1.0f + (n - 0.5f) * 0.22f);
                            break;
                        }
                    }
                    bd.setPixelColour (x, y, col);
                }
            return img;
        }

        const juce::Image& tileFor (const Theme& t, Tex kind, juce::Colour base, juce::Colour alt)
        {
            static std::map<juce::String, juce::Image> cache;      // solo thread dei messaggi
            const auto key = t.id + ":" + juce::String ((int) kind);
            auto it = cache.find (key);
            if (it == cache.end()) it = cache.emplace (key, makeTile (kind, base, alt)).first;
            return it->second;
        }

        void fillTexture (juce::Graphics& g, const juce::Image& tile, const juce::Path& area, float scale, juce::Point<float> origin)
        {
            g.setFillType (juce::FillType (tile, juce::AffineTransform::scale (juce::jmax (0.5f, scale)).translated (origin)));
            g.fillPath (area);
        }

        void screw (juce::Graphics& g, juce::Point<float> p, float r, juce::Colour metal)
        {
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.fillEllipse (juce::Rectangle<float> (r * 2.3f, r * 2.3f).withCentre (p.translated (0.0f, r * 0.35f)));
            g.setGradientFill (juce::ColourGradient (metal.brighter (0.7f), p.x - r, p.y - r, metal.darker (0.6f), p.x + r, p.y + r, false));
            g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (p));
            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.drawLine (p.x - r * 0.6f, p.y, p.x + r * 0.6f, p.y, juce::jmax (1.0f, r * 0.28f));
            g.drawLine (p.x, p.y - r * 0.6f, p.x, p.y + r * 0.6f, juce::jmax (1.0f, r * 0.28f));
        }

        void cornerGuard (juce::Graphics& g, juce::Rectangle<float> a, float size, int corner)
        {
            // angolare cromato (0 alto-sx, 1 alto-dx, 2 basso-dx, 3 basso-sx)
            juce::Path p;                       // triangolo con il lato interno incurvato
            p.startNewSubPath (0.0f, 0.0f);
            p.lineTo (size, 0.0f);
            p.quadraticTo (size * 0.35f, size * 0.35f, 0.0f, size);
            p.closeSubPath();
            const float rot = juce::MathConstants<float>::halfPi * (float) corner;
            const juce::Point<float> at = corner == 0 ? a.getTopLeft() : corner == 1 ? a.getTopRight() : corner == 2 ? a.getBottomRight() : a.getBottomLeft();
            const auto tr = juce::AffineTransform::rotation (rot).translated (at);
            g.setColour (juce::Colours::black.withAlpha (0.45f));
            g.fillPath (p, tr.translated (0.0f, size * 0.06f));
            g.setGradientFill (juce::ColourGradient (c (0xffd2d5d9), at.x, at.y, c (0xff4b4e53), at.x + size * 0.7f, at.y + size * 0.7f, false));
            g.fillPath (p, tr);
            for (auto q : { juce::Point<float> (size * 0.17f, size * 0.42f), juce::Point<float> (size * 0.42f, size * 0.17f) })
                screw (g, q.transformedBy (tr), size * 0.07f, c (0xffc9ccd1));
        }
    }

    void paintBoard (juce::Graphics& g, const Theme& t, juce::Rectangle<float> a, float s)
    {
        const float radius = 10.0f * s;
        juce::Path outline;
        outline.addRoundedRectangle (a, radius);

        switch (t.board)
        {
            case BoardStyle::Rails:
            case BoardStyle::BrushedRails:
            {
                const bool brushed = t.board == BoardStyle::BrushedRails;
                // telaio in alluminio anodizzato
                g.setGradientFill (juce::ColourGradient (t.trim.brighter (0.35f), a.getX(), a.getY(), t.trim.darker (0.55f), a.getX(), a.getBottom(), false));
                g.fillPath (outline);
                auto inner = a.reduced (9.0f * s);
                g.setColour (t.boardAlt);
                g.fillRoundedRectangle (inner, radius * 0.6f);
                // lamelle con fessure tra l'una e l'altra
                const float gap = 10.0f * s;
                const int n = juce::jmax (3, juce::roundToInt ((inner.getHeight() + gap) / (70.0f * s + gap)));
                const float h = (inner.getHeight() - gap * (float) (n - 1)) / (float) n;
                const auto& tile = tileFor (t, brushed ? Tex::Brushed : Tex::Loop, t.boardBase, t.boardAlt);
                for (int i = 0; i < n; ++i)
                {
                    const auto r = juce::Rectangle<float> (inner.getX(), inner.getY() + (float) i * (h + gap), inner.getWidth(), h);
                    juce::Path slat;
                    slat.addRoundedRectangle (r, 3.0f * s);
                    fillTexture (g, tile, slat, s, { 0.0f, (float) i * 37.0f });
                    g.setColour (juce::Colours::white.withAlpha (brushed ? 0.35f : 0.07f));
                    g.fillRect (r.withHeight (juce::jmax (1.0f, 1.2f * s)));
                    g.setColour (juce::Colours::black.withAlpha (0.55f));
                    g.fillRect (r.withTop (r.getBottom() - juce::jmax (1.0f, 2.0f * s)));
                    for (float x : { r.getX() + 12.0f * s, r.getRight() - 12.0f * s })
                        screw (g, { x, r.getCentreY() }, 4.2f * s, brushed ? c (0xffb8bcc2) : c (0xff55575d));
                }
                break;
            }
            case BoardStyle::Tolex:
            {
                fillTexture (g, tileFor (t, Tex::Pebble, t.boardBase, t.boardAlt), outline, s, a.getTopLeft());
                // profilo (piping) e leggera vignettatura
                const auto pipe = a.reduced (13.0f * s);
                g.setColour (juce::Colours::black.withAlpha (0.45f));
                g.drawRoundedRectangle (pipe.translated (0.0f, 1.5f * s), radius * 0.7f, 3.4f * s);
                g.setColour (t.trim);
                g.drawRoundedRectangle (pipe, radius * 0.7f, 3.0f * s);
                g.setColour (t.trim.brighter (0.5f).withAlpha (0.6f));
                g.drawRoundedRectangle (pipe.translated (0.0f, -0.8f * s), radius * 0.7f, 0.8f * s);
                juce::ColourGradient vig (juce::Colours::transparentBlack, a.getCentreX(), a.getCentreY(),
                                          juce::Colours::black.withAlpha (0.35f), a.getX(), a.getY(), true);
                g.setGradientFill (vig);
                g.fillPath (outline);
                for (int k = 0; k < 4; ++k) cornerGuard (g, a, 30.0f * s, k);
                break;
            }
            case BoardStyle::Wood:
            {
                const float plank = 118.0f * s;
                const int n = juce::jmax (2, juce::roundToInt (a.getHeight() / plank));
                const float h = a.getHeight() / (float) n;
                const auto& tile = tileFor (t, Tex::Grain, t.boardBase, t.boardAlt);
                g.saveState();
                g.reduceClipRegion (outline);
                for (int i = 0; i < n; ++i)
                {
                    const auto r = juce::Rectangle<float> (a.getX(), a.getY() + (float) i * h, a.getWidth(), h);
                    juce::Path p; p.addRectangle (r);
                    fillTexture (g, tile, p, s * 1.15f, { (float) (i * 173 % 256), (float) (i * 61) });
                    g.setColour (juce::Colours::black.withAlpha (i % 2 == 0 ? 0.05f : 0.0f));
                    g.fillRect (r);
                    g.setColour (juce::Colours::black.withAlpha (0.6f));
                    g.fillRect (r.withHeight (juce::jmax (1.0f, 2.0f * s)));
                    for (float x : { r.getX() + 16.0f * s, r.getRight() - 16.0f * s })
                        screw (g, { x, r.getCentreY() }, 4.0f * s, t.trim);
                }
                // finitura a olio: riflesso morbido in alto, ombra in basso
                juce::ColourGradient sheen (juce::Colours::white.withAlpha (0.07f), a.getX(), a.getY(),
                                            juce::Colours::black.withAlpha (0.18f), a.getX(), a.getBottom(), false);
                g.setGradientFill (sheen);
                g.fillRect (a);
                g.restoreState();
                g.setColour (juce::Colours::black.withAlpha (0.7f));
                g.drawRoundedRectangle (a.reduced (1.0f), radius, 3.0f * s);
                break;
            }
            case BoardStyle::Carpet:
            {
                g.setGradientFill (juce::ColourGradient (t.boardBase, a.getX(), a.getY(), t.boardAlt, a.getX(), a.getBottom(), false));
                g.fillPath (outline);
                g.setOpacity (0.55f);
                fillTexture (g, tileFor (t, Tex::Carpet, t.boardBase, t.boardAlt), outline, s, a.getTopLeft());
                g.setOpacity (1.0f);
                break;
            }
        }
    }

    //==============================================================================
    ThemeManager::ThemeManager()
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "Options";
        o.folderName = "PedalTrinity";
        o.filenameSuffix = ".settings";
        o.osxLibrarySubFolder = "Application Support";
        props = std::make_unique<juce::PropertiesFile> (o);
        const auto id = props->getValue ("theme", "pro");
        for (int i = 0; i < (int) allThemes().size(); ++i)
            if (allThemes()[(size_t) i].id == id) index = i;
        cables = props->getBoolValue ("cables", true);
        cableColour = juce::jlimit (0, (int) cableColours().size() - 1, props->getIntValue ("cableColour", 0));
    }

    ThemeManager::~ThemeManager() { props->saveIfNeeded(); }

    const Theme& ThemeManager::current() const { return allThemes()[(size_t) index]; }
    const CableColour& ThemeManager::cable() const { return cableColours()[(size_t) cableColour]; }

    void ThemeManager::select (int i)
    {
        i = juce::jlimit (0, (int) allThemes().size() - 1, i);
        if (i == index) return;
        index = i;
        save();
    }

    void ThemeManager::preview (int i)
    {
        index = juce::jlimit (0, (int) allThemes().size() - 1, i);
        sendChangeMessage();
    }

    void ThemeManager::setShowCables (bool b) { if (b != cables) { cables = b; save(); } }
    void ThemeManager::setCableColour (int i)
    {
        i = juce::jlimit (0, (int) cableColours().size() - 1, i);
        if (i != cableColour) { cableColour = i; save(); }
    }

    void ThemeManager::save()
    {
        props->setValue ("theme", current().id);
        props->setValue ("cables", cables);
        props->setValue ("cableColour", cableColour);
        props->saveIfNeeded();
        sendChangeMessage();
    }
}
