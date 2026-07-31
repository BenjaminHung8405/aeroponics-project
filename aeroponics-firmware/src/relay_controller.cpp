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
        uint8_t pin = getPinForRelay(i);
        if (pin != 255) {
            // Rule S1-HW-01: digitalWrite(LOW) MUST precede pinMode(OUTPUT)
            portENTER_CRITICAL(&spinlock_);
            fault_latched_[i].store(false);
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

#ifdef ENABLE_FAULT_INJECTION_TEST
static std::atomic<bool> s_fault_test_hook_active(false);
static uint8_t s_fault_test_relay_id = 0;
static SemaphoreHandle_t s_sem_writer_at_prewrite = nullptr;
static SemaphoreHandle_t s_sem_allow_writer_continue = nullptr;
#endif

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
    digitalWrite(pin, LOW);
    state_cache_[relay_id] = RELAY_OFF;
    override_state_[relay_id].active = false;
}

void RelayController::applyRelayOutputLocked(uint8_t relay_id, uint8_t pin, RelayState state) {
    if (state == RELAY_ON) {
        digitalWrite(pin, HIGH);
    } else {
        digitalWrite(pin, LOW);
    }
    state_cache_[relay_id] = state;
}

bool RelayController::setRelayLocked(uint8_t relay_id, RelayState state) {
    uint8_t pin = 255;
    if (!validateRelayPin(relay_id, pin)) {
        return false;
    }

#ifdef ENABLE_FAULT_INJECTION_TEST
    if (s_fault_test_hook_active.load() && relay_id == s_fault_test_relay_id && state == RELAY_ON) {
        if (s_sem_writer_at_prewrite != nullptr) {
            xSemaphoreGive(s_sem_writer_at_prewrite);
        }
        if (s_sem_allow_writer_continue != nullptr) {
            xSemaphoreTake(s_sem_allow_writer_continue, pdMS_TO_TICKS(2000));
        }
    }
#endif

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
        portENTER_CRITICAL(&spinlock_);
        override_state_[relay_id].active = false;
        override_state_[relay_id].remaining_s = 0;
        override_state_[relay_id].expires_at = 0;
        portEXIT_CRITICAL(&spinlock_);
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
    portENTER_CRITICAL(&spinlock_);
    if (override_state_[relay_id].active) {
        TickType_t now = xTaskGetTickCount();
        int32_t diff = (int32_t)(override_state_[relay_id].expires_at - now);
        if (diff > 0) {
            active = true;
        }
    }
    portEXIT_CRITICAL(&spinlock_);
    return active;
}

void RelayController::tickOverride(uint8_t relay_id) {
    if (relay_id >= TOTAL_RELAYS) {
        return;
    }

    portENTER_CRITICAL(&spinlock_);
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
    portEXIT_CRITICAL(&spinlock_);
}

bool RelayController::applyScheduledStateUnlessOverride(uint8_t relay_id, RelayState scheduled_state) {
    if (relay_id >= TOTAL_RELAYS) {
        return false;
    }

    if (mutex_ != nullptr && xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
        RelayState target_state = scheduled_state;
        portENTER_CRITICAL(&spinlock_);
        if (fault_latched_[relay_id].load()) {
            portEXIT_CRITICAL(&spinlock_);
            xSemaphoreGive(mutex_);
            ESP_LOGE(TAG, "applyScheduledStateUnlessOverride rejected: Relay ID %u latched in safe-state.", relay_id);
            return false;
        }
        if (override_state_[relay_id].active) {
            TickType_t now = xTaskGetTickCount();
            int32_t diff = (int32_t)(override_state_[relay_id].expires_at - now);
            if (diff <= 0) {
                override_state_[relay_id].active = false;
                override_state_[relay_id].remaining_s = 0;
                target_state = scheduled_state;
            } else {
                uint32_t rem_ms = pdTICKS_TO_MS((uint32_t)diff);
                override_state_[relay_id].remaining_s = (rem_ms + 999) / 1000;
                target_state = override_state_[relay_id].forced_state;
            }
        }
        portEXIT_CRITICAL(&spinlock_);

        bool applied = setRelayLocked(relay_id, target_state);
        xSemaphoreGive(mutex_);
        return applied;
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
    override_state_[relay_id].expires_at = 0;
    override_state_[relay_id].forced_state = RELAY_OFF;
    portEXIT_CRITICAL(&spinlock_);

    ESP_LOGE(TAG, "EMERGENCY SAFE-STATE LATCH: Relay ID %u (GPIO %u) forced OFF & latched safe", relay_id, pin);
    return true;
}

bool RelayController::isFaultLatched(uint8_t relay_id) const {
    if (relay_id >= TOTAL_RELAYS) {
        return false;
    }
    return fault_latched_[relay_id].load();
}

#ifdef ENABLE_FAULT_INJECTION_TEST
void RelayController::resetFaultLatch(uint8_t relay_id) {
    if (relay_id < TOTAL_RELAYS) {
        uint8_t pin = getPinForRelay(relay_id);
        portENTER_CRITICAL(&spinlock_);
        if (pin != 255) {
            digitalWrite(pin, LOW);
        }
        state_cache_[relay_id] = RELAY_OFF;
        override_state_[relay_id] = RelayOverrideState{ false, 0, RELAY_OFF, 0 };
        fault_latched_[relay_id].store(false);
        portEXIT_CRITICAL(&spinlock_);
        ESP_LOGI(TAG, "Fault safe-state latch reset for Relay ID %u", relay_id);
    }
}

struct FaultTestParam {
    RelayController* controller;
    uint8_t relay_id;
    SemaphoreHandle_t sem_done;
    std::atomic<uint32_t> write_attempts_after_latch;
    std::atomic<uint32_t> write_successes_after_latch;
};

static void faultInjectionWriterTask(void* pvParameters) {
    FaultTestParam* param = static_cast<FaultTestParam*>(pvParameters);
    if (param != nullptr && param->controller != nullptr) {
        for (uint32_t i = 0; i < 100; i++) {
            param->write_attempts_after_latch++;
            bool res = param->controller->setRelay(param->relay_id, RELAY_ON);
            if (res) {
                param->write_successes_after_latch++;
            }
        }
        if (param->sem_done != nullptr) {
            xSemaphoreGive(param->sem_done);
        }
    }
    vTaskDelete(NULL);
}

bool RelayController::createFaultTestResources(FaultTestParam &param) {
    s_sem_writer_at_prewrite = xSemaphoreCreateBinary();
    s_sem_allow_writer_continue = xSemaphoreCreateBinary();
    param.sem_done = xSemaphoreCreateBinary();
    param.write_attempts_after_latch.store(0);
    param.write_successes_after_latch.store(0);

    if (s_sem_writer_at_prewrite == nullptr || s_sem_allow_writer_continue == nullptr || param.sem_done == nullptr) {
        ESP_LOGE(TAG, "[FAULT-INJECTION TEST] Failed to create test semaphores.");
        cleanupFaultTestResources(param);
        return false;
    }
    s_fault_test_relay_id = param.relay_id;
    s_fault_test_hook_active.store(true);
    return true;
}

void RelayController::cleanupFaultTestResources(FaultTestParam &param) {
    s_fault_test_hook_active.store(false);
    if (s_sem_writer_at_prewrite != nullptr) {
        vSemaphoreDelete(s_sem_writer_at_prewrite);
        s_sem_writer_at_prewrite = nullptr;
    }
    if (s_sem_allow_writer_continue != nullptr) {
        vSemaphoreDelete(s_sem_allow_writer_continue);
        s_sem_allow_writer_continue = nullptr;
    }
    if (param.sem_done != nullptr) {
        vSemaphoreDelete(param.sem_done);
        param.sem_done = nullptr;
    }
}

bool RelayController::startFaultWriterTask(FaultTestParam &param, TaskHandle_t &writer_handle) {
    BaseType_t task_created = xTaskCreatePinnedToCore(
        faultInjectionWriterTask,
        "fault_writer",
        4096,
        &param,
        configMAX_PRIORITIES - 1,
        &writer_handle,
        1
    );

    if (task_created != pdPASS) {
        ESP_LOGE(TAG, "[FAULT-INJECTION TEST] Failed to create fault_writer task.");
        return false;
    }
    return true;
}

bool RelayController::waitForFaultWriterCompletion(FaultTestParam &param, TaskHandle_t writer_handle) {
    if (xSemaphoreTake(s_sem_writer_at_prewrite, pdMS_TO_TICKS(2000)) != pdTRUE) {
        ESP_LOGE(TAG, "[FAULT-INJECTION TEST] Timed out waiting for writer_at_prewrite barrier!");
        if (writer_handle != nullptr) vTaskDelete(writer_handle);
        return false;
    }

    forceRelayOffEmergency(param.relay_id);

    xSemaphoreGive(s_sem_allow_writer_continue);

    bool done = (xSemaphoreTake(param.sem_done, pdMS_TO_TICKS(2000)) == pdTRUE);
    if (!done) {
        ESP_LOGE(TAG, "[FAULT-INJECTION TEST] Writer task timed out waiting for sem_done!");
        if (writer_handle != nullptr) vTaskDelete(writer_handle);
        return false;
    }
    return true;
}

bool RelayController::verifyFaultSafeState(uint8_t relay_id, const FaultTestParam &param) {
    uint8_t pin = getPinForRelay(relay_id);
    int pin_val = digitalRead(pin);
    RelayState cached_val = getRelayState(relay_id);
    bool latched = isFaultLatched(relay_id);
    bool post_latch_write_attempt = setRelay(relay_id, RELAY_ON);

    uint32_t attempts = param.write_attempts_after_latch.load();
    uint32_t successes = param.write_successes_after_latch.load();

    bool pass = (pin_val == LOW) &&
                (cached_val == RELAY_OFF) &&
                latched &&
                (!post_latch_write_attempt) &&
                (attempts > 0) &&
                (successes == 0);

    if (pass) {
        ESP_LOGI(TAG, "[FAULT-INJECTION TEST] PASS: Pre-write barrier concurrency test verified! GPIO=LOW, Cache=OFF, Latched=YES, Attempts=%u, Successes=0", (unsigned)attempts);
        resetFaultLatch(relay_id);
    } else {
        ESP_LOGE(TAG, "[FAULT-INJECTION TEST] FAIL: GPIO=%d, Cache=%d, Latched=%d, PostWrite=%d, Attempts=%u, Successes=%u",
                 pin_val, cached_val, latched, post_latch_write_attempt, (unsigned)attempts, (unsigned)successes);
    }
    return pass;
}

bool RelayController::testFaultInjectionEmergency(uint8_t relay_id) {
    if (relay_id >= TOTAL_RELAYS) {
        return false;
    }

    ESP_LOGI(TAG, "[FAULT-INJECTION TEST] Starting pre-write barrier concurrency emergency test on Relay %u...", relay_id);
    resetFaultLatch(relay_id);

    FaultTestParam param;
    param.controller = this;
    param.relay_id = relay_id;

    if (!createFaultTestResources(param)) {
        return false;
    }

    TaskHandle_t writer_handle = nullptr;
    if (!startFaultWriterTask(param, writer_handle)) {
        cleanupFaultTestResources(param);
        return false;
    }

    bool completed = waitForFaultWriterCompletion(param, writer_handle);
    bool pass = false;
    if (completed) {
        pass = verifyFaultSafeState(relay_id, param);
    }

    cleanupFaultTestResources(param);
    return pass;
}
#endif

RelayOverrideState RelayController::getOverrideState(uint8_t relay_id) const {
    RelayOverrideState state{ false, 0, RELAY_OFF, 0 };
    if (relay_id >= TOTAL_RELAYS) {
        return state;
    }
    portENTER_CRITICAL(&spinlock_);
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
    portEXIT_CRITICAL(&spinlock_);
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
