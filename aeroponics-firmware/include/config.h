#pragma once

#include <cstddef>
#include <cstdint>
#include "core/hmac_sha256.h"

#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
#include <freertos/FreeRTOS.h>
#else
using TickType_t = uint32_t;
using UBaseType_t = uint32_t;
using BaseType_t = int32_t;
#endif

// ============================================================================
// Aeroponics Lean Firmware — Single Source of Truth (SSOT) Production Config
// Industrial IoT Architecture Specification — Unified Configuration Standard
// ============================================================================

// ============================================================================
// SECTION 1: RTOS Task Topologies, Watchdog & Priority Invariants
// ============================================================================
// Core Pinning Model (ESP32-S3 Dual-Core):
// - Core 0 (Protocol Core): Wi-Fi driver, LwIP stack, esp_event loop, wifi_ctrl_task
// - Core 1 (Application Core): mqtt_task, application business logic, scheduler
constexpr uint32_t WDT_TIMEOUT_S = 30;
constexpr uint32_t WDT_TIMEOUT_MS = WDT_TIMEOUT_S * 1000U;

// MQTT Background FreeRTOS Task Configuration
constexpr const char *MQTT_TASK_NAME = "mqtt_task";
constexpr uint32_t MQTT_TASK_STACK_SIZE = 8192;
constexpr UBaseType_t MQTT_TASK_PRIORITY = 3; // Application layer; above wifi_ctrl_task (2)
constexpr BaseType_t MQTT_TASK_CORE = 1;      // Core 1: app core, Wi-Fi driver on Core 0
constexpr uint32_t MQTT_TASK_TICK_INTERVAL_MS = 100;

// Wi-Fi Controller FreeRTOS Task Configuration
constexpr const char *WIFI_TASK_NAME = "wifi_ctrl_task";
constexpr uint32_t WIFI_TASK_STACK_SIZE = 16384;
constexpr UBaseType_t WIFI_TASK_PRIORITY = 2; // Protocol layer; below mqtt_task (3)
constexpr BaseType_t WIFI_TASK_CORE = 0;      // Core 0: Protocol core
constexpr uint32_t WIFI_TASK_TICK_DELAY_MS = 50;

// Legacy / Prototype Relay Task Configuration
constexpr uint32_t RELAY_TASK_STACK_SIZE = 8192;
constexpr UBaseType_t RELAY_TASK_PRIORITY = 3;
constexpr BaseType_t RELAY_TASK_CORE = 1;
constexpr uint32_t RELAY_MUTEX_TIMEOUT_MS = 100;
constexpr uint32_t SCHEDULER_STATE_MUTEX_TIMEOUT_MS = 100;
constexpr uint32_t RELAY_TASK_TICK_INTERVAL_MS = 1000;
constexpr uint32_t RELAY_TASK_STARTUP_TIMEOUT_MS = 1000;
constexpr uint32_t RELAY_TASK_CALLBACK_EXIT_TIMEOUT_MS = WDT_TIMEOUT_MS;

// ============================================================================
// SECTION 2: Hardware Pinouts & Board IO Contracts (ESP32-S3 DevKitC-1)
// ============================================================================
// Hardware I2C for DS3231 RTC
constexpr uint8_t RTC_SDA_PIN = 21;
constexpr uint8_t RTC_SCL_PIN = 22;

// Farmer Portal / Configuration Trigger Button & UI LED
constexpr int8_t PORTAL_BUTTON_PIN = 0; // ESP32-S3 BOOT button (active LOW)
#if defined(RGB_BUILTIN)
constexpr int16_t LED_STATUS_PIN = RGB_BUILTIN; // Built-in WS2812 RGB LED on ESP32-S3 DevKitC-1
#else
constexpr int16_t LED_STATUS_PIN = 48; // Built-in WS2812 RGB LED on ESP32-S3 DevKitC-1 (GPIO 48)
#endif
constexpr uint32_t BUTTON_DEBOUNCE_DELAY_MS = 50;
constexpr uint32_t BUTTON_LONG_PRESS_DURATION_MS = 2500; // 2.5s for portal trigger
constexpr uint32_t LED_BLINK_INTERVAL_MS = 500;

// RF UART Interface (Separate from USB Debug Serial)
// MKE-K01 / ESP32-S3 DevKitC-1 Pinout Contract (5-pin Jack on Header J1: Pins 18-22):
// - Pin 18 (GPIO17): RF UART TX (ESP32 TX -> RF RXD)
// - Pin 19 (GPIO18): RF UART RX (ESP32 RX <- RF TXD)
// - Pin 20 (GPIO14): RỖNG / NC (Unconnected pin on 5-pin jack)
// - Pin 21 (GND):    Common Logic Ground
// - Pin 22 (5V):     5V VCC Power Rail (VBUS)
constexpr uint8_t RF_UART_NUM = 1;
constexpr int8_t RF_UART_TX_PIN = 17;
constexpr int8_t RF_UART_RX_PIN = 18;
constexpr int8_t RF_UART_NC_PIN = 14;
constexpr int8_t RF_UART_M0_PIN = -1;
constexpr int8_t RF_UART_M1_PIN = -1;
constexpr int8_t RF_UART_AUX_PIN = -1;
// The RF module host UART is configured by the AGU-Aeroponics deployment.
// The module's over-the-air/node UART rate is a separate setting.
constexpr uint32_t RF_UART_DEFAULT_BAUD_RATE = 38400;

// Production RF hardware aliases
constexpr uint8_t RF_DEFAULT_UART_NUM = RF_UART_NUM;
constexpr int8_t RF_DEFAULT_TX_PIN = RF_UART_TX_PIN;
constexpr int8_t RF_DEFAULT_RX_PIN = RF_UART_RX_PIN;
constexpr int8_t RF_DEFAULT_M0_PIN = RF_UART_M0_PIN;
constexpr int8_t RF_DEFAULT_M1_PIN = RF_UART_M1_PIN;
constexpr int8_t RF_DEFAULT_AUX_PIN = RF_UART_AUX_PIN;
constexpr uint32_t RF_DEFAULT_BAUD_RATE = RF_UART_DEFAULT_BAUD_RATE;

// Legacy 4-Relay Prototype Hardware Pinouts
constexpr uint8_t RELAY_PIN_1 = 1;
constexpr uint8_t RELAY_PIN_2 = 2;
constexpr uint8_t RELAY_PIN_3 = 3;
constexpr uint8_t RELAY_PIN_4 = 4;
constexpr uint8_t TOTAL_RELAYS = 4;

// ============================================================================
// SECTION 3: Serial Interface & Debug CLI Work Budgets
// ============================================================================
constexpr uint32_t SERIAL_BAUD_RATE = 115200;
constexpr uint32_t BOOT_FAILURE_SAFE_STATE_INTERVAL_MS = 1000;
constexpr size_t MAX_SERIAL_BYTES_PER_TICK = 64;
constexpr size_t SERIAL_COMMAND_BUFFER_SIZE = 128;

// ============================================================================
// SECTION 4: Wireless Radio / RF Subsystem & Node Topologies
// ============================================================================
constexpr uint8_t RF_GATEWAY_NODE_ID = 0;
constexpr uint8_t RF_MIN_NODE_ID = 1; // protocol address-space minimum
// Protocol capacity supports up to 12 nodes (Backlog/Expansion)
constexpr uint8_t RF_MAX_NODE_ID = 12;
constexpr uint8_t AGU_LEGACY_MIN_NODE_ID = 4;
constexpr uint8_t AGU_LEGACY_MAX_NODE_ID = 7;
constexpr uint8_t RF_PRODUCTION_MIN_NODE_ID = 4;
constexpr uint8_t RF_PRODUCTION_MAX_NODE_ID = 7;
constexpr uint8_t PRODUCTION_NODE_COUNT =
    RF_PRODUCTION_MAX_NODE_ID - RF_PRODUCTION_MIN_NODE_ID + 1;
constexpr uint8_t RF_MAX_PROTOCOL_NODE_ID = RF_MAX_NODE_ID;
constexpr uint8_t MAX_NODES = RF_MAX_NODE_ID;
constexpr uint8_t PRODUCTION_MAX_NODES = PRODUCTION_NODE_COUNT;

inline bool isProductionNodeId(uint8_t node_id)
{
    return node_id >= RF_PRODUCTION_MIN_NODE_ID &&
           node_id <= RF_PRODUCTION_MAX_NODE_ID;
}

// AGU legacy SCI topology. These are physical RF addresses, not logical
// actuator slots. The legacy client firmware accepts only these four IDs.
constexpr uint32_t AGU_LEGACY_ACK_TIMEOUT_MS = 300;
constexpr uint8_t AGU_LEGACY_MAX_ATTEMPTS = 3;
constexpr uint32_t AGU_LEGACY_RETRY_GUARD_MS = 50;

inline bool isAguLegacyNodeId(uint8_t node_id)
{
    return node_id >= AGU_LEGACY_MIN_NODE_ID && node_id <= AGU_LEGACY_MAX_NODE_ID;
}

// RF Transport Framing Constraints
constexpr uint8_t RF_SOF_BYTE_1 = 0xAA;
constexpr uint8_t RF_SOF_BYTE_2 = 0x55;
constexpr uint8_t RF_PROTOCOL_VERSION = 0x02;
constexpr size_t RF_HEADER_SIZE = 17;
constexpr size_t RF_MAX_PAYLOAD_SIZE = 64;
constexpr size_t RF_HEADER_PAYLOAD_LENGTH_OFFSET = 16;
constexpr size_t RF_MAX_FRAME_SIZE = RF_HEADER_SIZE + RF_MAX_PAYLOAD_SIZE + HMAC_TAG_SIZE + 2;
constexpr size_t RF_MAX_RX_BUFFER_SIZE = RF_MAX_FRAME_SIZE;
constexpr size_t UART_RF_DEFAULT_RX_BUFFER_CAPACITY = 256;
constexpr size_t DUPLICATE_CACHE_DEFAULT_CAPACITY = 64;

// RF Timing, Deadman Leases & Retry Policies
constexpr uint8_t DEFAULT_MAX_RF_RETRIES = 3;
constexpr uint32_t DEFAULT_RF_RETRY_INTERVAL_MS = 1000;
constexpr uint8_t MAX_RF_RETRIES = DEFAULT_MAX_RF_RETRIES;
constexpr uint32_t RF_RETRY_INTERVAL_MS = DEFAULT_RF_RETRY_INTERVAL_MS;

constexpr uint32_t DEFAULT_RUN_LEASE_MS = 60000;        // 60-second lease
constexpr uint32_t DEFAULT_MAX_ON_DURATION_MS = 300000; // 5-minute max safety cap
constexpr uint32_t RF_FEEDBACK_DEADLINE_MS = 5000;      // 5-second feedback window
constexpr uint32_t RF_INTER_BYTE_TIMEOUT_MS = 50;
constexpr uint32_t RF_HEARTBEAT_INTERVAL_MS = 5000;
constexpr uint32_t RF_STALE_THRESHOLD_MS = 15000;
constexpr uint16_t RF_SEQUENCE_WRAP_WINDOW = 32767;

// ============================================================================
// SECTION 5: Wi-Fi Station, Captive Farmer Portal & Roaming
// ============================================================================
constexpr size_t MAX_SAVED_WIFI = 3;
constexpr size_t WIFI_MAX_SSID_LEN = 32;
constexpr size_t WIFI_MAX_PASS_LEN = 64;
constexpr uint32_t WIFI_BLOB_MAGIC = 0x57494649; // "WIFI"
constexpr uint8_t WIFI_BLOB_VERSION = 1;

constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 30000;
constexpr uint32_t WIFI_INITIAL_BACKOFF_MS = 5000; // Initial WiFi reconnect backoff
constexpr uint32_t WIFI_CONNECT_POLL_INTERVAL_MS = 500;
constexpr uint32_t WIFI_RECONNECT_CHECK_INTERVAL_MS = 60000;
constexpr uint32_t WIFI_CONNECT_ATTEMPT_TIMEOUT_MS = 20000;
constexpr uint32_t WIFI_MAX_FAILED_ATTEMPTS = 3;
constexpr uint32_t WIFI_MAX_NO_MATCH_SCANS = 5;

// Wi-Fi Roaming & RSSI Thresholds
constexpr uint32_t WIFI_ROAMING_CHECK_INTERVAL_MS = 60000;
constexpr int8_t WIFI_ROAMING_RSSI_THRESHOLD = -80; // dBm
constexpr int8_t WIFI_ROAMING_HYSTERESIS_DBM = 15;  // dBm
constexpr uint32_t WIFI_RECURRENT_WARN_SUPPRESSION_MS = 60000;

// Farmer Portal SoftAP & Web Configuration
constexpr uint32_t PORTAL_TIMEOUT_MS = 300000; // 5-minute auto-close
constexpr uint32_t FARMER_PORTAL_TIMEOUT_MS = PORTAL_TIMEOUT_MS;
constexpr uint32_t PORTAL_SCAN_CACHE_TTL_MS = 20000;    // 20-second scan cache TTL
constexpr uint32_t PORTAL_SCAN_TIMEOUT_MS = 10000;      // 10-second async scan timeout
constexpr uint32_t PORTAL_AP_STABILIZE_DELAY_MS = 1000; // 1s stabilization delay
constexpr char PORTAL_AP_SSID[] = "KHI_CANH_CAI_DAT";
constexpr size_t MAX_RAW_SCAN_NETWORKS = 32;
constexpr size_t MAX_SCAN_NETWORKS = 20;
constexpr size_t PORTAL_SCAN_JSON_BUFFER_SIZE = 2048;

// ============================================================================
// SECTION 6: Network Time Protocol (NTP) & Regional Localization
// ============================================================================
constexpr int32_t TIMEZONE_OFFSET_S = 25200; // UTC+7 (7 * 3600 seconds) Asia/Ho_Chi_Minh
constexpr int32_t DAYLIGHT_OFFSET_S = 0;
constexpr int32_t ICT_TIMEZONE_OFFSET_SECONDS = TIMEZONE_OFFSET_S;

constexpr const char *NTP_SERVER_PRIMARY = "pool.ntp.org";
constexpr uint32_t NTP_POLL_INTERVAL_MS = 500;
constexpr uint32_t NTP_SYNC_TIMEOUT_MS = 10000;
constexpr uint32_t SYSTEM_TIME_READ_TIMEOUT_MS = 10;

constexpr uint8_t DAY_START_HOUR = 6;    // 06:00
constexpr uint8_t NIGHT_START_HOUR = 18; // 18:00

// ============================================================================
// SECTION 7: MQTT Control Plane & Telemetry Pipeline
// ============================================================================
constexpr uint32_t MQTT_HEARTBEAT_INTERVAL_MS = 10000;
constexpr uint32_t MQTT_RECONNECT_BASE_S = 1;
constexpr uint32_t MQTT_RECONNECT_MAX_S = 60;
constexpr uint32_t MQTT_MAX_RECONNECT_RETRIES = 5;
constexpr uint16_t MQTT_KEEPALIVE_S = 30;

// MQTT Buffer Capacities & Wire Constraints
constexpr size_t MQTT_BUFFER_SIZE = 2048;
constexpr size_t MQTT_LWT_DOC_SIZE = 256;
constexpr size_t MQTT_HEARTBEAT_PAYLOAD_SIZE = 512;
constexpr size_t MQTT_HEARTBEAT_DOC_SIZE = 512;
constexpr size_t MQTT_COMMAND_DOC_SIZE = 1024;
constexpr size_t MQTT_TELEMETRY_DOC_SIZE = 512;
// Gateway scan results include one bounded record per physical AGU node.
// Keep enough room for the serialized 4-node legacy scan payload; the scan
// publisher uses a 768-byte serialization buffer and must not be rejected by
// the outbound queue's shared payload limit.
constexpr size_t MQTT_TELEMETRY_PAYLOAD_SIZE = 768;
constexpr size_t MQTT_TOPIC_BUFFER_SIZE = 192;
constexpr size_t MQTT_CLIENT_ID_BUFFER_SIZE = 96;
constexpr size_t MQTT_BROKER_HOST_BUFFER_SIZE = 128;
constexpr size_t MQTT_USERNAME_BUFFER_SIZE = 64;
constexpr size_t MQTT_PASSWORD_BUFFER_SIZE = 64;
constexpr size_t MQTT_DEVICE_ID_BUFFER_SIZE = 64;
constexpr size_t MQTT_DEVICE_ID_MAX_LENGTH = MQTT_DEVICE_ID_BUFFER_SIZE - 1;
constexpr const char *MQTT_CLIENT_ID_PREFIX = "aero-";

constexpr uint8_t MQTT_LWT_QOS = 1;
constexpr bool MQTT_LWT_RETAIN = true;
constexpr uint8_t MQTT_COMMAND_QOS = 1;
constexpr bool MQTT_PUBLISH_RETAIN = false;

// MQTT Queue Topology (Admission & Lane Isolation)
constexpr size_t MQTT_INBOUND_COMMAND_QUEUE_DEPTH = 16;
constexpr size_t MQTT_OUTBOUND_EVENT_QUEUE_DEPTH = 24;
constexpr size_t MQTT_OUTBOUND_ACK_QUEUE_DEPTH = MQTT_INBOUND_COMMAND_QUEUE_DEPTH + 1;
constexpr size_t MQTT_BACKPRESSURE_FAILURE_QUEUE_DEPTH = MQTT_INBOUND_COMMAND_QUEUE_DEPTH;
constexpr size_t MQTT_OUTBOUND_TELEMETRY_QUEUE_DEPTH =
    MQTT_OUTBOUND_EVENT_QUEUE_DEPTH - MQTT_INBOUND_COMMAND_QUEUE_DEPTH;
constexpr size_t MQTT_OUTBOUND_DRAIN_BUDGET = 8;
constexpr uint32_t MQTT_QUEUE_AUDIT_INTERVAL_MS = 10000;

// MQTT Topic Hierarchy & Suffixes
constexpr const char *MQTT_TOPIC_BASE = "aeroponics/device";
constexpr const char *MQTT_STATUS_SUFFIX = "/status";
constexpr const char *MQTT_TELEMETRY_GROUP_SUFFIX = "/telemetry/group/";
constexpr const char *MQTT_TELEMETRY_NODE_SUFFIX = "/telemetry/node/";
constexpr const char *MQTT_COMMAND_TREATMENT_SUFFIX = "/command/config/treatment";
constexpr const char *MQTT_COMMAND_ASSIGNMENT_SUFFIX = "/command/config/assignment";
constexpr const char *MQTT_COMMAND_FLOW_POLICY_SUFFIX = "/command/config/flow-policy";
constexpr const char *MQTT_COMMAND_NODE_OVERRIDE_SUFFIX = "/command/node/";
constexpr const char *MQTT_COMMAND_GROUP_CONTROL_SUFFIX = "/command/group/";
constexpr const char *MQTT_COMMAND_GATEWAY_SUFFIX = "/command/gateway/";
constexpr const char *MQTT_TELEMETRY_GATEWAY_SCAN_RESULTS_SUFFIX = "/telemetry/gateway/scan_results";
constexpr const char *MQTT_ACK_PREFIX_SUFFIX = "/ack/";
constexpr const char *MQTT_COMMAND_EVENT_PREFIX_SUFFIX = "/telemetry/command/";
constexpr const char *MQTT_COMMAND_EVENT_SUFFIX = "/event";
constexpr const char *MQTT_WILDCARD_SINGLE_LEVEL = "+";

// Legacy Relay MQTT Topics & Tokens
constexpr const char *MQTT_COMMAND_SUFFIX = "/command/relay/";
constexpr const char *MQTT_TELEMETRY_SUFFIX = "/telemetry/relay/";
constexpr const char *MQTT_SCHEDULE_SUFFIX = "/schedule";
constexpr const char *MQTT_OVERRIDE_SUFFIX = "/override";
constexpr const char *MQTT_SCHEDULE_TOKEN = "schedule";
constexpr const char *MQTT_OVERRIDE_TOKEN = "override";
constexpr const char *MQTT_RELAY_ID_KEY = "relay_id";
constexpr const char *MQTT_SPRAY_DAY_KEY = "spray_day_s";
constexpr const char *MQTT_COOLDOWN_DAY_KEY = "cooldown_day_s";
constexpr const char *MQTT_SPRAY_NIGHT_KEY = "spray_night_s";
constexpr const char *MQTT_COOLDOWN_NIGHT_KEY = "cooldown_night_s";
constexpr const char *MQTT_OVERRIDE_ACTION_KEY = "action";
constexpr const char *MQTT_OVERRIDE_STATE_KEY = "state";
constexpr const char *MQTT_OVERRIDE_DURATION_KEY = "duration_s";
constexpr const char *MQTT_ACTION_START = "START";
constexpr const char *MQTT_ACTION_CANCEL = "CANCEL";
constexpr const char *MQTT_ACTION_CLEAR = "CLEAR";
constexpr const char *MQTT_ACTION_ON = "on";
constexpr const char *MQTT_ACTION_OFF = "off";
constexpr const char *MQTT_STATE_ON = "ON";
constexpr const char *MQTT_STATE_OFF = "OFF";

// ============================================================================
// SECTION 8: Flow Sensing, Calibration & Hydraulic Safety FSM
// ============================================================================
// Approved flow sensor operating range: 0.00 to 6.00 L/min
constexpr uint16_t FLOW_SENSOR_MAX_LPM_X100 = 600;
constexpr uint32_t FLOW_PULSES_PER_LITRE_NOMINAL = 4450; // OF06ZAT standard K-factor
constexpr uint16_t FLOW_LOW_CUTOFF_LPM_X100 = 15;        // Cutoff: 0.15 L/min
constexpr uint16_t FLOW_MAX_LIMIT_LPM_X100 = 600;        // Over-range: 6.00 L/min
constexpr uint32_t FLOW_MIN_PULSE_INTERVAL_US = 500;     // Hardware debounce refractory window (max 2000 Hz)
constexpr uint32_t FLOW_STALE_TIMEOUT_MS = 3000;         // Timeout for zero pulses when ON
constexpr uint32_t FLOW_MAX_SAMPLE_WINDOW_MS = 10000;    // Max window before re-sync

// Calibration Engine Limits & Quantitative Quality Gate Thresholds (SPEC-FLOW-CAL-001)
constexpr size_t MAX_CALIBRATION_POINTS = 5;
constexpr size_t MAX_CALIBRATION_TRIALS = 10;
constexpr size_t MIN_CALIBRATION_TRIALS = 3;
constexpr size_t MAX_CALIBRATION_HISTORY_PER_NODE = 4;
constexpr size_t MAX_SUPPORTED_NODES = MAX_NODES + 1;
constexpr size_t MAX_EVALUATOR_NODES = MAX_NODES;
constexpr size_t SENSOR_SERIAL_MAX_LEN = 16;
constexpr size_t OPERATOR_ID_MAX_LEN = 16;
constexpr size_t AUDIT_HASH_HEX_LEN = 65;
constexpr size_t FLOW_AUDIT_STRING_MAX_LEN = 96;

constexpr uint16_t MAX_ACCEPTABLE_REPEATABILITY_PCT_X100 = 150;  // E_rep <= 1.50% (150 in x100)
constexpr uint16_t MAX_ACCEPTABLE_ACCURACY_ERROR_PCT_X100 = 200; // E_acc <= 2.00% (200 in x100)
constexpr uint16_t MIN_ACCEPTABLE_LINEARITY_R2_X10000 = 9900;    // R^2 >= 0.9900 (9900 in x10000)
constexpr uint32_t MAX_ACCEPTABLE_ZERO_LEAK_PULSES_60S = 1;      // <= 1 pulse in 60s at zero flow

// Multi-Tier Pump Feedback & Electrical Safety Default Thresholds
constexpr uint32_t FEEDBACK_INRUSH_BLANKING_DEFAULT_MS = 80;
constexpr uint32_t FEEDBACK_DRIVER_MISMATCH_TIMEOUT_DEFAULT_MS = 30;
constexpr uint32_t FEEDBACK_OPEN_LOAD_TIMEOUT_DEFAULT_MS = 150;
constexpr uint32_t FEEDBACK_OVERCURRENT_DEBOUNCE_DEFAULT_MS = 50;
constexpr uint32_t FEEDBACK_FLOW_CONFIRM_TIMEOUT_DEFAULT_MS = 3000;
constexpr uint32_t FEEDBACK_DRY_RUN_TIMEOUT_DEFAULT_MS = 3000;

constexpr uint16_t FEEDBACK_CURRENT_LEAKAGE_OFF_MAX_MA = 50;
constexpr uint16_t FEEDBACK_CURRENT_OPEN_LOAD_MIN_MA = 150;
constexpr uint16_t FEEDBACK_CURRENT_DRY_RUN_MAX_MA = 1200;
constexpr uint16_t FEEDBACK_CURRENT_NOMINAL_MIN_MA = 1600;
constexpr uint16_t FEEDBACK_CURRENT_NOMINAL_MAX_MA = 2600;
constexpr uint16_t FEEDBACK_CURRENT_STALL_OVERCURRENT_MA = 3800;

constexpr float FEEDBACK_FLOW_LEAKAGE_MAX_LPM = 0.2f;
constexpr float FEEDBACK_FLOW_CONFIRMED_MIN_LPM = 0.5f;
constexpr float FEEDBACK_FLOW_OVER_RANGE_MAX_LPM = 6.5f;

// ============================================================================
// SECTION 9: Agricultural Treatment & Node Scheduling
// ============================================================================
constexpr uint8_t MAX_TIMER_GROUPS = 4;
constexpr uint8_t UNASSIGNED_GROUP_ID = 0;

// Schedule Default Durations (Production & Prototype SSOT)
constexpr uint32_t GROUP_DEFAULT_SPRAY_DAY_S = 30;
constexpr uint32_t GROUP_DEFAULT_COOLDOWN_DAY_S = 300;
constexpr uint32_t GROUP_DEFAULT_SPRAY_NIGHT_S = 30;
constexpr uint32_t GROUP_DEFAULT_COOLDOWN_NIGHT_S = 600;

constexpr uint32_t GROUP_MIN_SPRAY_DURATION_S = 5;
constexpr uint32_t GROUP_MAX_SPRAY_DURATION_S = 300;
constexpr uint32_t GROUP_MIN_COOLDOWN_DURATION_S = 30;
constexpr uint32_t GROUP_MAX_COOLDOWN_DURATION_S = 7200;

constexpr uint32_t GROUP_MIN_OVERRIDE_DURATION_S = 1;
constexpr uint32_t GROUP_MAX_OVERRIDE_DURATION_S = 3600;

// Compatibility aliases for legacy relay scheduler
constexpr uint32_t DEFAULT_SPRAY_DAY_S = GROUP_DEFAULT_SPRAY_DAY_S;
constexpr uint32_t DEFAULT_COOLDOWN_DAY_S = GROUP_DEFAULT_COOLDOWN_DAY_S;
constexpr uint32_t DEFAULT_SPRAY_NIGHT_S = GROUP_DEFAULT_SPRAY_NIGHT_S;
constexpr uint32_t DEFAULT_COOLDOWN_NIGHT_S = GROUP_DEFAULT_COOLDOWN_NIGHT_S;

constexpr uint32_t MIN_SPRAY_DURATION_S = GROUP_MIN_SPRAY_DURATION_S;
constexpr uint32_t MAX_SPRAY_DURATION_S = GROUP_MAX_SPRAY_DURATION_S;
constexpr uint32_t MIN_COOLDOWN_DURATION_S = GROUP_MIN_COOLDOWN_DURATION_S;
constexpr uint32_t MAX_COOLDOWN_DURATION_S = GROUP_MAX_COOLDOWN_DURATION_S;

constexpr uint32_t MIN_OVERRIDE_DURATION_S = GROUP_MIN_OVERRIDE_DURATION_S;
constexpr uint32_t MAX_OVERRIDE_DURATION_S = GROUP_MAX_OVERRIDE_DURATION_S;

// ============================================================================
// SECTION 10: Persistent Storage (NVS Namespaces & Partition Keys)
// ============================================================================
constexpr char DEFAULT_NVS_NAMESPACE[] = "aeroponics";
constexpr char WIFI_NVS_NAMESPACE[] = "wifi_store";
constexpr char RF_NVS_NAMESPACE[] = "rf_config";
constexpr char TREATMENT_NVS_NAMESPACE[] = "aero_treatment";

constexpr char WIFI_NVS_BLOB_KEY[] = "wifi_blob";
constexpr char RF_NVS_PSK_WORD_KEYS[][11] = {"psk_word_0", "psk_word_1", "psk_word_2", "psk_word_3"};
constexpr char RF_NVS_BOOT_SESSION_KEY[] = "boot_session";
constexpr char RF_NVS_UART_NUM_KEY[] = "uart_num";
constexpr char RF_NVS_UART_TX_PIN_KEY[] = "uart_tx_pin";
constexpr char RF_NVS_UART_RX_PIN_KEY[] = "uart_rx_pin";
constexpr char RF_NVS_UART_BAUD_KEY[] = "uart_baud";
constexpr char RF_NVS_UART_M0_PIN_KEY[] = "uart_m0_pin";
constexpr char RF_NVS_UART_M1_PIN_KEY[] = "uart_m1_pin";
constexpr char RF_NVS_UART_AUX_PIN_KEY[] = "uart_aux_pin";

// Treatment Manager NVS Keys
constexpr char NVS_KEY_TR_ID[] = "tr_id";
constexpr char NVS_KEY_TR_VID[] = "tr_vid";
constexpr char NVS_KEY_TR_CVER[] = "tr_cver";
constexpr char NVS_KEY_TR_SP_D[] = "tr_sp_d";
constexpr char NVS_KEY_TR_CD_D[] = "tr_cd_d";
constexpr char NVS_KEY_TR_SP_N[] = "tr_sp_n";
constexpr char NVS_KEY_TR_CD_N[] = "tr_cd_n";
constexpr char NVS_KEY_TR_PUB[] = "tr_pub";
constexpr char NVS_KEY_TR_CRC[] = "tr_crc";

// ============================================================================
// SECTION 11: Network & Broker Credentials (Secrets Integration)
// ============================================================================
#if __has_include("secrets.h")
#include "secrets.h"
#endif

#if __has_include("config_secret.h")
#include "config_secret.h"
#endif

#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif

#ifndef WIFI_PASS
#define WIFI_PASS ""
#endif

#ifndef MQTT_HOST
#define MQTT_HOST "192.168.0.100"
#endif

#ifndef MQTT_PORT
#define MQTT_PORT 10883
#endif

#ifndef MQTT_USER
#define MQTT_USER "esp32_device"
#endif

#ifndef MQTT_PASS
#define MQTT_PASS "123456"
#endif

#ifndef MQTT_DEVICE_ID
#define MQTT_DEVICE_ID "esp32_device"
#endif

// ============================================================================
// SECTION 12: Compile-Time Invariant Verifications (static_assert)
// ============================================================================
static_assert(SERIAL_COMMAND_BUFFER_SIZE > 1,
              "Serial command buffer must reserve one byte for the terminator");
static_assert(MQTT_BUFFER_SIZE >= 1024, "MQTT_BUFFER_SIZE too small");
static_assert(MQTT_DEVICE_ID_MAX_LENGTH < MQTT_CLIENT_ID_BUFFER_SIZE,
              "MQTT client ID buffer must accommodate the provisioned device ID");
static_assert(MQTT_RECONNECT_MAX_S >= MQTT_RECONNECT_BASE_S * 2, "Backoff config invalid");
static_assert(WDT_TIMEOUT_MS > RF_FEEDBACK_DEADLINE_MS,
              "Watchdog timeout must exceed RF feedback deadline to avoid false reset");
static_assert(MQTT_TASK_PRIORITY > WIFI_TASK_PRIORITY,
              "MQTT task must have higher priority than Wi-Fi control task");
static_assert(RF_PRODUCTION_MAX_NODE_ID <= RF_MAX_NODE_ID,
              "Production max nodes cannot exceed protocol address capacity");
static_assert(GROUP_MAX_SPRAY_DURATION_S <= DEFAULT_MAX_ON_DURATION_MS / 1000U,
              "Max spray duration must not exceed max physical ON safety cap");
// HC-12 module baud rate (separate from RF_UART_DEFAULT_BAUD_RATE which is 38400)
constexpr uint32_t RF_UART_HC12_BAUD_RATE = 9600;

// FreeRTOS Core pinning for UART RX ISR + consumer task
constexpr BaseType_t RF_UART_RX_TASK_CORE = 1;  // Core 1: RF/Application core
constexpr UBaseType_t RF_UART_RX_TASK_PRIORITY = 4;  // Above mqtt_task (3)
constexpr uint32_t RF_UART_RX_TASK_STACK_SIZE = 4096;
constexpr const char* RF_UART_RX_TASK_NAME = "rf_uart_rx_task";

// Bounded ring buffer anti-overrun
constexpr size_t RF_UART_RING_BUFFER_SIZE = 512;  // bytes, power-of-2 preferred
constexpr size_t RF_UART_RX_QUEUE_DEPTH = 64;     // FreeRTOS queue depth for ISR→task

// Compile-time invariant verifications (static_assert)
static_assert(RF_UART_RING_BUFFER_SIZE >= 256,
              "RF UART ring buffer must hold at least one full AGU burst response");
static_assert(RF_UART_RX_TASK_PRIORITY > MQTT_TASK_PRIORITY,
              "UART RX task must have higher priority than MQTT task to prevent overrun");

// ============================================================================
// SECTION 13: Virtual FSM Safety Timers & Evidence Pipeline Constants
// ============================================================================
// Flow settle timeout: max wait after RF_ACK for flow evidence (S2-TIMER-04)
constexpr uint32_t T_FLOW_SETTLE_MS = 2500;

// Cooldown minimum: min pause between consecutive ON commands (S2-TIMER-05)
constexpr uint32_t T_COOLDOWN_MIN_MS = 60000;  // 60 seconds

// Polling interval for opcode 0x0E per node (S2-TIMER-04)
constexpr uint32_t T_POLL_0x0E_MS = 1000;  // 1 second

// Deadman lease bounds (from interface-wire-contract S3.3, S2-TIMER-03)
constexpr uint32_t RUN_LEASE_MIN_MS = 1000;          // 1 second minimum
constexpr uint32_t RUN_LEASE_MAX_MS = 300000;        // 5 minutes maximum
constexpr uint32_t DEFAULT_DEADMAN_LEASE_MS = 60000; // 60 seconds default

// Command correlation table bounds (S2-TABLE-06)
constexpr size_t COMMAND_TABLE_MAX_ENTRIES = 16;
constexpr uint32_t COMMAND_TABLE_TTL_MS = 2000;  // 2 seconds TTL cleanup

// Evidence pipeline timing
constexpr uint32_t AGU_ACK_TIMEOUT_MS = AGU_LEGACY_ACK_TIMEOUT_MS;     // 300ms
constexpr uint32_t GATE_FEEDBACK_TIMEOUT_MS = 1000;  // Max wait for gate feedback
constexpr uint32_t CURRENT_DETECT_TIMEOUT_MS = 500;   // Max wait for current

// Flow thresholds (re-aliased for FSM context, from Section 8)
constexpr uint16_t FSM_FLOW_CONFIRMED_MIN_LPM_X100 = 50;  // 0.50 L/min
constexpr uint16_t FSM_FLOW_LEAKAGE_MAX_LPM_X100 = 20;    // 0.20 L/min

// Compile-time invariants for FSM safety timers
static_assert(RUN_LEASE_MIN_MS >= 1000,
              "Minimum lease must be >= 1 second (S2-TIMER-03)");
static_assert(RUN_LEASE_MAX_MS <= 300000,
              "Maximum lease must be <= 5 minutes (S2-TIMER-03)");
static_assert(T_FLOW_SETTLE_MS >= 1000,
              "Flow settle must be >= 1 second (S2-TIMER-04)");
static_assert(T_COOLDOWN_MIN_MS >= 30000,
              "Cooldown minimum must be >= 30 seconds (S2-TIMER-05)");
static_assert(COMMAND_TABLE_MAX_ENTRIES <= 32,
              "Command table bounded to 32 entries max (S2-TABLE-06)");

// Legacy 4-Relay Prototype Hardware Pinouts
