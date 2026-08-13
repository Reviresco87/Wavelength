#pragma once

namespace wave {

// A single real, physically-distinct wave train (e.g. primary swell, wind
// sea) as reported by a partitioned/spectral data source. Optional -- most
// sources (CCO, the mock feed) only ever give bulk numbers, so `present`
// defaults false and callers must check it before use.
struct WavePartition {
    float hsMetres = 0.0f;
    float tpSeconds = 0.0f;
    float dirDeg = 0.0f;  // compass bearing waves travel FROM, same convention as BuoyReading::mwdDeg
    bool present = false;
};

} // namespace wave
