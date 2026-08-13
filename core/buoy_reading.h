#pragma once

#include <cstdint>

#include "wave_partition.h"

namespace wave {

// A sea-state reading. The bulk fields are always meaningful (a single
// summary reading -- what CCO's buoys report, or a mock stand-in). The three
// partitions are optional: present only when a source has real directionally-
// resolved data (e.g. Copernicus's wind-sea/swell split). deriveComponents()
// uses them directly when present and falls back to synthesizing texture
// from the bulk fields alone when they're not.
struct BuoyReading {
    int64_t timestampUnix = 0;

    float hsMetres = 0.0f;    // significant wave height
    float tpSeconds = 0.0f;   // peak wave period
    float mwdDeg = 0.0f;      // mean wave direction, compass bearing (0=N, 90=E, ...)
                              // the direction waves are travelling FROM, per standard
                              // oceanographic convention -- NOT the direction they're heading.
    float spreadDeg = 20.0f;  // directional spread; default used if a source doesn't supply one
    float seaTempC = 0.0f;

    WavePartition windSea;
    WavePartition primarySwell;
    WavePartition secondarySwell;

    float cloudCoverPercent = 0.0f; // 0-100; only meaningful when cloudCoverPresent
    bool cloudCoverPresent = false;

    bool valid = false;
};

} // namespace wave
