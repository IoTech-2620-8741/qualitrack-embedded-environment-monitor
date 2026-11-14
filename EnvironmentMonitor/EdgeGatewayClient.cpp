/**
 * @file EdgeGatewayClient.cpp
 * @brief Implements the requests of a node to the QualiTrack Edge API.
 */
#include "EdgeGatewayClient.h"
#include <HTTPClient.h>
#include <WiFi.h>
#include "EdgeTelemetryPackage.h"
#include "NodeLog.h"

namespace {
    constexpr const char* LOG_TAG = "EdgeGateway";
    constexpr int HTTP_OK = 200;
    constexpr int HTTP_CREATED = 201;
    constexpr int HTTP_BAD_REQUEST = 400;
    constexpr int HTTP_UNAUTHORIZED = 401;
    constexpr int HTTP_NOT_FOUND = 404;
    constexpr int HTTP_CONFLICT = 409;
}

EdgeGatewayClient::EdgeGatewayClient(WiFiConnectivityDriver& transportDriver, const char* edgeBaseUrl,
                                     const char* deviceId, const char* apiKey,
                                     const char* timeZone, const char* ntpServer)
    : GatewayClient(transportDriver, edgeBaseUrl),
      wifiDriver(transportDriver),
      deviceId(deviceId),
      apiKey(apiKey),
      timeZone(timeZone),
      ntpServer(ntpServer),
      configurationListener(nullptr),
      configurationRefreshIntervalInMilliseconds(DEFAULT_CONFIGURATION_REFRESH_IN_MILLISECONDS),
      sendsSucceededCount(0),
      sendsFailedCount(0),
      signedIn(false),
      configurationRead(false),
      lastClockAttemptAt(0),
      lastSignInAttemptAt(0),
      lastConfigurationAttemptAt(0),
      serializationBuffer{} {}

bool EdgeGatewayClient::sendTelemetryRecord(const TelemetryPackage& abstractTelemetryPayload) {
    if (!wifiDriver.canTransmit()) {
        wifiDriver.connect();
        sendsFailedCount++;
        return false;
    }
    // Every package this node enqueues is an EdgeTelemetryPackage (see EnvironmentMonitorDevice).
    const auto& package = static_cast<const EdgeTelemetryPackage&>(abstractTelemetryPayload);

    JsonDocument request;
    request["device_id"] = deviceId;
    package.serialize(request); // The fields of the Edge only: no framework envelope.
    if (serializeJson(request, serializationBuffer, sizeof(serializationBuffer)) == 0) {
        NODE_LOG_W(LOG_TAG, "Request too large for %u bytes", static_cast<unsigned>(SERIALIZATION_BUFFER_SIZE));
        sendsFailedCount++;
        return false;
    }

    const int status = exchange("POST", package.getEdgeRoute(), serializationBuffer, nullptr);
    if (status == HTTP_CREATED || status == HTTP_OK) {
        sendsSucceededCount++;
        NODE_LOG_D(LOG_TAG, "%s -> %d", serializationBuffer, status);
        return true;
    }
    sendsFailedCount++;
    switch (status) {
        case HTTP_UNAUTHORIZED:
            signedIn = false;
            NODE_LOG_E(LOG_TAG, "The Edge rejected %s or its API key", deviceId);
            break;
        case HTTP_CONFLICT:
            NODE_LOG_W(LOG_TAG, "The Edge already has this observation with other values: %s", serializationBuffer);
            break;
        case HTTP_BAD_REQUEST:
            NODE_LOG_W(LOG_TAG, "The Edge rejected the data: %s", serializationBuffer);
            break;
        default:
            NODE_LOG_W(LOG_TAG, "Telemetry not delivered to %s%s (status %d)",
                         serviceEndpointUrl, package.getEdgeRoute(), status);
    }
    return false;
}

void EdgeGatewayClient::service() {
    if (!wifiDriver.isConnected()) return;
    synchronizeClock();
    checkSignIn();
    refreshConfiguration();
}

GatewayStatistics EdgeGatewayClient::getStatistics() const {
    GatewayStatistics statistics;
    statistics.sendsSucceeded = sendsSucceededCount.load();
    statistics.sendsFailed = sendsFailedCount.load();
    return statistics;
}

bool EdgeGatewayClient::isConnected() {
    return wifiDriver.isConnected();
}

void EdgeGatewayClient::setConfigurationListener(EdgeConfigurationListener* listener) {
    configurationListener = listener;
}

void EdgeGatewayClient::setConfigurationRefreshInterval(const unsigned long intervalInMilliseconds) {
    configurationRefreshIntervalInMilliseconds = intervalInMilliseconds;
}

bool EdgeGatewayClient::isClockSynchronized() {
    return TimeSynchronizationDriver::isSynchronized();
}

bool EdgeGatewayClient::isSignedIn() const {
    return signedIn.load();
}

bool EdgeGatewayClient::hasElapsed(const unsigned long since, const unsigned long intervalInMilliseconds) {
    return millis() - since >= intervalInMilliseconds;
}

void EdgeGatewayClient::synchronizeClock() {
    if (TimeSynchronizationDriver::isSynchronized()) return;
    if (lastClockAttemptAt != 0 && !hasElapsed(lastClockAttemptAt, RETRY_INTERVAL_IN_MILLISECONDS)) return;
    lastClockAttemptAt = millis();
    // The driver only marks the clock as synchronized inside initialize(), so the gateway retries it.
    TimeSynchronizationDriver::initialize(timeZone, ntpServer);
    if (TimeSynchronizationDriver::isSynchronized()) {
        NODE_LOG_I(LOG_TAG, "Clock synchronized with %s", ntpServer);
    } else {
        NODE_LOG_W(LOG_TAG, "Clock not synchronized yet; observations wait for it");
    }
}

void EdgeGatewayClient::checkSignIn() {
    if (signedIn.load()) return;
    if (lastSignInAttemptAt != 0 && !hasElapsed(lastSignInAttemptAt, RETRY_INTERVAL_IN_MILLISECONDS)) return;
    lastSignInAttemptAt = millis();

    const int healthStatus = exchange("GET", HEALTH_ROUTE, nullptr, nullptr);
    if (healthStatus <= 0) {
        NODE_LOG_W(LOG_TAG, "Edge not reachable at %s", serviceEndpointUrl);
        return;
    }

    JsonDocument request;
    request["device_id"] = deviceId;
    char body[96];
    serializeJson(request, body, sizeof(body));
    const int status = exchange("POST", SIGN_IN_ROUTE, body, nullptr);
    if (status == HTTP_OK) {
        signedIn = true;
        NODE_LOG_I(LOG_TAG, "Edge accepted %s (health %d)", deviceId, healthStatus);
    } else if (status == HTTP_UNAUTHORIZED) {
        NODE_LOG_E(LOG_TAG, "Edge rejected %s: check DEVICE_ID and DEVICE_API_KEY", deviceId);
    } else {
        NODE_LOG_W(LOG_TAG, "Sign-in of %s answered %d", deviceId, status);
    }
}

void EdgeGatewayClient::refreshConfiguration() {
    if (configurationListener == nullptr) return;
    const unsigned long interval = configurationRead ? configurationRefreshIntervalInMilliseconds
                                                     : RETRY_INTERVAL_IN_MILLISECONDS;
    if (lastConfigurationAttemptAt != 0 && !hasElapsed(lastConfigurationAttemptAt, interval)) return;
    lastConfigurationAttemptAt = millis();

    char route[URL_BUFFER_SIZE];
    snprintf(route, sizeof(route), "%s%s", CONFIGURATION_ROUTE_PREFIX, deviceId);
    String responseBody;
    const int status = exchange("GET", route, nullptr, &responseBody);
    if (status == HTTP_NOT_FOUND) {
        NODE_LOG_W(LOG_TAG, "The Edge has no profile of the cloud for %s yet", deviceId);
        return;
    }
    if (status != HTTP_OK) {
        NODE_LOG_W(LOG_TAG, "Configuration of %s not read (status %d)", deviceId, status);
        return;
    }
    JsonDocument configuration;
    if (const DeserializationError error = deserializeJson(configuration, responseBody); error) {
        NODE_LOG_W(LOG_TAG, "Configuration of %s not understood: %s", deviceId, error.c_str());
        return;
    }
    configurationRead = true;
    configurationListener->onEdgeConfiguration(configuration.as<JsonObjectConst>());
}

int EdgeGatewayClient::exchange(const char* method, const char* route, const char* body, String* responseBody) const {
    char url[URL_BUFFER_SIZE];
    if (snprintf(url, sizeof(url), "%s%s", serviceEndpointUrl, route) >= static_cast<int>(sizeof(url))) {
        NODE_LOG_E(LOG_TAG, "URL too long for %s", route);
        return -1;
    }
    WiFiClient socket;
    HTTPClient http;
    if (!http.begin(socket, url)) {
        return -1;
    }
    http.setTimeout(HTTP_TIMEOUT_IN_MILLISECONDS);
    http.addHeader("Content-Type", "application/json");
    http.addHeader(API_KEY_HEADER, apiKey);
    const int status = body == nullptr
        ? http.sendRequest(method)
        : http.sendRequest(method, reinterpret_cast<uint8_t*>(const_cast<char*>(body)), strlen(body));
    if (responseBody != nullptr && status > 0) {
        *responseBody = http.getString();
    }
    http.end();
    return status;
}
