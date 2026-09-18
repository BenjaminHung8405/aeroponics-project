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

// Log tag for gateway application orchestrator
static const char *TAG = "GATEWAY_MAIN";

// Global instances of gateway core software controllers
static NvsStorage g_nvs_storage;
static NvsStorage g_rf_nvs_storage(nullptr, RF_NVS_NAMESPACE);
static WifiStorageManager g_wifi_storage;
static HardwareButton g_hardware_button(PORTAL_BUTTON_PIN, LED_STATUS_PIN);
static WifiControllerTask g_wifi_controller;
static RtcManager g_rtc_manager;
static UartRfTransport* g_rf_transport = nullptr;
static NodeRegistry g_node_registry;
static CommandManager g_command_manager;
static GroupScheduler g_group_scheduler;

static MqttClient mqtt_client;
static MqttConfig mqtt_config;
static bool g_mqtt_initialized = false;

// Serial command and timing state variables
static bool g_pending_factory_confirm = false;
static uint32_t g_last_wifi_check_ms = 0;
static uint32_t g_last_command_fanout_ms = 0;
static uint32_t g_last_stale_eval_ms = 0;
static bool g_wdt_registered = false;
static bool g_boot_successful = false;
static bool g_gateway_operational = false;

// Forward declaration of helper functions
static bool isWifiProvisioned();
static bool configureTaskWdt();
static bool setupMainWdt();
static void initializeNvs();
static bool provisionRfBoundary(RfHardwareConfig& config);
static bool initializeRfTransport(const RfHardwareConfig& config);
static void initializeRtc();
static bool initializeGatewayCore();
static bool initializeRfControlBoundary();
static bool initializeNetworkTelemetry();
static void enterDegradedSafeState(const char* reason);
static void connectWifiWithTimeout();
static bool initializeMqtt();
static bool createMqttTask();
static void serviceRfRx(uint32_t current_time_ms);
static void serviceCommandFanoutTick(uint32_t current_time_ms);
static void serviceStaleEvaluationTick(uint32_t current_time_ms);
static void processSerialCommands();
static void handleCommand(const char *cmd);
static void handleFactoryResetConfirmation(const char *cmd);
static void printSystemStatus();
static void printWifiStatus();
static void runSystemDiagnostics();
static void mqttTask(void *pvParameters);

static bool registerMqttTaskWdt();
static bool resetMqttTaskWdt();
static bool attemptMqttReconnect(MqttTaskState& state, uint32_t now);
static void serviceConnectedMqtt(MqttTaskState& state, uint32_t now);
static void serviceMqttIteration(MqttTaskState& state);

static bool isWifiProvisioned() {
    return g_wifi_storage.isProvisioned();
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
    g_wifi_storage.begin();
}

static bool provisionRfBoundary(RfHardwareConfig& config) {
#if !defined(UNIT_TEST_HOST)
    if (!RF_PROVISIONING_INDEPENDENT_SIGNOFF_PRESENT) {
        ESP_LOGE(TAG, "RF production provisioning lacks independent security sign-off; gateway remains fail-closed");
        return false;
    }
#endif
    uint32_t uart_num = 0, tx_pin = 0, rx_pin = 0;
    if (!g_rf_nvs_storage.begin() || !g_rf_nvs_storage.getU32(RF_NVS_UART_NUM_KEY, uart_num) ||
        !g_rf_nvs_storage.getU32(RF_NVS_UART_TX_PIN_KEY, tx_pin) ||
        !g_rf_nvs_storage.getU32(RF_NVS_UART_RX_PIN_KEY, rx_pin) ||
        !g_rf_nvs_storage.getU32(RF_NVS_UART_BAUD_KEY, config.baud_rate) ||
        uart_num > 2 || tx_pin > 127 || rx_pin > 127) {
        ESP_LOGE(TAG, "RF provisioning absent or invalid; gateway remains fail-closed");
        return false;
    }
    config.uart_num = static_cast<uint8_t>(uart_num);
    config.tx_pin = static_cast<int8_t>(tx_pin);
    config.rx_pin = static_cast<int8_t>(rx_pin);

    uint32_t m0_pin = 0, m1_pin = 0, aux_pin = 0;
    if (g_rf_nvs_storage.getU32(RF_NVS_UART_M0_PIN_KEY, m0_pin) && m0_pin <= 127) {
        config.m0_pin = static_cast<int8_t>(m0_pin);
    }
    if (g_rf_nvs_storage.getU32(RF_NVS_UART_M1_PIN_KEY, m1_pin) && m1_pin <= 127) {
        config.m1_pin = static_cast<int8_t>(m1_pin);
    }
    if (g_rf_nvs_storage.getU32(RF_NVS_UART_AUX_PIN_KEY, aux_pin) && aux_pin <= 127) {
        config.aux_pin = static_cast<int8_t>(aux_pin);
    }

    if (!config.isValid()) return false;
    return g_command_manager.provisionFromNvs(g_rf_nvs_storage);
}

struct RfRxBuffer {
    uint8_t bytes[256] = {};
    size_t length = 0;
    uint32_t last_byte_ms = 0;
};

static void expirePartialRfFrame(RfRxBuffer& buffer, uint32_t now) {
    if (buffer.length > 0 && now - buffer.last_byte_ms > RF_INTER_BYTE_TIMEOUT_MS) buffer.length = 0;
}

static void readRfBytes(RfRxBuffer& buffer, uint32_t now) {
    if (!g_rf_transport || buffer.length == sizeof(buffer.bytes) || g_rf_transport->available() == 0) return;
    const size_t read = g_rf_transport->receive(buffer.bytes + buffer.length, sizeof(buffer.bytes) - buffer.length);
    buffer.length += read;
    if (read > 0) buffer.last_byte_ms = now;
}

static bool discardUntilSof(RfRxBuffer& buffer) {
    for (size_t i = 0; i + 1 < buffer.length; ++i) {
        if (buffer.bytes[i] == RF_SOF_BYTE_1 && buffer.bytes[i + 1] == RF_SOF_BYTE_2) {
            if (i > 0) std::memmove(buffer.bytes, buffer.bytes + i, buffer.length - i);
            buffer.length -= i;
            return true;
        }
    }
    buffer.length = 0;
    return false;
}

static void processAvailableRfFrames(RfRxBuffer& buffer, uint32_t now) {
    for (size_t processed = 0; processed < 8; ++processed) {
        if (buffer.length < RF_HEADER_SIZE + HMAC_TAG_SIZE + 2 || !discardUntilSof(buffer) || buffer.length < RF_HEADER_SIZE) return;
        const uint8_t payload_len = buffer.bytes[RF_HEADER_PAYLOAD_LENGTH_OFFSET];
        if (payload_len > 64) { std::memmove(buffer.bytes, buffer.bytes + 2, buffer.length - 2); buffer.length -= 2; continue; }
        const size_t frame_len = RF_HEADER_SIZE + payload_len + HMAC_TAG_SIZE + 2;
        if (buffer.length < frame_len) return;
        g_command_manager.handleIncomingFrame(buffer.bytes, frame_len, now);
        std::memmove(buffer.bytes, buffer.bytes + frame_len, buffer.length - frame_len);
        buffer.length -= frame_len;
    }
}

static bool initializeRfTransport(const RfHardwareConfig& config) {
    static UartRfTransport uart(config.uart_num, config.rx_pin, config.tx_pin, config.baud_rate,
                                UART_RF_DEFAULT_RX_BUFFER_CAPACITY, config.m0_pin, config.m1_pin, config.aux_pin);
    if (!uart.begin()) return false;
    g_rf_transport = &uart;
    return g_command_manager.begin(&g_node_registry, g_rf_transport);
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
        ESP_LOGW(TAG, "Wi-Fi not provisioned in NVS. Farmer Portal is auto-starting on AP '%s' (URL: http://192.168.4.1)...", PORTAL_AP_SSID);
        return;
    }

    ESP_LOGI(TAG, "Connecting to Wi-Fi via Core 0 network engine (max wait: 6000 ms)...");

    uint32_t wifi_start_ms = millis();
    while (!g_wifi_controller.isConnected() && (millis() - wifi_start_ms < 6000)) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    bool wifi_connected = g_wifi_controller.isConnected();
    uint32_t elapsed_ms = millis() - wifi_start_ms;
    if (wifi_connected) {
        ESP_LOGI(TAG, "Wi-Fi connected successfully in %u ms via Core 0 engine!", (unsigned)elapsed_ms);
        ESP_LOGI(TAG, "Synchronizing system time with NTP server...");
        bool ntp_ok = g_rtc_manager.syncFromNtp();
        if (ntp_ok) {
            ESP_LOGI(TAG, "NTP time synchronization completed and RTC updated.");
        } else {
            ESP_LOGW(TAG, "NTP time synchronization failed or timed out. Relying on RTC internal clock.");
        }
    } else {
        ESP_LOGW(TAG, "Wi-Fi connection pending in background after %u ms. Operating in offline autonomous mode.", (unsigned)elapsed_ms);
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
    mqtt_client.serviceOutgoingEvents();
    mqttServiceHeartbeat(state, now, []() { return mqtt_client.publishConnectedHeartbeat(); });
    mqtt_client.serviceOutgoingEvents();
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
    return mqtt_client.begin(mqtt_config, &g_rtc_manager, &g_node_registry, &g_command_manager,
                             &g_group_scheduler);
}

static bool createMqttTask() {
    const BaseType_t result = xTaskCreatePinnedToCore(
        mqttTask, MQTT_TASK_NAME, MQTT_TASK_STACK_SIZE, NULL,
        MQTT_TASK_PRIORITY, NULL, MQTT_TASK_CORE);
    if (result == pdPASS) return true;
    ESP_LOGE(TAG, "Failed to create MQTT FreeRTOS task (err: %d)!", static_cast<int>(result));
    return false;
}

static void serviceRfRx(uint32_t current_time_ms) {
    static RfRxBuffer buffer;
    if (!g_gateway_operational || g_rf_transport == nullptr) return;
    expirePartialRfFrame(buffer, current_time_ms);
    readRfBytes(buffer, current_time_ms);
    processAvailableRfFrames(buffer, current_time_ms);
}

static void serviceCommandFanoutTick(uint32_t current_time_ms) {
    if (!g_gateway_operational) return;
    // MQTT task only parses into its bounded queue. Main loop is the sole
    // owner of CommandManager mutation, correlation state and RF fan-out.
    mqtt_client.serviceIncomingCommands();
    if (current_time_ms - g_last_command_fanout_ms >= 100) {
        g_last_command_fanout_ms = current_time_ms;
        g_command_manager.serviceCommandFanout(current_time_ms);
    }
}

static void serviceStaleEvaluationTick(uint32_t current_time_ms) {
    if (!g_gateway_operational) return;
    if (current_time_ms - g_last_stale_eval_ms >= 5000) {
        g_last_stale_eval_ms = current_time_ms;
        uint16_t newly_stale = g_node_registry.evaluateStaleNodes(current_time_ms, 15000);
        for (uint8_t i = 0; i < RF_PRODUCTION_MAX_NODE_ID; ++i) {
            if (newly_stale & (1 << i)) {
                uint8_t node_id = i + 1;
                g_command_manager.cancelNodeCommands(node_id);
                char reason_buf[128];
                snprintf(reason_buf, sizeof(reason_buf), "Node %u went STALE; forced OFF, latched fault and canceled pending commands", node_id);
                mqtt_client.publishSafetyAudit("STALE_SAFE_OFF", reason_buf);
                ESP_LOGW(TAG, "STALE_SAFE_OFF for Node %u", node_id);
            }
        }
    }
}

static bool initializeGatewayCore() {
    g_command_manager.setOutcomeSink(&mqtt_client);
    initializeNvs();
    if (!g_node_registry.begin()) {
        ESP_LOGE(TAG, "Failed to initialize NodeRegistry");
        return false;
    }
    if (!g_group_scheduler.begin(&g_rtc_manager, &g_node_registry, nullptr, &mqtt_client, &g_command_manager)) {
        ESP_LOGE(TAG, "Failed to initialize GroupScheduler");
        return false;
    }
    ESP_LOGI(TAG, "NodeRegistry & GroupScheduler initialized successfully (production scope: 4 nodes, IDs 1..4).");
    return true;
}

static bool initializeRfControlBoundary() {
    RfHardwareConfig rf_config;
    if (!provisionRfBoundary(rf_config)) return false;
    if (!initializeRfTransport(rf_config) || !g_command_manager.isProvisioned()) return false;
    ESP_LOGI(TAG, "RF provisioning, transport, and command manager initialized.");
    return true;
}

static void enterDegradedSafeState(const char* reason) {
    g_boot_successful = false;
    g_gateway_operational = false;
    ESP_LOGE(TAG, "Gateway boot DEGRADED: %s; desired state OFF; RF/MQTT control disabled.", reason);
}

static bool initializeNetworkTelemetry() {
    connectWifiWithTimeout();
    if (!setupMainWdt()) return false;
    const bool mqtt_started = initializeMqtt();
    const bool mqtt_task_created = mqtt_started && createMqttTask();
    if (mqtt_started && !finalizeMqttTaskStartup(mqtt_client, mqtt_task_created)) {
        ESP_LOGW(TAG, "MQTT facade rolled back after task creation failure; initialized=%s connected=%s",
                 mqtt_client.isInitialized() ? "true" : "false",
                 mqtt_client.isConnected() ? "true" : "false");
    }
    g_mqtt_initialized = mqtt_task_created && mqtt_client.isInitialized();
    return true;
}

void setup() {
    Serial.begin(SERIAL_BAUD_RATE);
    ESP_LOGI(TAG, "Initializing Aeroponics Gateway Composition Root...");

    // Initialize NVS storage and start Core 0 Network/Button Engine immediately
    initializeNvs();
    g_hardware_button.begin();
    g_wifi_controller.begin(&g_wifi_storage, &g_hardware_button);
    g_wifi_controller.startCore0Task();

    // Initialize Core Domain & RF Control Boundaries
    const bool core_ok = initializeGatewayCore();
    const bool rf_ok = initializeRfControlBoundary();
    if (!core_ok || !rf_ok) {
        enterDegradedSafeState("mandatory control boundary initialization failed");
    } else {
        g_boot_successful = true;
        g_gateway_operational = true;
    }

    // Initialize Network Telemetry & Watchdog (Runs even in degraded mode so Farmer Portal & Wi-Fi operate)
    if (!initializeNetworkTelemetry()) {
        enterDegradedSafeState("network telemetry watchdog initialization failed");
    }

    ESP_LOGI(TAG, "Gateway Boot Complete (status: %s). Gateway Composition Root fully wired.",
             g_boot_successful ? "SUCCESS" : "DEGRADED");
}

void loop() {
    uint32_t current_ms = millis();

    if (g_wdt_registered) {
        esp_err_t err = esp_task_wdt_reset();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Main loop esp_task_wdt_reset failed");
            esp_restart();
        }
    }

    // Service RF RX loop: read bytes, slice frames, decode, update node telemetry/ACKs
    serviceRfRx(current_ms);

    // Service Command fan-out & retry loop: dispatch pending commands via RF
    serviceCommandFanoutTick(current_ms);

    // Service Stale evaluation: evaluate node telemetry freshness and flag offline/stale nodes
    serviceStaleEvaluationTick(current_ms);

    // Wi-Fi connection and roaming is managed asynchronously on Core 0 by WifiControllerTask.
    // Sync NTP when Wi-Fi becomes connected
    if (g_wifi_controller.isConnected()) {
        static bool s_ntp_synced = false;
        if (!s_ntp_synced) {
            s_ntp_synced = true;
            g_rtc_manager.syncFromNtp();
        }
    }

    // Parse and handle Gateway Serial debug commands
    processSerialCommands();

    // Yield CPU 1 to IDLE task to satisfy Task Watchdog requirements
    vTaskDelay(pdMS_TO_TICKS(1));
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
    ESP_LOGI(TAG, "RF Transport Initialized: %s", (g_rf_transport && g_rf_transport->isInitialized() ? "YES" : "NO"));
    ESP_LOGI(TAG, "MQTT Initialized: %s | Connected: %s",
             (g_mqtt_initialized ? "YES" : "NO"),
             (mqtt_client.isConnected() ? "YES" : "NO"));
    ESP_LOGI(TAG, "Composition Root Wired: NodeRegistry, CommandManager, RF transport, and MQTT override paths ACTIVE.");
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
                 (g_rf_transport && g_rf_transport->isInitialized() ? "YES" : "NO"),
                 g_rf_transport ? g_rf_transport->available() : 0U);
    } else if (strcasecmp(cmd, "wifi") == 0) {
        printWifiStatus();
    } else if (strcasecmp(cmd, "wifireset") == 0) {
        g_wifi_storage.clearAllProfiles();
        ESP_LOGI(TAG, "All Wi-Fi profiles cleared from NVS namespace 'wifi_store'.");
    } else if (strcasecmp(cmd, "portal") == 0) {
        g_wifi_controller.triggerPortalMode();
        ESP_LOGI(TAG, "Farmer Captive Portal mode triggered manually via Serial.");
    } else if (strcasecmp(cmd, "factory") == 0) {
        g_pending_factory_confirm = true;
        ESP_LOGW(TAG, "CRITICAL: Gateway Factory reset requested! Type 'YES' to confirm NVS flash erasure.");
    } else {
        ESP_LOGW(TAG, "Unknown Serial command: '%s'. Valid commands: 'status', 'test', 'rfstatus', 'wifi', 'wifireset', 'portal', 'factory'", cmd);
    }
}

static void printWifiStatus() {
    ESP_LOGI(TAG, "=== WI-FI & MULTI-AP STATUS ===");
    ESP_LOGI(TAG, "Status: %s | SSID: %s | RSSI: %d dBm | Portal: %s",
             g_wifi_controller.isConnected() ? "CONNECTED" : "DISCONNECTED",
             g_wifi_controller.getCurrentSsid(),
             static_cast<int>(g_wifi_controller.getCurrentRssi()),
             g_wifi_controller.isPortalActive() ? "ACTIVE" : "INACTIVE");
    const WifiConfigBlob& blob = g_wifi_storage.cachedBlob();
    ESP_LOGI(TAG, "Saved Wi-Fi Profiles in NVS (%u/%zu):", blob.count, MAX_SAVED_WIFI);
    for (uint8_t i = 0; i < blob.count; ++i) {
        ESP_LOGI(TAG, "  [%u] SSID: '%s' | Priority: %d",
                 i + 1, blob.profiles[i].ssid, blob.profiles[i].priority);
    }
    ESP_LOGI(TAG, "===============================");
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
             (g_rf_transport && g_rf_transport->isInitialized() ? "YES" : "NO"),
             g_rf_transport ? g_rf_transport->available() : 0U);
    ESP_LOGI(TAG, "MQTT Gateway: %s | Connected: %s",
             (g_mqtt_initialized ? "YES" : "NO"),
             (mqtt_client.isConnected() ? "YES" : "NO"));
    ESP_LOGI(TAG, "=================================");
}

#endif // ESP_PLATFORM || ARDUINO
