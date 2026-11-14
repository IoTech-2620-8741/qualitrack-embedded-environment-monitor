/**
 * @file NodeLog.h
 * @brief Serial messages of the node, visible in the serial monitor of Arduino and Wokwi.
 *
 * On the ESP32 the `MODEST_LOG_*` macros of the framework go to the ESP-IDF log, which prints nothing
 * with the default core debug level. The node writes its own messages to `Serial`, as the examples of
 * the framework do. Define `NODE_LOG_DEBUG` to also print the requests sent to the Edge.
 */
#ifndef NODE_LOG_H
#define NODE_LOG_H

#include <Arduino.h>

#define NODE_LOG(level, tag, fmt, ...) Serial.printf("[%s] [%s] " fmt "\n", level, tag, ##__VA_ARGS__)
#define NODE_LOG_E(tag, fmt, ...) NODE_LOG("E", tag, fmt, ##__VA_ARGS__)
#define NODE_LOG_W(tag, fmt, ...) NODE_LOG("W", tag, fmt, ##__VA_ARGS__)
#define NODE_LOG_I(tag, fmt, ...) NODE_LOG("I", tag, fmt, ##__VA_ARGS__)

#if defined(NODE_LOG_DEBUG)
#define NODE_LOG_D(tag, fmt, ...) NODE_LOG("D", tag, fmt, ##__VA_ARGS__)
#else
#define NODE_LOG_D(tag, fmt, ...) ((void) 0)
#endif

#endif // NODE_LOG_H
