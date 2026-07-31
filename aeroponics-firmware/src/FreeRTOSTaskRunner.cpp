#include "FreeRTOSTaskRunner.h"
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
    uint8_t relay_id;
    void (*task_func)(uint8_t relay_id, void* arg);
    void* arg;
};

FreeRTOSTaskRunner::FreeRTOSTaskRunner() {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    for (uint8_t i = 0; i < TOTAL_RELAYS; ++i) {
        task_handles_[i] = nullptr;
    }
#else
    for (uint8_t i = 0; i < TOTAL_RELAYS; ++i) {
        task_alive_[i] = false;
    }
#endif
}

FreeRTOSTaskRunner::~FreeRTOSTaskRunner() {
    for (uint8_t i = 0; i < TOTAL_RELAYS; ++i) {
        stopTask(i);
    }
}

#if defined(ESP_PLATFORM) || defined(ARDUINO)
void FreeRTOSTaskRunner::taskEntryTrampoline(void* param) {
    TaskRunnerParam* p = reinterpret_cast<TaskRunnerParam*>(param);
    uint8_t relay_id = p->relay_id;
    void (*func)(uint8_t, void*) = p->task_func;
    void* arg = p->arg;
    delete p;

    if (func != nullptr) {
        func(relay_id, arg);
    }
}
#endif

bool FreeRTOSTaskRunner::startTask(uint8_t relay_id, void (*task_func)(uint8_t relay_id, void* arg), void* arg) {
    if (relay_id >= TOTAL_RELAYS || task_func == nullptr) {
        return false;
    }

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (task_handles_[relay_id] != nullptr) {
        ESP_LOGE(TAG, "Task for relay %u already exists", relay_id);
        return false;
    }

    char task_name[16];
    snprintf(task_name, sizeof(task_name), "relay_task_%u", relay_id);

    TaskRunnerParam* param = new TaskRunnerParam{ relay_id, task_func, arg };

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
    return true;
#endif
}

void FreeRTOSTaskRunner::stopTask(uint8_t relay_id) {
    if (relay_id >= TOTAL_RELAYS) return;

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (task_handles_[relay_id] != nullptr) {
        vTaskDelete(task_handles_[relay_id]);
        task_handles_[relay_id] = nullptr;
    }
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
