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

    bool startTask(uint8_t relay_id, void (*task_func)(uint8_t relay_id, void* arg), void* arg) override;
    void stopTask(uint8_t relay_id) override;
    bool isTaskAlive(uint8_t relay_id) const override;

private:
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    TaskHandle_t task_handles_[TOTAL_RELAYS];
    static void taskEntryTrampoline(void* param);
#else
    bool task_alive_[TOTAL_RELAYS];
#endif
};
