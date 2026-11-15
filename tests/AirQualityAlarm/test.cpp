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

} // namespace

int main() {
    std::printf("Running Air Quality Alarm tests...\n");
    RUN_TEST(calibrationTurnsTheLedFromBlueToGreen);
    return 0;
}
