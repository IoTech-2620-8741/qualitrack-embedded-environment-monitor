/**
 * @file EnvironmentMonitorDevice.cpp
 * @brief Implements the reactions of the Environment Monitor to its sensors and to the Edge profile.
 */
#include "EnvironmentMonitorDevice.h"
#include "AirQualityProfileTranslator.h"
#include "EnvironmentReadingPackage.h"
#include "TimeSynchronizationDriver.h"

namespace {
    constexpr const char* LOG_TAG = "EnvironmentMonitor";
}

void ProfileMailbox::post(const AirQualityProfile& profile) {
    portENTER_CRITICAL(&lock);
    pendingProfile = profile;
    hasPendingProfile = true;
    portEXIT_CRITICAL(&lock);
}

bool ProfileMailbox::take(AirQualityProfile& destination) {
    portENTER_CRITICAL(&lock);
    const bool hadProfile = hasPendingProfile;
    if (hadProfile) {
        destination = pendingProfile;
        hasPendingProfile = false;
    }
    portEXIT_CRITICAL(&lock);
    return hadProfile;
}

EnvironmentMonitorDevice::EnvironmentMonitorDevice(const int motionPin, const int airQualityPin, const int buzzerPin,
                                                   const int redPin, const int greenPin, const int bluePin,
                                                   EdgeGatewayClient& gateway)
    : Device(SAMPLER_TICK_IN_MILLISECONDS),
      motionSensor(motionPin, this),
      airQualitySensor(airQualityPin, -1, this, GasSensor::OperatingMode::RawMode),
      alarmBuzzer(buzzerPin, this),
      stateLed(redPin, greenPin, bluePin, false, this),
      gatewayClient(gateway),
      environmentalState(EnvironmentalState::Normal),
      calibrated(false),
      airQualityFaulted(false),
      lastAirQualityInPpm(0.0f)
{
    airQualitySensor.setGasCalibrationParameters(AIR_QUALITY_GAS_TYPE, AIR_QUALITY_CURVE_SLOPE,
                                                 AIR_QUALITY_CURVE_INTERCEPT);
    airQualitySensor.setTargetGas(AIR_QUALITY_GAS_TYPE);
    airQualitySensor.setFaultThreshold(SENSOR_FAULT_THRESHOLD);
    // R0 is measured during the first 30 seconds: the node must start in clean air.
    airQualitySensor.startCalibration();
    showColor(RgbLed::BLUE);

    gatewayClient.setConfigurationListener(this);

    // The scheduler only accepts registrations before the engine starts.
    appendSensorToScheduler(&airQualitySensor, Sensor::MEASURE_DATA_REQUESTED_EVENT_IDENTIFIER);
    appendSensorToTriggeringScheduler(&motionSensor, Sensor::MEASURE_DATA_REQUESTED_EVENT_IDENTIFIER, CHANGE);
    appendSensorToScheduler(this, AIR_QUALITY_REPORT_EVENT_IDENTIFIER, AIR_QUALITY_REPORT_INTERVAL_IN_MILLISECONDS);
    appendSensorToScheduler(this, ALARM_REPEAT_EVENT_IDENTIFIER, ALARM_REPEAT_INTERVAL_IN_MILLISECONDS);

    initializeAsynchronousEngine(QUEUE_LENGTH);
    setTelemetryGateway(&gatewayClient);
    MODEST_LOG_I(LOG_TAG, "Calibrating the MQ-135 in clean air for 30 seconds");
}

EnvironmentMonitorDevice::~EnvironmentMonitorDevice() {
    stopAsynchronousEngine();
}

void EnvironmentMonitorDevice::on(const Event event) {
    switch (event.identifier) {
        case Sensor::DATA_READ_EVENT_IDENTIFIER:
            onAirQualityRead();
            break;
        case PassiveInfraredSensor::MOTION_DETECTED_EVENT_IDENTIFIER:
            reportMotion(true);
            break;
        case PassiveInfraredSensor::MOTION_STOPPED_EVENT_IDENTIFIER:
            reportMotion(false);
            break;
        case AIR_QUALITY_REPORT_EVENT_IDENTIFIER:
            applyNewProfile();
            reportAirQuality();
            break;
        case ALARM_REPEAT_EVENT_IDENTIFIER:
            if (calibrated && environmentalState == EnvironmentalState::Critical) {
                playTone(CRITICAL_TONE_FREQUENCY_IN_HERTZ, CRITICAL_TONE_DURATION_IN_MILLISECONDS);
            }
            break;
        case Sensor::SENSOR_FAULT_EVENT_IDENTIFIER:
            airQualityFaulted = true;
            MODEST_LOG_E(LOG_TAG, "Sensor fault on GPIO %d", event.sourceId);
            showColor(RgbLed::PURPLE);
            break;
        case Sensor::SENSOR_RECOVERED_EVENT_IDENTIFIER:
            airQualityFaulted = false;
            MODEST_LOG_I(LOG_TAG, "Sensor recovered on GPIO %d", event.sourceId);
            showState();
            break;
        default:
            break;
    }
}

void EnvironmentMonitorDevice::handle(Command command) {
    (void) command;
}

void EnvironmentMonitorDevice::onEdgeConfiguration(const JsonObjectConst configuration) {
    AirQualityProfile edgeProfile;
    if (AirQualityProfileTranslator::fromEdgeConfiguration(configuration, edgeProfile)) {
        profileMailbox.post(edgeProfile);
    } else {
        MODEST_LOG_W(LOG_TAG, "The configuration of the Edge has no valid air quality profile");
    }
}

void EnvironmentMonitorDevice::processQueuedTelemetryData(const TelemetryPackage* rawQueueItemPayload) const {
    if (rawQueueItemPayload != nullptr) {
        gatewayClient.sendTelemetryRecord(*rawQueueItemPayload);
    }
}

void EnvironmentMonitorDevice::maintainNetworkServices() const {
    gatewayClient.service();
}

void EnvironmentMonitorDevice::onAirQualityRead() {
    if (airQualitySensor.getOperatingMode() != GasSensor::OperatingMode::AccurateMode) return;
    lastAirQualityInPpm = airQualitySensor.getGasConcentrationInPpm();
    if (!calibrated) {
        calibrated = true;
        MODEST_LOG_I(LOG_TAG, "MQ-135 calibrated; first reading %.1f ppm", lastAirQualityInPpm);
        changeState(profile.classify(lastAirQualityInPpm));
        reportAirQuality();
        return;
    }
    applyNewProfile();
    const EnvironmentalState newState = profile.evaluate(lastAirQualityInPpm, environmentalState);
    if (newState != environmentalState) {
        changeState(newState);
        reportAirQuality(); // The Edge and the cloud learn about the change at once.
    }
}

void EnvironmentMonitorDevice::reportAirQuality() {
    if (!calibrated) return;
    if (!EdgeGatewayClient::isClockSynchronized()) {
        MODEST_LOG_W(LOG_TAG, "Air quality %.1f ppm not sent: clock not synchronized", lastAirQualityInPpm);
        return;
    }
    enqueueTelemetryPayload(EnvironmentReadingPackage::airQuality(
        TimeSynchronizationDriver::getCurrentTimestamp(), lastAirQualityInPpm, profile.getVersion()));
}

void EnvironmentMonitorDevice::reportMotion(const bool motionDetected) {
    MODEST_LOG_I(LOG_TAG, "Motion %s", motionDetected ? "detected" : "stopped");
    if (!EdgeGatewayClient::isClockSynchronized()) {
        MODEST_LOG_W(LOG_TAG, "Motion not sent: clock not synchronized");
        return;
    }
    enqueueTelemetryPayload(EnvironmentReadingPackage::motion(
        TimeSynchronizationDriver::getCurrentTimestamp(), motionDetected, profile.getVersion()));
}

void EnvironmentMonitorDevice::applyNewProfile() {
    AirQualityProfile edgeProfile;
    if (!profileMailbox.take(edgeProfile) || edgeProfile.getVersion() == profile.getVersion()) return;
    profile = edgeProfile;
    MODEST_LOG_I(LOG_TAG, "Applying version %u of the air quality profile", static_cast<unsigned>(profile.getVersion()));
    if (calibrated) {
        const EnvironmentalState newState = profile.classify(lastAirQualityInPpm);
        if (newState != environmentalState) changeState(newState);
    }
}

void EnvironmentMonitorDevice::changeState(const EnvironmentalState newState) {
    const EnvironmentalState previousState = environmentalState;
    environmentalState = newState;
    MODEST_LOG_I(LOG_TAG, "Air quality %.1f ppm: %s -> %s", lastAirQualityInPpm,
                 AirQualityProfile::nameOf(previousState), AirQualityProfile::nameOf(newState));
    showState();
    switch (newState) {
        case EnvironmentalState::Warning:
            playTone(WARNING_TONE_FREQUENCY_IN_HERTZ, WARNING_TONE_DURATION_IN_MILLISECONDS);
            break;
        case EnvironmentalState::Critical:
            playTone(CRITICAL_TONE_FREQUENCY_IN_HERTZ, CRITICAL_TONE_DURATION_IN_MILLISECONDS);
            break;
        case EnvironmentalState::Normal:
        default:
            alarmBuzzer.handle(Buzzer::TURN_OFF_COMMAND);
            break;
    }
}

void EnvironmentMonitorDevice::showState() {
    if (airQualityFaulted) {
        showColor(RgbLed::PURPLE);
        return;
    }
    if (!calibrated) {
        showColor(RgbLed::BLUE);
        return;
    }
    switch (environmentalState) {
        case EnvironmentalState::Warning: showColor(RgbLed::YELLOW); break;
        case EnvironmentalState::Critical: showColor(RgbLed::RED); break;
        case EnvironmentalState::Normal:
        default: showColor(RgbLed::GREEN); break;
    }
}

void EnvironmentMonitorDevice::showColor(const RgbColor& color) {
    stateLed.setColor(color);
    stateLed.handle(RgbLed::SET_COLOR_COMMAND);
}

void EnvironmentMonitorDevice::playTone(const int frequencyInHertz, const unsigned long durationInMilliseconds) {
    alarmBuzzer.setTone(frequencyInHertz, durationInMilliseconds);
    alarmBuzzer.handle(Buzzer::PLAY_TONE_COMMAND);
}
