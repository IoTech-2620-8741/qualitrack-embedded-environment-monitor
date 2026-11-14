/**
 * @file AirQualityProfile.cpp
 * @brief Implements the classification of air quality readings with the rule of the cloud.
 */
#include "AirQualityProfile.h"
#include <initializer_list>

namespace {
    /// Order of severity of a state, to compare two states.
    uint8_t severityOf(const EnvironmentalState state) {
        return static_cast<uint8_t>(state);
    }

    /// True when the limit is present (the side of the range is not open).
    bool isPresent(const float limit) {
        return !std::isnan(limit);
    }
}

AirQualityProfile::AirQualityProfile()
    : AirQualityProfile(NAN, DEFAULT_NORMAL_MAXIMUM_IN_PPM, NAN, DEFAULT_CRITICAL_MAXIMUM_IN_PPM,
                        DEFAULT_PROFILE_VERSION) {}

AirQualityProfile::AirQualityProfile(const float normalMinimum, const float normalMaximum,
                                     const float criticalMinimum, const float criticalMaximum,
                                     const uint32_t version)
    : normalMinimumInPpm(normalMinimum),
      normalMaximumInPpm(normalMaximum),
      criticalMinimumInPpm(criticalMinimum),
      criticalMaximumInPpm(criticalMaximum),
      profileVersion(version) {}

bool AirQualityProfile::isValid() const {
    const bool lowerSideComplete = isPresent(normalMinimumInPpm) == isPresent(criticalMinimumInPpm);
    const bool upperSideComplete = isPresent(normalMaximumInPpm) == isPresent(criticalMaximumInPpm);
    if (!lowerSideComplete || !upperSideComplete) return false;
    if (!isPresent(normalMinimumInPpm) && !isPresent(normalMaximumInPpm)) return false;
    for (const float limit : {normalMinimumInPpm, normalMaximumInPpm, criticalMinimumInPpm, criticalMaximumInPpm}) {
        if (isPresent(limit) && std::isinf(limit)) return false;
    }
    if (isPresent(normalMinimumInPpm) && criticalMinimumInPpm >= normalMinimumInPpm) return false;
    if (isPresent(normalMaximumInPpm) && criticalMaximumInPpm <= normalMaximumInPpm) return false;
    if (isPresent(normalMinimumInPpm) && isPresent(normalMaximumInPpm) && normalMinimumInPpm >= normalMaximumInPpm) {
        return false;
    }
    return true;
}

EnvironmentalState AirQualityProfile::classify(const float valueInPpm) const {
    if (isPresent(criticalMinimumInPpm) && valueInPpm < criticalMinimumInPpm) return EnvironmentalState::Critical;
    if (isPresent(criticalMaximumInPpm) && valueInPpm > criticalMaximumInPpm) return EnvironmentalState::Critical;
    if (isPresent(normalMinimumInPpm) && valueInPpm < normalMinimumInPpm) return EnvironmentalState::Warning;
    if (isPresent(normalMaximumInPpm) && valueInPpm > normalMaximumInPpm) return EnvironmentalState::Warning;
    return EnvironmentalState::Normal;
}

EnvironmentalState AirQualityProfile::evaluate(const float valueInPpm, const EnvironmentalState previousState) const {
    EnvironmentalState candidate = classify(valueInPpm);
    // A worse (or the same) state applies at once; a better one only with the hysteresis margin.
    while (severityOf(candidate) < severityOf(previousState)
           && !isInsideRangeOf(candidate, valueInPpm, HYSTERESIS_IN_PPM)) {
        candidate = static_cast<EnvironmentalState>(severityOf(candidate) + 1);
    }
    return candidate;
}

bool AirQualityProfile::isInsideRangeOf(const EnvironmentalState state, const float valueInPpm,
                                        const float marginInPpm) const {
    switch (state) {
        case EnvironmentalState::Normal:
            return (!isPresent(normalMinimumInPpm) || valueInPpm >= normalMinimumInPpm + marginInPpm)
                && (!isPresent(normalMaximumInPpm) || valueInPpm <= normalMaximumInPpm - marginInPpm);
        case EnvironmentalState::Warning:
            return (!isPresent(criticalMinimumInPpm) || valueInPpm >= criticalMinimumInPpm + marginInPpm)
                && (!isPresent(criticalMaximumInPpm) || valueInPpm <= criticalMaximumInPpm - marginInPpm);
        case EnvironmentalState::Critical:
        default:
            return true;
    }
}

uint32_t AirQualityProfile::getVersion() const {
    return profileVersion;
}

bool AirQualityProfile::isFromCloud() const {
    return profileVersion != DEFAULT_PROFILE_VERSION;
}

const char* AirQualityProfile::nameOf(const EnvironmentalState state) {
    switch (state) {
        case EnvironmentalState::Warning: return "WARNING";
        case EnvironmentalState::Critical: return "CRITICAL";
        case EnvironmentalState::Normal:
        default: return "NORMAL";
    }
}
