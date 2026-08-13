#pragma once

#include "sun_position.h"

namespace wave {

// Per-tick (not per-pixel) lighting state derived from real sun position
// and, when available, live cloud cover. Computed once per
// WaveEngine::tick() and reused across every pixel in that frame -- no
// added per-pixel cost versus the fixed constants this replaced.
struct SkyState {
    float lightDirX = 0.0f;
    float lightDirY = 0.0f;
    float lightDirZ = 1.0f;
    float halfDirX = 0.0f;
    float halfDirY = 0.0f;
    float halfDirZ = 1.0f;
    float brightnessScale = 1.0f; // overall ambient/diffuse multiplier
    float warmth = 0.0f;          // 0..1, blends the palette toward gold/orange
    float glintScale = 1.0f;      // 0..1, multiplies specular + sparkle strength

    float sunElevationDeg = 0.0f; // diagnostic only -- not used by the shading math directly
};

// cloudCoverPresent=false is treated as "no live sky data" -- brightness
// and glint are still shaped by sun position alone, just without the
// overcast-dulling term.
SkyState deriveSkyState(const SunPosition& sun, float cloudCoverPercent, bool cloudCoverPresent);

} // namespace wave
