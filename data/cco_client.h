#pragma once

#include <string>

#include "../core/buoy_reading.h"

namespace wave {

// Parses a Channel Coastal Observatory (coastalmonitoring.org) wave
// observation GeoJSON response into a BuoyReading.
//
// IMPORTANT: the property field-name mapping in cco_client.cpp is
// UNVERIFIED. CCO's public API docs don't spell out the response schema in
// enough detail to confirm exact key names, so the `kField*` constants
// there are best-guesses based on common wave-buoy naming conventions.
// Confirm against a real response (once a CCO API key exists -- see the
// README's "Data source" section) and fix those constants before relying
// on this for real data. Not wired into preview/main_native.cpp yet for
// that reason; the mock feed remains the default data source for now.
class CcoClient {
public:
    // Parses a single-feature (or first-feature-of-a-collection) GeoJSON
    // wave observation. Returns a BuoyReading with valid=false on any
    // parse failure or missing core field.
    static BuoyReading parseObservation(const std::string& geoJson);
};

} // namespace wave
