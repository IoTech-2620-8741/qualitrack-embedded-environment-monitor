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

} // namespace

int main() {
    std::printf("Running Air Quality Alarm tests...\n");
    RUN_TEST(calibrationTurnsTheLedFromBlueToGreen);
    RUN_TEST(airQualityIsNotClassifiedDuringCalibration);
    RUN_TEST(warningTurnsTheLedYellowWithASingleBeep);
    RUN_TEST(criticalTurnsTheLedRedAndBeepsEveryTwoSeconds);
    RUN_TEST(aBetterStateNeedsTwentyPpmOfMargin);
    RUN_TEST(returningToNormalSilencesTheBuzzer);
    return 0;
}
