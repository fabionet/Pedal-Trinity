// File GENERATO da tools/art/assemble.py - non modificare a mano.
// Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3
#pragma once

namespace pt::ui
{
    inline constexpr float uiWidth = 1200.000f, uiHeight = 624.500f, renderScale = 2.000f;

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

    /** Pomello: ancoraggio (base), centro della sommita', raggio di presa (px @1x). */
    struct Knob { const char* paramId; StripId strip; float ax, ay, tx, ty, radius; };
    inline constexpr Knob knobs[] =
    {
        { "od_drive", tsBig, 162.353f, 195.389f, 162.353f, 174.235f, 33.882f },
        { "od_level", tsBig, 296.471f, 195.389f, 296.471f, 174.235f, 33.882f },
        { "od_tone", tsSmall, 229.412f, 247.748f, 229.412f, 228.577f, 26.824f },
        { "dist_level", boss, 510.000f, 190.101f, 510.000f, 170.269f, 25.765f },
        { "dist_low", bossOuter, 570.000f, 190.101f, 570.000f, 176.879f, 27.882f },
        { "dist_high", bossInner, 570.000f, 176.879f, 570.000f, 166.963f, 17.294f },
        { "dist_mid", bossOuter, 630.000f, 190.101f, 630.000f, 176.879f, 27.882f },
        { "dist_midfreq", bossInner, 630.000f, 176.879f, 630.000f, 166.963f, 17.294f },
        { "dist_gain", boss, 690.000f, 190.101f, 690.000f, 170.269f, 25.765f },
    };

    /** Cursore del GQ-7: ancoraggio del cappuccio al minimo e al massimo. */
    struct Fader { const char* paramId; float x, yMin, yMax; };
    inline constexpr Fader faders[] =
    {
        { "eq_0", 874.235f, 226.097f, 134.470f },
        { "eq_1", 901.765f, 226.097f, 134.470f },
        { "eq_2", 929.294f, 226.097f, 134.470f },
        { "eq_3", 956.824f, 226.097f, 134.470f },
        { "eq_4", 984.353f, 226.097f, 134.470f },
        { "eq_5", 1011.882f, 226.097f, 134.470f },
        { "eq_6", 1039.412f, 226.097f, 134.470f },
        { "eq_level", 1066.941f, 226.097f, 134.470f },
    };

    struct Led { const char* pedal; float x, y, radius; };
    inline constexpr Led leds[] =
    {
        { "ed9", 229.412f, 140.122f, 9.176f },
        { "mc2", 540.000f, 116.835f, 8.471f },
        { "gq7", 1064.118f, 276.289f, 8.471f },
    };

    struct Footswitch { const char* pedal; float x[4], y[4]; };
    inline constexpr Footswitch footswitches[] =
    {
        { "ed9", { 141.176f, 317.647f, 317.647f, 141.176f }, { 478.007f, 478.007f, 353.656f, 353.656f } },
        { "mc2", { 471.176f, 728.824f, 728.824f, 471.176f }, { 537.488f, 537.488f, 259.515f, 259.515f } },
        { "gq7", { 841.765f, 1099.412f, 1099.412f, 841.765f }, { 537.488f, 537.488f, 259.515f, 259.515f } },
    };

    inline constexpr float modeToggleX = 660.000f, modeToggleY = 119.744f;
    inline constexpr float infoX = 1104.706f, infoY = 54.360f, infoRadius = 16.941f;
}
