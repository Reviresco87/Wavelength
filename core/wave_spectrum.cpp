#include "wave_spectrum.h"

#include <algorithm>
#include <cmath>

namespace wave {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDegToRad = kPi / 180.0f;
constexpr float kG = 9.81f;

// JONSWAP peak-enhancement constants (Hasselmann et al. 1973).
constexpr float kGamma = 3.3f;
constexpr float kSigmaLow = 0.07f;   // f <= fp
constexpr float kSigmaHigh = 0.09f;  // f > fp

constexpr int kMaxGridDim = 8; // headroom over sqrt(kComponentCount); never actually clamps at kComponentCount=16

// Real wavelength (deep-water dispersion, lambda = 1.56 * T^2) doesn't fit a
// 64px panel -- a 20s groundswell is 600+ metres -- so this was always going
// to be an artistic rescale, not physically-to-scale. It was originally
// rescaled by mapping physical (T^2-scaled) metres onto the pixel range, but
// that compresses realistic UK coastal periods (mostly 3-10s) into a sliver
// of the low end -- two real components a season apart in period (e.g. 3.9s
// and 7.7s) came out only ~60% apart in wavelength, reading as near-
// duplicates instead of visually distinct wave trains. Mapping period
// directly (linear, not squared) spreads the range we actually see day-to-
// day across much more of the pixel range, while keeping the same endpoints
// and the same "longer period -> longer wavelength" direction.
float wavelengthFromPeriod(float periodSeconds) {
    constexpr float periodMin = 3.0f;
    constexpr float periodMax = 20.0f;
    float t = (periodSeconds - periodMin) / (periodMax - periodMin);
    t = std::max(0.0f, std::min(1.0f, t));
    return 6.0f + t * (40.0f - 6.0f);
}

// JONSWAP energy density shape at frequency f (Hz), unnormalised -- alpha is
// left at 1 because the caller rescales the whole sampled set to match a
// target Hs afterward, so any constant multiplier cancels out. The
// f-dependent shape (power-law tail + peak enhancement) is what actually
// matters for a realistic spectrum and is kept exact.
float jonswapShape(float f, float fp) {
    float sigma = (f <= fp) ? kSigmaLow : kSigmaHigh;
    float base = kG * kG * std::pow(2.0f * kPi, -4.0f) * std::pow(f, -5.0f);
    float decay = std::exp(-1.25f * std::pow(f / fp, -4.0f));
    float peakExp = std::exp(-((f - fp) * (f - fp)) / (2.0f * sigma * sigma * fp * fp));
    float peak = std::pow(kGamma, peakExp);
    return base * decay * peak;
}

// Longuet-Higgins cos^2s directional spreading shape (unnormalised, same
// reasoning as jonswapShape -- caller rescales). thetaDiffRad is kept within
// (-pi, pi) by the caller's sampling window, so cos(0.5*thetaDiffRad) is
// never negative and pow() never sees a negative base.
float cos2sShape(float thetaDiffRad, float s) {
    float base = std::max(0.0f, std::cos(0.5f * thetaDiffRad));
    return std::pow(base, 2.0f * s);
}

// Fills `count` strictly increasing values into `out`, with exactly one
// entry equal to `center` -- so a spectral grid built from this always
// includes the source reading's exact reported value as one sample point,
// not just something nearby.
void buildCenteredSamples(float center, float lowBound, float highBound, int count, float* out) {
    if (count <= 1) {
        out[0] = center;
        return;
    }
    int nBelow = (count - 1) / 2;
    int nAbove = count - 1 - nBelow;
    int idx = 0;
    for (int i = 0; i < nBelow; ++i) {
        float t = static_cast<float>(i + 1) / static_cast<float>(nBelow + 1);
        out[idx++] = lowBound + t * (center - lowBound);
    }
    out[idx++] = center;
    for (int i = 0; i < nAbove; ++i) {
        float t = static_cast<float>(i + 1) / static_cast<float>(nAbove + 1);
        out[idx++] = center + t * (highBound - center);
    }
}

// Per-sample bin width for a strictly increasing, possibly irregularly
// spaced sample array -- half the gap to each neighbour, extended to the
// domain boundary at the two ends. Standard "Voronoi bin width" treatment
// for numerically integrating a function known only at discrete points.
float binWidth(const float* samples, int count, int i, float lowBound, float highBound) {
    if (count == 1) return highBound - lowBound;
    if (i == 0) return (samples[1] - samples[0]) * 0.5f + (samples[0] - lowBound);
    if (i == count - 1) return (samples[i] - samples[i - 1]) * 0.5f + (highBound - samples[i]);
    return (samples[i + 1] - samples[i - 1]) * 0.5f;
}

int intAbs(int v) { return v < 0 ? -v : v; }

} // namespace

void sampleSpectrumComponents(float hsMetres, float tpSeconds, float dirFromDeg, float spreadDeg,
                               WaveComponent* outComponents, int startSlot, int slotCount, float amplitudeScale) {
    if (slotCount <= 0) return;

    float tp = std::max(0.5f, tpSeconds);
    float fp = 1.0f / tp;
    float fMin = fp * 0.5f;
    float fMax = fp * 2.5f;

    // WaveComponent::directionRad is the bearing waves travel TOWARD, but
    // dirFromDeg (matching BuoyReading::mwdDeg / WavePartition::dirDeg) is
    // the bearing waves travel FROM -- convert once, up front, the same way
    // every other direction-consuming site in this codebase does.
    float travelBearingDeg = std::fmod(dirFromDeg + 180.0f + 360.0f, 360.0f);

    // Floor the spread so a near-zero reported spread can't blow up s
    // (s -> infinity as spread -> 0), and floor s itself so a very wide
    // reported spread can't push s negative, which would hand a negative
    // exponent to cos2sShape's pow().
    float spreadFloorDeg = std::max(5.0f, spreadDeg);
    float spreadRad = spreadFloorDeg * kDegToRad;
    float s = std::max(1.0f, 2.0f / (spreadRad * spreadRad) - 1.0f);

    // +/- 2.5x spread covers the meaningful energy of a cos^2s lobe; capped
    // to +/-150 deg so the sampling window can never approach +/-180, which
    // would break the "half-angle cosine never negative" assumption above.
    float dirHalfWindowDeg = std::min(150.0f, 2.5f * spreadFloorDeg);
    float dirLowDeg = travelBearingDeg - dirHalfWindowDeg;
    float dirHighDeg = travelBearingDeg + dirHalfWindowDeg;

    // Grid sized so nf*nd is close to slotCount without going far over --
    // any surplus grid points are dropped below (farthest from the exact
    // reading first).
    int nf = std::max(1, static_cast<int>(std::lround(std::sqrt(static_cast<double>(slotCount)))));
    int nd = std::max(1, (slotCount + nf - 1) / nf);
    nf = std::min(nf, kMaxGridDim);
    nd = std::min(nd, kMaxGridDim);

    float freqs[kMaxGridDim];
    float dirs[kMaxGridDim];
    buildCenteredSamples(fp, fMin, fMax, nf, freqs);
    buildCenteredSamples(travelBearingDeg, dirLowDeg, dirHighDeg, nd, dirs);

    struct Candidate {
        int fi = 0;
        int di = 0;
        int rank = 0;
    };
    Candidate candidates[kMaxGridDim * kMaxGridDim];
    int candidateCount = 0;
    int centerFi = (nf - 1) / 2;
    int centerDi = (nd - 1) / 2;
    for (int fi = 0; fi < nf; ++fi) {
        for (int di = 0; di < nd; ++di) {
            candidates[candidateCount].fi = fi;
            candidates[candidateCount].di = di;
            candidates[candidateCount].rank = intAbs(fi - centerFi) + intAbs(di - centerDi);
            ++candidateCount;
        }
    }
    std::sort(candidates, candidates + candidateCount,
              [](const Candidate& a, const Candidate& b) { return a.rank < b.rank; });

    int usedCount = std::min(slotCount, candidateCount);

    float rawWeights[kMaxGridDim * kMaxGridDim];
    float binAreas[kMaxGridDim * kMaxGridDim];
    double m0Raw = 0.0;
    for (int n = 0; n < usedCount; ++n) {
        int fi = candidates[n].fi;
        int di = candidates[n].di;
        float f = freqs[fi];
        float thetaDiffRad = (dirs[di] - travelBearingDeg) * kDegToRad;
        float weight = jonswapShape(f, fp) * cos2sShape(thetaDiffRad, s);
        float dF = binWidth(freqs, nf, fi, fMin, fMax);
        float dTheta = binWidth(dirs, nd, di, dirLowDeg, dirHighDeg) * kDegToRad;
        rawWeights[n] = weight;
        binAreas[n] = dF * dTheta;
        m0Raw += static_cast<double>(weight) * dF * dTheta;
    }

    // Rescale so this call's own components reconstruct hsMetres exactly:
    // Hs = 4*sqrt(m0), so target m0 = (Hs/4)^2. This sidesteps ever needing
    // the analytic JONSWAP alpha or cos^2s normalising constant C(s) -- both
    // cancel out, since every raw weight is scaled by the same factor.
    double targetM0 = static_cast<double>(hsMetres) * hsMetres / 16.0;
    double scale = (m0Raw > 1e-12) ? (targetM0 / m0Raw) : 0.0;

    for (int n = 0; n < usedCount; ++n) {
        int fi = candidates[n].fi;
        int di = candidates[n].di;
        float variance = static_cast<float>(rawWeights[n] * scale * binAreas[n]);
        float amplitude = std::sqrt(std::max(0.0f, 2.0f * variance)) * amplitudeScale;
        float periodS = 1.0f / freqs[fi];
        float dirDegTravel = std::fmod(dirs[di] + 360.0f, 360.0f);

        WaveComponent& c = outComponents[startSlot + n];
        c.amplitude = amplitude;
        c.wavelengthPx = wavelengthFromPeriod(periodS);
        c.directionRad = dirDegTravel * kDegToRad;
        c.angularFreq = 2.0f * kPi / periodS;
    }
}

} // namespace wave
