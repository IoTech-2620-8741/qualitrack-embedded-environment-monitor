/**
 * @file TimeSynchronizationDriverFake.cpp
 * @brief Test-controlled NTP clock, linked instead of the framework's `TimeSynchronizationDriver.cpp`.
 *
 * On the host the framework's driver keeps its `synchronized` flag private and always false, so the node would never
 * send a reading. The tests set the clock through the two variables below.
 */
#include "TimeSynchronizationDriver.h"

/** @brief Whether the node's clock counts as synchronized with NTP. */
bool fakeClockSynchronized = false;

/** @brief UTC time, in seconds since the epoch, that the clock returns (2026-11-13T15:00:00Z). */
uint32_t fakeUnixTime = 1794582000u;

/** @brief Does nothing: the tests set fakeClockSynchronized. */
void TimeSynchronizationDriver::initialize(const char*, const char*, const char*) {}

/** @brief @return fakeUnixTime. */
uint32_t TimeSynchronizationDriver::getCurrentTimestamp() { return fakeUnixTime; }

/** @brief @return fakeClockSynchronized. */
bool TimeSynchronizationDriver::isSynchronized() { return fakeClockSynchronized; }
