#include "FreeRTOSTaskRunner.h"
#include "schedule_manager.h"
#include <cstdio>

#if defined(ESP_PLATFORM) || defined(ARDUINO)
#include <esp_log.h>
static const char* TAG = "FREERTOS_TASK_RUNNER";
#else
#define TAG "FREERTOS_TASK_RUNNER"
#define ESP_LOGI(tag, fmt, ...) printf("[INFO][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGE(tag, fmt, ...) printf("[ERR][%s] " fmt "\n", tag, ##__VA_ARGS__)
#endif

struct TaskRunnerParam {
    RelayTaskContext context;
    FreeRTOSTaskRunner* runner;
};

FreeRTOSTaskRunner::FreeRTOSTaskRunner() {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    lifecycle_events_ = xEventGroupCreate();
    lifecycle_lock_ = portMUX_INITIALIZER_UNLOCKED;
    for (uint8_t i = 0; i < TOTAL_RELAYS; ++i) {
        task_handles_[i] = nullptr;
    }
#else
    for (uint8_t i = 0; i < TOTAL_RELAYS; ++i) {
        task_alive_[i] = false;
        startup_complete_[i] = false;
        startup_succeeded_[i] = false;
    }
#endif
}

FreeRTOSTaskRunner::~FreeRTOSTaskRunner() {
    for (uint8_t i = 0; i < TOTAL_RELAYS; ++i) {
        requestStop(i);
        waitUntilManagerCallbackExited(i, 1000);
    }
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    // The callback-exit acknowledgement guarantees no relay task can touch
    // this runner after it is observed; it is deliberately not a kernel join.
    if (lifecycle_events_ != nullptr) {
        vEventGroupDelete(lifecycle_events_);
        lifecycle_events_ = nullptr;
    }
#endif
}

#if defined(ESP_PLATFORM) || defined(ARDUINO)
void FreeRTOSTaskRunner::taskEntryTrampoline(void* param) {
    TaskRunnerParam* p = reinterpret_cast<TaskRunnerParam*>(param);
    RelayTaskContext context = p->context;
    FreeRTOSTaskRunner* runner = p->runner;
    delete p;

    if (context.manager != nullptr) {
        context.manager->runRelayTask(context.relay_id);
        // From this point the task will not access ScheduleManager again.
        runner->notifyManagerCallbackExited(context.relay_id);
    }
    // A FreeRTOS task must not return from its entry function. The preceding
    // acknowledgement means only that the manager callback exited, never that
    // the kernel has already reclaimed this self-deleting task.
    vTaskDelete(nullptr);
}
#endif

bool FreeRTOSTaskRunner::startTask(uint8_t relay_id, const RelayTaskContext& context) {
    if (relay_id >= TOTAL_RELAYS || context.manager == nullptr || context.relay_id != relay_id) {
        return false;
    }

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (lifecycle_events_ == nullptr) return false;

    portENTER_CRITICAL(&lifecycle_lock_);
    const bool callback_active = task_handles_[relay_id] != nullptr;
    portEXIT_CRITICAL(&lifecycle_lock_);
    if (callback_active) {
        ESP_LOGE(TAG, "Task for relay %u already exists", relay_id);
        return false;
    }

    char task_name[16];
    snprintf(task_name, sizeof(task_name), "relay_task_%u", relay_id);

    TaskRunnerParam* param = new TaskRunnerParam{ context, this };
    xEventGroupClearBits(lifecycle_events_, startupCompleteBit(relay_id) |
                         startupSucceededBit(relay_id) |
                         managerCallbackExitedBit(relay_id));

    // Keep the lifecycle lock while FreeRTOS publishes the new handle so a
    // task scheduled on the other core cannot acknowledge exit first.
    portENTER_CRITICAL(&lifecycle_lock_);
    BaseType_t res = xTaskCreatePinnedToCore(
        taskEntryTrampoline,
        task_name,
        RELAY_TASK_STACK_SIZE,
        param,
        RELAY_TASK_PRIORITY,
        &task_handles_[relay_id],
        RELAY_TASK_CORE
    );
    if (res != pdPASS) task_handles_[relay_id] = nullptr;
    portEXIT_CRITICAL(&lifecycle_lock_);

    if (res != pdPASS) {
        delete param;
        ESP_LOGE(TAG, "xTaskCreatePinnedToCore failed for relay %u", relay_id);
        return false;
    }
    return true;
#else
    task_alive_[relay_id] = true;
    startup_complete_[relay_id] = true;
    startup_succeeded_[relay_id] = true;
    return true;
#endif
}

bool FreeRTOSTaskRunner::requestStop(uint8_t relay_id) {
    if (relay_id >= TOTAL_RELAYS) return false;

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    portENTER_CRITICAL(&lifecycle_lock_);
    const bool callback_active = task_handles_[relay_id] != nullptr;
    portEXIT_CRITICAL(&lifecycle_lock_);
    if (callback_active) {
        // Cooperative stop is acknowledged by ScheduleManager before the
        // runner releases the execution context. Never force-delete here.
        return true;
    }
    return true;
#else
    task_alive_[relay_id] = false;
    return true;
#endif
}

bool FreeRTOSTaskRunner::waitUntilStarted(uint8_t relay_id, uint32_t timeout_ms) {
    if (relay_id >= TOTAL_RELAYS) return false;
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (lifecycle_events_ == nullptr) return false;
    const EventBits_t complete_bit = startupCompleteBit(relay_id);
    const EventBits_t succeeded_bit = startupSucceededBit(relay_id);
    const EventBits_t bits = xEventGroupWaitBits(
        lifecycle_events_, complete_bit, pdFALSE, pdTRUE,
        pdMS_TO_TICKS(timeout_ms));
    return (bits & (complete_bit | succeeded_bit)) == (complete_bit | succeeded_bit);
#else
    (void)timeout_ms;
    return startup_complete_[relay_id] && startup_succeeded_[relay_id];
#endif
}

void FreeRTOSTaskRunner::notifyStarted(uint8_t relay_id, bool succeeded) {
    if (relay_id >= TOTAL_RELAYS) return;
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (lifecycle_events_ == nullptr) return;
    EventBits_t bits = startupCompleteBit(relay_id);
    if (succeeded) bits |= startupSucceededBit(relay_id);
    xEventGroupSetBits(lifecycle_events_, bits);
#else
    startup_succeeded_[relay_id] = succeeded;
    startup_complete_[relay_id] = true;
#endif
}

bool FreeRTOSTaskRunner::waitUntilManagerCallbackExited(uint8_t relay_id, uint32_t timeout_ms) {
    if (relay_id >= TOTAL_RELAYS) return false;
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (lifecycle_events_ == nullptr) return false;
    const EventBits_t callback_exited_bit = managerCallbackExitedBit(relay_id);
    const EventBits_t bits = xEventGroupWaitBits(
        lifecycle_events_, callback_exited_bit, pdFALSE, pdTRUE,
        pdMS_TO_TICKS(timeout_ms));
    return (bits & callback_exited_bit) != 0;
#else
    (void)timeout_ms;
    return !task_alive_[relay_id];
#endif
}

void FreeRTOSTaskRunner::notifyManagerCallbackExited(uint8_t relay_id) {
    if (relay_id >= TOTAL_RELAYS) return;
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    // Protected handle access and EventGroup publication establish the same
    // callback-exit protocol used by all lifecycle methods. This does not
    // claim that vTaskDelete() has completed.
    portENTER_CRITICAL(&lifecycle_lock_);
    task_handles_[relay_id] = nullptr;
    portEXIT_CRITICAL(&lifecycle_lock_);
    if (lifecycle_events_ != nullptr) {
        xEventGroupSetBits(lifecycle_events_, managerCallbackExitedBit(relay_id));
    }
#else
    task_alive_[relay_id] = false;
#endif
}

bool FreeRTOSTaskRunner::isManagerCallbackActive(uint8_t relay_id) const {
    if (relay_id >= TOTAL_RELAYS) return false;

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    portENTER_CRITICAL(&lifecycle_lock_);
    const bool callback_active = task_handles_[relay_id] != nullptr;
    portEXIT_CRITICAL(&lifecycle_lock_);
    return callback_active;
#else
    return task_alive_[relay_id];
#endif
}
