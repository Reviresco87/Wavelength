#pragma once

#include "wave_component.h"

namespace wave {

// Samples `slotCount` discrete wave components (starting at
// outComponents[startSlot]) from a JONSWAP frequency spectrum (Hasselmann et
// al. 1973) and a Longuet-Higgins cos^2s directional spreading function
// (spread-to-s conversion per Kuik et al. 1988), given one wave train's bulk
// statistics -- the classical "linear random wave" technique for
// synthesizing a realistic sea state from summary parameters
// (Hs/Tp/direction/spread) rather than from raw wind/fetch (which is what
// full FFT ocean synthesis needs, and we don't have -- our data sources
// already report summarized wave trains, not raw wind fields).
//
// Deterministic: a fixed stratified (frequency, direction) grid centred
// exactly on (1/tpSeconds, dirFromDeg), not random sampling, so behaviour is
// reproducible and testable like the rest of core/. Amplitudes are rescaled
// so this call's own components reconstruct hsMetres exactly
// (4*sqrt(sum(amplitude^2/2)) == hsMetres) -- the same normalisation real
// spectral tools use, and what keeps the output faithful to the source
// reading rather than merely visually plausible.
//
// dirFromDeg follows the same convention as BuoyReading::mwdDeg /
// WavePartition::dirDeg -- the compass bearing waves travel FROM.
void sampleSpectrumComponents(float hsMetres, float tpSeconds, float dirFromDeg, float spreadDeg,
                               WaveComponent* outComponents, int startSlot, int slotCount, float amplitudeScale);

} // namespace wave
