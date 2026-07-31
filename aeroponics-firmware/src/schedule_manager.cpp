#include "schedule_manager.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include <cstdio>
#include <cstring>

static const char *TAG = "SCHEDULE_MANAGER";

struct TaskParam {
    ScheduleManager* instance;
    uint8_t relay_id;
};

static TaskParam s_task_params[TOTAL_RELAYS];

ScheduleManager::ScheduleManager()
    : nvs_(nullptr),
      rtc_(nullptr),
      relay_(nullptr),
      profile_mutex_(nullptr),
      state_mutex_(nullptr),
      is_initialized_(false) {
    for (uint8_t i = 0; i < TOTAL_RELAYS; i++) {
        profiles_[i] = RelayProfile{
            DEFAULT_SPRAY_DAY_S,
            DEFAULT_COOLDOWN_DAY_S,
            DEFAULT_SPRAY_NIGHT_S,
            DEFAULT_COOLDOWN_NIGHT_S
        };
        runtime_states_[i] = RelayRuntimeState{
            PHASE_COOLING_DOWN,
            0,
            profiles_[i],
            false
        };
        task_handles_[i] = nullptr;
        wdt_registered_[i] = false;
    }
}

ScheduleManager::~ScheduleManager() {
    for (uint8_t i = 0; i < TOTAL_RELAYS; i++) {
        if (task_handles_[i] != nullptr) {
            vTaskDelete(task_handles_[i]);
            task_handles_[i] = nullptr;
        }
    }

    if (profile_mutex_ != nullptr) {
        vSemaphoreDelete(profile_mutex_);
        profile_mutex_ = nullptr;
    }

    if (state_mutex_ != nullptr) {
        vSemaphoreDelete(state_mutex_);
        state_mutex_ = nullptr;
    }
}

void ScheduleManager::loadInitialProfiles(RelayProfile profile_snapshot[TOTAL_RELAYS]) {
    if (xSemaphoreTake(profile_mutex_, pdMS_TO_TICKS(1000)) == pdTRUE) {
        bool load_ok = nvs_->loadAllProfiles(profiles_);
        if (!load_ok) {
            ESP_LOGW(TAG, "Failed to load profiles from NVS. Fallback defaults will be used.");
        }
        for (uint8_t i = 0; i < TOTAL_RELAYS; i++) {
            profile_snapshot[i] = profiles_[i];
        }
        xSemaphoreGive(profile_mutex_);
    } else {
        ESP_LOGE(TAG, "Timeout taking profile_mutex_ during begin(). Using default profile snapshot.");
        for (uint8_t i = 0; i < TOTAL_RELAYS; i++) {
            profile_snapshot[i] = RelayProfile{
                DEFAULT_SPRAY_DAY_S, DEFAULT_COOLDOWN_DAY_S,
                DEFAULT_SPRAY_NIGHT_S, DEFAULT_COOLDOWN_NIGHT_S
            };
        }
    }
}

bool ScheduleManager::begin(NvsStorage* nvs, RtcManager* rtc, RelayController* relay) {
    if (nvs == nullptr || rtc == nullptr || relay == nullptr) {
        ESP_LOGE(TAG, "Failed to initialize ScheduleManager: null dependency pointer provided.");
        return false;
    }

    nvs_ = nvs;
    rtc_ = rtc;
    relay_ = relay;

    profile_mutex_ = xSemaphoreCreateMutex();
    state_mutex_ = xSemaphoreCreateMutex();
    if (profile_mutex_ == nullptr || state_mutex_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create FreeRTOS mutexes in ScheduleManager::begin()");
        if (profile_mutex_) { vSemaphoreDelete(profile_mutex_); profile_mutex_ = nullptr; }
        if (state_mutex_) { vSemaphoreDelete(state_mutex_); state_mutex_ = nullptr; }
        return false;
    }

    RelayProfile profile_snapshot[TOTAL_RELAYS];
    loadInitialProfiles(profile_snapshot);

    if (xSemaphoreTake(state_mutex_, pdMS_TO_TICKS(1000)) == pdTRUE) {
        for (uint8_t i = 0; i < TOTAL_RELAYS; i++) {
            runtime_states_[i].phase = PHASE_COOLING_DOWN;
            runtime_states_[i].phase_remaining_s = 0;
            runtime_states_[i].current_profile = profile_snapshot[i];
            runtime_states_[i].is_night_mode = false;
        }
        xSemaphoreGive(state_mutex_);
    } else {
        ESP_LOGE(TAG, "Timeout taking state_mutex_ during ScheduleManager::begin()");
    }

    is_initialized_ = true;
    ESP_LOGI(TAG, "ScheduleManager initialized successfully with mutex protection.");
    return true;
}

bool ScheduleManager::startAllTasks() {
    if (!is_initialized_) {
        ESP_LOGE(TAG, "Cannot start tasks: ScheduleManager is not initialized.");
        return false;
    }

    for (uint8_t i = 0; i < TOTAL_RELAYS; i++) {
        s_task_params[i].instance = this;
        s_task_params[i].relay_id = i;

        char task_name[16];
        snprintf(task_name, sizeof(task_name), "relay_task_%u", i);

        BaseType_t ret = xTaskCreatePinnedToCore(
            relayTaskWrapper,
            task_name,
            RELAY_TASK_STACK_SIZE,
            &s_task_params[i],
            RELAY_TASK_PRIORITY,
            &task_handles_[i],
            RELAY_TASK_CORE
        );

        if (ret != pdPASS) {
            ESP_LOGE(TAG, "Failed to create FreeRTOS task %s (err: %d). Rolling back all created tasks...", task_name, (int)ret);
            for (uint8_t j = 0; j < i; j++) {
                if (task_handles_[j] != nullptr) {
                    vTaskDelete(task_handles_[j]);
                    task_handles_[j] = nullptr;
                }
            }
            task_handles_[i] = nullptr;
            if (relay_ != nullptr) {
                for (uint8_t j = 0; j < TOTAL_RELAYS; j++) {
                    relay_->setRelay(j, RELAY_OFF);
                }
            }
            return false;
        }
        ESP_LOGI(TAG, "Created FreeRTOS task %s pinned to CORE_%d", task_name, (int)RELAY_TASK_CORE);
    }

    return true;
}

bool ScheduleManager::updateProfile(uint8_t relay_id, const RelayProfile &profile) {
    if (!is_initialized_ || relay_id >= TOTAL_RELAYS || profile_mutex_ == nullptr) {
        ESP_LOGE(TAG, "updateProfile failed: invalid relay_id (%u) or ScheduleManager not initialized.", relay_id);
        return false;
    }

    if (xSemaphoreTake(profile_mutex_, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire profile_mutex_ for relay %u update within timeout.", relay_id);
        return false;
    }

    bool nvs_saved = nvs_->saveProfile(relay_id, profile);
    if (!nvs_saved) {
        ESP_LOGE(TAG, "Failed to persist profile for relay %u to NVS. RAM update aborted for consistency.", relay_id);
        xSemaphoreGive(profile_mutex_);
        return false;
    }

    profiles_[relay_id] = profile;
    xSemaphoreGive(profile_mutex_);

    ESP_LOGI(TAG, "Relay %u profile updated in NVS and RAM successfully.", relay_id);
    return true;
}

RelayRuntimeState ScheduleManager::getRuntimeState(uint8_t relay_id) const {
    RelayRuntimeState state = {};
    if (!is_initialized_ || relay_id >= TOTAL_RELAYS) {
        return state;
    }

    if (xSemaphoreTake(state_mutex_, pdMS_TO_TICKS(1000)) == pdTRUE) {
        state = runtime_states_[relay_id];
        xSemaphoreGive(state_mutex_);
    } else {
        ESP_LOGW(TAG, "getRuntimeState timeout taking state_mutex_ for relay %u", relay_id);
    }

    return state;
}

void ScheduleManager::relayTaskWrapper(void* parameter) {
    TaskParam* param = static_cast<TaskParam*>(parameter);
    if (param != nullptr && param->instance != nullptr) {
        param->instance->relayTaskLoop(param->relay_id);
    }
    vTaskDelete(NULL);
}

bool ScheduleManager::fetchProfileSafely(uint8_t relay_id, RelayProfile &out_profile) {
    if (xSemaphoreTake(profile_mutex_, pdMS_TO_TICKS(1000)) == pdTRUE) {
        out_profile = profiles_[relay_id];
        xSemaphoreGive(profile_mutex_);
        return true;
    }
    ESP_LOGE(TAG, "Failed to acquire profile_mutex_ for relay %u task within timeout.", relay_id);
    return false;
}

bool ScheduleManager::executePhase(uint8_t relay_id, SchedulePhase phase, uint32_t duration_s, RelayState pin_state, const RelayProfile& profile, bool is_night) {
    if (xSemaphoreTake(state_mutex_, pdMS_TO_TICKS(1000)) == pdTRUE) {
        runtime_states_[relay_id].phase = phase;
        runtime_states_[relay_id].phase_remaining_s = duration_s;
        runtime_states_[relay_id].current_profile = profile;
        runtime_states_[relay_id].is_night_mode = is_night;
        xSemaphoreGive(state_mutex_);
    } else {
        ESP_LOGE(TAG, "Timeout taking state_mutex_ at start of phase for relay %u", relay_id);
    }

    for (uint32_t rem = duration_s; rem > 0; rem--) {
        if (!wdt_registered_[relay_id]) {
            ESP_LOGE(TAG, "Relay task %u not registered with WDT during executePhase! Forcing relay OFF.", relay_id);
            if (relay_ != nullptr) {
                relay_->forceRelayOffEmergency(relay_id);
            }
            return false;
        }

        esp_err_t reset_err = esp_task_wdt_reset();
        if (reset_err != ESP_OK) {
            ESP_LOGE(TAG, "CRITICAL WDT FAILURE: esp_task_wdt_reset returned 0x%x for relay task %u! Latching safe-state.", reset_err, relay_id);
            if (relay_ != nullptr) {
                relay_->forceRelayOffEmergency(relay_id);
            }
            return false;
        }

        if (xSemaphoreTake(state_mutex_, pdMS_TO_TICKS(1000)) == pdTRUE) {
            runtime_states_[relay_id].phase_remaining_s = rem;
            xSemaphoreGive(state_mutex_);
        }

        if (relay_ != nullptr) {
            bool applied = relay_->applyScheduledStateUnlessOverride(relay_id, pin_state);
            if (!applied) {
                ESP_LOGE(TAG, "Apply scheduled state failed for relay %u. Executing emergency RELAY_OFF fail-safe.", relay_id);
                relay_->forceRelayOffEmergency(relay_id);
                return false;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    return true;
}

void ScheduleManager::relayTaskLoop(uint8_t relay_id) {
    ESP_LOGI(TAG, "Relay task %u running on Core %d", relay_id, xPortGetCoreID());
    esp_err_t add_err = esp_task_wdt_add(NULL);
    if (add_err == ESP_OK || add_err == ESP_ERR_INVALID_STATE) {
        wdt_registered_[relay_id] = true;
        ESP_LOGI(TAG, "Relay task %u registered with Task WDT successfully.", relay_id);
    } else {
        wdt_registered_[relay_id] = false;
        ESP_LOGE(TAG, "esp_task_wdt_add failed for relay task %u: 0x%x. Forcing relay OFF and terminating task.", relay_id, add_err);
        if (relay_ != nullptr) {
            relay_->forceRelayOffEmergency(relay_id);
        }
        vTaskDelete(NULL);
        return;
    }

    while (true) {
        if (!wdt_registered_[relay_id]) {
            ESP_LOGE(TAG, "Relay task %u lost WDT registration! Forcing relay OFF and terminating task.", relay_id);
            if (relay_ != nullptr) {
                relay_->forceRelayOffEmergency(relay_id);
            }
            vTaskDelete(NULL);
            return;
        }

        esp_err_t reset_err = esp_task_wdt_reset();
        if (reset_err != ESP_OK) {
            ESP_LOGE(TAG, "CRITICAL WDT FAILURE: esp_task_wdt_reset returned 0x%x in relay task loop %u! Latching safe-state and terminating task.", reset_err, relay_id);
            if (relay_ != nullptr) {
                relay_->forceRelayOffEmergency(relay_id);
            }
            if (wdt_registered_[relay_id]) {
                esp_task_wdt_delete(NULL);
                wdt_registered_[relay_id] = false;
            }
            vTaskDelete(NULL);
            return;
        }

        RelayProfile current_profile = {
            DEFAULT_SPRAY_DAY_S, DEFAULT_COOLDOWN_DAY_S,
            DEFAULT_SPRAY_NIGHT_S, DEFAULT_COOLDOWN_NIGHT_S
        };

        if (!fetchProfileSafely(relay_id, current_profile)) {
            ESP_LOGE(TAG, "Safely turning off relay %u and retrying after 1s delay.", relay_id);
            if (relay_ != nullptr) {
                relay_->forceRelayOffEmergency(relay_id);
            }
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        bool is_night = (rtc_ != nullptr) ? rtc_->isNightMode() : false;
        uint32_t spray_s = is_night ? current_profile.spray_night_s : current_profile.spray_day_s;
        uint32_t cooldown_s = is_night ? current_profile.cooldown_night_s : current_profile.cooldown_day_s;

        bool spray_ok = executePhase(relay_id, PHASE_SPRAYING, spray_s, RELAY_ON, current_profile, is_night);
        if (!spray_ok) {
            ESP_LOGE(TAG, "Relay task %u failed during SPRAYING phase. Terminating task in FAULTED state.", relay_id);
            if (wdt_registered_[relay_id]) {
                esp_task_wdt_delete(NULL);
                wdt_registered_[relay_id] = false;
            }
            vTaskDelete(NULL);
            return;
        }

        bool cooldown_ok = executePhase(relay_id, PHASE_COOLING_DOWN, cooldown_s, RELAY_OFF, current_profile, is_night);
        if (!cooldown_ok) {
            ESP_LOGE(TAG, "Relay task %u failed during COOLING_DOWN phase. Terminating task in FAULTED state.", relay_id);
            if (wdt_registered_[relay_id]) {
                esp_task_wdt_delete(NULL);
                wdt_registered_[relay_id] = false;
            }
            vTaskDelete(NULL);
            return;
        }
    }
}

