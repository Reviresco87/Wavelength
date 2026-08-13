#pragma once

#include <string>

#include "../core/buoy_reading.h"

namespace wave {

// Parses the blended JSON payload published by cloud/fetch_and_blend.py
// (bulk fields, optional windSea/primarySwell/secondarySwell partitions,
// and optional live cloud cover) into a BuoyReading. Distinct from
// CcoClient: this is our own output schema (real JSON numbers, written by
// Python's json.dump), not CCO's raw GeoJSON (which uses JSON strings for
// numbers -- see cco_client.cpp).
class LiveFeedClient {
public:
    // Returns a BuoyReading with valid=false on any parse failure or
    // missing core bulk field.
    static BuoyReading parsePayload(const std::string& json);
};

} // namespace wave
