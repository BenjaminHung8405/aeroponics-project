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

// Log tag for main application orchestrator
static const char *TAG = "MAIN";

// Global instances of core hardware and software controllers
static RelayController g_relay_controller;
static NvsStorage g_nvs_storage;
static RtcManager g_rtc_manager;
static ScheduleManager g_schedule_manager;

// Serial command state variables
static bool g_pending_factory_confirm = false;
static uint32_t g_last_wifi_check_ms = 0;
static bool g_wdt_registered = false;
static bool g_boot_successful = false;

// Forward declaration of helper functions
static bool isWifiProvisioned();
static bool configureTaskWdt();
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

    ESP_LOGI(TAG, "Connecting to Wi-Fi network...");
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    uint32_t wifi_start_ms = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - wifi_start_ms < WIFI_CONNECT_TIMEOUT_MS)) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    bool wifi_connected = (WiFi.status() == WL_CONNECTED);
    if (wifi_connected) {
        ESP_LOGI(TAG, "Wi-Fi connected successfully!");
        ESP_LOGI(TAG, "Synchronizing system time with NTP server...");
        bool ntp_ok = g_rtc_manager.syncFromNtp();
        if (ntp_ok) {
            ESP_LOGI(TAG, "NTP time synchronization completed and RTC updated.");
        } else {
            ESP_LOGW(TAG, "NTP time synchronization failed. Relying on RTC internal clock.");
        }
    } else {
        ESP_LOGW(TAG, "Wi-Fi connection timed out after %u ms. Operating in offline fail-safe mode.", (unsigned)WIFI_CONNECT_TIMEOUT_MS);
    }
}

static bool initializeScheduleTasks() {
    bool sm_init = g_schedule_manager.begin(&g_nvs_storage, &g_rtc_manager, &g_relay_controller);
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

void setup() {
    // Step 1: Initialize Serial Communications
    Serial.begin(115200);

    // Step 2: Initialize Relay GPIO Pins (RULE S1-HW-01 ENFORCEMENT - HARD REQUIREMENT)
    // MUST BE THE VERY FIRST HARDWARE CALL AFTER Serial.begin TO PREVENT RELAY GLITCHING
    g_relay_controller.initPins();

    // Step 3: Run Hardware Concurrency Fault-Injection Self-Test to prove Emergency Latch
    bool test_passed = g_relay_controller.testFaultInjectionEmergency(0);
    if (test_passed) {
        ESP_LOGI(TAG, "Emergency fail-safe concurrency fault-injection self-test: PASS");
    } else {
        ESP_LOGE(TAG, "Emergency fail-safe concurrency fault-injection self-test: FAIL");
    }

    // Step 4: Configure Task Watchdog Timer
    bool wdt_ok = configureTaskWdt();
    if (wdt_ok) {
        esp_err_t add_err = esp_task_wdt_add(NULL);
        if (add_err == ESP_OK || add_err == ESP_ERR_INVALID_STATE) {
            g_wdt_registered = true;
            ESP_LOGI(TAG, "Main loop task registered with Task WDT successfully.");
        } else {
            ESP_LOGE(TAG, "Failed to register main loop task with Task WDT: 0x%x", add_err);
            wdt_ok = false;
        }
    }

    if (!wdt_ok) {
        ESP_LOGE(TAG, "CRITICAL: Task WDT setup or registration failed! Forcing all relays OFF and blocking relay task launch.");
        for (uint8_t i = 0; i < TOTAL_RELAYS; i++) {
            g_relay_controller.forceRelayOffEmergency(i);
        }
        g_boot_successful = false;
        return;
    }

    // Step 5 & 6: NVS & RTC Init
    initializeNvs();
    initializeRtc();

    // Step 7 & 8: Wi-Fi Non-Blocking Connection & NTP Sync
    connectWifiWithTimeout();

    // Step 9: Schedule Manager Init & FreeRTOS Tasks Launch
    g_boot_successful = initializeScheduleTasks();

    // Step 10: Log Boot Status
    if (g_boot_successful) {
        ESP_LOGI(TAG, "Boot Complete");
    } else {
        ESP_LOGE(TAG, "CRITICAL: Boot sequence incomplete due to task creation failure! Safe state active.");
    }
}

void loop() {
    if (g_wdt_registered) {
        esp_err_t err = esp_task_wdt_reset();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "CRITICAL WDT FAILURE: esp_task_wdt_reset in main loop failed: 0x%x! Latching safe-state for all relays & restarting...", err);
            for (uint8_t i = 0; i < TOTAL_RELAYS; i++) {
                g_relay_controller.forceRelayOffEmergency(i);
            }
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

    // Parse and handle Serial debug commands (100% non-blocking)
    processSerialCommands();
}

/**
 * @brief Non-blocking reading and buffering of incoming Serial bytes.
 */
static void processSerialCommands() {
    static char buffer[128];
    static size_t buf_idx = 0;
    static bool discarding_overflow = false;

    while (Serial.available() > 0) {
        char c = static_cast<char>(Serial.read());
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
        ESP_LOGI(TAG, "Running manual fault-injection concurrency test on Relay 0...");
        g_relay_controller.testFaultInjectionEmergency(0);
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
