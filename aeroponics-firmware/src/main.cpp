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

// Forward declaration of helper functions
static void initializeNvs();
static void initializeRtc();
static void connectWifiWithTimeout();
static void initializeScheduleTasks();
static void processSerialCommands();
static void handleCommand(const char *cmd);
static void handleFactoryResetConfirmation(const char *cmd);
static void handleOverrideCommand(const char *cmd);
static void printSystemStatus();

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
    ESP_LOGI(TAG, "Connecting to Wi-Fi network '%s'...", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    uint32_t wifi_start_ms = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - wifi_start_ms < WIFI_CONNECT_TIMEOUT_MS)) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    bool wifi_connected = (WiFi.status() == WL_CONNECTED);
    if (wifi_connected) {
        ESP_LOGI(TAG, "Wi-Fi connected successfully! IP Address: %s", WiFi.localIP().toString().c_str());
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

static void initializeScheduleTasks() {
    bool sm_init = g_schedule_manager.begin(&g_nvs_storage, &g_rtc_manager, &g_relay_controller);
    if (!sm_init) {
        ESP_LOGE(TAG, "Failed to initialize ScheduleManager dependency injection.");
    } else {
        bool tasks_started = g_schedule_manager.startAllTasks();
        if (!tasks_started) {
            ESP_LOGE(TAG, "Failed to start FreeRTOS tasks for relay channels.");
        } else {
            ESP_LOGI(TAG, "All FreeRTOS relay background tasks started successfully.");
        }
    }
}

void setup() {
    // Step 1: Initialize Serial Communications
    Serial.begin(115200);

    // Step 2: Initialize Relay GPIO Pins (RULE S1-HW-01 ENFORCEMENT - HARD REQUIREMENT)
    // MUST BE THE VERY FIRST HARDWARE CALL AFTER Serial.begin TO PREVENT RELAY GLITCHING
    g_relay_controller.initPins();

    // Step 3 & 4: NVS Storage Init & Profile Load
    initializeNvs();

    // Step 5 & 6: I2C & RTC Manager Init
    initializeRtc();

    // Step 7 & 8: Wi-Fi Non-Blocking Connection & NTP Sync
    connectWifiWithTimeout();

    // Step 9: Schedule Manager Init & FreeRTOS Tasks Launch
    initializeScheduleTasks();

    // Step 10: Log Boot Complete Signal
    ESP_LOGI(TAG, "Boot Complete");
}

void loop() {
    // Feed the Task Watchdog Timer for the main loop task
    esp_task_wdt_reset();

    // Check Wi-Fi connection status every 60 seconds (non-blocking)
    uint32_t current_ms = millis();
    if (current_ms - g_last_wifi_check_ms >= 60000) {
        g_last_wifi_check_ms = current_ms;
        if (WiFi.status() != WL_CONNECTED) {
            ESP_LOGW(TAG, "Wi-Fi disconnected. Attempting non-blocking reconnect...");
            WiFi.reconnect();
        }
    }

    // Parse and handle Serial debug commands
    processSerialCommands();

    // Lightweight yield to give background FreeRTOS tasks execution time
    vTaskDelay(pdMS_TO_TICKS(100));
}

/**
 * @brief Non-blocking reading and buffering of incoming Serial bytes.
 */
static void processSerialCommands() {
    static char buffer[128];
    static size_t buf_idx = 0;

    while (Serial.available() > 0) {
        char c = static_cast<char>(Serial.read());
        if (c == '\r' || c == '\n') {
            if (buf_idx > 0) {
                buffer[buf_idx] = '\0';
                handleCommand(buffer);
                buf_idx = 0;
            }
        } else if (buf_idx < sizeof(buffer) - 1) {
            buffer[buf_idx++] = c;
        }
    }
}

static void handleFactoryResetConfirmation(const char *cmd) {
    if (strcasecmp(cmd, "YES") == 0) {
        ESP_LOGW(TAG, "Executing NVS Factory Reset as confirmed by user...");
        bool ok = g_nvs_storage.factoryReset();
        if (ok) {
            ESP_LOGI(TAG, "Factory reset successful. Restarting ESP32 in 1 second...");
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_restart();
        } else {
            ESP_LOGE(TAG, "Factory reset failed during NVS erase.");
        }
    } else {
        ESP_LOGI(TAG, "Factory reset request cancelled.");
    }
    g_pending_factory_confirm = false;
}

static void handleOverrideCommand(const char *cmd) {
    unsigned long long relay_id_input = 0;
    char token2[16] = {0};
    char token3[32] = {0};
    char extra_token[16] = {0};

    int count = sscanf(cmd, "override %llu %15s %31s %15s", &relay_id_input, token2, token3, extra_token);

    if (count < 2) {
        ESP_LOGW(TAG, "Invalid override command format. Usage: override <id> <on|off> <seconds> OR override <id> cancel");
        return;
    }

    if (relay_id_input >= TOTAL_RELAYS) {
        ESP_LOGE(TAG, "Invalid relay_id %llu. Valid range: [0..%u]", relay_id_input, (unsigned)(TOTAL_RELAYS - 1));
        return;
    }

    const uint8_t relay_id = static_cast<uint8_t>(relay_id_input);

    if (strcasecmp(token2, "cancel") == 0) {
        if (count > 2) {
            ESP_LOGE(TAG, "Extra token '%s' rejected in override cancel command.", token3);
            return;
        }
        g_relay_controller.cancelOverride(relay_id);
        ESP_LOGI(TAG, "Manual override cancelled for Relay channel %u.", relay_id);
        return;
    }

    if (count < 3) {
        ESP_LOGW(TAG, "Missing duration for override command. Usage: override <id> <on|off> <seconds>");
        return;
    }

    if (count > 3) {
        ESP_LOGE(TAG, "Extra token '%s' rejected in override command.", extra_token);
        return;
    }

    RelayState forced_state = RELAY_OFF;
    bool valid_state = false;
    if (strcasecmp(token2, "on") == 0 || strcmp(token2, "1") == 0) {
        forced_state = RELAY_ON;
        valid_state = true;
    } else if (strcasecmp(token2, "off") == 0 || strcmp(token2, "0") == 0) {
        forced_state = RELAY_OFF;
        valid_state = true;
    }

    if (!valid_state) {
        ESP_LOGE(TAG, "Invalid override state '%s'. Use 'on' or 'off'.", token2);
        return;
    }

    if (token3[0] == '-') {
        ESP_LOGE(TAG, "Invalid duration '%s'. Must be a positive integer.", token3);
        return;
    }

    char *endptr = nullptr;
    errno = 0;
    unsigned long long duration_input = strtoull(token3, &endptr, 10);
    if (errno != 0 || endptr == token3 || *endptr != '\0' || duration_input > UINT32_MAX) {
        ESP_LOGE(TAG, "Invalid duration '%s'. Exceeds max 32-bit limit or invalid number.", token3);
        return;
    }

    const uint32_t duration_s = static_cast<uint32_t>(duration_input);
    bool ok = g_relay_controller.startManualOverride(relay_id, forced_state, duration_s);
    if (ok) {
        ESP_LOGI(TAG, "Manual override started: Relay %u set to %s for %u s",
                 relay_id, (forced_state == RELAY_ON ? "ON" : "OFF"), (unsigned)duration_s);
    } else {
        ESP_LOGE(TAG, "Failed to start manual override for Relay %u (duration %u s outside [%u..%u])",
                 relay_id, (unsigned)duration_s, (unsigned)MIN_OVERRIDE_DURATION_S, (unsigned)MAX_OVERRIDE_DURATION_S);
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
    } else if (strncasecmp(cmd, "override", 8) == 0 && (cmd[8] == ' ' || cmd[8] == '\0')) {
        handleOverrideCommand(cmd);
    } else if (strcasecmp(cmd, "factory") == 0) {
        g_pending_factory_confirm = true;
        ESP_LOGW(TAG, "CRITICAL: Factory reset requested! Type 'YES' to confirm NVS flash erasure.");
    } else {
        ESP_LOGW(TAG, "Unknown Serial command: '%s'. Valid commands: 'status', 'override <id> <on|off> <seconds>', 'factory'", cmd);
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

        ESP_LOGI(TAG, "Relay [%u] -> Pin State: %-3s | Phase: %-12s | Phase Rem: %5us | Override Active: %-3s (Rem: %us, Forced: %s)",
                 i,
                 (pin_state == RELAY_ON ? "ON" : "OFF"),
                 (rt.phase == PHASE_SPRAYING ? "SPRAYING" : "COOLING_DOWN"),
                 rt.phase_remaining_s,
                 (ov.active ? "YES" : "NO"),
                 ov.remaining_s,
                 (ov.forced_state == RELAY_ON ? "ON" : "OFF"));
    }
    ESP_LOGI(TAG, "===================================");
}
