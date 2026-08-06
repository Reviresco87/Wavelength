#pragma once

#include <cstddef>
#include <vector>

#include "../core/buoy_reading.h"

namespace wave {

// Emits a short, illustrative sequence of plausible Looe Bay-ish readings on
// a compressed timer, so transitions/direction-wrap/staleness are all
// observable in a short terminal session instead of a real ~30 minute buoy
// cadence. Values are placeholders for engine development, not verified
// historical data -- swap for data/cco_client once that's sanity-checked
// against a real API response.
class MockBuoyFeed {
public:
    explicit MockBuoyFeed(double intervalSeconds = 45.0);

    // Returns true and fills `out` if a new reading is due at `nowSeconds`.
    // Returns false forever once the sequence is exhausted, so the engine's
    // staleness/confidence-decay behaviour can be observed too.
    bool poll(double nowSeconds, BuoyReading& out);

private:
    double intervalSeconds_;
    double nextDueSeconds_ = 0.0;
    std::size_t index_ = 0;
    std::vector<BuoyReading> sequence_;
};

} // namespace wave
