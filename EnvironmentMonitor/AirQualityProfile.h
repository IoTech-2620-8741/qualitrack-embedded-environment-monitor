/**
 * @file AirQualityProfile.h
 * @brief Air quality limits of the environmental profile and the state they give to a reading.
 *
 * The Environment Monitor applies the same rule as the QualiTrack cloud: a value inside the normal
 * range is NORMAL, outside it but inside the critical range WARNING, and beyond the critical range
 * CRITICAL. The limits belong to the less severe state (a value equal to the normal maximum is
 * NORMAL, one equal to the critical maximum is WARNING). Each side of the range is either complete
 * (normal and critical limit) or open.
 *
 * To avoid switching the alarm on and off when a reading stays around a limit, the node only returns
 * to a less severe state once the value is inside its range by the hysteresis margin.
 */
#ifndef AIR_QUALITY_PROFILE_H
#define AIR_QUALITY_PROFILE_H

#include <cmath>
#include <cstdint>

/** @brief Condition of the environment against the profile, as the cloud names it. */
enum class EnvironmentalState : uint8_t {
    Normal,   ///< Inside the normal range.
    Warning,  ///< Outside the normal range, inside the critical range.
    Critical  ///< Beyond the critical range.
};

/**
 * @brief Value object with the air quality limits (ppm) of the profile the node applies.
 *
 * A missing limit is stored as NaN. Version 0 means the default profile of the firmware, used until
 * the Edge hands the node the profile of the cloud.
 */
class AirQualityProfile {
public:
    /// Default normal maximum (ppm) used until the Edge hands the node its profile.
    static constexpr float DEFAULT_NORMAL_MAXIMUM_IN_PPM = 800.0f;
    /// Default critical maximum (ppm) used until the Edge hands the node its profile.
    static constexpr float DEFAULT_CRITICAL_MAXIMUM_IN_PPM = 1000.0f;
    /// Margin (ppm) a value must be inside a less severe range before the node returns to it.
    static constexpr float HYSTERESIS_IN_PPM = 20.0f;
    /// Version of the default profile of the firmware (not a profile of the cloud).
    static constexpr uint32_t DEFAULT_PROFILE_VERSION = 0;

    /** @brief Creates the default profile: open minimum, 800 ppm normal and 1000 ppm critical maximum. */
    AirQualityProfile();

    /**
     * @brief Creates a profile from the limits of the cloud.
     * @param normalMinimum Normal minimum in ppm, or NaN when the lower side is open.
     * @param normalMaximum Normal maximum in ppm, or NaN when the upper side is open.
     * @param criticalMinimum Critical minimum in ppm, or NaN when the lower side is open.
     * @param criticalMaximum Critical maximum in ppm, or NaN when the upper side is open.
     * @param version Version of the profile in the cloud.
     */
    AirQualityProfile(float normalMinimum, float normalMaximum,
                      float criticalMinimum, float criticalMaximum, uint32_t version);

    /**
     * @brief Checks the rules of the cloud: complete sides, at least one side and ordered limits.
     * @return True when the limits form a valid profile.
     */
    [[nodiscard]] bool isValid() const;

    /**
     * @brief Classifies a reading with the rule of the cloud, without hysteresis.
     * @param valueInPpm Air quality reading in ppm.
     * @return The state of the reading.
     */
    [[nodiscard]] EnvironmentalState classify(float valueInPpm) const;

    /**
     * @brief Classifies a reading keeping the previous state until the value leaves it by the hysteresis margin.
     * @param valueInPpm Air quality reading in ppm.
     * @param previousState State the node showed before this reading.
     * @return The state the node must show now. A worse state applies at once.
     */
    [[nodiscard]] EnvironmentalState evaluate(float valueInPpm, EnvironmentalState previousState) const;

    /** @return Version of the profile; 0 for the default profile of the firmware. */
    [[nodiscard]] uint32_t getVersion() const;

    /** @return True when the profile came from the cloud through the Edge. */
    [[nodiscard]] bool isFromCloud() const;

    /**
     * @brief Name of a state as the cloud and the Edge spell it.
     * @param state The state.
     * @return "NORMAL", "WARNING" or "CRITICAL".
     */
    static const char* nameOf(EnvironmentalState state);

private:
    float normalMinimumInPpm;
    float normalMaximumInPpm;
    float criticalMinimumInPpm;
    float criticalMaximumInPpm;
    uint32_t profileVersion;

    /** @return True when the value is inside the range of the state, shrunk by the margin. */
    [[nodiscard]] bool isInsideRangeOf(EnvironmentalState state, float valueInPpm, float marginInPpm) const;
};

#endif // AIR_QUALITY_PROFILE_H
