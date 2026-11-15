/**
 * @file test.cpp
 * @brief Host tests of the sign-in of the Environment Monitor and of the profile it reads from the Edge (MS14, MS15).
 *
 * The network tests calibrate the node first, so the first network pass does not run at millis() 0; the retry
 * intervals are measured from that pass.
 */
#include "../support/EnvironmentMonitorHarness.h"

namespace {

/** @brief Profile of the Edge, version 3, with another metric first and the absent limits as JSON nulls. */
constexpr const char* PROFILE_VERSION_3 =
    R"({"profile_version":3,"thresholds":[{"metric":"TEMPERATURE","normal_max":8,"critical_max":10},)"
    R"({"metric":"AIR_QUALITY","normal_min":null,"normal_max":950,"critical_min":null,"critical_max":1100}]})";

/** @return Whether a node that reads @p configuration from the Edge queues no command and applies no profile. */
bool profileIsIgnored(const char* configuration) {
    EnvironmentMonitorHarness harness;
    HTTPClient::setResponse(200, configuration);
    harness.monitor().maintainNetworkServices();
    return harness.pendingCommandCount() == 0 && !harness.logged("applied");
}

/** @brief MS14: setup() sends the API key of the node as a Bearer token. */
void setupSendsTheApiKeyAsABearerToken() {
    resetFakes();
    setup();
    assert(std::string(authorizationHeader) == std::string("Bearer ") + DEVICE_API_KEY);
    assert(Serial.output.find(std::string(DEVICE_ID) + " starting") != std::string::npos);

    delete environmentMonitor;
    delete edgeGateway;
    delete transportDriver;
    environmentMonitor = nullptr;
    edgeGateway = nullptr;
    transportDriver = nullptr;
}

/** @brief MS14: the sign-in posts the identifier of the node. */
void signInPostsTheDeviceId() {
    EnvironmentMonitorHarness harness;
    harness.calibrateInCleanAir();
    harness.runNetworkServices();
    assert(HTTPClient::postCount == 1);
    assert(HTTPClient::lastPostPayload == std::string(R"({"device_id":")") + DEVICE_ID + R"("})");
    assert(harness.logged(std::string("Edge accepted ") + DEVICE_ID));
}

/** @brief MS14: a sign-in the Edge rejects is tried again 30 seconds later, not before. */
void aRejectedSignInIsRetriedEveryThirtySeconds() {
    EnvironmentMonitorHarness harness;
    HTTPClient::setResponse(401);
    harness.calibrateInCleanAir();
    harness.runNetworkServices();
    assert(harness.logged("Edge rejected the node: check DEVICE_ID and DEVICE_API_KEY"));
    assert(HTTPClient::postCount == 1);

    harness.advanceMilliseconds(29900);
    harness.runNetworkServices();
    assert(HTTPClient::postCount == 1);

    harness.advanceMilliseconds(100);
    harness.runNetworkServices();
    assert(HTTPClient::postCount == 2);
}

/** @brief MS14, MS15: without Wi-Fi the node does not call the Edge. */
void noWifiSkipsTheEdge() {
    EnvironmentMonitorHarness harness;
    WiFi.setStatus(WL_DISCONNECTED);
    harness.calibrateInCleanAir();
    harness.runNetworkServices();
    assert(HTTPClient::postCount == 0);
    assert(HTTPClient::getCount == 0);
}

/** @brief MS15: a valid profile reaches the sampler as five commands and changes the state at once. */
void aValidProfileIsApplied() {
    EnvironmentMonitorHarness harness;
    harness.calibrateInCleanAir();
    harness.setAirQualityInPpm(900);
    harness.advanceMilliseconds(1000);
    assert(harness.ledColor() == LedColor::Yellow);

    HTTPClient::setResponse(200, PROFILE_VERSION_3);
    harness.monitor().maintainNetworkServices();
    assert(harness.pendingCommandCount() == 5); // Four limits and APPLY_PROFILE.

    harness.advanceMilliseconds(100);
    assert(harness.pendingCommandCount() == 0);
    assert(harness.ledColor() == LedColor::Green);
    assert(harness.logged("Air quality profile version 3 applied"));
}

/** @brief MS15: a profile with lower limits only is applied: under 500 ppm is WARNING, under 300 ppm CRITICAL. */
void aLowerOnlyProfileIsApplied() {
    EnvironmentMonitorHarness harness;
    HTTPClient::setResponse(
        200, R"({"profile_version":4,"thresholds":[{"metric":"AIR_QUALITY","normal_min":500,"critical_min":300}]})");
    harness.calibrateInCleanAir();
    harness.runNetworkServices();
    assert(harness.ledColor() == LedColor::Yellow); // Clean air reads 408 ppm.

    harness.setAirQualityInPpm(250);
    harness.advanceMilliseconds(1000);
    assert(harness.ledColor() == LedColor::Red);
}

/** @brief MS15: a malformed, partial or inconsistent profile is ignored and the current profile stays. */
void invalidProfilesAreIgnored() {
    assert(profileIsIgnored(R"({"profile_version":)"));
    assert(profileIsIgnored(""));
    assert(profileIsIgnored(
        R"({"profile_version":0,"thresholds":[{"metric":"AIR_QUALITY","normal_max":950,"critical_max":1100}]})"));
    assert(profileIsIgnored(
        R"({"profile_version":3,"thresholds":[{"metric":"TEMPERATURE","normal_max":8,"critical_max":10}]})"));
    assert(profileIsIgnored(R"({"profile_version":3,"thresholds":[]})"));
    assert(profileIsIgnored(
        R"({"profile_version":3,"thresholds":[{"metric":"AIR_QUALITY","normal_max":950,"critical_max":950}]})"));
    assert(profileIsIgnored(R"({"profile_version":3,"thresholds":[{"metric":"AIR_QUALITY","normal_max":950}]})"));
    assert(profileIsIgnored(R"({"profile_version":3,"thresholds":[{"metric":"AIR_QUALITY",)"
                            R"("normal_min":900,"normal_max":800,"critical_min":700,"critical_max":1000}]})"));
    assert(profileIsIgnored(
        R"({"profile_version":3,"thresholds":[{"metric":"AIR_QUALITY","normal_max":"950","critical_max":1100}]})"));
}

/** @brief MS15: while the Edge has no profile for the node (404), the node asks again every 30 seconds. */
void aMissingProfileIsReadAgainEveryThirtySeconds() {
    EnvironmentMonitorHarness harness;
    HTTPClient::setResponse(404);
    harness.calibrateInCleanAir();
    harness.runNetworkServices();
    assert(harness.logged("The Edge has no profile for this node yet"));
    assert(HTTPClient::getCount == 1);

    harness.advanceMilliseconds(29900);
    harness.runNetworkServices();
    assert(HTTPClient::getCount == 1);

    harness.advanceMilliseconds(100);
    harness.runNetworkServices();
    assert(HTTPClient::getCount == 2);
}

/** @brief MS15: once a profile is applied, the node reads it again every five minutes. */
void anAppliedProfileIsReadAgainAfterFiveMinutes() {
    EnvironmentMonitorHarness harness;
    HTTPClient::setResponse(200, PROFILE_VERSION_3);
    harness.calibrateInCleanAir();
    harness.runNetworkServices();
    const int readsAfterTheProfile = HTTPClient::getCount;

    harness.advanceMilliseconds(30000);
    harness.runNetworkServices();
    assert(HTTPClient::getCount == readsAfterTheProfile);

    harness.advanceMilliseconds(270000);
    harness.runNetworkServices();
    assert(HTTPClient::getCount == readsAfterTheProfile + 1);
}

} // namespace

int main() {
    std::printf("Running Edge Configuration tests...\n");
    RUN_TEST(setupSendsTheApiKeyAsABearerToken);
    RUN_TEST(signInPostsTheDeviceId);
    RUN_TEST(aRejectedSignInIsRetriedEveryThirtySeconds);
    RUN_TEST(noWifiSkipsTheEdge);
    RUN_TEST(aValidProfileIsApplied);
    RUN_TEST(aLowerOnlyProfileIsApplied);
    RUN_TEST(invalidProfilesAreIgnored);
    RUN_TEST(aMissingProfileIsReadAgainEveryThirtySeconds);
    RUN_TEST(anAppliedProfileIsReadAgainAfterFiveMinutes);
    return 0;
}
