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

bool ScheduleManager::begin(NvsStorage* nvs, RtcManager* rtc, RelayController* relay) {
    if (nvs == nullptr || rtc == nullptr || relay == nullptr) {
        ESP_LOGE(TAG, "Failed to initialize ScheduleManager: null dependency pointer provided.");
        return false;
    }

    nvs_ = nvs;
    rtc_ = rtc;
    relay_ = relay;

    profile_mutex_ = xSemaphoreCreateMutex();
    if (profile_mutex_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create profile_mutex_");
        return false;
    }

    state_mutex_ = xSemaphoreCreateMutex();
    if (state_mutex_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create state_mutex_");
        vSemaphoreDelete(profile_mutex_);
        profile_mutex_ = nullptr;
        return false;
    }

    // Load initial profiles from NVS into RAM under profile_mutex_ guard (Rule S1-MUTEX-05)
    RelayProfile profile_snapshot[TOTAL_RELAYS];
    if (xSemaphoreTake(profile_mutex_, portMAX_DELAY) == pdTRUE) {
        bool load_ok = nvs_->loadAllProfiles(profiles_);
        if (!load_ok) {
            ESP_LOGW(TAG, "Failed to load profiles from NVS. Fallback defaults will be used.");
        }
        for (uint8_t i = 0; i < TOTAL_RELAYS; i++) {
            profile_snapshot[i] = profiles_[i];
        }
        xSemaphoreGive(profile_mutex_);
    } else {
        for (uint8_t i = 0; i < TOTAL_RELAYS; i++) {
            profile_snapshot[i] = RelayProfile{
                DEFAULT_SPRAY_DAY_S,
                DEFAULT_COOLDOWN_DAY_S,
                DEFAULT_SPRAY_NIGHT_S,
                DEFAULT_COOLDOWN_NIGHT_S
            };
        }
    }

    // Initialize runtime state snapshots under state_mutex_ guard using local snapshot
    if (xSemaphoreTake(state_mutex_, portMAX_DELAY) == pdTRUE) {
        for (uint8_t i = 0; i < TOTAL_RELAYS; i++) {
            runtime_states_[i].phase = PHASE_COOLING_DOWN;
            runtime_states_[i].phase_remaining_s = 0;
            runtime_states_[i].current_profile = profile_snapshot[i];
            runtime_states_[i].is_night_mode = false;
        }
        xSemaphoreGive(state_mutex_);
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
            ESP_LOGE(TAG, "Failed to create FreeRTOS task %s (error code: %d)", task_name, (int)ret);
            return false;
        }
        ESP_LOGI(TAG, "Created FreeRTOS task %s pinned to CORE_%d (stack: %u, priority: %u)",
                 task_name, (int)RELAY_TASK_CORE, (unsigned)RELAY_TASK_STACK_SIZE, (unsigned)RELAY_TASK_PRIORITY);
    }

    return true;
}

bool ScheduleManager::updateProfile(uint8_t relay_id, const RelayProfile &profile) {
    if (!is_initialized_ || relay_id >= TOTAL_RELAYS || profile_mutex_ == nullptr) {
        ESP_LOGE(TAG, "updateProfile failed: invalid relay_id (%u) or ScheduleManager not initialized.", relay_id);
        return false;
    }

    // Acquire profile_mutex_ before persisting/updating (Atomic update pattern)
    if (xSemaphoreTake(profile_mutex_, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire profile_mutex_ for relay %u update.", relay_id);
        return false;
    }

    // Persist to NVS first under profile_mutex_ guard (Rule S1-NVS-02: only called on actual config change)
    bool nvs_saved = nvs_->saveProfile(relay_id, profile);
    if (!nvs_saved) {
        ESP_LOGE(TAG, "Failed to persist profile for relay %u to NVS. RAM update aborted for consistency.", relay_id);
        xSemaphoreGive(profile_mutex_);
        return false;
    }

    // Update RAM under profile_mutex_ guard after NVS save succeeds
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

    if (xSemaphoreTake(state_mutex_, portMAX_DELAY) == pdTRUE) {
        state = runtime_states_[relay_id];
        xSemaphoreGive(state_mutex_);
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

void ScheduleManager::relayTaskLoop(uint8_t relay_id) {
    ESP_LOGI(TAG, "Relay task %u running on Core %d", relay_id, xPortGetCoreID());
    esp_task_wdt_add(NULL);

    while (true) {
        // Rule S1-WDT-06 (CỨNG): esp_task_wdt_reset() MUST be the very first call inside task loop iteration
        esp_task_wdt_reset();

        // --- 1. Fetch current profile (Thread-safe, Rule S1-MUTEX-05) ---
        // Initialize current_profile with safe default values before attempting lock
        RelayProfile current_profile = {
            DEFAULT_SPRAY_DAY_S,
            DEFAULT_COOLDOWN_DAY_S,
            DEFAULT_SPRAY_NIGHT_S,
            DEFAULT_COOLDOWN_NIGHT_S
        };

        bool profile_ok = false;
        if (xSemaphoreTake(profile_mutex_, pdMS_TO_TICKS(1000)) == pdTRUE) {
            current_profile = profiles_[relay_id];
            xSemaphoreGive(profile_mutex_);
            profile_ok = true;
        }

        if (!profile_ok) {
            ESP_LOGE(TAG, "Failed to acquire profile_mutex_ for relay %u task. Safely turning off relay and retrying.", relay_id);
            if (relay_ != nullptr) {
                relay_->setRelay(relay_id, RELAY_OFF);
            }
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        // --- 2. Determine Day / Night mode ---
        bool is_night = (rtc_ != nullptr) ? rtc_->isNightMode() : false;
        uint32_t spray_s = is_night ? current_profile.spray_night_s : current_profile.spray_day_s;
        uint32_t cooldown_s = is_night ? current_profile.cooldown_night_s : current_profile.cooldown_day_s;

        // --- 3. PHASE_SPRAYING ---
        if (xSemaphoreTake(state_mutex_, portMAX_DELAY) == pdTRUE) {
            runtime_states_[relay_id].phase = PHASE_SPRAYING;
            runtime_states_[relay_id].phase_remaining_s = spray_s;
            runtime_states_[relay_id].current_profile = current_profile;
            runtime_states_[relay_id].is_night_mode = is_night;
            xSemaphoreGive(state_mutex_);
        }

        for (uint32_t rem = spray_s; rem > 0; rem--) {
            // Rule S1-WDT-06 (CỨNG): esp_task_wdt_reset() MUST be first in iteration
            esp_task_wdt_reset();

            if (xSemaphoreTake(state_mutex_, portMAX_DELAY) == pdTRUE) {
                runtime_states_[relay_id].phase_remaining_s = rem;
                xSemaphoreGive(state_mutex_);
            }

            if (relay_ != nullptr) {
                relay_->applyScheduledStateUnlessOverride(relay_id, RELAY_ON);
            }

            vTaskDelay(pdMS_TO_TICKS(1000));
        }

        // --- 4. PHASE_COOLING_DOWN ---
        if (xSemaphoreTake(state_mutex_, portMAX_DELAY) == pdTRUE) {
            runtime_states_[relay_id].phase = PHASE_COOLING_DOWN;
            runtime_states_[relay_id].phase_remaining_s = cooldown_s;
            runtime_states_[relay_id].current_profile = current_profile;
            runtime_states_[relay_id].is_night_mode = is_night;
            xSemaphoreGive(state_mutex_);
        }

        for (uint32_t rem = cooldown_s; rem > 0; rem--) {
            // Rule S1-WDT-06 (CỨNG): esp_task_wdt_reset() MUST be first in iteration
            esp_task_wdt_reset();

            if (xSemaphoreTake(state_mutex_, portMAX_DELAY) == pdTRUE) {
                runtime_states_[relay_id].phase_remaining_s = rem;
                xSemaphoreGive(state_mutex_);
            }

            if (relay_ != nullptr) {
                relay_->applyScheduledStateUnlessOverride(relay_id, RELAY_OFF);
            }

            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}
