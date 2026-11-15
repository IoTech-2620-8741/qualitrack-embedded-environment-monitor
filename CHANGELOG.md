# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Changed

- The node is one sketch, `EnvironmentMonitor.ino`, as in the examples of the Modest IoT Nano Framework: it writes only
  the `EnvironmentReading` telemetry package and the `EnvironmentMonitor` device, and takes the rest from the
  framework. It opens in the Arduino IDE with the framework installed from the Library Manager (#3).

- The node reaches the Edge with the framework's `HttpGatewayClient` and sends its API key as
  `Authorization: Bearer <key>`, which the Edge accepts since its ADR-0018. The configuration of the Edge reaches the
  sampler task through `enqueueCommand()` (#3).

### Removed

- The node's own profile, translator, telemetry packages, gateway client, device and log files (13 files), replaced by
  the sketch and the classes of the framework; the behavior of the node does not change. The node no longer calls
  `GET /api/v1/health`: the sign-in tells whether the Edge answers (#3).

## [0.2.0] - 2026-11-14

### Changed

- The framework v1.0.0 comes from the Arduino Library Manager (`Modest-IoT-Nano-Framework`), in arduino-cli and in
  Wokwi, so the Wokwi project no longer carries 28 source files of the framework and the build needs no clone of it.
  The sketch includes `<ModestIoT.h>`, as the examples of the framework do (#2).

## [0.1.0] - 2026-11-14

First version of the Embedded Application of the Environment Monitor: the stories MS01–MS05, MS14, MS15 and US66 of
the QualiTrack Report on the Modest IoT Nano Framework v1.0.0.

### Added

- Arduino sketch for the ESP32 DevKit v1 with the `EnvironmentMonitorDevice` mediator, which owns the MQ-135
  `GasSensor`, the PIR `PassiveInfraredSensor`, the `RgbLed` and the `Buzzer` and reacts to their events on the
  sampler task of the framework (MS01–MS04, US66) (#1).

- Air quality states on the RGB LED (blue while calibrating, green NORMAL, yellow WARNING, red CRITICAL, purple sensor
  fault), one beep on WARNING and a beep every two seconds while CRITICAL (MS03, MS04) (#1).

- Air quality profile with the classification rule of the cloud and a 20 ppm hysteresis to leave a state, and a
  default profile (NORMAL up to 800 ppm, WARNING up to 1000 ppm) while the Edge has none (MS15) (#1).

- `EdgeGatewayClient`, a `GatewayClient` for the QualiTrack Edge API: it sends the readings to
  `POST /api/v1/monitoring/environment-readings` with the `X-API-Key` header and the observation time in UTC from NTP,
  checks the node with `POST /api/v1/auth/sign-in`, and reads the profile from `GET /api/v1/configurations/{deviceId}`
  every five minutes (MS05, MS14, MS15) (#1).

- The air quality is sent when its state changes and every minute, and the motion on every change (MS05) (#1).

- The device classifies every air quality sample with the R0 of the calibration, because the `GasSensor` of the
  framework raises `DATA_READ` only when Rs/R0 changes by more than 0.05 (#1).

- Messages of the node on the serial monitor (`NodeLog.h`), since the framework log prints nothing on the ESP32 at
  the default debug level (#1).

- Wokwi project files (`diagram.json`, `libraries.txt`) with the circuit of report section 5.6 and an RGB LED instead
  of the LED ring, the published Wokwi project, and `secrets.example.h` with the settings of a node (#1).

- README with the build with arduino-cli and the Wokwi simulation, LICENSE.md with the QualiTrack Proprietary
  Software License, and this changelog (#1).
