/**
 * @file AirQualityProfileTranslator.h
 * @brief Translates the configuration the Edge hands the node into its air quality profile.
 *
 * `GET /api/v1/configurations/{deviceId}` answers with the profile of the cloud the node must apply:
 *
 * @code{.json}
 * {
 *   "device_id": "ENV-001", "device_type": "ENVIRONMENTAL_DEVICE", "profile_version": 3,
 *   "thresholds": [{ "metric": "AIR_QUALITY", "unit": "ppm", "normal_min": null, "normal_max": 800,
 *                    "critical_min": null, "critical_max": 1000 }],
 *   "actuation_rules": [], "updated_at": "2026-11-13T15:00:00Z", "synced_at": "2026-11-13T15:05:00Z"
 * }
 * @endcode
 *
 * Only the AIR_QUALITY thresholds concern this node: motion has no thresholds.
 */
#ifndef AIR_QUALITY_PROFILE_TRANSLATOR_H
#define AIR_QUALITY_PROFILE_TRANSLATOR_H

#include <ArduinoJson.h>
#include "AirQualityProfile.h"

/**
 * @brief Anti-corruption translation from the Edge configuration resource to `AirQualityProfile`.
 */
class AirQualityProfileTranslator {
public:
    /// Device type of the Environment Monitor in the Edge and in the cloud.
    static constexpr const char* ENVIRONMENTAL_DEVICE_TYPE = "ENVIRONMENTAL_DEVICE";
    /// Metric of the air quality thresholds.
    static constexpr const char* AIR_QUALITY_METRIC = "AIR_QUALITY";

    /**
     * @brief Reads the air quality profile of a configuration of the Edge.
     * @param configuration Body of the configuration resource.
     * @param destination Receives the profile when the configuration is valid.
     * @return False when the configuration is not of an environmental device, has no air quality
     *         thresholds or breaks the rules of the cloud; the node then keeps its current profile.
     */
    static bool fromEdgeConfiguration(JsonObjectConst configuration, AirQualityProfile& destination);

private:
    /** @return The limit, or NaN when it is null or missing (open side). */
    static float limitOf(JsonObjectConst threshold, const char* key);
};

#endif // AIR_QUALITY_PROFILE_TRANSLATOR_H
