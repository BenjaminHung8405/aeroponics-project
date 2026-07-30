#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <esp_log.h>
#include <esp_task_wdt.h>
#include <cstring>
#include <strings.h>

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

// Forward declaration of Serial command handlers
static void processSerialCommands();
static void handleCommand(const char *cmd);
static void printSystemStatus();

void setup() {
    // -------------------------------------------------------------------------
    // Step 1: Initialize Serial Communications
    // -------------------------------------------------------------------------
    Serial.begin(115200);

    // -------------------------------------------------------------------------
    // Step 2: Initialize Relay GPIO Pins (RULE S1-HW-01 ENFORCEMENT - HARD REQUIREMENT)
    // MUST BE THE VERY FIRST HARDWARE CALL AFTER Serial.begin TO PREVENT RELAY GLITCHING
    // -------------------------------------------------------------------------
    g_relay_controller.initPins();

    // -------------------------------------------------------------------------
    // Step 3: Initialize Non-Volatile Storage (NVS) Flash
    // -------------------------------------------------------------------------
    bool nvs_ok = g_nvs_storage.begin();
    if (!nvs_ok) {
        ESP_LOGW(TAG, "NVS storage init failed. System will operate using hardcoded defaults.");
    }

    // -------------------------------------------------------------------------
    // Step 4: Load Relay Profiles from NVS Storage
    // -------------------------------------------------------------------------
    RelayProfile initial_profiles[TOTAL_RELAYS];
    bool profiles_ok = g_nvs_storage.loadAllProfiles(initial_profiles);
    if (!profiles_ok) {
        ESP_LOGW(TAG, "Failed to load profiles from NVS storage. Fallback default profiles applied.");
    }

    // -------------------------------------------------------------------------
    // Step 5: Initialize I2C Bus for Hardware RTC
    // -------------------------------------------------------------------------
    Wire.begin(RTC_SDA_PIN, RTC_SCL_PIN);

    // -------------------------------------------------------------------------
    // Step 6: Initialize Real-Time Clock (DS3231) Manager
    // -------------------------------------------------------------------------
    bool rtc_ok = g_rtc_manager.begin();
    if (!rtc_ok) {
        ESP_LOGW(TAG, "RTC DS3231 initialization failed or hardware not detected. Fallback system time will be used.");
    }

    // -------------------------------------------------------------------------
    // Step 7: Connect to Wi-Fi Network with 30s Timeout (Non-Blocking Pattern)
    // -------------------------------------------------------------------------
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
    } else {
        ESP_LOGW(TAG, "Wi-Fi connection timed out after %u ms. Operating in offline fail-safe mode.", (unsigned)WIFI_CONNECT_TIMEOUT_MS);
    }

    // -------------------------------------------------------------------------
    // Step 8: Perform NTP Synchronization if Wi-Fi is Connected
    // -------------------------------------------------------------------------
    if (wifi_connected) {
        ESP_LOGI(TAG, "Synchronizing system time with NTP server...");
        bool ntp_ok = g_rtc_manager.syncFromNtp();
        if (ntp_ok) {
            ESP_LOGI(TAG, "NTP time synchronization completed and RTC updated.");
        } else {
            ESP_LOGW(TAG, "NTP time synchronization failed. Relying on RTC internal clock.");
        }
    }

    // -------------------------------------------------------------------------
    // Step 9: Initialize Schedule Manager & Start FreeRTOS Tasks
    // -------------------------------------------------------------------------
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

    // -------------------------------------------------------------------------
    // Step 10: Log Boot Complete Signal
    // -------------------------------------------------------------------------
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

/**
 * @brief Dispatcher for parsed Serial text commands.
 * Commands supported:
 *  - "status" : Displays detailed system runtime and relay channel states.
 *  - "override <id> <on|off> <seconds>" OR "override <id> cancel" : Triggers or cancels manual relay override.
 *  - "factory" : Prompts confirmation for NVS factory reset.
 */
static void handleCommand(const char *cmd) {
    if (cmd == nullptr || strlen(cmd) == 0) {
        return;
    }

    // Handle factory reset confirmation prompt
    if (g_pending_factory_confirm) {
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
        return;
    }

    // Process standard commands
    if (strcasecmp(cmd, "status") == 0) {
        printSystemStatus();
    } else if (strncasecmp(cmd, "override", 8) == 0) {
        uint8_t relay_id = 0;
        char state_buf[16] = {0};
        uint32_t duration_s = 0;

        if (sscanf(cmd, "override %hhu cancel", &relay_id) == 1) {
            if (relay_id < TOTAL_RELAYS) {
                g_relay_controller.cancelOverride(relay_id);
                ESP_LOGI(TAG, "Manual override cancelled for Relay channel %u.", relay_id);
            } else {
                ESP_LOGE(TAG, "Invalid relay_id %u. Valid range: [0..%u]", relay_id, TOTAL_RELAYS - 1);
            }
        } else if (sscanf(cmd, "override %hhu %15s %u", &relay_id, state_buf, &duration_s) == 3) {
            if (relay_id >= TOTAL_RELAYS) {
                ESP_LOGE(TAG, "Invalid relay_id %u. Valid range: [0..%u]", relay_id, TOTAL_RELAYS - 1);
            } else {
                RelayState forced_state = RELAY_OFF;
                bool valid_state = false;
                if (strcasecmp(state_buf, "on") == 0 || strcmp(state_buf, "1") == 0) {
                    forced_state = RELAY_ON;
                    valid_state = true;
                } else if (strcasecmp(state_buf, "off") == 0 || strcmp(state_buf, "0") == 0) {
                    forced_state = RELAY_OFF;
                    valid_state = true;
                }

                if (!valid_state) {
                    ESP_LOGE(TAG, "Invalid override state '%s'. Use 'on' or 'off'.", state_buf);
                } else {
                    bool ok = g_relay_controller.startManualOverride(relay_id, forced_state, duration_s);
                    if (ok) {
                        ESP_LOGI(TAG, "Manual override started: Relay %u set to %s for %u s",
                                 relay_id, (forced_state == RELAY_ON ? "ON" : "OFF"), duration_s);
                    } else {
                        ESP_LOGE(TAG, "Failed to start manual override for Relay %u (duration %u s outside [%u..%u])",
                                 relay_id, duration_s, MIN_OVERRIDE_DURATION_S, MAX_OVERRIDE_DURATION_S);
                    }
                }
            }
        } else {
            ESP_LOGW(TAG, "Invalid override command format. Usage: override <id> <on|off> <seconds> OR override <id> cancel");
        }
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
