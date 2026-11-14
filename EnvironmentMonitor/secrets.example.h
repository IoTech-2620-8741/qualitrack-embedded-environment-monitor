/**
 * @file secrets.example.h
 * @brief Settings of one Environment Monitor node.
 *
 * Copy this file as `secrets.h` (ignored by Git) and set the values of the node. The sketch uses
 * `secrets.h` when it exists and this file otherwise, so the example values below run in Wokwi.
 *
 * - `DEVICE_ID` is the identifier (sensorExternalId) the quality manager registered for the
 *   environmental device in QualiTrack, and that the Edge registered with
 *   `flask --app app devices register <DEVICE_ID> ENVIRONMENTAL_DEVICE`.
 * - `DEVICE_API_KEY` is the key that command printed once. Never commit a real key.
 */
#ifndef ENVIRONMENT_MONITOR_SECRETS_H
#define ENVIRONMENT_MONITOR_SECRETS_H

/// Wi-Fi network of the local station ("Wokwi-GUEST" in the simulator).
static constexpr const char* WIFI_SSID = "Wokwi-GUEST";
/// Wi-Fi password (empty for "Wokwi-GUEST").
static constexpr const char* WIFI_PASSWORD = "";

/// Base URL of the QualiTrack Edge Service of the facility, without a trailing slash.
static constexpr const char* EDGE_BASE_URL = "http://192.168.1.50:5000";
/// Identifier of the node registered in QualiTrack and in the Edge.
static constexpr const char* DEVICE_ID = "ENV-001";
/// API key the Edge issued for the node.
static constexpr const char* DEVICE_API_KEY = "replace-with-the-api-key-of-the-node";

/// POSIX time zone of the clock; the Edge receives UTC times, so UTC is enough.
static constexpr const char* TIME_ZONE = "UTC0";
/// NTP server of the clock.
static constexpr const char* NTP_SERVER = "pool.ntp.org";

#endif // ENVIRONMENT_MONITOR_SECRETS_H
