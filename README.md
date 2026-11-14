# QualiTrack Embedded Environment Monitor

Embedded Application of the **Environment Monitor** of [QualiTrack](https://github.com/IoTech-2620-8741), the IoT
platform of IoTech for the quality control of pharmaceutical laboratories and warehouses. There is one Environment
Monitor in each monitored environment: an ESP32 that measures the air quality and detects motion, evaluates the
environmental profile of its environment, warns locally with an LED ring and a buzzer, and sends its readings to the
QualiTrack Edge Service of the facility.

## Responsibilities

| Story | Responsibility |
|-------|----------------|
| MS01 | Read the air quality (MQ-135) in ppm, with a moving average to reduce noise |
| MS02 | Detect motion in the environment (PIR) |
| MS03 | Show the state of the environment (NORMAL, WARNING, CRITICAL) on the LED ring |
| MS04 | Sound the buzzer when the environment reaches a warning or critical state |
| MS05 | Send the telemetry of the environment to the Edge API |
| MS14 | Authenticate with the Edge API with its own identifier and API key |
| MS15 | Read its environmental profile (thresholds and version) from the Edge API |
| US66 | Let the operational staff recognize the state of the environment from the device |

## Hardware

Circuit of report section 5.6 (Cirkit Designer).

| Component | Component pin | ESP32 connection | Notes |
|-----------|---------------|------------------|-------|
| ESP32 DevKit v1 | — | — | Wi-Fi 2.4 GHz, USB 5 V power (1 A or more) |
| PIR sensor HC-SR501 | VCC / D / GND | 3V3 / GPIO 27 / GND | Digital presence output |
| MQ-135 module | VCC / AO / GND | 5V / GPIO 34 / GND | AO goes through a 2.2 kΩ / 3.3 kΩ divider (≈ 3.0 V); DO not connected; needs preheating and calibration |
| Buzzer | + / − | GPIO 25 / GND | Alert sound |
| 16-pixel LED ring | 5V / DIN / GND | 5V / GPIO 4 (470 Ω) / GND | The resistor protects the data line |

## Contract with the Edge API

The node talks to the QualiTrack Edge Service (`http://<local station>:5000`) over the local network with JSON over
HTTP. It sends its `device_id` (the identifier registered in QualiTrack, for example `ENV-001`) in the body or the path
and the API key the Edge issued for it in the `X-API-Key` header.

| Endpoint | Use |
|----------|-----|
| `POST /api/v1/auth/sign-in` | Check the identifier and the API key of the node |
| `POST /api/v1/monitoring/environment-readings` | Send the air quality and motion measured at one moment |
| `GET /api/v1/configurations/{deviceId}` | Read the thresholds and the version of the environmental profile |

Example of a reading:

```json
{
  "device_id": "ENV-001",
  "measured_at": "2026-11-13T15:00:00Z",
  "air_quality": 412.5,
  "motion": false,
  "profile_version": 3
}
```

The Edge answers `201` when it stores the readings, `200` for an identical retry, `400` for invalid data, `401` for a
missing or rejected API key and `409` when a retry changes a stored reading. See the
[Edge Service](https://github.com/IoTech-2620-8741/qualitrack-edge-service) for the full contract.

## Technologies

C++ with the Arduino Framework for the ESP32, built with PlatformIO.

## Branches and Commits

The repository follows GitFlow: `main` keeps the released versions, `develop` integrates the work, and each change is
made in a `feature/*` branch created from `develop`. Commit messages follow Conventional Commits.

## Related Repositories

- [qualitrack-embedded-container-monitor](https://github.com/IoTech-2620-8741/qualitrack-embedded-container-monitor): Embedded Application of the Container Monitor.
- [qualitrack-edge-service](https://github.com/IoTech-2620-8741/qualitrack-edge-service): Edge Service that receives the readings of the nodes.
- [qualitrack-platform](https://github.com/IoTech-2620-8741/qualitrack-platform): Cloud REST API of QualiTrack.
