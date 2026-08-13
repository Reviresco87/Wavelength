#include "mock_buoy_feed.h"

namespace wave {

namespace {

// Explicit named construction rather than positional aggregate init -- the
// latter silently misaligned when BuoyReading grew the partition members
// (a trailing `true` meant to set `valid` ended up initializing `windSea`
// instead). This is immune to that class of bug if the struct grows again.
BuoyReading makeBulkReading(float hs, float tp, float mwd, float spread, float seaTemp) {
    BuoyReading r;
    r.hsMetres = hs;
    r.tpSeconds = tp;
    r.mwdDeg = mwd;
    r.spreadDeg = spread;
    r.seaTempC = seaTemp;
    r.valid = true;
    return r;
}

} // namespace

MockBuoyFeed::MockBuoyFeed(double intervalSeconds) : intervalSeconds_(intervalSeconds) {
    // Illustrative south-Cornwall/Channel-facing bay values -- walks Hs up
    // into a moderate swell and back down, and deliberately sweeps direction
    // across the 350->10 degree wrap so that behaviour gets exercised
    // without waiting for it to happen naturally. No partition data (this is
    // the bulk-only fallback path); timestampUnix is overwritten in poll().
    sequence_ = {
        makeBulkReading(0.6f, 6.5f, 205.0f, 18.0f, 15.5f),
        makeBulkReading(0.9f, 7.2f, 215.0f, 20.0f, 15.4f),
        makeBulkReading(1.4f, 8.5f, 225.0f, 22.0f, 15.2f),
        makeBulkReading(1.8f, 9.5f, 350.0f, 24.0f, 15.0f),  // big swing across the wrap boundary
        makeBulkReading(2.1f, 10.0f, 10.0f, 20.0f, 14.8f),  // continues past 360/0
        makeBulkReading(1.6f, 8.8f, 40.0f, 18.0f, 15.0f),
        makeBulkReading(1.1f, 7.5f, 60.0f, 16.0f, 15.1f),
        makeBulkReading(0.7f, 6.0f, 70.0f, 15.0f, 15.3f),
    };
}

bool MockBuoyFeed::poll(double nowSeconds, BuoyReading& out) {
    if (index_ >= sequence_.size()) return false;
    if (nowSeconds < nextDueSeconds_) return false;

    out = sequence_[index_];
    out.timestampUnix = static_cast<int64_t>(nowSeconds);
    ++index_;
    nextDueSeconds_ = nowSeconds + intervalSeconds_;
    return true;
}

} // namespace wave
