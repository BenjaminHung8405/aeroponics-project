#if defined(ESP_PLATFORM) || defined(ARDUINO)

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <esp_log.h>
#include <esp_task_wdt.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <cstring>
#include <strings.h>
#include <cerrno>
#include <cstdlib>
#include <cstdint>
#include <algorithm>

#include "config.h"
#include "nvs_storage.h"
#include "rtc_manager.h"
#include "node_registry.h"
#include "command_manager.h"
#include "mqtt_client.h"
#include "mqtt_lifecycle.h"
#include "mqtt_task_policy.h"
#include "mqtt_config_provider.h"
#include "ESPTaskWatchdog.h"
#include "core/IRfTransport.h"
#include "uart_rf_transport.h"
#include "rf_provisioning.h"
#include "group_scheduler.h"
#include "wifi_storage_manager.h"
#include "wifi_controller_task.h"
#include "hardware_button.h"
#include "agu_legacy_codec.h"
#include "agu_legacy_rf_host.h"
#include "node_fsm.h"
#include "hmi_display.h"

// Log tag for gateway application orchestrator
static const char *TAG = "GATEWAY_MAIN";

// Global instances of gateway core software controllers
static NvsStorage g_nvs_storage;
static NvsStorage g_rf_nvs_storage(nullptr, RF_NVS_NAMESPACE);
static NvsStorage g_clock_nvs_storage(nullptr, CLOCK_NVS_NAMESPACE);
static WifiStorageManager g_wifi_storage;
static HardwareButton g_hardware_button(PORTAL_BUTTON_PIN, LED_STATUS_PIN);
static WifiControllerTask g_wifi_controller;
static RtcManager g_rtc_manager;
static UartRfTransport *g_rf_transport = nullptr;
static AguLegacyRfHost *g_agu_legacy_host = nullptr;
static NodeRegistry g_node_registry;
static CommandManager g_command_manager;
static GroupScheduler g_group_scheduler;

static MqttClient mqtt_client;
static MqttConfig mqtt_config;
static bool g_mqtt_initialized = false;
// Control Slots are the authoritative scheduler authorization after boot.
// Before the first retained slot reconcile completes, all groups stay OFF.
static bool g_control_slots_reconciled = false;
static uint32_t g_control_slots_boot_ms = 0;
static constexpr uint32_t BOOT_RECONCILE_TIMEOUT_MS = 15000;
enum class HmiTargetType : uint8_t
{
    EMPTY,
    NODE,
    GROUP
};
struct HmiSlotTarget
{
    HmiTargetType type = HmiTargetType::EMPTY;
    uint8_t id = 0;
};
static HmiSlotTarget g_hmi_slot_targets[4] = {};
static HmiSlotTarget g_pending_hmi_slot_targets[4] = {};
static std::atomic<bool> g_hmi_slot_config_pending{false};

static bool provideScheduleState(ScheduleStateSnapshot &snapshot)
{
    snapshot.slots_reconciled = g_control_slots_reconciled;
    for (uint8_t i = 0; i < 4; ++i)
    {
        snapshot.slots[i].idx = i + 1;
        snapshot.slots[i].type = g_hmi_slot_targets[i].type == HmiTargetType::NODE    ? 1
                                 : g_hmi_slot_targets[i].type == HmiTargetType::GROUP ? 2
                                                                                      : 0;
        snapshot.slots[i].id = g_hmi_slot_targets[i].id;
    }
    snapshot.assignment_version = g_group_scheduler.getActiveAssignmentVersion();
    uint8_t node_ids[MAX_NODES] = {};
    uint8_t group_ids[MAX_NODES] = {};
    snapshot.assignment_count = g_group_scheduler.getNodeAssignments(node_ids, group_ids, MAX_NODES);
    for (size_t i = 0; i < snapshot.assignment_count; ++i)
    {
        snapshot.assignments[i].node_id = node_ids[i];
        snapshot.assignments[i].group_id = group_ids[i];
    }
    return true;
}

// Serial command and timing state variables
static bool g_pending_factory_confirm = false;
static bool g_rf_raw_dump = false;
static uint32_t g_last_wifi_check_ms = 0;
static uint32_t g_last_command_fanout_ms = 0;
static uint32_t g_last_stale_eval_ms = 0;
static uint32_t g_last_agu_ping_ms = 0;
static bool g_wdt_registered = false;
static bool g_boot_successful = false;
static bool g_gateway_operational = false;

// Virtual FSM per-node state (Track D1): replaces LegacyOverride entirely.
// g_node_fsm[id].node_id is initialized for the AGU legacy compatibility nodes.
static NodeFsmState g_node_fsm[RF_PRODUCTION_MAX_NODE_ID + 1] = {};
// Bounded correlation table: rf_command_id ↔ mqtt_command_id, static array only.
static PendingCommandTable g_pending_commands;
// Last MQTT command_id per node, for snapshot publishing only (not RF state).
static char g_last_command_id[RF_PRODUCTION_MAX_NODE_ID + 1][65] = {};
// Manual-override source label per node, for snapshot publishing only.
static char g_override_source[RF_PRODUCTION_MAX_NODE_ID + 1][24] = {};

struct NodeLivenessRecord
{
    uint32_t last_ping_sent_ms = 0;
    uint32_t last_ping_ok_ms = 0;
    uint32_t ping_rtt_ms = 0;
    uint16_t consecutive_failures = 0;
    bool last_ping_ok = false;
    char last_result[24] = "INIT";
    uint32_t health_transition_ms = 0;
    bool is_healthy = false;
};
static NodeLivenessRecord g_node_liveness[RF_PRODUCTION_MAX_NODE_ID + 1] = {};

static bool g_agu_bus_busy = false;
static bool g_rf_bus_locked = false;

struct StaggeredNodeScheduleState
{
    int64_t target_off_us = 0;    // Monotonic timestamp when node must turn OFF (esp_timer_get_time)
    bool is_scheduled_on = false; // Node is currently spraying under schedule
    uint8_t off_retries = 0;      // Retries for PUMP_OFF if not ACKed
};
static StaggeredNodeScheduleState g_staggered_schedule[RF_PRODUCTION_MAX_NODE_ID + 1] = {};

struct GroupSprayRuntime
{
    bool is_spraying = false;
    int64_t target_off_us = 0;
    uint32_t spray_duration_s = 30;
    GroupPhase last_phase = GroupPhase::PHASE_COOLING_DOWN;
    bool phase_initialized = false;
};
static GroupSprayRuntime g_group_spray_state[MAX_TIMER_GROUPS + 1] = {};
static void resetScheduleEdgeState(uint8_t logical_group);

static void resetScheduleEdgeState(uint8_t logical_group)
{
    if (logical_group > MAX_TIMER_GROUPS)
        return;
    g_group_spray_state[logical_group].last_phase = GroupPhase::PHASE_COOLING_DOWN;
    g_group_spray_state[logical_group].phase_initialized = false;
}

static void resetAllScheduleEdgeStates()
{
    for (uint8_t group_id = 1; group_id <= MAX_TIMER_GROUPS; ++group_id)
    {
        resetScheduleEdgeState(group_id);
    }
}
static void executeGroupPump(uint8_t logical_group, bool turn_on, uint32_t duration_sec = 30);
static esp_timer_handle_t g_group_cutoff_timer[MAX_TIMER_GROUPS + 1] = {nullptr};
static void onGroupCutoffTimer(void *arg)
{
    uint8_t grp_id = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(arg));
    if (grp_id >= 1 && grp_id <= MAX_TIMER_GROUPS)
    {
        ESP_LOGI(TAG, "[TIMER ISR] Group %u spray duration met (exact hardware timer cutoff)", (unsigned)grp_id);

        // Đặt cờ ngắt ngay lập tức để serviceScheduleTick (backup cutoff) không phát lệnh lần 2
        const bool was_spraying = g_group_spray_state[grp_id].is_spraying;
        g_group_spray_state[grp_id].is_spraying = false;

        if (was_spraying)
        {
            executeGroupPump(grp_id, false);
        }
    }
}

// Periodic AGU liveness uses PING (0x05). Production hardware test confirms
// that deployed legacy nodes (nodes 8, 10, etc.) respond with 0xA5 in ~37ms
// when RF UART is configured at 38400 baud 8N2.
// Enabled by default with time-sliced round-robin probing.
static bool g_agu_liveness_enabled = true;
static const char *g_reset_reason_str = "POWERON";

// Forward declaration of helper functions
static bool isWifiProvisioned();
static bool configureTaskWdt();
static bool setupMainWdt();
static void initializeNvs();
static bool provisionRfBoundary(RfHardwareConfig &config);
static bool initializeRfTransport(const RfHardwareConfig &config);
static void initializeRtc();
static bool initializeGatewayCore();
static bool provisionAutonomousSchedules();
static bool initializeRfControlBoundary();
static bool initializeNetworkTelemetry();
static void enterDegradedSafeState(const char *reason);
static void connectWifiWithTimeout();
static bool initializeMqtt();
static bool createMqttTask();
static void serviceRfRx(uint32_t current_time_ms);
static void serviceCommandFanoutTick(uint32_t current_time_ms);
static void serviceStaleEvaluationTick(uint32_t current_time_ms);
static void serviceFsmTick(uint32_t current_time_ms);
static void servicePollTelemetry(uint32_t current_time_ms);
static void updateNodeEvidenceFromTelemetry(uint8_t node_id, const uint8_t ram_data[8], uint32_t current_ms);
static void publishNodeLifecycleEvent(uint8_t node_id, LifecycleEvent event);
static void serviceAguLivenessTick(uint32_t current_ms);
static void publishLegacyNodeSnapshot(uint8_t node_id, const char *source = nullptr, const char *transition_reason = nullptr);
static void processSerialCommands();
static void handleCommand(const char *cmd);
static void executeRfScan(const char *scan_id);

static void executeRfClaimNode(uint8_t from_id, uint8_t to_id, const char *command_id);
static bool executeAguPump(uint8_t node_id, bool turn_on, const char *command_id = nullptr, const char *source = nullptr);
static bool executeAguPing(uint8_t node_id, bool ignore_bus_lock = false, bool *was_deferred = nullptr);
static void onGatewayCommand(const MqttInboundCommand &command);
static void onControlSlotsConfig(const JsonDocument &doc);
static void handleFactoryResetConfirmation(const char *cmd);
static void printSystemStatus();
static void printWifiStatus();
static void runSystemDiagnostics();
static void runRfUartDiagnostic(bool loopback);
static void mqttTask(void *pvParameters);

#if AUTONOMOUS_FALLBACK_ENABLED
static constexpr char AUTONOMOUS_NVS_VERSION_KEY[] = "auto_cfg_v";

// Provisioning runs on the Arduino loop task, which setupMainWdt() registers
// with the Task WDT. Feed it between blocking NVS commits so a first-boot
// provisioning of five records cannot trip the watchdog.
static void resetAutonomousProvisioningWdt()
{
#if defined(ESP_PLATFORM)
    // initializeGatewayCore() runs before initializeNetworkTelemetry(), so the
    // main task may not be subscribed yet on first boot.
    if (g_wdt_registered)
        esp_task_wdt_reset();
#endif
}

static const uint8_t *autonomousTargetNodes()
{
    static const uint8_t target_nodes[4] = {
        static_cast<uint8_t>(TARGET_ACTIVE_NODE_1),
        static_cast<uint8_t>(TARGET_ACTIVE_NODE_2),
        static_cast<uint8_t>(TARGET_ACTIVE_NODE_3),
        static_cast<uint8_t>(TARGET_ACTIVE_NODE_4)};
    return target_nodes;
}

static bool autonomousConfigValid(const GroupProfile (&profiles)[MAX_TIMER_GROUPS])
{
    for (const GroupProfile &profile : profiles)
    {
        if (!profile.isValid())
            return false;
    }
    if (TARGET_ACTIVE_GROUP_ID < 1 || TARGET_ACTIVE_GROUP_ID > MAX_TIMER_GROUPS)
        return false;
#if SELECTED_OPERATION_MODE == OP_MODE_NODE
    const uint8_t *target_nodes = autonomousTargetNodes();
    for (uint8_t i = 0; i < MAX_TIMER_GROUPS; ++i)
    {
        if (target_nodes[i] > AGU_LEGACY_MAX_NODE_ID)
            return false;
        for (uint8_t j = static_cast<uint8_t>(i + 1); j < MAX_TIMER_GROUPS; ++j)
        {
            if (target_nodes[i] != 0 && target_nodes[i] == target_nodes[j])
                return false;
        }
    }
#endif
    return true;
}

// Each logical group is also the RF actuation slot for its target, so a group
// can be active only when the target it drives is actually provisioned.
static bool autonomousGroupIsSelected(uint8_t group_id)
{
#if SELECTED_OPERATION_MODE == OP_MODE_GROUP
    return group_id == TARGET_ACTIVE_GROUP_ID;
#else
    const uint8_t *target_nodes = autonomousTargetNodes();
    return target_nodes[group_id - 1] > 0;
#endif
}

static uint8_t autonomousNodeForGroup(uint8_t group_id)
{
#if SELECTED_OPERATION_MODE == OP_MODE_NODE
    if (group_id < 1 || group_id > MAX_TIMER_GROUPS)
        return 0;
    return autonomousTargetNodes()[group_id - 1];
#else
    (void)group_id;
    return 0;
#endif
}

static void applyAutonomousNodeMapping()
{
    for (uint8_t node_id = 1; node_id <= AGU_LEGACY_MAX_NODE_ID; ++node_id)
    {
#if SELECTED_OPERATION_MODE == OP_MODE_NODE
        uint8_t target_group = 0;
        for (uint8_t slot = 0; slot < MAX_TIMER_GROUPS; ++slot)
        {
            if (autonomousTargetNodes()[slot] == node_id)
                target_group = static_cast<uint8_t>(slot + 1);
        }
        g_node_registry.assignNodeToGroup(node_id, target_group);
#else
        g_node_registry.assignNodeToGroup(node_id,
                                          static_cast<uint8_t>(node_id <= 3 ? 1 : node_id <= 7 ? 2
                                                                              : node_id <= 11  ? 3
                                                                                               : 4));
#endif
    }
}

static void applyAutonomousHmiSlots()
{
    memset(g_hmi_slot_targets, 0, sizeof(g_hmi_slot_targets));
    memset(g_pending_hmi_slot_targets, 0, sizeof(g_pending_hmi_slot_targets));
#if SELECTED_OPERATION_MODE == OP_MODE_GROUP
    const uint8_t slot_index = static_cast<uint8_t>(TARGET_ACTIVE_GROUP_ID - 1);
    g_hmi_slot_targets[slot_index].type = HmiTargetType::GROUP;
    g_hmi_slot_targets[slot_index].id = static_cast<uint8_t>(TARGET_ACTIVE_GROUP_ID);
#else
    for (uint8_t slot = 0; slot < MAX_TIMER_GROUPS; ++slot)
    {
        const uint8_t node_id = autonomousTargetNodes()[slot];
        if (node_id == 0)
            continue;
        g_hmi_slot_targets[slot].type = HmiTargetType::NODE;
        g_hmi_slot_targets[slot].id = node_id;
    }
#endif
}

/**
 * @brief Provision compile-time autonomous schedules into NVS exactly once per
 * AUTONOMOUS_NVS_CONFIG_VERSION, then grant local control authority in RAM.
 *
 * Ordering contract: cutoff timers already exist, NVS records are verified
 * before the version key is written, and every group is authorized directly in
 * PHASE_COOLING_DOWN so power-on never produces a spray inrush.
 */
static bool provisionAutonomousSchedules()
{
    GroupProfile profiles[MAX_TIMER_GROUPS] = {
        GroupProfile(NT1_SPRAY_DAY_S, NT1_COOLDOWN_DAY_S, NT1_SPRAY_NIGHT_S, NT1_COOLDOWN_NIGHT_S),
        GroupProfile(NT2_SPRAY_DAY_S, NT2_COOLDOWN_DAY_S, NT2_SPRAY_NIGHT_S, NT2_COOLDOWN_NIGHT_S),
        GroupProfile(NT3_SPRAY_DAY_S, NT3_COOLDOWN_DAY_S, NT3_SPRAY_NIGHT_S, NT3_COOLDOWN_NIGHT_S),
        GroupProfile(NT4_SPRAY_DAY_S, NT4_COOLDOWN_DAY_S, NT4_SPRAY_NIGHT_S, NT4_COOLDOWN_NIGHT_S)};
    if (!autonomousConfigValid(profiles))
    {
        ESP_LOGE(TAG, "[AUTONOMOUS] Invalid compile-time configuration; groups stay safe-OFF.");
        return false;
    }
    if (!g_nvs_storage.isInitialized())
    {
        ESP_LOGE(TAG, "[AUTONOMOUS] NVS unavailable; groups stay safe-OFF.");
        return false;
    }

    uint32_t stored_version = 0;
    const bool version_current =
        g_nvs_storage.getU32(AUTONOMOUS_NVS_VERSION_KEY, stored_version) &&
        stored_version == AUTONOMOUS_NVS_CONFIG_VERSION;

    if (!version_current)
    {
        ESP_LOGW(TAG, "[AUTONOMOUS] Provisioning schedules version %u into NVS...",
                 static_cast<unsigned>(AUTONOMOUS_NVS_CONFIG_VERSION));
        applyAutonomousNodeMapping();
        if (!g_group_scheduler.persistNodeAssignments())
        {
            ESP_LOGE(TAG, "[AUTONOMOUS] Node assignment persist failed; version not advanced.");
            return false;
        }
        resetAutonomousProvisioningWdt();

        for (uint8_t gid = 1; gid <= MAX_TIMER_GROUPS; ++gid)
        {
            PublishedTreatmentAssignment assignment{};
            assignment.season_id = AUTONOMOUS_NVS_CONFIG_VERSION;
            assignment.treatment_version_id = AUTONOMOUS_NVS_CONFIG_VERSION;
            assignment.version = 1;
            assignment.profile = profiles[gid - 1];

            const bool selected = autonomousGroupIsSelected(gid);
            if (!selected)
            {
                // Inactive slots still need a valid record so a later boot cannot
                // resurrect a half-provisioned group from a stale blob.
                if (!g_group_scheduler.setGroupProfile(gid, profiles[gid - 1]) ||
                    !g_group_scheduler.unassignGroup(gid, "AUTONOMOUS_INACTIVE"))
                {
                    ESP_LOGE(TAG, "[AUTONOMOUS] Failed to park inactive group %u", gid);
                    return false;
                }
                resetAutonomousProvisioningWdt();
                continue;
            }
            if (!g_group_scheduler.setGroupProfile(gid, profiles[gid - 1]) ||
                !g_group_scheduler.authorizeAndActivateWithSafeCooldown(gid, assignment))
            {
                ESP_LOGE(TAG, "[AUTONOMOUS] Failed to activate group %u in safe cooldown", gid);
                return false;
            }
            resetAutonomousProvisioningWdt();
        }

        if (!g_nvs_storage.setU32(AUTONOMOUS_NVS_VERSION_KEY, AUTONOMOUS_NVS_CONFIG_VERSION))
        {
            ESP_LOGE(TAG, "[AUTONOMOUS] Failed to persist config version; will retry next boot.");
            return false;
        }
        resetAutonomousProvisioningWdt();
    }
    else
    {
        ESP_LOGI(TAG, "[AUTONOMOUS] NVS schedules already at version %u; skipping NVS writes.",
                 static_cast<unsigned>(stored_version));
    }

    // Grant local authority in RAM. Re-applying activation is idempotent and is
    // required when the previous boot stopped before the version key was written.
    applyAutonomousHmiSlots();
    for (uint8_t gid = 1; gid <= MAX_TIMER_GROUPS; ++gid)
    {
        if (!autonomousGroupIsSelected(gid))
            continue;
        GroupRuntimeState state{};
        if (!g_group_scheduler.getGroupRuntimeState(gid, state) ||
            state.assignment_state != GroupAssignmentState::ACTIVE)
        {
            PublishedTreatmentAssignment assignment{};
            assignment.season_id = AUTONOMOUS_NVS_CONFIG_VERSION;
            assignment.treatment_version_id = AUTONOMOUS_NVS_CONFIG_VERSION;
            assignment.version = 1;
            assignment.profile = profiles[gid - 1];
            if (!g_group_scheduler.setGroupProfile(gid, profiles[gid - 1]) ||
                !g_group_scheduler.authorizeAndActivateWithSafeCooldown(gid, assignment))
            {
                ESP_LOGE(TAG, "[AUTONOMOUS] Failed to restore authority for group %u", gid);
                return false;
            }
            resetAutonomousProvisioningWdt();
        }
    }

#if SELECTED_OPERATION_MODE == OP_MODE_GROUP
    ESP_LOGI(TAG, "[AUTONOMOUS] MODE=GROUP target=Group %u (RF 0x%02X)",
             static_cast<unsigned>(TARGET_ACTIVE_GROUP_ID),
             static_cast<unsigned>(rfGroupIdFromLogical(TARGET_ACTIVE_GROUP_ID)));
#else
    for (uint8_t slot = 0; slot < MAX_TIMER_GROUPS; ++slot)
    {
        const uint8_t node_id = autonomousTargetNodes()[slot];
        if (node_id == 0)
            continue;
        ESP_LOGI(TAG, "[AUTONOMOUS] MODE=NODE slot %u -> Node %u (profile %u)",
                 static_cast<unsigned>(slot + 1), static_cast<unsigned>(node_id),
                 static_cast<unsigned>(slot + 1));
    }
#endif

    g_control_slots_reconciled = true;
    return true;
}

#endif // AUTONOMOUS_FALLBACK_ENABLED

static bool registerMqttTaskWdt();
static bool resetMqttTaskWdt();
static bool attemptMqttReconnect(MqttTaskState &state, uint32_t now);
static void serviceConnectedMqtt(MqttTaskState &state, uint32_t now);
static void serviceMqttIteration(MqttTaskState &state);

static bool isWifiProvisioned()
{
    return g_wifi_storage.isProvisioned();
}

static bool configureTaskWdt()
{
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    esp_task_wdt_config_t twdt_config = {
        .timeout_ms = WDT_TIMEOUT_MS,
        .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,
        .trigger_panic = true};
    esp_err_t err = esp_task_wdt_init(&twdt_config);
    if (err == ESP_ERR_INVALID_STATE)
    {
        err = esp_task_wdt_reconfigure(&twdt_config);
    }
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to configure Task WDT: %s (0x%x)", esp_err_to_name(err), err);
        return false;
    }
#else
    esp_err_t err = esp_task_wdt_init(WDT_TIMEOUT_S, true);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE(TAG, "Failed to init Task WDT: %s (0x%x)", esp_err_to_name(err), err);
        return false;
    }
#endif
    ESP_LOGI(TAG, "Task WDT configured/reconfigured with timeout %u s", WDT_TIMEOUT_S);
    return true;
}

static bool setupMainWdt()
{
    bool wdt_ok = configureTaskWdt();
    if (!wdt_ok)
    {
        return false;
    }

    esp_err_t add_err = esp_task_wdt_add(NULL);
    bool is_added = false;

    if (add_err == ESP_OK)
    {
        is_added = true;
    }
    else if (add_err == ESP_ERR_INVALID_STATE)
    {
        esp_err_t stat_err = esp_task_wdt_status(NULL);
        if (stat_err == ESP_OK)
        {
            is_added = true;
            ESP_LOGI(TAG, "Main loop task was already subscribed to Task WDT.");
        }
        else
        {
            ESP_LOGE(TAG, "esp_task_wdt_add returned INVALID_STATE and status verification failed (0x%x) for main loop task", stat_err);
        }
    }
    else
    {
        ESP_LOGE(TAG, "esp_task_wdt_add failed for main loop task: 0x%x", add_err);
    }

    if (!is_added)
    {
        g_wdt_registered = false;
        return false;
    }

    esp_err_t reset_err = esp_task_wdt_reset();
    if (reset_err != ESP_OK)
    {
        ESP_LOGE(TAG, "Initial esp_task_wdt_reset verification failed for main loop task: 0x%x", reset_err);
        esp_task_wdt_delete(NULL);
        g_wdt_registered = false;
        return false;
    }

    g_wdt_registered = true;
    ESP_LOGI(TAG, "Main loop task registered and verified with Task WDT successfully.");
    return true;
}

static void provisionDefaultRfConfigIfMissing()
{
    // Provision default RF config from config.h if NVS doesn't have it yet
    // This allows gateway to boot successfully even without prior RF provisioning
    if (!g_rf_nvs_storage.begin())
    {
        ESP_LOGW(TAG, "RF NVS storage init failed; using compile-time defaults only");
        return;
    }

    uint32_t uart_num = 0;
    bool has_config = g_rf_nvs_storage.getU32(RF_NVS_UART_NUM_KEY, uart_num);

    if (!has_config)
    {
        ESP_LOGI(TAG, "RF config not found in NVS; provisioning defaults from config.h...");
        g_rf_nvs_storage.setU32(RF_NVS_UART_NUM_KEY, RF_DEFAULT_UART_NUM);
        g_rf_nvs_storage.setU32(RF_NVS_UART_TX_PIN_KEY, RF_DEFAULT_TX_PIN);
        g_rf_nvs_storage.setU32(RF_NVS_UART_RX_PIN_KEY, RF_DEFAULT_RX_PIN);
        g_rf_nvs_storage.setU32(RF_NVS_UART_BAUD_KEY, RF_DEFAULT_BAUD_RATE);
        g_rf_nvs_storage.setU32(RF_NVS_UART_M0_PIN_KEY,
                                RF_DEFAULT_M0_PIN >= 0 ? RF_DEFAULT_M0_PIN : 255);
        g_rf_nvs_storage.setU32(RF_NVS_UART_M1_PIN_KEY,
                                RF_DEFAULT_M1_PIN >= 0 ? RF_DEFAULT_M1_PIN : 255);
        g_rf_nvs_storage.setU32(RF_NVS_UART_AUX_PIN_KEY,
                                RF_DEFAULT_AUX_PIN >= 0 ? RF_DEFAULT_AUX_PIN : 255);
        ESP_LOGI(TAG, "RF default config provisioned: UART%u TX=%d RX=%d BAUD=%u",
                 RF_DEFAULT_UART_NUM, RF_DEFAULT_TX_PIN, RF_DEFAULT_RX_PIN, RF_DEFAULT_BAUD_RATE);
    }

    uint32_t session = 0;
    if (!g_rf_nvs_storage.getU32(RF_NVS_BOOT_SESSION_KEY, session) || session == 0)
    {
        g_rf_nvs_storage.setU32(RF_NVS_BOOT_SESSION_KEY, 1);
        for (size_t i = 0; i < 4; ++i)
        {
            uint32_t word = 0;
            if (!g_rf_nvs_storage.getU32(RF_NVS_PSK_WORD_KEYS[i], word))
            {
#if defined(ESP_PLATFORM)
                word = esp_random();
#else
                word = 0x11223344 + static_cast<uint32_t>(i);
#endif
                g_rf_nvs_storage.setU32(RF_NVS_PSK_WORD_KEYS[i], word);
            }
        }
        ESP_LOGI(TAG, "RF session & security key provisioned in NVS.");
    }
}

static void initializeNvs()
{
    bool nvs_ok = g_nvs_storage.begin();
    if (!nvs_ok)
    {
        ESP_LOGW(TAG, "NVS storage init failed. Gateway operating with default configuration.");
    }
    else
    {
        ESP_LOGI(TAG, "NVS storage initialized successfully.");
    }
    if (!g_clock_nvs_storage.begin())
    {
        ESP_LOGW(TAG, "Clock NVS init failed; backend time will not persist across reboots.");
    }
    g_wifi_storage.begin();

    // Provision default RF hardware config if not already provisioned
    provisionDefaultRfConfigIfMissing();
}

static bool provisionRfBoundary(RfHardwareConfig &config)
{
#if !defined(UNIT_TEST_HOST)
    if (!RF_PROVISIONING_INDEPENDENT_SIGNOFF_PRESENT)
    {
        ESP_LOGE(TAG, "RF production provisioning lacks independent security sign-off; gateway remains fail-closed");
        return false;
    }
#endif
    uint32_t uart_num = 0, tx_pin = 0, rx_pin = 0;
    if (!g_rf_nvs_storage.begin() || !g_rf_nvs_storage.getU32(RF_NVS_UART_NUM_KEY, uart_num) ||
        !g_rf_nvs_storage.getU32(RF_NVS_UART_TX_PIN_KEY, tx_pin) ||
        !g_rf_nvs_storage.getU32(RF_NVS_UART_RX_PIN_KEY, rx_pin) ||
        !g_rf_nvs_storage.getU32(RF_NVS_UART_BAUD_KEY, config.baud_rate) ||
        uart_num > 2 || tx_pin > 127 || rx_pin > 127)
    {
        ESP_LOGE(TAG, "RF provisioning absent or invalid; gateway remains fail-closed");
        return false;
    }
    config.uart_num = static_cast<uint8_t>(uart_num);
    config.tx_pin = static_cast<int8_t>(tx_pin);
    config.rx_pin = static_cast<int8_t>(rx_pin);

    uint32_t m0_pin = 0, m1_pin = 0, aux_pin = 0;
    if (g_rf_nvs_storage.getU32(RF_NVS_UART_M0_PIN_KEY, m0_pin) && m0_pin <= 127)
    {
        config.m0_pin = static_cast<int8_t>(m0_pin);
    }
    if (g_rf_nvs_storage.getU32(RF_NVS_UART_M1_PIN_KEY, m1_pin) && m1_pin <= 127)
    {
        config.m1_pin = static_cast<int8_t>(m1_pin);
    }
    if (g_rf_nvs_storage.getU32(RF_NVS_UART_AUX_PIN_KEY, aux_pin) && aux_pin <= 127)
    {
        config.aux_pin = static_cast<int8_t>(aux_pin);
    }

    if (!config.isValid())
        return false;
    return g_command_manager.provisionFromNvs(g_rf_nvs_storage);
}

struct RfRxBuffer
{
    uint8_t bytes[256] = {};
    size_t length = 0;
    uint32_t last_byte_ms = 0;
};

static void expirePartialRfFrame(RfRxBuffer &buffer, uint32_t now)
{
    if (buffer.length > 0 && now - buffer.last_byte_ms > RF_INTER_BYTE_TIMEOUT_MS)
        buffer.length = 0;
}

static void readRfBytes(RfRxBuffer &buffer, uint32_t now)
{
    if (!g_rf_transport || buffer.length == sizeof(buffer.bytes) || g_rf_transport->available() == 0)
        return;
    const size_t read = g_rf_transport->receive(buffer.bytes + buffer.length, sizeof(buffer.bytes) - buffer.length);
    buffer.length += read;
    if (read > 0)
        buffer.last_byte_ms = now;
}

static bool discardUntilSof(RfRxBuffer &buffer)
{
    for (size_t i = 0; i + 1 < buffer.length; ++i)
    {
        if (buffer.bytes[i] == RF_SOF_BYTE_1 && buffer.bytes[i + 1] == RF_SOF_BYTE_2)
        {
            if (i > 0)
                std::memmove(buffer.bytes, buffer.bytes + i, buffer.length - i);
            buffer.length -= i;
            return true;
        }
    }
    buffer.length = 0;
    return false;
}

static void processAvailableRfFrames(RfRxBuffer &buffer, uint32_t now)
{
    for (size_t processed = 0; processed < 8; ++processed)
    {
        if (buffer.length < RF_HEADER_SIZE + HMAC_TAG_SIZE + 2 || !discardUntilSof(buffer) || buffer.length < RF_HEADER_SIZE)
            return;
        const uint8_t payload_len = buffer.bytes[RF_HEADER_PAYLOAD_LENGTH_OFFSET];
        if (payload_len > 64)
        {
            std::memmove(buffer.bytes, buffer.bytes + 2, buffer.length - 2);
            buffer.length -= 2;
            continue;
        }
        const size_t frame_len = RF_HEADER_SIZE + payload_len + HMAC_TAG_SIZE + 2;
        if (buffer.length < frame_len)
            return;
        g_command_manager.handleIncomingFrame(buffer.bytes, frame_len, now);
        std::memmove(buffer.bytes, buffer.bytes + frame_len, buffer.length - frame_len);
        buffer.length -= frame_len;
    }
}

static bool initializeRfTransport(const RfHardwareConfig &config)
{
    static UartRfTransport uart(config.uart_num, config.rx_pin, config.tx_pin,
                                (config.baud_rate > 0 ? config.baud_rate : RF_UART_HC12_BAUD_RATE),
                                UART_RF_DEFAULT_RX_BUFFER_CAPACITY, config.m0_pin, config.m1_pin, config.aux_pin);
    if (!uart.begin())
        return false;
    if (!uart.startRxTask())
    {
        ESP_LOGE(TAG, "Failed to start RF UART RX task on Core 1");
        return false;
    }
    g_rf_transport = &uart;
    static AguLegacyRfHost legacy_host(g_rf_transport);
    g_agu_legacy_host = &legacy_host;
    return g_command_manager.begin(&g_node_registry, g_rf_transport);
}

/**
 * Persist the last backend-authoritative reference so a gateway that boots
 * offline with a dead DS1307 cell still has a plausible (if stale) clock
 * instead of dropping straight to safe-OFF. Only called after a successful
 * GATEWAY_CLOCK apply.
 */
static void persistBackendClock(int64_t unix_time_utc, int32_t tz_offset_s)
{
    if (!g_clock_nvs_storage.isInitialized())
    {
        ESP_LOGW(TAG, "Clock NVS unavailable; backend time not persisted across reboot.");
        return;
    }
    if (unix_time_utc < CLOCK_UNIX_TIME_MIN_VALID || unix_time_utc > CLOCK_UNIX_TIME_MAX_VALID)
    {
        ESP_LOGW(TAG, "Refusing to persist implausible backend epoch %lld.",
                 static_cast<long long>(unix_time_utc));
        return;
    }
    const uint32_t magic = CLOCK_NVS_RECORD_VERSION;
    g_clock_nvs_storage.setU32(NVS_KEY_CLOCK_MAGIC, magic);
    g_clock_nvs_storage.setU32(NVS_KEY_CLOCK_UNIX, static_cast<uint32_t>(unix_time_utc));
    g_clock_nvs_storage.setU32(NVS_KEY_CLOCK_TZ_OFFSET, static_cast<uint32_t>(tz_offset_s));
    ESP_LOGI(TAG, "Persisted backend clock reference epoch=%lld tz=%d.",
             static_cast<long long>(unix_time_utc), static_cast<int>(tz_offset_s));
}

/**
 * Restore the last backend reference when the DS1307 lost power and no NTP is
 * reachable yet. The restored value is deliberately treated as a bounded
 * fallback: it seeds the hardware clock so schedules stay deterministic.
 */
static void restorePersistedClockIfUntrusted()
{
    if (g_rtc_manager.isHardwarePresent() && !g_rtc_manager.hasPowerLoss())
        return;
    if (!g_clock_nvs_storage.isInitialized())
        return;

    uint32_t magic = 0;
    uint32_t unix_time = 0;
    uint32_t tz_offset = 0;
    if (!g_clock_nvs_storage.getU32(NVS_KEY_CLOCK_MAGIC, magic) || magic != CLOCK_NVS_RECORD_VERSION ||
        !g_clock_nvs_storage.getU32(NVS_KEY_CLOCK_UNIX, unix_time) ||
        !g_clock_nvs_storage.getU32(NVS_KEY_CLOCK_TZ_OFFSET, tz_offset))
    {
        ESP_LOGW(TAG, "No valid persisted clock reference available for restore.");
        return;
    }
    if (unix_time < CLOCK_UNIX_TIME_MIN_VALID || unix_time > CLOCK_UNIX_TIME_MAX_VALID)
    {
        ESP_LOGW(TAG, "Persisted clock reference %lu is implausible; ignoring.", static_cast<unsigned long>(unix_time));
        return;
    }
    if (g_rtc_manager.applyUtcClockFromBackend(static_cast<int64_t>(unix_time),
                                               static_cast<int32_t>(tz_offset)))
    {
        ESP_LOGW(TAG, "Persisted backend reference restored to system clock (stale but plausible).");
    }
}

/** Apply a backend GATEWAY_CLOCK push with deadband and persistence protection. */
static void applyBackendClock(int64_t unix_time_utc, int32_t tz_offset_s)
{
    const int64_t current_posix = static_cast<int64_t>(time(nullptr));
    const int64_t drift_s = std::abs(current_posix - unix_time_utc);

    static uint32_t s_last_backend_sync_ms = 0;
    static int32_t s_last_persisted_tz = 0;
    const uint32_t now_ms = millis();

    constexpr int64_t CLOCK_DRIFT_THRESHOLD_S = 2;               // Bỏ qua nếu lệch <= 2 giây
    constexpr uint32_t MIN_BACKEND_SYNC_INTERVAL_MS = 3600000UL; // Tối thiểu 1 giờ giữa các lần sync

    // 1. Kiểm tra xem đồng hồ hiện tại đã chuẩn chưa
    const bool clock_already_valid = (drift_s <= CLOCK_DRIFT_THRESHOLD_S);

    // 2. Cooldown đang có hiệu lực (đã sync cách đây chưa quá 1 giờ)
    const bool cooldown_active = (s_last_backend_sync_ms != 0 && (now_ms - s_last_backend_sync_ms < MIN_BACKEND_SYNC_INTERVAL_MS));

    // BỎ QUA nếu:
    // - Lần sync trước cách đây chưa quá 1 giờ (bất kể lệch bao nhiêu nếu cùng múi giờ)
    // - HOẶC giờ hiện tại đã khớp (drift <= 2s), kể cả khi Gateway vừa mới boot (s_last_backend_sync_ms == 0)
    if ((clock_already_valid && (s_last_persisted_tz == 0 || tz_offset_s == s_last_persisted_tz)) ||
        (cooldown_active && tz_offset_s == s_last_persisted_tz))
    {
        ESP_LOGD(TAG, "Backend clock ignored (within deadband: drift=%llds, last_sync=%u ms ago).",
                 static_cast<long long>(drift_s), now_ms - s_last_backend_sync_ms);

        // Cập nhật lại mốc để duy trì cooldown
        s_last_backend_sync_ms = now_ms;
        s_last_persisted_tz = tz_offset_s;
        return;
    }

    // 3. Chỉ thực hiện khi sai số > 2 giây hoặc múi giờ thay đổi
    if (g_rtc_manager.applyUtcClockFromBackend(unix_time_utc, tz_offset_s))
    {
        resetAllScheduleEdgeStates();
        ESP_LOGI(TAG, "Backend clock applied to system clock: epoch=%lld tz=%d (drift was %llds).",
                 static_cast<long long>(unix_time_utc), static_cast<int>(tz_offset_s),
                 static_cast<long long>(drift_s));

        // CHỈ ghi NVS khi áp dụng mốc giờ mới thành công
        persistBackendClock(unix_time_utc, tz_offset_s);

        s_last_backend_sync_ms = now_ms;
        s_last_persisted_tz = tz_offset_s;
    }
    else
    {
        ESP_LOGW(TAG, "Backend clock rejected; runtime clock unchanged.");
    }
}

static void initializeRtc()
{
    // Internal epochs remain UTC; local calendar reads used by the scheduler
    // are always ICT (UTC+7), regardless of the active clock source.
    setenv("TZ", "ICT-7", 1);
    tzset();
    Wire.begin(RTC_SDA_PIN, RTC_SCL_PIN);
    bool rtc_ok = g_rtc_manager.begin();
    if (!rtc_ok)
    {
        ESP_LOGW(TAG, "DS1307 RTC init failed or hardware not detected; using system time fallback.");
    }
    // A DS1307 with a live coin cell keeps time across resets. Only fall back
    // to the persisted backend reference when the oscillator actually stopped.
    restorePersistedClockIfUntrusted();
}

static void connectWifiWithTimeout()
{
    if (!isWifiProvisioned())
    {
        ESP_LOGW(TAG, "Wi-Fi not provisioned; Farmer Portal auto-starts on AP '%s' (http://192.168.4.1).", PORTAL_AP_SSID);
        return;
    }

    // WiFi controller runs async on Core 0; allow ample time for scan+connect
    const uint32_t wifi_wait_ms = 20000; // Increased from 6000ms to allow async scan (~10s) + connect (~5-10s)
    ESP_LOGI(TAG, "Waiting for Wi-Fi connection (max %u ms)...", wifi_wait_ms);

    uint32_t wifi_start_ms = millis();
    while (!g_wifi_controller.isConnected() && (millis() - wifi_start_ms < wifi_wait_ms))
    {
        if (g_wdt_registered)
        {
            esp_task_wdt_reset();
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    bool wifi_connected = g_wifi_controller.isConnected();
    uint32_t elapsed_ms = millis() - wifi_start_ms;
    if (wifi_connected)
    {
        ESP_LOGI(TAG, "Wi-Fi connected in %u ms.", (unsigned)elapsed_ms);
        bool ntp_ok = g_rtc_manager.syncFromNtp();
        if (ntp_ok)
        {
            ESP_LOGI(TAG, "NTP sync completed and RTC updated.");
        }
        else
        {
            ESP_LOGW(TAG, "NTP sync failed or timed out; relying on RTC clock.");
        }
    }
    else
    {
        ESP_LOGW(TAG, "Wi-Fi still connecting after %u ms; running offline for now.", (unsigned)elapsed_ms);
    }
}

static bool registerMqttTaskWdt()
{
#if defined(ESP_PLATFORM)
    const esp_err_t add_err = esp_task_wdt_add(NULL);
    if (add_err != ESP_OK)
    {
        ESP_LOGE(TAG, "MQTT task WDT registration failed: 0x%x", add_err);
        return false;
    }
#endif
    return true;
}

static bool resetMqttTaskWdt()
{
#if defined(ESP_PLATFORM)
    const esp_err_t reset_err = esp_task_wdt_reset();
    if (reset_err != ESP_OK)
    {
        ESP_LOGE(TAG, "MQTT task WDT reset failed: 0x%x", reset_err);
        esp_task_wdt_delete(NULL);
        return false;
    }
#endif
    return true;
}

static bool attemptMqttReconnect(MqttTaskState &state, uint32_t now)
{
    if (!mqttReconnectDue(state, now))
        return false;
    mqttRecordReconnectAttempt(state, now);
    if (mqtt_client.connect())
    {
        mqttRecordReconnectSuccess(state, now);
        return true;
    }
    mqttRecordReconnectFailure(state);
    return false;
}

static void serviceConnectedMqtt(MqttTaskState &state, uint32_t now)
{
    state.was_connected = true;
    mqtt_client.loop();
    mqtt_client.serviceOutgoingEvents();
    mqttServiceHeartbeat(state, now, []()
                         { return mqtt_client.publishConnectedHeartbeat(); });
    mqtt_client.serviceOutgoingEvents();
}

static void serviceMqttIteration(MqttTaskState &state)
{
    if (WiFi.status() != WL_CONNECTED)
    {
        mqttRecordWifiLoss(state);
        return;
    }
    const uint32_t now = millis();
    if (!mqtt_client.isConnected())
    {
        if (state.was_connected)
            state.was_connected = false;
        attemptMqttReconnect(state, now);
        return;
    }
    serviceConnectedMqtt(state, now);
}

static void mqttTask(void *pvParameters)
{
    (void)pvParameters;
    if (!registerMqttTaskWdt())
    {
        vTaskDelete(NULL);
        return;
    }
    MqttTaskState state;
    for (;;)
    {
        if (!resetMqttTaskWdt())
        {
            vTaskDelete(NULL);
            return;
        }
        serviceMqttIteration(state);
        vTaskDelay(pdMS_TO_TICKS(MQTT_TASK_TICK_INTERVAL_MS));
    }
}

static bool initializeMqtt()
{
    mqtt_config = MqttConfigProvider::load();
    if (!mqtt_config.broker_host || !mqtt_config.device_id ||
        mqtt_config.broker_host[0] == '\0' || mqtt_config.device_id[0] == '\0')
    {
        ESP_LOGW(TAG, "MQTT config is not provisioned; MQTT gateway task remains disabled.");
        return false;
    }
    bool ok = mqtt_client.begin(mqtt_config, &g_rtc_manager, &g_node_registry, &g_command_manager,
                                &g_group_scheduler);
    if (ok)
    {
        mqtt_client.setGatewayCommandHandler(onGatewayCommand);
        mqtt_client.setClockAdjustHandler(applyBackendClock);
        mqtt_client.setControlSlotsHandler(onControlSlotsConfig);
        mqtt_client.setScheduleStateProvider(provideScheduleState);
        mqtt_client.setTimeTelemetry(&g_rtc_manager);
    }
    return ok;
}

static void onControlSlotsConfig(const JsonDocument &doc)
{
#if AUTONOMOUS_FALLBACK_ENABLED && FALLBACK_LOCK_FROM_MQTT_OVERWRITE
    (void)doc;
    ESP_LOGW(TAG, "[AUTONOMOUS] Ignored MQTT control-slot overwrite; local slots preserved.");
    g_control_slots_reconciled = true;
    return;
#endif
    JsonArrayConst slots = doc["slots"].as<JsonArrayConst>();
    if (slots.isNull())
        return;
    HmiSlotTarget parsed[4] = {};
    bool seen[4] = {};
    bool group_whitelisted[MAX_TIMER_GROUPS + 1] = {false};
    for (JsonObjectConst slot : slots)
    {
        const uint8_t idx = slot["idx"] | 0;
        if (idx < 1 || idx > 4 || seen[idx - 1])
            return;
        seen[idx - 1] = true;
        const char *type = slot["type"] | "";
        if (strcmp(type, "NODE") == 0 && slot["id"].is<uint8_t>())
        {
            const uint8_t id = slot["id"].as<uint8_t>();
            if (id < 1 || id > 15)
                return;
            parsed[idx - 1].type = HmiTargetType::NODE;
            parsed[idx - 1].id = id;
        }
        else if (strcmp(type, "GROUP") == 0 && slot["id"].is<uint8_t>())
        {
            const uint8_t id = slot["id"].as<uint8_t>();
            if (id < 1 || id > 4)
                return;
            parsed[idx - 1].type = HmiTargetType::GROUP;
            parsed[idx - 1].id = id;
            group_whitelisted[id] = true;
        }
        else if ((type[0] == '\0' || strcmp(type, "null") == 0) && slot["id"].isNull())
        {
            parsed[idx - 1] = {};
        }
        else
            return;
    }
    for (bool present : seen)
        if (!present)
            return;

    memcpy(g_pending_hmi_slot_targets, parsed, sizeof(parsed));
    g_hmi_slot_config_pending.store(true);

    ESP_LOGI(TAG, "[SLOT SYNC] Reconciling group authorization with control slots...");
    bool reconcile_ok = true;
    for (uint8_t g = 1; g <= MAX_TIMER_GROUPS; ++g)
    {
        if (!group_whitelisted[g])
        {
            GroupRuntimeState grp_state{};
            if (g_group_scheduler.getGroupRuntimeState(g, grp_state) &&
                grp_state.assignment_state != GroupAssignmentState::UNASSIGNED)
            {
                ESP_LOGW(TAG, "[SLOT SYNC] Group %u is not authorized by Control Slots; forcing safe-OFF and unassign.", g);
                if (g_group_spray_state[g].is_spraying)
                    executeGroupPump(g, false);
                if (g_group_cutoff_timer[g] != nullptr)
                    esp_timer_stop(g_group_cutoff_timer[g]);
                g_group_spray_state[g] = {};
                if (!g_group_scheduler.unassignGroup(g, "SLOT_UNASSIGN"))
                    reconcile_ok = false;
                resetScheduleEdgeState(g);
            }
        }
    }

    // Retained slot config is the authority gate; never open the gate on a
    // failed safe-off/NVS purge.
    g_control_slots_reconciled = reconcile_ok;
    mqtt_client.requestScheduleStatePublish();
    ESP_LOGI(TAG, "[SLOT SYNC] Control-slot reconcile %s", reconcile_ok ? "complete" : "FAILED");
}

static bool createMqttTask()
{
    const BaseType_t result = xTaskCreatePinnedToCore(
        mqttTask, MQTT_TASK_NAME, MQTT_TASK_STACK_SIZE, NULL,
        MQTT_TASK_PRIORITY, NULL, MQTT_TASK_CORE);
    if (result == pdPASS)
        return true;
    ESP_LOGE(TAG, "Failed to create MQTT FreeRTOS task (err: %d)!", static_cast<int>(result));
    return false;
}

static void serviceRfRx(uint32_t current_time_ms)
{
    static RfRxBuffer buffer;
    if (g_rf_transport == nullptr)
        return;

    // Legacy transactions synchronously own this UART. Do not feed legacy
    // response bytes into the RF_AUTH_V1 parser while compatibility mode is
    // active; RF_AUTH_V1 remains compiled for the migration path.
    if (g_agu_legacy_host != nullptr && !g_rf_raw_dump)
        return;

    // In raw hex dump mode, print received RF bytes immediately to Serial (rate-limited)
    if (g_rf_raw_dump && g_rf_transport->available() > 0)
    {
        static uint32_t last_raw_log_ms = 0;
        uint8_t raw[32];
        size_t r = g_rf_transport->receive(raw, sizeof(raw));
        if (r > 0 && (current_time_ms - last_raw_log_ms >= 50))
        {
            last_raw_log_ms = current_time_ms;
            char hex_buf[128] = {};
            size_t pos = 0;
            for (size_t i = 0; i < r && pos + 4 < sizeof(hex_buf); ++i)
            {
                pos += snprintf(hex_buf + pos, sizeof(hex_buf) - pos, "%02X ", raw[i]);
            }
            ESP_LOGI(TAG, "[RF_RAW RX %zu bytes]: %s", r, hex_buf);
        }
        vTaskDelay(pdMS_TO_TICKS(2));
        return;
    }

    if (!g_gateway_operational)
        return;
    expirePartialRfFrame(buffer, current_time_ms);
    readRfBytes(buffer, current_time_ms);
    processAvailableRfFrames(buffer, current_time_ms);
}

static void serviceCommandFanoutTick(uint32_t current_time_ms)
{
    if (!g_gateway_operational)
        return;
    // MQTT task only parses into its bounded queue. Main loop is the sole
    // owner of CommandManager mutation, correlation state and RF fan-out.
    mqtt_client.serviceIncomingCommands();
    if (g_agu_legacy_host != nullptr)
        return;
    if (current_time_ms - g_last_command_fanout_ms >= 100)
    {
        g_last_command_fanout_ms = current_time_ms;
        g_command_manager.serviceCommandFanout(current_time_ms);
    }
}

static void publishLegacyNodeSnapshot(uint8_t node_id, const char *source, const char *transition_reason)
{
    if (!isAguLegacyNodeId(node_id))
        return;
    NodeState st{};
    if (!g_node_registry.getNodeState(node_id, st))
        return;

    const NodeLivenessRecord &live = g_node_liveness[node_id];

    MqttClient::NodeSnapshotContext ctx{};
    const NodeFsmState &fsm = g_node_fsm[node_id];
    ctx.override_state = (fsm.macro_state == MacroState::OVERRIDE_RUN) ? "ON_LEASE" : (fsm.macro_state == MacroState::OVERRIDE_HOLD_OFF) ? "OFF_PAUSE"
                                                                                                                                         : "NONE";
    ctx.override_expiry_ms = fsm.lease_expiry_ms;
    ctx.last_command_id = g_last_command_id[node_id][0] != '\0' ? g_last_command_id[node_id] : nullptr;
    ctx.last_command_result = (fsm.last_lifecycle_event == LifecycleEvent::RF_ACKED) ? "RF_ACKED" : (fsm.last_lifecycle_event == LifecycleEvent::RF_TIMEOUT_OR_NACK) ? "TIMEOUT"
                                                                                                                                                                     : "REJECTED";
    ctx.last_ping_at = live.last_ping_sent_ms;
    ctx.last_ping_ok = live.last_ping_ok;
    ctx.ping_rtt_ms = live.ping_rtt_ms;
    ctx.consecutive_ping_failures = live.consecutive_failures;
    ctx.reset_reason = g_reset_reason_str;
    ctx.source = source ? source : ((fsm.macro_state == MacroState::OVERRIDE_RUN || fsm.macro_state == MacroState::OVERRIDE_HOLD_OFF) ? "MANUAL_OVERRIDE" : "SCHEDULE");
    ctx.transition_reason = transition_reason ? transition_reason : "STATE_UPDATE";

    const char *sched_state = "IDLE";
    if (st.group_id > 0)
    {
        GroupRuntimeState grp{};
        if (g_group_scheduler.getGroupRuntimeState(st.group_id, grp))
        {
            if (grp.assignment_state == GroupAssignmentState::PAUSED)
            {
                sched_state = "PAUSED";
            }
            else if (grp.assignment_state == GroupAssignmentState::ACTIVE)
            {
                sched_state = (grp.current_phase == GroupPhase::PHASE_SPRAYING) ? "SPRAYING" : "COOLING_DOWN";
            }
            else
            {
                sched_state = "IDLE";
            }
        }
    }
    else
    {
        if (fsm.macro_state == MacroState::SCHEDULE_SPRAY)
        {
            sched_state = "SPRAYING";
        }
        else if (fsm.macro_state == MacroState::SCHEDULE_COOLDOWN)
        {
            sched_state = "COOLING_DOWN";
        }
    }
    ctx.schedule_state = sched_state;

    mqtt_client.publishNodeSnapshot(node_id, st, &ctx);
}

static void serviceStaleEvaluationTick(uint32_t current_time_ms)
{
    if (!g_gateway_operational)
        return;
    if (current_time_ms - g_last_stale_eval_ms >= 5000)
    {
        g_last_stale_eval_ms = current_time_ms;
        uint16_t newly_stale = g_node_registry.evaluateStaleNodes(current_time_ms, 60000);
        for (uint8_t i = 0; i < PRODUCTION_NODE_COUNT; ++i)
        {
            if (newly_stale & (1 << i))
            {
                uint8_t node_id = static_cast<uint8_t>(RF_PRODUCTION_MIN_NODE_ID + i);
                // Skip nodes that are actively running under a manual override lease.
                // The OVERRIDE_RUN lease expiry (serviceLegacyOverrideExpiry) is the
                // correct mechanism to turn them off; a stale-safe-off here would
                // fight with an in-progress pump command and immediately undo it.
                if (isAguLegacyNodeId(node_id) &&
                    (g_node_fsm[node_id].macro_state == MacroState::OVERRIDE_RUN ||
                     g_node_fsm[node_id].macro_state == MacroState::SCHEDULE_SPRAY ||
                     g_staggered_schedule[node_id].is_scheduled_on))
                {
                    ESP_LOGW(TAG, "[STALE_EVAL] Node %u is STALE but RUNNING (spray/override active) — "
                                  "skipping stale-safe-off, schedule/lease timer will handle it.",
                             node_id);
                    continue;
                }
                g_command_manager.cancelNodeCommands(node_id);
                if (isAguLegacyNodeId(node_id))
                {
                    initNodeFsm(g_node_fsm[node_id], node_id);
                    g_last_command_id[node_id][0] = '\0';
                }
                char reason_buf[128];
                snprintf(reason_buf, sizeof(reason_buf), "Node %u went STALE; forced OFF, latched fault and canceled pending commands", node_id);
                mqtt_client.publishSafetyAudit("STALE_SAFE_OFF", reason_buf);
                if (isAguLegacyNodeId(node_id))
                {
                    publishLegacyNodeSnapshot(node_id, "SAFE_OFF", "STALE_SAFE_OFF");
                }
                ESP_LOGW(TAG, "Node %u stale-safe-off executed.", node_id);
            }
        }
    }
}

static void executeGroupPump(uint8_t logical_group, bool turn_on, uint32_t duration_sec)
{
    if (!g_gateway_operational || !g_agu_legacy_host)
        return;
    if (logical_group < 1 || logical_group > MAX_TIMER_GROUPS)
        return;

    uint8_t min_node = 0, max_node = 0;
    getGroupMemberNodes(logical_group, min_node, max_node);

    g_rf_bus_locked = true;
#if AUTONOMOUS_FALLBACK_ENABLED && SELECTED_OPERATION_MODE == OP_MODE_NODE
    const uint8_t target_node = autonomousNodeForGroup(logical_group);
    if (!isAguLegacyNodeId(target_node))
    {
        ESP_LOGE(TAG, "[AUTONOMOUS RF] Invalid node target for logical group %u", logical_group);
        g_rf_bus_locked = false;
        return;
    }
    ESP_LOGI(TAG, "[AUTONOMOUS RF] Unicast Node %u -> Pump_%s (slot %u, %u s)",
             static_cast<unsigned>(target_node), turn_on ? "ON" : "OFF",
             static_cast<unsigned>(logical_group), static_cast<unsigned>(duration_sec));

    const uint32_t phase_ms = millis();
    g_agu_legacy_host->setRadioSilenceWindow(
        phase_ms - RF_RADIO_SILENCE_BEFORE_PHASE_MS,
        phase_ms + RF_RADIO_SILENCE_AFTER_PHASE_MS);
    const AguRfTransactionResult tx = g_agu_legacy_host->setPump(target_node, turn_on);
    if (tx.result != AguRfResult::ACKED)
    {
        ESP_LOGW(TAG, "[AUTONOMOUS RF] Unicast rejected node=%u result=%u attempts=%u",
                 static_cast<unsigned>(target_node), static_cast<unsigned>(tx.result),
                 static_cast<unsigned>(tx.attempts));
        g_rf_bus_locked = false;
        return;
    }
#else
    const uint8_t rf_gid = rfGroupIdFromLogical(logical_group);
    if (!isValidRfGroupAddress(rf_gid))
    {
        g_rf_bus_locked = false;
        return;
    }
    ESP_LOGI(TAG, "[GROUP CMD] Broadcast PUMP_%s for Group %u (RF GID=0x%02X, Nodes %u..%u) for %u s",
             turn_on ? "ON" : "OFF", logical_group, rf_gid, min_node, max_node, (unsigned)duration_sec);

    // The host owns the bus for the complete broadcast/burst transaction.
    const uint32_t phase_ms = millis();
    g_agu_legacy_host->setRadioSilenceWindow(
        phase_ms - RF_RADIO_SILENCE_BEFORE_PHASE_MS,
        phase_ms + RF_RADIO_SILENCE_AFTER_PHASE_MS);
    const AguRfTransactionResult tx = g_agu_legacy_host->setGroupPump(rf_gid, turn_on);
    if (tx.result != AguRfResult::ACKED)
    {
        ESP_LOGW(TAG, "[GROUP CMD] Broadcast rejected result=%u burst=%u",
                 static_cast<unsigned>(tx.result), static_cast<unsigned>(tx.burst_frames_sent));
        bool any_group_spraying = false;
        for (uint8_t g = 1; g <= MAX_TIMER_GROUPS; ++g)
        {
            if (g_group_spray_state[g].is_spraying)
            {
                any_group_spraying = true;
                break;
            }
        }
        g_rf_bus_locked = any_group_spraying;
        return;
    }
#endif

    const int64_t now_us = esp_timer_get_time();
    g_group_spray_state[logical_group].is_spraying = turn_on;
    g_group_spray_state[logical_group].spray_duration_s = duration_sec;
    g_group_spray_state[logical_group].target_off_us = now_us + (static_cast<int64_t>(duration_sec) * 1000000LL);

    if (!turn_on)
    {
        g_group_scheduler.notifyPumpCutoff(logical_group, now_us);
    }

    // 2. Hardware Timer for exact microsecond cutoff (esp_timer ISR / daemon)
    if (turn_on && duration_sec > 0)
    {
        if (g_group_cutoff_timer[logical_group] != nullptr)
        {
            esp_timer_stop(g_group_cutoff_timer[logical_group]);
            esp_timer_start_once(g_group_cutoff_timer[logical_group], static_cast<uint64_t>(duration_sec) * 1000000ULL);
        }
    }
    else
    {
        if (g_group_cutoff_timer[logical_group] != nullptr)
        {
            esp_timer_stop(g_group_cutoff_timer[logical_group]);
        }
    }

    // 3. Update local FSM and desired states for all member nodes
    uint8_t state_first = min_node;
    uint8_t state_last = max_node;
#if AUTONOMOUS_FALLBACK_ENABLED && SELECTED_OPERATION_MODE == OP_MODE_NODE
    state_first = autonomousNodeForGroup(logical_group);
    state_last = state_first;
#endif
    for (uint8_t id = state_first; id <= state_last; ++id)
    {
        NodeFsmState &fsm = g_node_fsm[id];
        fsm.macro_state = turn_on ? MacroState::SCHEDULE_SPRAY : MacroState::SCHEDULE_COOLDOWN;
        fsm.lease_active = false;
        fsm.run_lease_ms = 0;
        fsm.lease_expiry_ms = 0;
        g_node_registry.setDesiredState(id, turn_on ? NodePumpState::ON : NodePumpState::OFF);
        g_staggered_schedule[id].is_scheduled_on = turn_on;
        g_staggered_schedule[id].target_off_us = g_group_spray_state[logical_group].target_off_us;
        publishLegacyNodeSnapshot(id, "SCHEDULE", turn_on ? "GROUP_SPRAY_ON" : "GROUP_SPRAY_OFF");
    }

    // Non-blocking architecture: member nodes are verified by the background
    // liveness and telemetry ticks rather than a synchronous blocking loop.

    // If turned off, check if any other group is still spraying before unlocking bus
    if (!turn_on)
    {
        bool any_group_spraying = false;
        for (uint8_t g = 1; g <= MAX_TIMER_GROUPS; ++g)
        {
            if (g_group_spray_state[g].is_spraying)
            {
                any_group_spraying = true;
                break;
            }
        }
        g_rf_bus_locked = any_group_spraying;
    }
}

static void serviceScheduleTick(uint32_t current_ms)
{
    if (!g_gateway_operational)
        return;

    // NVS may contain schedules from a previous Web UI configuration. Do not
    // let those records produce even one spray edge before the retained slot
    // authorization has been reconciled.
    if (!g_control_slots_reconciled)
    {
        static bool timeout_logged = false;
        for (uint8_t group_id = 1; group_id <= MAX_TIMER_GROUPS; ++group_id)
        {
            if (g_group_spray_state[group_id].is_spraying)
                executeGroupPump(group_id, false);
            if (g_group_cutoff_timer[group_id] != nullptr)
                esp_timer_stop(g_group_cutoff_timer[group_id]);
            g_group_spray_state[group_id] = {};
        }
        resetAllScheduleEdgeStates();
        if (!timeout_logged && g_control_slots_boot_ms != 0 &&
            current_ms - g_control_slots_boot_ms >= BOOT_RECONCILE_TIMEOUT_MS)
        {
            ESP_LOGW(TAG, "[SLOT SYNC] CONTROL_SLOT_RECONCILE_TIMEOUT; remaining fail-safe OFF");
            timeout_logged = true;
        }
        return;
    }

    // 1. Advance GroupScheduler 1-second deterministic RTC tick
    static uint32_t last_schedule_ms = 0;
    if (current_ms - last_schedule_ms >= 1000)
    {
        last_schedule_ms = current_ms;
        if (!g_group_scheduler.stepGroupSchedule())
        {
            // Clock invalidation is fail-safe. Clear local spray runtime so a
            // later clock recovery cannot replay a stale SPRAYING edge.
            for (uint8_t group_id = 1; group_id <= MAX_TIMER_GROUPS; ++group_id)
            {
                g_group_spray_state[group_id].is_spraying = false;
                g_group_spray_state[group_id].target_off_us = 0;
                if (g_group_cutoff_timer[group_id] != nullptr)
                {
                    esp_timer_stop(g_group_cutoff_timer[group_id]);
                }
            }
            resetAllScheduleEdgeStates();
        }
    }

    const int64_t now_us = esp_timer_get_time();

    // 2. Evaluate all 4 groups for activation & microsecond-precise termination
    for (uint8_t grp_id = 1; grp_id <= MAX_TIMER_GROUPS; ++grp_id)
    {
        GroupRuntimeState grp_state{};
        const bool has_state = g_group_scheduler.getGroupRuntimeState(grp_id, grp_state);
        GroupSprayRuntime &g_spray = g_group_spray_state[grp_id];

        const GroupPhase current_phase = has_state
                                             ? grp_state.current_phase
                                             : (g_spray.is_spraying ? g_spray.last_phase : GroupPhase::PHASE_COOLING_DOWN);

        if (g_spray.is_spraying)
        {
            // Backup check in case hardware timer was delayed or missed
            if (now_us >= g_spray.target_off_us)
            {
                ESP_LOGI(TAG, "[GROUP SCHEDULER] Group %u spray duration met (backup monotonic cutoff)", grp_id);
                executeGroupPump(grp_id, false);
            }
            g_spray.last_phase = current_phase;
            g_spray.phase_initialized = true;
        }
        else if (has_state &&
                 grp_state.assignment_state == GroupAssignmentState::ACTIVE &&
                 current_phase == GroupPhase::PHASE_SPRAYING)
        {
            if (!g_spray.is_spraying)
            {
                uint32_t duration_s = grp_state.is_night_mode
                                          ? grp_state.profile.spray_night_s
                                          : grp_state.profile.spray_day_s;
                // If applied mid-cycle or clock shifted, only spray for the remaining seconds!
                if (grp_state.phase_remaining_s > 0 && grp_state.phase_remaining_s < duration_s)
                {
                    duration_s = grp_state.phase_remaining_s;
                }
                if (duration_s < 1)
                    duration_s = 1;

                ESP_LOGI(TAG, "[GROUP SCHEDULER] Group %u schedule triggered (spray target=%u s, phase remaining=%u s)",
                         grp_id, (unsigned)duration_s, (unsigned)grp_state.phase_remaining_s);
                executeGroupPump(grp_id, true, duration_s);
            }
            g_spray.last_phase = current_phase;
            g_spray.phase_initialized = true;
        }
        else
        {
            g_spray.last_phase = current_phase;
            g_spray.phase_initialized = true;
        }
    }

    // 3. Maintain RF bus lock if any group is actively spraying
    bool any_group_spraying = false;
    for (uint8_t g = 1; g <= MAX_TIMER_GROUPS; ++g)
    {
        if (g_group_spray_state[g].is_spraying)
        {
            any_group_spraying = true;
            break;
        }
    }
    g_rf_bus_locked = any_group_spraying;
}

static void serviceLegacyOverrideExpiry(uint32_t current_ms)
{
    if (!g_gateway_operational)
        return;
    for (uint8_t id = AGU_LEGACY_MIN_NODE_ID; id <= AGU_LEGACY_MAX_NODE_ID; ++id)
    {
        NodeFsmState &fsm = g_node_fsm[id];
        if ((fsm.macro_state != MacroState::OVERRIDE_RUN && fsm.macro_state != MacroState::OVERRIDE_HOLD_OFF) ||
            !fsm.lease_active || current_ms < fsm.lease_expiry_ms)
            continue;
        if (fsm.macro_state == MacroState::OVERRIDE_RUN)
        {
            ESP_LOGI(TAG, "Legacy node %u ON lease expired; executing auto safe-OFF", id);
            executeAguPump(id, false, nullptr, "SCHEDULE");
            initNodeFsm(fsm, id);
            publishLegacyNodeSnapshot(id, "SCHEDULE", "LEASE_EXPIRED");
            ESP_LOGI(TAG, "Legacy node %u override expired; schedule control restored", id);
        }
        else if (fsm.macro_state == MacroState::OVERRIDE_HOLD_OFF)
        {
            ESP_LOGI(TAG, "Legacy node %u OFF pause expired; restoring schedule control", id);
            initNodeFsm(fsm, id);
            publishLegacyNodeSnapshot(id, "SCHEDULE", "PAUSE_EXPIRED");
        }
    }
}

static uint8_t s_liveness_cursor = AGU_LEGACY_MIN_NODE_ID;

static void serviceAguLivenessTick(uint32_t current_ms)
{
    if (!g_agu_liveness_enabled || !g_gateway_operational || !g_agu_legacy_host)
        return;
    if (g_agu_legacy_host->pumpPending() || g_agu_legacy_host->isRadioSilenceActive(current_ms))
        return;
    if (current_ms - g_last_agu_ping_ms < 1500)
        return; // Time-sliced: probe 1 node every 1.5s (all 15 nodes in ~22s)
    const uint8_t id = s_liveness_cursor;

    const NodeFsmState &fsm = g_node_fsm[id];
    // Do not disrupt an active pump spray cycle or hold-off with PING
    if (fsm.macro_state != MacroState::OVERRIDE_RUN &&
        fsm.macro_state != MacroState::OVERRIDE_HOLD_OFF)
    {
        bool was_deferred = false;
        executeAguPing(id, false, &was_deferred);
        if (was_deferred)
        {
            return;
        }
    }
    g_last_agu_ping_ms = current_ms;
    ++s_liveness_cursor;
    if (s_liveness_cursor > AGU_LEGACY_MAX_NODE_ID)
    {
        s_liveness_cursor = AGU_LEGACY_MIN_NODE_ID;
    }
}

/** Publish a lifecycle event for a node via MQTT command event topic. */
static void publishNodeLifecycleEvent(uint8_t node_id, LifecycleEvent event)
{
    const char *event_name = nullptr;
    switch (event)
    {
    case LifecycleEvent::LEASE_EXPIRED_SAFE_OFF:
        event_name = "LEASE_EXPIRED_SAFE_OFF";
        break;
    case LifecycleEvent::FAULT_LATCHED:
        event_name = "FAULT_LATCHED";
        break;
    case LifecycleEvent::RF_ACKED:
        event_name = "RF_ACKED";
        break;
    case LifecycleEvent::RF_TIMEOUT_OR_NACK:
        event_name = "RF_TIMEOUT_OR_NACK";
        break;
    default:
        event_name = "UNKNOWN";
        break;
    }
    mqtt_client.publishCommandEvent(
        g_last_command_id[node_id][0] ? g_last_command_id[node_id] : nullptr,
        event_name,
        node_id,
        "auto");
}

/** Update evidence pipeline from an 8-byte 0x0E RAM burst. */
static void updateNodeEvidenceFromTelemetry(uint8_t node_id, const uint8_t ram_data[8], uint32_t current_ms)
{
    NodeFsmState &fsm = g_node_fsm[node_id];

    uint8_t reported_pump_state = ram_data[0];
    uint8_t driver_feedback = ram_data[1];
    uint16_t flow_lpm_x100 = static_cast<uint16_t>(ram_data[2]) | (static_cast<uint16_t>(ram_data[3]) << 8);
    uint8_t fault_flags = ram_data[6];

    fsm.fault_flags = fault_flags;

    // Advance evidence stage if waiting for gate feedback
    if (fsm.evidence_stage == EvidenceStage::RF_ACKNOWLEDGED ||
        fsm.evidence_stage == EvidenceStage::GATE_FEEDBACK_ON)
    {
        if (driver_feedback == 1)
        {
            advanceEvidenceStage(fsm, EvidenceStage::GATE_FEEDBACK_ON, current_ms);
        }
    }

    // If flow confirmed, advance to FLOW_CONFIRMED
    if (fsm.evidence_stage == EvidenceStage::CURRENT_DETECTED ||
        fsm.evidence_stage == EvidenceStage::GATE_FEEDBACK_ON ||
        fsm.evidence_stage == EvidenceStage::RF_ACKNOWLEDGED)
    {
        if (flow_lpm_x100 >= FSM_FLOW_CONFIRMED_MIN_LPM_X100)
        {
            advanceEvidenceStage(fsm, EvidenceStage::FLOW_CONFIRMED, current_ms);
        }
    }

    // Update registry telemetry
    NodePumpState reported = reported_pump_state ? NodePumpState::ON : NodePumpState::OFF;
    g_node_registry.updateTelemetryDetailed(
        node_id, reported, driver_feedback,
        0, 0, flow_lpm_x100, 0, 0, 0, current_ms, 0, fault_flags);
}

/** Service FSM tick per Track D2:
 * - leaseTick → expired → OFF txn, SCHEDULE_COOLDOWN, LEASE_EXPIRED_SAFE_OFF
 * - flow settle timeout → FAULT_LATCH
 * - pending-command-table cleanup
 * No blocking, no malloc/new.
 */
static void serviceFsmTick(uint32_t current_ms)
{
    if (!g_gateway_operational)
        return;
    for (uint8_t id = AGU_LEGACY_MIN_NODE_ID; id <= AGU_LEGACY_MAX_NODE_ID; ++id)
    {
        NodeFsmState &fsm = g_node_fsm[id];

        // 1. Lease tick — check for expired deadman lease
        if (leaseTick(fsm, current_ms))
        {
            // Lease expired: dispatch OFF transaction, transition to SCHEDULE_COOLDOWN
            executeAguPump(id, false, nullptr);
            fsm.cooldown_boundary_ms = current_ms + T_COOLDOWN_MIN_MS;
            transitionMacroState(fsm, MacroState::SCHEDULE_COOLDOWN, current_ms);
            publishNodeLifecycleEvent(id, LifecycleEvent::LEASE_EXPIRED_SAFE_OFF);
        }

        // 2. Flow settle timeout check (applies to SCHEDULE_SPRAY; manual override runs are bounded by run_lease_ms deadman timer)
        if (HARDWARE_FLOW_SENSOR_PRESENT)
        {
            if (fsm.macro_state == MacroState::SCHEDULE_SPRAY)
            {
                if (fsm.evidence_stage == EvidenceStage::RF_ACKNOWLEDGED ||
                    fsm.evidence_stage == EvidenceStage::GATE_FEEDBACK_ON ||
                    fsm.evidence_stage == EvidenceStage::CURRENT_DETECTED)
                {
                    if ((current_ms - fsm.last_evidence_ms) > T_FLOW_SETTLE_MS)
                    {
                        // Flow not confirmed within settle time → fault
                        executeAguPump(id, false, nullptr);
                        transitionMacroState(fsm, MacroState::FAULT_LATCH, current_ms);
                        publishNodeLifecycleEvent(id, LifecycleEvent::FAULT_LATCHED);
                    }
                }
            }
        }

        // 3. Command table cleanup
        g_pending_commands.cleanup(current_ms);
    }
}

/** Service 0x0E telemetry polling per Track D3:
 * - Poll opcode 0x0E every T_POLL_0x0E_MS (1s)
 * - Only on Core 1 (application core), never Core 0 with Wi-Fi driver
 * - Parse 8-byte RAM burst, call updateNodeEvidenceFromTelemetry
 * - No blocking calls, no malloc; vTaskDelay(20) between nodes
 */
static void servicePollTelemetry(uint32_t current_ms)
{
    if (!g_gateway_operational || !g_agu_legacy_host)
        return;
    if (g_agu_legacy_host->pumpPending() || g_agu_legacy_host->isRadioSilenceActive(current_ms))
        return;
    static uint32_t last_poll_ms = 0;
    if (current_ms - last_poll_ms < T_POLL_0x0E_MS)
        return;
    for (uint8_t id = AGU_LEGACY_MIN_NODE_ID; id <= AGU_LEGACY_MAX_NODE_ID; ++id)
    {
        if (g_agu_legacy_host->pumpPending() || g_agu_legacy_host->isRadioSilenceActive(current_ms))
            break;
        NodeFsmState &fsm = g_node_fsm[id];

        // Only poll if node is in SCHEDULE_SPRAY (automated schedule)
        if (fsm.macro_state != MacroState::SCHEDULE_SPRAY)
        {
            continue;
        }

        // Execute 0x0E readRamBurst and update evidence pipeline
        uint8_t ram_data[8] = {};
        AguRfTransactionResult result = g_agu_legacy_host->readRamBurst(id, 0x0100, ram_data);

        if (result.result == AguRfResult::SILENCE_DEFERRED || result.result == AguRfResult::BUS_BUSY)
            break;

        if (result.result == AguRfResult::ACKED)
        {
            // Parse 8-byte RAM block:
            //   byte 0: reported_pump_state
            //   byte 1: driver_feedback
            //   byte 2-3: flow_lpm_x100 (uint16 LE)
            //   byte 4-5: pulse_count (uint16 LE)
            //   byte 6: fault_flags
            //   byte 7: reserved
            updateNodeEvidenceFromTelemetry(id, ram_data, current_ms);
        }
        else
        {
            // Timeout or error → potential stale
            g_node_registry.updateHealth(id, NodeHealthStatus::STALE);
        }

        vTaskDelay(pdMS_TO_TICKS(20)); // Bus guard delay between nodes
    }
    last_poll_ms = current_ms;
}

static bool initializeGatewayCore()
{
    g_command_manager.setOutcomeSink(&mqtt_client);
    initializeNvs();
    // Wire.begin(RTC_SDA_PIN, RTC_SCL_PIN) + DS1307 detection + restore of a
    // persisted backend reference when the coin cell died. Must run before the
    // scheduler begins validating the clock on every tick.
    initializeRtc();
    if (!g_node_registry.begin())
    {
        ESP_LOGE(TAG, "Failed to initialize NodeRegistry");
        return false;
    }
    if (!g_group_scheduler.begin(&g_rtc_manager, &g_node_registry, nullptr, &mqtt_client, &g_command_manager, &g_nvs_storage))
    {
        ESP_LOGE(TAG, "Failed to initialize GroupScheduler");
        return false;
    }

    // Initialize hardware esp_timer cutoff handles for all 4 timer groups
    for (uint8_t g = 1; g <= MAX_TIMER_GROUPS; ++g)
    {
        if (g_group_cutoff_timer[g] == nullptr)
        {
            esp_timer_create_args_t timer_args = {};
            timer_args.callback = &onGroupCutoffTimer;
            timer_args.arg = reinterpret_cast<void *>(static_cast<uintptr_t>(g));
            timer_args.dispatch_method = ESP_TIMER_TASK;
            timer_args.name = "grp_cutoff";
            const esp_err_t timer_err = esp_timer_create(&timer_args, &g_group_cutoff_timer[g]);
            if (timer_err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to create hardware esp_timer for Group %u (0x%x)", g, timer_err);
            }
        }
    }

#if AUTONOMOUS_FALLBACK_ENABLED
    // Runs after the cutoff timers exist so an activated group can always arm
    // its exact-duration cutoff. Failure keeps every group safe-OFF.
    if (!provisionAutonomousSchedules())
    {
        ESP_LOGE(TAG, "Autonomous scheduling provisioning failed");
        return false;
    }
#endif

    ESP_LOGI(TAG, "NodeRegistry, GroupScheduler, and Hardware Cutoff Timers initialized.");
    return true;
}

static bool initializeRfControlBoundary()
{
    RfHardwareConfig rf_config;
    // Legacy AGU SCI owns the deployed RF link. Initialize the physical UART
    // without requiring RF_AUTH_V1 PSK/session provisioning; otherwise a
    // missing modern credential puts the whole gateway in degraded mode and
    // prevents MQTT scan commands from ever reaching executeRfScan().
    uint32_t uart_num = 0, tx_pin = 0, rx_pin = 0;
    if (g_rf_nvs_storage.begin() &&
        g_rf_nvs_storage.getU32(RF_NVS_UART_NUM_KEY, uart_num) &&
        g_rf_nvs_storage.getU32(RF_NVS_UART_TX_PIN_KEY, tx_pin) &&
        g_rf_nvs_storage.getU32(RF_NVS_UART_RX_PIN_KEY, rx_pin) &&
        g_rf_nvs_storage.getU32(RF_NVS_UART_BAUD_KEY, rf_config.baud_rate) &&
        uart_num <= 2 && tx_pin <= 127 && rx_pin <= 127)
    {
        rf_config.uart_num = static_cast<uint8_t>(uart_num);
        rf_config.tx_pin = static_cast<int8_t>(tx_pin);
        rf_config.rx_pin = static_cast<int8_t>(rx_pin);
        uint32_t m0_pin = 0, m1_pin = 0, aux_pin = 0;
        if (g_rf_nvs_storage.getU32(RF_NVS_UART_M0_PIN_KEY, m0_pin) && m0_pin <= 127)
            rf_config.m0_pin = static_cast<int8_t>(m0_pin);
        if (g_rf_nvs_storage.getU32(RF_NVS_UART_M1_PIN_KEY, m1_pin) && m1_pin <= 127)
            rf_config.m1_pin = static_cast<int8_t>(m1_pin);
        if (g_rf_nvs_storage.getU32(RF_NVS_UART_AUX_PIN_KEY, aux_pin) && aux_pin <= 127)
            rf_config.aux_pin = static_cast<int8_t>(aux_pin);
    }
    // The deployed AGU harness is physically wired to GPIO17 (TX) and GPIO18
    // (RX). Do not let an older rf_config NVS record override the board wiring
    // contract used by this image.
    rf_config.tx_pin = RF_DEFAULT_TX_PIN;
    rf_config.rx_pin = RF_DEFAULT_RX_PIN;
    if (!initializeRfTransport(rf_config) || g_rf_transport == nullptr ||
        !g_rf_transport->isInitialized() || g_agu_legacy_host == nullptr)
    {
        ESP_LOGE(TAG, "AGU legacy RF UART initialization failed");
        return false;
    }
    ESP_LOGW(TAG, "AGU legacy RF active: UART%u TX=%d RX=%d baud=%u (verify 8N2 against AGU-Aeroponics), physical node IDs 1..15; RF_AUTH_V1 provisioning is bypassed",
             rf_config.uart_num, rf_config.tx_pin, rf_config.rx_pin,
             static_cast<unsigned>(g_rf_transport->getBaudRate()));
    return true;
}

static void enterDegradedSafeState(const char *reason)
{
    g_boot_successful = false;
    g_gateway_operational = false;
    ESP_LOGE(TAG, "Gateway boot degraded: %s; RF/MQTT control disabled.", reason);
}

static bool initializeNetworkTelemetry()
{
    if (!setupMainWdt())
        return false;
    connectWifiWithTimeout();
    const bool mqtt_started = initializeMqtt();
    const bool mqtt_task_created = mqtt_started && createMqttTask();
    if (mqtt_started && !finalizeMqttTaskStartup(mqtt_client, mqtt_task_created))
    {
        ESP_LOGW(TAG, "MQTT facade rolled back after task creation failure; initialized=%s connected=%s",
                 mqtt_client.isInitialized() ? "true" : "false",
                 mqtt_client.isConnected() ? "true" : "false");
    }
    g_mqtt_initialized = mqtt_task_created && mqtt_client.isInitialized();
    return true;
}

void setup()
{
    Serial.begin(SERIAL_BAUD_RATE);
    g_control_slots_boot_ms = millis();
    g_control_slots_reconciled = false;
#if defined(ESP_PLATFORM)
    esp_reset_reason_t reason = esp_reset_reason();
    switch (reason)
    {
    case ESP_RST_POWERON:
        g_reset_reason_str = "POWERON";
        break;
    case ESP_RST_EXT:
        g_reset_reason_str = "EXT_PIN";
        break;
    case ESP_RST_SW:
        g_reset_reason_str = "SW_RESET";
        break;
    case ESP_RST_PANIC:
        g_reset_reason_str = "EXCEPTION_PANIC";
        break;
    case ESP_RST_INT_WDT:
        g_reset_reason_str = "INT_WDT";
        break;
    case ESP_RST_TASK_WDT:
        g_reset_reason_str = "TASK_WDT";
        break;
    case ESP_RST_WDT:
        g_reset_reason_str = "OTHER_WDT";
        break;
    case ESP_RST_DEEPSLEEP:
        g_reset_reason_str = "DEEPSLEEP";
        break;
    case ESP_RST_BROWNOUT:
        g_reset_reason_str = "BROWNOUT";
        break;
    case ESP_RST_SDIO:
        g_reset_reason_str = "SDIO";
        break;
    default:
        g_reset_reason_str = "UNKNOWN";
        break;
    }
    ESP_LOGI(TAG, "[BOOT] ESP32 reset reason: %s (%d)", g_reset_reason_str, static_cast<int>(reason));
    mqtt_client.setResetReason(g_reset_reason_str);
#endif
    ESP_LOGI(TAG, "Initializing Aeroponics gateway composition root...");

    // Initialize NVS storage and prepare Core 0 Network/Button Engine
    initializeNvs();
    size_t hmi_slots_size = sizeof(g_hmi_slot_targets);
    if (g_nvs_storage.getBlob("hmi_slots", g_hmi_slot_targets, &hmi_slots_size) &&
        hmi_slots_size != sizeof(g_hmi_slot_targets))
    {
        memset(g_hmi_slot_targets, 0, sizeof(g_hmi_slot_targets));
    }
    g_hardware_button.begin();
    g_wifi_controller.begin(&g_wifi_storage, &g_hardware_button);

    // Initialize Core Domain & RF Control Boundaries
    const bool core_ok = initializeGatewayCore();
    const bool rf_ok = initializeRfControlBoundary();
    if (!core_ok || !rf_ok)
    {
        enterDegradedSafeState("mandatory control boundary initialization failed");
    }
    else
    {
        g_boot_successful = true;
        g_gateway_operational = true;
    }

    // Initialize FSM state for AGU legacy nodes only; modern nodes use the
    // authenticated PumpNodeController path.
    for (uint8_t id = AGU_LEGACY_MIN_NODE_ID; id <= AGU_LEGACY_MAX_NODE_ID; ++id)
    {
        initNodeFsm(g_node_fsm[id], id);
    }

    // START CORE 0 Wi-Fi & Portal Engine BEFORE network telemetry init so WiFi task
    // has time to scan/connect during the subsequent connectWifiWithTimeout() wait
    g_wifi_controller.startCore0Task();
    ESP_LOGI(TAG, "[BOOT] Wi-Fi task spawned on Core 0. TX power capped at 8.5 dBm (inrush protection).");
    // RC-3 Fix: Yield 250ms to allow the Wi-Fi driver to complete PHY calibration,
    // NVS parameter load, and regulatory domain setup before connectWifiWithTimeout()
    // begins polling. 100ms was a race condition — IDF source shows phy_init alone
    // can take 120-180ms on first boot depending on calibration data availability.
    vTaskDelay(pdMS_TO_TICKS(250));

    // Initialize Network Telemetry & Watchdog (Runs even in degraded mode so Farmer Portal & Wi-Fi operate)
    if (!initializeNetworkTelemetry())
    {
        enterDegradedSafeState("network telemetry watchdog initialization failed");
    }

    ESP_LOGI(TAG, "Gateway boot complete: %s.", g_boot_successful ? "SUCCESS" : "DEGRADED");
    // Initialize 2.4" TFT SPI Field Diagnostic HMI
    hmi_init();
}

/**
 * Periodic SNTP re-sync. The DS1307 crystal drifts roughly +/-20 s/day vs the
 * DS3231's +/-2 ppm, so a bounded cadence is required to keep wall-clock and
 * RTC aligned. A backend GATEWAY_CLOCK push suppresses NTP override for one
 * full interval so the authoritative value is not immediately re-driven.
 */
static void serviceNtpResyncTick(uint32_t current_ms)
{
    if (!g_wifi_controller.isConnected())
        return;

    static uint32_t s_last_ntp_ms = 0;
    static bool s_ntp_synced = false;

    if (g_rtc_manager.isBackendTimeAuthoritative(current_ms))
    {
        // Backend holds the authoritative reference this interval.
        s_last_ntp_ms = current_ms;
        return;
    }

    const bool interval_elapsed =
        s_last_ntp_ms == 0 ||
        (current_ms - s_last_ntp_ms) >= NTP_RESYNC_INTERVAL_MS;
    if (!interval_elapsed)
        return;

    s_last_ntp_ms = current_ms;
    if (g_rtc_manager.syncFromNtp())
    {
        s_ntp_synced = true;
        ESP_LOGI(TAG, "Periodic NTP re-sync succeeded (DS1307 drift corrected).");
    }
    else if (!s_ntp_synced)
    {
        ESP_LOGW(TAG, "Initial NTP sync failed; will retry on next interval.");
    }
}

static void serviceHmiTick(uint32_t current_ms)
{
    if (g_hmi_slot_config_pending.exchange(false))
    {
        memcpy(g_hmi_slot_targets, g_pending_hmi_slot_targets, sizeof(g_hmi_slot_targets));
        g_nvs_storage.setBlob("hmi_slots", g_hmi_slot_targets, sizeof(g_hmi_slot_targets));
        ESP_LOGI(TAG, "Applied dynamic HMI slot configuration from backend");
    }
    static uint32_t s_last_hmi_ms = 0;
    if (current_ms - s_last_hmi_ms < 200)
        return;
    s_last_hmi_ms = current_ms;

    HmiGlobalData g_data = {};
    g_data.wifi_connected = g_wifi_controller.isConnected();
    g_data.wifi_rssi = g_data.wifi_connected ? WiFi.RSSI() : -100;
    if (g_data.wifi_connected)
    {
        IPAddress ip = WiFi.localIP();
        snprintf(g_data.ip_short, sizeof(g_data.ip_short), ".%u.%u", ip[2], ip[3]);
    }
    else
    {
        snprintf(g_data.ip_short, sizeof(g_data.ip_short), "DISCON");
    }
    g_data.mqtt_connected = mqtt_client.isConnected();
    SystemTime st = g_rtc_manager.getTime();
    g_data.rtc_synced = st.is_valid;
    snprintf(g_data.clock_str, sizeof(g_data.clock_str), "%02u:%02u:%02u", st.hour, st.minute, st.second);
    g_data.free_heap_kb = esp_get_free_heap_size() / 1024;
    g_data.sys_safety_mode = 0; // NORMAL

    hmi_update_global(g_data);

    for (uint8_t i = 0; i < 4; i++)
    {
        HmiSlotData s_data = {};
        const HmiSlotTarget &target = g_hmi_slot_targets[i];
        if (target.type == HmiTargetType::EMPTY)
        {
            s_data.target_type = 0;
            s_data.state = HMI_STATE_BOOT_OFF;
            hmi_update_slot(i, s_data);
            continue;
        }
        if (target.type == HmiTargetType::GROUP)
        {
            s_data.target_type = 2;
            s_data.group_id = target.id;
            bool found = false;
            for (uint8_t id = 1; id <= 15; ++id)
            {
                NodeState node_st;
                if (!g_node_registry.getNodeState(id, node_st) || node_st.group_id != target.id)
                    continue;
                found = true;
                s_data.node_id = id;
                s_data.current_ma = node_st.current_ma;
                s_data.opto_feedback = node_st.driver_feedback == 1;
                s_data.last_seen_ms = node_st.last_seen_ms;
                if (node_st.fault_latched)
                    s_data.state = HMI_STATE_FAULT_LATCH;
                else if (node_st.health == NodeHealthStatus::OFFLINE || node_st.health == NodeHealthStatus::STALE)
                    s_data.state = HMI_STATE_DISCONNECTED;
                else if (node_st.reported_state == NodePumpState::ON)
                    s_data.state = HMI_STATE_SCHEDULE_SPRAY;
                else
                    s_data.state = HMI_STATE_SCHEDULE_COOLDOWN;
                break;
            }
            if (!found)
                s_data.state = HMI_STATE_BOOT_OFF;
            hmi_update_slot(i, s_data);
            continue;
        }
        const uint8_t n_id = target.id;
        s_data.target_type = 1;
        s_data.node_id = n_id;
        NodeState node_st;
        if (g_node_registry.getNodeState(n_id, node_st))
        {
            s_data.group_id = node_st.group_id;
            s_data.current_ma = node_st.current_ma;
            s_data.opto_feedback = (node_st.driver_feedback == 1);
            s_data.last_seen_ms = node_st.last_seen_ms;

            if (node_st.fault_latched)
            {
                s_data.state = HMI_STATE_FAULT_LATCH;
                snprintf(s_data.fault_msg, sizeof(s_data.fault_msg), "FAULT_LATCH");
            }
            else if (node_st.health == NodeHealthStatus::OFFLINE || node_st.health == NodeHealthStatus::STALE)
            {
                s_data.state = HMI_STATE_DISCONNECTED;
            }
            else if (node_st.reported_state == NodePumpState::ON)
            {
                s_data.state = HMI_STATE_SCHEDULE_SPRAY;
            }
            else
            {
                s_data.state = HMI_STATE_SCHEDULE_COOLDOWN;
            }
        }
        else
        {
            s_data.group_id = 0;
            s_data.state = HMI_STATE_BOOT_OFF;
        }

        hmi_update_slot(i, s_data);
    }

    hmi_service_tick(current_ms);
}

void loop()
{
    uint32_t current_ms = millis();

    if (g_wdt_registered)
    {
        esp_err_t err = esp_task_wdt_reset();
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Main loop WDT reset failed");
            esp_restart();
        }
    }

    // Service RF RX loop: read bytes, slice frames, decode, update node telemetry/ACKs
    serviceRfRx(current_ms);

    // Service Command fan-out & retry loop: dispatch pending commands via RF
    serviceCommandFanoutTick(current_ms);

    // Service Stale evaluation: evaluate node telemetry freshness and flag offline/stale nodes
    serviceStaleEvaluationTick(current_ms);

    // Service Autonomous Irrigation Schedule
    serviceLegacyOverrideExpiry(current_ms);
    serviceScheduleTick(current_ms);
    mqtt_client.serviceScheduleState(current_ms);

    // Service FSM deadman lease, evidence timeout, and pending-command cleanup (Track D2)
    serviceFsmTick(current_ms);

    // Service 0x0E telemetry polling on Core 1 only (Track D3)
    servicePollTelemetry(current_ms);

    // Service AGU legacy node periodic PING liveness
    serviceAguLivenessTick(current_ms);

    serviceNtpResyncTick(current_ms);

    // Service 2.4" TFT Field Diagnostic HMI
    serviceHmiTick(current_ms);

    // Parse and handle Gateway Serial debug commands
    processSerialCommands();

    // Yield CPU 1 to IDLE task to satisfy Task Watchdog requirements
    vTaskDelay(pdMS_TO_TICKS(1));
}

static void processSerialCommands()
{
    static char buffer[SERIAL_COMMAND_BUFFER_SIZE];
    static size_t buf_idx = 0;
    static bool discarding_overflow = false;
    size_t bytes_processed = 0;
    while (Serial.available() > 0 && bytes_processed < MAX_SERIAL_BYTES_PER_TICK)
    {
        char c = static_cast<char>(Serial.read());
        bytes_processed++;

        if (c == '\r' || c == '\n')
        {
            if (discarding_overflow)
            {
                ESP_LOGE(TAG, "Serial line exceeded buffer limit (%u bytes). Line discarded.",
                         static_cast<unsigned>(SERIAL_COMMAND_BUFFER_SIZE - 1));
                discarding_overflow = false;
                buf_idx = 0;
            }
            else if (buf_idx > 0)
            {
                buffer[buf_idx] = '\0';
                handleCommand(buffer);
                buf_idx = 0;
            }
        }
        else
        {
            if (discarding_overflow)
            {
                continue;
            }
            if (buf_idx < sizeof(buffer) - 1)
            {
                buffer[buf_idx++] = c;
            }
            else
            {
                discarding_overflow = true;
                buf_idx = 0;
            }
        }
    }
}

static void handleFactoryResetConfirmation(const char *cmd)
{
    if (strcasecmp(cmd, "YES") == 0)
    {
        ESP_LOGW(TAG, "Executing confirmed NVS factory reset...");
        bool ok = g_nvs_storage.factoryReset();
        if (ok)
        {
            ESP_LOGI(TAG, "Factory reset successful; restarting now.");
            esp_restart();
        }
        else
        {
            ESP_LOGE(TAG, "Factory reset failed during NVS erase.");
        }
    }
    else
    {
        ESP_LOGI(TAG, "Factory reset request cancelled.");
    }
    g_pending_factory_confirm = false;
}

static void runSystemDiagnostics()
{
    ESP_LOGI(TAG, "Diagnostics: boot=%s wdt=%s",
             (g_boot_successful ? "SUCCESS" : "FAILED"),
             (g_wdt_registered ? "YES" : "NO"));
    ESP_LOGI(TAG, "Diagnostics: rf=%s mqtt=%s connected=%s",
             (g_rf_transport && g_rf_transport->isInitialized() ? "YES" : "NO"),
             (g_mqtt_initialized ? "YES" : "NO"),
             (mqtt_client.isConnected() ? "YES" : "NO"));
    ESP_LOGI(TAG, "Diagnostics: composition root wired and active.");
}

static void executeRfSetup()
{
    if (!g_rf_transport)
    {
        ESP_LOGE(TAG, "RF transport not initialized!");
        return;
    }
    ESP_LOGI(TAG, "=== Starting AGU RF Module AT Setup ===");
    const char *commands[][2] = {
        {"AT\r\n", "Handshake"},
        {"AT+B38400\r\n", "Baudrate 38400"},
        {"AT+UN2\r\n", "UART Format (8N2)"},
        {"AT+A123\r\n", "Network ID 123"},
        {"AT+C001\r\n", "Channel 001 (433MHz)"}};
    for (size_t i = 0; i < 5; ++i)
    {
        const char *cmd_str = commands[i][0];
        const char *desc = commands[i][1];
        ESP_LOGI(TAG, "[TX] Sending: %s (%s)", cmd_str, desc);
        g_rf_transport->send(reinterpret_cast<const uint8_t *>(cmd_str), strlen(cmd_str));
        vTaskDelay(pdMS_TO_TICKS(500));
        uint8_t resp[64] = {};
        size_t r = g_rf_transport->receive(resp, sizeof(resp) - 1);
        if (r > 0)
        {
            resp[r] = '\0';
            ESP_LOGI(TAG, "[RX] Response: %s", reinterpret_cast<char *>(resp));
        }
        else
        {
            ESP_LOGW(TAG, "[RX] No response (timeout or already in transparent mode)");
        }
    }
    ESP_LOGI(TAG, "=== RF Setup Sequence Completed ===");
}

static bool executeAguPing(uint8_t node_id, bool ignore_bus_lock, bool *was_deferred)
{
    if (!g_rf_transport || !g_rf_transport->isInitialized() || !g_agu_legacy_host)
    {
        ESP_LOGE(TAG, "[AGU LEGACY] RF transport/host not initialized");
        return false;
    }
    if (!isAguLegacyNodeId(node_id))
    {
        ESP_LOGW(TAG, "[AGU LEGACY] Refusing probe for unsupported physical client ID %u (allowed: 1..15)", node_id);
        return false;
    }
    if (g_agu_legacy_host->pumpPending() ||
        (g_agu_legacy_host->isRadioSilenceActive(millis()) && !ignore_bus_lock))
    {
        if (was_deferred)
            *was_deferred = true;
        return false;
    }

    if (g_wdt_registered)
        esp_task_wdt_reset();

    NodeLivenessRecord &live = g_node_liveness[node_id];
    live.last_ping_sent_ms = millis();

    // Use Opcode 0x08 [04] [08] [id] [crc_lo] [crc_hi]
    // Response: 1 byte: 0: OFF / 1: ON (also serves as liveness ping, 1 cong 3 viec)
    const AguRfTransactionResult result = g_agu_legacy_host->getPumpState(node_id);

    if (g_wdt_registered)
        esp_task_wdt_reset();
    ESP_LOGD(TAG, "[AGU LEGACY] GET_PUMP_STATE node=%u response=0x%02X result=%u rtt=%u ms attempt=%u/%u",
             node_id, result.response_byte, static_cast<unsigned>(result.result),
             (unsigned)result.rtt_ms, result.attempts, AGU_LEGACY_MAX_ATTEMPTS);

    if (result.result == AguRfResult::SILENCE_DEFERRED || result.result == AguRfResult::BUS_BUSY)
    {
        if (was_deferred)
            *was_deferred = true;
        return false;
    }

    if (result.result == AguRfResult::ACKED)
    {
        live.last_ping_ok = true;
        live.last_ping_ok_ms = millis();
        live.ping_rtt_ms = result.rtt_ms;
        live.consecutive_failures = 0;
        strncpy(live.last_result, "ACKED", sizeof(live.last_result) - 1);
        if (!live.is_healthy)
        {
            live.is_healthy = true;
            live.health_transition_ms = millis();
            ESP_LOGI(TAG, "[AGU LIVENESS] Node %u recovered ONLINE (RTT=%u ms)", node_id, (unsigned)result.rtt_ms);
        }
        g_node_registry.resetFault(node_id);

        const uint8_t reported_pump = (result.response_byte == 1) ? 1 : 0;
        const NodePumpState reported_state = (reported_pump == 1) ? NodePumpState::ON : NodePumpState::OFF;

        g_node_registry.updateTelemetryDetailed(node_id, reported_state, reported_pump,
                                                0, 0, 0, 0, 0, 0, millis());
        publishLegacyNodeSnapshot(node_id, "LIVENESS", reported_pump ? "PUMP_IS_ON" : "PUMP_IS_OFF");

        // CLOSED-LOOP RECONCILIATION: Check for ghost running pump
        NodeState st{};
        if (g_node_registry.getNodeState(node_id, st))
        {
            const NodeFsmState &fsm = g_node_fsm[node_id];
            // If Gateway wants OFF (cooldown, idle, unassigned) and no manual override ON is active:
            if (st.desired_state == NodePumpState::OFF &&
                fsm.macro_state != MacroState::OVERRIDE_RUN &&
                !g_staggered_schedule[node_id].is_scheduled_on &&
                reported_state == NodePumpState::ON)
            {
                ESP_LOGW(TAG, "[RECONCILIATION] Ghost running detected on Node %u! Desired=OFF but Node reports ON. Dispatching emergency PUMP_OFF!", node_id);
                executeAguPump(node_id, false, nullptr, "RECONCILIATION");
            }
        }

        return true;
    }
    else
    {
        live.last_ping_ok = false;
        live.consecutive_failures++;
        strncpy(live.last_result, result.result == AguRfResult::TIMEOUT ? "TIMEOUT" : "ERROR", sizeof(live.last_result) - 1);
        // ESP_LOGW(TAG, "[AGU LIVENESS] Node %u probe failed (%s), consecutive failures: %u",
        //          node_id, live.last_result, live.consecutive_failures);
        if (live.consecutive_failures >= 2 && live.is_healthy)
        {
            live.is_healthy = false;
            live.health_transition_ms = millis();
            ESP_LOGE(TAG, "[AGU LIVENESS] Node %u marked STALE after %u failures; entering safe-off",
                     node_id, live.consecutive_failures);
            g_node_registry.updateHealth(node_id, NodeHealthStatus::STALE);
            char audit_msg[128];
            snprintf(audit_msg, sizeof(audit_msg), "Node %u liveness lost after %u consecutive probe timeouts",
                     node_id, live.consecutive_failures);
            mqtt_client.publishSafetyAudit("LIVENESS_LOST", audit_msg);
            publishLegacyNodeSnapshot(node_id, "LIVENESS", "LIVENESS_LOST");
        }
        return false;
    }
}

static void runRfUartDiagnostic(bool loopback)
{
    if (!g_rf_transport || !g_rf_transport->isInitialized() || !g_agu_legacy_host)
    {
        ESP_LOGE(TAG, "RF UART diagnostic unavailable: transport/host is not initialized");
        return;
    }
    RfBusGuard guard(g_agu_legacy_host, RfTrafficClass::DIAGNOSTIC);
    if (!guard.isLocked())
    {
        ESP_LOGW(TAG, "RF UART diagnostic skipped: bus busy or in radio silence");
        return;
    }

    // The first pattern is the exact AGU Node 7 ping frame. The second pattern
    // is deliberately distinctive for a physical TX-to-RX loopback test.
    // The PING trailer is the CRC16-Modbus of [len][0x05][0xA5][node] and must
    // be valid or a deployed node rejects the frame before parsing it. For node
    // 7 that is 2A 7B (the previous 2B B8 failed the whole-frame remainder).
    static const uint8_t agu_ping[] = {0x05, 0x05, 0xA5, 0x07, 0x2A, 0x7B};
    static const uint8_t loopback_pattern[] = {0x55, 0xAA, 0x00, 0xFF, 0x05, 0x05, 0xA5, 0x07, 0x2A, 0x7B};
    const uint8_t *frame = loopback ? loopback_pattern : agu_ping;
    const size_t frame_size = loopback ? sizeof(loopback_pattern) : sizeof(agu_ping);

    g_rf_transport->flushRx();
    const size_t written = g_rf_transport->send(frame, frame_size);
    ESP_LOGI(TAG, "[RF DIAG] %s TX %zu/%zu bytes (17->18 loopback required=%s)",
             loopback ? "LOOPBACK" : "RAW AGU PING", written, frame_size,
             loopback ? "YES" : "NO");

    uint8_t received[32] = {};
    size_t received_size = 0;
    const uint32_t started = millis();
    while (millis() - started < 500 && received_size < sizeof(received))
    {
        if (g_rf_transport->available() > 0)
        {
            received_size += g_rf_transport->receive(received + received_size,
                                                     sizeof(received) - received_size);
        }
        else
        {
            delay(1);
        }
    }

    ESP_LOGI(TAG, "[RF DIAG] RX %zu bytes", received_size);
    if (received_size > 0)
    {
        char hex[3 * sizeof(received) + 1] = {};
        size_t offset = 0;
        for (size_t i = 0; i < received_size && offset + 3 < sizeof(hex); ++i)
        {
            offset += snprintf(hex + offset, sizeof(hex) - offset, "%02X%s",
                               received[i], i + 1 < received_size ? " " : "");
        }
        ESP_LOGI(TAG, "[RF DIAG] RX bytes: %s", hex);
    }
    if (loopback)
    {
        const bool pass = received_size == frame_size &&
                          memcmp(received, frame, frame_size) == 0;
        ESP_LOGI(TAG, "[RF DIAG] LOOPBACK %s", pass ? "PASS" : "FAIL");
    }
}

static bool executeAguPump(uint8_t node_id, bool turn_on, const char *command_id, const char *source)
{
    if (!g_rf_transport || !g_rf_transport->isInitialized() || !g_agu_legacy_host)
    {
        ESP_LOGE(TAG, "[AGU LEGACY] RF transport/host not initialized");
        if (command_id && command_id[0] != '\0')
            mqtt_client.publishCommandAck(command_id, "REJECTED", node_id, "RF transport unavailable");
        return false;
    }
    if (!isAguLegacyNodeId(node_id))
    {
        ESP_LOGW(TAG, "[AGU LEGACY] Refusing PUMP command for unsupported physical client ID %u (allowed: 1..15)", node_id);
        if (command_id && command_id[0] != '\0')
            mqtt_client.publishCommandAck(command_id, "REJECTED", node_id, "Unsupported AGU legacy client ID");
        return false;
    }

    if (g_wdt_registered)
        esp_task_wdt_reset();

    const uint32_t phase_ms = millis();
    g_agu_legacy_host->setRadioSilenceWindow(
        phase_ms - RF_RADIO_SILENCE_BEFORE_PHASE_MS,
        phase_ms + RF_RADIO_SILENCE_AFTER_PHASE_MS);
    const AguRfTransactionResult result = g_agu_legacy_host->setPump(node_id, turn_on);

    if (g_wdt_registered)
        esp_task_wdt_reset();
    ESP_LOGI(TAG, "[AGU LEGACY] PUMP %s node=%u response=0x%02X result=%u rtt=%u ms attempt=%u/%u source=%s",
             turn_on ? "ON" : "OFF", node_id, result.response_byte, static_cast<unsigned>(result.result),
             (unsigned)result.rtt_ms, result.attempts, AGU_LEGACY_MAX_ATTEMPTS, source ? source : "DEFAULT");

    NodeFsmState &fsm = g_node_fsm[node_id];
    fsm.last_lifecycle_event = (result.result == AguRfResult::ACKED)
                                   ? LifecycleEvent::RF_ACKED
                                   : LifecycleEvent::RF_TIMEOUT_OR_NACK;

    if (result.result != AguRfResult::ACKED)
    {
        const char *reason = result.result == AguRfResult::TIMEOUT ? "Legacy node timeout after 3 retries" : result.result == AguRfResult::UART_NOT_READY ? "Legacy RF UART not ready"
                                                                                                         : result.result == AguRfResult::TX_ERROR         ? "Legacy RF transport TX error"
                                                                                                         : result.result == AguRfResult::INVALID_NODE_ID  ? "Invalid legacy node ID"
                                                                                                                                                          : "Unexpected legacy response";
        if (command_id && command_id[0] != '\0')
            mqtt_client.publishCommandAck(command_id, "REJECTED", node_id, reason);
        publishLegacyNodeSnapshot(node_id, g_override_source[node_id][0] ? g_override_source[node_id] : "MANUAL_OVERRIDE",
                                  "PUMP_REJECTED");
        return false;
    }

    const NodePumpState state = turn_on ? NodePumpState::ON : NodePumpState::OFF;
    // A confirmed 0x5A ACK from a previously-STALE/FAULT node proves it is alive.
    // Clear fault_latched FIRST so that updateTelemetryDetailed (which checks
    // health == STALE and promotes it to FAULT + desired_state=OFF) does not
    // immediately undo the ON command.  resetFault() sets health=OFFLINE and
    // desired_state=OFF transiently; setDesiredState + updateTelemetryDetailed
    // below then set the correct final state (ONLINE + desired ON/OFF).
    if (turn_on)
    {
        g_node_registry.resetFault(node_id); // clears fault_latched, sets health=OFFLINE
    }
    g_node_registry.setDesiredState(node_id, state);
    // A verified 0x5A ACK is valid liveness evidence. Pass the gateway timestamp
    // as gateway_timestamp_ms (arg 10) so updateTelemetryDetailed refreshes
    // last_seen_ms; the previous all-zero tail set last_seen_ms = 0, which made
    // evaluateStaleNodes treat a just-ACKed node as immediately stale.
    g_node_registry.updateTelemetryDetailed(node_id, state, turn_on ? 1 : 0,
                                            0, 0, 0, 0, 0, 0, millis());

    const bool is_schedule = (source != nullptr && strcmp(source, "SCHEDULE") == 0);

    // --- Track E2: FSM integration after AGU ACK ---
    g_pending_commands.insert(node_id, command_id ? command_id : (is_schedule ? "SCHEDULE" : "LOCAL"), millis());
    advanceEvidenceStage(fsm, EvidenceStage::RF_ACKNOWLEDGED, millis());

    if (is_schedule)
    {
        // AUTOMATED SCHEDULE EXECUTION: strictly decoupled from manual override
        fsm.lease_active = false;
        fsm.run_lease_ms = 0;
        fsm.lease_expiry_ms = 0;
        fsm.macro_state = turn_on ? MacroState::SCHEDULE_SPRAY : MacroState::SCHEDULE_COOLDOWN;
        g_override_source[node_id][0] = '\0';
    }
    else if (turn_on)
    {
        // MANUAL OVERRIDE ON
        if (fsm.run_lease_ms < NodeFsmLimits::RUN_LEASE_MIN_MS)
        {
            fsm.run_lease_ms = 30000; // Safe default 30s lease for manual/bench commands
        }
        fsm.lease_active = true;
        fsm.lease_start_ms = millis();
        fsm.lease_expiry_ms = millis() + fsm.run_lease_ms;
        fsm.macro_state = MacroState::OVERRIDE_RUN;
    }
    else
    {
        // MANUAL OVERRIDE OFF or FAIL-SAFE
        fsm.lease_active = false;
        fsm.run_lease_ms = 0;
        fsm.macro_state = MacroState::BOOT_OFF;
    }

    mqtt_client.publishLifecycleEvent(node_id, command_id, LifecycleEvent::RF_ACKED);
    publishNodeLifecycleEvent(node_id, LifecycleEvent::RF_ACKED);

    if (command_id && command_id[0] != '\0')
    {
        mqtt_client.publishCommandAck(command_id, "RF_ACKED", node_id, "Legacy AGU ACK 0x5A received");
    }

    const char *effective_source = source ? source : (is_schedule ? "SCHEDULE" : (g_override_source[node_id][0] ? g_override_source[node_id] : "MANUAL_OVERRIDE"));

    publishLegacyNodeSnapshot(node_id, effective_source,
                              turn_on ? (is_schedule ? "SCHEDULE_SPRAY_ON" : "PUMP_ON_ACKED")
                                      : (is_schedule ? "SCHEDULE_COOLDOWN_OFF" : "PUMP_OFF_ACKED"));
    return true;
}

static void executeAguGetId()
{
    if (!g_rf_transport || !g_agu_legacy_host)
    {
        ESP_LOGE(TAG, "RF transport/host not initialized");
        return;
    }
    RfBusGuard guard(g_agu_legacy_host, RfTrafficClass::DIAGNOSTIC);
    if (!guard.isLocked())
    {
        ESP_LOGW(TAG, "[AGU TX] GET_ID skipped: RF bus busy or in radio silence");
        return;
    }
    uint8_t tx_buf[16];
    size_t len = AguLegacy::AguLegacyCodec::encodeGetId(tx_buf, sizeof(tx_buf));
    ESP_LOGI(TAG, "[AGU TX] GET_ID command (%zu bytes: %02X %02X)", len, tx_buf[0], tx_buf[1]);
    g_rf_transport->flushRx();
    uint32_t start_ms = millis();
    g_rf_transport->send(tx_buf, len);

    uint8_t resp[16] = {};
    size_t count = 0;
    while (millis() - start_ms < 600 && count < sizeof(resp))
    {
        if (g_rf_transport->available() > 0)
        {
            count += g_rf_transport->receive(resp + count, sizeof(resp) - count);
            if (count >= 3)
                break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    uint8_t id = 0;
    if (AguLegacy::AguLegacyCodec::decodeFramedId(resp, count, id))
    {
        ESP_LOGI(TAG, "[AGU RX] Node ID Frame Valid! Detected Node ID = %u (Raw: %02X %02X %02X)",
                 id, resp[0], resp[1], resp[2]);
        if (isAguLegacyNodeId(id))
        {
            guard.unlock();
            executeAguPing(id);
        }
    }
    else
    {
        ESP_LOGW(TAG, "[AGU RX] Failed to decode Node ID frame (received %zu bytes)", count);
    }
}

static void executeAguSetId(uint8_t new_id)
{
    if (!g_rf_transport || !g_agu_legacy_host)
    {
        ESP_LOGE(TAG, "RF transport/host not initialized");
        return;
    }
    if (!isAguLegacyNodeId(new_id))
    {
        ESP_LOGW(TAG, "Warning: Node ID %u is outside AGU legacy client range (1..15)!", new_id);
        return;
    }
    {
        RfBusGuard guard(g_agu_legacy_host, RfTrafficClass::DIAGNOSTIC);
        if (!guard.isLocked())
        {
            ESP_LOGW(TAG, "[AGU TX] SET_ID skipped: RF bus busy or in radio silence");
            return;
        }
        uint8_t tx_buf[16];
        size_t len = AguLegacy::AguLegacyCodec::encodeSetId(new_id, tx_buf, sizeof(tx_buf));
        ESP_LOGI(TAG, "[AGU TX] SET_ID command -> New ID = %u (%zu bytes: %02X %02X %02X)",
                 new_id, len, tx_buf[0], tx_buf[1], tx_buf[2]);
        g_rf_transport->flushRx();
        g_rf_transport->send(tx_buf, len);
        vTaskDelay(pdMS_TO_TICKS(300));
    }
    ESP_LOGI(TAG, "[AGU TX] SET_ID transmitted. Reading back ID to verify...");
    executeAguGetId();
}

static void executeRfScan(const char *scan_id)
{
    if (!g_rf_transport || !g_rf_transport->isInitialized() || !g_agu_legacy_host)
    {
        ESP_LOGE(TAG, "[AGU LEGACY SCAN] RF transport/host unavailable");
        mqtt_client.publishScanResults(scan_id, nullptr, 0, 0, "FAILED", "RF_LEGACY_UNAVAILABLE");
        return;
    }
    ESP_LOGW(TAG, "[AGU LEGACY] RF scan uses unauthenticated AGU_LEGACY_SCI compatibility mode");
    const uint32_t started = millis();
    constexpr size_t agu_node_count = AGU_LEGACY_MAX_NODE_ID - AGU_LEGACY_MIN_NODE_ID + 1;
    MqttClient::DiscoveredRfNodeInfo results[agu_node_count]{};
    for (size_t index = 0; index < agu_node_count; ++index)
    {
        const uint8_t node_id = static_cast<uint8_t>(AGU_LEGACY_MIN_NODE_ID + index);
        auto &result = results[index];
        result.node_id = node_id;
        const AguRfTransactionResult transaction = g_agu_legacy_host->pingNode(node_id);
        result.online = transaction.result == AguRfResult::ACKED;
        result.rtt_ms = transaction.rtt_ms;
        result.failure_code = result.online ? 0 : transaction.result == AguRfResult::TIMEOUT           ? 1
                                              : transaction.result == AguRfResult::UNEXPECTED_RESPONSE ? 2
                                              : transaction.result == AguRfResult::INVALID_NODE_ID     ? 4
                                              : transaction.result == AguRfResult::UART_NOT_READY      ? 5
                                                                                                       : 3;
        ESP_LOGI(TAG, "[AGU LEGACY SCAN] node=%u online=%s response=0x%02X result=%u rtt=%u ms attempt=%u/%u", node_id, result.online ? "yes" : "no", transaction.response_byte, static_cast<unsigned>(transaction.result), (unsigned)transaction.rtt_ms, transaction.attempts, AGU_LEGACY_MAX_ATTEMPTS);
    }
    const uint32_t duration_ms = millis() - started;
    const bool published = mqtt_client.publishScanResults(
        scan_id, results, agu_node_count, duration_ms);
    ESP_LOGI(TAG, "[AGU LEGACY SCAN] result publish %s scan_id=%s duration=%u ms",
             published ? "QUEUED" : "FAILED", scan_id ? scan_id : "(null)",
             static_cast<unsigned>(duration_ms));
}

static void executeRfClaimNode(uint8_t from_id, uint8_t to_id, const char *command_id)
{
    if (!g_rf_transport || !g_agu_legacy_host)
    {
        ESP_LOGE(TAG, "RF transport/host not initialized");
        mqtt_client.publishCommandAck(command_id, "REJECTED", to_id, "RF transport unavailable");
        return;
    }
    if (!isAguLegacyNodeId(to_id))
    {
        ESP_LOGE(TAG, "[RF CLAIM] Invalid target node ID %u (must be 1..15)", to_id);
        mqtt_client.publishCommandAck(command_id, "REJECTED", to_id, "Target node ID must be one of 1..15");
        return;
    }
    RfBusGuard guard(g_agu_legacy_host, RfTrafficClass::DIAGNOSTIC);
    if (!guard.isLocked())
    {
        ESP_LOGW(TAG, "[RF CLAIM] Bus busy or in radio silence; rejecting claim");
        mqtt_client.publishCommandAck(command_id, "REJECTED", to_id, "RF bus busy or in radio silence");
        return;
    }

    ESP_LOGI(TAG, "[RF CLAIM] Commissioning: Re-assigning Node %u -> Node %u (cmd_id: %s)...",
             from_id, to_id, command_id ? command_id : "none");

    uint8_t tx_buf[16];
    size_t len = AguLegacy::AguLegacyCodec::encodeSetId(to_id, tx_buf, sizeof(tx_buf));
    g_rf_transport->flushRx();
    g_rf_transport->send(tx_buf, len);

    vTaskDelay(pdMS_TO_TICKS(200));

    const uint8_t ping_val = AguLegacy::PING_DEFAULT_VAL;
    len = AguLegacy::AguLegacyCodec::encodePing(ping_val, to_id, tx_buf, sizeof(tx_buf));
    g_rf_transport->flushRx();
    uint32_t start_ms = millis();
    g_rf_transport->send(tx_buf, len);

    uint8_t resp = 0;
    bool verified = false;
    while (millis() - start_ms < 300)
    {
        if (g_rf_transport->available() > 0 && g_rf_transport->receive(&resp, 1) == 1)
        {
            if (resp == ping_val)
            {
                verified = true;
                break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    if (verified)
    {
        ESP_LOGI(TAG, "[RF CLAIM] Verification SUCCESS! Node %u is alive and confirmed.", to_id);
        // Pass the gateway timestamp so the freshly verified node is not
        // immediately re-marked STALE by evaluateStaleNodes (last_seen_ms = 0).
        g_node_registry.updateTelemetryDetailed(to_id, NodePumpState::OFF, 0,
                                                0, 0, 0, 0, 0, 0, millis());
        NodeState state{};
        if (g_node_registry.getNodeState(to_id, state))
        {
            publishLegacyNodeSnapshot(to_id, "CLAIM", "CLAIM_VERIFIED");
        }
        mqtt_client.publishCommandAck(command_id, "COMPLETED", to_id, "Node claimed and verified successfully");
    }
    else
    {
        ESP_LOGE(TAG, "[RF CLAIM] Verification TIMEOUT: Node %u did not respond to PING", to_id);
        mqtt_client.publishCommandAck(command_id, "REJECTED", to_id, "Verification timeout on new node ID");
    }
}

static void onGatewayCommand(const MqttInboundCommand &command)
{
    if (command.type == MqttInboundCommandType::GATEWAY_SCAN)
    {
        executeRfScan(command.command_id);
    }
    else if (command.type == MqttInboundCommandType::GATEWAY_CLAIM)
    {
        // Physical IDs 1..15 are immutable in the production path. Keep the
        // legacy handler available for bench diagnostics, but never allow an
        // MQTT/UI command to emit SET_ID on a production gateway.
        mqtt_client.publishCommandAck(command.command_id, "REJECTED", command.node_id,
                                      "NODE_ID_FIXED: claim/SET_ID is disabled in production");
    }
    else if (command.type == MqttInboundCommandType::NODE_OVERRIDE)
    {
        const uint8_t node_id = command.node_id;
        const bool is_on = (command.desired_state == NodePumpState::ON);

        // Modern RF nodes use authenticated unicast framing. AGU nodes remain
        // on the synchronous legacy SCI compatibility path below.
        if (!isAguLegacyNodeId(node_id))
        {
            const ExternalOverridePolicy policy{command.source, command.values[0], command.values[1]};
            const bool accepted = g_command_manager.queueExternalNodeCommand(
                node_id, command.desired_state, command.command_id, &policy);
            mqtt_client.publishCommandAck(command.command_id, accepted ? "ACCEPTED" : "REJECTED",
                                          node_id, accepted ? "Modern node override accepted and queued" : "Modern node override mutation failed");
            return;
        }

        const uint32_t duration_ms = is_on ? command.values[0] : command.values[1];
        const uint32_t effective_duration_ms = (duration_ms > 0) ? duration_ms : 30000;

        if (!isAguLegacyNodeId(node_id))
        {
            mqtt_client.publishCommandAck(command.command_id, "REJECTED", node_id, "Invalid legacy override duration or node");
            return;
        }

        // Authoritative manual override state machine (FSM mapping)
        NodeFsmState &fsm = g_node_fsm[node_id];
        fsm.macro_state = is_on ? MacroState::OVERRIDE_RUN : MacroState::OVERRIDE_HOLD_OFF;
        fsm.lease_active = true;
        fsm.lease_start_ms = millis();
        fsm.lease_expiry_ms = millis() + effective_duration_ms;
        fsm.run_lease_ms = effective_duration_ms;
        g_staggered_schedule[node_id].is_scheduled_on = false;
        g_staggered_schedule[node_id].off_retries = 0;
        // Persist the MQTT command_id for snapshot publishing only (not RF state)
        strncpy(g_last_command_id[node_id], command.command_id, sizeof(g_last_command_id[node_id]) - 1);
        g_last_command_id[node_id][sizeof(g_last_command_id[node_id]) - 1] = '\0';
        // Persist the source label for snapshot publishing only
        strncpy(g_override_source[node_id], command.source[0] ? command.source : "MANUAL_OVERRIDE", sizeof(g_override_source[node_id]) - 1);
        g_override_source[node_id][sizeof(g_override_source[node_id]) - 1] = '\0';

        // 1. Admission is explicit: command is accepted once recorded in state machine
        mqtt_client.publishCommandAck(command.command_id, "ACCEPTED", node_id,
                                      is_on ? "Legacy ON lease accepted and queued" : "Legacy OFF pause accepted and queued");

        // 2. Perform synchronous AGU transaction; reports RF_ACKED or REJECTED
        if (!executeAguPump(node_id, is_on, command.command_id, "MANUAL_OVERRIDE"))
        {
            // Lease persists on transaction failure; FSM state remains active.
            fsm.lease_active = false;
            fsm.lease_expiry_ms = 0;
            initNodeFsm(fsm, node_id);
        }
    }
}

static void handleCommand(const char *cmd)
{
    if (cmd == nullptr || strlen(cmd) == 0)
    {
        return;
    }

    if (g_pending_factory_confirm)
    {
        handleFactoryResetConfirmation(cmd);
        return;
    }

    if (strcasecmp(cmd, "status") == 0)
    {
        printSystemStatus();
    }
    else if (strcasecmp(cmd, "test") == 0)
    {
        runSystemDiagnostics();
    }
    else if (strcasecmp(cmd, "rfstatus") == 0)
    {
        ESP_LOGI(TAG, "RF transport: initialized=%s TX_PIN=%d RX_PIN=%d baud=%u available_bytes=%zu format=0x%X",
                 (g_rf_transport && g_rf_transport->isInitialized() ? "YES" : "NO"),
                 g_rf_transport ? g_rf_transport->getTxPin() : -1,
                 g_rf_transport ? g_rf_transport->getRxPin() : -1,
                 g_rf_transport ? (unsigned)g_rf_transport->getBaudRate() : 0U,
                 g_rf_transport ? g_rf_transport->available() : 0U,
                 g_rf_transport ? (unsigned)g_rf_transport->getSerialConfig() : 0U);
    }
    else if (strcasecmp(cmd, "rftest tx") == 0)
    {
        runRfUartDiagnostic(false);
    }
    else if (strcasecmp(cmd, "rftest loopback") == 0)
    {
        runRfUartDiagnostic(true);
    }
    else if (strncasecmp(cmd, "rfpins", 6) == 0)
    {
        int tx = -1, rx = -1;
        if (sscanf(cmd + 6, "%d %d", &tx, &rx) == 2 && tx >= 0 && rx >= 0 && tx != rx)
        {
            if (g_rf_transport)
            {
                g_rf_transport->setPins(static_cast<int8_t>(rx), static_cast<int8_t>(tx));
            }
            g_rf_nvs_storage.begin();
            g_rf_nvs_storage.setU32(RF_NVS_UART_TX_PIN_KEY, static_cast<uint32_t>(tx));
            g_rf_nvs_storage.setU32(RF_NVS_UART_RX_PIN_KEY, static_cast<uint32_t>(rx));
            ESP_LOGI(TAG, "RF pins updated to TX=%d (GPIO%d), RX=%d (GPIO%d) and saved to NVS.", tx, tx, rx, rx);
        }
        else
        {
            ESP_LOGW(TAG, "Usage: rfpins <tx_gpio> <rx_gpio> (e.g. 'rfpins 12 13' or 'rfpins 17 18')");
        }
    }
    else if (strncasecmp(cmd, "rfmode", 6) == 0)
    {
        if (strstr(cmd, "8n1") != nullptr || strstr(cmd, "8N1") != nullptr)
        {
            if (g_rf_transport)
                g_rf_transport->setBaudRate(g_rf_transport->getBaudRate(), 0x800001c);
            ESP_LOGI(TAG, "RF UART format set to SERIAL_8N1 (1 stop bit).");
        }
        else if (strstr(cmd, "8n2") != nullptr || strstr(cmd, "8N2") != nullptr)
        {
            if (g_rf_transport)
                g_rf_transport->setBaudRate(g_rf_transport->getBaudRate(), 0x800003c);
            ESP_LOGI(TAG, "RF UART format set to SERIAL_8N2 (2 stop bits, Delphi match).");
        }
        else
        {
            ESP_LOGW(TAG, "Usage: rfmode 8n1 | rfmode 8n2");
        }
    }
    else if (strcasecmp(cmd, "rfsetup") == 0)
    {
        executeRfSetup();
    }
    else if (strncasecmp(cmd, "ping", 4) == 0)
    {
        int node = 1;
        if (strlen(cmd) > 4)
            node = atoi(cmd + 4);
        if (node < 1)
            node = 1;
        executeAguPing(static_cast<uint8_t>(node));
    }
    else if (strncasecmp(cmd, "group", 5) == 0 && (cmd[5] == ' ' || cmd[5] == '\0'))
    {
        // Syntax: group <1..4> on [seconds] | group <1..4> off
        const char *p = cmd + 5;
        while (*p == ' ')
            p++;
        int grp = atoi(p);
        while (*p && *p != ' ')
            p++;
        while (*p == ' ')
            p++;
        bool is_on = false;
        int duration_s = 30;
        if (strncasecmp(p, "on", 2) == 0)
        {
            is_on = true;
            p += 2;
            while (*p == ' ')
                p++;
            if (*p)
                duration_s = atoi(p);
            if (duration_s < 1)
                duration_s = 1;
            if (duration_s > 300)
                duration_s = 300;
        }
        else if (strncasecmp(p, "off", 3) == 0)
        {
            is_on = false;
        }
        else
        {
            ESP_LOGW(TAG, "Usage: group <1..4> on [sec] | group <1..4> off");
            return;
        }
        executeGroupPump(static_cast<uint8_t>(grp), is_on, static_cast<uint32_t>(duration_s));
    }
    else if (strncasecmp(cmd, "on", 2) == 0 && (cmd[2] == ' ' || cmd[2] == '\0'))
    {
        int node = 1;
        int duration_sec = 30; // default 30s
        const char *p = cmd + 2;
        while (*p == ' ')
            p++;
        if (*p)
        {
            node = atoi(p);
            while (*p && *p != ' ')
                p++;
            while (*p == ' ')
                p++;
            if (*p)
                duration_sec = atoi(p);
        }
        if (node < 1)
            node = 1;
        if (duration_sec < 1)
            duration_sec = 1;
        if (duration_sec > 300)
            duration_sec = 300;

        NodeFsmState &fsm = g_node_fsm[node];
        fsm.run_lease_ms = static_cast<uint32_t>(duration_sec) * 1000U;
        g_staggered_schedule[node].is_scheduled_on = false;
        g_staggered_schedule[node].off_retries = 0;
        ESP_LOGI(TAG, "[CLI] PUMP ON node %d for %d seconds (lease=%u ms)", node, duration_sec, (unsigned)fsm.run_lease_ms);
        executeAguPump(static_cast<uint8_t>(node), true);
    }
    else if (strncasecmp(cmd, "off", 3) == 0 && (cmd[3] == ' ' || cmd[3] == '\0'))
    {
        int node = 1;
        if (strlen(cmd) > 3)
            node = atoi(cmd + 3);
        if (node < 1)
            node = 1;
        g_staggered_schedule[node].is_scheduled_on = false;
        g_staggered_schedule[node].off_retries = 0;
        executeAguPump(static_cast<uint8_t>(node), false);
    }
    else if (strcasecmp(cmd, "getid") == 0)
    {
        executeAguGetId();
    }
    else if (strncasecmp(cmd, "setid", 5) == 0)
    {
        int id = 1;
        if (strlen(cmd) > 5)
            id = atoi(cmd + 5);
        executeAguSetId(static_cast<uint8_t>(id));
    }
    else if (strcasecmp(cmd, "poll") == 0)
    {
        ESP_LOGI(TAG, "=== Polling AGU legacy clients (1..15) ===");
        for (uint8_t i = AGU_LEGACY_MIN_NODE_ID; i <= AGU_LEGACY_MAX_NODE_ID; ++i)
        {
            executeAguPing(i);
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        ESP_LOGI(TAG, "=== Polling cycle completed ===");
    }
    else if (strcasecmp(cmd, "rfraw") == 0)
    {
        g_rf_raw_dump = !g_rf_raw_dump;
        ESP_LOGI(TAG, "RF raw RX hex dump is now %s", g_rf_raw_dump ? "ENABLED" : "DISABLED");
    }
    else if (strncasecmp(cmd, "rfbaud", 6) == 0)
    {
        uint32_t baud = 38400;
        if (strlen(cmd) > 6)
            baud = strtoul(cmd + 6, nullptr, 10);
        if (baud >= 1200 && baud <= 115200 && g_rf_transport)
        {
            g_rf_transport->setBaudRate(baud);
            g_rf_nvs_storage.begin();
            g_rf_nvs_storage.setU32(RF_NVS_UART_BAUD_KEY, baud);
            ESP_LOGI(TAG, "RF transport baud rate set to %u and saved to NVS.", (unsigned)baud);
        }
        else
        {
            ESP_LOGW(TAG, "Invalid baud rate: %s", cmd + 6);
        }
    }
    else if (strcasecmp(cmd, "pinscan") == 0)
    {
        ESP_LOGI(TAG, "=== Scanning GPIO pins with internal PULL-DOWN ===");
        const int test_pins[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 21, 38, 39, 40, 41, 42, 47, 48};
        for (int p : test_pins)
        {
            pinMode(p, INPUT_PULLDOWN);
            vTaskDelay(pdMS_TO_TICKS(5));
            int val = digitalRead(p);
            if (val == HIGH)
            {
                ESP_LOGI(TAG, ">>> GPIO %d reads HIGH! (Active external driver detected)", p);
            }
        }
        if (g_rf_transport)
        {
            g_rf_transport->setPins(g_rf_transport->getRxPin(), g_rf_transport->getTxPin());
        }
        ESP_LOGI(TAG, "=== Pin scan completed ===");
    }
    else if (strcasecmp(cmd, "pincheck") == 0)
    {
        const int check_pins[] = {10, 11, 12, 13, 14, 17, 18, 21};
        for (int p : check_pins)
        {
            pinMode(p, INPUT_PULLUP);
            int pu = digitalRead(p);
            pinMode(p, INPUT_PULLDOWN);
            int pd = digitalRead(p);
            pinMode(p, INPUT);
            int fl = digitalRead(p);
            ESP_LOGI(TAG, "GPIO %02d: PULLUP=%d PULLDOWN=%d FLOAT=%d (%s)",
                     p, pu, pd, fl, (pu == 0) ? "SHORTED TO GND!" : (pd == 1) ? "ACTIVE HIGH!"
                                                                              : "NORMAL/FLOATING");
        }

        if (g_rf_transport)
        {
            g_rf_transport->setPins(g_rf_transport->getRxPin(), g_rf_transport->getTxPin());
        }
    }
    else if (strcasecmp(cmd, "scan") == 0)
    {
        executeRfScan("cli_scan");
    }
    else if (strncasecmp(cmd, "claim", 5) == 0)
    {
        uint8_t from_id = 0;
        uint8_t to_id = 0;
        if (sscanf(cmd + 5, "%hhu %hhu", &from_id, &to_id) == 2)
        {
            executeRfClaimNode(from_id, to_id, "cli_claim");
        }
        else
        {
            ESP_LOGW(TAG, "Usage: claim <from_node_id> <to_node_id (1..15)>");
        }
    }
    else if (strcasecmp(cmd, "wifi") == 0)
    {
        printWifiStatus();
    }
    else if (strcasecmp(cmd, "wifireset") == 0)
    {
        g_wifi_storage.clearAllProfiles();
        ESP_LOGI(TAG, "Cleared all Wi-Fi profiles from NVS namespace 'wifi_store'.");
    }
    else if (strcasecmp(cmd, "portal") == 0)
    {
        g_wifi_controller.triggerPortalMode();
        ESP_LOGI(TAG, "Farmer Portal triggered from Serial.");
    }
    else if (strcasecmp(cmd, "factory") == 0)
    {
        g_pending_factory_confirm = true;
        ESP_LOGW(TAG, "CRITICAL: Gateway Factory reset requested! Type 'YES' to confirm NVS flash erasure.");
    }
    else if (strncasecmp(cmd, "liveness", 8) == 0)
    {
        if (strstr(cmd, "0") || strstr(cmd, "off"))
        {
            g_agu_liveness_enabled = false;
            ESP_LOGW(TAG, "[LIVENESS] Periodic AGU ping disabled");
        }
        else
        {
            g_agu_liveness_enabled = true;
            ESP_LOGW(TAG, "[LIVENESS] Periodic AGU ping ENABLED: node firmware must echo "
                          "PING 0x05 or the node will be falsely marked STALE and safe-OFFed.");
            ESP_LOGI(TAG, "[LIVENESS] Periodic AGU ping enabled");
        }
    }
    else if (strncasecmp(cmd, "rfchannel", 9) == 0)
    {
        int ch = 1;
        if (strlen(cmd) > 9)
            ch = atoi(cmd + 9);
        if (ch >= 1 && ch <= 127 && g_rf_transport && g_agu_legacy_host)
        {
            RfBusGuard guard(g_agu_legacy_host, RfTrafficClass::DIAGNOSTIC);
            if (!guard.isLocked())
            {
                ESP_LOGW(TAG, "[RF CHANNEL] Bus busy or in radio silence");
                return;
            }
            char at_ch[32];
            snprintf(at_ch, sizeof(at_ch), "AT+C%03d", ch);
            g_rf_transport->flushRx();
            ESP_LOGI(TAG, "[RF CHANNEL] Setting channel -> %s", at_ch);
            g_rf_transport->send(reinterpret_cast<const uint8_t *>(at_ch), strlen(at_ch));
            vTaskDelay(pdMS_TO_TICKS(500));
            uint8_t resp[64] = {};
            size_t r = g_rf_transport->receive(resp, sizeof(resp) - 1);
            if (r > 0)
            {
                resp[r] = '\0';
                char hex_str[128] = {};
                for (size_t i = 0; i < r && i < 16; ++i)
                {
                    snprintf(hex_str + strlen(hex_str), sizeof(hex_str) - strlen(hex_str), "%02X ", resp[i]);
                }
                ESP_LOGI(TAG, "[RF CHANNEL] Response (%zu bytes: %s): %s", r, hex_str, reinterpret_cast<char *>(resp));
            }
            else
            {
                ESP_LOGW(TAG, "[RF CHANNEL] No response (ensure SET pin is connected to GND)");
            }
        }
        else
        {
            ESP_LOGW(TAG, "Usage: rfchannel <1..127> (e.g. 'rfchannel 7' or 'rfchannel 1')");
        }
    }
    else if (strncasecmp(cmd, "at", 2) == 0)
    {
        if (g_rf_transport && g_agu_legacy_host)
        {
            RfBusGuard guard(g_agu_legacy_host, RfTrafficClass::DIAGNOSTIC);
            if (!guard.isLocked())
            {
                ESP_LOGW(TAG, "[AT TX] Bus busy or in radio silence");
                return;
            }
            g_rf_transport->flushRx();
            char at_cmd[64];
            snprintf(at_cmd, sizeof(at_cmd), "%s", cmd);
            for (char *p = at_cmd; *p; ++p)
                *p = toupper(static_cast<unsigned char>(*p));
            ESP_LOGI(TAG, "[AT TX] Sending without CRLF: %s", at_cmd);
            g_rf_transport->send(reinterpret_cast<const uint8_t *>(at_cmd), strlen(at_cmd));
            vTaskDelay(pdMS_TO_TICKS(500));
            uint8_t resp[128] = {};
            size_t r = g_rf_transport->receive(resp, sizeof(resp) - 1);
            if (r > 0)
            {
                resp[r] = '\0';
                char hex_str[256] = {};
                for (size_t i = 0; i < r && i < 32; ++i)
                {
                    snprintf(hex_str + strlen(hex_str), sizeof(hex_str) - strlen(hex_str), "%02X ", resp[i]);
                }
                ESP_LOGI(TAG, "[AT RX] Response (%zu bytes: %s): %s", r, hex_str, reinterpret_cast<char *>(resp));
            }
            else
            {
                ESP_LOGW(TAG, "[AT RX] No response / timeout");
            }
        }
    }
    else
    {
        ESP_LOGW(TAG, "Unknown Serial command: '%s'. Valid: 'status', 'test', 'rfstatus', 'rftest tx', 'rftest loopback', 'rfpins <tx> <rx>', 'rfmode <8n1|8n2>', 'rfsetup', 'rfchannel <ch>', 'at<...>', 'liveness <0|1>', 'scan', 'claim <from> <to>', 'ping <node>', 'on <node>', 'off <node>', 'getid', 'setid <node>', 'poll', 'rfraw', 'rfbaud <baud>', 'wifi', 'wifireset', 'portal', 'factory'", cmd);
    }
}

static void printWifiStatus()
{
    ESP_LOGI(TAG, "Wi-Fi status: %s | SSID: %s | RSSI: %d dBm | Portal: %s",
             g_wifi_controller.isConnected() ? "CONNECTED" : "DISCONNECTED",
             g_wifi_controller.getCurrentSsid(),
             static_cast<int>(g_wifi_controller.getCurrentRssi()),
             g_wifi_controller.isPortalActive() ? "ACTIVE" : "INACTIVE");
    const WifiConfigBlob &blob = g_wifi_storage.cachedBlob();
    ESP_LOGI(TAG, "Saved Wi-Fi profiles in NVS: %u/%zu", blob.count, MAX_SAVED_WIFI);
    for (uint8_t i = 0; i < blob.count; ++i)
    {
        ESP_LOGI(TAG, "  [%u] SSID='%s' priority=%d",
                 i + 1, blob.profiles[i].ssid, blob.profiles[i].priority);
    }
}

static void printSystemStatus()
{
    SystemTime t = g_rtc_manager.getTime();
    bool is_night = g_rtc_manager.isNightMode();
    bool wifi_ok = (WiFi.status() == WL_CONNECTED);

    ESP_LOGI(TAG, "Gateway status: time=%02u:%02u:%02u valid=%s mode=%s wifi=%s",
             t.hour, t.minute, t.second,
             (t.is_valid ? "YES" : "NO (Fallback)"),
             (is_night ? "NIGHT" : "DAY"),
             (wifi_ok ? "CONNECTED" : "DISCONNECTED"));
    ESP_LOGI(TAG, "RF transport: initialized=%s rx_bytes=%zu",
             (g_rf_transport && g_rf_transport->isInitialized() ? "YES" : "NO"),
             g_rf_transport ? g_rf_transport->available() : 0U);
    ESP_LOGI(TAG, "MQTT gateway: initialized=%s connected=%s",
             (g_mqtt_initialized ? "YES" : "NO"),
             (mqtt_client.isConnected() ? "YES" : "NO"));
}

#endif // ESP_PLATFORM || ARDUINO
