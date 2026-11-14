/**
 * @file EdgeTelemetryPackage.cpp
 * @brief Implements the observation time and the profile version of the packages sent to the Edge.
 */
#include "EdgeTelemetryPackage.h"
#include <ctime>

EdgeTelemetryPackage::EdgeTelemetryPackage(const uint32_t observedAtEpochSeconds, const uint32_t profileVersion)
    : observedAtEpochSeconds(observedAtEpochSeconds), appliedProfileVersion(profileVersion) {}

uint32_t EdgeTelemetryPackage::getObservedAtEpochSeconds() const {
    return observedAtEpochSeconds;
}

uint32_t EdgeTelemetryPackage::getProfileVersion() const {
    return appliedProfileVersion;
}

bool EdgeTelemetryPackage::formatIsoTime(const uint32_t epochSeconds, char* destination, const size_t destinationSize) {
    if (destination == nullptr || destinationSize < ISO_TIME_BUFFER_SIZE) return false;
    const time_t time = static_cast<time_t>(epochSeconds);
    tm utcTime{};
    if (gmtime_r(&time, &utcTime) == nullptr) return false;
    return strftime(destination, destinationSize, "%Y-%m-%dT%H:%M:%SZ", &utcTime) > 0;
}

void EdgeTelemetryPackage::serializeProfileVersion(JsonDocument& destination) const {
    if (appliedProfileVersion != 0) {
        destination["profile_version"] = appliedProfileVersion;
    }
}

void EdgeTelemetryPackage::serializeObservationTime(JsonDocument& destination, const char* key) const {
    char isoTime[ISO_TIME_BUFFER_SIZE];
    if (formatIsoTime(observedAtEpochSeconds, isoTime, sizeof(isoTime))) {
        destination[key] = isoTime; // A char array is copied into the document.
    }
}
