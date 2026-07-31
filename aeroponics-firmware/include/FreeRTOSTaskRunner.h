#pragma once

#include "core/ITaskRunner.h"

#if defined(ESP_PLATFORM) || defined(ARDUINO)
#include <freertos/event_groups.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#endif

class FreeRTOSTaskRunner : public ITaskRunner {
public:
    FreeRTOSTaskRunner();
    ~FreeRTOSTaskRunner() override;

    bool startTask(uint8_t relay_id, const RelayTaskContext& context) override;
    bool requestStop(uint8_t relay_id) override;
    bool waitUntilStarted(uint8_t relay_id, uint32_t timeout_ms) override;
    void notifyStarted(uint8_t relay_id, uint32_t generation, bool succeeded) override;
    bool waitUntilManagerCallbackExited(uint8_t relay_id, uint32_t timeout_ms) override;
    void notifyManagerCallbackExited(uint8_t relay_id, uint32_t generation) override;
    bool isManagerCallbackActive(uint8_t relay_id) const override;

private:
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    enum class LifecycleState : uint8_t { IDLE, CREATING, ACTIVE, EXITED };
    struct LifecycleRecord {
        LifecycleState state;
        uint32_t generation;
        TaskHandle_t task_handle;
    };

    LifecycleRecord lifecycle_[TOTAL_RELAYS];
    EventGroupHandle_t lifecycle_events_;
    mutable SemaphoreHandle_t lifecycle_mutex_;
    static constexpr EventBits_t startupCompleteBit(uint8_t relay_id) { return static_cast<EventBits_t>(1u << relay_id); }
    static constexpr EventBits_t startupSucceededBit(uint8_t relay_id) { return static_cast<EventBits_t>(1u << (8u + relay_id)); }
    static constexpr EventBits_t managerCallbackExitedBit(uint8_t relay_id) { return static_cast<EventBits_t>(1u << (16u + relay_id)); }
    bool prepareLifecycle(uint8_t relay_id, RelayTaskContext& context);
    bool publishCreatedTask(uint8_t relay_id, uint32_t generation, TaskHandle_t handle);
    bool isCurrentGeneration(uint8_t relay_id, uint32_t generation) const;
    static void taskEntryTrampoline(void* param);
#else
    bool task_alive_[TOTAL_RELAYS];
    bool startup_complete_[TOTAL_RELAYS];
    bool startup_succeeded_[TOTAL_RELAYS];
#endif
};
