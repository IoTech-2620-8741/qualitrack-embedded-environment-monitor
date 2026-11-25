/**
 * @file test.cpp
 * @brief Host tests of the air quality alarm of the Environment Monitor (MS01, MS03, MS04).
 */
#include "../support/EnvironmentMonitorHarness.h"

namespace {

/** @brief MS01, MS03: the LED is blue during the 30-second calibration and green once the MQ-135 is calibrated. */
void calibrationTurnsTheLedFromBlueToGreen() {
    EnvironmentMonitorHarness harness;
    assert(harness.ledColor() == LedColor::Blue);
    harness.advanceMilliseconds(29000);
    assert(harness.ledColor() == LedColor::Blue);
    harness.advanceMilliseconds(2000);
    assert(harness.ledColor() == LedColor::Green);
    assert(harness.logged("MQ-135 calibrated: 408.4 ppm"));
}

/** @brief MS01: until the MQ-135 is calibrated, a bad sample changes neither the LED nor the buzzer. */
void airQualityIsNotClassifiedDuringCalibration() {
    EnvironmentMonitorHarness harness;
    harness.setAirQualityInPpm(1200);
    harness.advanceMilliseconds(5000);
    assert(harness.ledColor() == LedColor::Blue);
    assert(!fakeToneCalled);
}

/** @brief MS03, MS04: above the normal maximum (800 ppm) the LED turns yellow and the buzzer beeps once. */
void warningTurnsTheLedYellowWithASingleBeep() {
    EnvironmentMonitorHarness harness;
    harness.calibrateInCleanAir();
    harness.setAirQualityInPpm(900);
    harness.advanceMilliseconds(1000);
    assert(harness.ledColor() == LedColor::Yellow);
    assert(fakeToneCall.pin == BUZZER_PIN && fakeToneCall.frequency == 1000 && fakeToneCall.duration == 200);

    fakeToneCalled = false;
    harness.advanceMilliseconds(4000);
    assert(!fakeToneCalled);
}

/** @brief MS03, MS04: above the critical maximum (1000 ppm) the LED turns red and the buzzer beeps every two seconds. */
void criticalTurnsTheLedRedAndBeepsEveryTwoSeconds() {
    EnvironmentMonitorHarness harness;
    harness.calibrateInCleanAir();
    harness.setAirQualityInPpm(1200);
    harness.advanceMilliseconds(1000);
    assert(harness.ledColor() == LedColor::Red);
    assert(fakeToneCall.frequency == 2000 && fakeToneCall.duration == 500);

    fakeToneCalled = false;
    harness.advanceMilliseconds(2000);
    assert(fakeToneCalled && fakeToneCall.frequency == 2000);
}

/** @brief MS01: a worse state applies at once; a better one only once the value is 20 ppm inside its range. */
void aBetterStateNeedsTwentyPpmOfMargin() {
    EnvironmentMonitorHarness harness;
    harness.calibrateInCleanAir();
    const auto ledColorAfterOneSecondAt = [&harness](float airQualityInPpm) {
        harness.setAirQualityInPpm(airQualityInPpm);
        harness.advanceMilliseconds(1000);
        return harness.ledColor();
    };
    assert(ledColorAfterOneSecondAt(1200) == LedColor::Red);
    assert(ledColorAfterOneSecondAt(990) == LedColor::Red);
    assert(ledColorAfterOneSecondAt(970) == LedColor::Yellow);
    assert(ledColorAfterOneSecondAt(790) == LedColor::Yellow);
    assert(ledColorAfterOneSecondAt(770) == LedColor::Green);
}

/** @brief MS04: back in NORMAL the buzzer is turned off. */
void returningToNormalSilencesTheBuzzer() {
    EnvironmentMonitorHarness harness;
    harness.calibrateInCleanAir();
    harness.setAirQualityInPpm(1200);
    harness.advanceMilliseconds(1000);

    fakeNoToneCalled = false;
    harness.setAirQualityInPpm(500);
    harness.advanceMilliseconds(1000);
    assert(harness.ledColor() == LedColor::Green);
    assert(fakeNoToneCalled);
}


/** @brief A fault in the MQ-135 turns the RGB LED purple. */
void sensorFaultTurnsTheLedPurple() {
    EnvironmentMonitorHarness harness;
    harness.calibrateInCleanAir();

    assert(harness.ledColor() == LedColor::Green);

    harness.monitor().on(
        Event{Sensor::SENSOR_FAULT_EVENT_IDENTIFIER, AIR_QUALITY_PIN}
    );

    assert(harness.ledColor() == LedColor::Purple);
    assert(harness.logged("MQ-135 fault"));
}


/** @brief Air quality evaluation resumes after sensor recovery. */
void sensorRecoveryResumesAirQualityEvaluation() {
    EnvironmentMonitorHarness harness;
    harness.calibrateInCleanAir();

    harness.monitor().on(
        Event{Sensor::SENSOR_FAULT_EVENT_IDENTIFIER, AIR_QUALITY_PIN}
    );

    harness.setAirQualityInPpm(1200);
    harness.advanceMilliseconds(1000);

    assert(harness.ledColor() == LedColor::Purple);

    harness.monitor().on(
        Event{Sensor::SENSOR_RECOVERED_EVENT_IDENTIFIER, AIR_QUALITY_PIN}
    );

    assert(harness.ledColor() == LedColor::Green);
    assert(harness.logged("MQ-135 recovered"));

    harness.advanceMilliseconds(1000);

    assert(harness.ledColor() == LedColor::Red);
}

/** @brief An MQ-135 fault stops the critical alarm. */
void sensorFaultStopsTheCriticalAlarm() {
    EnvironmentMonitorHarness harness;
    harness.calibrateInCleanAir();

    harness.setAirQualityInPpm(1200);
    harness.advanceMilliseconds(1000);

    assert(harness.ledColor() == LedColor::Red);
    assert(fakeToneCalled);

    // Simulate an MQ-135 fault.
    fakeNoToneCalled = false;

    harness.monitor().on(
        Event{Sensor::SENSOR_FAULT_EVENT_IDENTIFIER, AIR_QUALITY_PIN}
    );

    assert(harness.ledColor() == LedColor::Purple);
    assert(fakeNoToneCalled);

    // No additional critical alarms while the sensor is faulted.
    fakeToneCalled = false;
    harness.advanceMilliseconds(4000);

    assert(!fakeToneCalled);
}


/** @brief Critical alarms resume after MQ-135 recovery. */
void sensorRecoveryResumesTheCriticalAlarm() {
    EnvironmentMonitorHarness harness;
    harness.calibrateInCleanAir();

    harness.setAirQualityInPpm(1200);
    harness.advanceMilliseconds(1000);

    assert(harness.ledColor() == LedColor::Red);

    harness.monitor().on(
        Event{Sensor::SENSOR_FAULT_EVENT_IDENTIFIER, AIR_QUALITY_PIN}
    );

    assert(harness.ledColor() == LedColor::Purple);

    harness.monitor().on(
        Event{Sensor::SENSOR_RECOVERED_EVENT_IDENTIFIER, AIR_QUALITY_PIN}
    );

    fakeToneCalled = false;
    harness.advanceMilliseconds(2000);

    assert(harness.ledColor() == LedColor::Red);
    assert(fakeToneCalled);
    assert(fakeToneCall.frequency == 2000);
}

/** @brief MS04, MS15: a profile update must not activate an alarm during a sensor fault. */
void profileUpdateDoesNotTriggerAlarmDuringSensorFault() {
    EnvironmentMonitorHarness harness;
    harness.calibrateInCleanAir();

    harness.monitor().on(
        Event{Sensor::SENSOR_FAULT_EVENT_IDENTIFIER, AIR_QUALITY_PIN}
    );

    fakeToneCalled = false;

    HTTPClient::setResponse(
        200,
        R"({"profile_version":4,"thresholds":[{"metric":"AIR_QUALITY","normal_max":300,"critical_max":350}]})"
    );

    harness.runNetworkServices();

    assert(harness.logged("Air quality profile version 4 applied"));
    assert(harness.ledColor() == LedColor::Purple);
    assert(!fakeToneCalled);
}

} // namespace

int main() {
    std::printf("Running Air Quality Alarm tests...\n");
    RUN_TEST(calibrationTurnsTheLedFromBlueToGreen);
    RUN_TEST(airQualityIsNotClassifiedDuringCalibration);
    RUN_TEST(warningTurnsTheLedYellowWithASingleBeep);
    RUN_TEST(criticalTurnsTheLedRedAndBeepsEveryTwoSeconds);
    RUN_TEST(aBetterStateNeedsTwentyPpmOfMargin);
    RUN_TEST(returningToNormalSilencesTheBuzzer);
    RUN_TEST(sensorFaultTurnsTheLedPurple);
    RUN_TEST(sensorRecoveryResumesAirQualityEvaluation);
    RUN_TEST(sensorFaultStopsTheCriticalAlarm);
    RUN_TEST(sensorRecoveryResumesTheCriticalAlarm);
    RUN_TEST(profileUpdateDoesNotTriggerAlarmDuringSensorFault);
    return 0;
}
