/**
 * @file test.cpp
 * @brief Host tests of the light sensor (BH1750) of the Environment Monitor and of the motion watch it drives.
 */
#include "../support/EnvironmentMonitorHarness.h"

namespace {

/** @brief Takes the readings queued since the last call, asserts there is exactly one, and parses it. */
JsonDocument takeTheOnlyReading(EnvironmentMonitorHarness& harness) {
    const std::vector<std::string> readings = harness.takeSentReadings();
    assert(readings.size() == 1);
    return parseJson(readings.front());
}

/** @brief The reading that ends the calibration carries the air quality and the luminosity of the room. */
void theLuminosityIsSentWithTheAirQuality() {
    EnvironmentMonitorHarness harness;
    harness.setLuminosityInLux(300);
    harness.calibrateInCleanAir();
    const JsonDocument reading = takeTheOnlyReading(harness);
    assert(std::fabs(reading["air_quality"].as<float>() - 408.4f) < 0.05f);
    assert(std::fabs(reading["luminosity"].as<float>() - 300.0f) < 0.05f);
    assert(reading["motion"].isNull());
}

/** @brief The luminosity is the average of the last 10 samples, so a sudden change reaches it over 10 seconds. */
void theLuminosityIsTheAverageOfTheLastTenSamples() {
    EnvironmentMonitorHarness harness;
    harness.setLuminosityInLux(100);
    harness.calibrateInCleanAir();      // Node time 31 s.
    harness.takeSentReadings();

    harness.advanceMilliseconds(24000); // 55 s.
    harness.setLuminosityInLux(200);
    harness.advanceMilliseconds(5100);  // 60.1 s: the report of the minute, about 5 of 10 samples at 200 lux.
    const float halfway = takeTheOnlyReading(harness)["luminosity"].as<float>();
    assert(halfway > 100.0f && halfway < 200.0f);

    harness.advanceMilliseconds(60000); // 120.1 s: every sample at 200 lux.
    assert(std::fabs(takeTheOnlyReading(harness)["luminosity"].as<float>() - 200.0f) < 0.05f);
}

/** @brief Motion readings carry only the motion, not the luminosity. */
void motionReadingsDoNotCarryTheLuminosity() {
    EnvironmentMonitorHarness harness;
    harness.setLuminosityInLux(5); // Dark room: the motion watch is armed.
    harness.calibrateInCleanAir();
    harness.takeSentReadings();
    harness.setMotion(true);
    const JsonDocument reading = takeTheOnlyReading(harness);
    assert(reading["motion"] == true);
    assert(reading["luminosity"].isNull());
    assert(reading["air_quality"].isNull());
}

/**
 * @brief A BH1750 that stops answering is a fault after 3 failed reads: the luminosity is no longer sent, but the air
 * quality and the LED go on. The node sets it up again once a minute; when it answers again it recovers and the
 * luminosity comes back.
 */
void aFaultyLightSensorStopsOnlyTheLuminosity() {
    EnvironmentMonitorHarness harness;
    harness.setLuminosityInLux(300);
    harness.calibrateInCleanAir();      // 31 s.
    harness.takeSentReadings();

    harness.setLightSensorConnected(false);
    harness.advanceMilliseconds(5000);  // 36 s.
    assert(harness.logged("BH1750 fault"));
    assert(harness.ledColor() == LedColor::Green);

    harness.advanceMilliseconds(24100); // 60.1 s: the report of the minute.
    const JsonDocument withoutLight = takeTheOnlyReading(harness);
    assert(std::fabs(withoutLight["air_quality"].as<float>() - 408.4f) < 0.05f);
    assert(withoutLight["luminosity"].isNull());

    harness.setLightSensorConnected(true);
    harness.advanceMilliseconds(120000); // 180.1 s: set up again within a minute, then 10 samples at 300 lux.
    assert(harness.logged("BH1750 recovered"));
    const std::vector<std::string> readings = harness.takeSentReadings();
    assert(readings.size() == 2);
    assert(std::fabs(parseJson(readings.back())["luminosity"].as<float>() - 300.0f) < 0.05f);
}

/** @brief A node started without a BH1750 works without luminosity and finds the sensor once it is connected. */
void aMissingLightSensorIsFoundWhenConnected() {
    EnvironmentMonitorHarness harness(false);
    assert(harness.logged("BH1750 not found on the I2C bus"));
    harness.setLuminosityInLux(250);
    harness.calibrateInCleanAir();      // 31 s.
    const JsonDocument withoutLight = takeTheOnlyReading(harness);
    assert(withoutLight["air_quality"].is<float>());
    assert(withoutLight["luminosity"].isNull());

    harness.setLightSensorConnected(true);
    harness.advanceMilliseconds(29100); // 60.1 s: the report of the minute sets it up again.
    assert(harness.logged("BH1750 found on the I2C bus"));
    assert(takeTheOnlyReading(harness)["luminosity"].isNull()); // The first sample takes 180 ms.

    harness.advanceMilliseconds(60000); // 120.1 s.
    assert(std::fabs(takeTheOnlyReading(harness)["luminosity"].as<float>() - 250.0f) < 0.05f);
}

/** @brief With the lights on (50 lux or more) the PIR is off: a motion is neither sent nor beeped. */
void withTheLightsOnMotionIsIgnored() {
    EnvironmentMonitorHarness harness;
    harness.setLuminosityInLux(300);
    harness.calibrateInCleanAir();
    harness.takeSentReadings();
    assert(harness.logged("Motion watch off: the lights are on"));

    fakeToneCalled = false;
    harness.setMotion(true);
    harness.setMotion(false);
    assert(harness.takeSentReadings().empty());
    assert(!fakeToneCalled);
    assert(harness.logged("Motion detected ignored: the lights are on"));
}

/** @brief In the dark the PIR is armed: a motion is sent to the Edge and beeps once with a long, high tone. */
void inTheDarkMotionIsSentAndBeeps() {
    EnvironmentMonitorHarness harness;
    harness.setLuminosityInLux(5);
    harness.calibrateInCleanAir();
    harness.takeSentReadings();

    harness.setMotion(true);
    assert(takeTheOnlyReading(harness)["motion"] == true);
    assert(fakeToneCall.pin == BUZZER_PIN && fakeToneCall.frequency == 3000 && fakeToneCall.duration == 1000);

    fakeToneCalled = false;
    harness.setMotion(false);
    assert(takeTheOnlyReading(harness)["motion"] == false);
    assert(!fakeToneCalled); // Only a detection beeps.
}

/** @brief Turning the lights off arms the PIR within a second, and turning them on turns it off again. */
void theLightsSwitchTheMotionWatch() {
    EnvironmentMonitorHarness harness;
    harness.setLuminosityInLux(300);
    harness.calibrateInCleanAir();
    harness.takeSentReadings();

    harness.setLuminosityInLux(2);
    harness.advanceMilliseconds(1000);
    assert(harness.logged("Motion watch armed: the room is dark"));
    harness.setMotion(true);
    assert(takeTheOnlyReading(harness)["motion"] == true);
    harness.setMotion(false);
    harness.takeSentReadings();

    harness.setLuminosityInLux(300);
    harness.advanceMilliseconds(1000);
    harness.setMotion(true);
    assert(harness.takeSentReadings().empty());
}

/** @brief A BH1750 that fails cannot see the lights, so the PIR is armed: no intrusion goes unnoticed. */
void aFaultyLightSensorArmsTheMotionWatch() {
    EnvironmentMonitorHarness harness;
    harness.setLuminosityInLux(300);
    harness.calibrateInCleanAir();
    harness.takeSentReadings();

    harness.setLightSensorConnected(false);
    harness.advanceMilliseconds(5000);
    assert(harness.logged("BH1750 fault"));
    harness.setMotion(true);
    assert(takeTheOnlyReading(harness)["motion"] == true);
}

/** @brief A node started without a BH1750 watches the motion as before the light sensor existed. */
void withoutALightSensorMotionIsWatched() {
    EnvironmentMonitorHarness harness(false);
    harness.calibrateInCleanAir();
    harness.takeSentReadings();
    harness.setMotion(true);
    assert(takeTheOnlyReading(harness)["motion"] == true);
}

} // namespace

int main() {
    std::printf("Running Light Sensor tests...\n");
    RUN_TEST(theLuminosityIsSentWithTheAirQuality);
    RUN_TEST(theLuminosityIsTheAverageOfTheLastTenSamples);
    RUN_TEST(motionReadingsDoNotCarryTheLuminosity);
    RUN_TEST(aFaultyLightSensorStopsOnlyTheLuminosity);
    RUN_TEST(aMissingLightSensorIsFoundWhenConnected);
    RUN_TEST(withTheLightsOnMotionIsIgnored);
    RUN_TEST(inTheDarkMotionIsSentAndBeeps);
    RUN_TEST(theLightsSwitchTheMotionWatch);
    RUN_TEST(aFaultyLightSensorArmsTheMotionWatch);
    RUN_TEST(withoutALightSensorMotionIsWatched);
    return 0;
}
