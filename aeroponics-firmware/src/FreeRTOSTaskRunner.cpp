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
    for (uint8_t i = 0; i < TOTAL_RELAYS; ++i) {
        task_handles_[i] = nullptr;
        startup_complete_[i] = false;
        startup_succeeded_[i] = false;
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
        waitUntilStopped(i, 1000);
    }
}

#if defined(ESP_PLATFORM) || defined(ARDUINO)
void FreeRTOSTaskRunner::taskEntryTrampoline(void* param) {
    TaskRunnerParam* p = reinterpret_cast<TaskRunnerParam*>(param);
    RelayTaskContext context = p->context;
    FreeRTOSTaskRunner* runner = p->runner;
    delete p;

    if (context.manager != nullptr) {
        context.manager->runRelayTask(context.relay_id);
        // The manager callback has returned: no further access to its context.
        runner->notifyStopped(context.relay_id);
    }
}
#endif

bool FreeRTOSTaskRunner::startTask(uint8_t relay_id, const RelayTaskContext& context) {
    if (relay_id >= TOTAL_RELAYS || context.manager == nullptr || context.relay_id != relay_id) {
        return false;
    }

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (task_handles_[relay_id] != nullptr) {
        ESP_LOGE(TAG, "Task for relay %u already exists", relay_id);
        return false;
    }

    char task_name[16];
    snprintf(task_name, sizeof(task_name), "relay_task_%u", relay_id);

    TaskRunnerParam* param = new TaskRunnerParam{ context, this };
    startup_complete_[relay_id] = false;
    startup_succeeded_[relay_id] = false;

    BaseType_t res = xTaskCreatePinnedToCore(
        taskEntryTrampoline,
        task_name,
        RELAY_TASK_STACK_SIZE,
        param,
        RELAY_TASK_PRIORITY,
        &task_handles_[relay_id],
        RELAY_TASK_CORE
    );

    if (res != pdPASS) {
        delete param;
        task_handles_[relay_id] = nullptr;
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
    if (task_handles_[relay_id] != nullptr) {
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
    TickType_t start = xTaskGetTickCount();
    while (!startup_complete_[relay_id] &&
           (xTaskGetTickCount() - start) < pdMS_TO_TICKS(timeout_ms)) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return startup_complete_[relay_id] && startup_succeeded_[relay_id];
#else
    (void)timeout_ms;
    return startup_complete_[relay_id] && startup_succeeded_[relay_id];
#endif
}

void FreeRTOSTaskRunner::notifyStarted(uint8_t relay_id, bool succeeded) {
    if (relay_id >= TOTAL_RELAYS) return;
    startup_succeeded_[relay_id] = succeeded;
    startup_complete_[relay_id] = true;
}

bool FreeRTOSTaskRunner::waitUntilStopped(uint8_t relay_id, uint32_t timeout_ms) {
    if (relay_id >= TOTAL_RELAYS) return false;
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    TickType_t start = xTaskGetTickCount();
    while (task_handles_[relay_id] != nullptr &&
           (xTaskGetTickCount() - start) < pdMS_TO_TICKS(timeout_ms)) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return task_handles_[relay_id] == nullptr;
#else
    (void)timeout_ms;
    return !task_alive_[relay_id];
#endif
}

void FreeRTOSTaskRunner::notifyStopped(uint8_t relay_id) {
    if (relay_id >= TOTAL_RELAYS) return;
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    task_handles_[relay_id] = nullptr;
#else
    task_alive_[relay_id] = false;
#endif
}

bool FreeRTOSTaskRunner::isTaskAlive(uint8_t relay_id) const {
    if (relay_id >= TOTAL_RELAYS) return false;

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    return task_handles_[relay_id] != nullptr;
#else
    return task_alive_[relay_id];
#endif
}
