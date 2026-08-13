#include "sky_state.h"

#include <algorithm>
#include <cmath>

namespace wave {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDegToRad = kPi / 180.0f;

float clamp01(float v) { return std::max(0.0f, std::min(1.0f, v)); }
float smoothstep01(float t) {
    t = clamp01(t);
    return t * t * (3.0f - 2.0f * t);
}

} // namespace

SkyState deriveSkyState(const SunPosition& sun, float cloudCoverPercent, bool cloudCoverPresent) {
    float elevRad = sun.elevationDeg * kDegToRad;
    float azRad = sun.azimuthDeg * kDegToRad;

    // Same "toward this compass bearing" convention used for wave direction
    // elsewhere in core/, just with a vertical (z) component added: z=sin(elevation)
    // reduces to 0 when the sun is on the horizon and 1 at zenith.
    float lightDirX = std::sin(azRad) * std::cos(elevRad);
    float lightDirY = -std::cos(azRad) * std::cos(elevRad);
    float lightDirZ = std::sin(elevRad);

    // Blinn-Phong half-vector against a fixed straight-up view direction
    // (this is a top-down orthographic render) -- recomputed each tick
    // since the light direction is no longer fixed.
    float halfX = lightDirX, halfY = lightDirY, halfZ = lightDirZ + 1.0f;
    float halfLen = std::sqrt(halfX * halfX + halfY * halfY + halfZ * halfZ);
    if (halfLen > 1e-5f) {
        halfX /= halfLen;
        halfY /= halfLen;
        halfZ /= halfLen;
    }

    // Full "day" character above ~15deg elevation, fully "night" at/below
    // -6deg (just past civil twilight), smooth between.
    float dayFactor = smoothstep01((sun.elevationDeg - (-6.0f)) / (15.0f - (-6.0f)));

    // Warmth peaks near the horizon (dawn/dusk gold) and fades both toward
    // high daytime elevation and toward deep night -- a Gaussian bump
    // naturally does this without needing a separate day/night branch.
    constexpr float kWarmthCenterDeg = 2.0f;
    constexpr float kWarmthWidthDeg = 12.0f;
    float horizonDist = sun.elevationDeg - kWarmthCenterDeg;
    float warmth = std::exp(-(horizonDist * horizonDist) / (2.0f * kWarmthWidthDeg * kWarmthWidthDeg));

    float brightnessScale = 0.15f + 0.85f * dayFactor; // never fully black even at night
    float glintScale = dayFactor;

    if (cloudCoverPresent) {
        float cloudFrac = clamp01(cloudCoverPercent / 100.0f);
        glintScale *= (1.0f - 0.85f * cloudFrac);      // overcast: glint mostly gone, not zero
        brightnessScale *= (1.0f - 0.25f * cloudFrac); // overcast: a bit flatter/dimmer overall
    }

    SkyState sky;
    sky.lightDirX = lightDirX;
    sky.lightDirY = lightDirY;
    sky.lightDirZ = lightDirZ;
    sky.halfDirX = halfX;
    sky.halfDirY = halfY;
    sky.halfDirZ = halfZ;
    sky.brightnessScale = brightnessScale;
    sky.warmth = warmth;
    sky.glintScale = glintScale;
    sky.sunElevationDeg = sun.elevationDeg;
    return sky;
}

} // namespace wave
