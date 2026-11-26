/**
 * @file test.cpp
 * @brief Host tests of the readings the Environment Monitor sends to the Edge (MS02, MS05).
 */
#include "../support/EnvironmentMonitorHarness.h"

namespace {

/** @brief Profile of the Edge, version 3: NORMAL up to 950 ppm, CRITICAL above 1100 ppm. */
constexpr const char* PROFILE_VERSION_3 =
    R"({"profile_version":3,"thresholds":[{"metric":"AIR_QUALITY","normal_max":950,"critical_max":1100}]})";

/** @brief Takes the readings queued since the last call, asserts there is exactly one, and parses it. */
JsonDocument takeTheOnlyReading(EnvironmentMonitorHarness& harness) {
    const std::vector<std::string> readings = harness.takeSentReadings();
    assert(readings.size() == 1);
    JsonDocument reading = parseJson(readings.front());
    assert(reading["device_id"] == DEVICE_ID);
    assert(reading["measured_at"] == "2026-11-13T15:00:00Z");
    return reading;
}

/** @brief MS05: the sample that ends the calibration is sent at once, without a profile version. */
void calibrationSendsTheFirstReading() {
    EnvironmentMonitorHarness harness;
    harness.calibrateInCleanAir();
    const JsonDocument reading = takeTheOnlyReading(harness);
    assert(std::fabs(reading["air_quality"].as<float>() - 408.4f) < 0.05f);
    assert(reading["motion"].isNull());
    assert(reading["profile_version"].isNull());
}

/** @brief MS05: a change of state is sent at once. */
void aChangeOfStateIsSentAtOnce() {
    EnvironmentMonitorHarness harness;
    harness.calibrateInCleanAir();
    harness.takeSentReadings();
    harness.setAirQualityInPpm(900);
    harness.advanceMilliseconds(1000);
    const JsonDocument reading = takeTheOnlyReading(harness);
    assert(std::fabs(reading["air_quality"].as<float>() - 900.0f) < 3.0f);
}

/**
 * @brief MS05: with no change of state, the air quality is sent once a minute, not sooner.
 *
 * The minute is counted by the sampler's scheduler, not from the last reading: it fires the report first on its first
 * tick (100 ms, before the calibration ends, so nothing is sent) and then every 60 s, so the readings fall at 60.1 s
 * and 120.1 s of node time. Two periods are checked so that an interval that divides 60 s also fails.
 */
void theAirQualityIsSentEveryMinute() {
    EnvironmentMonitorHarness harness;
    harness.calibrateInCleanAir(); // Node time 31 s.
    harness.takeSentReadings();

    harness.advanceMilliseconds(29000); // 60.0 s.
    assert(harness.takeSentReadings().empty());
    harness.advanceMilliseconds(100); // 60.1 s.
    assert(harness.takeSentReadings().size() == 1);

    harness.advanceMilliseconds(59900); // 120.0 s.
    assert(harness.takeSentReadings().empty());
    harness.advanceMilliseconds(100); // 120.1 s.
    assert(harness.takeSentReadings().size() == 1);
}

/** @brief MS02, MS05: every motion change is sent, as a reading with `motion` and no `air_quality`. */
void motionChangesAreSent() {
    EnvironmentMonitorHarness harness;
    harness.calibrateInCleanAir();
    harness.takeSentReadings();

    harness.setMotion(true);
    const JsonDocument motionDetected = takeTheOnlyReading(harness);
    assert(motionDetected["motion"] == true);
    assert(motionDetected["air_quality"].isNull());

    harness.setMotion(false);
    const JsonDocument motionStopped = takeTheOnlyReading(harness);
    assert(motionStopped["motion"] == false);
}

/** @brief MS05: before NTP synchronizes the clock no reading is queued, but the LED still reacts. */
void readingsWaitForTheClock() {
    EnvironmentMonitorHarness harness;
    fakeClockSynchronized = false;
    harness.calibrateInCleanAir();
    harness.setAirQualityInPpm(1200);
    harness.advanceMilliseconds(1000);
    harness.setMotion(true);
    assert(harness.ledColor() == LedColor::Red);
    assert(harness.takeSentReadings().empty());
    assert(harness.logged("Motion detected"));
}

/** @brief MS05: the network task posts a reading to the readings route of the Edge. */
void aReadingIsPostedToTheReadingsRoute() {
    EnvironmentMonitorHarness harness;
    harness.calibrateInCleanAir();
    harness.deliverNextReading();
    assert(HTTPClient::lastUrl == std::string(EDGE_BASE_URL) + "/api/v1/monitoring/environment-readings");
    const JsonDocument posted = parseJson(HTTPClient::lastPostPayload);
    assert(posted["device_id"] == DEVICE_ID);
    assert(std::fabs(posted["air_quality"].as<float>() - 408.4f) < 0.05f);
}

/** @brief MS14, MS05: when the Edge rejects a reading with 401, the next network pass signs in again. */
void aRejectedReadingSignsInAgain() {
    EnvironmentMonitorHarness harness;
    harness.calibrateInCleanAir();
    harness.runNetworkServices();

    HTTPClient::setResponse(401);
    harness.deliverNextReading();
    assert(harness.logged("Reading not delivered (status 401)"));

    HTTPClient::setResponse(200);
    const int postsBefore = HTTPClient::postCount;
    harness.advanceMilliseconds(30000);
    harness.runNetworkServices();
    assert(HTTPClient::postCount == postsBefore + 1); // The sign-in is the only POST of a network pass.
}

/** @brief MS15, MS05: once a profile of the Edge is applied, every reading carries its version. */
void readingsCarryTheProfileVersionOnceApplied() {
    EnvironmentMonitorHarness harness;
    harness.calibrateInCleanAir();
    harness.setAirQualityInPpm(900);
    harness.advanceMilliseconds(1000);
    harness.takeSentReadings();

    HTTPClient::setResponse(200, PROFILE_VERSION_3);
    harness.runNetworkServices(); // 900 ppm is NORMAL in version 3, so the change of state is sent.
    const JsonDocument airQuality = takeTheOnlyReading(harness);
    assert(airQuality["profile_version"] == 3);

    harness.setMotion(true);
    const JsonDocument motion = takeTheOnlyReading(harness);
    assert(motion["motion"] == true);
    assert(motion["profile_version"] == 3);
}

/**@brief An MQ-135 fault prevents stale air quality readings without interrupting motion telemetry.*/
void sensorFaultPreventsStaleAirQualityReadings() {
    EnvironmentMonitorHarness harness;
    harness.calibrateInCleanAir();

    // Discard the reading generated after calibration.
    harness.takeSentReadings();

    // Generate a valid critical reading.
    harness.setAirQualityInPpm(1200);
    harness.advanceMilliseconds(1000);

    const JsonDocument criticalReading = takeTheOnlyReading(harness);
    assert(criticalReading["air_quality"].as<float>() > 1000.0f);

    // Simulate an MQ-135 fault.
    harness.monitor().on(
        Event{Sensor::SENSOR_FAULT_EVENT_IDENTIFIER, AIR_QUALITY_PIN}
    );

    // Advance beyond the next periodic report at 60.1 seconds.
    harness.advanceMilliseconds(30000);

    // A faulted sensor must not report its previous ppm value; the BH1750 keeps sending the luminosity.
    const JsonDocument faultedReport = takeTheOnlyReading(harness);
    assert(faultedReport["air_quality"].isNull());
    assert(!faultedReport["luminosity"].isNull());

    // Motion telemetry must continue working independently.
    harness.setMotion(true);

    const JsonDocument motionReading = takeTheOnlyReading(harness);
    assert(motionReading["motion"] == true);
    assert(motionReading["air_quality"].isNull());

    // Simulate sensor recovery.
    harness.monitor().on(
        Event{Sensor::SENSOR_RECOVERED_EVENT_IDENTIFIER, AIR_QUALITY_PIN}
    );

    // A fresh reading should be reported after recovery.
    harness.setAirQualityInPpm(500);
    harness.advanceMilliseconds(1000);

    const JsonDocument recoveredReading = takeTheOnlyReading(harness);
    assert(
        std::fabs(recoveredReading["air_quality"].as<float>() - 500.0f)
        < 3.0f
    );
}

} // namespace

int main() {
    std::printf("Running Edge Readings tests...\n");
    RUN_TEST(calibrationSendsTheFirstReading);
    RUN_TEST(aChangeOfStateIsSentAtOnce);
    RUN_TEST(theAirQualityIsSentEveryMinute);
    RUN_TEST(motionChangesAreSent);
    RUN_TEST(readingsWaitForTheClock);
    RUN_TEST(aReadingIsPostedToTheReadingsRoute);
    RUN_TEST(aRejectedReadingSignsInAgain);
    RUN_TEST(readingsCarryTheProfileVersionOnceApplied);
    RUN_TEST(sensorFaultPreventsStaleAirQualityReadings);
    return 0;
}
