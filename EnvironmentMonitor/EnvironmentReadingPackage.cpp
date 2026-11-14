/**
 * @file EnvironmentReadingPackage.cpp
 * @brief Implements the readings of the Environment Monitor in the format of the Edge API.
 */
#include "EnvironmentReadingPackage.h"
#include <cmath>

EnvironmentReadingPackage::EnvironmentReadingPackage(const uint32_t measuredAtEpochSeconds, const uint32_t profileVersion,
                                                     const bool includesAirQuality, const float airQualityInPpm,
                                                     const bool includesMotion, const bool motionDetected)
    : EdgeTelemetryPackage(measuredAtEpochSeconds, profileVersion),
      includesAirQuality(includesAirQuality),
      airQualityInPpm(airQualityInPpm),
      includesMotion(includesMotion),
      motionDetected(motionDetected) {}

std::unique_ptr<EnvironmentReadingPackage> EnvironmentReadingPackage::airQuality(
        const uint32_t measuredAtEpochSeconds, const float airQualityInPpm, const uint32_t profileVersion) {
    return std::unique_ptr<EnvironmentReadingPackage>(
        new EnvironmentReadingPackage(measuredAtEpochSeconds, profileVersion, true, airQualityInPpm, false, false));
}

std::unique_ptr<EnvironmentReadingPackage> EnvironmentReadingPackage::motion(
        const uint32_t measuredAtEpochSeconds, const bool motionDetected, const uint32_t profileVersion) {
    return std::unique_ptr<EnvironmentReadingPackage>(
        new EnvironmentReadingPackage(measuredAtEpochSeconds, profileVersion, false, 0.0f, true, motionDetected));
}

const char* EnvironmentReadingPackage::getEdgeRoute() const {
    return EDGE_ROUTE;
}

void EnvironmentReadingPackage::serialize(JsonDocument& serializationDestination) const {
    serializeObservationTime(serializationDestination, "measured_at");
    if (includesAirQuality) {
        // One decimal is the resolution of the indicative ppm value of the MQ-135.
        serializationDestination["air_quality"] = roundf(airQualityInPpm * 10.0f) / 10.0f;
    }
    if (includesMotion) {
        serializationDestination["motion"] = motionDetected;
    }
    serializeProfileVersion(serializationDestination);
}

bool EnvironmentReadingPackage::hasAirQuality() const {
    return includesAirQuality;
}

float EnvironmentReadingPackage::getAirQualityInPpm() const {
    return airQualityInPpm;
}
