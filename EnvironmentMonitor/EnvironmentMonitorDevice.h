/**
 * @file EnvironmentMonitorDevice.h
 * @brief Mediator of the QualiTrack Environment Monitor (one ESP32 per monitored environment).
 *
 * Following the Modest IoT Nano Framework, the device owns its sensors and actuators and reacts to
 * their events on the sampler task:
 *
 * | Story | Behavior |
 * |-------|----------|
 * | MS01 | Reads the air quality (MQ-135) in ppm after a calibration in clean air |
 * | MS02 | Detects motion (PIR) by interrupt |
 * | MS03 | Shows the state of the environment on the RGB LED (blue calibrating, green NORMAL, yellow WARNING, red CRITICAL, purple sensor fault) |
 * | MS04 | Sounds the buzzer once on WARNING and every two seconds while CRITICAL |
 * | MS05 | Sends the readings to the Edge every minute, at once when the state changes, and on every motion change |
 * | MS15 | Applies the air quality profile the Edge hands it, classifying the readings as the cloud does |
 * | US66 | Lets the staff recognize the state of the environment without opening the application |
 *
 * The local reaction does not depend on the network: with no Edge or no profile yet, the node keeps
 * evaluating the readings with its default profile.
 */
#ifndef ENVIRONMENT_MONITOR_DEVICE_H
#define ENVIRONMENT_MONITOR_DEVICE_H

// Only the framework components the node uses, so the Wokwi project needs no other framework file.
#include "Buzzer.h"
#include "Device.h"
#include "GasSensor.h"
#include "PassiveInfraredSensor.h"
#include "RgbLed.h"
#include "AirQualityProfile.h"
#include "EdgeGatewayClient.h"

/**
 * @brief Hands the profile read on the network task to the sampler task.
 *
 * The framework workers share no state but the telemetry queue; this small mailbox, guarded by a
 * FreeRTOS critical section, is the only other value they exchange.
 */
class ProfileMailbox {
public:
    /** @brief Leaves a profile for the sampler task (network task). */
    void post(const AirQualityProfile& profile);

    /**
     * @brief Takes the last profile left, if any (sampler task).
     * @param destination Receives the profile.
     * @return True when there was a new profile.
     */
    bool take(AirQualityProfile& destination);

private:
    portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
    AirQualityProfile pendingProfile;
    bool hasPendingProfile = false;
};

/**
 * @brief Environment Monitor: air quality and motion of an environment, with local alerts.
 */
class EnvironmentMonitorDevice : public Device, public EdgeConfigurationListener {
public:
    /// Tick of the sampler, in milliseconds.
    static constexpr unsigned long SAMPLER_TICK_IN_MILLISECONDS = 100;
    /// Depth of the telemetry and command queues.
    static constexpr uint8_t QUEUE_LENGTH = 10;
    /// Event the scheduler raises to send the air quality even when it does not change (MS05).
    static constexpr int AIR_QUALITY_REPORT_EVENT_IDENTIFIER = 1000;
    /// Interval of the air quality report, which also tells the Edge the node is alive.
    static constexpr uint32_t AIR_QUALITY_REPORT_INTERVAL_IN_MILLISECONDS = 60000;
    /// Event the scheduler raises to repeat the alarm while the environment is CRITICAL.
    static constexpr int ALARM_REPEAT_EVENT_IDENTIFIER = 1001;
    /// Interval of the critical alarm.
    static constexpr uint32_t ALARM_REPEAT_INTERVAL_IN_MILLISECONDS = 2000;
    /// Consecutive failed reads before the sensors report a fault.
    static constexpr uint8_t SENSOR_FAULT_THRESHOLD = 3;

    /// Warning tone: one short beep when the environment enters WARNING.
    static constexpr int WARNING_TONE_FREQUENCY_IN_HERTZ = 1000;
    static constexpr unsigned long WARNING_TONE_DURATION_IN_MILLISECONDS = 200;
    /// Critical tone, repeated every two seconds while CRITICAL.
    static constexpr int CRITICAL_TONE_FREQUENCY_IN_HERTZ = 2000;
    static constexpr unsigned long CRITICAL_TONE_DURATION_IN_MILLISECONDS = 500;

    /**
     * @name MQ-135 curve
     * The framework keeps one custom curve per gas type; the air quality curve of the MQ-135 is stored
     * in the Benzene slot (a gas of the MQ-135 family). With R0 measured in clean air, Rs/R0 = 1 is
     * about 400 ppm and the value grows as the air gets worse: PPM = 10^((log10(Rs/R0) - 0.94) / -0.36).
     * The ppm value is indicative, as report section 5.6 states.
     */
    //@{
    static constexpr GasSensor::GasType AIR_QUALITY_GAS_TYPE = GasSensor::GasType::Benzene;
    static constexpr float AIR_QUALITY_CURVE_SLOPE = -0.36f;
    static constexpr float AIR_QUALITY_CURVE_INTERCEPT = 0.94f;
    //@}

    /**
     * @brief Creates the device, registers its sensors and starts the asynchronous engine.
     * @param motionPin GPIO of the PIR output.
     * @param airQualityPin ADC GPIO of the MQ-135 analog output (through the voltage divider).
     * @param buzzerPin GPIO of the buzzer.
     * @param redPin GPIO of the red channel of the RGB LED.
     * @param greenPin GPIO of the green channel of the RGB LED.
     * @param bluePin GPIO of the blue channel of the RGB LED.
     * @param gateway Gateway to the Edge API.
     */
    EnvironmentMonitorDevice(int motionPin, int airQualityPin, int buzzerPin,
                             int redPin, int greenPin, int bluePin, EdgeGatewayClient& gateway);

    ~EnvironmentMonitorDevice() override;

    /**
     * @brief Reacts to the sensors and to the periodic events of the device.
     * @param event Event raised by a sensor or by the scheduler.
     * @runs_on{sampler}
     */
    void on(Event event) override;

    /** @brief The device receives no commands; its actuators are driven from on(). */
    void handle(Command command) override;

    /**
     * @brief Receives the configuration of the node and leaves its profile for the sampler task.
     * @param configuration Body of the Edge configuration resource.
     * @runs_on{network}
     */
    void onEdgeConfiguration(JsonObjectConst configuration) override;

protected:
    /** @runs_on{network} */
    void processQueuedTelemetryData(const TelemetryPackage* rawQueueItemPayload) const override;

    /** @runs_on{network} */
    void maintainNetworkServices() const override;

private:
    PassiveInfraredSensor motionSensor;
    GasSensor airQualitySensor;
    Buzzer alarmBuzzer;
    RgbLed stateLed;
    EdgeGatewayClient& gatewayClient;
    ProfileMailbox profileMailbox;

    AirQualityProfile profile;
    EnvironmentalState environmentalState;
    bool calibrated;
    bool airQualityFaulted;
    float lastAirQualityInPpm;

    void onAirQualityRead();
    void reportAirQuality();
    void reportMotion(bool motionDetected);
    void applyNewProfile();
    void changeState(EnvironmentalState newState);
    void showState();
    void showColor(const RgbColor& color);
    void playTone(int frequencyInHertz, unsigned long durationInMilliseconds);
};

#endif // ENVIRONMENT_MONITOR_DEVICE_H
