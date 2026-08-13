#pragma once

#include <array>

#include "buoy_reading.h"
#include "field_sample.h"
#include "grid.h"
#include "noise.h"
#include "sky_state.h"
#include "wave_component.h"

namespace wave {

// Mevagissey, Cornwall -- what this whole project targets.
constexpr double kSiteLatDeg = 50.2705;
constexpr double kSiteLonDeg = -4.7827;

// The living sea-state engine. Holds a previous/target reading pair and
// eases between them, while wave-component phases and a drifting noise
// field keep advancing every tick regardless of when the next reading
// arrives -- that continuous motion is what makes it look alive between
// buoy updates rather than a slideshow of static frames.
class WaveEngine {
public:
    // expectedUpdateIntervalSeconds should match how often the data source
    // actually updates (real CCO buoys: ~30 min; the mock feed overrides
    // this to its own cadence so staleness is observable in a short session).
    // siteLat/LonDeg default to Mevagissey but are real parameters, not a
    // buried constant, so the engine isn't secretly non-reusable.
    explicit WaveEngine(double expectedUpdateIntervalSeconds = 1800.0, double siteLatDeg = kSiteLatDeg,
                        double siteLonDeg = kSiteLonDeg);

    void setExpectedUpdateInterval(double seconds) { expectedUpdateIntervalSeconds_ = seconds; }

    // Call whenever a new reading arrives (from the mock feed or, later, cco_client).
    void ingest(const BuoyReading& reading, double nowSeconds);

    // Call every frame with a monotonically increasing clock for animation
    // (nowSeconds) and the real current UTC time for sun position
    // (wallClockUnixSeconds) -- these are deliberately different clocks:
    // one is "how long has this program been running", the other is
    // "what time is it really", and only the latter is meaningful for
    // where the sun actually is.
    void tick(double nowSeconds, int64_t wallClockUnixSeconds);

    void renderFrame(Grid<RGB8>& out) const;

    bool hasData() const { return everIngested_; }
    float confidence() const { return confidence_; }
    const BuoyReading& currentState() const { return current_; }
    const std::array<WaveComponent, kComponentCount>& currentComponents() const { return currentSpecs_; }
    const SkyState& currentSky() const { return currentSky_; }
    double secondsSinceLastUpdate(double nowSeconds) const { return nowSeconds - lastIngestTime_; }

private:
    static std::array<WaveComponent, kComponentCount> deriveComponents(const BuoyReading& reading);
    FieldSample sampleField(int x, int y) const;

    double expectedUpdateIntervalSeconds_;
    double siteLatDeg_;
    double siteLonDeg_;

    bool everIngested_ = false;
    double lastIngestTime_ = 0.0;
    double transitionStartTime_ = 0.0;
    double lastTickTime_ = 0.0;
    double noiseTime_ = 0.0;

    BuoyReading prevState_{};
    BuoyReading targetState_{};
    BuoyReading current_{};

    std::array<WaveComponent, kComponentCount> prevSpecs_{};
    std::array<WaveComponent, kComponentCount> targetSpecs_{};
    std::array<WaveComponent, kComponentCount> currentSpecs_{};
    std::array<float, kComponentCount> phase_{};

    SkyState currentSky_{};

    float confidence_ = 1.0f;
    PerlinNoise noise_;

    static constexpr double kTransitionSeconds = 20.0;
    static constexpr double kStaleThresholdMultiplier = 2.5;
};

} // namespace wave
