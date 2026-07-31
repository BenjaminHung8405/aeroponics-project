#pragma once

#include "core/ITaskRunner.h"

#if defined(ESP_PLATFORM) || defined(ARDUINO)
#include <freertos/event_groups.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/portmacro.h>
#endif

class FreeRTOSTaskRunner : public ITaskRunner {
public:
    FreeRTOSTaskRunner();
    ~FreeRTOSTaskRunner() override;

    bool startTask(uint8_t relay_id, const RelayTaskContext& context) override;
    bool requestStop(uint8_t relay_id) override;
    bool waitUntilStarted(uint8_t relay_id, uint32_t timeout_ms) override;
    void notifyStarted(uint8_t relay_id, bool succeeded) override;
    bool waitUntilManagerCallbackExited(uint8_t relay_id, uint32_t timeout_ms) override;
    void notifyManagerCallbackExited(uint8_t relay_id) override;
    bool isManagerCallbackActive(uint8_t relay_id) const override;

private:
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    TaskHandle_t task_handles_[TOTAL_RELAYS];
    EventGroupHandle_t lifecycle_events_;
    mutable portMUX_TYPE lifecycle_lock_;
    static constexpr EventBits_t startupCompleteBit(uint8_t relay_id) { return static_cast<EventBits_t>(1u << relay_id); }
    static constexpr EventBits_t startupSucceededBit(uint8_t relay_id) { return static_cast<EventBits_t>(1u << (8u + relay_id)); }
    static constexpr EventBits_t managerCallbackExitedBit(uint8_t relay_id) { return static_cast<EventBits_t>(1u << (16u + relay_id)); }
    static void taskEntryTrampoline(void* param);
#else
    bool task_alive_[TOTAL_RELAYS];
    bool startup_complete_[TOTAL_RELAYS];
    bool startup_succeeded_[TOTAL_RELAYS];
#endif
};
