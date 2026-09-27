#if defined(ESP_PLATFORM) || defined(ARDUINO)

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <esp_log.h>
#include <esp_task_wdt.h>
#include <esp_system.h>
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
static AguLegacyRfHost *g_agu_legacy_host = nullptr;
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

struct NodeLivenessRecord {
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
static bool executeAguPump(uint8_t node_id, bool turn_on, const char *command_id = nullptr);
static bool executeAguPing(uint8_t node_id);
static void onGatewayCommand(const MqttInboundCommand &command);
static void handleFactoryResetConfirmation(const char *cmd);
static void printSystemStatus();
static void printWifiStatus();
static void runSystemDiagnostics();
static void runRfUartDiagnostic(bool loopback);
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
    static UartRfTransport uart(config.uart_num, config.rx_pin, config.tx_pin, RF_UART_HC12_BAUD_RATE,
                                UART_RF_DEFAULT_RX_BUFFER_CAPACITY, config.m0_pin, config.m1_pin, config.aux_pin);
    if (!uart.begin())
        return false;
    if (!uart.startRxTask()) {
        ESP_LOGE(TAG, "Failed to start RF UART RX task on Core 1");
        return false;
    }
    g_rf_transport = &uart;
    static AguLegacyRfHost legacy_host(g_rf_transport);
    g_agu_legacy_host = &legacy_host;
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
    }
    return ok;
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
    if (!isAguLegacyNodeId(node_id)) return;
    NodeState st{};
    if (!g_node_registry.getNodeState(node_id, st)) return;

    const NodeLivenessRecord &live = g_node_liveness[node_id];

    MqttClient::NodeSnapshotContext ctx{};
    const NodeFsmState &fsm = g_node_fsm[node_id];
    ctx.override_state = (fsm.macro_state == MacroState::OVERRIDE_RUN) ? "ON_LEASE" :
                         (fsm.macro_state == MacroState::OVERRIDE_HOLD_OFF) ? "OFF_PAUSE" :
                         "NONE";
    ctx.override_expiry_ms = fsm.lease_expiry_ms;
    ctx.last_command_id = g_last_command_id[node_id][0] != '\0' ? g_last_command_id[node_id] : nullptr;
    ctx.last_command_result = (fsm.last_lifecycle_event == LifecycleEvent::RF_ACKED) ? "RF_ACKED" :
                              (fsm.last_lifecycle_event == LifecycleEvent::RF_TIMEOUT_OR_NACK) ? "TIMEOUT" :
                              "REJECTED";
    ctx.last_ping_at = live.last_ping_sent_ms;
    ctx.last_ping_ok = live.last_ping_ok;
    ctx.ping_rtt_ms = live.ping_rtt_ms;
    ctx.consecutive_ping_failures = live.consecutive_failures;
    ctx.reset_reason = g_reset_reason_str;
    ctx.source = source ? source : ((fsm.macro_state == MacroState::OVERRIDE_RUN ||
                                      fsm.macro_state == MacroState::OVERRIDE_HOLD_OFF) ? "MANUAL_OVERRIDE" : "SCHEDULE");
    ctx.transition_reason = transition_reason ? transition_reason : "STATE_UPDATE";

    mqtt_client.publishNodeSnapshot(node_id, st, &ctx);
}

static void serviceStaleEvaluationTick(uint32_t current_time_ms)
{
    if (!g_gateway_operational)
        return;
    if (current_time_ms - g_last_stale_eval_ms >= 5000)
    {
        g_last_stale_eval_ms = current_time_ms;
        uint16_t newly_stale = g_node_registry.evaluateStaleNodes(current_time_ms, 15000);
        for (uint8_t i = 0; i < PRODUCTION_NODE_COUNT; ++i)
        {
            if (newly_stale & (1 << i))
            {
                uint8_t node_id = static_cast<uint8_t>(RF_PRODUCTION_MIN_NODE_ID + i);
                g_command_manager.cancelNodeCommands(node_id);
                if (isAguLegacyNodeId(node_id)) {
                    initNodeFsm(g_node_fsm[node_id], node_id);
                    g_last_command_id[node_id][0] = '\0';
                }
                char reason_buf[128];
                snprintf(reason_buf, sizeof(reason_buf), "Node %u went STALE; forced OFF, latched fault and canceled pending commands", node_id);
                mqtt_client.publishSafetyAudit("STALE_SAFE_OFF", reason_buf);
                if (isAguLegacyNodeId(node_id)) {
                    publishLegacyNodeSnapshot(node_id, "SAFE_OFF", "STALE_SAFE_OFF");
                }
                ESP_LOGW(TAG, "Node %u stale-safe-off executed.", node_id);
            }
        }
    }
}

static void serviceScheduleTick(uint32_t current_ms)
{
    if (!g_gateway_operational)
        return;
    static uint32_t last_schedule_ms = 0;
    if (current_ms - last_schedule_ms >= 1000)
    {
        last_schedule_ms = current_ms;
        g_group_scheduler.stepGroupSchedule();
        for (uint8_t id = AGU_LEGACY_MIN_NODE_ID; id <= AGU_LEGACY_MAX_NODE_ID; ++id)
        {
            const NodeFsmState &fsm = g_node_fsm[id];
            NodeState st{};
            if (g_node_registry.getNodeState(id, st))
            {
                // Active manual override strictly protects node from schedule overwrite
                if (fsm.macro_state == MacroState::OVERRIDE_RUN ||
                    fsm.macro_state == MacroState::OVERRIDE_HOLD_OFF) {
                    const NodePumpState override_desired =
                        (fsm.macro_state == MacroState::OVERRIDE_RUN) ? NodePumpState::ON : NodePumpState::OFF;
                    if (st.desired_state != override_desired) {
                        ESP_LOGD(TAG, "[SCHEDULER] Node %u schedule transition suppressed by active override (%s)",
                                 id, fsm.macro_state == MacroState::OVERRIDE_RUN ? "ON_LEASE" : "OFF_PAUSE");
                        g_node_registry.setDesiredState(id, override_desired);
                    }
                    continue;
                }
                // Schedule-driven actuation only when node is healthy and not in fault
                if (st.desired_state != st.reported_state &&
                    !st.fault_latched &&
                    st.health == NodeHealthStatus::ONLINE)
                {
                    executeAguPump(id, st.desired_state == NodePumpState::ON, nullptr);
                }
            }
        }
    }
}

static void serviceLegacyOverrideExpiry(uint32_t current_ms)
{
    if (!g_gateway_operational) return;
    for (uint8_t id = AGU_LEGACY_MIN_NODE_ID; id <= AGU_LEGACY_MAX_NODE_ID; ++id) {
        NodeFsmState &fsm = g_node_fsm[id];
        if ((fsm.macro_state != MacroState::OVERRIDE_RUN && fsm.macro_state != MacroState::OVERRIDE_HOLD_OFF) ||
            !fsm.lease_active || current_ms < fsm.lease_expiry_ms) continue;
        if (fsm.macro_state == MacroState::OVERRIDE_RUN) {
            ESP_LOGI(TAG, "Legacy node %u ON lease expired; executing auto safe-OFF", id);
            executeAguPump(id, false, nullptr);
            initNodeFsm(fsm, id);
            publishLegacyNodeSnapshot(id, "MANUAL_OVERRIDE", "LEASE_EXPIRED");
            ESP_LOGI(TAG, "Legacy node %u override expired; schedule control restored", id);
        } else if (fsm.macro_state == MacroState::OVERRIDE_HOLD_OFF) {
            ESP_LOGI(TAG, "Legacy node %u OFF pause expired; restoring schedule control", id);
            initNodeFsm(fsm, id);
            publishLegacyNodeSnapshot(id, "MANUAL_OVERRIDE", "PAUSE_EXPIRED");
        }
    }
}

static void serviceAguLivenessTick(uint32_t current_ms)
{
    if (!g_agu_liveness_enabled || !g_gateway_operational || !g_agu_legacy_host || (current_ms - g_last_agu_ping_ms < 5000)) return;
    if (g_agu_bus_busy) return;
    g_last_agu_ping_ms = current_ms;

    for (uint8_t id = AGU_LEGACY_MIN_NODE_ID; id <= AGU_LEGACY_MAX_NODE_ID; ++id) {
        if (g_agu_bus_busy) break;
        const NodeFsmState &fsm = g_node_fsm[id];
        if (fsm.macro_state != MacroState::OVERRIDE_RUN &&
            fsm.macro_state != MacroState::OVERRIDE_HOLD_OFF) {
            executeAguPing(id);
#if defined(ESP_PLATFORM) || defined(ARDUINO)
            vTaskDelay(pdMS_TO_TICKS(20));
#endif
        }
    }
}

/** Publish a lifecycle event for a node via MQTT command event topic. */
static void publishNodeLifecycleEvent(uint8_t node_id, LifecycleEvent event)
{
    const char *event_name = nullptr;
    switch (event) {
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
        fsm.evidence_stage == EvidenceStage::GATE_FEEDBACK_ON) {
        if (driver_feedback == 1) {
            advanceEvidenceStage(fsm, EvidenceStage::GATE_FEEDBACK_ON, current_ms);
        }
    }

    // If flow confirmed, advance to FLOW_CONFIRMED
    if (fsm.evidence_stage == EvidenceStage::CURRENT_DETECTED ||
        fsm.evidence_stage == EvidenceStage::GATE_FEEDBACK_ON ||
        fsm.evidence_stage == EvidenceStage::RF_ACKNOWLEDGED) {
        if (flow_lpm_x100 >= FSM_FLOW_CONFIRMED_MIN_LPM_X100) {
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
    if (!g_gateway_operational) return;
    for (uint8_t id = AGU_LEGACY_MIN_NODE_ID; id <= AGU_LEGACY_MAX_NODE_ID; ++id) {
        NodeFsmState &fsm = g_node_fsm[id];

        // 1. Lease tick — check for expired deadman lease
        if (leaseTick(fsm, current_ms)) {
            // Lease expired: dispatch OFF transaction, transition to SCHEDULE_COOLDOWN
            executeAguPump(id, false, nullptr);
            fsm.cooldown_boundary_ms = current_ms + T_COOLDOWN_MIN_MS;
            transitionMacroState(fsm, MacroState::SCHEDULE_COOLDOWN, current_ms);
            publishNodeLifecycleEvent(id, LifecycleEvent::LEASE_EXPIRED_SAFE_OFF);
        }

        // 2. Flow settle timeout check (applies to SCHEDULE_SPRAY; manual override runs are bounded by run_lease_ms deadman timer)
        if (fsm.macro_state == MacroState::SCHEDULE_SPRAY) {
            if (fsm.evidence_stage == EvidenceStage::RF_ACKNOWLEDGED ||
                fsm.evidence_stage == EvidenceStage::GATE_FEEDBACK_ON ||
                fsm.evidence_stage == EvidenceStage::CURRENT_DETECTED) {
                if ((current_ms - fsm.last_evidence_ms) > T_FLOW_SETTLE_MS) {
                    // Flow not confirmed within settle time → fault
                    executeAguPump(id, false, nullptr);
                    transitionMacroState(fsm, MacroState::FAULT_LATCH, current_ms);
                    publishNodeLifecycleEvent(id, LifecycleEvent::FAULT_LATCHED);
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
    if (!g_gateway_operational || !g_agu_legacy_host) return;
    static uint32_t last_poll_ms = 0;
    if (current_ms - last_poll_ms < T_POLL_0x0E_MS) return;
    last_poll_ms = current_ms;

    for (uint8_t id = AGU_LEGACY_MIN_NODE_ID; id <= AGU_LEGACY_MAX_NODE_ID; ++id) {
        if (g_agu_bus_busy) break;
        NodeFsmState &fsm = g_node_fsm[id];

        // Only poll if node is in active state (not BOOT_OFF or FAULT_LATCH)
        if (fsm.macro_state == MacroState::BOOT_OFF ||
            fsm.macro_state == MacroState::FAULT_LATCH) {
            continue;
        }

        // Execute 0x0E readRamBurst and update evidence pipeline
        uint8_t ram_data[8] = {};
        AguRfTransactionResult result = g_agu_legacy_host->readRamBurst(id, 0x0100, ram_data);

        if (result.result == AguRfResult::ACKED) {
            // Parse 8-byte RAM block:
            //   byte 0: reported_pump_state
            //   byte 1: driver_feedback
            //   byte 2-3: flow_lpm_x100 (uint16 LE)
            //   byte 4-5: pulse_count (uint16 LE)
            //   byte 6: fault_flags
            //   byte 7: reserved
            updateNodeEvidenceFromTelemetry(id, ram_data, current_ms);
        } else {
            // Timeout or error → potential stale
            g_node_registry.updateHealth(id, NodeHealthStatus::STALE);
        }

        vTaskDelay(pdMS_TO_TICKS(20)); // Bus guard delay between nodes
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
    ESP_LOGI(TAG, "NodeRegistry and GroupScheduler initialized (physical nodes 1..15).");
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
#if defined(ESP_PLATFORM)
    esp_reset_reason_t reason = esp_reset_reason();
    switch (reason) {
        case ESP_RST_POWERON: g_reset_reason_str = "POWERON"; break;
        case ESP_RST_EXT: g_reset_reason_str = "EXT_PIN"; break;
        case ESP_RST_SW: g_reset_reason_str = "SW_RESET"; break;
        case ESP_RST_PANIC: g_reset_reason_str = "EXCEPTION_PANIC"; break;
        case ESP_RST_INT_WDT: g_reset_reason_str = "INT_WDT"; break;
        case ESP_RST_TASK_WDT: g_reset_reason_str = "TASK_WDT"; break;
        case ESP_RST_WDT: g_reset_reason_str = "OTHER_WDT"; break;
        case ESP_RST_DEEPSLEEP: g_reset_reason_str = "DEEPSLEEP"; break;
        case ESP_RST_BROWNOUT: g_reset_reason_str = "BROWNOUT"; break;
        case ESP_RST_SDIO: g_reset_reason_str = "SDIO"; break;
        default: g_reset_reason_str = "UNKNOWN"; break;
    }
    ESP_LOGI(TAG, "[BOOT] ESP32 reset reason: %s (%d)", g_reset_reason_str, static_cast<int>(reason));
    mqtt_client.setResetReason(g_reset_reason_str);
#endif
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

    // Initialize FSM state for AGU legacy nodes only; modern nodes use the
    // authenticated PumpNodeController path.
    for (uint8_t id = AGU_LEGACY_MIN_NODE_ID; id <= AGU_LEGACY_MAX_NODE_ID; ++id) {
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

    // Service FSM deadman lease, evidence timeout, and pending-command cleanup (Track D2)
    serviceFsmTick(current_ms);

    // Service 0x0E telemetry polling on Core 1 only (Track D3)
    servicePollTelemetry(current_ms);

    // Service AGU legacy node periodic PING liveness
    serviceAguLivenessTick(current_ms);

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

static bool executeAguPing(uint8_t node_id)
{
    if (!g_rf_transport || !g_rf_transport->isInitialized() || !g_agu_legacy_host)
    {
        ESP_LOGE(TAG, "[AGU LEGACY] RF transport/host not initialized");
        return false;
    }
    if (!isAguLegacyNodeId(node_id))
    {
        ESP_LOGW(TAG, "[AGU LEGACY] Refusing PING for unsupported physical client ID %u (allowed: 1..15)", node_id);
        return false;
    }
    if (g_agu_bus_busy)
    {
        return false;
    }
    g_agu_bus_busy = true;

    if (g_wdt_registered) esp_task_wdt_reset();

    NodeLivenessRecord &live = g_node_liveness[node_id];
    live.last_ping_sent_ms = millis();

    const AguRfTransactionResult result = g_agu_legacy_host->pingNode(node_id);

    if (g_wdt_registered) esp_task_wdt_reset();
    g_agu_bus_busy = false;

    ESP_LOGI(TAG, "[AGU LEGACY] PING node=%u response=0x%02X result=%u rtt=%u ms attempt=%u/%u",
             node_id, result.response_byte, static_cast<unsigned>(result.result),
             (unsigned)result.rtt_ms, result.attempts, AGU_LEGACY_MAX_ATTEMPTS);

    if (result.result == AguRfResult::ACKED)
    {
        live.last_ping_ok = true;
        live.last_ping_ok_ms = millis();
        live.ping_rtt_ms = result.rtt_ms;
        live.consecutive_failures = 0;
        strncpy(live.last_result, "ACKED", sizeof(live.last_result) - 1);
        if (!live.is_healthy) {
            live.is_healthy = true;
            live.health_transition_ms = millis();
            ESP_LOGI(TAG, "[AGU LIVENESS] Node %u recovered ONLINE (RTT=%u ms)", node_id, (unsigned)result.rtt_ms);
        }
        g_node_registry.refreshLiveness(node_id, millis());
        publishLegacyNodeSnapshot(node_id, "LIVENESS", "PING_SUCCESS");
        return true;
    }
    else
    {
        live.last_ping_ok = false;
        live.consecutive_failures++;
        strncpy(live.last_result, result.result == AguRfResult::TIMEOUT ? "TIMEOUT" : "ERROR", sizeof(live.last_result) - 1);
        ESP_LOGW(TAG, "[AGU LIVENESS] Node %u ping failed (%s), consecutive failures: %u",
                 node_id, live.last_result, live.consecutive_failures);
        if (live.consecutive_failures >= 3 && live.is_healthy)
        {
            live.is_healthy = false;
            live.health_transition_ms = millis();
            ESP_LOGE(TAG, "[AGU LIVENESS] Node %u marked STALE after %u failures; entering safe-off",
                     node_id, live.consecutive_failures);
            g_node_registry.updateHealth(node_id, NodeHealthStatus::STALE);
            char audit_msg[128];
            snprintf(audit_msg, sizeof(audit_msg), "Node %u liveness lost after %u consecutive ping timeouts",
                     node_id, live.consecutive_failures);
            mqtt_client.publishSafetyAudit("LIVENESS_LOST", audit_msg);
            publishLegacyNodeSnapshot(node_id, "LIVENESS", "LIVENESS_LOST");
        }
        return false;
    }
}

static void runRfUartDiagnostic(bool loopback)
{
    if (!g_rf_transport || !g_rf_transport->isInitialized())
    {
        ESP_LOGE(TAG, "RF UART diagnostic unavailable: transport is not initialized");
        return;
    }

    // The first pattern is the exact AGU Node 7 ping frame. The second pattern
    // is deliberately distinctive for a physical TX-to-RX loopback test.
    static const uint8_t agu_ping[] = {0x05, 0x05, 0xA5, 0x07, 0x2B, 0xB8};
    static const uint8_t loopback_pattern[] = {0x55, 0xAA, 0x00, 0xFF, 0x05, 0x05, 0xA5, 0x07, 0x2B, 0xB8};
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

static bool executeAguPump(uint8_t node_id, bool turn_on, const char *command_id)
{
    if (!g_rf_transport || !g_rf_transport->isInitialized() || !g_agu_legacy_host) {
        ESP_LOGE(TAG, "[AGU LEGACY] RF transport/host not initialized");
        if (command_id && command_id[0] != '\0') mqtt_client.publishCommandAck(command_id, "REJECTED", node_id, "RF transport unavailable");
        return false;
    }
    if (!isAguLegacyNodeId(node_id)) {
        ESP_LOGW(TAG, "[AGU LEGACY] Refusing PUMP command for unsupported physical client ID %u (allowed: 1..15)", node_id);
        if (command_id && command_id[0] != '\0') mqtt_client.publishCommandAck(command_id, "REJECTED", node_id, "Unsupported AGU legacy client ID");
        return false;
    }

    if (g_agu_bus_busy) {
        ESP_LOGW(TAG, "[AGU LEGACY] Bus busy during PUMP request; waiting...");
        uint32_t wait_start = millis();
        while (g_agu_bus_busy && (millis() - wait_start < 1000)) {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
            vTaskDelay(pdMS_TO_TICKS(10));
#endif
        }
    }
    g_agu_bus_busy = true;

    if (g_wdt_registered) esp_task_wdt_reset();

    const AguRfTransactionResult result = g_agu_legacy_host->setPump(node_id, turn_on);

    if (g_wdt_registered) esp_task_wdt_reset();
    g_agu_bus_busy = false;

    ESP_LOGI(TAG, "[AGU LEGACY] PUMP %s node=%u response=0x%02X result=%u rtt=%u ms attempt=%u/%u",
             turn_on ? "ON" : "OFF", node_id, result.response_byte, static_cast<unsigned>(result.result),
             (unsigned)result.rtt_ms, result.attempts, AGU_LEGACY_MAX_ATTEMPTS);

    NodeFsmState &fsm = g_node_fsm[node_id];
    fsm.last_lifecycle_event = (result.result == AguRfResult::ACKED)
                                   ? LifecycleEvent::RF_ACKED
                                   : LifecycleEvent::RF_TIMEOUT_OR_NACK;

    if (result.result != AguRfResult::ACKED) {
        const char *reason = result.result == AguRfResult::TIMEOUT ? "Legacy node timeout after 3 retries" :
                             result.result == AguRfResult::UART_NOT_READY ? "Legacy RF UART not ready" :
                             result.result == AguRfResult::TX_ERROR ? "Legacy RF transport TX error" :
                             result.result == AguRfResult::INVALID_NODE_ID ? "Invalid legacy node ID" : "Unexpected legacy response";
        if (command_id && command_id[0] != '\0') mqtt_client.publishCommandAck(command_id, "REJECTED", node_id, reason);
        publishLegacyNodeSnapshot(node_id, g_override_source[node_id][0] ? g_override_source[node_id] : "MANUAL_OVERRIDE",
                                  "PUMP_REJECTED");
        return false;
    }

    const NodePumpState state = turn_on ? NodePumpState::ON : NodePumpState::OFF;
    g_node_registry.setDesiredState(node_id, state);
    g_node_registry.updateTelemetryDetailed(node_id, state, turn_on ? 1 : 0, 0, 0, 0, 0, 0, 0, 0);

    // --- Track E2: FSM integration after AGU ACK ---
    g_pending_commands.insert(node_id, command_id ? command_id : "LOCAL", millis());
    advanceEvidenceStage(fsm, EvidenceStage::RF_ACKNOWLEDGED, millis());

    if (turn_on) {
        if (fsm.run_lease_ms < NodeFsmLimits::RUN_LEASE_MIN_MS) {
            fsm.run_lease_ms = 30000; // Safe default 30s lease for manual/bench commands
        }
        fsm.lease_active = true;
        fsm.lease_start_ms = millis();
        fsm.lease_expiry_ms = millis() + fsm.run_lease_ms;
        fsm.macro_state = MacroState::OVERRIDE_RUN;
    } else {
        fsm.lease_active = false;
        fsm.run_lease_ms = 0;
        fsm.macro_state = MacroState::BOOT_OFF;
    }

    mqtt_client.publishLifecycleEvent(node_id, command_id, LifecycleEvent::RF_ACKED);
    publishNodeLifecycleEvent(node_id, LifecycleEvent::RF_ACKED);

    if (command_id && command_id[0] != '\0') {
        mqtt_client.publishCommandAck(command_id, "RF_ACKED", node_id, "Legacy AGU ACK 0x5A received");
    }
    publishLegacyNodeSnapshot(node_id, g_override_source[node_id][0] ? g_override_source[node_id] : "MANUAL_OVERRIDE",
                              turn_on ? "PUMP_ON_ACKED" : "PUMP_OFF_ACKED");
    return true;
}

static void executeAguGetId()
{
    if (!g_rf_transport)
    {
        ESP_LOGE(TAG, "RF transport not initialized");
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
            if (count >= 3) break;
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
    if (!g_rf_transport)
    {
        ESP_LOGE(TAG, "RF transport not initialized");
        return;
    }
    if (!isAguLegacyNodeId(new_id))
    {
        ESP_LOGW(TAG, "Warning: Node ID %u is outside AGU legacy client range (1..15)!", new_id);
        return;
    }
    uint8_t tx_buf[16];
    size_t len = AguLegacy::AguLegacyCodec::encodeSetId(new_id, tx_buf, sizeof(tx_buf));
    ESP_LOGI(TAG, "[AGU TX] SET_ID command -> New ID = %u (%zu bytes: %02X %02X %02X)",
             new_id, len, tx_buf[0], tx_buf[1], tx_buf[2]);
    g_rf_transport->flushRx();
    g_rf_transport->send(tx_buf, len);
    vTaskDelay(pdMS_TO_TICKS(300));
    ESP_LOGI(TAG, "[AGU TX] SET_ID transmitted. Reading back ID to verify...");
    executeAguGetId();
}

static void executeRfScan(const char *scan_id)
{
    if (!g_rf_transport || !g_rf_transport->isInitialized() || !g_agu_legacy_host) {
        ESP_LOGE(TAG, "[AGU LEGACY SCAN] RF transport/host unavailable");
        mqtt_client.publishScanResults(scan_id, nullptr, 0, 0, "FAILED", "RF_LEGACY_UNAVAILABLE");
        return;
    }
    ESP_LOGW(TAG, "[AGU LEGACY] RF scan uses unauthenticated AGU_LEGACY_SCI compatibility mode");
    g_agu_bus_busy = true;
    const uint32_t started = millis();
    constexpr size_t agu_node_count = AGU_LEGACY_MAX_NODE_ID - AGU_LEGACY_MIN_NODE_ID + 1;
    MqttClient::DiscoveredRfNodeInfo results[agu_node_count]{};
    for (size_t index = 0; index < agu_node_count; ++index) {
        const uint8_t node_id = static_cast<uint8_t>(AGU_LEGACY_MIN_NODE_ID + index);
        auto &result = results[index];
        result.node_id = node_id;
        const AguRfTransactionResult transaction = g_agu_legacy_host->pingNode(node_id);
        result.online = transaction.result == AguRfResult::ACKED;
        result.rtt_ms = transaction.rtt_ms;
        result.failure_code = result.online ? 0 : transaction.result == AguRfResult::TIMEOUT ? 1 : transaction.result == AguRfResult::UNEXPECTED_RESPONSE ? 2 : transaction.result == AguRfResult::INVALID_NODE_ID ? 4 : transaction.result == AguRfResult::UART_NOT_READY ? 5 : 3;
        ESP_LOGI(TAG, "[AGU LEGACY SCAN] node=%u online=%s response=0x%02X result=%u rtt=%u ms attempt=%u/%u", node_id, result.online ? "yes" : "no", transaction.response_byte, static_cast<unsigned>(transaction.result), (unsigned)transaction.rtt_ms, transaction.attempts, AGU_LEGACY_MAX_ATTEMPTS);
    }
    g_agu_bus_busy = false;
    const uint32_t duration_ms = millis() - started;
    const bool published = mqtt_client.publishScanResults(
        scan_id, results, agu_node_count, duration_ms);
    ESP_LOGI(TAG, "[AGU LEGACY SCAN] result publish %s scan_id=%s duration=%u ms",
             published ? "QUEUED" : "FAILED", scan_id ? scan_id : "(null)",
             static_cast<unsigned>(duration_ms));
}

static void executeRfClaimNode(uint8_t from_id, uint8_t to_id, const char *command_id)
{
    if (!g_rf_transport)
    {
        ESP_LOGE(TAG, "RF transport not initialized");
        mqtt_client.publishCommandAck(command_id, "REJECTED", to_id, "RF transport unavailable");
        return;
    }
    if (!isAguLegacyNodeId(to_id))
    {
        ESP_LOGE(TAG, "[RF CLAIM] Invalid target node ID %u (must be 1..15)", to_id);
        mqtt_client.publishCommandAck(command_id, "REJECTED", to_id, "Target node ID must be one of 1..15");
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
        g_node_registry.updateTelemetryDetailed(to_id, NodePumpState::OFF, 0, 0, 0, 0, 0, 0, 0, 0);
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
        if (!isAguLegacyNodeId(node_id)) {
            const ExternalOverridePolicy policy{command.source, command.values[0], command.values[1]};
            const bool accepted = g_command_manager.queueExternalNodeCommand(
                node_id, command.desired_state, command.command_id, &policy);
            mqtt_client.publishCommandAck(command.command_id, accepted ? "ACCEPTED" : "REJECTED",
                                          node_id, accepted ? "Modern node override accepted and queued"
                                                            : "Modern node override mutation failed");
            return;
        }

        const uint32_t duration_ms = is_on ? command.values[0] : command.values[1];
        const uint32_t effective_duration_ms = (duration_ms > 0) ? duration_ms : 30000;

        if (!isAguLegacyNodeId(node_id)) {
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
        if (!executeAguPump(node_id, is_on, command.command_id)) {
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
            if (g_rf_transport) g_rf_transport->setBaudRate(g_rf_transport->getBaudRate(), 0x800001c);
            ESP_LOGI(TAG, "RF UART format set to SERIAL_8N1 (1 stop bit).");
        }
        else if (strstr(cmd, "8n2") != nullptr || strstr(cmd, "8N2") != nullptr)
        {
            if (g_rf_transport) g_rf_transport->setBaudRate(g_rf_transport->getBaudRate(), 0x800003c);
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
        if (strlen(cmd) > 4) node = atoi(cmd + 4);
        if (node < 1) node = 1;
        executeAguPing(static_cast<uint8_t>(node));
    }
    else if (strncasecmp(cmd, "on", 2) == 0 && (cmd[2] == ' ' || cmd[2] == '\0'))
    {
        int node = 1;
        int duration_sec = 30; // default 30s
        const char *p = cmd + 2;
        while (*p == ' ') p++;
        if (*p) {
            node = atoi(p);
            while (*p && *p != ' ') p++;
            while (*p == ' ') p++;
            if (*p) duration_sec = atoi(p);
        }
        if (node < 1) node = 1;
        if (duration_sec < 1) duration_sec = 1;
        if (duration_sec > 300) duration_sec = 300;

        NodeFsmState &fsm = g_node_fsm[node];
        fsm.run_lease_ms = static_cast<uint32_t>(duration_sec) * 1000U;
        ESP_LOGI(TAG, "[CLI] PUMP ON node %d for %d seconds (lease=%u ms)", node, duration_sec, (unsigned)fsm.run_lease_ms);
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
    else if (strncasecmp(cmd, "setid", 5) == 0)
    {
        int id = 1;
        if (strlen(cmd) > 5) id = atoi(cmd + 5);
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
        if (strlen(cmd) > 6) baud = strtoul(cmd + 6, nullptr, 10);
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
                     p, pu, pd, fl, (pu == 0) ? "SHORTED TO GND!" : (pd == 1) ? "ACTIVE HIGH!" : "NORMAL/FLOATING");
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
            ESP_LOGI(TAG, "[LIVENESS] Periodic AGU ping enabled");
        }
    }
    else if (strncasecmp(cmd, "rfchannel", 9) == 0)
    {
        int ch = 1;
        if (strlen(cmd) > 9) ch = atoi(cmd + 9);
        if (ch >= 1 && ch <= 127 && g_rf_transport)
        {
            g_agu_bus_busy = true;
            char at_ch[32];
            snprintf(at_ch, sizeof(at_ch), "AT+C%03d", ch);
            g_rf_transport->flushRx();
            ESP_LOGI(TAG, "[RF CHANNEL] Setting channel -> %s", at_ch);
            g_rf_transport->send(reinterpret_cast<const uint8_t*>(at_ch), strlen(at_ch));
            vTaskDelay(pdMS_TO_TICKS(500));
            uint8_t resp[64] = {};
            size_t r = g_rf_transport->receive(resp, sizeof(resp) - 1);
            if (r > 0)
            {
                resp[r] = '\0';
                char hex_str[128] = {};
                for (size_t i = 0; i < r && i < 16; ++i) {
                    snprintf(hex_str + strlen(hex_str), sizeof(hex_str) - strlen(hex_str), "%02X ", resp[i]);
                }
                ESP_LOGI(TAG, "[RF CHANNEL] Response (%zu bytes: %s): %s", r, hex_str, reinterpret_cast<char*>(resp));
            }
            else
            {
                ESP_LOGW(TAG, "[RF CHANNEL] No response (ensure SET pin is connected to GND)");
            }
            g_agu_bus_busy = false;
        }
        else
        {
            ESP_LOGW(TAG, "Usage: rfchannel <1..127> (e.g. 'rfchannel 7' or 'rfchannel 1')");
        }
    }
    else if (strncasecmp(cmd, "at", 2) == 0)
    {
        if (g_rf_transport)
        {
            g_agu_bus_busy = true;
            g_rf_transport->flushRx();
            char at_cmd[64];
            snprintf(at_cmd, sizeof(at_cmd), "%s", cmd);
            for (char *p = at_cmd; *p; ++p) *p = toupper(static_cast<unsigned char>(*p));
            ESP_LOGI(TAG, "[AT TX] Sending without CRLF: %s", at_cmd);
            g_rf_transport->send(reinterpret_cast<const uint8_t*>(at_cmd), strlen(at_cmd));
            vTaskDelay(pdMS_TO_TICKS(500));
            uint8_t resp[128] = {};
            size_t r = g_rf_transport->receive(resp, sizeof(resp) - 1);
            if (r > 0)
            {
                resp[r] = '\0';
                char hex_str[256] = {};
                for (size_t i = 0; i < r && i < 32; ++i) {
                    snprintf(hex_str + strlen(hex_str), sizeof(hex_str) - strlen(hex_str), "%02X ", resp[i]);
                }
                ESP_LOGI(TAG, "[AT RX] Response (%zu bytes: %s): %s", r, hex_str, reinterpret_cast<char*>(resp));
            }
            else
            {
                ESP_LOGW(TAG, "[AT RX] No response / timeout");
            }
            g_agu_bus_busy = false;
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
