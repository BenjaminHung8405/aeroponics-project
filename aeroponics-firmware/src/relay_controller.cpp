#include "relay_controller.h"

#if defined(ESP_PLATFORM) || defined(ARDUINO)

#include "esp_log.h"
#include <Arduino.h>

static const char *TAG = "RELAY_CONTROLLER";

RelayController::RelayController() : mutex_(nullptr), spinlock_(portMUX_INITIALIZER_UNLOCKED) {
    for (uint8_t i = 0; i < TOTAL_RELAYS; ++i) {
        state_cache_[i] = RELAY_OFF;
        override_state_[i] = RelayOverrideState{ false, 0, RELAY_OFF, 0 };
        fault_latched_[i].store(false);
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

uint8_t RelayController::getPinForRelay(uint8_t relay_id) const {
    switch (relay_id) {
        case 0: return RELAY_PIN_1;
        case 1: return RELAY_PIN_2;
        case 2: return RELAY_PIN_3;
        case 3: return RELAY_PIN_4;
        default: return 255;
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
            portENTER_CRITICAL(&spinlock_);
            fault_latched_[i].store(false);
            
            // Rule S1-HW-01 (HARD REQUIREMENT): digitalWrite(LOW) MUST precede pinMode(OUTPUT)
            digitalWrite(pin, LOW);
            pinMode(pin, OUTPUT);

            state_cache_[i] = RELAY_OFF;
            override_state_[i] = RelayOverrideState{ false, 0, RELAY_OFF, 0 };
            portEXIT_CRITICAL(&spinlock_);

            ESP_LOGI(TAG, "Relay ID %u (GPIO %u) initialized: LOW -> OUTPUT (State: OFF)", i, pin);
        } else {
            ESP_LOGE(TAG, "Invalid pin mapping for relay ID %u during initPins()", i);
        }
    }
}

bool RelayController::validateRelayPin(uint8_t relay_id, uint8_t &out_pin) const {
    if (relay_id >= TOTAL_RELAYS) {
        ESP_LOGE(TAG, "setRelay failed: invalid relay_id %u (must be < %u)", relay_id, TOTAL_RELAYS);
        return false;
    }
    out_pin = getPinForRelay(relay_id);
    if (out_pin == 255) {
        ESP_LOGE(TAG, "setRelay failed: invalid GPIO pin mapping for relay_id %u", relay_id);
        return false;
    }
    return true;
}

void RelayController::applySafeLatchedStateLocked(uint8_t relay_id, uint8_t pin) {
    if (pin != 255) {
        digitalWrite(pin, LOW);
    }
    state_cache_[relay_id] = RELAY_OFF;
    override_state_[relay_id].active = false;
}

void RelayController::applyRelayOutputLocked(uint8_t relay_id, uint8_t pin, RelayState state) {
    if (pin != 255) {
        if (state == RELAY_ON) {
            digitalWrite(pin, HIGH);
        } else {
            digitalWrite(pin, LOW);
        }
    }
    state_cache_[relay_id] = state;
}

bool RelayController::setRelayLocked(uint8_t relay_id, RelayState state) {
    uint8_t pin = 255;
    if (!validateRelayPin(relay_id, pin)) {
        return false;
    }

    bool is_latched = false;
    portENTER_CRITICAL(&spinlock_);
    is_latched = fault_latched_[relay_id].load();
    if (is_latched) {
        applySafeLatchedStateLocked(relay_id, pin);
    } else {
        applyRelayOutputLocked(relay_id, pin, state);
    }
    portEXIT_CRITICAL(&spinlock_);

    if (is_latched) {
        if (state == RELAY_ON) {
            ESP_LOGE(TAG, "BLOCKED WRITE HIGH: Relay ID %u is FAULT LATCHED IN SAFE STATE! Write HIGH rejected.", relay_id);
        }
        return false;
    }

    ESP_LOGD(TAG, "Relay ID %u (GPIO %u) set to %s", relay_id, pin, state == RELAY_ON ? "ON" : "OFF");
    return true;
}

bool RelayController::setRelay(uint8_t relay_id, RelayState state) {
    if (relay_id >= TOTAL_RELAYS) {
        return false;
    }
    if (fault_latched_[relay_id].load()) {
        ESP_LOGE(TAG, "setRelay failed: Relay ID %u is latched in fault safe-state.", relay_id);
        return false;
    }
    bool result = false;
    if (mutex_ != nullptr && xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
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
    portENTER_CRITICAL(&spinlock_);
    if (fault_latched_[relay_id].load()) {
        state = RELAY_OFF;
    } else {
        state = state_cache_[relay_id];
    }
    portEXIT_CRITICAL(&spinlock_);
    return state;
}

bool RelayController::startManualOverride(uint8_t relay_id, RelayState forced_state, uint32_t duration_s) {
    if (relay_id >= TOTAL_RELAYS) {
        ESP_LOGE(TAG, "startManualOverride failed: invalid relay_id %u", relay_id);
        return false;
    }

    if (fault_latched_[relay_id].load()) {
        ESP_LOGE(TAG, "startManualOverride failed: Relay ID %u is latched in fault safe-state.", relay_id);
        return false;
    }

    if (duration_s < MIN_OVERRIDE_DURATION_S || duration_s > MAX_OVERRIDE_DURATION_S) {
        ESP_LOGE(TAG, "startManualOverride failed: duration_s %u out of valid range [%u, %u]",
                 duration_s, MIN_OVERRIDE_DURATION_S, MAX_OVERRIDE_DURATION_S);
        return false;
    }

    if (mutex_ != nullptr && xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
        TickType_t now = xTaskGetTickCount();
        TickType_t duration_ticks = pdMS_TO_TICKS(duration_s * 1000);

        portENTER_CRITICAL(&spinlock_);
        if (fault_latched_[relay_id].load()) {
            portEXIT_CRITICAL(&spinlock_);
            xSemaphoreGive(mutex_);
            return false;
        }
        override_state_[relay_id].active = true;
        override_state_[relay_id].remaining_s = duration_s;
        override_state_[relay_id].forced_state = forced_state;
        override_state_[relay_id].expires_at = now + duration_ticks;
        portEXIT_CRITICAL(&spinlock_);

        bool ok = setRelayLocked(relay_id, forced_state);
        xSemaphoreGive(mutex_);

        if (ok) {
            ESP_LOGI(TAG, "Manual override started for relay ID %u: state=%s, duration=%u s",
                     relay_id, forced_state == RELAY_ON ? "ON" : "OFF", duration_s);
        }
        return ok;
    }

    ESP_LOGE(TAG, "startManualOverride failed: could not acquire mutex_");
    return false;
}

bool RelayController::cancelOverride(uint8_t relay_id) {
    if (relay_id >= TOTAL_RELAYS) {
        ESP_LOGE(TAG, "cancelOverride failed: invalid relay_id %u", relay_id);
        return false;
    }

    if (mutex_ != nullptr && xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
        portENTER_CRITICAL(&spinlock_);
        bool was_active = override_state_[relay_id].active;
        override_state_[relay_id].active = false;
        override_state_[relay_id].remaining_s = 0;
        override_state_[relay_id].expires_at = 0;
        portEXIT_CRITICAL(&spinlock_);

        xSemaphoreGive(mutex_);
        if (was_active) {
            ESP_LOGI(TAG, "Manual override cancelled for relay ID %u", relay_id);
        }
        return true;
    }

    ESP_LOGE(TAG, "cancelOverride failed: could not acquire mutex_");
    return false;
}

bool RelayController::isOverrideActive(uint8_t relay_id) const {
    if (relay_id >= TOTAL_RELAYS) {
        return false;
    }
    bool active = false;
    portENTER_CRITICAL(&spinlock_);
    active = override_state_[relay_id].active;
    portEXIT_CRITICAL(&spinlock_);
    return active;
}

void RelayController::tickOverride(uint8_t relay_id) {
    if (relay_id >= TOTAL_RELAYS) {
        return;
    }

    portENTER_CRITICAL(&spinlock_);
    if (override_state_[relay_id].active) {
        if (override_state_[relay_id].remaining_s > 0) {
            override_state_[relay_id].remaining_s--;
        }
        if (override_state_[relay_id].remaining_s == 0) {
            override_state_[relay_id].active = false;
            ESP_LOGI(TAG, "Manual override expired for relay ID %u. Returning to schedule.", relay_id);
        }
    }
    portEXIT_CRITICAL(&spinlock_);
}

bool RelayController::applyScheduledStateUnlessOverride(uint8_t relay_id, RelayState scheduled_state) {
    if (relay_id >= TOTAL_RELAYS) {
        return false;
    }

    if (mutex_ != nullptr && xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
        bool override_active = false;
        RelayState target_state = scheduled_state;

        portENTER_CRITICAL(&spinlock_);
        if (override_state_[relay_id].active) {
            if (override_state_[relay_id].remaining_s > 0) {
                override_state_[relay_id].remaining_s--;
            }
            if (override_state_[relay_id].remaining_s > 0) {
                override_active = true;
                target_state = override_state_[relay_id].forced_state;
            } else {
                override_state_[relay_id].active = false;
                override_active = false;
                target_state = scheduled_state;
                ESP_LOGI(TAG, "Manual override expired on tick for relay ID %u -> Restoring scheduled state %s",
                         relay_id, scheduled_state == RELAY_ON ? "ON" : "OFF");
            }
        }
        portEXIT_CRITICAL(&spinlock_);

        bool result = setRelayLocked(relay_id, target_state);
        xSemaphoreGive(mutex_);
        return result;
    }

    ESP_LOGE(TAG, "applyScheduledStateUnlessOverride failed: mutex_ timeout for relay ID %u", relay_id);
    return false;
}

bool RelayController::forceRelayOffEmergency(uint8_t relay_id) {
    if (relay_id >= TOTAL_RELAYS) {
        ESP_LOGE(TAG, "forceRelayOffEmergency failed: invalid relay_id %u", relay_id);
        return false;
    }

    uint8_t pin = getPinForRelay(relay_id);
    fault_latched_[relay_id].store(true);

    portENTER_CRITICAL(&spinlock_);
    if (pin != 255) {
        digitalWrite(pin, LOW);
    }
    state_cache_[relay_id] = RELAY_OFF;
    override_state_[relay_id].active = false;
    override_state_[relay_id].remaining_s = 0;
    override_state_[relay_id].expires_at = 0;
    portEXIT_CRITICAL(&spinlock_);

    ESP_LOGE(TAG, "EMERGENCY SAFE-STATE LATCHED: Relay ID %u (GPIO %u) forced LOW!", relay_id, pin);
    return true;
}

bool RelayController::isFaultLatched(uint8_t relay_id) const {
    if (relay_id >= TOTAL_RELAYS) {
        return true;
    }
    return fault_latched_[relay_id].load();
}

RelayOverrideState RelayController::getOverrideState(uint8_t relay_id) const {
    RelayOverrideState res = { false, 0, RELAY_OFF, 0 };
    if (relay_id >= TOTAL_RELAYS) {
        return res;
    }
    portENTER_CRITICAL(&spinlock_);
    res = override_state_[relay_id];
    portEXIT_CRITICAL(&spinlock_);
    return res;
}

#endif // ESP_PLATFORM || ARDUINO
