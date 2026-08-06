#include "cco_client.h"

#include "../vendor/ArduinoJson.h"

namespace wave {

namespace {

// UNVERIFIED property key names -- see the warning in cco_client.h. Update
// these once a real CCO response has actually been inspected.
constexpr const char* kFieldHs = "Hs";
constexpr const char* kFieldTp = "Tp";
constexpr const char* kFieldMeanDirection = "MeanDirection";
constexpr const char* kFieldSpread = "SpreadDirection";
constexpr const char* kFieldSeaTemp = "SeaTemperature";

} // namespace

BuoyReading CcoClient::parseObservation(const std::string& geoJson) {
    BuoyReading reading;

    JsonDocument doc;
    if (deserializeJson(doc, geoJson) != DeserializationError::Ok) {
        return reading; // valid stays false
    }

    JsonVariantConst properties;
    if (doc["features"].is<JsonArrayConst>() && doc["features"].size() > 0) {
        properties = doc["features"][0]["properties"];
    } else if (doc["properties"].is<JsonObjectConst>()) {
        properties = doc["properties"]; // tolerate a single bare Feature too
    }

    if (properties.isNull()) {
        return reading;
    }

    if (!properties[kFieldHs].is<float>() || !properties[kFieldTp].is<float>() ||
        !properties[kFieldMeanDirection].is<float>()) {
        return reading; // core fields missing: unparseable/unexpected payload shape
    }

    reading.hsMetres = properties[kFieldHs].as<float>();
    reading.tpSeconds = properties[kFieldTp].as<float>();
    reading.mwdDeg = properties[kFieldMeanDirection].as<float>();

    if (properties[kFieldSpread].is<float>()) {
        reading.spreadDeg = properties[kFieldSpread].as<float>();
    }
    if (properties[kFieldSeaTemp].is<float>()) {
        reading.seaTempC = properties[kFieldSeaTemp].as<float>();
    }
    // Timestamp intentionally left to the caller (e.g. stamp with receipt
    // time) -- CCO's DateTime string format hasn't been confirmed either,
    // not worth guessing a parser for a format we haven't actually seen.

    reading.valid = true;
    return reading;
}

} // namespace wave
