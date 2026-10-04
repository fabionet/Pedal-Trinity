/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)
*/

#include "Theme.h"
#include "RealMod.h"
#include "../engine/NamSecurity.h"
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
            { "redtolex", "Tolex rosso",
              "Tolex rosso ciliegia come i combo degli anni '60, profilo crema e angolari cromati.",
              BoardStyle::Tolex, c (0xff6e1616), c (0xff3c0a0a), c (0xffe6d6b0), c (0xfff0dcc0),
              c (0xff120c0b), c (0xff33201d), c (0xff1c1210), c (0xffe8b04a), c (0xffefe7dc), c (0xffc2b3a6),
              c (0xff261816), c (0xff170e0d), c (0xff21140f), c (0xff35231f), c (0xff7a2a22) },
            { "tweed", "Tweed '59",
              "Tela tweed a spina di pesce con profilo in cuoio scuro, come gli amplificatori di fine anni '50.",
              BoardStyle::Tweed, c (0xffc9b48a), c (0xff6e5a3a), c (0xff3a2a1a), c (0xff2a1e12),
              c (0xff14100b), c (0xff3a2d1e), c (0xff21190f), c (0xffe0b66a), c (0xfff1e8d6), c (0xffc9bb9f),
              c (0xff2b2115), c (0xff1a140c), c (0xff251c11), c (0xff3a2d1e), c (0xff6e5226) },
            { "diamond", "Lamiera mandorlata",
              "Lamiera d'alluminio antiscivolo a mandorle in rilievo, viti a vista: la pedana da palco.",
              BoardStyle::DiamondPlate, c (0xffa3a8ad), c (0xff5c6166), c (0xff2c2f33), c (0xff15181b),
              c (0xff0f1012), c (0xff2d3034), c (0xff191b1e), c (0xfff2a33a), c (0xffeceeef), c (0xffb7bcc1),
              c (0xff212327), c (0xff151719), c (0xff1c1e21), c (0xff2c2f34), c (0xff8a4f12) },
            { "carbon", "Fibra di carbonio",
              "Trama di carbonio 2x2 sotto vernice lucida, profilo in alluminio scuro e accento rosso.",
              BoardStyle::Carbon, c (0xff24272a), c (0xff0b0c0d), c (0xff3a3d42), c (0xffa0a6ad),
              c (0xff0b0c0d), c (0xff26282c), c (0xff141518), c (0xffe5484d), c (0xffeceeef), c (0xffb4b9bf),
              c (0xff1c1e21), c (0xff111214), c (0xff1a1b1e), c (0xff292b2f), c (0xff8a2226) },
            { "maple", "Acero chiaro",
              "Tavole d'acero chiaro con venatura fine, finitura satinata e viti brunite.",
              BoardStyle::Wood, c (0xffc9a273), c (0xff9a7348), c (0xff6a5638), c (0xff2a1a0a),
              c (0xff15100a), c (0xff3b2c1c), c (0xff21180f), c (0xffe3b26b), c (0xfff2e9da), c (0xffcbbca4),
              c (0xff2c2115), c (0xff1b140c), c (0xff261d12), c (0xff3b2c1c), c (0xff7a5a2a) },
            { "ebony", "Ebano e argento",
              "Ebano quasi nero con riflessi caldi e viti cromate, eleganza da liuteria.",
              BoardStyle::Wood, c (0xff2e221c), c (0xff110b08), c (0xffc0c4c8), c (0xffcfc5bb),
              c (0xff0d0a09), c (0xff2a2320), c (0xff171210), c (0xffcfd6de), c (0xffefebe6), c (0xffbdb6ae),
              c (0xff1f1916), c (0xff130f0d), c (0xff1b1613), c (0xff2c2522), c (0xff4a5560) },
            { "surf", "Surf blu",
              "Tolex blu surf con profilo bianco, il colore delle tavole e delle chitarre californiane.",
              BoardStyle::Tolex, c (0xff1d3f6e), c (0xff0f2646), c (0xffeae4d4), c (0xffe6eef8),
              c (0xff0a0f16), c (0xff1f2c3d), c (0xff111a25), c (0xff7cc4ff), c (0xffe9eef4), c (0xffb3c0cf),
              c (0xff17212e), c (0xff0e151e), c (0xff142030), c (0xff22324a), c (0xff245a8a) },
            { "purple", "Velluto viola",
              "Moquette di velluto viola da sala prove, morbida e scura, accento lilla.",
              BoardStyle::Carpet, c (0xff3d2152), c (0xff1d0e29), c (0xff5a3a70), c (0xffdccbea),
              c (0xff0f0a13), c (0xff2c2034), c (0xff19121e), c (0xffc890ff), c (0xffefe9f4), c (0xffbfb3ca),
              c (0xff221a29), c (0xff151018), c (0xff1e1624), c (0xff2f2438), c (0xff5a2f80) },
            { "slate", "Ardesia",
              "Lastra d'ardesia grigio scuro con venature chiare, bordo smussato e accento verde acqua.",
              BoardStyle::Stone, c (0xff3d4247), c (0xff23272b), c (0xff6b7178), c (0xffd6dade),
              c (0xff0d0f10), c (0xff292d31), c (0xff16191b), c (0xff8fd0c0), c (0xffe8ecee), c (0xffb3bcc1),
              c (0xff1e2225), c (0xff131618), c (0xff1a1e21), c (0xff2a2f33), c (0xff2f6b62) },
            { "goldrails", "Guide oro",
              "Lamelle in alluminio anodizzato oro spazzolato con fessure scure: il palco dei grandi tour.",
              BoardStyle::BrushedRails, c (0xffc9a656), c (0xff2a2216), c (0xff8a6d2c), c (0xff1e1608),
              c (0xff120f09), c (0xff332a1b), c (0xff1d1810), c (0xffe8c66a), c (0xfff1e9d5), c (0xffc8bb9c),
              c (0xff272015), c (0xff18130c), c (0xff221c12), c (0xff352c1d), c (0xff6e5520) },
            { "white", "Tolex bianco",
              "Tolex bianco avorio con profilo oro e angolari cromati, come i combo di lusso d'epoca.",
              BoardStyle::Tolex, c (0xffe2ddd2), c (0xffb7b0a1), c (0xffc9a55a), c (0xff2b2925),
              c (0xff121212), c (0xff2f2e2c), c (0xff1a1918), c (0xffd9b464), c (0xffeeece7), c (0xffbcb8b0),
              c (0xff232221), c (0xff161515), c (0xff1e1d1c), c (0xff2f2e2c), c (0xff5c5338) },
            { "neon", "Neon notte",
              "Moquette nera con riflessi viola e accento magenta al neon, atmosfera da club.",
              BoardStyle::Carpet, c (0xff181225), c (0xff07050d), c (0xff2a2140), c (0xffb8a8ff),
              c (0xff09070e), c (0xff241d33), c (0xff120e1b), c (0xffff4fd8), c (0xffefeaf7), c (0xffbcb4cc),
              c (0xff1b1626), c (0xff100c17), c (0xff181322), c (0xff2a2238), c (0xff7a1f68) },
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
            { "Rosso fuoco", c (0xffa3191c), c (0xffd8383a) },
            { "Arancio", c (0xffb85a10), c (0xffe98a35) },
            { "Giallo", c (0xffb39212), c (0xffe8c63a) },
            { "Verde lime", c (0xff4f8a14), c (0xff7cc23a) },
            { "Verde bosco", c (0xff1d4a2a), c (0xff336e45) },
            { "Azzurro", c (0xff1a6ea8), c (0xff45a1dc) },
            { "Viola", c (0xff4b2a7a), c (0xff7550ad) },
            { "Rosa", c (0xffb0457f), c (0xffe07ab0) },
            { "Bianco", c (0xffc9c9c6), c (0xfff2f2ef) },
            { "Oro", c (0xff8f7128), c (0xffd1ad55) },
            { "Argento", c (0xff7d8186), c (0xffc3c7cc) },
            { "Tessuto nero e oro", c (0xff1a1714), c (0xff8a7038) },
            { "Multicolore", c (0xff141416), c (0xff2a2b30) },
        };
        return v;
    }

    int multiCableIndex() { return (int) cableColours().size() - 1; }

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

    juce::Colour inkFor (juce::Colour board)
    {
        const juce::Colour dark (0xff111111), light (0xffeeeeee);
        return contrastRatio (dark, board) > contrastRatio (light, board) ? dark : light;
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

        enum class Tex { Loop, Brushed, Pebble, Grain, Carpet, Tweed, Diamond, Carbon, Stone };

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
                            const float amp = base.getPerceivedBrightness() > 0.6f ? 0.4f : 1.0f;    // tolex chiaro: grana piu' tenue
                            float k = 1.0f + ((n1 - 0.5f) * 0.55f + (n2 - 0.5f) * 0.18f) * amp;
                            if (n1 > 0.72f) k += (n1 - 0.72f) * 1.4f * amp;
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
                        case Tex::Tweed:    // tela a spina di pesce: fili diagonali chiari e scuri che cambiano verso a bande
                        {
                            const int band = (x / 16) % 2;
                            const int d = band == 0 ? (x + y) : (x - y + tileSize);
                            const float twill = 0.5f + 0.5f * std::sin ((float) d * juce::MathConstants<float>::twoPi / 6.0f);
                            const float fleck = hash (x, y, 13);
                            col = alt.interpolatedWith (base, 0.35f + 0.5f * twill + 0.15f * vnoise (x, y, 32, 32, 14));
                            if (fleck > 0.985f) col = col.darker (0.6f);
                            col = shade (col, 0.94f + 0.12f * hash (x / 2, y / 2, 15));
                            break;
                        }
                        case Tex::Diamond:  // lamiera mandorlata: mandorle in rilievo alternate a 90 gradi
                        {
                            constexpr int cell = 32;
                            const int cx = x / cell, cy = y / cell;
                            const float u0 = (float) (x % cell) - cell * 0.5f, v0 = (float) (y % cell) - cell * 0.5f;
                            const float sgn = (cx + cy) % 2 == 0 ? 1.0f : -1.0f;
                            const float u = (u0 + sgn * v0) * 0.7071f, v = (v0 - sgn * u0) * 0.7071f;
                            const float e = (u * u) / (11.0f * 11.0f) + (v * v) / (3.2f * 3.2f);
                            float k = 1.0f + (vnoise (x, y, 4, 128, 16) - 0.5f) * 0.12f;          // spazzolatura di fondo
                            if (e < 1.0f)
                            {
                                const float lit = juce::jlimit (-1.0f, 1.0f, -(u * 0.08f + v * 0.35f) * sgn);
                                k += 0.18f + 0.22f * lit * (1.0f - e);
                            }
                            else if (e < 1.35f) k -= 0.16f;                                        // ombra del bordo
                            col = shade (base, k);
                            break;
                        }
                        case Tex::Carbon:   // trama 2x2 di fibra: ogni quadretto riflette la luce in un verso
                        {
                            constexpr int cell = 8;
                            const int cx = x / cell, cy = y / cell;
                            const bool horiz = ((cx + cy / 2) % 2) == 0;
                            const float along = (float) (horiz ? y % cell : x % cell) / (float) cell;
                            const float sheen = std::sin (along * juce::MathConstants<float>::pi);
                            const float fibre = 0.9f + 0.1f * hash (horiz ? x : x / 3, horiz ? y / 3 : y, 17);
                            col = alt.interpolatedWith (base, juce::jlimit (0.0f, 1.0f, (horiz ? 0.35f : 0.75f) * sheen * fibre + 0.15f));
                            break;
                        }
                        case Tex::Stone:    // ardesia: strati orizzontali sottili, grana fine e qualche scaglia chiara
                        {
                            const float layers = vnoise (x, y, 3, 40, 18) * 0.6f + vnoise (x, y, 9, 90, 19) * 0.4f;
                            const float grain = 0.6f * vnoise (x, y, 64, 64, 20) + 0.4f * hash (x, y, 21);
                            col = alt.interpolatedWith (base, juce::jlimit (0.0f, 1.0f, 0.3f + layers * 0.6f + (grain - 0.5f) * 0.25f));
                            if (hash (x / 3, y, 22) > 0.997f) col = col.interpolatedWith (juce::Colour (0xffd8dde0), 0.25f);
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
            juce::ignoreUnused (t);
            const auto key = juce::String ((int) kind) + ":" + base.toString() + ":" + alt.toString();
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
            case BoardStyle::Tweed:
            {
                fillTexture (g, tileFor (t, Tex::Tweed, t.boardBase, t.boardAlt), outline, s, a.getTopLeft());
                // profilo in cuoio scuro con cucitura chiara
                const auto pipe = a.reduced (11.0f * s);
                g.setColour (juce::Colours::black.withAlpha (0.4f));
                g.drawRoundedRectangle (pipe.translated (0.0f, 1.6f * s), radius * 0.7f, 5.0f * s);
                g.setColour (t.trim);
                g.drawRoundedRectangle (pipe, radius * 0.7f, 4.6f * s);
                const float dash[] = { 5.0f * s, 4.0f * s };
                juce::Path stitch, sp;
                stitch.addRoundedRectangle (pipe, radius * 0.7f);
                juce::PathStrokeType (0.9f * s).createDashedStroke (sp, stitch, dash, 2);
                g.setColour (t.boardBase.brighter (0.4f).withAlpha (0.7f));
                g.fillPath (sp);
                juce::ColourGradient vig (juce::Colours::transparentBlack, a.getCentreX(), a.getCentreY(),
                                          juce::Colours::black.withAlpha (0.38f), a.getX(), a.getY(), true);
                g.setGradientFill (vig);
                g.fillPath (outline);
                g.setColour (t.trim.darker (0.5f));
                g.drawRoundedRectangle (a.reduced (1.0f), radius, 3.0f * s);
                break;
            }
            case BoardStyle::DiamondPlate:
            {
                g.setColour (t.trim);
                g.fillPath (outline);
                const auto inner = a.reduced (7.0f * s);
                juce::Path ip;
                ip.addRoundedRectangle (inner, radius * 0.6f);
                fillTexture (g, tileFor (t, Tex::Diamond, t.boardBase, t.boardAlt), ip, s * 0.9f, inner.getTopLeft());
                // riflesso diagonale del metallo e bordo piegato
                juce::ColourGradient glare (juce::Colours::white.withAlpha (0.16f), inner.getX(), inner.getY(),
                                            juce::Colours::black.withAlpha (0.22f), inner.getRight(), inner.getBottom(), false);
                glare.addColour (0.45, juce::Colours::transparentWhite);
                g.setGradientFill (glare);
                g.fillPath (ip);
                g.setColour (juce::Colours::white.withAlpha (0.35f));
                g.drawRoundedRectangle (inner.reduced (0.5f), radius * 0.6f, juce::jmax (1.0f, 1.2f * s));
                for (auto p : { inner.getTopLeft(), inner.getTopRight(), inner.getBottomLeft(), inner.getBottomRight() })
                    screw (g, p + juce::Point<float> (p.x < inner.getCentreX() ? 14.0f * s : -14.0f * s, p.y < inner.getCentreY() ? 14.0f * s : -14.0f * s),
                           5.0f * s, t.boardBase.brighter (0.2f));
                break;
            }
            case BoardStyle::Carbon:
            {
                g.setGradientFill (juce::ColourGradient (t.trim.brighter (0.3f), a.getX(), a.getY(), t.trim.darker (0.6f), a.getX(), a.getBottom(), false));
                g.fillPath (outline);
                const auto inner = a.reduced (6.0f * s);
                juce::Path ip;
                ip.addRoundedRectangle (inner, radius * 0.7f);
                fillTexture (g, tileFor (t, Tex::Carbon, t.boardBase, t.boardAlt), ip, s * 1.1f, inner.getTopLeft());
                // vernice trasparente lucida: riflesso largo in alto
                juce::ColourGradient clear (juce::Colours::white.withAlpha (0.14f), inner.getX(), inner.getY(),
                                            juce::Colours::transparentWhite, inner.getX(), inner.getY() + inner.getHeight() * 0.45f, false);
                g.setGradientFill (clear);
                g.fillPath (ip);
                g.setColour (juce::Colours::black.withAlpha (0.6f));
                g.drawRoundedRectangle (inner, radius * 0.7f, 1.4f * s);
                break;
            }
            case BoardStyle::Stone:
            {
                fillTexture (g, tileFor (t, Tex::Stone, t.boardBase, t.boardAlt), outline, s * 1.4f, a.getTopLeft());
                // bordo smussato: luce in alto a sinistra, ombra in basso a destra
                const float bev = 7.0f * s;
                g.setColour (juce::Colours::white.withAlpha (0.12f));
                g.drawRoundedRectangle (a.reduced (bev * 0.5f).translated (-1.0f * s, -1.0f * s), radius, bev * 0.5f);
                g.setColour (juce::Colours::black.withAlpha (0.35f));
                g.drawRoundedRectangle (a.reduced (bev * 0.5f).translated (1.0f * s, 1.5f * s), radius, bev * 0.4f);
                juce::ColourGradient vig (juce::Colours::transparentBlack, a.getCentreX(), a.getCentreY(),
                                          juce::Colours::black.withAlpha (0.3f), a.getX(), a.getY(), true);
                g.setGradientFill (vig);
                g.fillPath (outline);
                g.setColour (juce::Colours::black.withAlpha (0.7f));
                g.drawRoundedRectangle (a.reduced (1.0f), radius, 2.0f * s);
                break;
            }
        }
    }

    void paintBoardImage (juce::Graphics& g, const juce::Image& img, juce::Rectangle<float> a, float s)
    {
        const float radius = 10.0f * s;
        juce::Path outline;
        outline.addRoundedRectangle (a, radius);
        {
            juce::Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (outline);
            g.setOpacity (1.0f);
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            // riempie l'area mantenendo le proporzioni (taglia l'eccesso)
            g.drawImage (img, a, juce::RectanglePlacement::centred | juce::RectanglePlacement::fillDestination);
            juce::ColourGradient vig (juce::Colours::transparentBlack, a.getCentreX(), a.getCentreY(),
                                      juce::Colours::black.withAlpha (0.35f), a.getX(), a.getY(), true);
            g.setGradientFill (vig);
            g.fillPath (outline);
        }
        g.setColour (juce::Colours::black.withAlpha (0.75f));
        g.drawRoundedRectangle (a.reduced (1.0f), radius, 3.0f * s);
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
        real = props->getBoolValue ("realMod", false);
        realListName = props->getValue ("realList", {});
        if (RealPhotos::checkListName (realListName).isNotEmpty()) realListName = {};
        boardSrc = props->getValue ("board", {});
        const auto tintText = props->getValue ("boardTint", {});
        tint = tintText.isNotEmpty() ? juce::Colour::fromString (tintText).withAlpha (1.0f) : juce::Colours::transparentBlack;
        boardImagePath = juce::File (props->getValue ("boardImage", {}));
        if (boardSrc == "image") loadBoardImage();
    }

    ThemeManager::~ThemeManager() { props->saveIfNeeded(); }

    const Theme& ThemeManager::current() const { return allThemes()[(size_t) index]; }
    const CableColour& ThemeManager::cable() const { return cableColours()[(size_t) cableColour]; }

    const CableColour& ThemeManager::cableFor (int k) const
    {
        if (cableColour != multiCableIndex()) return cable();
        // multicolore: si alternano i colori vivaci (salta neri, grigi e il tessuto)
        static const int bright[] = { 5, 10, 7, 8, 12, 6, 11, 14, 4, 13 };
        constexpr int nb = (int) (sizeof (bright) / sizeof (bright[0]));
        return cableColours()[(size_t) bright[((k % nb) + nb) % nb]];
    }

    void ThemeManager::previewCableColour (int i)
    {
        cableColour = juce::jlimit (0, (int) cableColours().size() - 1, i);
        sendChangeMessage();
    }

    void ThemeManager::previewBoardSource (const juce::String& src)
    {
        boardSrc = src == "image" ? juce::String() : src;
        sendChangeMessage();
    }

    void ThemeManager::setBoardSource (const juce::String& src)
    {
        if (src == boardSrc) return;
        boardSrc = src;
        if (boardSrc == "image") loadBoardImage();
        save();
    }

    void ThemeManager::setBoardTint (juce::Colour col)
    {
        if (col == tint) return;
        tint = col;
        save();
    }

    juce::String ThemeManager::setBoardImage (const juce::File& f)
    {
        if (! pt::namsafe::isSafeLocalFile (f.getFullPathName(), 25 * 1024 * 1024)) return "file non ammesso (solo file locali, massimo 25 MB)";
        int w = 0, h = 0;
        if (auto err = RealPhotos::checkImageFile (f, w, h); err.isNotEmpty()) return err;
        const auto img = juce::ImageFileFormat::loadFrom (f);
        if (! img.isValid() || img.getWidth() != w || img.getHeight() != h) return "immagine non decodificabile";
        boardImagePath = f;
        boardImg = img;
        boardSrc = "image";
        save();
        return {};
    }

    void ThemeManager::loadBoardImage()
    {
        boardImg = {};
        int w = 0, h = 0;
        if (boardImagePath.getFullPathName().isEmpty()
            || ! pt::namsafe::isSafeLocalFile (boardImagePath.getFullPathName(), 25 * 1024 * 1024)
            || RealPhotos::checkImageFile (boardImagePath, w, h).isNotEmpty()) return;
        const auto img = juce::ImageFileFormat::loadFrom (boardImagePath);
        if (img.isValid() && img.getWidth() == w && img.getHeight() == h) boardImg = img;
    }

    Theme ThemeManager::boardTheme() const
    {
        Theme t = current();
        if (boardSrc.isNotEmpty() && boardSrc != "image")
            for (const auto& o : allThemes())
                if (o.id == boardSrc)
                {
                    t.board = o.board; t.boardBase = o.boardBase; t.boardAlt = o.boardAlt; t.trim = o.trim; t.ink = o.ink;
                }
        if (! tint.isTransparent())
        {
            // stesso materiale nel colore scelto: il secondario resta proporzionato all'originale
            const float ratio = juce::jlimit (0.05f, 1.0f, (t.boardAlt.getPerceivedBrightness() + 0.02f) / (t.boardBase.getPerceivedBrightness() + 0.02f));
            t.boardBase = tint;
            t.boardAlt = tint.withMultipliedBrightness (ratio);
            t.ink = inkFor (tint);
        }
        t.id = boardKey();
        return t;
    }

    juce::String ThemeManager::boardKey() const
    {
        return current().id + "|" + boardSrc + "|" + (tint.isTransparent() ? juce::String() : tint.toString())
               + (boardSrc == "image" ? "|" + boardImagePath.getFullPathName() : juce::String());
    }

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

    void ThemeManager::setRealMode (bool b) { if (b != real) { real = b; save(); } }
    void ThemeManager::previewRealMode (bool b) { if (b != real) { real = b; sendChangeMessage(); } }
    void ThemeManager::setRealList (const juce::String& l)
    {
        const auto v = RealPhotos::checkListName (l).isEmpty() ? l : juce::String();
        if (v != realListName) { realListName = v; save(); }
    }

    void ThemeManager::save()
    {
        props->setValue ("theme", current().id);
        props->setValue ("cables", cables);
        props->setValue ("cableColour", cableColour);
        props->setValue ("realMod", real);
        props->setValue ("realList", realListName);
        props->setValue ("board", boardSrc);
        props->setValue ("boardTint", tint.isTransparent() ? juce::String() : tint.toString());
        props->setValue ("boardImage", boardImagePath.getFullPathName());
        props->saveIfNeeded();
        sendChangeMessage();
    }
}
