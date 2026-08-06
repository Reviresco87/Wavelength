#pragma once

#include <cstdint>

namespace wave {

// A single bulk-parameter buoy observation. Mirrors what CCO's wave buoys
// report (or a mock stand-in for it): no directional spectrum, just the
// summary statistics -- the engine synthesizes texture from these.
struct BuoyReading {
    int64_t timestampUnix = 0;

    float hsMetres = 0.0f;    // significant wave height
    float tpSeconds = 0.0f;   // peak wave period
    float mwdDeg = 0.0f;      // mean wave direction, compass bearing (0=N, 90=E, ...)
                              // the direction waves are travelling FROM, per standard
                              // oceanographic convention -- NOT the direction they're heading.
    float spreadDeg = 20.0f;  // directional spread; default used if a source doesn't supply one
    float seaTempC = 0.0f;

    bool valid = false;
};

} // namespace wave
