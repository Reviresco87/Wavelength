#pragma once

#include <array>

#include "buoy_reading.h"
#include "field_sample.h"
#include "grid.h"
#include "noise.h"
#include "wave_component.h"

namespace wave {

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
    explicit WaveEngine(double expectedUpdateIntervalSeconds = 1800.0);

    void setExpectedUpdateInterval(double seconds) { expectedUpdateIntervalSeconds_ = seconds; }

    // Call whenever a new reading arrives (from the mock feed or, later, cco_client).
    void ingest(const BuoyReading& reading, double nowSeconds);

    // Call every frame with a monotonically increasing clock.
    void tick(double nowSeconds);

    void renderFrame(Grid<RGB8>& out) const;

    bool hasData() const { return everIngested_; }
    float confidence() const { return confidence_; }
    const BuoyReading& currentState() const { return current_; }
    const std::array<WaveComponent, kComponentCount>& currentComponents() const { return currentSpecs_; }
    double secondsSinceLastUpdate(double nowSeconds) const { return nowSeconds - lastIngestTime_; }

private:
    static std::array<WaveComponent, kComponentCount> deriveComponents(const BuoyReading& reading);
    FieldSample sampleField(int x, int y) const;

    double expectedUpdateIntervalSeconds_;

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

    float confidence_ = 1.0f;
    PerlinNoise noise_;

    static constexpr double kTransitionSeconds = 20.0;
    static constexpr double kStaleThresholdMultiplier = 2.5;
};

} // namespace wave
