#pragma once

#include "core/ITaskRunner.h"

#if defined(ESP_PLATFORM) || defined(ARDUINO)
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

class FreeRTOSTaskRunner : public ITaskRunner {
public:
    FreeRTOSTaskRunner();
    ~FreeRTOSTaskRunner() override;

    bool startTask(uint8_t relay_id, const RelayTaskContext& context) override;
    bool requestStop(uint8_t relay_id) override;
    bool waitUntilStarted(uint8_t relay_id, uint32_t timeout_ms) override;
    void notifyStarted(uint8_t relay_id, bool succeeded) override;
    bool waitUntilStopped(uint8_t relay_id, uint32_t timeout_ms) override;
    void notifyStopped(uint8_t relay_id) override;
    bool isTaskAlive(uint8_t relay_id) const override;

private:
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    TaskHandle_t task_handles_[TOTAL_RELAYS];
    volatile bool startup_complete_[TOTAL_RELAYS];
    volatile bool startup_succeeded_[TOTAL_RELAYS];
    static void taskEntryTrampoline(void* param);
#else
    bool task_alive_[TOTAL_RELAYS];
    bool startup_complete_[TOTAL_RELAYS];
    bool startup_succeeded_[TOTAL_RELAYS];
#endif
};
