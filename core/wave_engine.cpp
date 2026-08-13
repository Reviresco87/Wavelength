#include "wave_engine.h"

#include <algorithm>
#include <cmath>

#include "field_sample.h"
#include "palette.h"
#include "sun_position.h"

namespace wave {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDegToRad = kPi / 180.0f;

// How each derived component's direction/period/amplitude relates to the
// primary reading -- fixed per slot index so identity is stable across
// ingests (slot 0 is always "the primary", slot 1 always "+0.5 spread", etc.),
// which lets prev/target blending match components up by index.
constexpr float kSpreadFractions[kComponentCount] = {0.0f, 0.5f, -0.5f, 1.0f, -1.0f};
constexpr float kAmplitudeWeights[kComponentCount] = {1.0f, 0.55f, 0.55f, 0.30f, 0.30f};
constexpr float kPeriodJitter[kComponentCount] = {1.0f, 1.08f, 0.93f, 1.15f, 0.88f};
constexpr float kWeightSum = 1.0f + 0.55f + 0.55f + 0.30f + 0.30f;

// How much of the buoy's reported Hs becomes on-screen amplitude. Tuned so
// a typical sheltered-bay sea (Hs ~1-2m) sits comfortably mid-ramp and a
// storm (Hs ~4-5m+) reads as maxed-out/clipped, which is the right feel.
constexpr float kArtisticAmplitudeScale = 0.65f;

// Real wavelength (deep-water dispersion, lambda = 1.56 * T^2) doesn't fit a
// 64px panel -- a 20s groundswell is 600+ metres. Rescale the plausible
// period range onto a chosen on-screen wavelength range instead of trying
// to be physically-to-scale.
float wavelengthFromPeriod(float periodSeconds) {
    float physicalMetres = 1.56f * periodSeconds * periodSeconds;
    constexpr float physicalMin = 1.56f * 3.0f * 3.0f;
    constexpr float physicalMax = 1.56f * 20.0f * 20.0f;
    float t = (physicalMetres - physicalMin) / (physicalMax - physicalMin);
    t = std::max(0.0f, std::min(1.0f, t));
    return 6.0f + t * (40.0f - 6.0f);
}

float clamp01(float v) { return std::max(0.0f, std::min(1.0f, v)); }

float smoothstep(float t) { return t * t * (3.0f - 2.0f * t); }

float lerp(float a, float b, float t) { return a + (b - a) * t; }

// Shortest-arc interpolation between two angles in degrees, wrapping at
// 0/360 -- a linear lerp would spin the wrong way whenever a transition
// crosses due north.
float lerpAngleDeg(float aDeg, float bDeg, float t) {
    float diff = std::fmod(bDeg - aDeg + 180.0f, 360.0f);
    if (diff < 0.0f) diff += 360.0f;
    diff -= 180.0f;
    return aDeg + diff * t;
}

float lerpAngleRad(float aRad, float bRad, float t) {
    float diff = std::fmod(bRad - aRad + kPi, 2.0f * kPi);
    if (diff < 0.0f) diff += 2.0f * kPi;
    diff -= kPi;
    return aRad + diff * t;
}

BuoyReading blendReading(const BuoyReading& a, const BuoyReading& b, float t) {
    BuoyReading r;
    r.timestampUnix = b.timestampUnix;
    r.hsMetres = lerp(a.hsMetres, b.hsMetres, t);
    r.tpSeconds = lerp(a.tpSeconds, b.tpSeconds, t);
    r.mwdDeg = std::fmod(lerpAngleDeg(a.mwdDeg, b.mwdDeg, t) + 360.0f, 360.0f);
    r.spreadDeg = lerp(a.spreadDeg, b.spreadDeg, t);
    r.seaTempC = lerp(a.seaTempC, b.seaTempC, t);
    r.cloudCoverPercent = lerp(a.cloudCoverPercent, b.cloudCoverPercent, t);
    r.cloudCoverPresent = a.cloudCoverPresent || b.cloudCoverPresent;
    r.valid = a.valid || b.valid;
    return r;
}

WaveComponent blendSpec(const WaveComponent& a, const WaveComponent& b, float t) {
    WaveComponent s;
    s.amplitude = lerp(a.amplitude, b.amplitude, t);
    s.wavelengthPx = lerp(a.wavelengthPx, b.wavelengthPx, t);
    s.directionRad = lerpAngleRad(a.directionRad, b.directionRad, t);
    s.angularFreq = lerp(a.angularFreq, b.angularFreq, t);
    return s;
}

// Builds one component directly from a real, physically-distinct wave train
// (e.g. Copernicus's primary swell or wind-sea partition) -- no jitter, this
// train's own reported height/period/direction are used as-is.
WaveComponent componentFromPartition(const WavePartition& p) {
    WaveComponent c;
    float periodS = std::max(0.5f, p.tpSeconds);
    float travelBearingDeg = std::fmod(p.dirDeg + 180.0f + 360.0f, 360.0f);
    c.amplitude = p.hsMetres * kArtisticAmplitudeScale;
    c.wavelengthPx = wavelengthFromPeriod(periodS);
    c.directionRad = travelBearingDeg * kDegToRad;
    c.angularFreq = 2.0f * kPi / periodS;
    return c;
}

} // namespace

WaveEngine::WaveEngine(double expectedUpdateIntervalSeconds, double siteLatDeg, double siteLonDeg)
    : expectedUpdateIntervalSeconds_(expectedUpdateIntervalSeconds), siteLatDeg_(siteLatDeg), siteLonDeg_(siteLonDeg) {}

std::array<WaveComponent, kComponentCount> WaveEngine::deriveComponents(const BuoyReading& reading) {
    std::array<WaveComponent, kComponentCount> specs{};

    const WavePartition* candidates[3] = {&reading.primarySwell, &reading.secondarySwell, &reading.windSea};
    const WavePartition* present[3] = {};
    int presentCount = 0;
    for (const WavePartition* p : candidates) {
        if (p->present) present[presentCount++] = p;
    }

    if (presentCount == 0) {
        // Bulk-only fallback (mock feed, or a source without real spectral
        // partitions): exactly the original spread-jitter derivation from a
        // single bulk reading -- unchanged, so existing verified transition/
        // wrap/staleness behaviour carries over with zero regression risk.
        float baseAmplitude = reading.hsMetres * kArtisticAmplitudeScale;
        float travelBearingDeg = std::fmod(reading.mwdDeg + 180.0f + 360.0f, 360.0f);
        for (int i = 0; i < kComponentCount; ++i) {
            float dirDeg = std::fmod(travelBearingDeg + kSpreadFractions[i] * reading.spreadDeg + 360.0f, 360.0f);
            float periodS = std::max(0.5f, reading.tpSeconds * kPeriodJitter[i]);
            specs[i].amplitude = baseAmplitude * (kAmplitudeWeights[i] / kWeightSum);
            specs[i].wavelengthPx = wavelengthFromPeriod(periodS);
            specs[i].directionRad = dirDeg * kDegToRad;
            specs[i].angularFreq = 2.0f * kPi / periodS;
        }
        return specs;
    }

    // Real partitioned data: one component slot per real wave train, taken
    // directly rather than jittered -- we're no longer faking directionality
    // we now genuinely have.
    int slot = 0;
    for (int i = 0; i < presentCount && slot < kComponentCount; ++i, ++slot) {
        specs[slot] = componentFromPartition(*present[i]);
    }

    // Fill any remaining slots with small texture-only jitter around the
    // dominant (largest Hs) real train -- same trick the bulk path uses,
    // just kept subtle (1/4 amplitude) since it's texture on top of real
    // data, not standing in for missing directional information.
    if (slot < kComponentCount) {
        const WavePartition* dominant = present[0];
        for (int i = 1; i < presentCount; ++i) {
            if (present[i]->hsMetres > dominant->hsMetres) dominant = present[i];
        }
        float dominantTravelBearingDeg = std::fmod(dominant->dirDeg + 180.0f + 360.0f, 360.0f);
        float dominantAmplitude = dominant->hsMetres * kArtisticAmplitudeScale;

        constexpr float kTextureJitterDirDeg[] = {12.0f, -12.0f, 22.0f, -22.0f};
        constexpr float kTextureJitterPeriodMul[] = {1.07f, 0.94f, 1.13f, 0.89f};
        int j = 0;
        while (slot < kComponentCount) {
            int ji = j % 4;
            float dirDeg = std::fmod(dominantTravelBearingDeg + kTextureJitterDirDeg[ji] + 360.0f, 360.0f);
            float periodS = std::max(0.5f, dominant->tpSeconds * kTextureJitterPeriodMul[ji]);
            specs[slot].amplitude = dominantAmplitude * 0.25f;
            specs[slot].wavelengthPx = wavelengthFromPeriod(periodS);
            specs[slot].directionRad = dirDeg * kDegToRad;
            specs[slot].angularFreq = 2.0f * kPi / periodS;
            ++slot;
            ++j;
        }
    }

    return specs;
}

void WaveEngine::ingest(const BuoyReading& reading, double nowSeconds) {
    if (everIngested_) {
        // Snapshot the CURRENT blended state (not the old target) as the new
        // prev, so a reading that arrives mid-transition doesn't jump.
        double elapsed = nowSeconds - transitionStartTime_;
        float t = smoothstep(clamp01(static_cast<float>(elapsed / kTransitionSeconds)));
        prevState_ = blendReading(prevState_, targetState_, t);
        for (int i = 0; i < kComponentCount; ++i) {
            prevSpecs_[i] = blendSpec(prevSpecs_[i], targetSpecs_[i], t);
        }
    } else {
        prevState_ = reading;
        prevSpecs_ = deriveComponents(reading);
        everIngested_ = true;
        lastTickTime_ = nowSeconds;
    }

    targetState_ = reading;
    targetSpecs_ = deriveComponents(reading);
    transitionStartTime_ = nowSeconds;
    lastIngestTime_ = nowSeconds;
}

void WaveEngine::tick(double nowSeconds, int64_t wallClockUnixSeconds) {
    SunPosition sun = solarPosition(siteLatDeg_, siteLonDeg_, wallClockUnixSeconds);

    if (!everIngested_) {
        lastTickTime_ = nowSeconds;
        // current_ is still default-constructed (no cloud data) here, which
        // is correct -- before any reading has ever arrived, sun position
        // alone drives the sky, same as it will once cloud data exists.
        currentSky_ = deriveSkyState(sun, current_.cloudCoverPercent, current_.cloudCoverPresent);
        return;
    }

    double dt = nowSeconds - lastTickTime_;
    if (dt < 0.0) dt = 0.0;
    lastTickTime_ = nowSeconds;

    double elapsed = nowSeconds - transitionStartTime_;
    float t = smoothstep(clamp01(static_cast<float>(elapsed / kTransitionSeconds)));

    current_ = blendReading(prevState_, targetState_, t);
    for (int i = 0; i < kComponentCount; ++i) {
        currentSpecs_[i] = blendSpec(prevSpecs_[i], targetSpecs_[i], t);
        phase_[i] += currentSpecs_[i].angularFreq * static_cast<float>(dt);
    }
    noiseTime_ += dt;

    double sinceLast = nowSeconds - lastIngestTime_;
    double staleAt = expectedUpdateIntervalSeconds_ * kStaleThresholdMultiplier;
    if (sinceLast <= staleAt) {
        confidence_ = 1.0f;
    } else {
        double decayWindow = std::max(expectedUpdateIntervalSeconds_ * 1.5, 1.0);
        confidence_ = static_cast<float>(std::max(0.0, 1.0 - (sinceLast - staleAt) / decayWindow));
    }

    // Computed from the freshly-updated current_ above, not the previous
    // tick's value -- a new reading's cloud-cover data (or an override)
    // must be visible in this same tick, not one tick late.
    currentSky_ = deriveSkyState(sun, current_.cloudCoverPercent, current_.cloudCoverPresent);
}

FieldSample WaveEngine::sampleField(int x, int y) const {
    float height = 0.0f;
    float gradX = 0.0f;
    float gradY = 0.0f;

    constexpr float kSteepnessToSharpness = 2.0f;
    constexpr float kMaxExtraSharpness = 2.5f;

    for (int i = 0; i < kComponentCount; ++i) {
        const WaveComponent& s = currentSpecs_[i];
        float k = 2.0f * kPi / std::max(s.wavelengthPx, 0.5f);
        float dirX = std::sin(s.directionRad);
        float dirY = -std::cos(s.directionRad);
        float proj = static_cast<float>(x) * dirX + static_cast<float>(y) * dirY;
        float angle = k * proj - phase_[i];

        // Gerstner-ish crest sharpening: true Gerstner waves also displace
        // particles horizontally, which bunches crests narrow and spreads
        // troughs wide -- there's no closed-form per-pixel inverse for that,
        // so instead we reshape the raw cosine's symmetric crest/trough into
        // that same peaked/broad asymmetry. u in [0,1]; raising it to a
        // power > 1 compresses most of the cycle toward the trough and
        // leaves a narrow spike at the crest, and steeper (shorter/taller)
        // components get more of the effect, same as real wave steepness
        // A*k governs how peaked a real trochoidal wave looks. sharpness==1
        // degenerates back to plain cosine exactly.
        float c = std::cos(angle);
        float sn = std::sin(angle);
        float u = (c + 1.0f) * 0.5f;
        float steepness = s.amplitude * k;
        float sharpness = 1.0f + std::min(steepness * kSteepnessToSharpness, kMaxExtraSharpness);
        float shaped = 2.0f * std::pow(u, sharpness) - 1.0f;

        height += s.amplitude * shaped;

        // d(shaped)/d(angle) = -sharpness * u^(sharpness-1) * sin(angle);
        // chain-ruled the same way as the plain-cosine case through
        // proj = x*dirX + y*dirY to keep the analytical slope for lighting.
        float dShapedDAngle = -sharpness * std::pow(u, sharpness - 1.0f) * sn;
        float dTermDProj = s.amplitude * dShapedDAngle * k;
        gradX += dTermDProj * dirX;
        gradY += dTermDProj * dirY;
    }

    constexpr float kSlowNoiseScale = 0.045f;
    constexpr float kFastNoiseScale = 0.16f;
    constexpr float kSparkleScale = 0.9f;
    constexpr float kSlowNoiseTimeScale = 0.05f;
    constexpr float kFastNoiseTimeScale = 0.35f;
    constexpr float kSparkleTimeScale = 1.6f;

    float slow = noise_.noise3(x * kSlowNoiseScale, y * kSlowNoiseScale,
                                static_cast<float>(noiseTime_) * kSlowNoiseTimeScale);
    float fast = noise_.noise3(x * kFastNoiseScale + 137.0f, y * kFastNoiseScale + 137.0f,
                                static_cast<float>(noiseTime_) * kFastNoiseTimeScale);
    float sparkleN = noise_.noise3(x * kSparkleScale + 401.0f, y * kSparkleScale + 401.0f,
                                    static_cast<float>(noiseTime_) * kSparkleTimeScale);

    // The longer data has been stale, the more noise dominates over the
    // deterministic signal -- "losing certainty" rather than freezing.
    float staleBoost = 1.0f + (1.0f - confidence_) * 1.5f;
    height += (slow * 0.28f + fast * 0.14f) * staleBoost;

    // Foam: irregular and thresholded, not a smooth ramp. Steeper facets and
    // higher crests are more likely to foam, and bigger sea states foam more
    // readily (lower threshold) -- ties the texture back to the data.
    constexpr float kFoamBaseThreshold = 0.55f;
    constexpr float kFoamHsInfluence = 0.10f;
    constexpr float kFoamSteepnessWeight = 0.35f;
    constexpr float kFoamNoiseWeight = 0.5f;
    constexpr float kFoamSoftness = 0.35f;

    float steepness = std::sqrt(gradX * gradX + gradY * gradY);
    float foamThreshold = std::max(0.15f, kFoamBaseThreshold - current_.hsMetres * kFoamHsInfluence);
    float foamDrive = height + steepness * kFoamSteepnessWeight + fast * kFoamNoiseWeight;
    float foamAmount = smoothstep(clamp01((foamDrive - foamThreshold) / kFoamSoftness));

    // Sparkle: sparse, punchy twinkle points (sun-glitter) rather than
    // smooth blobby noise -- raise a clamped, boosted noise sample to a high
    // power so only its peaks survive.
    float sparkleRaw = std::max(0.0f, sparkleN);
    float sparkle = std::min(1.0f, std::pow(sparkleRaw * 1.4f, 5.0f));

    FieldSample sample;
    sample.height = height;
    sample.gradX = gradX;
    sample.gradY = gradY;
    sample.foamAmount = foamAmount;
    sample.sparkle = sparkle;
    return sample;
}

void WaveEngine::renderFrame(Grid<RGB8>& out) const {
    for (int y = 0; y < kGridSize; ++y) {
        for (int x = 0; x < kGridSize; ++x) {
            out.at(x, y) = colorize(sampleField(x, y), confidence_, currentSky_);
        }
    }
}

} // namespace wave
