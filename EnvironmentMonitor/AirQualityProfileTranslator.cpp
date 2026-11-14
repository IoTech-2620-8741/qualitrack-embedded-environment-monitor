/**
 * @file AirQualityProfileTranslator.cpp
 * @brief Implements the translation of the Edge configuration into the air quality profile.
 */
#include "AirQualityProfileTranslator.h"
#include <cmath>
#include <cstring>

bool AirQualityProfileTranslator::fromEdgeConfiguration(const JsonObjectConst configuration,
                                                         AirQualityProfile& destination) {
    const char* deviceType = configuration["device_type"];
    if (deviceType == nullptr || strcmp(deviceType, ENVIRONMENTAL_DEVICE_TYPE) != 0) return false;

    const JsonVariantConst version = configuration["profile_version"];
    if (!version.is<uint32_t>() || version.as<uint32_t>() == AirQualityProfile::DEFAULT_PROFILE_VERSION) return false;

    for (const JsonObjectConst threshold : configuration["thresholds"].as<JsonArrayConst>()) {
        const char* metric = threshold["metric"];
        if (metric == nullptr || strcmp(metric, AIR_QUALITY_METRIC) != 0) continue;
        const AirQualityProfile profile(limitOf(threshold, "normal_min"), limitOf(threshold, "normal_max"),
                                        limitOf(threshold, "critical_min"), limitOf(threshold, "critical_max"),
                                        version.as<uint32_t>());
        if (!profile.isValid()) return false;
        destination = profile;
        return true;
    }
    return false;
}

float AirQualityProfileTranslator::limitOf(const JsonObjectConst threshold, const char* key) {
    const JsonVariantConst limit = threshold[key];
    return limit.is<float>() ? limit.as<float>() : NAN;
}
