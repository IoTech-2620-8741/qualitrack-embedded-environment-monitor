/**
 * @file EdgeGatewayClient.h
 * @brief Gateway of the Modest IoT Nano Framework for the QualiTrack Edge Service.
 *
 * The `HttpGatewayClient` of the framework sends every package to one URL, authenticates with the
 * `Authorization` header and adds the `timestamp` and `sequence` envelope. The QualiTrack Edge API
 * needs something else, so this class extends the `GatewayClient` extension point of the framework:
 *
 * - every request carries the identifier of the node in the body (`device_id`) and its API key in the
 *   `X-API-Key` header;
 * - each `EdgeTelemetryPackage` goes to its own route, without the envelope and with the observation
 *   time in ISO 8601 taken from NTP;
 * - on the network task, it synchronizes the clock, checks the identity of the node
 *   (`POST /api/v1/auth/sign-in`) and reads the configuration of the node
 *   (`GET /api/v1/configurations/{deviceId}`) from time to time, handing it to a listener.
 *
 * Each request uses its own `HTTPClient`, so a call from `setup()` and a call from the network task
 * never share a connection.
 */
#ifndef EDGE_GATEWAY_CLIENT_H
#define EDGE_GATEWAY_CLIENT_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <atomic>
#include <ModestIoT.h>

/**
 * @brief Receives the configuration the Edge hands the node.
 * @note Called on the network task: implementations must hand the data to the sampler task safely.
 */
class EdgeConfigurationListener {
public:
    virtual ~EdgeConfigurationListener() = default;

    /**
     * @brief Called with each configuration the node reads from the Edge.
     * @param configuration Body of `GET /api/v1/configurations/{deviceId}`.
     */
    virtual void onEdgeConfiguration(JsonObjectConst configuration) = 0;
};

/**
 * @brief Sends the telemetry of the node to the QualiTrack Edge API and reads its configuration.
 */
class EdgeGatewayClient : public GatewayClient {
public:
    /// Route that checks the identifier and the API key of the node.
    static constexpr const char* SIGN_IN_ROUTE = "/api/v1/auth/sign-in";
    /// Route of the health of the Edge (no API key needed).
    static constexpr const char* HEALTH_ROUTE = "/api/v1/health";
    /// Route prefix of the configuration of a node.
    static constexpr const char* CONFIGURATION_ROUTE_PREFIX = "/api/v1/configurations/";
    /// Header of the API key the Edge issued for the node.
    static constexpr const char* API_KEY_HEADER = "X-API-Key";

    /// Time limit of each HTTP request.
    static constexpr uint16_t HTTP_TIMEOUT_IN_MILLISECONDS = 5000;
    /// Default interval between two reads of the configuration.
    static constexpr unsigned long DEFAULT_CONFIGURATION_REFRESH_IN_MILLISECONDS = 300000;
    /// Wait between two attempts of the clock synchronization, the sign-in or a failed configuration read.
    static constexpr unsigned long RETRY_INTERVAL_IN_MILLISECONDS = 30000;
    /// Size of the buffer of a serialized request.
    static constexpr size_t SERIALIZATION_BUFFER_SIZE = 384;
    /// Size of the buffer of a URL.
    static constexpr size_t URL_BUFFER_SIZE = 160;

    /**
     * @brief Creates the gateway of a node.
     * @param transportDriver Wi-Fi driver of the framework.
     * @param edgeBaseUrl Base URL of the Edge without a trailing slash, for example "http://192.168.1.50:5000".
     * @param deviceId Identifier of the node registered in QualiTrack and in the Edge.
     * @param apiKey API key the Edge issued for the node.
     * @param timeZone POSIX time zone for the clock (UTC is enough: the Edge receives UTC times).
     * @param ntpServer NTP server.
     */
    EdgeGatewayClient(WiFiConnectivityDriver& transportDriver, const char* edgeBaseUrl, const char* deviceId,
                      const char* apiKey, const char* timeZone, const char* ntpServer);

    ~EdgeGatewayClient() override = default;

    /**
     * @brief Sends an `EdgeTelemetryPackage` to its route.
     * @param abstractTelemetryPayload Package to send; it must be an `EdgeTelemetryPackage`.
     * @return True when the Edge stored it (201) or already had it (200).
     * @runs_on{network}
     */
    bool sendTelemetryRecord(const TelemetryPackage& abstractTelemetryPayload) override;

    /**
     * @brief Keeps the link with the Edge: clock, sign-in check and configuration refresh.
     * @runs_on{network}
     */
    void service() override;

    /** @return Successful and failed telemetry sends. */
    [[nodiscard]] GatewayStatistics getStatistics() const override;

    /** @return True when Wi-Fi is up. */
    [[nodiscard]] bool isConnected() override;

    /**
     * @brief Registers who receives the configuration of the node.
     * @param listener Listener, or nullptr to stop reading the configuration.
     */
    void setConfigurationListener(EdgeConfigurationListener* listener);

    /**
     * @brief Changes how often the configuration is read.
     * @param intervalInMilliseconds Interval between two successful reads.
     */
    void setConfigurationRefreshInterval(unsigned long intervalInMilliseconds);

    /** @return True when the clock is synchronized, so observations can be timed. */
    [[nodiscard]] static bool isClockSynchronized();

    /** @return True once the Edge accepted the identifier and the API key of the node. */
    [[nodiscard]] bool isSignedIn() const;

private:
    WiFiConnectivityDriver& wifiDriver;
    const char* deviceId;
    const char* apiKey;
    const char* timeZone;
    const char* ntpServer;
    EdgeConfigurationListener* configurationListener;
    unsigned long configurationRefreshIntervalInMilliseconds;

    std::atomic<uint32_t> sendsSucceededCount;
    std::atomic<uint32_t> sendsFailedCount;
    std::atomic<bool> signedIn;

    bool configurationRead;
    unsigned long lastClockAttemptAt;
    unsigned long lastSignInAttemptAt;
    unsigned long lastConfigurationAttemptAt;
    char serializationBuffer[SERIALIZATION_BUFFER_SIZE];

    /** @return True when the interval passed since the moment (handles millis() overflow). */
    static bool hasElapsed(unsigned long since, unsigned long intervalInMilliseconds);

    /**
     * @brief Sends one request to the Edge with the API key of the node.
     * @param method "GET" or "POST".
     * @param route Route relative to the Edge base URL.
     * @param body JSON body, or nullptr.
     * @param responseBody Receives the body of the answer when not nullptr.
     * @return HTTP status, or a negative HTTPClient error.
     */
    int exchange(const char* method, const char* route, const char* body, String* responseBody) const;

    void synchronizeClock();
    void checkSignIn();
    void refreshConfiguration();
};

#endif // EDGE_GATEWAY_CLIENT_H
