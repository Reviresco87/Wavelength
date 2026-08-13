#pragma once

#include <string>

#include "../core/buoy_reading.h"

namespace wave {

// Parses a Channel Coastal Observatory (coastalmonitoring.org) wave
// observation GeoJSON response into a BuoyReading.
//
// Field names below were confirmed against a real live response pulled this
// session (via the exact endpoint coastalmonitoring.org's own public map
// uses), not guessed -- including a non-obvious quirk: CCO serializes its
// numeric values as JSON strings (e.g. "hs":"0.670"), not JSON numbers,
// which parseObservation's implementation accounts for. The
// `/latest.geojson` feed returns every CCO wave site nationally in one
// FeatureCollection, so parseObservation needs the target sensor's name to
// pick the right feature out of it.
class CcoClient {
public:
    // Finds the feature whose "sensor" property equals `sensorName` and
    // parses it. Returns a BuoyReading with valid=false if that sensor isn't
    // present in this response, the JSON is malformed, or a core field is
    // missing (e.g. that site reported no data this cycle).
    static BuoyReading parseObservation(const std::string& geoJson, const std::string& sensorName);
};

} // namespace wave
