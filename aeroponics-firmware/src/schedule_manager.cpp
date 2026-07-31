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
    : nvs_(nullptr), rtc_(nullptr), relay_(nullptr), wdt_(nullptr), task_runner_(nullptr),
      is_initialized_(false), lifecycle_state_(ScheduleLifecycleState::NOT_STARTED) {
    for (uint8_t i = 0; i < TOTAL_RELAYS; ++i) {
        profiles_[i] = RelayProfile{ DEFAULT_SPRAY_DAY_S, DEFAULT_COOLDOWN_DAY_S, DEFAULT_SPRAY_NIGHT_S, DEFAULT_COOLDOWN_NIGHT_S };
        runtime_states_[i] = RelayRuntimeState{ PHASE_SPRAYING, DEFAULT_SPRAY_DAY_S, profiles_[i], false };
        wdt_registered_[i] = false;
        stop_requested_[i].store(false);
        task_stopped_[i].store(false);
    }
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    profile_mutex_ = nullptr;
    state_mutex_ = nullptr;
#endif
}

ScheduleManager::~ScheduleManager() {
    for (uint8_t i = 0; i < TOTAL_RELAYS; ++i) {
        stop_requested_[i].store(true);
        if (task_runner_ != nullptr) {
            task_runner_->stopTask(i);
        }
    }
#if defined(ESP_PLATFORM) || defined(ARDUINO)
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

bool ScheduleManager::begin(IProfileRepository* nvs, IClock* rtc, IRelayOutput* relay, IWatchdog* wdt, ITaskRunner* task_runner) {
    if (nvs == nullptr || rtc == nullptr || relay == nullptr) {
        ESP_LOGE(TAG, "begin failed: null dependency passed (nvs=%p, rtc=%p, relay=%p)", (void*)nvs, (void*)rtc, (void*)relay);
        return false;
    }

    nvs_ = nvs;
    rtc_ = rtc;
    relay_ = relay;
    wdt_ = wdt;
    task_runner_ = task_runner;

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (profile_mutex_ == nullptr) profile_mutex_ = xSemaphoreCreateMutex();
    if (state_mutex_ == nullptr) state_mutex_ = xSemaphoreCreateMutex();
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
    lifecycle_state_ = ScheduleLifecycleState::NOT_STARTED;
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

bool ScheduleManager::getRuntimeStateSafely(uint8_t relay_id, RelayRuntimeState &out_state) const {
    if (relay_id >= TOTAL_RELAYS) {
        return false;
    }
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (state_mutex_ != nullptr && xSemaphoreTake(state_mutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
        out_state = runtime_states_[relay_id];
        xSemaphoreGive(state_mutex_);
        return true;
    }
    return false;
#else
    out_state = runtime_states_[relay_id];
    return true;
#endif
}

RelayRuntimeState ScheduleManager::getRuntimeState(uint8_t relay_id) const {
    RelayRuntimeState fallback = { PHASE_SPRAYING, 0, RelayProfile{0,0,0,0}, false };
    if (relay_id >= TOTAL_RELAYS) {
        return fallback;
    }
    getRuntimeStateSafely(relay_id, fallback);
    return fallback;
}

bool ScheduleManager::stepRelayPhase(uint8_t relay_id) {
    if (relay_id >= TOTAL_RELAYS || !is_initialized_) {
        return false;
    }

    RelayProfile prof;
    if (!fetchProfileSafely(relay_id, prof)) {
        ESP_LOGE(TAG, "stepRelayPhase failed for relay %u: fetchProfileSafely mutex timeout", relay_id);
        if (relay_ != nullptr) {
            relay_->forceRelayOffEmergency(relay_id);
        }
        return false;
    }

    RelayRuntimeState state;
    if (!getRuntimeStateSafely(relay_id, state)) {
        ESP_LOGE(TAG, "stepRelayPhase failed for relay %u: getRuntimeStateSafely mutex timeout", relay_id);
        if (relay_ != nullptr) {
            relay_->forceRelayOffEmergency(relay_id);
        }
        return false;
    }

    bool night_mode = (rtc_ != nullptr) ? rtc_->isNightMode() : false;

    // Rule S1: Check if manual override is active BEFORE decrementing phase_remaining_s or changing phase
    if (relay_ != nullptr && relay_->isOverrideActive(relay_id)) {
        // Tick override countdown timer in relay output (single timer ownership!)
        relay_->tickOverride(relay_id);

        if (relay_->isOverrideActive(relay_id)) {
            // Override STILL active after tick
            if (!updateRuntimePhaseState(relay_id, state.phase, state.phase_remaining_s, &prof, &night_mode)) {
                ESP_LOGE(TAG, "stepRelayPhase failed for relay %u: updateRuntimePhaseState mutex timeout during override", relay_id);
                relay_->forceRelayOffEmergency(relay_id);
                return false;
            }
            return true;
        }

        // Override JUST EXPIRED on this tick!
        // Immediately restore scheduled relay output state in this same tick
        RelayState target_scheduled_state = (state.phase == PHASE_SPRAYING) ? RELAY_ON : RELAY_OFF;
        if (!relay_->setRelay(relay_id, target_scheduled_state)) {
            ESP_LOGE(TAG, "stepRelayPhase failed for relay %u: setRelay failed on override expiration", relay_id);
            relay_->forceRelayOffEmergency(relay_id);
            return false;
        }

        // Phase & phase_remaining_s MUST remain UNCHANGED on this tick
        if (!updateRuntimePhaseState(relay_id, state.phase, state.phase_remaining_s, &prof, &night_mode)) {
            ESP_LOGE(TAG, "stepRelayPhase failed for relay %u: updateRuntimePhaseState mutex timeout on override expiration", relay_id);
            relay_->forceRelayOffEmergency(relay_id);
            return false;
        }

        return true;
    }

    // Override is NOT active: proceed with auto-timer countdown & phase transitions
    if (state.phase_remaining_s > 0) {
        state.phase_remaining_s--;
    }

    RelayState target_scheduled_state = (state.phase == PHASE_SPRAYING) ? RELAY_ON : RELAY_OFF;

    if (relay_ != nullptr) {
        if (!relay_->setRelay(relay_id, target_scheduled_state)) {
            ESP_LOGE(TAG, "stepRelayPhase failed for relay %u: setRelay failed", relay_id);
            relay_->forceRelayOffEmergency(relay_id);
            return false;
        }
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

    if (!updateRuntimePhaseState(relay_id, state.phase, state.phase_remaining_s, &prof, &night_mode)) {
        ESP_LOGE(TAG, "stepRelayPhase failed for relay %u: updateRuntimePhaseState mutex timeout", relay_id);
        if (relay_ != nullptr) {
            relay_->forceRelayOffEmergency(relay_id);
        }
        return false;
    }

    return true;
}

bool ScheduleManager::isTaskWdtRegistered(uint8_t relay_id) const {
    if (relay_id >= TOTAL_RELAYS) return false;
    return wdt_registered_[relay_id];
}

bool ScheduleManager::isTaskAlive(uint8_t relay_id) const {
    if (relay_id >= TOTAL_RELAYS || task_runner_ == nullptr) return false;
    return task_runner_->isTaskAlive(relay_id);
}

ScheduleLifecycleState ScheduleManager::getLifecycleState() const {
    return lifecycle_state_;
}

void ScheduleManager::relayTaskTrampoline(uint8_t relay_id, void* arg) {
    ScheduleManager* mgr = static_cast<ScheduleManager*>(arg);
    if (mgr != nullptr) {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
        mgr->relayTaskLoop(relay_id);
#endif
    }
}

bool ScheduleManager::registerTaskWdt(uint8_t relay_id) {
    if (relay_id >= TOTAL_RELAYS) return false;
    if (wdt_ != nullptr) {
        bool ok = wdt_->registerWatchdog(relay_id);
        if (ok) wdt_registered_[relay_id] = true;
        return ok;
    }
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    esp_err_t err = esp_task_wdt_add(NULL);
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
        wdt_registered_[relay_id] = true;
        return true;
    }
    ESP_LOGE(TAG, "esp_task_wdt_add failed for relay task %u: 0x%x", relay_id, err);
    return false;
#else
    wdt_registered_[relay_id] = true;
    return true;
#endif
}

bool ScheduleManager::resetTaskWdt(uint8_t relay_id) {
    if (relay_id >= TOTAL_RELAYS) return false;
    if (wdt_ != nullptr) {
        return wdt_->resetWatchdog(relay_id);
    }
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    esp_err_t err = esp_task_wdt_reset();
    if (err == ESP_OK) {
        return true;
    }
    ESP_LOGE(TAG, "esp_task_wdt_reset failed for relay task %u: 0x%x", relay_id, err);
    return false;
#else
    return true;
#endif
}

bool ScheduleManager::deregisterTaskWdt(uint8_t relay_id) {
    if (relay_id >= TOTAL_RELAYS) return false;
    if (wdt_ != nullptr) {
        bool ok = wdt_->deregisterWatchdog(relay_id);
        wdt_registered_[relay_id] = false;
        return ok;
    }
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    esp_err_t err = esp_task_wdt_delete(NULL);
    wdt_registered_[relay_id] = false;
    return (err == ESP_OK || err == ESP_ERR_NOT_FOUND);
#else
    wdt_registered_[relay_id] = false;
    return true;
#endif
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

#if defined(ESP_PLATFORM) || defined(ARDUINO)

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

void ScheduleManager::relayTaskLoop(uint8_t relay_id) {
    ESP_LOGI(TAG, "Relay Task %u started on CORE %d", relay_id, xPortGetCoreID());

    if (!ensureTaskWatchdogHealthy(relay_id)) {
        task_stopped_[relay_id].store(true);
        vTaskDelete(NULL);
        return;
    }

    while (!stop_requested_[relay_id].load()) {
        if (!ensureTaskWatchdogHealthy(relay_id)) {
            task_stopped_[relay_id].store(true);
            vTaskDelete(NULL);
            return;
        }

        if (!stepRelayPhase(relay_id)) {
            handleTaskTermination(relay_id, "stepRelayPhase failed");
            task_stopped_[relay_id].store(true);
            vTaskDelete(NULL);
            return;
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    ESP_LOGI(TAG, "Relay Task %u stopping cleanly via request...", relay_id);
    deregisterTaskWdt(relay_id);
    task_stopped_[relay_id].store(true);
    vTaskDelete(NULL);
}

#endif // ESP_PLATFORM || ARDUINO

void ScheduleManager::performRollback(uint8_t created_count) {
    ESP_LOGE(TAG, "Performing orderly rollback for %u created relay task(s)...", created_count);

    for (uint8_t k = 0; k < created_count; ++k) {
        stop_requested_[k].store(true);
        if (task_runner_ != nullptr) {
            task_runner_->stopTask(k);
        }
        if (wdt_registered_[k]) {
            deregisterTaskWdt(k);
        }
        task_stopped_[k].store(true);
    }

    for (uint8_t j = 0; j < TOTAL_RELAYS; ++j) {
        if (relay_ != nullptr) {
            bool off_ok = relay_->forceRelayOffEmergency(j);
            ESP_LOGI(TAG, "Rollback emergency off for relay %u: %s", j, off_ok ? "OK" : "FAILED");
        }
        wdt_registered_[j] = false;
        stop_requested_[j].store(false);
        task_stopped_[j].store(false);
    }

    lifecycle_state_ = ScheduleLifecycleState::FAULTED;
}

bool ScheduleManager::startAllTasks() {
    if (!is_initialized_) {
        ESP_LOGE(TAG, "Cannot startAllTasks: ScheduleManager not initialized");
        return false;
    }

    if (lifecycle_state_ == ScheduleLifecycleState::STARTING || lifecycle_state_ == ScheduleLifecycleState::RUNNING) {
        ESP_LOGW(TAG, "startAllTasks rejected: already in state %s",
                 lifecycle_state_ == ScheduleLifecycleState::STARTING ? "STARTING" : "RUNNING");
        return false;
    }

    if (lifecycle_state_ == ScheduleLifecycleState::FAULTED) {
        bool all_latched_off = true;
        if (relay_ != nullptr) {
            for (uint8_t j = 0; j < TOTAL_RELAYS; ++j) {
                if (!relay_->isFaultLatched(j)) {
                    all_latched_off = false;
                    break;
                }
            }
        }
        if (!all_latched_off) {
            ESP_LOGE(TAG, "startAllTasks rejected: system FAULTED and not all relays are latched OFF");
            return false;
        }
        ESP_LOGI(TAG, "Retrying startAllTasks after FAULTED state (all relays confirmed latched OFF)");
    }

    lifecycle_state_ = ScheduleLifecycleState::STARTING;

    for (uint8_t i = 0; i < TOTAL_RELAYS; ++i) {
        stop_requested_[i].store(false);
        task_stopped_[i].store(false);
        if (task_runner_ == nullptr || !task_runner_->startTask(i, relayTaskTrampoline, this)) {
            ESP_LOGE(TAG, "Failed to create relay task %u. Initiating atomic rollback...", i);
            performRollback(i);
            return false;
        }
        if (wdt_ != nullptr) {
            registerTaskWdt(i);
        }
    }

    lifecycle_state_ = ScheduleLifecycleState::RUNNING;
    ESP_LOGI(TAG, "All 4 relay tasks started successfully (State: RUNNING)");
    return true;
}
