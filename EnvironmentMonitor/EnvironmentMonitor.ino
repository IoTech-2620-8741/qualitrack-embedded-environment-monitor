/**
 * @file EnvironmentMonitor.ino
 * @brief Embedded Application of the QualiTrack Environment Monitor (ESP32 DevKit v1).
 *
 * One Environment Monitor watches each environment of a pharmaceutical facility: it measures the air
 * quality (MQ-135) and detects motion (PIR), shows the state of the environment on an RGB LED, warns
 * with a buzzer, and sends its readings to the QualiTrack Edge Service of the local station, which
 * synchronizes them with the cloud.
 *
 * @par Modest IoT Nano Framework aspects used:
 * - `Device` mediator with the asynchronous engine (sampler and network tasks, empty `loop()`)
 * - `GasSensor` with calibration and a custom curve, `PassiveInfraredSensor` by interrupt
 * - `RgbLed` and `Buzzer` driven by commands
 * - `WiFiConnectivityDriver` and `TimeSynchronizationDriver`
 * - `GatewayClient` extended by `EdgeGatewayClient` for the QualiTrack Edge API
 * - Scheduler slots of the device itself for the periodic report and the critical alarm
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
#include <Arduino.h>
#include "WiFiConnectivityDriver.h"
#include "EdgeGatewayClient.h"
#include "EnvironmentMonitorDevice.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
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

/// Serial monitor speed.
static constexpr unsigned long SERIAL_BAUD_RATE = 115200;
/// Wait of the empty loop; the framework tasks do the work.
static constexpr unsigned long MAIN_LOOP_DELAY_IN_MILLISECONDS = 60000;

static WiFiConnectivityDriver* transportDriver = nullptr;
static EdgeGatewayClient* edgeGateway = nullptr;
static EnvironmentMonitorDevice* environmentMonitor = nullptr;

void setup() {
    Serial.begin(SERIAL_BAUD_RATE);
    Serial.printf("\n[EnvironmentMonitor] %s starting\n", DEVICE_ID);

    transportDriver = new WiFiConnectivityDriver(WIFI_SSID, WIFI_PASSWORD);
    transportDriver->connect(); // The driver keeps reconnecting in the background.

    edgeGateway = new EdgeGatewayClient(*transportDriver, EDGE_BASE_URL, DEVICE_ID, DEVICE_API_KEY,
                                        TIME_ZONE, NTP_SERVER);

    environmentMonitor = new EnvironmentMonitorDevice(MOTION_PIN, AIR_QUALITY_PIN, BUZZER_PIN,
                                                      LED_RED_PIN, LED_GREEN_PIN, LED_BLUE_PIN,
                                                      *edgeGateway);
}

void loop() {
    // The sampler and network tasks of the framework run the node; the loop only yields.
    vTaskDelay(pdMS_TO_TICKS(MAIN_LOOP_DELAY_IN_MILLISECONDS));
}
