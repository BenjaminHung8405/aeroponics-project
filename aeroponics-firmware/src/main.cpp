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
#include "agu_legacy_codec.h"

// Log tag for gateway application orchestrator
static const char *TAG = "GATEWAY_MAIN";

// Global instances of gateway core software controllers
static NvsStorage g_nvs_storage;
static NvsStorage g_rf_nvs_storage(nullptr, RF_NVS_NAMESPACE);
static WifiStorageManager g_wifi_storage;
static HardwareButton g_hardware_button(PORTAL_BUTTON_PIN, LED_STATUS_PIN);
static WifiControllerTask g_wifi_controller;
static RtcManager g_rtc_manager;
static UartRfTransport *g_rf_transport = nullptr;
static NodeRegistry g_node_registry;
static CommandManager g_command_manager;
static GroupScheduler g_group_scheduler;

static MqttClient mqtt_client;
static MqttConfig mqtt_config;
static bool g_mqtt_initialized = false;

// Serial command and timing state variables
static bool g_pending_factory_confirm = false;
static bool g_rf_raw_dump = false;
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
static bool provisionRfBoundary(RfHardwareConfig &config);
static bool initializeRfTransport(const RfHardwareConfig &config);
static void initializeRtc();
static bool initializeGatewayCore();
static bool initializeRfControlBoundary();
static bool initializeNetworkTelemetry();
static void enterDegradedSafeState(const char *reason);
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
    static UartRfTransport uart(config.uart_num, config.rx_pin, config.tx_pin, config.baud_rate,
                                UART_RF_DEFAULT_RX_BUFFER_CAPACITY, config.m0_pin, config.m1_pin, config.aux_pin);
    if (!uart.begin())
        return false;
    g_rf_transport = &uart;
    return g_command_manager.begin(&g_node_registry, g_rf_transport);
}

static void initializeRtc()
{
    Wire.begin(RTC_SDA_PIN, RTC_SCL_PIN);
    bool rtc_ok = g_rtc_manager.begin();
    if (!rtc_ok)
    {
        ESP_LOGW(TAG, "RTC init failed or hardware not detected; using system time fallback.");
    }
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
    return mqtt_client.begin(mqtt_config, &g_rtc_manager, &g_node_registry, &g_command_manager,
                             &g_group_scheduler);
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

    // In raw hex dump mode, print received RF bytes immediately to Serial
    if (g_rf_raw_dump && g_rf_transport->available() > 0)
    {
        uint8_t raw[32];
        size_t r = g_rf_transport->receive(raw, sizeof(raw));
        if (r > 0)
        {
            char hex_buf[128] = {};
            size_t pos = 0;
            for (size_t i = 0; i < r && pos + 4 < sizeof(hex_buf); ++i)
            {
                pos += snprintf(hex_buf + pos, sizeof(hex_buf) - pos, "%02X ", raw[i]);
            }
            ESP_LOGI(TAG, "[RF_RAW RX %zu bytes]: %s", r, hex_buf);
        }
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
    if (current_time_ms - g_last_command_fanout_ms >= 100)
    {
        g_last_command_fanout_ms = current_time_ms;
        g_command_manager.serviceCommandFanout(current_time_ms);
    }
}

static void serviceStaleEvaluationTick(uint32_t current_time_ms)
{
    if (!g_gateway_operational)
        return;
    if (current_time_ms - g_last_stale_eval_ms >= 5000)
    {
        g_last_stale_eval_ms = current_time_ms;
        uint16_t newly_stale = g_node_registry.evaluateStaleNodes(current_time_ms, 15000);
        for (uint8_t i = 0; i < RF_PRODUCTION_MAX_NODE_ID; ++i)
        {
            if (newly_stale & (1 << i))
            {
                uint8_t node_id = i + 1;
                g_command_manager.cancelNodeCommands(node_id);
                char reason_buf[128];
                snprintf(reason_buf, sizeof(reason_buf), "Node %u went STALE; forced OFF, latched fault and canceled pending commands", node_id);
                mqtt_client.publishSafetyAudit("STALE_SAFE_OFF", reason_buf);
                ESP_LOGW(TAG, "Node %u stale-safe-off executed.", node_id);
            }
        }
    }
}

static bool initializeGatewayCore()
{
    g_command_manager.setOutcomeSink(&mqtt_client);
    initializeNvs();
    if (!g_node_registry.begin())
    {
        ESP_LOGE(TAG, "Failed to initialize NodeRegistry");
        return false;
    }
    if (!g_group_scheduler.begin(&g_rtc_manager, &g_node_registry, nullptr, &mqtt_client, &g_command_manager))
    {
        ESP_LOGE(TAG, "Failed to initialize GroupScheduler");
        return false;
    }
    ESP_LOGI(TAG, "NodeRegistry and GroupScheduler initialized (nodes 1..4).");
    return true;
}

static bool initializeRfControlBoundary()
{
    RfHardwareConfig rf_config;
    // Always initialize physical RF transport first so CLI diagnostics and AT setup can function
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
    initializeRfTransport(rf_config);

    if (!provisionRfBoundary(rf_config))
        return false;
    if (!g_command_manager.isProvisioned())
        return false;
    ESP_LOGI(TAG, "RF provisioning, transport, and command manager initialized.");
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
    connectWifiWithTimeout();
    if (!setupMainWdt())
        return false;
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
    ESP_LOGI(TAG, "Initializing Aeroponics gateway composition root...");

    // Initialize NVS storage and prepare Core 0 Network/Button Engine
    initializeNvs();
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

    // Wi-Fi connection and roaming is managed asynchronously on Core 0 by WifiControllerTask.
    // Sync NTP when Wi-Fi becomes connected
    if (g_wifi_controller.isConnected())
    {
        static bool s_ntp_synced = false;
        if (!s_ntp_synced)
        {
            s_ntp_synced = true;
            g_rtc_manager.syncFromNtp();
        }
    }

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
        {"AT+C001\r\n", "Channel 001 (433MHz)"}
    };
    for (size_t i = 0; i < 5; ++i)
    {
        const char *cmd_str = commands[i][0];
        const char *desc = commands[i][1];
        ESP_LOGI(TAG, "[TX] Sending: %s (%s)", cmd_str, desc);
        g_rf_transport->send(reinterpret_cast<const uint8_t*>(cmd_str), strlen(cmd_str));
        vTaskDelay(pdMS_TO_TICKS(500));
        uint8_t resp[64] = {};
        size_t r = g_rf_transport->receive(resp, sizeof(resp) - 1);
        if (r > 0)
        {
            resp[r] = '\0';
            ESP_LOGI(TAG, "[RX] Response: %s", reinterpret_cast<char*>(resp));
        }
        else
        {
            ESP_LOGW(TAG, "[RX] No response (timeout or already in transparent mode)");
        }
    }
    ESP_LOGI(TAG, "=== RF Setup Sequence Completed ===");
}

static void executeAguPing(uint8_t node_id)
{
    if (!g_rf_transport)
    {
        ESP_LOGE(TAG, "RF transport not initialized");
        return;
    }
    uint8_t tx_buf[8];
    const uint8_t ping_val = AguLegacy::PING_DEFAULT_VAL;
    size_t len = AguLegacy::AguLegacyCodec::encodePing(ping_val, node_id, tx_buf, sizeof(tx_buf));
    ESP_LOGI(TAG, "[AGU TX] PING Node %u with val=0x%02X (%zu bytes: %02X %02X %02X)",
             node_id, ping_val, len, tx_buf[0], tx_buf[1], tx_buf[2]);
    g_rf_transport->flushRx();
    uint32_t start_ms = millis();
    g_rf_transport->send(tx_buf, len);

    uint8_t resp = 0;
    bool received = false;
    while (millis() - start_ms < 600)
    {
        if (g_rf_transport->available() > 0 && g_rf_transport->receive(&resp, 1) == 1)
        {
            received = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    uint32_t rtt_ms = millis() - start_ms;
    if (received)
    {
        if (resp == ping_val)
        {
            ESP_LOGI(TAG, "[AGU RX] PONG SUCCESS from Node %u! Echo: 0x%02X (RTT: %u ms)",
                     node_id, resp, (unsigned)rtt_ms);
        }
        else
        {
            ESP_LOGW(TAG, "[AGU RX] PONG MISMATCH! Received: 0x%02X, Expected: 0x%02X (RTT: %u ms)",
                     resp, ping_val, (unsigned)rtt_ms);
        }
    }
    else
    {
        ESP_LOGE(TAG, "[AGU RX] PING TIMEOUT! Node %u did not respond within %u ms", node_id, (unsigned)rtt_ms);
    }
}

static void executeAguPump(uint8_t node_id, bool turn_on)
{
    if (!g_rf_transport)
    {
        ESP_LOGE(TAG, "RF transport not initialized");
        return;
    }
    uint8_t tx_buf[8];
    size_t len = turn_on ? AguLegacy::AguLegacyCodec::encodePumpOn(node_id, tx_buf, sizeof(tx_buf))
                         : AguLegacy::AguLegacyCodec::encodePumpOff(node_id, tx_buf, sizeof(tx_buf));
    ESP_LOGI(TAG, "[AGU TX] PUMP %s -> Node %u (%zu bytes: %02X %02X)",
             turn_on ? "ON" : "OFF", node_id, len, tx_buf[0], tx_buf[1]);
    g_rf_transport->flushRx();
    uint32_t start_ms = millis();
    g_rf_transport->send(tx_buf, len);

    uint8_t resp = 0;
    bool received = false;
    while (millis() - start_ms < 600)
    {
        if (g_rf_transport->available() > 0 && g_rf_transport->receive(&resp, 1) == 1)
        {
            received = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    uint32_t rtt_ms = millis() - start_ms;
    if (received)
    {
        if (AguLegacy::AguLegacyCodec::isAck(resp))
        {
            ESP_LOGI(TAG, "[AGU RX] PUMP %s ACK SUCCESS from Node %u! (Byte: 0x%02X, RTT: %u ms)",
                     turn_on ? "ON" : "OFF", resp, node_id, (unsigned)rtt_ms);
        }
        else
        {
            ESP_LOGW(TAG, "[AGU RX] PUMP %s: unexpected response 0x%02X from Node %u (RTT: %u ms)",
                     turn_on ? "ON" : "OFF", resp, node_id, (unsigned)rtt_ms);
        }
    }
    else
    {
        ESP_LOGE(TAG, "[AGU RX] PUMP %s TIMEOUT: Node %u did not ACK within %u ms",
                 turn_on ? "ON" : "OFF", node_id, (unsigned)rtt_ms);
    }
}

static void executeAguGetId()
{
    if (!g_rf_transport)
    {
        ESP_LOGE(TAG, "RF transport not initialized");
        return;
    }
    uint8_t tx_buf[8];
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
            if (count >= 3) break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    uint8_t id = 0;
    if (AguLegacy::AguLegacyCodec::decodeFramedId(resp, count, id))
    {
        ESP_LOGI(TAG, "[AGU RX] Node ID Frame Valid! Detected Node ID = %u (Raw: %02X %02X %02X)",
                 id, resp[0], resp[1], resp[2]);
    }
    else
    {
        ESP_LOGW(TAG, "[AGU RX] Failed to decode Node ID frame (received %zu bytes)", count);
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
        ESP_LOGI(TAG, "RF transport: initialized=%s available_bytes=%zu baud=%u",
                 (g_rf_transport && g_rf_transport->isInitialized() ? "YES" : "NO"),
                 g_rf_transport ? g_rf_transport->available() : 0U,
                 g_rf_transport ? (unsigned)g_rf_transport->getBaudRate() : 0U);
    }
    else if (strcasecmp(cmd, "rfsetup") == 0)
    {
        executeRfSetup();
    }
    else if (strncasecmp(cmd, "ping", 4) == 0)
    {
        int node = 1;
        if (strlen(cmd) > 4) node = atoi(cmd + 4);
        if (node < 1) node = 1;
        executeAguPing(static_cast<uint8_t>(node));
    }
    else if (strncasecmp(cmd, "on", 2) == 0 && (cmd[2] == ' ' || cmd[2] == '\0'))
    {
        int node = 1;
        if (strlen(cmd) > 2) node = atoi(cmd + 2);
        if (node < 1) node = 1;
        executeAguPump(static_cast<uint8_t>(node), true);
    }
    else if (strncasecmp(cmd, "off", 3) == 0 && (cmd[3] == ' ' || cmd[3] == '\0'))
    {
        int node = 1;
        if (strlen(cmd) > 3) node = atoi(cmd + 3);
        if (node < 1) node = 1;
        executeAguPump(static_cast<uint8_t>(node), false);
    }
    else if (strcasecmp(cmd, "getid") == 0)
    {
        executeAguGetId();
    }
    else if (strcasecmp(cmd, "rfraw") == 0)
    {
        g_rf_raw_dump = !g_rf_raw_dump;
        ESP_LOGI(TAG, "RF raw RX hex dump is now %s", g_rf_raw_dump ? "ENABLED" : "DISABLED");
    }
    else if (strncasecmp(cmd, "rfbaud", 6) == 0)
    {
        uint32_t baud = 38400;
        if (strlen(cmd) > 6) baud = strtoul(cmd + 6, nullptr, 10);
        if (baud >= 1200 && baud <= 115200 && g_rf_transport)
        {
            g_rf_transport->setBaudRate(baud);
            ESP_LOGI(TAG, "RF transport baud rate set to %u", (unsigned)baud);
        }
        else
        {
            ESP_LOGW(TAG, "Invalid baud rate: %s", cmd + 6);
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
    else
    {
        ESP_LOGW(TAG, "Unknown Serial command: '%s'. Valid: 'status', 'test', 'rfstatus', 'rfsetup', 'ping <node>', 'on <node>', 'off <node>', 'getid', 'rfraw', 'rfbaud <baud>', 'wifi', 'wifireset', 'portal', 'factory'", cmd);
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
