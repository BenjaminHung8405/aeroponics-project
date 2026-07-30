#include "relay_controller.h"
#include "esp_log.h"
#include <Arduino.h>

static const char *TAG = "RELAY_CONTROLLER";

RelayController::RelayController() {
    for (uint8_t i = 0; i < TOTAL_RELAYS; ++i) {
        state_cache_[i] = RELAY_OFF;
        override_state_[i] = RelayOverrideState{ false, 0, RELAY_OFF };
    }
}

RelayController::~RelayController() {}

void RelayController::initPins() {
    ESP_LOGI(TAG, "Initializing relay GPIO pins with hardware fail-safe sequence...");
    for (uint8_t i = 0; i < TOTAL_RELAYS; ++i) {
        uint8_t pin = getPinForRelay(i);
        if (pin != 255) {
            // Rule S1-HW-01 (TUYỆT ĐỐI): digitalWrite(LOW) BẮT BUỘC đứng TRƯỚC pinMode(OUTPUT)
            // Cơ chế duy nhất ngăn relay bị kích lúc boot (glitch)
            digitalWrite(pin, LOW);
            pinMode(pin, OUTPUT);
            // Cập nhật trạng thái khởi tạo vào cache
            state_cache_[i] = RELAY_OFF;
            ESP_LOGI(TAG, "Relay ID %u (GPIO %u) initialized: LOW -> OUTPUT (State: OFF)", i, pin);
        } else {
            ESP_LOGE(TAG, "Invalid pin mapping for relay ID %u during initPins()", i);
        }
    }
}

bool RelayController::setRelay(uint8_t relay_id, RelayState state) {
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

RelayState RelayController::getRelayState(uint8_t relay_id) const {
    if (relay_id >= TOTAL_RELAYS) {
        ESP_LOGE(TAG, "getRelayState failed: invalid relay_id %u", relay_id);
        return RELAY_OFF;
    }
    return state_cache_[relay_id];
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

    override_state_[relay_id].active = true;
    override_state_[relay_id].remaining_s = duration_s;
    override_state_[relay_id].forced_state = forced_state;

    // Áp dụng ngay lập tức trạng thái cưỡng chế lên chân phần cứng
    bool result = setRelay(relay_id, forced_state);

    ESP_LOGI(TAG, "Manual override activated for Relay ID %u: forced_state=%s, duration=%u s",
             relay_id, forced_state == RELAY_ON ? "ON" : "OFF", duration_s);

    return result;
}

bool RelayController::cancelOverride(uint8_t relay_id) {
    if (relay_id >= TOTAL_RELAYS) {
        ESP_LOGE(TAG, "cancelOverride failed: invalid relay_id %u", relay_id);
        return false;
    }

    override_state_[relay_id].active = false;
    override_state_[relay_id].remaining_s = 0;
    ESP_LOGI(TAG, "Manual override cancelled for Relay ID %u", relay_id);
    return true;
}

bool RelayController::isOverrideActive(uint8_t relay_id) const {
    if (relay_id >= TOTAL_RELAYS) {
        return false;
    }
    return override_state_[relay_id].active;
}

void RelayController::tickOverride(uint8_t relay_id) {
    if (relay_id >= TOTAL_RELAYS) {
        return;
    }

    if (!override_state_[relay_id].active) {
        return;
    }

    if (override_state_[relay_id].remaining_s > 0) {
        override_state_[relay_id].remaining_s--;
        if (override_state_[relay_id].remaining_s == 0) {
            override_state_[relay_id].active = false;
            ESP_LOGI(TAG, "Manual override expired for Relay ID %u", relay_id);
        }
    }
}

RelayOverrideState RelayController::getOverrideState(uint8_t relay_id) const {
    if (relay_id >= TOTAL_RELAYS) {
        return RelayOverrideState{ false, 0, RELAY_OFF };
    }
    return override_state_[relay_id];
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
