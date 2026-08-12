#pragma once

#include <atomic>
#include "legacy_relay_config.h"
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
    bool consumeStopRequest(uint8_t relay_id, uint32_t generation) override;
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
        bool stop_requested;
    };

    LifecycleRecord lifecycle_[TOTAL_RELAYS];
    EventGroupHandle_t lifecycle_events_;
    mutable SemaphoreHandle_t lifecycle_mutex_;
    // The relay task reads this generation-bound signal without taking the
    // lifecycle mutex, so its watchdog feed can never wait on lifecycle I/O.
    std::atomic<uint32_t> stop_generation_[TOTAL_RELAYS];
    static constexpr EventBits_t startupCompleteBit(uint8_t relay_id) { return static_cast<EventBits_t>(1u << relay_id); }
    static constexpr EventBits_t startupSucceededBit(uint8_t relay_id) { return static_cast<EventBits_t>(1u << (8u + relay_id)); }
    static constexpr EventBits_t managerCallbackExitedBit(uint8_t relay_id) { return static_cast<EventBits_t>(1u << (16u + relay_id)); }
    bool prepareLifecycle(uint8_t relay_id, RelayTaskContext& context);
    void rollbackLifecycle(uint8_t relay_id, uint32_t generation);
    bool publishCreatedTask(uint8_t relay_id, uint32_t generation, TaskHandle_t handle);
    bool isCurrentGeneration(uint8_t relay_id, uint32_t generation) const;
    static void taskEntryTrampoline(void* param);
#else
    bool task_alive_[TOTAL_RELAYS];
    bool stop_requested_[TOTAL_RELAYS];
    bool startup_complete_[TOTAL_RELAYS];
    bool startup_succeeded_[TOTAL_RELAYS];
#endif
};
