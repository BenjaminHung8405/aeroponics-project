#include "schedule_manager.h"
#include <cstdio>
#include <cstring>

#if defined(ESP_PLATFORM) || defined(ARDUINO)
#include <esp_log.h>
#include <esp_task_wdt.h>
static const char *TAG = "SCHEDULE_MANAGER";
#else
#define TAG "SCHEDULE_MANAGER"
#define ESP_LOGI(tag, fmt, ...) printf("[INFO][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) printf("[WARN][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGE(tag, fmt, ...) printf("[ERR][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...) printf("[DBG][%s] " fmt "\n", tag, ##__VA_ARGS__)
#endif

ScheduleManager::ScheduleManager()
    : nvs_(nullptr), rtc_(nullptr), relay_(nullptr), wdt_(nullptr), is_initialized_(false) {
    for (uint8_t i = 0; i < TOTAL_RELAYS; ++i) {
        profiles_[i] = RelayProfile{ DEFAULT_SPRAY_DAY_S, DEFAULT_COOLDOWN_DAY_S, DEFAULT_SPRAY_NIGHT_S, DEFAULT_COOLDOWN_NIGHT_S };
        runtime_states_[i] = RelayRuntimeState{ PHASE_SPRAYING, DEFAULT_SPRAY_DAY_S, profiles_[i], false };
#if defined(ESP_PLATFORM) || defined(ARDUINO)
        task_handles_[i] = nullptr;
        wdt_registered_[i] = false;
#endif
    }
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    profile_mutex_ = nullptr;
    state_mutex_ = nullptr;
#endif
}

ScheduleManager::~ScheduleManager() {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    for (uint8_t i = 0; i < TOTAL_RELAYS; ++i) {
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
#endif
}

bool ScheduleManager::begin(IProfileRepository* nvs, IClock* rtc, IRelayOutput* relay, IWatchdog* wdt) {
    if (nvs == nullptr || rtc == nullptr || relay == nullptr) {
        ESP_LOGE(TAG, "begin failed: null dependency passed (nvs=%p, rtc=%p, relay=%p)", (void*)nvs, (void*)rtc, (void*)relay);
        return false;
    }

    nvs_ = nvs;
    rtc_ = rtc;
    relay_ = relay;
    wdt_ = wdt;

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    profile_mutex_ = xSemaphoreCreateMutex();
    state_mutex_ = xSemaphoreCreateMutex();
    if (profile_mutex_ == nullptr || state_mutex_ == nullptr) {
        ESP_LOGE(TAG, "begin failed: could not create FreeRTOS mutexes");
        return false;
    }
#endif

    loadInitialProfiles(profiles_);
    bool is_night = rtc_->isNightMode();

    for (uint8_t i = 0; i < TOTAL_RELAYS; ++i) {
        uint32_t init_spray = is_night ? profiles_[i].spray_night_s : profiles_[i].spray_day_s;
        runtime_states_[i] = RelayRuntimeState{ PHASE_SPRAYING, init_spray, profiles_[i], is_night };
    }

    is_initialized_ = true;
    ESP_LOGI(TAG, "ScheduleManager initialized successfully via Dependency Injection.");
    return true;
}

void ScheduleManager::loadInitialProfiles(RelayProfile profile_snapshot[TOTAL_RELAYS]) {
    if (nvs_ != nullptr) {
        nvs_->loadAllProfiles(profile_snapshot);
    }
}

bool ScheduleManager::fetchProfileSafely(uint8_t relay_id, RelayProfile &out_profile) {
    if (relay_id >= TOTAL_RELAYS) {
        return false;
    }
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (profile_mutex_ != nullptr && xSemaphoreTake(profile_mutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
        out_profile = profiles_[relay_id];
        xSemaphoreGive(profile_mutex_);
        return true;
    }
    out_profile = profiles_[relay_id];
    return false;
#else
    out_profile = profiles_[relay_id];
    return true;
#endif
}

bool ScheduleManager::updateRuntimePhaseState(uint8_t relay_id, SchedulePhase phase, uint32_t remaining_s, const RelayProfile* profile, const bool* is_night) {
    if (relay_id >= TOTAL_RELAYS) {
        return false;
    }
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (state_mutex_ != nullptr && xSemaphoreTake(state_mutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
        runtime_states_[relay_id].phase = phase;
        runtime_states_[relay_id].phase_remaining_s = remaining_s;
        if (profile != nullptr) {
            runtime_states_[relay_id].current_profile = *profile;
        }
        if (is_night != nullptr) {
            runtime_states_[relay_id].is_night_mode = *is_night;
        }
        xSemaphoreGive(state_mutex_);
        return true;
    }
    return false;
#else
    runtime_states_[relay_id].phase = phase;
    runtime_states_[relay_id].phase_remaining_s = remaining_s;
    if (profile != nullptr) {
        runtime_states_[relay_id].current_profile = *profile;
    }
    if (is_night != nullptr) {
        runtime_states_[relay_id].is_night_mode = *is_night;
    }
    return true;
#endif
}

bool ScheduleManager::updateProfile(uint8_t relay_id, const RelayProfile &profile) {
    if (relay_id >= TOTAL_RELAYS || !is_initialized_) {
        ESP_LOGE(TAG, "updateProfile failed: invalid relay_id %u or uninitialized", relay_id);
        return false;
    }

    if (nvs_ != nullptr && !nvs_->saveProfile(relay_id, profile)) {
        ESP_LOGE(TAG, "updateProfile failed: repository save rejected for relay %u", relay_id);
        return false;
    }

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (profile_mutex_ != nullptr && xSemaphoreTake(profile_mutex_, pdMS_TO_TICKS(500)) == pdTRUE) {
        profiles_[relay_id] = profile;
        xSemaphoreGive(profile_mutex_);
        ESP_LOGI(TAG, "Updated RAM profile for relay ID %u successfully", relay_id);
        return true;
    }
    return false;
#else
    profiles_[relay_id] = profile;
    return true;
#endif
}

RelayRuntimeState ScheduleManager::getRuntimeState(uint8_t relay_id) const {
    RelayRuntimeState fallback = { PHASE_SPRAYING, 0, RelayProfile{0,0,0,0}, false };
    if (relay_id >= TOTAL_RELAYS) {
        return fallback;
    }
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (state_mutex_ != nullptr && xSemaphoreTake(state_mutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
        RelayRuntimeState state = runtime_states_[relay_id];
        xSemaphoreGive(state_mutex_);
        return state;
    }
    return runtime_states_[relay_id];
#else
    return runtime_states_[relay_id];
#endif
}

void ScheduleManager::stepRelayPhase(uint8_t relay_id) {
    if (relay_id >= TOTAL_RELAYS || !is_initialized_) {
        return;
    }

    RelayRuntimeState state = getRuntimeState(relay_id);
    bool night_mode = (rtc_ != nullptr) ? rtc_->isNightMode() : false;
    RelayProfile prof;
    fetchProfileSafely(relay_id, prof);

    if (state.phase_remaining_s > 0) {
        state.phase_remaining_s--;
    }

    RelayState target_scheduled_state = (state.phase == PHASE_SPRAYING) ? RELAY_ON : RELAY_OFF;

    if (relay_ != nullptr) {
        relay_->applyScheduledStateUnlessOverride(relay_id, target_scheduled_state);
    }

    if (state.phase_remaining_s == 0) {
        if (state.phase == PHASE_SPRAYING) {
            state.phase = PHASE_COOLING_DOWN;
            state.phase_remaining_s = night_mode ? prof.cooldown_night_s : prof.cooldown_day_s;
        } else {
            state.phase = PHASE_SPRAYING;
            state.phase_remaining_s = night_mode ? prof.spray_night_s : prof.spray_day_s;
        }
    }

    updateRuntimePhaseState(relay_id, state.phase, state.phase_remaining_s, &prof, &night_mode);
}

bool ScheduleManager::isTaskWdtRegistered(uint8_t relay_id) const {
    if (relay_id >= TOTAL_RELAYS) return false;
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    return wdt_registered_[relay_id];
#else
    return false;
#endif
}

bool ScheduleManager::isTaskAlive(uint8_t relay_id) const {
    if (relay_id >= TOTAL_RELAYS) return false;
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    return task_handles_[relay_id] != nullptr;
#else
    return false;
#endif
}

#if defined(ESP_PLATFORM) || defined(ARDUINO)

bool ScheduleManager::registerTaskWdt(uint8_t relay_id) {
    if (wdt_ != nullptr) {
        return wdt_->registerWatchdog(relay_id);
    }
    esp_err_t err = esp_task_wdt_add(NULL);
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
        wdt_registered_[relay_id] = true;
        return true;
    }
    ESP_LOGE(TAG, "esp_task_wdt_add failed for relay task %u: 0x%x", relay_id, err);
    return false;
}

bool ScheduleManager::resetTaskWdt(uint8_t relay_id) {
    if (wdt_ != nullptr) {
        return wdt_->resetWatchdog(relay_id);
    }
    esp_err_t err = esp_task_wdt_reset();
    if (err == ESP_OK) {
        return true;
    }
    ESP_LOGE(TAG, "esp_task_wdt_reset failed for relay task %u: 0x%x", relay_id, err);
    return false;
}

bool ScheduleManager::deregisterTaskWdt(uint8_t relay_id) {
    if (wdt_ != nullptr) {
        return wdt_->deregisterWatchdog(relay_id);
    }
    esp_err_t err = esp_task_wdt_delete(NULL);
    wdt_registered_[relay_id] = false;
    return (err == ESP_OK);
}

void ScheduleManager::handleTaskTermination(uint8_t relay_id, const char* reason) {
    ESP_LOGE(TAG, "Relay task %u terminating due to failure: %s", relay_id, reason);
    if (relay_ != nullptr) {
        relay_->forceRelayOffEmergency(relay_id);
    }
    if (wdt_registered_[relay_id]) {
        deregisterTaskWdt(relay_id);
    }
}

bool ScheduleManager::ensureTaskWatchdogHealthy(uint8_t relay_id) {
    if (!wdt_registered_[relay_id]) {
        if (!registerTaskWdt(relay_id)) {
            handleTaskTermination(relay_id, "Watchdog registration failed");
            return false;
        }
    }
    if (!resetTaskWdt(relay_id)) {
        handleTaskTermination(relay_id, "Watchdog feed failed");
        return false;
    }
    return true;
}

void ScheduleManager::relayTaskWrapper(void* parameter) {
    uint8_t relay_id = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(parameter));
    ScheduleManager* self = reinterpret_cast<ScheduleManager*>(parameter);
    // Correct instance pointer decoding:
    // If parameter passed is index, instance should be passed.
    // In startAllTasks, we pass struct or instance index.
}

void ScheduleManager::relayTaskLoop(uint8_t relay_id) {
    ESP_LOGI(TAG, "Relay Task %u started on CORE %d", relay_id, xPortGetCoreID());

    if (!ensureTaskWatchdogHealthy(relay_id)) {
        vTaskDelete(NULL);
        return;
    }

    while (true) {
        if (!ensureTaskWatchdogHealthy(relay_id)) {
            break;
        }

        stepRelayPhase(relay_id);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    handleTaskTermination(relay_id, "Loop exit");
    vTaskDelete(NULL);
}

struct RelayTaskParam {
    ScheduleManager* manager;
    uint8_t relay_id;
};

void ScheduleManager::relayTaskEntry(void* param) {
    RelayTaskParam* p = reinterpret_cast<RelayTaskParam*>(param);
    ScheduleManager* mgr = p->manager;
    uint8_t id = p->relay_id;
    delete p;
    mgr->relayTaskLoop(id);
}

bool ScheduleManager::startAllTasks() {
    if (!is_initialized_) {
        ESP_LOGE(TAG, "Cannot startAllTasks: ScheduleManager not initialized");
        return false;
    }

    bool all_ok = true;
    for (uint8_t i = 0; i < TOTAL_RELAYS; ++i) {
        char task_name[16];
        snprintf(task_name, sizeof(task_name), "relay_task_%u", i);

        RelayTaskParam* param = new RelayTaskParam{ this, i };
        BaseType_t res = xTaskCreatePinnedToCore(
            relayTaskEntry,
            task_name,
            RELAY_TASK_STACK_SIZE,
            param,
            RELAY_TASK_PRIORITY,
            &task_handles_[i],
            RELAY_TASK_CORE
        );

        if (res != pdPASS) {
            ESP_LOGE(TAG, "Failed to create task %s (res=%d)", task_name, res);
            delete param;
            task_handles_[i] = nullptr;
            all_ok = false;
        } else {
            ESP_LOGI(TAG, "Created task %s successfully pinned to core %d", task_name, RELAY_TASK_CORE);
        }
    }

    return all_ok;
}

#else

bool ScheduleManager::startAllTasks() {
    return true;
}

#endif // ESP_PLATFORM || ARDUINO
