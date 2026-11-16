#!/usr/bin/env bash
# Builds and runs the host tests of the Environment Monitor. Every tests/<Suite>/test.cpp compiles the real sketch
# against the fakes of the Modest IoT Nano Framework (see tests/fakes/Arduino.h) and runs as its own binary.
#
# Usage: tests/run_all.sh
#   ENABLE_SANITIZERS=0  turns off AddressSanitizer and UndefinedBehaviorSanitizer.
#   CXX=clang++          picks another compiler (g++ by default).
# The first run downloads the pinned framework and ArduinoJson into build/test-dependencies/.
set -euo pipefail

FRAMEWORK_VERSION="v1.0.0"
ARDUINOJSON_VERSION="v7.4.3"

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${PROJECT_ROOT}"

DEPENDENCIES_DIR="build/test-dependencies"
FRAMEWORK_DIR="${DEPENDENCIES_DIR}/modest-iot-nano-framework-${FRAMEWORK_VERSION}"
ARDUINOJSON_DIR="${DEPENDENCIES_DIR}/arduinojson-${ARDUINOJSON_VERSION}"
BUILD_DIR="build/host-tests"
OBJECTS_DIR="${BUILD_DIR}/objects"

# Downloads go to a temporary name first, so an interrupted download is never mistaken for a complete one.
if [[ ! -d "${FRAMEWORK_DIR}" ]]; then
    echo "==> Downloading Modest IoT Nano Framework ${FRAMEWORK_VERSION}"
    rm -rf "${FRAMEWORK_DIR}.partial"
    git -c advice.detachedHead=false clone --quiet --depth 1 --branch "${FRAMEWORK_VERSION}" \
        https://github.com/modest-iot/modest-iot-nano-framework.git "${FRAMEWORK_DIR}.partial"
    mv "${FRAMEWORK_DIR}.partial" "${FRAMEWORK_DIR}"
fi
if [[ ! -f "${ARDUINOJSON_DIR}/ArduinoJson.h" ]]; then
    echo "==> Downloading ArduinoJson ${ARDUINOJSON_VERSION}"
    mkdir -p "${ARDUINOJSON_DIR}"
    curl -fsSL -o "${ARDUINOJSON_DIR}/ArduinoJson.h.partial" \
        "https://github.com/bblanchon/ArduinoJson/releases/download/${ARDUINOJSON_VERSION}/ArduinoJson-${ARDUINOJSON_VERSION}.h"
    mv "${ARDUINOJSON_DIR}/ArduinoJson.h.partial" "${ARDUINOJSON_DIR}/ArduinoJson.h"
fi

CXX="${CXX:-g++}"
COMPILE_FLAGS=(-std=c++17 -Wall -Wextra -Werror)
if [[ "${ENABLE_SANITIZERS:-1}" == "1" ]]; then
    # Without -fno-sanitize-recover, undefined behavior is only printed and the test still passes.
    COMPILE_FLAGS+=(-fsanitize=address,undefined -fno-sanitize-recover=undefined)
fi
# Order matters: the shim in tests/fakes comes first, and the real ArduinoJson comes before the framework's fake one.
INCLUDE_FLAGS=(-include Arduino.h -Itests/fakes "-I${ARDUINOJSON_DIR}" "-I${FRAMEWORK_DIR}/tests/fakes"
               "-I${FRAMEWORK_DIR}/src" -IEnvironmentMonitor)

# The framework classes the sketch uses; TimeSynchronizationDriver.cpp is replaced by the fake clock.
SHARED_SOURCES=()
for framework_source in Device SamplingScheduler Sensor Actuator GasSensor PassiveInfraredSensor RgbLed Buzzer \
                        WiFiConnectivityDriver HttpGatewayClient; do
    SHARED_SOURCES+=("${FRAMEWORK_DIR}/src/${framework_source}.cpp")
done
SHARED_SOURCES+=("${FRAMEWORK_DIR}/tests/fakes/ArduinoFake.cpp" tests/fakes/TimeSynchronizationDriverFake.cpp)

echo "=========================================================="
echo " Environment Monitor host tests"
echo " Compiler: ${CXX}"
echo " Flags:    ${COMPILE_FLAGS[*]}"
echo "=========================================================="

rm -rf "${BUILD_DIR}"
mkdir -p "${OBJECTS_DIR}"
echo "==> [BUILD] framework and fakes"
shared_objects=()
for source in "${SHARED_SOURCES[@]}"; do
    object="${OBJECTS_DIR}/$(basename "${source%.cpp}").o"
    "${CXX}" "${COMPILE_FLAGS[@]}" "${INCLUDE_FLAGS[@]}" -c "${source}" -o "${object}"
    shared_objects+=("${object}")
done

passed=0
failed=0
for test_source in tests/*/test.cpp; do
    suite="test_$(basename "$(dirname "${test_source}")")"
    binary="${BUILD_DIR}/${suite}"

    echo "==> [BUILD] ${suite}"
    if ! "${CXX}" "${COMPILE_FLAGS[@]}" "${INCLUDE_FLAGS[@]}" "${test_source}" "${shared_objects[@]}" -o "${binary}"; then
        echo "==> [BUILD] ${suite} ... FAILED (compilation)"
        failed=$((failed + 1))
        continue
    fi

    if "${binary}"; then
        echo "==> [RUN]   ${suite} ... PASS"
        passed=$((passed + 1))
    else
        echo "==> [RUN]   ${suite} ... FAIL"
        failed=$((failed + 1))
    fi
done

echo "=========================================================="
echo " Suites passed: ${passed}, failed: ${failed}"
echo "=========================================================="
[[ "${failed}" -eq 0 ]]
