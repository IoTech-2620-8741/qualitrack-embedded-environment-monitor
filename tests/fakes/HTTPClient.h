/**
 * @file HTTPClient.h
 * @brief Host fake of the HTTPClient of the ESP32 core for the Environment Monitor tests, built on the framework's fake.
 *
 * The fake of the Modest IoT Nano Framework answers POST, PUT and the other methods with the body set by
 * `setResponse()`, but leaves GET with the body of the previous request of the same client. The node reads its
 * configuration with a client of its own, so here GET answers that body too; everything else comes from the framework's
 * fake as it is.
 */
#ifndef ENVIRONMENT_MONITOR_TEST_HTTP_CLIENT_H
#define ENVIRONMENT_MONITOR_TEST_HTTP_CLIENT_H

#define HTTPClient FrameworkFakeHttpClient
#include_next <HTTPClient.h>
#undef HTTPClient

/** @brief The framework's fake HTTPClient, whose GET also answers the body set with `setResponse()`. */
class HTTPClient : public FrameworkFakeHttpClient {
public:
    int GET() {
        answeredGet = true;
        return FrameworkFakeHttpClient::GET();
    }

    String getString() {
        return answeredGet ? String(customResponse) : FrameworkFakeHttpClient::getString();
    }

private:
    bool answeredGet = false;
};

#endif // ENVIRONMENT_MONITOR_TEST_HTTP_CLIENT_H
