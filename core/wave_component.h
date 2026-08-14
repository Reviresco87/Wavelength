#pragma once

namespace wave {

constexpr int kComponentCount = 16;

// One directional sinusoidal wave train. A BuoyReading (bulk statistics, or
// up to three real partitions) is expanded into kComponentCount of these via
// sampleSpectrumComponents (core/wave_spectrum.h) -- discrete samples drawn
// from a real JONSWAP frequency spectrum and cos^2s directional spreading
// function, not hand-picked jitter, so a single-point reading still produces
// a textured, non-repeating field grounded in how sea states are actually
// represented rather than an arbitrary heuristic.
struct WaveComponent {
    float amplitude = 0.0f;
    float wavelengthPx = 20.0f;
    float directionRad = 0.0f;  // propagation direction (waves travel TOWARD this bearing)
    float angularFreq = 0.0f;   // radians/sec, from the component's period
};

} // namespace wave
