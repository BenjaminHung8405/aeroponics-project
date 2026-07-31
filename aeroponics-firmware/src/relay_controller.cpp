#include "relay_controller.h"
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

void RelayController::initPins() {
    if (mutex_ == nullptr) {
        mutex_ = xSemaphoreCreateMutex();
    }

    ESP_LOGI(TAG, "Initializing relay GPIO pins with hardware fail-safe sequence...");
    for (uint8_t i = 0; i < TOTAL_RELAYS; ++i) {
        fault_latched_[i].store(false);
        uint8_t pin = getPinForRelay(i);
        if (pin != 255) {
            // Rule S1-HW-01 (TUYỆT ĐỐI): digitalWrite(LOW) BẮT BUỘC đứng TRƯỚC pinMode(OUTPUT)
            // Cơ chế duy nhất ngăn relay bị kích lúc boot (glitch)
            digitalWrite(pin, LOW);
            pinMode(pin, OUTPUT);
            // Cập nhật trạng thái khởi tạo vào cache
            if (mutex_ != nullptr && xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
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

    bool is_latched = false;
    bool write_success = false;

    portENTER_CRITICAL(&spinlock_);
    is_latched = fault_latched_[relay_id].load();
    if (is_latched) {
        digitalWrite(pin, LOW);
        state_cache_[relay_id] = RELAY_OFF;
        write_success = false;
    } else {
        if (state == RELAY_ON) {
            digitalWrite(pin, HIGH);
        } else {
            digitalWrite(pin, LOW);
        }
        state_cache_[relay_id] = state;
        write_success = true;
    }
    portEXIT_CRITICAL(&spinlock_);

    if (is_latched) {
        if (state == RELAY_ON) {
            ESP_LOGE(TAG, "BLOCKED WRITE HIGH: Relay ID %u is FAULT LATCHED IN SAFE STATE! Write HIGH rejected.", relay_id);
        }
        return false;
    }

    ESP_LOGD(TAG, "Relay ID %u (GPIO %u) set to %s", relay_id, pin, state == RELAY_ON ? "ON" : "OFF");
    return write_success;
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
    if (fault_latched_[relay_id].load()) {
        return RELAY_OFF;
    }
    RelayState state = RELAY_OFF;
    if (mutex_ != nullptr && xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
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

    if (fault_latched_[relay_id].load()) {
        ESP_LOGE(TAG, "startManualOverride failed: Relay ID %u is latched in fault safe-state.", relay_id);
        return false;
    }

    // Kiểm tra và bảo vệ dải thời gian Override: [MIN_OVERRIDE_DURATION_S, MAX_OVERRIDE_DURATION_S] (1s - 3600s)
    if (duration_s < MIN_OVERRIDE_DURATION_S || duration_s > MAX_OVERRIDE_DURATION_S) {
        ESP_LOGE(TAG, "startManualOverride failed: duration_s %u out of valid range [%u, %u]",
                 duration_s, MIN_OVERRIDE_DURATION_S, MAX_OVERRIDE_DURATION_S);
        return false;
    }

    if (mutex_ != nullptr && xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
        TickType_t now = xTaskGetTickCount();
        TickType_t duration_ticks = pdMS_TO_TICKS(duration_s * 1000);
        override_state_[relay_id].active = true;
        override_state_[relay_id].remaining_s = duration_s;
        override_state_[relay_id].forced_state = forced_state;
        override_state_[relay_id].expires_at = now + duration_ticks;

        bool ok = setRelayLocked(relay_id, forced_state);
        xSemaphoreGive(mutex_);

        if (ok) {
            ESP_LOGI(TAG, "Manual override activated for Relay ID %u: forced_state=%s, duration=%u s",
                     relay_id, forced_state == RELAY_ON ? "ON" : "OFF", duration_s);
            return true;
        } else {
            ESP_LOGE(TAG, "startManualOverride failed to set relay hardware state for Relay ID %u", relay_id);
            return false;
        }
    } else {
        ESP_LOGE(TAG, "startManualOverride failed for Relay ID %u: could not acquire mutex_", relay_id);
        return false;
    }
}

bool RelayController::cancelOverride(uint8_t relay_id) {
    if (relay_id >= TOTAL_RELAYS) {
        ESP_LOGE(TAG, "cancelOverride failed: invalid relay_id %u", relay_id);
        return false;
    }

    if (mutex_ != nullptr && xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
        override_state_[relay_id].active = false;
        override_state_[relay_id].remaining_s = 0;
        override_state_[relay_id].expires_at = 0;
        xSemaphoreGive(mutex_);
        ESP_LOGI(TAG, "Manual override cancelled for Relay ID %u", relay_id);
        return true;
    } else {
        ESP_LOGE(TAG, "cancelOverride failed for Relay ID %u: could not acquire mutex_", relay_id);
        return false;
    }
}

bool RelayController::isOverrideActive(uint8_t relay_id) const {
    if (relay_id >= TOTAL_RELAYS) {
        return false;
    }
    bool active = false;
    if (mutex_ != nullptr && xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
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

    if (mutex_ != nullptr && xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
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

bool RelayController::applyScheduledStateUnlessOverride(uint8_t relay_id, RelayState scheduled_state) {
    if (relay_id >= TOTAL_RELAYS) {
        return false;
    }

    if (fault_latched_[relay_id].load()) {
        uint8_t pin = getPinForRelay(relay_id);
        if (pin != 255) {
            portENTER_CRITICAL(&spinlock_);
            digitalWrite(pin, LOW);
            state_cache_[relay_id] = RELAY_OFF;
            portEXIT_CRITICAL(&spinlock_);
        }
        ESP_LOGE(TAG, "applyScheduledStateUnlessOverride rejected: Relay ID %u latched in safe-state.", relay_id);
        return false;
    }

    if (mutex_ != nullptr && xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
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
        return true;
    } else {
        ESP_LOGE(TAG, "applyScheduledStateUnlessOverride failed for Relay ID %u: could not acquire mutex_", relay_id);
        return false;
    }
}

bool RelayController::forceRelayOffEmergency(uint8_t relay_id) {
    if (relay_id >= TOTAL_RELAYS) {
        return false;
    }

    uint8_t pin = getPinForRelay(relay_id);

    portENTER_CRITICAL(&spinlock_);
    fault_latched_[relay_id].store(true);
    if (pin != 255) {
        digitalWrite(pin, LOW);
    }
    state_cache_[relay_id] = RELAY_OFF;
    override_state_[relay_id].active = false;
    override_state_[relay_id].remaining_s = 0;
    portEXIT_CRITICAL(&spinlock_);

    ESP_LOGE(TAG, "EMERGENCY SAFE-STATE LATCH: Relay ID %u (GPIO %u) forced OFF & latched safe", relay_id, pin);

    if (mutex_ != nullptr && xSemaphoreTake(mutex_, 0) == pdTRUE) {
        xSemaphoreGive(mutex_);
    }
    return true;
}

bool RelayController::isFaultLatched(uint8_t relay_id) const {
    if (relay_id >= TOTAL_RELAYS) {
        return false;
    }
    return fault_latched_[relay_id].load();
}

void RelayController::resetFaultLatch(uint8_t relay_id) {
    if (relay_id < TOTAL_RELAYS) {
        portENTER_CRITICAL(&spinlock_);
        fault_latched_[relay_id].store(false);
        portEXIT_CRITICAL(&spinlock_);
        ESP_LOGI(TAG, "Fault safe-state latch reset for Relay ID %u", relay_id);
    }
}

struct FaultTestParam {
    RelayController* controller;
    uint8_t relay_id;
    SemaphoreHandle_t sem_start;
    SemaphoreHandle_t sem_done;
};

static void faultInjectionWriterTask(void* pvParameters) {
    FaultTestParam* param = static_cast<FaultTestParam*>(pvParameters);
    if (param != nullptr && param->controller != nullptr) {
        xSemaphoreTake(param->sem_start, portMAX_DELAY);
        for (uint32_t i = 0; i < 500; i++) {
            param->controller->setRelay(param->relay_id, RELAY_ON);
        }
        xSemaphoreGive(param->sem_done);
    }
    vTaskDelete(NULL);
}

bool RelayController::testFaultInjectionEmergency(uint8_t relay_id) {
    if (relay_id >= TOTAL_RELAYS) {
        return false;
    }

    ESP_LOGI(TAG, "[FAULT-INJECTION TEST] Starting 2-task multi-core concurrency emergency test on Relay %u...", relay_id);

    resetFaultLatch(relay_id);

    FaultTestParam param;
    param.controller = this;
    param.relay_id = relay_id;
    param.sem_start = xSemaphoreCreateBinary();
    param.sem_done = xSemaphoreCreateBinary();

    if (param.sem_start == nullptr || param.sem_done == nullptr) {
        ESP_LOGE(TAG, "[FAULT-INJECTION TEST] Failed to create test semaphores.");
        if (param.sem_start) vSemaphoreDelete(param.sem_start);
        if (param.sem_done) vSemaphoreDelete(param.sem_done);
        return false;
    }

    TaskHandle_t writer_task_handle = nullptr;
    BaseType_t task_created = xTaskCreatePinnedToCore(
        faultInjectionWriterTask,
        "fault_writer",
        4096,
        &param,
        configMAX_PRIORITIES - 1,
        &writer_task_handle,
        1
    );

    if (task_created != pdPASS) {
        ESP_LOGE(TAG, "[FAULT-INJECTION TEST] Failed to create fault_writer task.");
        vSemaphoreDelete(param.sem_start);
        vSemaphoreDelete(param.sem_done);
        return false;
    }

    xSemaphoreGive(param.sem_start);
    forceRelayOffEmergency(relay_id);

    bool done = (xSemaphoreTake(param.sem_done, pdMS_TO_TICKS(2000)) == pdTRUE);

    vSemaphoreDelete(param.sem_start);
    vSemaphoreDelete(param.sem_done);

    if (!done) {
        ESP_LOGE(TAG, "[FAULT-INJECTION TEST] Writer task timed out!");
        if (writer_task_handle != nullptr) vTaskDelete(writer_task_handle);
        return false;
    }

    uint8_t pin = getPinForRelay(relay_id);
    int pin_val = digitalRead(pin);
    RelayState cached_val = getRelayState(relay_id);
    bool latched = isFaultLatched(relay_id);
    bool post_latch_write_attempt = setRelay(relay_id, RELAY_ON);

    bool pass = (pin_val == LOW) && (cached_val == RELAY_OFF) && latched && (!post_latch_write_attempt);

    if (pass) {
        ESP_LOGI(TAG, "[FAULT-INJECTION TEST] PASS: Concurrency test verified! GPIO=LOW, Cache=OFF, Latched=YES, Subsequent Write HIGH rejected.");
        resetFaultLatch(relay_id);
    } else {
        ESP_LOGE(TAG, "[FAULT-INJECTION TEST] FAIL: GPIO=%d, Cache=%d, Latched=%d, PostWrite=%d",
                 pin_val, cached_val, latched, post_latch_write_attempt);
    }

    return pass;
}

RelayOverrideState RelayController::getOverrideState(uint8_t relay_id) const {
    RelayOverrideState state{ false, 0, RELAY_OFF, 0 };
    if (relay_id >= TOTAL_RELAYS) {
        return state;
    }
    if (mutex_ != nullptr && xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
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
