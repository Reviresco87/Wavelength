#include "sun_position.h"

#include <algorithm>
#include <cmath>
#include <ctime>

namespace wave {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDegToRad = kPi / 180.0;
constexpr double kRadToDeg = 180.0 / kPi;

} // namespace

SunPosition solarPosition(double latDeg, double lonDeg, int64_t unixTimeSeconds) {
    std::time_t t = static_cast<std::time_t>(unixTimeSeconds);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &t);
#else
    gmtime_r(&t, &utc);
#endif

    // Fractional UTC hour already folds in minutes and seconds -- don't add
    // them again when building true solar time below.
    double hourUtc = utc.tm_hour + utc.tm_min / 60.0 + utc.tm_sec / 3600.0;
    double dayOfYear = static_cast<double>(utc.tm_yday); // 0-based, Jan 1 = 0

    // Fractional year angle (Spencer 1971).
    double gamma = (2.0 * kPi / 365.0) * (dayOfYear + (hourUtc - 12.0) / 24.0);

    double declination = 0.006918 - 0.399912 * std::cos(gamma) + 0.070257 * std::sin(gamma) -
                          0.006758 * std::cos(2 * gamma) + 0.000907 * std::sin(2 * gamma) -
                          0.002697 * std::cos(3 * gamma) + 0.00148 * std::sin(3 * gamma);

    double eqTimeMin = 229.18 * (0.000075 + 0.001868 * std::cos(gamma) - 0.032077 * std::sin(gamma) -
                                  0.014615 * std::cos(2 * gamma) - 0.040849 * std::sin(2 * gamma));

    // True solar time, working entirely in UTC + longitude (no separate
    // civil timezone offset needed -- longitude alone corrects for it).
    double timeOffsetMin = eqTimeMin + 4.0 * lonDeg;
    double trueSolarTimeMin = hourUtc * 60.0 + timeOffsetMin;

    double hourAngleRad = (trueSolarTimeMin / 4.0 - 180.0) * kDegToRad;
    double latRad = latDeg * kDegToRad;

    double sinElevation = std::sin(latRad) * std::sin(declination) +
                           std::cos(latRad) * std::cos(declination) * std::cos(hourAngleRad);
    sinElevation = std::max(-1.0, std::min(1.0, sinElevation));
    double elevationRad = std::asin(sinElevation);

    double azimuthRad = std::atan2(
        -std::sin(hourAngleRad), std::tan(declination) * std::cos(latRad) - std::sin(latRad) * std::cos(hourAngleRad));

    SunPosition pos;
    pos.elevationDeg = static_cast<float>(elevationRad * kRadToDeg);
    pos.azimuthDeg = static_cast<float>(std::fmod(azimuthRad * kRadToDeg + 360.0, 360.0));
    return pos;
}

} // namespace wave
