#include "cco_client.h"

#include <cstdlib>

#include "../vendor/ArduinoJson.h"

namespace wave {

namespace {

// Confirmed against a real live coastalmonitoring.org response this
// session -- not guessed. Note: CCO serializes these values as JSON
// STRINGS (e.g. "hs":"0.670"), not JSON numbers -- tryParseFloatField
// below handles that, and tolerates a real JSON number too in case that
// ever changes.
constexpr const char* kFieldSensor = "sensor";
constexpr const char* kFieldHs = "hs";
constexpr const char* kFieldTp = "tp";
constexpr const char* kFieldDirection = "pdir";
constexpr const char* kFieldSpread = "spread";
constexpr const char* kFieldSeaTemp = "sst";

bool tryParseFloatField(JsonVariantConst obj, const char* field, float& out) {
    JsonVariantConst v = obj[field];
    if (v.is<const char*>()) {
        const char* s = v.as<const char*>();
        char* end = nullptr;
        float val = std::strtof(s, &end);
        if (end == s) return false; // string present but not numeric (e.g. empty)
        out = val;
        return true;
    }
    if (v.is<float>()) {
        out = v.as<float>();
        return true;
    }
    return false; // absent, null, or some other type
}

// The /latest.geojson feed returns every CCO wave site nationally in one
// FeatureCollection, not just the one we care about -- find the feature
// whose "sensor" property matches.
JsonVariantConst findSensorProperties(JsonDocument& doc, const std::string& sensorName) {
    if (doc["features"].is<JsonArrayConst>()) {
        for (JsonVariantConst feature : doc["features"].as<JsonArrayConst>()) {
            JsonVariantConst props = feature["properties"];
            if (props[kFieldSensor].is<const char*>() && sensorName == props[kFieldSensor].as<const char*>()) {
                return props;
            }
        }
    } else if (doc["properties"].is<JsonObjectConst>()) {
        // Tolerate a single bare Feature too (no FeatureCollection wrapper).
        JsonVariantConst props = doc["properties"];
        if (props[kFieldSensor].is<const char*>() && sensorName == props[kFieldSensor].as<const char*>()) {
            return props;
        }
    }
    return JsonVariantConst();
}

} // namespace

BuoyReading CcoClient::parseObservation(const std::string& geoJson, const std::string& sensorName) {
    BuoyReading reading;

    JsonDocument doc;
    if (deserializeJson(doc, geoJson) != DeserializationError::Ok) {
        return reading; // valid stays false
    }

    JsonVariantConst properties = findSensorProperties(doc, sensorName);
    if (properties.isNull()) {
        return reading; // sensor not present in this response
    }

    float hs = 0.0f, tp = 0.0f, dir = 0.0f;
    if (!tryParseFloatField(properties, kFieldHs, hs) || !tryParseFloatField(properties, kFieldTp, tp) ||
        !tryParseFloatField(properties, kFieldDirection, dir)) {
        return reading; // core fields missing/null -- that site reported no data this cycle
    }
    reading.hsMetres = hs;
    reading.tpSeconds = tp;
    reading.mwdDeg = dir;

    float spread = 0.0f;
    if (tryParseFloatField(properties, kFieldSpread, spread)) {
        reading.spreadDeg = spread;
    }
    float seaTemp = 0.0f;
    if (tryParseFloatField(properties, kFieldSeaTemp, seaTemp)) {
        reading.seaTempC = seaTemp;
    }
    // Timestamp intentionally left to the caller (e.g. stamp with receipt
    // time) -- CCO's date field (YYYYMMDD#HHMMSS) isn't a standard format
    // worth writing a bespoke parser for here.

    reading.valid = true;
    return reading;
}

} // namespace wave
