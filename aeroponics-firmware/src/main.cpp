#if defined(ESP_PLATFORM) || defined(ARDUINO)

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <esp_log.h>
#include <esp_task_wdt.h>
#include <cstring>
#include <strings.h>
#include <cerrno>
#include <cstdlib>
#include <cstdint>

#include "config.h"
#include "nvs_storage.h"
#include "rtc_manager.h"
#include "relay_controller.h"
#include "schedule_manager.h"
#include "FreeRTOSTaskRunner.h"

// Log tag for main application orchestrator
static const char *TAG = "MAIN";

// Global instances of core hardware and software controllers
static RelayController g_relay_controller;
static NvsStorage g_nvs_storage;
static RtcManager g_rtc_manager;
static FreeRTOSTaskRunner g_task_runner;
static ScheduleManager g_schedule_manager;

// Serial command state variables
static bool g_pending_factory_confirm = false;
static uint32_t g_last_wifi_check_ms = 0;
static bool g_wdt_registered = false;
static bool g_boot_successful = false;

// Forward declaration of helper functions
static bool isWifiProvisioned();
static bool configureTaskWdt();
static bool setupMainWdt();
static void initializeNvs();
static void initializeRtc();
static void connectWifiWithTimeout();
static bool initializeScheduleTasks();
static void processSerialCommands();
static void handleCommand(const char *cmd);
static void handleFactoryResetConfirmation(const char *cmd);
static bool executeOverrideDuration(uint8_t relay_id, RelayState forced_state, const char *duration_token);
static void handleOverrideCommand(const char *cmd);
static void printSystemStatus();
static void runSystemDiagnostics();

static bool isWifiProvisioned() {
    return (WIFI_SSID[0] != '\0' && strcmp(WIFI_SSID, "CHANGE_ME") != 0);
}

static bool configureTaskWdt() {
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    esp_task_wdt_config_t twdt_config = {
        .timeout_ms = WDT_TIMEOUT_S * 1000,
        .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,
        .trigger_panic = true
    };
    esp_err_t err = esp_task_wdt_init(&twdt_config);
    if (err == ESP_ERR_INVALID_STATE) {
        err = esp_task_wdt_reconfigure(&twdt_config);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure Task WDT: %s (0x%x)", esp_err_to_name(err), err);
        return false;
    }
#else
    esp_err_t err = esp_task_wdt_init(WDT_TIMEOUT_S, true);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to init Task WDT: %s (0x%x)", esp_err_to_name(err), err);
        return false;
    }
#endif
    ESP_LOGI(TAG, "Task WDT configured/reconfigured with timeout %u s", WDT_TIMEOUT_S);
    return true;
}

static bool setupMainWdt() {
    bool wdt_ok = configureTaskWdt();
    if (!wdt_ok) {
        return false;
    }

    esp_err_t add_err = esp_task_wdt_add(NULL);
    bool is_added = false;

    if (add_err == ESP_OK) {
        is_added = true;
    } else if (add_err == ESP_ERR_INVALID_STATE) {
        esp_err_t stat_err = esp_task_wdt_status(NULL);
        if (stat_err == ESP_OK) {
            is_added = true;
            ESP_LOGI(TAG, "Main loop task was already subscribed to Task WDT.");
        } else {
            ESP_LOGE(TAG, "esp_task_wdt_add returned INVALID_STATE and status verification failed (0x%x) for main loop task", stat_err);
        }
    } else {
        ESP_LOGE(TAG, "esp_task_wdt_add failed for main loop task: 0x%x", add_err);
    }

    if (!is_added) {
        g_wdt_registered = false;
        return false;
    }

    esp_err_t reset_err = esp_task_wdt_reset();
    if (reset_err != ESP_OK) {
        ESP_LOGE(TAG, "Initial esp_task_wdt_reset verification failed for main loop task: 0x%x", reset_err);
        esp_task_wdt_delete(NULL);
        g_wdt_registered = false;
        return false;
    }

    g_wdt_registered = true;
    ESP_LOGI(TAG, "Main loop task registered and verified with Task WDT successfully.");
    return true;
}

static void initializeNvs() {
    bool nvs_ok = g_nvs_storage.begin();
    if (!nvs_ok) {
        ESP_LOGW(TAG, "NVS storage init failed. System will operate using hardcoded defaults.");
    }

    RelayProfile initial_profiles[TOTAL_RELAYS];
    bool profiles_ok = g_nvs_storage.loadAllProfiles(initial_profiles);
    if (!profiles_ok) {
        ESP_LOGW(TAG, "Failed to load profiles from NVS storage. Fallback default profiles applied.");
    }
}

static void initializeRtc() {
    Wire.begin(RTC_SDA_PIN, RTC_SCL_PIN);
    bool rtc_ok = g_rtc_manager.begin();
    if (!rtc_ok) {
        ESP_LOGW(TAG, "RTC DS3231 initialization failed or hardware not detected. Fallback system time will be used.");
    }
}

static void connectWifiWithTimeout() {
    if (!isWifiProvisioned()) {
        ESP_LOGI(TAG, "Wi-Fi credentials not provisioned. Skipping Wi-Fi connection and operating in offline fail-safe mode.");
        return;
    }

    ESP_LOGI(TAG, "Connecting to Wi-Fi network (timeout limit: %u ms)...", (unsigned)WIFI_CONNECT_TIMEOUT_MS);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    uint32_t wifi_start_ms = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - wifi_start_ms < WIFI_CONNECT_TIMEOUT_MS)) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    bool wifi_connected = (WiFi.status() == WL_CONNECTED);
    uint32_t elapsed_ms = millis() - wifi_start_ms;
    if (wifi_connected) {
        ESP_LOGI(TAG, "Wi-Fi connected successfully in %u ms!", (unsigned)elapsed_ms);
        ESP_LOGI(TAG, "Synchronizing system time with NTP server...");
        bool ntp_ok = g_rtc_manager.syncFromNtp();
        if (ntp_ok) {
            ESP_LOGI(TAG, "NTP time synchronization completed and RTC updated.");
        } else {
            ESP_LOGW(TAG, "NTP time synchronization failed or timed out. Relying on RTC internal clock.");
        }
    } else {
        ESP_LOGW(TAG, "Wi-Fi connection timed out after %u ms. Operating in offline fail-safe mode.", (unsigned)elapsed_ms);
    }
}

static bool initializeScheduleTasks() {
    bool sm_init = g_schedule_manager.begin(&g_nvs_storage, &g_rtc_manager, &g_relay_controller, nullptr, &g_task_runner);
    if (!sm_init) {
        ESP_LOGE(TAG, "Failed to initialize ScheduleManager dependency injection.");
        return false;
    }
    bool tasks_started = g_schedule_manager.startAllTasks();
    if (!tasks_started) {
        ESP_LOGE(TAG, "Failed to start FreeRTOS tasks for relay channels.");
        return false;
    }
    ESP_LOGI(TAG, "All FreeRTOS relay background tasks started successfully.");
    return true;
}

static void latchAllRelaysOff(const char *reason) {
    ESP_LOGE(TAG, "Latching emergency safe-state for ALL relays! Reason: %s", reason);
    for (uint8_t i = 0; i < TOTAL_RELAYS; ++i) {
        g_relay_controller.forceRelayOffEmergency(i);
    }
}

void setup() {
    // Step 1: Initialize Serial Communications
    Serial.begin(115200);

    // Step 2: Initialize Relay GPIO Pins (RULE S1-HW-01 ENFORCEMENT - HARD REQUIREMENT)
    // MUST BE THE VERY FIRST HARDWARE CALL AFTER Serial.begin TO PREVENT RELAY GLITCHING
    g_relay_controller.initPins();

    // Step 3 & 4: NVS & RTC Init
    initializeNvs();
    initializeRtc();

    // Step 5: Wi-Fi Non-Blocking Connection (30s timeout) & NTP Sync (10s timeout)
    connectWifiWithTimeout();

    // Step 6: Configure & Register Task Watchdog Timer for Main Loop Task
    if (!setupMainWdt()) {
        latchAllRelaysOff("Task WDT setup or registration failed");
        g_boot_successful = false;
        return;
    }

    // Step 7: Schedule Manager Init & FreeRTOS Tasks Launch
    g_boot_successful = initializeScheduleTasks();

    // Step 8: Log Boot Status
    if (g_boot_successful) {
        ESP_LOGI(TAG, "Boot Complete");
    } else {
        latchAllRelaysOff("Boot sequence incomplete due to task creation failure");
    }
}

void loop() {
    if (!g_boot_successful) {
        static uint32_t last_fail_tick_ms = 0;
        uint32_t now = millis();
        if (now - last_fail_tick_ms >= 1000) {
            last_fail_tick_ms = now;
            latchAllRelaysOff("Boot failed safe-state hold");
        }
        if (g_wdt_registered) {
            esp_task_wdt_reset();
        }
        processSerialCommands();
        return;
    }

    if (g_wdt_registered) {
        esp_err_t err = esp_task_wdt_reset();
        if (err != ESP_OK) {
            latchAllRelaysOff("Main loop esp_task_wdt_reset failed");
            esp_restart();
        }
    }

    // Check Wi-Fi connection status every 60 seconds (non-blocking)
    if (isWifiProvisioned()) {
        uint32_t current_ms = millis();
        if (current_ms - g_last_wifi_check_ms >= 60000) {
            g_last_wifi_check_ms = current_ms;
            if (WiFi.status() != WL_CONNECTED) {
                ESP_LOGW(TAG, "Wi-Fi disconnected. Attempting non-blocking reconnect...");
                WiFi.reconnect();
            }
        }
    }

    // Parse and handle Serial debug commands (100% non-blocking with finite work budget)
    processSerialCommands();
}

/**
 * @brief Non-blocking reading and buffering of incoming Serial bytes with work budget.
 */
static void processSerialCommands() {
    static char buffer[128];
    static size_t buf_idx = 0;
    static bool discarding_overflow = false;
    static constexpr size_t MAX_SERIAL_BYTES_PER_TICK = 64;

    size_t bytes_processed = 0;
    while (Serial.available() > 0 && bytes_processed < MAX_SERIAL_BYTES_PER_TICK) {
        char c = static_cast<char>(Serial.read());
        bytes_processed++;

        if (c == '\r' || c == '\n') {
            if (discarding_overflow) {
                ESP_LOGE(TAG, "Serial line exceeded buffer limit (127 bytes). Line discarded.");
                discarding_overflow = false;
                buf_idx = 0;
            } else if (buf_idx > 0) {
                buffer[buf_idx] = '\0';
                handleCommand(buffer);
                buf_idx = 0;
            }
        } else {
            if (discarding_overflow) {
                continue;
            }
            if (buf_idx < sizeof(buffer) - 1) {
                buffer[buf_idx++] = c;
            } else {
                discarding_overflow = true;
                buf_idx = 0;
            }
        }
    }
}

static void handleFactoryResetConfirmation(const char *cmd) {
    if (strcasecmp(cmd, "YES") == 0) {
        ESP_LOGW(TAG, "Executing NVS Factory Reset as confirmed by user...");
        bool ok = g_nvs_storage.factoryReset();
        if (ok) {
            ESP_LOGI(TAG, "Factory reset successful. Restarting ESP32 immediately...");
            esp_restart();
        } else {
            ESP_LOGE(TAG, "Factory reset failed during NVS erase.");
        }
    } else {
        ESP_LOGI(TAG, "Factory reset request cancelled.");
    }
    g_pending_factory_confirm = false;
}

static bool executeOverrideDuration(uint8_t relay_id, RelayState forced_state, const char *duration_token) {
    if (duration_token[0] == '-') {
        ESP_LOGE(TAG, "Invalid duration '%s'. Must be a positive integer.", duration_token);
        return false;
    }

    char *endptr = nullptr;
    errno = 0;
    unsigned long long duration_input = strtoull(duration_token, &endptr, 10);
    if (errno != 0 || endptr == duration_token || *endptr != '\0' || duration_input > UINT32_MAX) {
        ESP_LOGE(TAG, "Invalid duration '%s'. Exceeds max 32-bit limit or invalid number.", duration_token);
        return false;
    }

    const uint32_t duration_s = static_cast<uint32_t>(duration_input);
    bool ok = g_relay_controller.startManualOverride(relay_id, forced_state, duration_s);
    if (ok) {
        ESP_LOGI(TAG, "Manual override started: Relay %u set to %s for %u s",
                 relay_id, (forced_state == RELAY_ON ? "ON" : "OFF"), (unsigned)duration_s);
    } else {
        ESP_LOGE(TAG, "Failed to start manual override for Relay %u. Latching safe-state...", relay_id);
        g_relay_controller.forceRelayOffEmergency(relay_id);
    }
    return ok;
}

static void handleOverrideCommand(const char *cmd) {
    unsigned long long relay_id_input = 0;
    char token2[16] = {0};
    char token3[32] = {0};
    char extra_token[16] = {0};

    int count = sscanf(cmd, "override %llu %15s %31s %15s", &relay_id_input, token2, token3, extra_token);
    if (count < 2 || relay_id_input >= TOTAL_RELAYS) {
        ESP_LOGW(TAG, "Invalid override command or relay_id %llu. Valid range: [0..%u]", relay_id_input, (unsigned)(TOTAL_RELAYS - 1));
        return;
    }

    const uint8_t relay_id = static_cast<uint8_t>(relay_id_input);
    if (strcasecmp(token2, "cancel") == 0) {
        if (count > 2) { ESP_LOGE(TAG, "Extra token '%s' rejected in override cancel command.", token3); return; }
        bool ok = g_relay_controller.cancelOverride(relay_id);
        if (ok) {
            ESP_LOGI(TAG, "Manual override cancelled successfully for Relay channel %u.", relay_id);
        } else {
            ESP_LOGE(TAG, "Failed to cancel manual override for Relay channel %u (mutex timeout or invalid state). Latching safe-state...", relay_id);
            g_relay_controller.forceRelayOffEmergency(relay_id);
        }
        return;
    }

    if (count < 3 || count > 3) {
        ESP_LOGW(TAG, "Invalid override command tokens. Usage: override <id> <on|off> <seconds>");
        return;
    }

    RelayState forced_state = RELAY_OFF;
    if (strcasecmp(token2, "on") == 0 || strcmp(token2, "1") == 0) {
        forced_state = RELAY_ON;
    } else if (strcasecmp(token2, "off") == 0 || strcmp(token2, "0") == 0) {
        forced_state = RELAY_OFF;
    } else {
        ESP_LOGE(TAG, "Invalid override state '%s'. Use 'on' or 'off'.", token2);
        return;
    }

    executeOverrideDuration(relay_id, forced_state, token3);
}

/**
 * @brief Performs read-only system diagnostics and verifies production relay task health & WDT status.
 */
static void runSystemDiagnostics() {
    ESP_LOGI(TAG, "=== SYSTEM DIAGNOSTICS & WDT REGRESSION CHECK ===");
    ESP_LOGI(TAG, "Boot Status: %s | Main Task WDT Registered: %s",
             (g_boot_successful ? "SUCCESS" : "FAILED"),
             (g_wdt_registered ? "YES" : "NO"));
    bool all_tasks_ok = true;
    for (uint8_t i = 0; i < TOTAL_RELAYS; i++) {
        bool alive = g_schedule_manager.isTaskAlive(i);
        bool wdt_ok = g_schedule_manager.isTaskWdtRegistered(i);
        bool latched = g_relay_controller.isFaultLatched(i);
        RelayState st = g_relay_controller.getRelayState(i);
        if (!alive || !wdt_ok || latched) {
            all_tasks_ok = false;
        }
        ESP_LOGI(TAG, "Relay Channel [%u] -> Task Alive: %s | WDT Registered: %s | Latched: %s | State: %s",
                 i, (alive ? "YES" : "NO"), (wdt_ok ? "YES" : "NO"), (latched ? "YES" : "NO"), (st == RELAY_ON ? "ON" : "OFF"));
    }
    if (all_tasks_ok) {
        ESP_LOGI(TAG, "REGRESSION CHECK PASSED: All 4 production relay tasks maintain WDT registration and normal scheduler cycle.");
    } else {
        ESP_LOGE(TAG, "REGRESSION CHECK FAILED: One or more production relay tasks lost WDT registration or entered fault latch!");
    }
}

/**
 * @brief Dispatcher for parsed Serial text commands.
 */
static void handleCommand(const char *cmd) {
    if (cmd == nullptr || strlen(cmd) == 0) {
        return;
    }

    if (g_pending_factory_confirm) {
        handleFactoryResetConfirmation(cmd);
        return;
    }

    if (strcasecmp(cmd, "status") == 0) {
        printSystemStatus();
    } else if (strcasecmp(cmd, "test") == 0) {
        runSystemDiagnostics();
    } else if (strncasecmp(cmd, "override", 8) == 0 && (cmd[8] == ' ' || cmd[8] == '\0')) {
        handleOverrideCommand(cmd);
    } else if (strcasecmp(cmd, "factory") == 0) {
        g_pending_factory_confirm = true;
        ESP_LOGW(TAG, "CRITICAL: Factory reset requested! Type 'YES' to confirm NVS flash erasure.");
    } else {
        ESP_LOGW(TAG, "Unknown Serial command: '%s'. Valid commands: 'status', 'test', 'override <id> <on|off> <seconds>', 'factory'", cmd);
    }
}

/**
 * @brief Pretty prints current system clock, mode, and channel status for all 4 relays.
 */
static void printSystemStatus() {
    SystemTime t = g_rtc_manager.getTime();
    bool is_night = g_rtc_manager.isNightMode();
    bool wifi_ok = (WiFi.status() == WL_CONNECTED);

    ESP_LOGI(TAG, "=== AEROPONICS FIRMWARE STATUS ===");
    ESP_LOGI(TAG, "System Time: %02u:%02u:%02u (Valid: %s) | Mode: %s | Wi-Fi: %s",
             t.hour, t.minute, t.second,
             (t.is_valid ? "YES" : "NO (Fallback)"),
             (is_night ? "NIGHT" : "DAY"),
             (wifi_ok ? "CONNECTED" : "DISCONNECTED"));

    for (uint8_t i = 0; i < TOTAL_RELAYS; i++) {
        RelayState pin_state = g_relay_controller.getRelayState(i);
        RelayOverrideState ov = g_relay_controller.getOverrideState(i);
        RelayRuntimeState rt = g_schedule_manager.getRuntimeState(i);
        bool latched = g_relay_controller.isFaultLatched(i);

        ESP_LOGI(TAG, "Relay [%u] -> Pin State: %-3s | Fault Latched: %-3s | Phase: %-12s | Phase Rem: %5us | Override Active: %-3s (Rem: %us, Forced: %s)",
                 i,
                 (pin_state == RELAY_ON ? "ON" : "OFF"),
                 (latched ? "YES" : "NO"),
                 (rt.phase == PHASE_SPRAYING ? "SPRAYING" : "COOLING_DOWN"),
                 rt.phase_remaining_s,
                 (ov.active ? "YES" : "NO"),
                 ov.remaining_s,
                 (ov.forced_state == RELAY_ON ? "ON" : "OFF"));
    }
    ESP_LOGI(TAG, "===================================");
}

#endif // ESP_PLATFORM || ARDUINO
