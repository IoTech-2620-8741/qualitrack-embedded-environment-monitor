/**
 * @file HardwareSerial.h
 * @brief Host fake of the serial port: it keeps what the sketch prints so the tests can read its log.
 */
#ifndef ENVIRONMENT_MONITOR_TEST_HARDWARE_SERIAL_H
#define ENVIRONMENT_MONITOR_TEST_HARDWARE_SERIAL_H

#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>

/** @brief Serial port whose output is kept in memory. */
class HardwareSerial {
public:
    std::string output; ///< Everything printed since the last clear.

    /** @brief Does nothing: the host has no baud rate. */
    void begin(unsigned long) {}

    /** @brief Appends @p text and a line break. @return Characters appended. */
    size_t println(const char* text) {
        output += text;
        output += '\n';
        return std::strlen(text) + 1;
    }

    /** @brief Appends the formatted text, cut at 255 characters. @return Characters appended. */
    size_t printf(const char* format, ...) __attribute__((format(printf, 2, 3))) {
        char buffer[256];
        va_list arguments;
        va_start(arguments, format);
        std::vsnprintf(buffer, sizeof(buffer), format, arguments);
        va_end(arguments);
        output += buffer;
        return std::strlen(buffer);
    }
};

/** @brief The serial port the sketch prints to. */
inline HardwareSerial Serial;

#endif // ENVIRONMENT_MONITOR_TEST_HARDWARE_SERIAL_H
