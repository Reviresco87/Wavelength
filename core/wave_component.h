#pragma once

namespace wave {

constexpr int kComponentCount = 5;

// One directional sinusoidal wave train. A BuoyReading (a single bulk
// height/period/direction/spread reading) is expanded into kComponentCount
// of these -- one primary plus jittered secondaries -- so a single-point
// reading still produces a textured, non-repeating field instead of one
// perfectly regular sine grid.
struct WaveComponent {
    float amplitude = 0.0f;
    float wavelengthPx = 20.0f;
    float directionRad = 0.0f;  // propagation direction (waves travel TOWARD this bearing)
    float angularFreq = 0.0f;   // radians/sec, from the component's period
};

} // namespace wave
