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
                wdt_registered_[j] = false;
            }
            task_handles_[i] = nullptr;
            wdt_registered_[i] = false;

            if (relay_ != nullptr) {
                for (uint8_t j = 0; j < TOTAL_RELAYS; j++) {
                    bool off_ok = relay_->forceRelayOffEmergency(j);
                    if (!off_ok) {
                        ESP_LOGE(TAG, "CRITICAL: Failed to force relay %u OFF during task creation rollback!", j);
                    }
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

bool ScheduleManager::isTaskWdtRegistered(uint8_t relay_id) const {
    if (relay_id >= TOTAL_RELAYS) return false;
    return wdt_registered_[relay_id];
}

bool ScheduleManager::isTaskAlive(uint8_t relay_id) const {
    if (relay_id >= TOTAL_RELAYS) return false;
    return task_handles_[relay_id] != nullptr;
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

bool ScheduleManager::updateRuntimePhaseState(uint8_t relay_id, SchedulePhase phase, uint32_t remaining_s, const RelayProfile* profile, const bool* is_night) {
    if (xSemaphoreTake(state_mutex_, pdMS_TO_TICKS(1000)) == pdTRUE) {
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
    ESP_LOGE(TAG, "Timeout taking state_mutex_ updating phase state for relay %u", relay_id);
    return false;
}

bool ScheduleManager::ensureTaskWatchdogHealthy(uint8_t relay_id) {
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
    return true;
}

int ScheduleManager::processActiveOverride(uint8_t relay_id, SchedulePhase phase, uint32_t rem) {
    if (relay_ != nullptr && relay_->isOverrideActive(relay_id)) {
        relay_->tickOverride(relay_id);
        if (!updateRuntimePhaseState(relay_id, phase, rem)) {
            ESP_LOGE(TAG, "Failed to update runtime state during override for relay %u due to state_mutex_ timeout.", relay_id);
            if (relay_ != nullptr) {
                relay_->forceRelayOffEmergency(relay_id);
            }
            return -1;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
        return 1;
    }
    return 0;
}

bool ScheduleManager::applyScheduledRelayState(uint8_t relay_id, RelayState pin_state) {
    if (relay_ != nullptr) {
        bool applied = relay_->applyScheduledStateUnlessOverride(relay_id, pin_state);
        if (!applied) {
            ESP_LOGE(TAG, "Apply scheduled state failed for relay %u. Executing emergency RELAY_OFF fail-safe.", relay_id);
            relay_->forceRelayOffEmergency(relay_id);
            return false;
        }
    }
    return true;
}

bool ScheduleManager::executePhase(uint8_t relay_id, SchedulePhase phase, uint32_t duration_s, RelayState pin_state, const RelayProfile& profile, bool is_night) {
    if (!updateRuntimePhaseState(relay_id, phase, duration_s, &profile, &is_night)) {
        ESP_LOGE(TAG, "Failed to update initial runtime state for relay %u due to state_mutex_ timeout.", relay_id);
        if (relay_ != nullptr) {
            relay_->forceRelayOffEmergency(relay_id);
        }
        return false;
    }

    uint32_t rem = duration_s;
    while (rem > 0) {
        if (!ensureTaskWatchdogHealthy(relay_id)) {
            return false;
        }

        int ov_res = processActiveOverride(relay_id, phase, rem);
        if (ov_res == -1) {
            return false;
        } else if (ov_res == 1) {
            continue;
        }

        if (!updateRuntimePhaseState(relay_id, phase, rem)) {
            ESP_LOGE(TAG, "Failed to update tick runtime state for relay %u due to state_mutex_ timeout.", relay_id);
            if (relay_ != nullptr) {
                relay_->forceRelayOffEmergency(relay_id);
            }
            return false;
        }

        if (!applyScheduledRelayState(relay_id, pin_state)) {
            return false;
        }

        rem--;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    return true;
}

bool ScheduleManager::registerTaskWdt(uint8_t relay_id) {
    esp_err_t add_err = esp_task_wdt_add(NULL);
    bool is_added = false;

    if (add_err == ESP_OK) {
        is_added = true;
    } else if (add_err == ESP_ERR_INVALID_STATE) {
        esp_err_t stat_err = esp_task_wdt_status(NULL);
        if (stat_err == ESP_OK) {
            is_added = true;
            ESP_LOGI(TAG, "Relay task %u was already subscribed to Task WDT.", relay_id);
        } else {
            ESP_LOGE(TAG, "esp_task_wdt_add returned INVALID_STATE and status verification failed (0x%x) for relay task %u", stat_err, relay_id);
        }
    } else {
        ESP_LOGE(TAG, "esp_task_wdt_add failed for relay task %u: 0x%x.", relay_id, add_err);
    }

    if (!is_added) {
        wdt_registered_[relay_id] = false;
        return false;
    }

    // Prove reset watchdog succeeds BEFORE setting wdt_registered_[relay_id] = true
    esp_err_t reset_err = esp_task_wdt_reset();
    if (reset_err != ESP_OK) {
        ESP_LOGE(TAG, "Verification esp_task_wdt_reset failed for relay task %u: 0x%x", relay_id, reset_err);
        esp_task_wdt_delete(NULL);
        wdt_registered_[relay_id] = false;
        return false;
    }

    wdt_registered_[relay_id] = true;
    ESP_LOGI(TAG, "Relay task %u registered and verified with Task WDT successfully.", relay_id);
    return true;
}

bool ScheduleManager::resetTaskWdt(uint8_t relay_id) {
    if (!wdt_registered_[relay_id]) {
        ESP_LOGE(TAG, "Relay task %u lost WDT registration!", relay_id);
        return false;
    }
    esp_err_t reset_err = esp_task_wdt_reset();
    if (reset_err != ESP_OK) {
        ESP_LOGE(TAG, "CRITICAL WDT FAILURE: esp_task_wdt_reset returned 0x%x in relay task loop %u!", reset_err, relay_id);
        return false;
    }
    return true;
}

void ScheduleManager::handleTaskTermination(uint8_t relay_id, const char* reason) {
    ESP_LOGE(TAG, "Terminating relay task %u in FAULTED safe-state due to: %s", relay_id, reason ? reason : "Unknown");
    if (relay_ != nullptr) {
        relay_->forceRelayOffEmergency(relay_id);
    }
    if (wdt_registered_[relay_id]) {
        esp_task_wdt_delete(NULL);
        wdt_registered_[relay_id] = false;
    }
    task_handles_[relay_id] = nullptr;
}

void ScheduleManager::relayTaskLoop(uint8_t relay_id) {
    ESP_LOGI(TAG, "Relay task %u running on Core %d", relay_id, xPortGetCoreID());
    if (!registerTaskWdt(relay_id)) {
        handleTaskTermination(relay_id, "esp_task_wdt_add failed");
        vTaskDelete(NULL);
        return;
    }

    while (true) {
        if (!resetTaskWdt(relay_id)) {
            handleTaskTermination(relay_id, "WDT reset failed");
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

        if (!executePhase(relay_id, PHASE_SPRAYING, spray_s, RELAY_ON, current_profile, is_night)) {
            handleTaskTermination(relay_id, "SPRAYING phase failed");
            vTaskDelete(NULL);
            return;
        }

        if (!executePhase(relay_id, PHASE_COOLING_DOWN, cooldown_s, RELAY_OFF, current_profile, is_night)) {
            handleTaskTermination(relay_id, "COOLING_DOWN phase failed");
            vTaskDelete(NULL);
            return;
        }
    }
}

#ifdef ENABLE_FAULT_INJECTION_TEST
struct OverrideTestParam {
    ScheduleManager* sm;
    uint8_t relay_id;
    SchedulePhase phase;
    uint32_t duration_s;
    RelayState pin_state;
    RelayProfile profile;
    bool is_night;
    bool execute_result;
    SemaphoreHandle_t sem_started;
    SemaphoreHandle_t sem_done;
};

void testPhaseTask(void* pvParameters) {
    OverrideTestParam* p = static_cast<OverrideTestParam*>(pvParameters);
    if (p != nullptr && p->sm != nullptr) {
        if (p->sem_started != nullptr) {
            xSemaphoreGive(p->sem_started);
        }
        p->execute_result = p->sm->executePhase(p->relay_id, p->phase, p->duration_s, p->pin_state, p->profile, p->is_night);
        if (p->sem_done != nullptr) {
            xSemaphoreGive(p->sem_done);
        }
    }
    vTaskDelete(NULL);
}

static bool createAndStartPhaseTestTask(OverrideTestParam& param, TaskHandle_t& phase_task) {
    param.sem_started = xSemaphoreCreateBinary();
    param.sem_done = xSemaphoreCreateBinary();
    if (param.sem_started == nullptr || param.sem_done == nullptr) {
        if (param.sem_started) vSemaphoreDelete(param.sem_started);
        if (param.sem_done) vSemaphoreDelete(param.sem_done);
        return false;
    }
    BaseType_t res = xTaskCreatePinnedToCore(
        testPhaseTask,
        "test_phase",
        4096,
        &param,
        configMAX_PRIORITIES - 1,
        &phase_task,
        1
    );
    if (res != pdPASS) {
        vSemaphoreDelete(param.sem_started);
        vSemaphoreDelete(param.sem_done);
        return false;
    }
    xSemaphoreTake(param.sem_started, pdMS_TO_TICKS(1000));
    vTaskDelay(pdMS_TO_TICKS(200));
    return true;
}

static bool verifyOverrideBehaviorAndExpiry(ScheduleManager* sm, RelayController* relay, uint8_t relay_id, RelayState scheduled_pin_state, RelayState override_forced_state, TaskHandle_t phase_task, OverrideTestParam& param) {
    RelayRuntimeState st_before = sm->getRuntimeState(relay_id);
    uint32_t rem_before = st_before.phase_remaining_s;

    bool override_start_ok = relay->startManualOverride(relay_id, override_forced_state, 3);
    if (!override_start_ok) {
        ESP_LOGE(TAG, "[OVERRIDE TEST] startManualOverride returned false!");
        if (phase_task) vTaskDelete(phase_task);
        return false;
    }

    if (relay->getRelayState(relay_id) != override_forced_state) {
        ESP_LOGE(TAG, "[OVERRIDE TEST] Relay state during override mismatched!");
        if (phase_task) vTaskDelete(phase_task);
        return false;
    }

    vTaskDelay(pdMS_TO_TICKS(1500));

    RelayRuntimeState st_during = sm->getRuntimeState(relay_id);
    if (st_during.phase_remaining_s != rem_before) {
        ESP_LOGE(TAG, "[OVERRIDE TEST] FAIL: phase countdown decremented during override! Before=%u, During=%u", (unsigned)rem_before, (unsigned)st_during.phase_remaining_s);
        if (phase_task) vTaskDelete(phase_task);
        return false;
    }

    vTaskDelay(pdMS_TO_TICKS(2000));

    if (relay->isOverrideActive(relay_id) || relay->getRelayState(relay_id) != scheduled_pin_state) {
        ESP_LOGE(TAG, "[OVERRIDE TEST] FAIL: state after expiry incorrect!");
        if (phase_task) vTaskDelete(phase_task);
        return false;
    }

    bool phase_done = (xSemaphoreTake(param.sem_done, pdMS_TO_TICKS(10000)) == pdTRUE);
    return (phase_done && param.execute_result);
}

static bool runSinglePhaseOverrideTest(ScheduleManager* sm, RelayController* relay, uint8_t relay_id, SchedulePhase phase, RelayState scheduled_pin_state, RelayState override_forced_state) {
    OverrideTestParam param;
    param.sm = sm;
    param.relay_id = relay_id;
    param.phase = phase;
    param.duration_s = 5;
    param.pin_state = scheduled_pin_state;
    param.profile = RelayProfile{ 5, 30, 5, 30 };
    param.is_night = false;
    param.execute_result = false;

    TaskHandle_t phase_task = nullptr;
    if (!createAndStartPhaseTestTask(param, phase_task)) {
        return false;
    }

    bool pass = verifyOverrideBehaviorAndExpiry(sm, relay, relay_id, scheduled_pin_state, override_forced_state, phase_task, param);

    if (param.sem_started) vSemaphoreDelete(param.sem_started);
    if (param.sem_done) vSemaphoreDelete(param.sem_done);
    return pass;
}

bool ScheduleManager::testOverridePauseResume() {
    ESP_LOGI(TAG, "[OVERRIDE PAUSE/RESUME TEST] Starting manual override pause/resume verification on ISOLATED test instances...");
    
    // Create isolated test harness instances to avoid touching production state
    RelayController test_rc;
    test_rc.initPins();

    NvsStorage test_nvs;
    test_nvs.begin();

    RtcManager test_rtc;
    test_rtc.begin();

    ScheduleManager test_sm;
    if (!test_sm.begin(&test_nvs, &test_rtc, &test_rc)) {
        ESP_LOGE(TAG, "[OVERRIDE TEST] Failed to initialize isolated ScheduleManager for testing!");
        return false;
    }

    uint8_t test_relay = 0;
    test_sm.wdt_registered_[test_relay] = true;
    test_rc.resetFaultLatch(test_relay);

    ESP_LOGI(TAG, "[OVERRIDE TEST] Testing PHASE_SPRAYING on isolated instance...");
    bool spraying_ok = runSinglePhaseOverrideTest(&test_sm, &test_rc, test_relay, PHASE_SPRAYING, RELAY_ON, RELAY_OFF);
    if (!spraying_ok) {
        ESP_LOGE(TAG, "[OVERRIDE TEST] FAIL on PHASE_SPRAYING test!");
        return false;
    }

    ESP_LOGI(TAG, "[OVERRIDE TEST] Testing PHASE_COOLING_DOWN on isolated instance...");
    bool cooldown_ok = runSinglePhaseOverrideTest(&test_sm, &test_rc, test_relay, PHASE_COOLING_DOWN, RELAY_OFF, RELAY_ON);
    if (!cooldown_ok) {
        ESP_LOGE(TAG, "[OVERRIDE TEST] FAIL on PHASE_COOLING_DOWN test!");
        return false;
    }

    ESP_LOGI(TAG, "[OVERRIDE PAUSE/RESUME TEST] PASS: Auto-timer paused and resumed accurately for both SPRAYING and COOLING_DOWN phases without mutating production state!");
    return true;
}
#endif
