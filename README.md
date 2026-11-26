# QualiTrack Embedded Environment Monitor

Embedded Application of the **Environment Monitor** of [QualiTrack](https://github.com/IoTech-2620-8741), the IoT
platform of IoTech for the quality control of pharmaceutical laboratories and warehouses. There is one Environment
Monitor in each monitored environment: an ESP32 that measures the air quality and detects motion, evaluates the
environmental profile of its environment, warns locally with an RGB LED and a buzzer, and sends its readings to the
QualiTrack Edge Service of the facility.

The firmware is an Arduino sketch built on the [Modest IoT Nano Framework](https://github.com/modest-iot/modest-iot-nano-framework)
v1.3.0, and it runs on the ESP32 DevKit v1 and in the [Wokwi](https://wokwi.com) simulator.

## Responsibilities

| Story | Responsibility |
|-------|----------------|
| MS01 | Read the air quality (MQ-135) in ppm, after a 30-second calibration in clean air, and classify it every second |
| MS02 | Detect motion in the environment (PIR) by interrupt |
| MS03 | Show the state of the environment on the RGB LED: blue while calibrating, green NORMAL, yellow WARNING, red CRITICAL, purple sensor fault |
| MS04 | Beep once when the environment enters WARNING and every two seconds while it is CRITICAL |
| MS05 | Send the air quality to the Edge API when the state changes and every minute, and the motion on every change |
| MS14 | Check its identifier and API key with the Edge API (sign-in) when it reaches the Edge, and send the key in every request |
| MS15 | Read its environmental profile (thresholds and version) from the Edge API every five minutes and apply it |
| US66 | Let the operational staff recognize the state of the environment from the device |

The local reaction does not depend on the network: with no Edge or no profile yet, the node classifies the readings
with its default profile (NORMAL up to 800 ppm, WARNING up to 1000 ppm, CRITICAL above).

## Hardware

Circuit of report section 5.6, with an RGB LED instead of the 16-pixel LED ring (the framework drives an RGB LED, and
Wokwi simulates it).

| Component | Component pin | ESP32 connection | Notes |
|-----------|---------------|------------------|-------|
| ESP32 DevKit v1 | — | — | Wi-Fi 2.4 GHz, USB 5 V power (1 A or more) |
| PIR sensor HC-SR501 | VCC / OUT / GND | 3V3 / GPIO 27 / GND | Digital presence output |
| MQ-135 module | VCC / AO / GND | 5V / GPIO 34 / GND | AO goes through a 2.2 kΩ / 3.3 kΩ divider (≈ 3.0 V); DO not connected; needs preheating and calibration in clean air |
| Buzzer | + / − | GPIO 25 / GND | Alert sound |
| RGB LED, common cathode | R / G / B / COM | GPIO 4 / 16 / 17 / GND | One 220 Ω resistor on each color |

## Firmware

The whole node is one sketch, as in the examples of the framework:

| File | Role |
|------|------|
| `EnvironmentMonitor.ino` | The node: pins, the `EnvironmentReading` telemetry package and the `EnvironmentMonitor` device |
| `secrets.example.h` | Settings of the node; copy it as `secrets.h` (ignored by Git) with the real values |
| `diagram.json`, `libraries.txt` | Circuit and libraries of the Wokwi project (the Arduino IDE ignores them) |

The sketch writes only what the framework asks an application to write: `EnvironmentReading` (a `TelemetryPackage`
with the fields of the Edge) and `EnvironmentMonitor` (a `Device` that reacts to the events of its sensors). The rest
comes from the framework: `GasSensor`, `PassiveInfraredSensor`, `RgbLed`, `Buzzer`, `WiFiConnectivityDriver`,
`TimeSynchronizationDriver` and `HttpGatewayClient`. The configuration read from the Edge on the network task reaches
the sampler task through `enqueueCommand()`.

The node classifies a reading with the rule of the cloud: a value above the critical maximum is CRITICAL, above the
normal maximum WARNING, and NORMAL otherwise, with a 20 ppm margin to leave a state. It sends readings only once its
clock is synchronized with NTP, because the Edge needs the time of each observation.

Notes on the framework v1.3.0:

- `HttpGatewayClient::sendTelemetryRecord()` adds the `timestamp` and `sequence` of the framework, which the Edge
  rejects, so the node writes the fields of each reading with a `JsonTelemetryWriter` and posts them with
  `sendRequest()`. The client sends only the `Authorization`
  header, so the API key goes as a Bearer token, which the Edge accepts since ADR-0018.
- The `GasSensor` raises `DATA_READ` only when Rs/R0 changes by more than 0.05 (an absolute step, about a fifth of the
  ppm value near 800 ppm). The device takes R0 from the sample that ends the calibration and classifies the latest
  sample every second with the MQ-135 curve, so it does not miss a change of state.
- On the ESP32 the `MODEST_LOG_*` macros go to the ESP-IDF log, which prints nothing at the default debug level, so
  the node writes its messages with `Serial`, as the examples of the framework do.

## Build with the Arduino IDE

1. Install the ESP32 core (Espressif, 3.x) and, in the Library Manager, `Modest-IoT-Nano-Framework` 1.3.0, which
   also installs MQTT, and ArduinoJson 7, which reads the profile of the Edge (the framework no longer brings it).
2. Copy `EnvironmentMonitor/secrets.example.h` as `EnvironmentMonitor/secrets.h` and set the Wi-Fi network, the URL
   of the Edge, the identifier of the node and its API key.
3. Open `EnvironmentMonitor/EnvironmentMonitor.ino`, choose the board "ESP32 Dev Module" and upload it.

## Build with arduino-cli

The same steps from a terminal:

1. Install the ESP32 core, the framework v1.3.0 from the Arduino Library Manager, which also installs MQTT, and
   ArduinoJson:

   ```bash
   arduino-cli core install esp32:esp32
   arduino-cli lib install "Modest-IoT-Nano-Framework@1.3.0" ArduinoJson
   ```

2. Copy `EnvironmentMonitor/secrets.example.h` as `EnvironmentMonitor/secrets.h` and set the Wi-Fi network, the URL
   of the Edge, the identifier of the node and its API key.

3. Compile (and add `--upload --port <port>` to flash the board):

   ```bash
   arduino-cli compile --fqbn esp32:esp32:esp32 EnvironmentMonitor
   ```

It was verified with arduino-cli 1.x, ESP32 core 3.3, the framework 1.3.0, ArduinoJson 7.4 and MQTT 2.5.

## Simulate in Wokwi

Open the published project: [qualitrack-environment-monitor](https://wokwi.com/projects/480697842499374081). When the
sketch changes, update its files there, or build a new project with the steps below.

`libraries.txt` takes the framework v1.3.0 from the Arduino Library Manager, as arduino-cli does. Keep the version
written: Wokwi may still resolve the bare name to the framework 1.2.1, which does not compile on its ESP32 core 3.3.12
(`BLE.h: No such file or directory`). The `ModestIoT`
library published on Wokwi (`ModestIoT@wokwi:...`) is an older version of the framework, without `ModestLog.h` and
with the MQTT client on PubSubClient, so the sketch does not use it.

1. Create a new ESP32 project in Wokwi.
2. Rename `sketch.ino` to `EnvironmentMonitor.ino`, as in the published project, paste `EnvironmentMonitor.ino`,
   `diagram.json` and `libraries.txt` into the files of the same name, and add `secrets.example.h` (without
   `secrets.h`, the sketch takes it).
3. Start the simulation. The first build compiles the whole framework and takes about two minutes.

What to expect:

- Keep the gas sensor at 400 ppm during the first 30 seconds: the node measures R0 in that "clean air" (blue LED) and
  then turns green.
- Wokwi simulates an MQ-2 module, whose curve differs from the MQ-135 curve of the firmware, so the ppm of the slider
  and the ppm the node reads differ (about 720 ppm on the slider gives WARNING, and about 880 ppm CRITICAL).
- Click the PIR sensor and then **Simulate motion** to send a motion change.
- The simulator reaches the internet through `Wokwi-GUEST` but not a local network, so the requests to the Edge fail
  with status -1 unless the Edge has a public address.
- Keep the browser tab in front: a hidden tab slows the simulation to about 1 % of real time.

## Tests

The host tests compile the real `EnvironmentMonitor.ino`, unchanged, on a computer, with the classes of the Modest IoT
Nano Framework v1.3.0 and its host fakes. The framework tests each of its classes alone, so its fakes do not run
FreeRTOS; `tests/fakes` adds what a whole sketch needs: working queues, tasks and timer, `Serial`, and an NTP clock the
tests control. The tests check what the node does from the outside: the LED, the buzzer, the readings it queues and the
requests it sends to the Edge.

| Suite | Stories | What it checks |
|-------|---------|----------------|
| `tests/AirQualityAlarm` | MS01, MS03, MS04 | Calibration, states, the 20 ppm margin, LED colors and beeps |
| `tests/EdgeReadings` | MS02, MS05 | JSON of the readings, when they are sent, motion, NTP clock, posting and 401 |
| `tests/EdgeConfiguration` | MS14, MS15 | Bearer key, sign-in, valid and invalid profiles, retry intervals |

Run them from the root of the repository; they need g++ with C++17, git and curl:

```bash
tests/run_all.sh
```

The first run downloads the framework v1.3.0 and ArduinoJson 7.4.3 into `build/test-dependencies/`. The tests run with
AddressSanitizer and UndefinedBehaviorSanitizer, which `ENABLE_SANITIZERS=0 tests/run_all.sh` turns off. Like the
sketch, they take `secrets.h` when it exists and `secrets.example.h` otherwise.

## Contract with the Edge API

The node talks to the QualiTrack Edge Service (`http://<local station>:5000`) over the local network with JSON over
HTTP. It sends its `device_id` (the identifier registered in QualiTrack, for example `ENV-001`) in the body or the path
and the API key the Edge issued for it as a Bearer token: `Authorization: Bearer <api key>` (the Edge also accepts it
in `X-API-Key`).

| Endpoint | Use |
|----------|-----|
| `POST /api/v1/auth/sign-in` | Check the identifier and the API key of the node |
| `POST /api/v1/monitoring/environment-readings` | Send the air quality or the motion measured at one moment |
| `GET /api/v1/configurations/{deviceId}` | Read the thresholds and the version of the environmental profile |

Example of a reading:

```json
{
  "device_id": "ENV-001",
  "measured_at": "2026-11-13T15:00:00Z",
  "air_quality": 412.5,
  "profile_version": 3
}
```

The node sends `air_quality` or `motion` in each reading, and `profile_version` once it has a profile of the Edge.
The Edge answers `201` when it stores the readings, `200` for an identical retry, `400` for invalid data, `401` for a
missing or rejected API key and `409` when a retry changes a stored reading. See the
[Edge Service](https://github.com/IoTech-2620-8741/qualitrack-edge-service) for the full contract.

## Technologies

C++ with the Arduino framework for the ESP32, the Modest IoT Nano Framework v1.3.0 and ArduinoJson 7, built with
arduino-cli and simulated in Wokwi.

## Branches and Commits

The repository follows GitFlow: `main` keeps the released versions, `develop` integrates the work, and each change is
made in a `feature/*` branch created from `develop`. Commit messages follow Conventional Commits.

The GitHub Actions workflow `.github/workflows/ci.yml` runs the host tests and compiles the sketch for the ESP32 DevKit v1
on every push and pull request to `main` and `develop`.

## Changelog and License

- [CHANGELOG.md](CHANGELOG.md) lists the changes of every version, following Keep a Changelog.
- QualiTrack is proprietary software of IoTech; see [LICENSE.md](LICENSE.md). The Modest IoT Nano Framework and
  ArduinoJson keep their own licenses (Apache-2.0 and MIT).

## Related Repositories

- [qualitrack-embedded-container-monitor](https://github.com/IoTech-2620-8741/qualitrack-embedded-container-monitor): Embedded Application of the Container Monitor.
- [qualitrack-edge-service](https://github.com/IoTech-2620-8741/qualitrack-edge-service): Edge Service that receives the readings of the nodes.
- [qualitrack-platform](https://github.com/IoTech-2620-8741/qualitrack-platform): Cloud REST API of QualiTrack.
- [modest-iot-nano-framework](https://github.com/modest-iot/modest-iot-nano-framework): framework of the firmware.
