#include "relay_controller.h"
#include "esp_log.h"
#include <Arduino.h>

static const char *TAG = "RELAY_CONTROLLER";

RelayController::RelayController() : mutex_(nullptr) {
    for (uint8_t i = 0; i < TOTAL_RELAYS; ++i) {
        state_cache_[i] = RELAY_OFF;
        override_state_[i] = RelayOverrideState{ false, 0, RELAY_OFF, 0 };
    }
    mutex_ = xSemaphoreCreateMutex();
    if (mutex_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create mutex_ in RelayController constructor");
    }
}

RelayController::~RelayController() {
    if (mutex_ != nullptr) {
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
    }
}

void RelayController::initPins() {
    if (mutex_ == nullptr) {
        mutex_ = xSemaphoreCreateMutex();
    }

    ESP_LOGI(TAG, "Initializing relay GPIO pins with hardware fail-safe sequence...");
    for (uint8_t i = 0; i < TOTAL_RELAYS; ++i) {
        uint8_t pin = getPinForRelay(i);
        if (pin != 255) {
            // Rule S1-HW-01 (TUYỆT ĐỐI): digitalWrite(LOW) BẮT BUỘC đứng TRƯỚC pinMode(OUTPUT)
            // Cơ chế duy nhất ngăn relay bị kích lúc boot (glitch)
            digitalWrite(pin, LOW);
            pinMode(pin, OUTPUT);
            // Cập nhật trạng thái khởi tạo vào cache
            if (mutex_ != nullptr && xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
                state_cache_[i] = RELAY_OFF;
                xSemaphoreGive(mutex_);
            } else {
                state_cache_[i] = RELAY_OFF;
            }
            ESP_LOGI(TAG, "Relay ID %u (GPIO %u) initialized: LOW -> OUTPUT (State: OFF)", i, pin);
        } else {
            ESP_LOGE(TAG, "Invalid pin mapping for relay ID %u during initPins()", i);
        }
    }
}

bool RelayController::setRelayLocked(uint8_t relay_id, RelayState state) {
    if (relay_id >= TOTAL_RELAYS) {
        ESP_LOGE(TAG, "setRelay failed: invalid relay_id %u (must be < %u)", relay_id, TOTAL_RELAYS);
        return false;
    }

    uint8_t pin = getPinForRelay(relay_id);
    if (pin == 255) {
        ESP_LOGE(TAG, "setRelay failed: invalid GPIO pin mapping for relay_id %u", relay_id);
        return false;
    }

    // Active HIGH Logic: RELAY_ON -> digitalWrite HIGH, RELAY_OFF -> digitalWrite LOW
    if (state == RELAY_ON) {
        digitalWrite(pin, HIGH); // Active HIGH ON: Đưa chân GPIO lên mức cao để bật Relay
    } else {
        digitalWrite(pin, LOW);  // Active HIGH OFF: Đưa chân GPIO xuống mức thấp để tắt Relay
    }

    // Cập nhật cache trạng thái relay
    state_cache_[relay_id] = state;
    ESP_LOGD(TAG, "Relay ID %u (GPIO %u) set to %s", relay_id, pin, state == RELAY_ON ? "ON" : "OFF");
    return true;
}

bool RelayController::setRelay(uint8_t relay_id, RelayState state) {
    if (relay_id >= TOTAL_RELAYS) {
        return false;
    }
    bool result = false;
    if (mutex_ != nullptr && xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
        result = setRelayLocked(relay_id, state);
        xSemaphoreGive(mutex_);
    } else {
        ESP_LOGE(TAG, "setRelay failed: could not acquire mutex_");
    }
    return result;
}

RelayState RelayController::getRelayState(uint8_t relay_id) const {
    if (relay_id >= TOTAL_RELAYS) {
        ESP_LOGE(TAG, "getRelayState failed: invalid relay_id %u", relay_id);
        return RELAY_OFF;
    }
    RelayState state = RELAY_OFF;
    if (mutex_ != nullptr && xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
        state = state_cache_[relay_id];
        xSemaphoreGive(mutex_);
    }
    return state;
}

bool RelayController::startManualOverride(uint8_t relay_id, RelayState forced_state, uint32_t duration_s) {
    if (relay_id >= TOTAL_RELAYS) {
        ESP_LOGE(TAG, "startManualOverride failed: invalid relay_id %u", relay_id);
        return false;
    }

    // Kiểm tra và bảo vệ dải thời gian Override: [MIN_OVERRIDE_DURATION_S, MAX_OVERRIDE_DURATION_S] (1s - 3600s)
    if (duration_s < MIN_OVERRIDE_DURATION_S || duration_s > MAX_OVERRIDE_DURATION_S) {
        ESP_LOGE(TAG, "startManualOverride failed: duration_s %u out of valid range [%u, %u]",
                 duration_s, MIN_OVERRIDE_DURATION_S, MAX_OVERRIDE_DURATION_S);
        return false;
    }

    bool result = false;
    if (mutex_ != nullptr && xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
        TickType_t now = xTaskGetTickCount();
        TickType_t duration_ticks = pdMS_TO_TICKS(duration_s * 1000);
        override_state_[relay_id].active = true;
        override_state_[relay_id].remaining_s = duration_s;
        override_state_[relay_id].forced_state = forced_state;
        override_state_[relay_id].expires_at = now + duration_ticks;

        result = setRelayLocked(relay_id, forced_state);
        xSemaphoreGive(mutex_);
    }

    ESP_LOGI(TAG, "Manual override activated for Relay ID %u: forced_state=%s, duration=%u s",
             relay_id, forced_state == RELAY_ON ? "ON" : "OFF", duration_s);

    return result;
}

bool RelayController::cancelOverride(uint8_t relay_id) {
    if (relay_id >= TOTAL_RELAYS) {
        ESP_LOGE(TAG, "cancelOverride failed: invalid relay_id %u", relay_id);
        return false;
    }

    if (mutex_ != nullptr && xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
        override_state_[relay_id].active = false;
        override_state_[relay_id].remaining_s = 0;
        override_state_[relay_id].expires_at = 0;
        xSemaphoreGive(mutex_);
    }

    ESP_LOGI(TAG, "Manual override cancelled for Relay ID %u", relay_id);
    return true;
}

bool RelayController::isOverrideActive(uint8_t relay_id) const {
    if (relay_id >= TOTAL_RELAYS) {
        return false;
    }
    bool active = false;
    if (mutex_ != nullptr && xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
        if (override_state_[relay_id].active) {
            TickType_t now = xTaskGetTickCount();
            int32_t diff = (int32_t)(override_state_[relay_id].expires_at - now);
            if (diff > 0) {
                active = true;
            }
        }
        xSemaphoreGive(mutex_);
    }
    return active;
}

void RelayController::tickOverride(uint8_t relay_id) {
    if (relay_id >= TOTAL_RELAYS) {
        return;
    }

    if (mutex_ != nullptr && xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
        if (override_state_[relay_id].active) {
            TickType_t now = xTaskGetTickCount();
            int32_t diff = (int32_t)(override_state_[relay_id].expires_at - now);
            if (diff <= 0) {
                override_state_[relay_id].active = false;
                override_state_[relay_id].remaining_s = 0;
                ESP_LOGI(TAG, "Manual override expired for Relay ID %u", relay_id);
            } else {
                uint32_t rem_ms = pdTICKS_TO_MS((uint32_t)diff);
                override_state_[relay_id].remaining_s = (rem_ms + 999) / 1000;
            }
        }
        xSemaphoreGive(mutex_);
    }
}

void RelayController::applyScheduledStateUnlessOverride(uint8_t relay_id, RelayState scheduled_state) {
    if (relay_id >= TOTAL_RELAYS) {
        return;
    }

    if (mutex_ != nullptr && xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
        if (override_state_[relay_id].active) {
            TickType_t now = xTaskGetTickCount();
            int32_t diff = (int32_t)(override_state_[relay_id].expires_at - now);
            if (diff <= 0) {
                override_state_[relay_id].active = false;
                override_state_[relay_id].remaining_s = 0;
                ESP_LOGI(TAG, "Manual override expired for Relay ID %u. Applying scheduled state: %s",
                         relay_id, scheduled_state == RELAY_ON ? "ON" : "OFF");
                setRelayLocked(relay_id, scheduled_state);
            } else {
                uint32_t rem_ms = pdTICKS_TO_MS((uint32_t)diff);
                override_state_[relay_id].remaining_s = (rem_ms + 999) / 1000;
                setRelayLocked(relay_id, override_state_[relay_id].forced_state);
            }
        } else {
            setRelayLocked(relay_id, scheduled_state);
        }
        xSemaphoreGive(mutex_);
    }
}

RelayOverrideState RelayController::getOverrideState(uint8_t relay_id) const {
    RelayOverrideState state{ false, 0, RELAY_OFF, 0 };
    if (relay_id >= TOTAL_RELAYS) {
        return state;
    }
    if (mutex_ != nullptr && xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
        state = override_state_[relay_id];
        if (state.active) {
            TickType_t now = xTaskGetTickCount();
            int32_t diff = (int32_t)(state.expires_at - now);
            if (diff <= 0) {
                state.active = false;
                state.remaining_s = 0;
            } else {
                uint32_t rem_ms = pdTICKS_TO_MS((uint32_t)diff);
                state.remaining_s = (rem_ms + 999) / 1000;
            }
        }
        xSemaphoreGive(mutex_);
    }
    return state;
}

uint8_t RelayController::getPinForRelay(uint8_t relay_id) const {
    switch (relay_id) {
        case 0: return RELAY_PIN_1;
        case 1: return RELAY_PIN_2;
        case 2: return RELAY_PIN_3;
        case 3: return RELAY_PIN_4;
        default: return 255;
    }
}
