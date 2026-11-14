/**
 * @file EdgeTelemetryPackage.h
 * @brief Telemetry package the QualiTrack Edge Service accepts, with its route and its observation time.
 *
 * The packages of the Modest IoT Nano Framework add an envelope (`timestamp` in milliseconds since boot
 * and `sequence`) that the Edge does not accept: it rejects unknown fields and needs the moment of the
 * observation in ISO 8601. An `EdgeTelemetryPackage` keeps the Unix time of the observation, taken
 * from `TimeSynchronizationDriver` when the reading is made, and names the Edge route it goes to, so
 * `EdgeGatewayClient` can send it without the envelope.
 */
#ifndef EDGE_TELEMETRY_PACKAGE_H
#define EDGE_TELEMETRY_PACKAGE_H

#include <ArduinoJson.h>
#include <cstddef>
#include <cstdint>
#include "TelemetryPackage.h"

/**
 * @brief Base of the packages the node sends to the Edge API.
 */
class EdgeTelemetryPackage : public TelemetryPackage {
public:
    /// Size of an ISO 8601 UTC time ("YYYY-MM-DDTHH:MM:SSZ") with its terminator.
    static constexpr size_t ISO_TIME_BUFFER_SIZE = 21;

    /**
     * @brief Creates a package observed at a moment.
     * @param observedAtEpochSeconds Unix time (UTC seconds) of the observation.
     * @param profileVersion Version of the profile the node applied; 0 when it applies its default profile.
     */
    EdgeTelemetryPackage(uint32_t observedAtEpochSeconds, uint32_t profileVersion);

    ~EdgeTelemetryPackage() override = default;

    /**
     * @brief Route of the Edge API that receives this package, relative to the Edge base URL.
     * @return For example "/api/v1/monitoring/environment-readings".
     */
    [[nodiscard]] virtual const char* getEdgeRoute() const = 0;

    /** @return Unix time (UTC seconds) of the observation. */
    [[nodiscard]] uint32_t getObservedAtEpochSeconds() const;

    /** @return Version of the profile the node applied; 0 for the default profile. */
    [[nodiscard]] uint32_t getProfileVersion() const;

    /**
     * @brief Writes a Unix time as an ISO 8601 UTC time.
     * @param epochSeconds Unix time in seconds.
     * @param destination Buffer of at least ISO_TIME_BUFFER_SIZE characters.
     * @param destinationSize Size of the buffer.
     * @return True when the time was written.
     */
    static bool formatIsoTime(uint32_t epochSeconds, char* destination, size_t destinationSize);

protected:
    /**
     * @brief Adds the profile version when the node applies a profile of the cloud.
     * @param destination JSON document of the package.
     */
    void serializeProfileVersion(JsonDocument& destination) const;

    /**
     * @brief Adds the observation time under the given key, in ISO 8601 UTC.
     * @param destination JSON document of the package.
     * @param key JSON key the Edge expects ("measured_at" or "occurred_at").
     */
    void serializeObservationTime(JsonDocument& destination, const char* key) const;

private:
    uint32_t observedAtEpochSeconds;
    uint32_t appliedProfileVersion;
};

#endif // EDGE_TELEMETRY_PACKAGE_H
