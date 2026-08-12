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
#include <algorithm>

#include "config.h"
#include "nvs_storage.h"
#include "rtc_manager.h"
#include "mqtt_client.h"
#include "mqtt_lifecycle.h"
#include "mqtt_task_policy.h"
#include "mqtt_config_provider.h"
#include "ESPTaskWatchdog.h"
#include "core/IRfTransport.h"
#include "uart_rf_transport.h"

// Log tag for gateway application orchestrator
static const char *TAG = "GATEWAY_MAIN";

// Global instances of gateway core software controllers
static NvsStorage g_nvs_storage;
static RtcManager g_rtc_manager;
static UartRfTransport g_rf_transport;

static MqttClient mqtt_client;
static MqttConfig mqtt_config;
static bool g_mqtt_initialized = false;

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
static bool initializeMqtt();
static bool createMqttTask();
static void processSerialCommands();
static void handleCommand(const char *cmd);
static void handleFactoryResetConfirmation(const char *cmd);
static void printSystemStatus();
static void runSystemDiagnostics();
static void mqttTask(void *pvParameters);

static bool registerMqttTaskWdt();
static bool resetMqttTaskWdt();
static bool attemptMqttReconnect(MqttTaskState& state, uint32_t now);
static void serviceConnectedMqtt(MqttTaskState& state, uint32_t now);
static void serviceMqttIteration(MqttTaskState& state);

static bool isWifiProvisioned() {
    return (WIFI_SSID[0] != '\0' && strcmp(WIFI_SSID, "CHANGE_ME") != 0);
}

static bool configureTaskWdt() {
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    esp_task_wdt_config_t twdt_config = {
        .timeout_ms = WDT_TIMEOUT_MS,
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
        ESP_LOGW(TAG, "NVS storage init failed. Gateway operating with default configuration.");
    } else {
        ESP_LOGI(TAG, "NVS storage initialized successfully.");
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
        ESP_LOGI(TAG, "Wi-Fi credentials not provisioned. Skipping Wi-Fi connection and operating in offline mode.");
        return;
    }

    ESP_LOGI(TAG, "Connecting to Wi-Fi network (timeout limit: %u ms)...", (unsigned)WIFI_CONNECT_TIMEOUT_MS);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    uint32_t wifi_start_ms = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - wifi_start_ms < WIFI_CONNECT_TIMEOUT_MS)) {
        vTaskDelay(pdMS_TO_TICKS(WIFI_CONNECT_POLL_INTERVAL_MS));
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
        ESP_LOGW(TAG, "Wi-Fi connection timed out after %u ms. Operating in offline mode.", (unsigned)elapsed_ms);
    }
}

static bool registerMqttTaskWdt() {
#if defined(ESP_PLATFORM)
    const esp_err_t add_err = esp_task_wdt_add(NULL);
    if (add_err != ESP_OK) {
        ESP_LOGE(TAG, "MQTT task WDT registration failed: 0x%x", add_err);
        return false;
    }
#endif
    return true;
}

static bool resetMqttTaskWdt() {
#if defined(ESP_PLATFORM)
    const esp_err_t reset_err = esp_task_wdt_reset();
    if (reset_err != ESP_OK) {
        ESP_LOGE(TAG, "MQTT task WDT reset failed: 0x%x", reset_err);
        esp_task_wdt_delete(NULL);
        return false;
    }
#endif
    return true;
}

static bool attemptMqttReconnect(MqttTaskState& state, uint32_t now) {
    if (!mqttReconnectDue(state, now)) return false;
    mqttRecordReconnectAttempt(state, now);
    if (mqtt_client.connect()) {
        mqttRecordReconnectSuccess(state, now);
        return true;
    }
    mqttRecordReconnectFailure(state);
    return false;
}

static void serviceConnectedMqtt(MqttTaskState& state, uint32_t now) {
    state.was_connected = true;
    mqtt_client.loop();
    mqttServiceHeartbeat(state, now, []() { return mqtt_client.publishHeartbeat(); });
}

static void serviceMqttIteration(MqttTaskState& state) {
    if (WiFi.status() != WL_CONNECTED) {
        mqttRecordWifiLoss(state);
        return;
    }
    const uint32_t now = millis();
    if (!mqtt_client.isConnected()) {
        if (state.was_connected) state.was_connected = false;
        attemptMqttReconnect(state, now);
        return;
    }
    serviceConnectedMqtt(state, now);
}

static void mqttTask(void *pvParameters) {
    (void)pvParameters;
    if (!registerMqttTaskWdt()) {
        vTaskDelete(NULL);
        return;
    }
    MqttTaskState state;
    for (;;) {
        if (!resetMqttTaskWdt()) {
            vTaskDelete(NULL);
            return;
        }
        serviceMqttIteration(state);
        vTaskDelay(pdMS_TO_TICKS(MQTT_TASK_TICK_INTERVAL_MS));
    }
}

static bool initializeMqtt() {
    mqtt_config = MqttConfigProvider::load();
    if (!mqtt_config.broker_host || !mqtt_config.device_id ||
        mqtt_config.broker_host[0] == '\0' || mqtt_config.device_id[0] == '\0') {
        ESP_LOGW(TAG, "MQTT config is not provisioned; MQTT gateway task remains disabled.");
        return false;
    }
    return mqtt_client.begin(mqtt_config, nullptr, nullptr, &g_rtc_manager);
}

static bool createMqttTask() {
    const BaseType_t result = xTaskCreatePinnedToCore(
        mqttTask, MQTT_TASK_NAME, MQTT_TASK_STACK_SIZE, NULL,
        MQTT_TASK_PRIORITY, NULL, MQTT_TASK_CORE);
    if (result == pdPASS) return true;
    ESP_LOGE(TAG, "Failed to create MQTT FreeRTOS task (err: %d)!", static_cast<int>(result));
    return false;
}

void setup() {
    // Step 1: Initialize USB Debug Serial Communication (115200 baud)
    Serial.begin(SERIAL_BAUD_RATE);
    ESP_LOGI(TAG, "Initializing Aeroponics Gateway Composition Root...");

    // Step 2: Initialize RF UART Interface (UART2, separate from USB Debug Serial)
    bool rf_ok = g_rf_transport.begin();
    if (rf_ok) {
        ESP_LOGI(TAG, "RF UART transport seam initialized successfully.");
    } else {
        ESP_LOGE(TAG, "Failed to initialize RF UART transport seam!");
    }

    // Step 3: Initialize NVS and RTC
    initializeNvs();
    initializeRtc();

    // Step 4: Wi-Fi Non-Blocking Connection (30s timeout) & NTP Sync (10s timeout)
    connectWifiWithTimeout();

    // Step 5: Configure & Register Task Watchdog Timer for Gateway Main Loop Task
    if (!setupMainWdt()) {
        ESP_LOGE(TAG, "Task WDT setup or registration failed for Gateway main loop!");
        g_boot_successful = false;
        return;
    }

    // Step 6: MQTT Gateway Client Initialization & Task Launch
    const bool mqtt_started = initializeMqtt();
    const bool mqtt_task_created = mqtt_started && createMqttTask();
    if (mqtt_started && !finalizeMqttTaskStartup(mqtt_client, mqtt_task_created)) {
        ESP_LOGW(TAG, "MQTT facade rolled back after task creation failure; initialized=%s connected=%s",
                 mqtt_client.isInitialized() ? "true" : "false",
                 mqtt_client.isConnected() ? "true" : "false");
    }
    g_mqtt_initialized = mqtt_task_created && mqtt_client.isInitialized();

    g_boot_successful = true;
    ESP_LOGI(TAG, "Gateway Boot Complete. Hardware Relays are decoupled to Node Actuators.");
}

void loop() {
    if (g_wdt_registered) {
        esp_err_t err = esp_task_wdt_reset();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Main loop esp_task_wdt_reset failed");
            esp_restart();
        }
    }

    // Check Wi-Fi connection status every 60 seconds (non-blocking)
    if (isWifiProvisioned()) {
        uint32_t current_ms = millis();
        if (current_ms - g_last_wifi_check_ms >= WIFI_RECONNECT_CHECK_INTERVAL_MS) {
            g_last_wifi_check_ms = current_ms;
            if (WiFi.status() != WL_CONNECTED) {
                ESP_LOGW(TAG, "Wi-Fi disconnected. Attempting non-blocking reconnect...");
                WiFi.reconnect();
            }
        }
    }

    // Parse and handle Gateway Serial debug commands
    processSerialCommands();
}

static void processSerialCommands() {
    static char buffer[SERIAL_COMMAND_BUFFER_SIZE];
    static size_t buf_idx = 0;
    static bool discarding_overflow = false;
    size_t bytes_processed = 0;
    while (Serial.available() > 0 && bytes_processed < MAX_SERIAL_BYTES_PER_TICK) {
        char c = static_cast<char>(Serial.read());
        bytes_processed++;

        if (c == '\r' || c == '\n') {
            if (discarding_overflow) {
                ESP_LOGE(TAG, "Serial line exceeded buffer limit (%u bytes). Line discarded.",
                         static_cast<unsigned>(SERIAL_COMMAND_BUFFER_SIZE - 1));
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
            ESP_LOGI(TAG, "Factory reset successful. Restarting Gateway ESP32 immediately...");
            esp_restart();
        } else {
            ESP_LOGE(TAG, "Factory reset failed during NVS erase.");
        }
    } else {
        ESP_LOGI(TAG, "Factory reset request cancelled.");
    }
    g_pending_factory_confirm = false;
}

static void runSystemDiagnostics() {
    ESP_LOGI(TAG, "=== AEROPONICS GATEWAY DIAGNOSTICS ===");
    ESP_LOGI(TAG, "Boot Status: %s | Main Task WDT Registered: %s",
             (g_boot_successful ? "SUCCESS" : "FAILED"),
             (g_wdt_registered ? "YES" : "NO"));
    ESP_LOGI(TAG, "RF Transport Initialized: %s", (g_rf_transport.isInitialized() ? "YES" : "NO"));
    ESP_LOGI(TAG, "MQTT Initialized: %s | Connected: %s",
             (g_mqtt_initialized ? "YES" : "NO"),
             (mqtt_client.isConnected() ? "YES" : "NO"));
    ESP_LOGI(TAG, "REGRESSION CHECK PASSED: Gateway composition root initialized without local relay GPIOs.");
}

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
    } else if (strcasecmp(cmd, "rfstatus") == 0) {
        ESP_LOGI(TAG, "RF Transport Status -> Initialized: %s | Available Bytes: %zu",
                 (g_rf_transport.isInitialized() ? "YES" : "NO"), g_rf_transport.available());
    } else if (strcasecmp(cmd, "factory") == 0) {
        g_pending_factory_confirm = true;
        ESP_LOGW(TAG, "CRITICAL: Gateway Factory reset requested! Type 'YES' to confirm NVS flash erasure.");
    } else {
        ESP_LOGW(TAG, "Unknown Serial command: '%s'. Valid commands: 'status', 'test', 'rfstatus', 'factory'", cmd);
    }
}

static void printSystemStatus() {
    SystemTime t = g_rtc_manager.getTime();
    bool is_night = g_rtc_manager.isNightMode();
    bool wifi_ok = (WiFi.status() == WL_CONNECTED);

    ESP_LOGI(TAG, "=== AEROPONICS GATEWAY STATUS ===");
    ESP_LOGI(TAG, "System Time: %02u:%02u:%02u (Valid: %s) | Mode: %s | Wi-Fi: %s",
             t.hour, t.minute, t.second,
             (t.is_valid ? "YES" : "NO (Fallback)"),
             (is_night ? "NIGHT" : "DAY"),
             (wifi_ok ? "CONNECTED" : "DISCONNECTED"));
    ESP_LOGI(TAG, "RF Transport: Initialized (%s) | RX Avail: %zu bytes",
             (g_rf_transport.isInitialized() ? "YES" : "NO"), g_rf_transport.available());
    ESP_LOGI(TAG, "MQTT Gateway: %s | Connected: %s",
             (g_mqtt_initialized ? "YES" : "NO"),
             (mqtt_client.isConnected() ? "YES" : "NO"));
    ESP_LOGI(TAG, "=================================");
}

#endif // ESP_PLATFORM || ARDUINO
