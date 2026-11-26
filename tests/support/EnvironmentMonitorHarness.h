/**
 * @file EnvironmentMonitorHarness.h
 * @brief Builds one Environment Monitor per test on the host fakes and drives its time, sensors and network task.
 *
 * It includes the sketch itself, so every test suite compiles the real `EnvironmentMonitor.ino`, unchanged.
 */
#ifndef ENVIRONMENT_MONITOR_HARNESS_H
#define ENVIRONMENT_MONITOR_HARNESS_H

#include "EnvironmentMonitor.ino"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

extern bool fakeClockSynchronized; ///< Defined in TimeSynchronizationDriverFake.cpp.
extern uint32_t fakeUnixTime;      ///< Defined in TimeSynchronizationDriverFake.cpp.

/** @brief Runs one test function and prints its `[PASS]` line; a failed `assert` aborts before the line. */
#define RUN_TEST(testFunction) (testFunction(), std::printf("  [PASS] %s\n", #testFunction))

/** @brief Environment Monitor whose network task hooks the tests can call. */
class TestableEnvironmentMonitor : public EnvironmentMonitor {
public:
    using EnvironmentMonitor::EnvironmentMonitor;
    using EnvironmentMonitor::processQueuedTelemetryData;
    using EnvironmentMonitor::maintainNetworkServices;
};

/** @brief Color shown by the RGB LED of the node. */
enum class LedColor { Blue, Green, Yellow, Red, Purple, Other };

/** @brief UTC time of every reading in the tests: 2026-11-13T15:00:00Z. */
constexpr uint32_t OBSERVATION_TIME = 1794582000u;

/**
 * @brief Puts every fake back in a known state: time 0, pins low, the BH1750 answering 0 lux, Wi-Fi connected, the Edge
 * answering 200, the clock synchronized at OBSERVATION_TIME and an empty serial log.
 */
inline void resetFakes() {
    fakeMillis = 0;
    std::fill(std::begin(fakePwm), std::end(fakePwm), 0);
    std::fill(std::begin(fakeAnalogRead), std::end(fakeAnalogRead), 0);
    fakeDigitalReadValue = LOW;
    fakeToneCall = {};
    fakeToneCalled = false;
    fakeNoToneCalled = false;
    Wire = FakeWire(); // The I2C bus of the BH1750, from the framework's fakes.
    HTTPClient::resetMockState();
    WiFi.setStatus(WL_CONNECTED);
    Serial.output.clear();
    fakeClockSynchronized = true;
    fakeUnixTime = OBSERVATION_TIME;
}

/** @brief Parses @p json, which must be valid. @return The parsed document. */
inline JsonDocument parseJson(const std::string& json) {
    JsonDocument document;
    [[maybe_unused]] const DeserializationError error = deserializeJson(document, json);
    assert(!error);
    return document;
}

/** @brief One Environment Monitor on the host fakes, in clean air, before its calibration. */
class EnvironmentMonitorHarness {
public:
    static constexpr int CLEAN_AIR_ANALOG_VALUE = 2048; ///< MQ-135 sample in clean air (mid scale of the 12-bit ADC).

    /**
     * @brief Resets the fakes and builds the node as setup() does, with the settings of the sketch.
     * @param lightSensorConnected False to start the node with no BH1750 on the I2C bus.
     */
    explicit EnvironmentMonitorHarness(bool lightSensorConnected = true) {
        resetFakes();
        fakeAnalogRead[AIR_QUALITY_PIN] = CLEAN_AIR_ANALOG_VALUE;
        setLightSensorConnected(lightSensorConnected);
        wifiDriver = std::make_unique<WiFiConnectivityDriver>(WIFI_SSID, WIFI_PASSWORD);
        edgeClient = std::make_unique<HttpGatewayClient>(*wifiDriver, EDGE_BASE_URL);
        monitorUnderTest = std::make_unique<TestableEnvironmentMonitor>(*wifiDriver, *edgeClient);
    }

    /**
     * @brief Lets @p milliseconds pass, one sampler tick (100 ms) at a time.
     * @runs_on{sampler}
     */
    void advanceMilliseconds(unsigned long milliseconds) {
        assert(fakeTimerInterrupt != nullptr);
        for (unsigned long elapsed = 0; elapsed < milliseconds; elapsed += SAMPLER_TICK_IN_MILLISECONDS) {
            if (!lightSensorPresent) Wire.clearCommands(); // Keeps every set-up of a missing BH1750 failing.
            fakeMillis += SAMPLER_TICK_IN_MILLISECONDS;
            fakeTimerInterrupt(fakeTimerInterruptArgument);
            runFakeTask(SAMPLER_TASK_NAME);
        }
    }

    /** @brief Keeps the MQ-135 in clean air for the 30-second calibration and the sample that ends it. */
    void calibrateInCleanAir() { advanceMilliseconds(31000); }

    /** @brief Sets the MQ-135 sample that the sketch's curve reads as @p airQualityInPpm (about 2 ppm per count near 800). */
    void setAirQualityInPpm(float airQualityInPpm) {
        // Inverse of the sketch's curve, PPM = 10^((log10(Rs/R0) - 0.94) / -0.36), and of the GasSensor divider,
        // Rs proportional to 4095 / sample - 1, relative to the clean air sample that gave R0.
        const float resistanceRatio = powf(10.0f, -0.36f * log10f(airQualityInPpm) + 0.94f);
        const float cleanAirRelativeResistance = 4095.0f / CLEAN_AIR_ANALOG_VALUE - 1.0f;
        fakeAnalogRead[AIR_QUALITY_PIN] =
            static_cast<int>(lroundf(4095.0f / (1.0f + resistanceRatio * cleanAirRelativeResistance)));
    }

    /** @brief Sets the raw value of the BH1750 that the framework reads as @p luminosityInLux (raw = lux × 1.2). */
    void setLuminosityInLux(float luminosityInLux) {
        Wire.raw = static_cast<uint16_t>(lroundf(luminosityInLux * 1.2f));
    }

    /** @brief Connects or disconnects the BH1750: a disconnected sensor acknowledges no command and answers no byte. */
    void setLightSensorConnected(bool connected) {
        lightSensorPresent = connected;
        Wire.clearCommands();
        Wire.failureAt = connected ? -1 : 1; // The fake fails the command that makes the count 1.
        Wire.byteCount = connected ? 2 : 0;
    }

    /**
     * @brief Sets the PIR output and raises its interrupt.
     * @runs_on{sampler}
     */
    void setMotion(bool detected) {
        fakeDigitalReadValue = detected ? HIGH : LOW;
        assert(fakeInterruptHandlers[MOTION_PIN] != nullptr);
        fakeInterruptHandlers[MOTION_PIN]();
        runFakeTask(SAMPLER_TASK_NAME);
    }

    /** @return The color the RGB LED shows. */
    LedColor ledColor() const {
        const int red = fakePwm[LED_RED_PIN], green = fakePwm[LED_GREEN_PIN], blue = fakePwm[LED_BLUE_PIN];
        if (red == 0 && green == 0 && blue == 255) return LedColor::Blue;
        if (red == 0 && green == 255 && blue == 0) return LedColor::Green;
        if (red == 255 && green == 255 && blue == 0) return LedColor::Yellow;
        if (red == 255 && green == 0 && blue == 0) return LedColor::Red;
        if (red == 128 && green == 0 && blue == 128) return LedColor::Purple;
        return LedColor::Other;
    }

    /** @return The JSON of every reading queued since the last call, oldest first. */
    std::vector<std::string> takeSentReadings() {
        std::vector<std::string> readings;
        TelemetryPackage* package = nullptr;
        while (xQueueReceive(telemetryQueue(), &package, 0) == pdPASS) {
            JsonTelemetryWriter writer;
            package->write(writer);
            writer.finalize();
            readings.push_back(writer.getString());
            delete package;
        }
        return readings;
    }

    /**
     * @brief Hands the oldest queued reading to the network task, which posts it to the Edge.
     * @runs_on{network}
     */
    void deliverNextReading() {
        TelemetryPackage* package = nullptr;
        [[maybe_unused]] const BaseType_t received = xQueueReceive(telemetryQueue(), &package, 0);
        assert(received == pdPASS);
        monitorUnderTest->processQueuedTelemetryData(package);
        delete package;
    }

    /**
     * @brief Runs one pass of the network services (clock, sign-in, profile), then lets the sampler apply the commands.
     * @runs_on{network}
     */
    void runNetworkServices() {
        monitorUnderTest->maintainNetworkServices();
        runFakeTask(SAMPLER_TASK_NAME);
    }

    /** @return Commands the network task queued and the sampler has not applied yet. */
    size_t pendingCommandCount() const { return findQueue(sizeof(Device::CommandEntry))->items.size(); }

    /** @return The node under test. */
    TestableEnvironmentMonitor& monitor() { return *monitorUnderTest; }

    /** @return Whether the node printed @p text since the harness was built. */
    bool logged(const std::string& text) const { return Serial.output.find(text) != std::string::npos; }

private:
    static constexpr unsigned long SAMPLER_TICK_IN_MILLISECONDS = 100;  ///< Tick of the sketch's sampler.
    static constexpr const char* SAMPLER_TASK_NAME = "HardwareWorker"; ///< Name `Device` gives its sampler task.
    static_assert(sizeof(TelemetryPackage*) != sizeof(Device::CommandEntry), "queues are told apart by item size");

    /** @return The queue of `Device` whose items are @p itemSize bytes. */
    FakeQueue* findQueue(std::uint32_t itemSize) const {
        for (FakeQueue* queue : fakeQueues) {
            if (queue->itemSize == itemSize) return queue;
        }
        assert(false && "Device created no queue with that item size");
        return nullptr;
    }

    /** @return The telemetry queue between the sampler and the network task. */
    FakeQueue* telemetryQueue() const { return findQueue(sizeof(TelemetryPackage*)); }

    bool lightSensorPresent = true;                               ///< Whether the BH1750 is on the I2C bus.
    std::unique_ptr<WiFiConnectivityDriver> wifiDriver;           ///< Wi-Fi of the node (fake).
    std::unique_ptr<HttpGatewayClient> edgeClient;                ///< HTTP client of the Edge (fake transport).
    std::unique_ptr<TestableEnvironmentMonitor> monitorUnderTest; ///< Built last, destroyed first.
};

#endif // ENVIRONMENT_MONITOR_HARNESS_H
