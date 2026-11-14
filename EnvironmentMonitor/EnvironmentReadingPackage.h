/**
 * @file EnvironmentReadingPackage.h
 * @brief Metrics the Environment Monitor measured at one moment (Edge route environment-readings, TS55).
 *
 * The Edge receives the air quality in ppm and whether motion was detected, both optional but at
 * least one of them, with the time of the measurement:
 *
 * @code{.json}
 * { "measured_at": "2026-11-13T15:00:00Z", "air_quality": 412.5, "motion": false, "profile_version": 3 }
 * @endcode
 *
 * `EdgeGatewayClient` adds the `device_id` of the node.
 */
#ifndef ENVIRONMENT_READING_PACKAGE_H
#define ENVIRONMENT_READING_PACKAGE_H

#include <memory>
#include "EdgeTelemetryPackage.h"

/**
 * @brief Air quality and motion of one moment, as the Edge API receives them.
 */
class EnvironmentReadingPackage : public EdgeTelemetryPackage {
public:
    /// Edge route of the readings of the environmental device.
    static constexpr const char* EDGE_ROUTE = "/api/v1/monitoring/environment-readings";

    /**
     * @brief Creates a reading with the air quality only.
     * @param measuredAtEpochSeconds Unix time of the measurement.
     * @param airQualityInPpm Air quality in ppm.
     * @param profileVersion Version of the profile the node applied (0 for the default profile).
     * @return The reading, ready to be enqueued.
     */
    static std::unique_ptr<EnvironmentReadingPackage> airQuality(uint32_t measuredAtEpochSeconds, float airQualityInPpm,
                                                uint32_t profileVersion);

    /**
     * @brief Creates a reading with the motion only.
     * @param measuredAtEpochSeconds Unix time of the detection or of the quiet zone.
     * @param motionDetected True when motion was detected, false when the zone is quiet again.
     * @param profileVersion Version of the profile the node applied (0 for the default profile).
     * @return The reading, ready to be enqueued.
     */
    static std::unique_ptr<EnvironmentReadingPackage> motion(uint32_t measuredAtEpochSeconds, bool motionDetected,
                                            uint32_t profileVersion);

    ~EnvironmentReadingPackage() override = default;

    [[nodiscard]] const char* getEdgeRoute() const override;

    /**
     * @brief Writes the fields of the Edge request (without the framework envelope).
     * @param serializationDestination JSON document of the request.
     */
    void serialize(JsonDocument& serializationDestination) const override;

    /** @return True when the reading carries the air quality. */
    [[nodiscard]] bool hasAirQuality() const;

    /** @return Air quality in ppm (meaningful only when hasAirQuality()). */
    [[nodiscard]] float getAirQualityInPpm() const;

private:
    EnvironmentReadingPackage(uint32_t measuredAtEpochSeconds, uint32_t profileVersion,
                              bool includesAirQuality, float airQualityInPpm,
                              bool includesMotion, bool motionDetected);

    bool includesAirQuality;
    float airQualityInPpm;
    bool includesMotion;
    bool motionDetected;
};

#endif // ENVIRONMENT_READING_PACKAGE_H
