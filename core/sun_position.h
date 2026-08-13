#pragma once

#include <cstdint>

namespace wave {

struct SunPosition {
    float elevationDeg = 0.0f;  // above horizon: positive; below: negative
    float azimuthDeg = 0.0f;    // compass bearing the sun is in, 0=N, 90=E, 180=S, 270=W
};

// NOAA/Spencer simplified solar position algorithm (Fourier-series
// declination + equation of time + hour angle). Pure math, no platform
// dependency beyond <ctime> for UTC calendar conversion -- portable to the
// ESP32 unchanged, same as everything else in core/. No leap-year
// correction (uses a fixed 365-day year); the resulting error is a small
// fraction of a degree, negligible for this use.
SunPosition solarPosition(double latDeg, double lonDeg, int64_t unixTimeSeconds);

} // namespace wave
