/**
 * @file Arduino.h
 * @brief Host fake of the Arduino core for the Environment Monitor tests, built on the framework's fake.
 *
 * The fake of the Modest IoT Nano Framework (`tests/fakes/Arduino.h` of the framework) makes every FreeRTOS call a
 * no-op: queues always fail, and tasks and the hardware timer never run. It also has no `Serial`. A whole sketch then
 * never samples its sensors, and its readings and commands are lost. This file includes that fake with those functions
 * renamed, then defines working ones under the real names; everything else (pins, ADC, PWM, `tone`, interrupts,
 * `millis`) comes from the framework's fake as it is.
 *
 * Tasks run cooperatively: runFakeTask() calls a task's function, which returns once the task has handled every
 * notification it was sent.
 */
#ifndef ENVIRONMENT_MONITOR_TEST_ARDUINO_H
#define ENVIRONMENT_MONITOR_TEST_ARDUINO_H

#define xTaskCreatePinnedToCore frameworkNoOpTaskCreatePinnedToCore
#define xTaskNotify frameworkNoOpTaskNotify
#define xTaskNotifyFromISR frameworkNoOpTaskNotifyFromIsr
#define xTaskNotifyWait frameworkNoOpTaskNotifyWait
#define xQueueCreate frameworkNoOpQueueCreate
#define xQueueSend frameworkNoOpQueueSend
#define xQueueReceive frameworkNoOpQueueReceive
#define vQueueDelete frameworkNoOpQueueDelete
#define uxQueueMessagesWaiting frameworkNoOpQueueMessagesWaiting
#define timerAttachInterruptArg frameworkNoOpTimerAttachInterruptArg
#define String FrameworkFakeString
#include_next <Arduino.h>
#undef xTaskCreatePinnedToCore
#undef xTaskNotify
#undef xTaskNotifyFromISR
#undef xTaskNotifyWait
#undef xQueueCreate
#undef xQueueSend
#undef xQueueReceive
#undef vQueueDelete
#undef uxQueueMessagesWaiting
#undef timerAttachInterruptArg
#undef String

#include <algorithm>
#include <cstring>
#include <deque>
#include <string>
#include <utility>
#include <vector>

#include "HardwareSerial.h"

/** @brief Text the real ArduinoJson can serialize into, which needs `write()`. */
class String : public std::string {
public:
    using std::string::string;
    String(const std::string& text) : std::string(text) {}
    String(std::string&& text) noexcept : std::string(std::move(text)) {}
    String& operator=(const std::string& text) {
        std::string::operator=(text);
        return *this;
    }
    String& operator=(std::string&& text) noexcept {
        std::string::operator=(std::move(text));
        return *this;
    }

    /** @brief Appends one character. @return 1. */
    size_t write(uint8_t character) {
        push_back(static_cast<char>(character));
        return 1;
    }

    /** @brief Appends @p length characters. @return @p length. */
    size_t write(const uint8_t* characters, size_t length) {
        append(reinterpret_cast<const char*>(characters), length);
        return length;
    }
};

/** @brief A FreeRTOS task the code under test created; its handle is a pointer to this record. */
struct FakeTask {
    const char* name = nullptr;              ///< Task name, e.g. "HardwareWorker" for the sampler of `Device`.
    void (*function)(void*) = nullptr;       ///< Task entry point.
    void* parameter = nullptr;               ///< Argument of the entry point.
    uint32_t pendingNotificationBits = 0;    ///< Bits notified and not yet taken by xTaskNotifyWait().
};

/** @brief Task slots; `Device` creates two tasks (sampler and network) and a new `Device` reuses them by name. */
inline FakeTask fakeTasks[2];

/** @brief Task whose function runFakeTask() is running, or `nullptr`. */
inline FakeTask* fakeRunningTask = nullptr;

/** @brief Records the task under its name instead of starting it; runFakeTask() runs it. */
inline void xTaskCreatePinnedToCore(void (*function)(void*), const char* name, std::uint32_t, void* parameter,
                                    std::uint8_t, TaskHandle_t* handle, BaseType_t) {
    for (FakeTask& task : fakeTasks) {
        if (task.name != nullptr && std::strcmp(task.name, name) != 0) continue;
        task = FakeTask{name, function, parameter, 0};
        if (handle != nullptr) *handle = &task;
        return;
    }
}

/** @brief Sets @p bits on the task. @return `pdPASS`. */
inline BaseType_t xTaskNotify(TaskHandle_t handle, uint32_t bits, int) {
    if (handle != nullptr) static_cast<FakeTask*>(handle)->pendingNotificationBits |= bits;
    return pdPASS;
}

/** @brief Same as xTaskNotify(). @return `pdPASS`. */
inline BaseType_t xTaskNotifyFromISR(TaskHandle_t handle, uint32_t bits, int action, BaseType_t*) {
    return xTaskNotify(handle, bits, action);
}

/**
 * @brief Hands the running task its pending bits.
 *
 * With nothing pending, a polling wait (@p timeout 0) returns `pdFALSE`, and a blocking wait returns the termination
 * bit of `Device` (`0x2`), so the task's loop ends and runFakeTask() returns.
 * @return `pdTRUE` when @p value was written, `pdFALSE` otherwise.
 */
inline BaseType_t xTaskNotifyWait(BaseType_t, BaseType_t, uint32_t* value, TickType_t timeout) {
    if (fakeRunningTask == nullptr) return pdFALSE;
    uint32_t& pendingBits = fakeRunningTask->pendingNotificationBits;
    if (pendingBits == 0 && timeout == 0) return pdFALSE;
    *value = pendingBits == 0 ? 0x2 : pendingBits;
    pendingBits = 0;
    return pdTRUE;
}

/** @brief Runs the task called @p name until it has handled all its notifications. */
inline void runFakeTask(const char* name) {
    for (FakeTask& task : fakeTasks) {
        if (task.name == nullptr || std::strcmp(task.name, name) != 0) continue;
        fakeRunningTask = &task;
        task.function(task.parameter);
        fakeRunningTask = nullptr;
        return;
    }
}

/** @brief A FreeRTOS queue that keeps copies of its items. */
struct FakeQueue {
    std::uint32_t capacity;                          ///< Maximum number of items.
    std::uint32_t itemSize;                          ///< Bytes copied per item.
    std::deque<std::vector<unsigned char>> items;    ///< Items, oldest first.
};

/** @brief Every queue created and not yet deleted, in creation order. */
inline std::vector<FakeQueue*> fakeQueues;

/** @brief Creates an empty queue. @return Its handle. */
inline QueueHandle_t xQueueCreate(std::uint32_t capacity, std::uint32_t itemSize) {
    fakeQueues.push_back(new FakeQueue{capacity, itemSize, {}});
    return fakeQueues.back();
}

/** @brief Copies @p item to the back of the queue. @return `pdFAIL` when the queue is full, `pdPASS` otherwise. */
inline BaseType_t xQueueSend(QueueHandle_t handle, const void* item, TickType_t) {
    auto* queue = static_cast<FakeQueue*>(handle);
    if (queue == nullptr || queue->items.size() >= queue->capacity) return pdFAIL;
    const auto* bytes = static_cast<const unsigned char*>(item);
    queue->items.emplace_back(bytes, bytes + queue->itemSize);
    return pdPASS;
}

/** @brief Moves the oldest item into @p item. @return `pdFAIL` when the queue is empty, `pdPASS` otherwise. */
inline BaseType_t xQueueReceive(QueueHandle_t handle, void* item, TickType_t) {
    auto* queue = static_cast<FakeQueue*>(handle);
    if (queue == nullptr || queue->items.empty()) return pdFAIL;
    std::memcpy(item, queue->items.front().data(), queue->itemSize);
    queue->items.pop_front();
    return pdPASS;
}

/** @brief @return Number of items in the queue. */
inline UBaseType_t uxQueueMessagesWaiting(QueueHandle_t handle) {
    const auto* queue = static_cast<const FakeQueue*>(handle);
    return queue == nullptr ? 0 : static_cast<UBaseType_t>(queue->items.size());
}

/** @brief Deletes the queue and forgets it. */
inline void vQueueDelete(QueueHandle_t handle) {
    auto* queue = static_cast<FakeQueue*>(handle);
    fakeQueues.erase(std::remove(fakeQueues.begin(), fakeQueues.end(), queue), fakeQueues.end());
    delete queue;
}

/** @brief Interrupt routine of the hardware timer, called by the tests for each tick. */
inline void (*fakeTimerInterrupt)(void*) = nullptr;

/** @brief Argument of fakeTimerInterrupt(). */
inline void* fakeTimerInterruptArgument = nullptr;

/** @brief Records the timer's interrupt routine instead of arming it. */
inline void timerAttachInterruptArg(hw_timer_t*, void (*function)(void*), void* argument) {
    fakeTimerInterrupt = function;
    fakeTimerInterruptArgument = argument;
}

#endif // ENVIRONMENT_MONITOR_TEST_ARDUINO_H
