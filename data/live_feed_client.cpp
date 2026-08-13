#include "live_feed_client.h"

#include "../vendor/ArduinoJson.h"

namespace wave {

namespace {

WavePartition parsePartition(JsonVariantConst obj) {
    WavePartition p;
    if (obj.isNull() || !obj["present"].is<bool>() || !obj["present"].as<bool>()) {
        return p; // present stays false: absent, or the source marked it absent
    }
    if (!obj["hsMetres"].is<float>() || !obj["tpSeconds"].is<float>() || !obj["dirDeg"].is<float>()) {
        return p; // malformed partition: treat as absent rather than guess at values
    }
    p.hsMetres = obj["hsMetres"].as<float>();
    p.tpSeconds = obj["tpSeconds"].as<float>();
    p.dirDeg = obj["dirDeg"].as<float>();
    p.present = true;
    return p;
}

} // namespace

BuoyReading LiveFeedClient::parsePayload(const std::string& json) {
    BuoyReading reading;

    JsonDocument doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok) {
        return reading;
    }

    if (!doc["hsMetres"].is<float>() || !doc["tpSeconds"].is<float>() || !doc["mwdDeg"].is<float>()) {
        return reading; // core bulk fields missing: unparseable/unexpected payload
    }

    reading.hsMetres = doc["hsMetres"].as<float>();
    reading.tpSeconds = doc["tpSeconds"].as<float>();
    reading.mwdDeg = doc["mwdDeg"].as<float>();
    if (doc["spreadDeg"].is<float>()) reading.spreadDeg = doc["spreadDeg"].as<float>();
    if (doc["seaTempC"].is<float>()) reading.seaTempC = doc["seaTempC"].as<float>();

    reading.windSea = parsePartition(doc["windSea"]);
    reading.primarySwell = parsePartition(doc["primarySwell"]);
    reading.secondarySwell = parsePartition(doc["secondarySwell"]);

    if (doc["cloudCoverPresent"].is<bool>() && doc["cloudCoverPresent"].as<bool>() &&
        doc["cloudCoverPercent"].is<float>()) {
        reading.cloudCoverPercent = doc["cloudCoverPercent"].as<float>();
        reading.cloudCoverPresent = true;
    }

    reading.valid = true;
    return reading;
}

} // namespace wave
