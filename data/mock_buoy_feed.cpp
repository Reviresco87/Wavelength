#include "mock_buoy_feed.h"

namespace wave {

MockBuoyFeed::MockBuoyFeed(double intervalSeconds) : intervalSeconds_(intervalSeconds) {
    // hsMetres, tpSeconds, mwdDeg, spreadDeg, seaTempC, valid.
    // Illustrative south-Cornwall/Channel-facing bay values -- walks Hs up
    // into a moderate swell and back down, and deliberately sweeps direction
    // across the 350->10 degree wrap so that behaviour gets exercised
    // without waiting for it to happen naturally.
    sequence_ = {
        {0, 0.6f, 6.5f, 205.0f, 18.0f, 15.5f, true},
        {0, 0.9f, 7.2f, 215.0f, 20.0f, 15.4f, true},
        {0, 1.4f, 8.5f, 225.0f, 22.0f, 15.2f, true},
        {0, 1.8f, 9.5f, 350.0f, 24.0f, 15.0f, true},  // big swing across the wrap boundary
        {0, 2.1f, 10.0f, 10.0f, 20.0f, 14.8f, true},  // continues past 360/0
        {0, 1.6f, 8.8f, 40.0f, 18.0f, 15.0f, true},
        {0, 1.1f, 7.5f, 60.0f, 16.0f, 15.1f, true},
        {0, 0.7f, 6.0f, 70.0f, 15.0f, 15.3f, true},
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
