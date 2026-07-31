#include "FreeRTOSTaskRunner.h"
#include "schedule_manager.h"
#include <cstdio>
#include <new>

#if defined(ESP_PLATFORM) || defined(ARDUINO)
#include <esp_log.h>
#include <esp_system.h>
static const char* TAG = "FREERTOS_TASK_RUNNER";
#else
#define TAG "FREERTOS_TASK_RUNNER"
#define ESP_LOGE(tag, fmt, ...) printf("[ERR][%s] " fmt "\n", tag, ##__VA_ARGS__)
#endif

struct TaskRunnerParam {
    RelayTaskContext context;
    FreeRTOSTaskRunner* runner;
};

FreeRTOSTaskRunner::FreeRTOSTaskRunner() {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    lifecycle_events_ = xEventGroupCreate();
    lifecycle_mutex_ = xSemaphoreCreateMutex();
    for (uint8_t i = 0; i < TOTAL_RELAYS; ++i) {
        lifecycle_[i] = LifecycleRecord{ LifecycleState::IDLE, 0, nullptr, false };
    }
#else
    for (uint8_t i = 0; i < TOTAL_RELAYS; ++i) {
        task_alive_[i] = false;
        stop_requested_[i] = false;
        startup_complete_[i] = false;
        startup_succeeded_[i] = false;
    }
#endif
}

FreeRTOSTaskRunner::~FreeRTOSTaskRunner() {
    bool callbacks_exited = true;
    for (uint8_t i = 0; i < TOTAL_RELAYS; ++i) {
        requestStop(i);
        if (isManagerCallbackActive(i) && !waitUntilManagerCallbackExited(i, 1000)) {
            callbacks_exited = false;
        }
    }
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (!callbacks_exited) {
        // The task trampoline can still call notifyManagerCallbackExited().
        // Keep the EventGroup/mutex alive and reboot instead of freeing them.
        ESP_LOGE(TAG, "Task runner teardown timed out; restarting before lifecycle resource destruction");
        esp_restart();
        for (;;) {
            vTaskDelay(portMAX_DELAY);
        }
    }
    if (lifecycle_events_ != nullptr) vEventGroupDelete(lifecycle_events_);
    if (lifecycle_mutex_ != nullptr) vSemaphoreDelete(lifecycle_mutex_);
#endif
}

#if defined(ESP_PLATFORM) || defined(ARDUINO)
void FreeRTOSTaskRunner::taskEntryTrampoline(void* param) {
    TaskRunnerParam* p = reinterpret_cast<TaskRunnerParam*>(param);
    const RelayTaskContext context = p->context;
    FreeRTOSTaskRunner* const runner = p->runner;
    delete p;

    if (context.manager != nullptr) {
        context.manager->runRelayTask(context.relay_id, context.generation);
        // This is a callback-exit acknowledgement, not a kernel-task join.
        runner->notifyManagerCallbackExited(context.relay_id, context.generation);
    }
    vTaskDelete(nullptr);
}

bool FreeRTOSTaskRunner::prepareLifecycle(uint8_t relay_id, RelayTaskContext& context) {
    if (lifecycle_mutex_ == nullptr || lifecycle_events_ == nullptr) return false;
    xSemaphoreTake(lifecycle_mutex_, portMAX_DELAY);
    LifecycleRecord& record = lifecycle_[relay_id];
    if (record.state == LifecycleState::CREATING || record.state == LifecycleState::ACTIVE) {
        xSemaphoreGive(lifecycle_mutex_);
        return false;
    }
    ++record.generation;
    if (record.generation == 0) ++record.generation;
    record.state = LifecycleState::CREATING;
    record.task_handle = nullptr;
    record.stop_requested = false;
    context.generation = record.generation;
    xSemaphoreGive(lifecycle_mutex_);

    xEventGroupClearBits(lifecycle_events_, startupCompleteBit(relay_id) |
                         startupSucceededBit(relay_id) |
                         managerCallbackExitedBit(relay_id));
    return true;
}

void FreeRTOSTaskRunner::rollbackLifecycle(uint8_t relay_id, uint32_t generation) {
    xSemaphoreTake(lifecycle_mutex_, portMAX_DELAY);
    LifecycleRecord& record = lifecycle_[relay_id];
    if (record.generation == generation && record.state == LifecycleState::CREATING) {
        record.state = LifecycleState::IDLE;
        record.task_handle = nullptr;
        record.stop_requested = false;
    }
    xSemaphoreGive(lifecycle_mutex_);
    xEventGroupClearBits(lifecycle_events_, startupCompleteBit(relay_id) |
                         startupSucceededBit(relay_id) |
                         managerCallbackExitedBit(relay_id));
}

bool FreeRTOSTaskRunner::publishCreatedTask(uint8_t relay_id, uint32_t generation, TaskHandle_t handle) {
    xSemaphoreTake(lifecycle_mutex_, portMAX_DELAY);
    LifecycleRecord& record = lifecycle_[relay_id];
    const bool created_is_current = record.generation == generation;
    bool active = false;
    bool stop_requested = false;
    if (created_is_current && record.state == LifecycleState::CREATING) {
        record.task_handle = handle;
        record.state = LifecycleState::ACTIVE;
        stop_requested = record.stop_requested;
        active = true;
    }
    // If the callback already exited, do not resurrect it by publishing a
    // stale handle. EXITED is the authoritative terminal state.
    xSemaphoreGive(lifecycle_mutex_);
    // A stop request can race task creation while no handle was available.
    // Deliver that already-recorded request as soon as the handle is published.
    if (active && stop_requested && handle != nullptr) xTaskNotifyGive(handle);
    return active;
}

bool FreeRTOSTaskRunner::isCurrentGeneration(uint8_t relay_id, uint32_t generation) const {
    if (lifecycle_mutex_ == nullptr) return false;
    xSemaphoreTake(lifecycle_mutex_, portMAX_DELAY);
    const bool current = lifecycle_[relay_id].generation == generation;
    xSemaphoreGive(lifecycle_mutex_);
    return current;
}
#endif

bool FreeRTOSTaskRunner::startTask(uint8_t relay_id, const RelayTaskContext& context) {
    if (relay_id >= TOTAL_RELAYS || context.manager == nullptr || context.relay_id != relay_id) return false;
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    RelayTaskContext task_context = context;
    if (!prepareLifecycle(relay_id, task_context)) {
        ESP_LOGE(TAG, "Task for relay %u already exists", relay_id);
        return false;
    }

    TaskRunnerParam* param = new (std::nothrow) TaskRunnerParam{ task_context, this };
    if (param == nullptr) {
        rollbackLifecycle(relay_id, task_context.generation);
        ESP_LOGE(TAG, "Unable to allocate task context for relay %u", relay_id);
        return false;
    }
    char task_name[16];
    snprintf(task_name, sizeof(task_name), "relay_task_%u", relay_id);
    TaskHandle_t handle = nullptr;
    // Allocation and scheduler operations deliberately occur outside every
    // portMUX/critical section. The lifecycle record is already CREATING.
    const BaseType_t result = xTaskCreatePinnedToCore(
        taskEntryTrampoline, task_name, RELAY_TASK_STACK_SIZE, param,
        RELAY_TASK_PRIORITY, &handle, RELAY_TASK_CORE);
    if (result != pdPASS) {
        delete param;
        rollbackLifecycle(relay_id, task_context.generation);
        ESP_LOGE(TAG, "xTaskCreatePinnedToCore failed for relay %u", relay_id);
        return false;
    }
    return publishCreatedTask(relay_id, task_context.generation, handle);
#else
    task_alive_[relay_id] = true;
    stop_requested_[relay_id] = false;
    startup_complete_[relay_id] = true;
    startup_succeeded_[relay_id] = true;
    return true;
#endif
}

bool FreeRTOSTaskRunner::requestStop(uint8_t relay_id) {
    if (relay_id >= TOTAL_RELAYS) return false;
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (lifecycle_mutex_ == nullptr) return false;
    TaskHandle_t handle = nullptr;
    xSemaphoreTake(lifecycle_mutex_, portMAX_DELAY);
    LifecycleRecord& record = lifecycle_[relay_id];
    if (record.state != LifecycleState::CREATING && record.state != LifecycleState::ACTIVE) {
        xSemaphoreGive(lifecycle_mutex_);
        return false;
    }
    record.stop_requested = true;
    handle = record.task_handle;
    xSemaphoreGive(lifecycle_mutex_);
    // Wake a task that is currently delayed; the generation-bound record also
    // preserves a request made before its handle was published.
    if (handle != nullptr) xTaskNotifyGive(handle);
    return true;
#else
    stop_requested_[relay_id] = true;
    task_alive_[relay_id] = false;
    return true;
#endif
}

bool FreeRTOSTaskRunner::consumeStopRequest(uint8_t relay_id, uint32_t generation) {
    if (relay_id >= TOTAL_RELAYS) return true;
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (lifecycle_mutex_ == nullptr) return true;
    xSemaphoreTake(lifecycle_mutex_, portMAX_DELAY);
    const LifecycleRecord& record = lifecycle_[relay_id];
    const bool requested = record.generation == generation && record.stop_requested;
    xSemaphoreGive(lifecycle_mutex_);
    // Clear a pending notification in the relay task. The lifecycle flag is
    // authoritative and remains set until this generation exits.
    (void)ulTaskNotifyTake(pdTRUE, 0);
    return requested;
#else
    (void)generation;
    return stop_requested_[relay_id];
#endif
}

bool FreeRTOSTaskRunner::waitUntilStarted(uint8_t relay_id, uint32_t timeout_ms) {
    if (relay_id >= TOTAL_RELAYS) return false;
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    const EventBits_t bits = xEventGroupWaitBits(lifecycle_events_, startupCompleteBit(relay_id),
        pdFALSE, pdTRUE, pdMS_TO_TICKS(timeout_ms));
    return (bits & (startupCompleteBit(relay_id) | startupSucceededBit(relay_id))) ==
           (startupCompleteBit(relay_id) | startupSucceededBit(relay_id));
#else
    (void)timeout_ms;
    return startup_complete_[relay_id] && startup_succeeded_[relay_id];
#endif
}

void FreeRTOSTaskRunner::notifyStarted(uint8_t relay_id, uint32_t generation, bool succeeded) {
    if (relay_id >= TOTAL_RELAYS) return;
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (!isCurrentGeneration(relay_id, generation)) return;
    EventBits_t bits = startupCompleteBit(relay_id);
    if (succeeded) bits |= startupSucceededBit(relay_id);
    xEventGroupSetBits(lifecycle_events_, bits);
#else
    (void)generation;
    startup_succeeded_[relay_id] = succeeded;
    startup_complete_[relay_id] = true;
#endif
}

bool FreeRTOSTaskRunner::waitUntilManagerCallbackExited(uint8_t relay_id, uint32_t timeout_ms) {
    if (relay_id >= TOTAL_RELAYS) return false;
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    const EventBits_t bits = xEventGroupWaitBits(lifecycle_events_, managerCallbackExitedBit(relay_id),
        pdFALSE, pdTRUE, pdMS_TO_TICKS(timeout_ms));
    return (bits & managerCallbackExitedBit(relay_id)) != 0;
#else
    (void)timeout_ms;
    return !task_alive_[relay_id];
#endif
}

void FreeRTOSTaskRunner::notifyManagerCallbackExited(uint8_t relay_id, uint32_t generation) {
    if (relay_id >= TOTAL_RELAYS) return;
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    xSemaphoreTake(lifecycle_mutex_, portMAX_DELAY);
    LifecycleRecord& record = lifecycle_[relay_id];
    const bool current = record.generation == generation;
    if (current) {
        record.task_handle = nullptr;
        record.state = LifecycleState::EXITED;
    }
    xSemaphoreGive(lifecycle_mutex_);
    if (current) xEventGroupSetBits(lifecycle_events_, managerCallbackExitedBit(relay_id));
#else
    (void)generation;
    task_alive_[relay_id] = false;
#endif
}

bool FreeRTOSTaskRunner::isManagerCallbackActive(uint8_t relay_id) const {
    if (relay_id >= TOTAL_RELAYS) return false;
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    xSemaphoreTake(lifecycle_mutex_, portMAX_DELAY);
    const LifecycleState state = lifecycle_[relay_id].state;
    xSemaphoreGive(lifecycle_mutex_);
    return state == LifecycleState::CREATING || state == LifecycleState::ACTIVE;
#else
    return task_alive_[relay_id];
#endif
}
