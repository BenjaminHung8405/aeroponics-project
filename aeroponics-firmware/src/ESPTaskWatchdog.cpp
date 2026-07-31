#include "ESPTaskWatchdog.h"

#if defined(ESP_PLATFORM) || defined(ARDUINO)
#include <esp_log.h>
#include <esp_task_wdt.h>

static const char* TAG = "ESP_TASK_WATCHDOG";

bool ESPTaskWatchdog::registerWatchdog(uint8_t relay_id) {
    const esp_err_t error = esp_task_wdt_add(nullptr);
    if (error == ESP_OK) {
        return true;
    }
    ESP_LOGE(TAG, "WDT registration failed for relay task %u: 0x%x", relay_id, error);
    return false;
}

bool ESPTaskWatchdog::resetWatchdog(uint8_t relay_id) {
    const esp_err_t error = esp_task_wdt_reset();
    if (error == ESP_OK) {
        return true;
    }
    ESP_LOGE(TAG, "WDT reset failed for relay task %u: 0x%x", relay_id, error);
    return false;
}

bool ESPTaskWatchdog::deregisterWatchdog(uint8_t relay_id) {
    const esp_err_t error = esp_task_wdt_delete(nullptr);
    if (error == ESP_OK || error == ESP_ERR_NOT_FOUND) {
        return true;
    }
    ESP_LOGE(TAG, "WDT deregistration failed for relay task %u: 0x%x", relay_id, error);
    return false;
}
#else
bool ESPTaskWatchdog::registerWatchdog(uint8_t) { return false; }
bool ESPTaskWatchdog::resetWatchdog(uint8_t) { return false; }
bool ESPTaskWatchdog::deregisterWatchdog(uint8_t) { return false; }
#endif
