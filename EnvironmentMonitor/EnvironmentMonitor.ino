/**
 * @file EnvironmentMonitor.ino
 * @brief Embedded Application of the QualiTrack Environment Monitor (ESP32 DevKit v1) on the Modest IoT Nano Framework.
 *
 * One Environment Monitor watches each environment of a pharmaceutical facility: it measures the air quality
 * (MQ-135) and detects motion (PIR), shows the state of the environment on an RGB LED, warns with a buzzer, and
 * sends its readings to the QualiTrack Edge Service of the local station, which synchronizes them with the cloud.
 *
 * | Story | Behavior |
 * |-------|----------|
 * | MS01 | Air quality in ppm after a 30-second calibration in clean air, classified every second |
 * | MS02 | Motion detected by interrupt |
 * | MS03 | RGB LED: blue calibrating, green NORMAL, yellow WARNING, red CRITICAL, purple sensor fault |
 * | MS04 | One beep on WARNING and a beep every two seconds while CRITICAL |
 * | MS05 | Air quality sent when the state changes and every minute; motion sent on every change |
 * | MS14 | Sign-in with the identifier and the API key of the node |
 * | MS15 | Air quality profile read from the Edge every five minutes and applied |
 *
 * Everything comes from the framework (`GasSensor`, `PassiveInfraredSensor`, `RgbLed`, `Buzzer`,
 * `WiFiConnectivityDriver`, `TimeSynchronizationDriver`, `HttpGatewayClient`). The sketch only adds what the framework
 * asks an application to write: the telemetry package of a reading and the device that reacts to the events.
 *
 * Requirements: ESP32 core 3.x and the library Modest-IoT-Nano-Framework 1.0.0 (Arduino Library Manager, with
 * ArduinoJson and MQTT). Copy secrets.example.h as secrets.h with the values of the node.
 *
 * Circuit of report section 5.6, with an RGB LED instead of the LED ring:
 *
 * | Component | ESP32 |
 * |-----------|-------|
 * | PIR HC-SR501 output | GPIO 27 |
 * | MQ-135 analog output (2.2 kΩ / 3.3 kΩ divider) | GPIO 34 |
 * | Buzzer | GPIO 25 |
 * | RGB LED red / green / blue (220 Ω each, common cathode) | GPIO 4 / 16 / 17 |
 */
#include <ModestIoT.h>
#include <algorithm>
#include <cmath>
#include <ctime>
#include <memory>

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h" // Example values, enough for Wokwi.
#endif

/** @name Pins of the circuit */
//@{
static constexpr int MOTION_PIN = 27;
static constexpr int AIR_QUALITY_PIN = 34;
static constexpr int BUZZER_PIN = 25;
static constexpr int LED_RED_PIN = 4;
static constexpr int LED_GREEN_PIN = 16;
static constexpr int LED_BLUE_PIN = 17;
//@}

/** @name Routes of the QualiTrack Edge API */
//@{
static constexpr const char* SIGN_IN_ROUTE = "/api/v1/auth/sign-in";
static constexpr const char* READINGS_ROUTE = "/api/v1/monitoring/environment-readings";
static constexpr const char* CONFIGURATION_ROUTE = "/api/v1/configurations/";
//@}

static constexpr unsigned long SERIAL_BAUD_RATE = 115200;
static constexpr unsigned long MAIN_LOOP_DELAY_IN_MILLISECONDS = 60000;

/**
 * @brief One observation of the environment, in the JSON the Edge expects.
 *
 * It carries the air quality or the motion, the UTC time of the observation and the version of the profile applied.
 * It is sent with `HttpGatewayClient::sendRequest()`, because the Edge accepts only its own fields and
 * `sendTelemetryRecord()` would add the `timestamp` and `sequence` of the framework.
 */
class EnvironmentReading : public TelemetryPackage {
public:
    static std::unique_ptr<EnvironmentReading> airQuality(uint32_t observedAt, float ppm, uint32_t profileVersion) {
        return std::unique_ptr<EnvironmentReading>(new EnvironmentReading(observedAt, true, ppm, false, profileVersion));
    }

    static std::unique_ptr<EnvironmentReading> motion(uint32_t observedAt, bool detected, uint32_t profileVersion) {
        return std::unique_ptr<EnvironmentReading>(new EnvironmentReading(observedAt, false, 0, detected, profileVersion));
    }

    void serialize(JsonDocument& json) const override {
        char measuredAt[21];
        const time_t seconds = observedAt;
        tm utc{};
        gmtime_r(&seconds, &utc);
        strftime(measuredAt, sizeof(measuredAt), "%Y-%m-%dT%H:%M:%SZ", &utc);

        json["device_id"] = DEVICE_ID;
        json["measured_at"] = measuredAt; // A char array is copied into the document.
        if (hasAirQuality) {
            json["air_quality"] = roundf(airQualityInPpm * 10.0f) / 10.0f; // The MQ-135 ppm is indicative.
        } else {
            json["motion"] = motionDetected;
        }
        if (profileVersion != 0) json["profile_version"] = profileVersion;
    }

private:
    EnvironmentReading(uint32_t observedAt, bool hasAirQuality, float ppm, bool detected, uint32_t profileVersion)
        : observedAt(observedAt), hasAirQuality(hasAirQuality), airQualityInPpm(ppm), motionDetected(detected),
          profileVersion(profileVersion) {}

    uint32_t observedAt;
    bool hasAirQuality;
    float airQualityInPpm;
    bool motionDetected;
    uint32_t profileVersion;
};

/**
 * @brief Environment Monitor: reacts on the sampler task to its sensors and talks to the Edge on the network task.
 */
class EnvironmentMonitor : public Device {
public:
    EnvironmentMonitor(WiFiConnectivityDriver& wifiDriver, HttpGatewayClient& edgeGateway)
        : Device(SAMPLER_TICK_IN_MILLISECONDS),
          motionSensor(MOTION_PIN, this),
          airQualitySensor(AIR_QUALITY_PIN, -1, this, GasSensor::OperatingMode::RawMode),
          alarmBuzzer(BUZZER_PIN, this),
          stateLed(LED_RED_PIN, LED_GREEN_PIN, LED_BLUE_PIN, false, this),
          wifi(wifiDriver),
          edge(edgeGateway)
    {
        snprintf(signInUrl, sizeof(signInUrl), "%s%s", EDGE_BASE_URL, SIGN_IN_ROUTE);
        snprintf(readingsUrl, sizeof(readingsUrl), "%s%s", EDGE_BASE_URL, READINGS_ROUTE);
        snprintf(configurationUrl, sizeof(configurationUrl), "%s%s%s", EDGE_BASE_URL, CONFIGURATION_ROUTE, DEVICE_ID);

        // MQ-135 curve, kept in the Benzene slot: Rs/R0 = 1 is about 400 ppm in clean air.
        airQualitySensor.setGasCalibrationParameters(GasSensor::GasType::Benzene, CURVE_SLOPE, CURVE_INTERCEPT);
        airQualitySensor.setTargetGas(GasSensor::GasType::Benzene);
        airQualitySensor.setFaultThreshold(SENSOR_FAULT_THRESHOLD);
        airQualitySensor.startCalibration(); // R0 is measured in the first 30 seconds: start in clean air.
        showColor(RgbLed::BLUE);

        // The scheduler only accepts registrations before the engine starts.
        appendSensorToScheduler(&airQualitySensor, Sensor::MEASURE_DATA_REQUESTED_EVENT_IDENTIFIER);
        appendSensorToTriggeringScheduler(&motionSensor, Sensor::MEASURE_DATA_REQUESTED_EVENT_IDENTIFIER, CHANGE);
        appendSensorToScheduler(this, EVALUATE_EVENT, 1000);       // Classify the latest sample every second.
        appendSensorToScheduler(this, REPORT_EVENT, 60000);        // Send the air quality every minute.
        appendSensorToScheduler(this, ALARM_REPEAT_EVENT, 2000);   // Beep every two seconds while CRITICAL.
        initializeAsynchronousEngine(QUEUE_LENGTH);
        Serial.println("[EnvironmentMonitor] Calibrating the MQ-135 in clean air for 30 seconds");
    }

    ~EnvironmentMonitor() override { stopAsynchronousEngine(); }

    /** @runs_on{sampler} */
    void on(Event event) override {
        switch (event.identifier) {
            case Sensor::DATA_READ_EVENT_IDENTIFIER: finishCalibration(); break;
            case EVALUATE_EVENT: evaluateAirQuality(); break;
            case REPORT_EVENT: sendAirQuality(); break;
            case ALARM_REPEAT_EVENT:
                if (calibrated && state == State::Critical) playTone(2000, 500);
                break;
            case PassiveInfraredSensor::MOTION_DETECTED_EVENT_IDENTIFIER: sendMotion(true); break;
            case PassiveInfraredSensor::MOTION_STOPPED_EVENT_IDENTIFIER: sendMotion(false); break;
            case Sensor::SENSOR_FAULT_EVENT_IDENTIFIER:
                faulted = true;
                Serial.println("[EnvironmentMonitor] MQ-135 fault");
                showState();
                break;
            case Sensor::SENSOR_RECOVERED_EVENT_IDENTIFIER:
                faulted = false;
                Serial.println("[EnvironmentMonitor] MQ-135 recovered");
                showState();
                break;
            default: break;
        }
    }

    /**
     * @brief Receives the air quality profile of the Edge, one limit per command, then its version.
     * @runs_on{sampler}
     */
    void handle(Command command) override {
        const float value = command.payload == NO_LIMIT ? NAN : command.payload / 10.0f;
        switch (command.identifier) {
            case SET_NORMAL_MINIMUM: pending.normalMin = value; break;
            case SET_NORMAL_MAXIMUM: pending.normalMax = value; break;
            case SET_CRITICAL_MINIMUM: pending.criticalMin = value; break;
            case SET_CRITICAL_MAXIMUM: pending.criticalMax = value; break;
            case APPLY_PROFILE:
                pending.version = static_cast<uint32_t>(command.payload);
                profile = pending;
                Serial.printf("[EnvironmentMonitor] Air quality profile version %lu applied\n",
                              static_cast<unsigned long>(profile.version));
                if (calibrated && changeState(profile.classify(lastPpm))) sendAirQuality();
                break;
            default: break;
        }
    }

protected:
    /** @runs_on{network} */
    void processQueuedTelemetryData(const TelemetryPackage* package) const override {
        if (package == nullptr) return;
        JsonDocument json;
        package->serialize(json);
        char body[256];
        serializeJson(json, body, sizeof(body));
        edge.setServiceEndpointUrl(readingsUrl);
        if (edge.sendRequest(HttpGatewayClient::HttpMethod::Post, body)) return;
        const int status = edge.getLastResponseStatusCode();
        if (status == 401) signedIn = false;
        Serial.printf("[EnvironmentMonitor] Reading not delivered (status %d): %s\n", status, body);
    }

    /** @runs_on{network} */
    void maintainNetworkServices() const override {
        if (!wifi.isConnected()) return;
        const unsigned long now = millis();
        if (!TimeSynchronizationDriver::isSynchronized() && due(lastClockAttemptAt, now, 30000)) {
            // The driver marks the clock as synchronized only inside initialize(), so it is retried.
            TimeSynchronizationDriver::initialize(TIME_ZONE, NTP_SERVER);
            if (TimeSynchronizationDriver::isSynchronized()) Serial.println("[EnvironmentMonitor] Clock synchronized");
        }
        if (!signedIn && due(lastSignInAttemptAt, now, 30000)) signIn();
        if (due(lastConfigurationAttemptAt, now, configurationRead ? 300000 : 30000)) readConfiguration();
    }

private:
    enum class State { Normal, Warning, Critical };

    /** @brief Air quality limits in ppm (NAN when a side has no limit); the cloud rule decides the state. */
    struct Profile {
        float normalMin = NAN, normalMax = 800, criticalMin = NAN, criticalMax = 1000;
        uint32_t version = 0; // 0 = default profile of the node, until the Edge hands one.

        State classify(float ppm) const {
            if (ppm < criticalMin || ppm > criticalMax) return State::Critical; // false for NAN limits
            if (ppm < normalMin || ppm > normalMax) return State::Warning;
            return State::Normal;
        }
    };

    static constexpr unsigned long SAMPLER_TICK_IN_MILLISECONDS = 100;
    static constexpr uint8_t QUEUE_LENGTH = 10;
    static constexpr uint8_t SENSOR_FAULT_THRESHOLD = 3;
    static constexpr float CURVE_SLOPE = -0.36f;    // PPM = 10^((log10(Rs/R0) - 0.94) / -0.36)
    static constexpr float CURVE_INTERCEPT = 0.94f;
    static constexpr float HYSTERESIS_IN_PPM = 20;  // Margin to leave a state, so the alarm does not flicker.
    static constexpr int EVALUATE_EVENT = 1000, REPORT_EVENT = 1001, ALARM_REPEAT_EVENT = 1002;
    static constexpr int SET_NORMAL_MINIMUM = 2000, SET_NORMAL_MAXIMUM = 2001, SET_CRITICAL_MINIMUM = 2002,
                         SET_CRITICAL_MAXIMUM = 2003, APPLY_PROFILE = 2004;
    static constexpr int32_t NO_LIMIT = INT32_MIN;

    PassiveInfraredSensor motionSensor;
    GasSensor airQualitySensor;
    Buzzer alarmBuzzer;
    RgbLed stateLed;
    WiFiConnectivityDriver& wifi;
    HttpGatewayClient& edge;
    char signInUrl[128]{}, readingsUrl[128]{}, configurationUrl[160]{};

    // Sampler task state.
    Profile profile, pending;
    State state = State::Normal;
    bool calibrated = false, faulted = false;
    float cleanAirResistance = 0, lastPpm = 0;

    // Network task state (its hooks are const in the framework).
    mutable bool signedIn = false, configurationRead = false;
    mutable unsigned long lastClockAttemptAt = 0, lastSignInAttemptAt = 0, lastConfigurationAttemptAt = 0;

    static bool due(unsigned long& lastAttemptAt, unsigned long now, unsigned long interval) {
        if (lastAttemptAt != 0 && now - lastAttemptAt < interval) return false;
        lastAttemptAt = now == 0 ? 1 : now;
        return true;
    }

    /** @brief The first DATA_READ of the GasSensor ends the calibration; R0 comes from that same sample. */
    void finishCalibration() {
        if (calibrated || airQualitySensor.getOperatingMode() != GasSensor::OperatingMode::AccurateMode) return;
        const float resistance = airQualitySensor.getSensorResistanceInKiloOhm();
        const float ratio = airQualitySensor.getRsR0Ratio();
        if (resistance <= 0 || ratio <= 0) return;
        cleanAirResistance = resistance / ratio;
        calibrated = true;
        lastPpm = readPpm();
        Serial.printf("[EnvironmentMonitor] MQ-135 calibrated: %.1f ppm\n", lastPpm);
        changeState(profile.classify(lastPpm));
        showState();
        sendAirQuality();
    }

    /**
     * @brief Ppm of the latest sample. The GasSensor raises DATA_READ only when Rs/R0 moves by 0.05, about a fifth of
     * the ppm near the limits, so the device classifies every sample from the fresh resistance and the R0.
     */
    float readPpm() const {
        const float resistance = airQualitySensor.getSensorResistanceInKiloOhm();
        if (resistance <= 0 || cleanAirResistance <= 0) return NAN;
        return powf(10.0f, (log10f(resistance / cleanAirResistance) - CURVE_INTERCEPT) / CURVE_SLOPE);
    }

    void evaluateAirQuality() {
        if (!calibrated || faulted) return;
        const float ppm = readPpm();
        if (std::isnan(ppm)) return;
        lastPpm = ppm;
        // A worse state applies at once; a better one only when the value is HYSTERESIS_IN_PPM inside its range.
        State next = profile.classify(ppm);
        if (next < state) {
            const State withMargin = std::max(profile.classify(ppm - HYSTERESIS_IN_PPM),
                                              profile.classify(ppm + HYSTERESIS_IN_PPM));
            next = std::min(state, std::max(next, withMargin));
        }
        if (changeState(next)) sendAirQuality(); // The Edge learns about the change at once.
    }

    bool changeState(State next) {
        if (next == state) return false;
        static const char* const NAMES[] = {"NORMAL", "WARNING", "CRITICAL"};
        Serial.printf("[EnvironmentMonitor] Air quality %.1f ppm: %s -> %s\n", lastPpm,
                      NAMES[static_cast<int>(state)], NAMES[static_cast<int>(next)]);
        state = next;
        showState();
        if (state == State::Warning) playTone(1000, 200);
        else if (state == State::Critical) playTone(2000, 500);
        else alarmBuzzer.handle(Buzzer::TURN_OFF_COMMAND);
        return true;
    }

    void showState() {
        if (faulted) showColor(RgbLed::PURPLE);
        else if (!calibrated) showColor(RgbLed::BLUE);
        else if (state == State::Critical) showColor(RgbLed::RED);
        else if (state == State::Warning) showColor(RgbLed::YELLOW);
        else showColor(RgbLed::GREEN);
    }

    void showColor(const RgbColor& color) {
        stateLed.setColor(color);
        stateLed.handle(RgbLed::SET_COLOR_COMMAND);
    }

    void playTone(int frequencyInHertz, unsigned long durationInMilliseconds) {
        alarmBuzzer.setTone(frequencyInHertz, durationInMilliseconds);
        alarmBuzzer.handle(Buzzer::PLAY_TONE_COMMAND);
    }

    /** @brief The Edge needs the UTC time of each observation, so readings wait for the NTP clock. */
    void sendAirQuality() {
        if (!calibrated || !TimeSynchronizationDriver::isSynchronized()) return;
        enqueueTelemetryPayload(EnvironmentReading::airQuality(TimeSynchronizationDriver::getCurrentTimestamp(),
                                                               lastPpm, profile.version));
    }

    void sendMotion(bool detected) {
        Serial.printf("[EnvironmentMonitor] Motion %s\n", detected ? "detected" : "stopped");
        if (!TimeSynchronizationDriver::isSynchronized()) return;
        enqueueTelemetryPayload(EnvironmentReading::motion(TimeSynchronizationDriver::getCurrentTimestamp(),
                                                           detected, profile.version));
    }

    /** @runs_on{network} */
    void signIn() const {
        char body[64];
        snprintf(body, sizeof(body), "{\"device_id\":\"%s\"}", DEVICE_ID);
        edge.setServiceEndpointUrl(signInUrl);
        signedIn = edge.sendRequest(HttpGatewayClient::HttpMethod::Post, body);
        const int status = edge.getLastResponseStatusCode();
        if (signedIn) Serial.printf("[EnvironmentMonitor] Edge accepted %s\n", DEVICE_ID);
        else if (status == 401) Serial.println("[EnvironmentMonitor] Edge rejected the node: check DEVICE_ID and DEVICE_API_KEY");
        else Serial.printf("[EnvironmentMonitor] Edge not reachable at %s (status %d)\n", EDGE_BASE_URL, status);
    }

    /**
     * @brief Reads the configuration of the node and hands its AIR_QUALITY limits to the sampler task.
     * @runs_on{network}
     */
    void readConfiguration() const {
        edge.setServiceEndpointUrl(configurationUrl);
        if (!edge.sendRequest(HttpGatewayClient::HttpMethod::Get)) {
            if (edge.getLastResponseStatusCode() == 404) Serial.println("[EnvironmentMonitor] The Edge has no profile for this node yet");
            return;
        }
        JsonDocument configuration;
        if (deserializeJson(configuration, edge.getResponseBody())) return;
        const uint32_t version = configuration["profile_version"] | 0u;
        for (JsonObjectConst threshold : configuration["thresholds"].as<JsonArrayConst>()) {
            if (strcmp(threshold["metric"] | "", "AIR_QUALITY") != 0) continue;
            const float limits[] = {threshold["normal_min"] | NAN, threshold["normal_max"] | NAN,
                                    threshold["critical_min"] | NAN, threshold["critical_max"] | NAN};
            if (version == 0 || !isValidProfile(limits)) return;
            configurationRead = true;
            // The framework's hooks are const; enqueueCommand() is how the network task hands data to the sampler.
            auto* device = const_cast<EnvironmentMonitor*>(this);
            for (int i = 0; i < 4; i++) {
                const int32_t payload = std::isnan(limits[i]) ? NO_LIMIT : static_cast<int32_t>(lroundf(limits[i] * 10));
                device->enqueueCommand(device, Command{SET_NORMAL_MINIMUM + i, payload});
            }
            device->enqueueCommand(device, Command{APPLY_PROFILE, static_cast<int32_t>(version)});
            return;
        }
    }

    /** @brief Same rule as the cloud: complete sides, at least one, and critical limits outside the normal ones. */
    static bool isValidProfile(const float limits[4]) {
        const float normalMin = limits[0], normalMax = limits[1], criticalMin = limits[2], criticalMax = limits[3];
        if (std::isnan(normalMin) != std::isnan(criticalMin) || std::isnan(normalMax) != std::isnan(criticalMax)) return false;
        if (std::isnan(normalMin) && std::isnan(normalMax)) return false;
        if (!std::isnan(normalMin) && criticalMin >= normalMin) return false;
        if (!std::isnan(normalMax) && criticalMax <= normalMax) return false;
        return std::isnan(normalMin) || std::isnan(normalMax) || normalMin < normalMax;
    }
};

static char authorizationHeader[128];
static WiFiConnectivityDriver* transportDriver = nullptr;
static HttpGatewayClient* edgeGateway = nullptr;
static EnvironmentMonitor* environmentMonitor = nullptr;

void setup() {
    Serial.begin(SERIAL_BAUD_RATE);
    Serial.printf("\n[EnvironmentMonitor] %s starting\n", DEVICE_ID);

    transportDriver = new WiFiConnectivityDriver(WIFI_SSID, WIFI_PASSWORD);
    transportDriver->connect(); // The driver keeps reconnecting in the background.

    // The Edge takes the API key of the node as a Bearer token (Edge ADR-0018).
    snprintf(authorizationHeader, sizeof(authorizationHeader), "Bearer %s", DEVICE_API_KEY);
    edgeGateway = new HttpGatewayClient(*transportDriver, EDGE_BASE_URL);
    edgeGateway->setAuthorizationHeader(authorizationHeader);

    environmentMonitor = new EnvironmentMonitor(*transportDriver, *edgeGateway);
}

void loop() {
    // The sampler and network tasks of the framework run the node; the loop only yields.
    vTaskDelay(pdMS_TO_TICKS(MAIN_LOOP_DELAY_IN_MILLISECONDS));
}
