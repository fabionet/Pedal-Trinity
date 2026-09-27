// File GENERATO da tools/art/assemble.py - non modificare a mano.
// Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3
#pragma once

namespace pt::ui
{
    inline constexpr float renderScale = 2.000f;     // i fotogrammi sono renderizzati a 2x delle coordinate logiche
    enum StripId { tsBig, tsSmall, boss, bossOuter, bossInner, sliderCap, toggle, numStrips };

    /** Filmstrip: dimensioni del fotogramma e punto d'ancoraggio in pixel del render (2x). */
    struct Strip { const char* resource; int frameW, frameH, cols, frames; float anchorX, anchorY; };
    inline constexpr Strip strips[numStrips] =
    {
        { "strip_ts_big_png", 271, 230, 8, 64, 135.676f, 113.500f },
        { "strip_ts_small_png", 232, 197, 8, 64, 114.618f, 97.500f },
        { "strip_boss_png", 234, 198, 8, 64, 116.559f, 98.500f },
        { "strip_boss_outer_png", 208, 180, 8, 64, 104.500f, 89.500f },
        { "strip_boss_inner_png", 162, 179, 8, 64, 50.441f, 63.057f },
        { "strip_slider_png", 108, 114, 1, 1, 54.382f, 56.500f },
        { "strip_toggle_png", 177, 128, 2, 2, 58.324f, 65.500f },
    };
}
