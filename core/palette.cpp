#include "palette.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace wave {

namespace {

float clamp01(float v) { return std::max(0.0f, std::min(1.0f, v)); }

uint8_t clampByte(float v) { return static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, v))); }

struct Stop {
    float t;
    RGB8 color;
};

// Naturalistic ocean ramp: deep navy trough through sea-blue toward a
// moderate teal -- deliberately stops short of white. Lighting (specular)
// and the foam layer below are what earn the brightest highlights now, so
// the base ramp alone shouldn't already be near-white at high t.
constexpr Stop kStops[] = {
    {0.00f, {5, 12, 35}},
    {0.40f, {8, 40, 92}},
    {0.70f, {22, 96, 138}},
    {1.00f, {58, 140, 156}},
};
constexpr int kStopCount = static_cast<int>(sizeof(kStops) / sizeof(kStops[0]));

RGB8 lerpColor(const RGB8& a, const RGB8& b, float t) {
    return RGB8{
        static_cast<uint8_t>(a.r + (b.r - a.r) * t),
        static_cast<uint8_t>(a.g + (b.g - a.g) * t),
        static_cast<uint8_t>(a.b + (b.b - a.b) * t),
    };
}

RGB8 scaleColor(const RGB8& c, float factor) {
    return RGB8{clampByte(c.r * factor), clampByte(c.g * factor), clampByte(c.b * factor)};
}

RGB8 addWhite(const RGB8& c, float amount) {
    float add = amount * 255.0f;
    return RGB8{clampByte(c.r + add), clampByte(c.g + add), clampByte(c.b + add)};
}

// Tints toward gold/orange proportional to sky.warmth, scaling existing
// channels rather than blending toward a fixed warm colour -- preserves
// the underlying wave detail instead of washing it out.
constexpr float kWarmthRedBoost = 0.35f;
constexpr float kWarmthGreenBoost = 0.12f;
constexpr float kWarmthBlueCut = 0.35f;

RGB8 applyWarmth(const RGB8& c, float warmth) {
    float w = clamp01(warmth);
    return RGB8{
        clampByte(c.r * (1.0f + w * kWarmthRedBoost)),
        clampByte(c.g * (1.0f + w * kWarmthGreenBoost)),
        clampByte(c.b * (1.0f - w * kWarmthBlueCut)),
    };
}

// How steep the artistic slopes read, how tightly the specular highlight
// clings to the light direction, and how much the diffuse term contributes
// -- the *character* of the shading model, not "where's the light right
// now", so these stay fixed even though the light itself no longer is.
constexpr float kNormalStrength = 3.2f;
constexpr float kAmbient = 0.55f;
constexpr float kDiffuseStrength = 0.6f;
constexpr float kShininess = 24.0f;
constexpr float kSpecularStrength = 0.9f;
constexpr float kSparkleStrength = 0.55f;

} // namespace

RGB8 colorize(const FieldSample& sample, float confidence, const SkyState& sky) {
    // --- base colour from height ---
    float t = clamp01((sample.height + 1.2f) / 2.4f);
    int i = 0;
    while (i < kStopCount - 2 && t > kStops[i + 1].t) ++i;
    float localT = clamp01((t - kStops[i].t) / (kStops[i + 1].t - kStops[i].t));
    RGB8 base = lerpColor(kStops[i].color, kStops[i + 1].color, localT);

    // --- fake lighting: height-field normal against the sky's real light direction ---
    float nx = -sample.gradX * kNormalStrength;
    float ny = -sample.gradY * kNormalStrength;
    float nz = 1.0f;
    float invLen = 1.0f / std::sqrt(nx * nx + ny * ny + nz * nz);
    nx *= invLen;
    ny *= invLen;
    nz *= invLen;

    float diffuse = std::max(0.0f, nx * sky.lightDirX + ny * sky.lightDirY + nz * sky.lightDirZ);
    float specDot = std::max(0.0f, nx * sky.halfDirX + ny * sky.halfDirY + nz * sky.halfDirZ);
    float specular = std::pow(specDot, kShininess);

    // Ambient and diffuse both scale with overall sky brightness -- night
    // dims everything, not just the glint.
    RGB8 c = scaleColor(base, (kAmbient + diffuse * kDiffuseStrength) * sky.brightnessScale);
    c = addWhite(c, specular * kSpecularStrength * sky.glintScale);

    // --- foam: irregular, thresholded, not a smooth gradient to white ---
    if (sample.foamAmount > 0.0f) {
        c = lerpColor(c, RGB8{232, 244, 240}, clamp01(sample.foamAmount));
    }

    // --- sparkle: fine twinkle, gated so it only shows on already-lit facets ---
    float sparkleGate = clamp01(diffuse + specular);
    c = addWhite(c, sample.sparkle * sparkleGate * kSparkleStrength * sky.glintScale);

    // --- warmth: dawn/dusk gold, cool at midday, cool-dark at night ---
    c = applyWarmth(c, sky.warmth);

    // --- confidence: desaturate toward neutral steel-blue-grey when stale ---
    float conf = clamp01(confidence);
    if (conf < 1.0f) {
        uint8_t grey = static_cast<uint8_t>((c.r + c.g + c.b) / 3.0f * 0.6f);
        RGB8 neutral{grey, static_cast<uint8_t>(grey * 1.05f), static_cast<uint8_t>(grey * 1.15f)};
        c = lerpColor(neutral, c, conf);
    }
    return c;
}

} // namespace wave
