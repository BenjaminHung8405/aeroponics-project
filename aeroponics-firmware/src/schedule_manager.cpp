#include "schedule_manager.h"
#include <cstdio>
#include <cstring>

#if defined(ESP_PLATFORM) || defined(ARDUINO)
#include <esp_log.h>
#include <esp_system.h>
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
      is_initialized_(false), lifecycle_state_(ScheduleLifecycleState::NOT_STARTED),
      teardown_pending_(false) {
    for (uint8_t i = 0; i < TOTAL_RELAYS; ++i) {
        profiles_[i] = RelayProfile{ DEFAULT_SPRAY_DAY_S, DEFAULT_COOLDOWN_DAY_S, DEFAULT_SPRAY_NIGHT_S, DEFAULT_COOLDOWN_NIGHT_S };
        runtime_states_[i] = RelayRuntimeState{ PHASE_SPRAYING, DEFAULT_SPRAY_DAY_S, profiles_[i], false };
        wdt_registered_[i] = false;
        stop_requested_[i].store(false);
    }
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    profile_mutex_ = nullptr;
    profile_update_mutex_ = nullptr;
    state_mutex_ = nullptr;
#endif
}

ScheduleManager::~ScheduleManager() {
    const bool callbacks_exited = performRollback(TOTAL_RELAYS);
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (!callbacks_exited) {
        // A callback can still dereference this manager or its mutexes. Do not
        // return into C++ destruction; reset while static storage is intact.
        ESP_LOGE(TAG, "ScheduleManager teardown timed out; restarting before resource destruction");
        esp_restart();
        for (;;) {
            vTaskDelay(portMAX_DELAY);
        }
    }
    if (profile_mutex_ != nullptr) {
        vSemaphoreDelete(profile_mutex_);
        profile_mutex_ = nullptr;
    }
    if (profile_update_mutex_ != nullptr) {
        vSemaphoreDelete(profile_update_mutex_);
        profile_update_mutex_ = nullptr;
    }
    if (state_mutex_ != nullptr) {
        vSemaphoreDelete(state_mutex_);
        state_mutex_ = nullptr;
    }
#endif
}

bool ScheduleManager::begin(IProfileRepository* nvs, IClock* rtc, IRelayOutput* relay, IWatchdog* wdt,
                            ITaskRunner* task_runner, const RelayProfile* boot_profiles) {
    if (nvs == nullptr || rtc == nullptr || relay == nullptr || wdt == nullptr || task_runner == nullptr) {
        ESP_LOGE(TAG, "begin failed: required dependency is null");
        return false;
    }

    if (teardown_pending_.load() || lifecycle_state_.load() == ScheduleLifecycleState::STARTING ||
        lifecycle_state_.load() == ScheduleLifecycleState::RUNNING) {
        ESP_LOGE(TAG, "begin rejected: relay tasks may still own manager state");
        return false;
    }

    nvs_ = nvs;
    rtc_ = rtc;
    relay_ = relay;
    wdt_ = wdt;
    task_runner_ = task_runner;

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (profile_mutex_ == nullptr) profile_mutex_ = xSemaphoreCreateMutex();
    if (profile_update_mutex_ == nullptr) profile_update_mutex_ = xSemaphoreCreateMutex();
    if (state_mutex_ == nullptr) state_mutex_ = xSemaphoreCreateMutex();
    if (profile_mutex_ == nullptr || profile_update_mutex_ == nullptr || state_mutex_ == nullptr) {
        ESP_LOGE(TAG, "begin failed: could not create FreeRTOS mutexes");
        return false;
    }
#endif

    RelayProfile initial_profiles[TOTAL_RELAYS];
    for (uint8_t i = 0; i < TOTAL_RELAYS; ++i) {
        initial_profiles[i] = RelayProfile{ DEFAULT_SPRAY_DAY_S, DEFAULT_COOLDOWN_DAY_S,
                                            DEFAULT_SPRAY_NIGHT_S, DEFAULT_COOLDOWN_NIGHT_S };
    }
    if (boot_profiles != nullptr) {
        for (uint8_t i = 0; i < TOTAL_RELAYS; ++i) {
            initial_profiles[i] = boot_profiles[i];
        }
    }
    const bool is_night = rtc_->isNightMode();
    RelayRuntimeState initial_states[TOTAL_RELAYS];

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    xSemaphoreTake(profile_mutex_, portMAX_DELAY);
#else
    std::lock_guard<std::mutex> profile_lock(profile_mutex_);
#endif
    for (uint8_t i = 0; i < TOTAL_RELAYS; ++i) {
        profiles_[i] = initial_profiles[i];
        const RelayProfile& profile = profiles_[i];
        const uint32_t init_spray = is_night ? profile.spray_night_s : profile.spray_day_s;
        initial_states[i] = RelayRuntimeState{ PHASE_SPRAYING, init_spray, profile, is_night };
    }
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    xSemaphoreGive(profile_mutex_);
    xSemaphoreTake(state_mutex_, portMAX_DELAY);
#endif
    for (uint8_t i = 0; i < TOTAL_RELAYS; ++i) runtime_states_[i] = initial_states[i];
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    xSemaphoreGive(state_mutex_);
#endif

    is_initialized_ = true;
    lifecycle_state_.store(ScheduleLifecycleState::NOT_STARTED);
    ESP_LOGI(TAG, "ScheduleManager initialized successfully via Dependency Injection.");
    return true;
}

bool ScheduleManager::fetchProfileSafely(uint8_t relay_id, RelayProfile &out_profile) {
    if (relay_id >= TOTAL_RELAYS) {
        return false;
    }
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (profile_mutex_ == nullptr) return false;
    xSemaphoreTake(profile_mutex_, portMAX_DELAY);
    out_profile = profiles_[relay_id];
    xSemaphoreGive(profile_mutex_);
    return true;
#else
    std::lock_guard<std::mutex> lock(profile_mutex_);
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

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (profile_update_mutex_ == nullptr || profile_mutex_ == nullptr) return false;
    // NVS persistence completes before RAM publication. The scheduler only
    // holds profile_mutex_ for its small atomic snapshot/publish operation.
    xSemaphoreTake(profile_update_mutex_, portMAX_DELAY);
    const bool saved = nvs_ == nullptr || nvs_->saveProfile(relay_id, profile);
    if (saved) {
        xSemaphoreTake(profile_mutex_, portMAX_DELAY);
        profiles_[relay_id] = profile;
        xSemaphoreGive(profile_mutex_);
    }
    xSemaphoreGive(profile_update_mutex_);
    if (!saved) {
        ESP_LOGE(TAG, "updateProfile failed: repository save rejected for relay %u", relay_id);
        return false;
    }
    ESP_LOGI(TAG, "Updated RAM profile for relay ID %u successfully", relay_id);
    return true;
#else
    std::lock_guard<std::mutex> update_lock(profile_update_mutex_);
    if (nvs_ != nullptr && !nvs_->saveProfile(relay_id, profile)) {
        ESP_LOGE(TAG, "updateProfile failed: repository save rejected for relay %u", relay_id);
        return false;
    }
    {
        std::lock_guard<std::mutex> profile_lock(profile_mutex_);
        profiles_[relay_id] = profile;
    }
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

    RelayProfile profile;
    RelayRuntimeState state;
    bool night_mode = false;
    if (!loadStepSnapshot(relay_id, profile, state, night_mode)) return false;
    if (relay_->isOverrideActive(relay_id)) {
        return processOverrideTick(relay_id, profile, state, night_mode);
    }
    return processScheduledTick(relay_id, profile, state, night_mode);
}

bool ScheduleManager::loadStepSnapshot(uint8_t relay_id, RelayProfile& profile, RelayRuntimeState& state, bool& night_mode) {
    if (!fetchProfileSafely(relay_id, profile) || !getRuntimeStateSafely(relay_id, state)) {
        return failRelaySafely(relay_id, "could not acquire scheduler snapshot");
    }
    night_mode = rtc_->isNightMode();
    return true;
}

bool ScheduleManager::processOverrideTick(uint8_t relay_id, const RelayProfile& profile,
                                          const RelayRuntimeState& state, bool night_mode) {
    relay_->tickOverride(relay_id);
    if (!relay_->isOverrideActive(relay_id) && !applyScheduledRelayState(relay_id, state.phase)) {
        return false;
    }
    if (!updateRuntimePhaseState(relay_id, state.phase, state.phase_remaining_s, &profile, &night_mode)) {
        return failRelaySafely(relay_id, "could not persist override snapshot");
    }
    return true;
}

bool ScheduleManager::processScheduledTick(uint8_t relay_id, RelayProfile& profile,
                                           RelayRuntimeState& state, bool night_mode) {
    if (state.phase_remaining_s > 0) --state.phase_remaining_s;
    if (!applyScheduledRelayState(relay_id, state.phase)) return false;
    if (state.phase_remaining_s == 0) {
        state.phase = state.phase == PHASE_SPRAYING ? PHASE_COOLING_DOWN : PHASE_SPRAYING;
        state.phase_remaining_s = state.phase == PHASE_SPRAYING
            ? (night_mode ? profile.spray_night_s : profile.spray_day_s)
            : (night_mode ? profile.cooldown_night_s : profile.cooldown_day_s);
    }
    if (!updateRuntimePhaseState(relay_id, state.phase, state.phase_remaining_s, &profile, &night_mode)) {
        return failRelaySafely(relay_id, "could not persist scheduled snapshot");
    }
    return true;
}

bool ScheduleManager::applyScheduledRelayState(uint8_t relay_id, SchedulePhase phase) {
    const RelayState target = phase == PHASE_SPRAYING ? RELAY_ON : RELAY_OFF;
    if (relay_->setRelay(relay_id, target)) return true;
    return failRelaySafely(relay_id, "could not apply scheduled relay state");
}

bool ScheduleManager::failRelaySafely(uint8_t relay_id, const char* reason) {
    ESP_LOGE(TAG, "Relay %u entering emergency safe state: %s", relay_id, reason);
    if (relay_ != nullptr) relay_->forceRelayOffEmergency(relay_id);
    return false;
}

bool ScheduleManager::isTaskWdtRegistered(uint8_t relay_id) const {
    if (relay_id >= TOTAL_RELAYS) return false;
    return wdt_registered_[relay_id].load();
}

bool ScheduleManager::isManagerCallbackActive(uint8_t relay_id) const {
    if (relay_id >= TOTAL_RELAYS || task_runner_ == nullptr) return false;
    return task_runner_->isManagerCallbackActive(relay_id);
}

ScheduleLifecycleState ScheduleManager::getLifecycleState() const {
    return lifecycle_state_.load();
}

bool ScheduleManager::isTeardownPending() const {
    return teardown_pending_.load();
}

bool ScheduleManager::registerTaskWdt(uint8_t relay_id) {
    if (relay_id >= TOTAL_RELAYS || wdt_ == nullptr) return false;
    const bool ok = wdt_->registerWatchdog(relay_id);
    wdt_registered_[relay_id].store(ok);
    return ok;
}

bool ScheduleManager::resetTaskWdt(uint8_t relay_id) {
    return relay_id < TOTAL_RELAYS && wdt_ != nullptr && wdt_->resetWatchdog(relay_id);
}

bool ScheduleManager::deregisterTaskWdt(uint8_t relay_id) {
    if (relay_id >= TOTAL_RELAYS || wdt_ == nullptr || !wdt_registered_[relay_id].load()) return false;
    const bool ok = wdt_->deregisterWatchdog(relay_id);
    if (ok) wdt_registered_[relay_id].store(false);
    return ok;
}

void ScheduleManager::handleTaskTermination(uint8_t relay_id, const char* reason) {
    ESP_LOGE(TAG, "Relay task %u terminating due to failure: %s", relay_id, reason);
    if (relay_ != nullptr) {
        relay_->forceRelayOffEmergency(relay_id);
    }
    if (wdt_registered_[relay_id].load()) {
        deregisterTaskWdt(relay_id);
    }
}

bool ScheduleManager::initializeRelayTask(uint8_t relay_id) {
    const bool registered = registerTaskWdt(relay_id);
    // The runner supplies a generation-bound context; relay tasks retrieve it
    // only through their task entry path, so notify is issued in runRelayTask.
    // This method stays focused on WDT registration.
    if (!registered) {
        failRelaySafely(relay_id, "watchdog registration failed");
    }
    return registered;
}

void ScheduleManager::runRelayTask(uint8_t relay_id, uint32_t generation) {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (stop_requested_[relay_id].load() || task_runner_->consumeStopRequest(relay_id, generation)) {
        return;
    }
    const bool started = initializeRelayTask(relay_id);
    task_runner_->notifyStarted(relay_id, generation, started);
    if (started) relayTaskLoop(relay_id, generation);
#else
    if (stop_requested_[relay_id].load() || task_runner_->consumeStopRequest(relay_id, generation)) {
        if (wdt_registered_[relay_id].load()) deregisterTaskWdt(relay_id);
        return;
    }
    const bool started = initializeRelayTask(relay_id);
    task_runner_->notifyStarted(relay_id, generation, started);
    // Native runners do not own a FreeRTOS trampoline. Publish the same
    // callback-exit acknowledgement when startup fails before return.
    if (!started) task_runner_->notifyManagerCallbackExited(relay_id, generation);
#endif
}

#if defined(ESP_PLATFORM) || defined(ARDUINO)

void ScheduleManager::relayTaskLoop(uint8_t relay_id, uint32_t generation) {
    ESP_LOGI(TAG, "Relay Task %u started on CORE %d", relay_id, xPortGetCoreID());

    while (!stop_requested_[relay_id].load() && !task_runner_->consumeStopRequest(relay_id, generation)) {
        // S1-WDT-06: feeding the relay task watchdog is the first operation
        // of every scheduling iteration.
        if (!resetTaskWdt(relay_id)) {
            handleTaskTermination(relay_id, "Watchdog feed failed");
            return;
        }

        if (!stepRelayPhase(relay_id)) {
            handleTaskTermination(relay_id, "stepRelayPhase failed");
            return;
        }

        // A task notification from FreeRTOSTaskRunner wakes this wait early
        // for a cooperative stop; otherwise it preserves 1-second cadence.
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
    }

    ESP_LOGI(TAG, "Relay Task %u stopping cleanly via request...", relay_id);
    deregisterTaskWdt(relay_id);
}

#endif // ESP_PLATFORM || ARDUINO

bool ScheduleManager::performRollback(uint8_t created_count) {
    ESP_LOGE(TAG, "Performing orderly rollback for %u created relay task(s)...", created_count);

    for (uint8_t k = 0; k < created_count; ++k) {
        stop_requested_[k].store(true);
        if (task_runner_ != nullptr && task_runner_->isManagerCallbackActive(k)) {
            task_runner_->requestStop(k);
        }
    }
    bool all_tasks_stopped = true;
    for (uint8_t k = 0; k < created_count; ++k) {
        if (task_runner_ != nullptr && task_runner_->isManagerCallbackActive(k) &&
            !task_runner_->waitUntilManagerCallbackExited(k, WDT_TIMEOUT_S * 1000)) {
            ESP_LOGE(TAG, "Relay task %u did not exit ScheduleManager callback before rollback timeout", k);
            all_tasks_stopped = false;
        }
        if (task_runner_ != nullptr && task_runner_->isManagerCallbackActive(k)) {
            ESP_LOGE(TAG, "Relay task %u may still access ScheduleManager after rollback", k);
            all_tasks_stopped = false;
        }
    }

    for (uint8_t j = 0; j < TOTAL_RELAYS; ++j) {
        if (relay_ != nullptr) {
            bool off_ok = relay_->forceRelayOffEmergency(j);
            ESP_LOGI(TAG, "Rollback emergency off for relay %u: %s", j, off_ok ? "OK" : "FAILED");
        }
        // Do not clear a stop request until every task has acknowledged exit.
        // Clearing it after a timeout could let a still-running task resume and
        // access the manager after its destructor returns.
        if (all_tasks_stopped) stop_requested_[j].store(false);
    }

    teardown_pending_.store(!all_tasks_stopped);
    lifecycle_state_.store(ScheduleLifecycleState::FAULTED);
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (!all_tasks_stopped) {
        // Static composition-root objects and all callback-owned primitives
        // remain valid until reset; never continue toward C++ destruction.
        ESP_LOGE(TAG, "Rollback callback timeout; relays latched OFF, restarting safely");
        esp_restart();
        for (;;) {
            vTaskDelay(portMAX_DELAY);
        }
    }
#endif
    return all_tasks_stopped;
}

bool ScheduleManager::startAllTasks() {
    if (!is_initialized_) {
        ESP_LOGE(TAG, "Cannot startAllTasks: ScheduleManager not initialized");
        return false;
    }

    const ScheduleLifecycleState current_state = lifecycle_state_.load();
    if (current_state == ScheduleLifecycleState::STARTING || current_state == ScheduleLifecycleState::RUNNING) {
        ESP_LOGW(TAG, "startAllTasks rejected: already in state %s",
                 current_state == ScheduleLifecycleState::STARTING ? "STARTING" : "RUNNING");
        return false;
    }

    if (current_state == ScheduleLifecycleState::FAULTED) {
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

    lifecycle_state_.store(ScheduleLifecycleState::STARTING);

    for (uint8_t i = 0; i < TOTAL_RELAYS; ++i) {
        stop_requested_[i].store(false);
        RelayTaskContext context{ i, this, 0 };
        if (!task_runner_->startTask(i, context)) {
            ESP_LOGE(TAG, "Failed to create relay task %u. Initiating atomic rollback...", i);
            performRollback(i);
            return false;
        }
        if (!task_runner_->waitUntilStarted(i, 1000)) {
            ESP_LOGE(TAG, "Relay task %u failed WDT startup verification. Initiating atomic rollback...", i);
            performRollback(i + 1);
            return false;
        }
        if (!task_runner_->isManagerCallbackActive(i)) {
            ESP_LOGE(TAG, "Relay task %u exited its manager callback during startup. Initiating atomic rollback...", i);
            performRollback(i + 1);
            return false;
        }
    }

    lifecycle_state_.store(ScheduleLifecycleState::RUNNING);
    ESP_LOGI(TAG, "All 4 relay tasks started successfully (State: RUNNING)");
    return true;
}
