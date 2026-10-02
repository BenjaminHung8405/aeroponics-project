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
// Hardware I2C for RTC DS3231 / DS1307 (Dedicated I2C bus on GPIO 21/47)
// Isolates I2C from high-speed SPI on GPIO 12/13 to prevent bus lockup.
// GPIO 21 (SDA) and GPIO 47 (SCL) sit adjacent on right header of ESP32-S3 DevKitC-1.
constexpr uint8_t RTC_SDA_PIN = 21;
constexpr uint8_t RTC_SCL_PIN = 47;
constexpr uint8_t RTC_I2C_ADDRESS = 0x68; // Fixed DS1307/DS3231 control-register address
// Verify 4.7k pull-up resistors to 3.3V on SDA/SCL lines.

// Hardware SPI Interface for 2.4" TFT Display (ST7789 / ILI9341 - 320x240)
// Field Diagnostic HMI pinout matching verified smart-farm wiring:
constexpr int8_t TFT_SPI_MOSI_PIN = 13; // SDI / MOSI
constexpr int8_t TFT_SPI_SCLK_PIN = 12; // SCK / CLK (27MHz hardware SPI)
constexpr int8_t TFT_SPI_CS_PIN = 15;   // CS (Chip Select)
constexpr int8_t TFT_SPI_DC_PIN = 4;    // DC / RS (Data/Command)
constexpr int8_t TFT_SPI_RST_PIN = 5;   // RST (Hardware Reset)
constexpr int8_t TFT_SPI_BL_PIN = 2;    // BL / LED (Backlight control)
constexpr int8_t TFT_SPI_MISO_PIN = 14; // SDO / MISO (SPI master input)

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

// ============================================================================
// SECTION 14: AUTONOMOUS DUAL-MODE FALLBACK ENGINE
// ============================================================================
#ifndef AUTONOMOUS_FALLBACK_ENABLED
#define AUTONOMOUS_FALLBACK_ENABLED 0
#endif
#ifndef FALLBACK_LOCK_FROM_MQTT_OVERWRITE
#define FALLBACK_LOCK_FROM_MQTT_OVERWRITE 0
#endif
#define OP_MODE_GROUP 1
#define OP_MODE_NODE 2
#ifndef SELECTED_OPERATION_MODE
#define SELECTED_OPERATION_MODE OP_MODE_NODE
#endif
#ifndef TARGET_ACTIVE_GROUP_ID
#define TARGET_ACTIVE_GROUP_ID 3
#endif
#ifndef TARGET_ACTIVE_NODE_1
#define TARGET_ACTIVE_NODE_1 4
#define TARGET_ACTIVE_NODE_2 5
#define TARGET_ACTIVE_NODE_3 6
#define TARGET_ACTIVE_NODE_4 7
#endif
#ifndef NT1_SPRAY_DAY_S
#define NT1_SPRAY_DAY_S 15
#define NT1_COOLDOWN_DAY_S 600
#define NT1_SPRAY_NIGHT_S 15
#define NT1_COOLDOWN_NIGHT_S 3600
#define NT2_SPRAY_DAY_S 15
#define NT2_COOLDOWN_DAY_S 600
#define NT2_SPRAY_NIGHT_S 15
#define NT2_COOLDOWN_NIGHT_S 3600
#define NT3_SPRAY_DAY_S 10
#define NT3_COOLDOWN_DAY_S 30
#define NT3_SPRAY_NIGHT_S 10
#define NT3_COOLDOWN_NIGHT_S 30
#define NT4_SPRAY_DAY_S 15
#define NT4_COOLDOWN_DAY_S 600
#define NT4_SPRAY_NIGHT_S 15
#define NT4_COOLDOWN_NIGHT_S 3600
#define AUTONOMOUS_NVS_CONFIG_VERSION 6
#endif
static_assert(SELECTED_OPERATION_MODE == OP_MODE_GROUP || SELECTED_OPERATION_MODE == OP_MODE_NODE,
              "Invalid autonomous operation mode");
static_assert(TARGET_ACTIVE_GROUP_ID >= 1 && TARGET_ACTIVE_GROUP_ID <= 4,
              "Target group must be 1..4");

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
// Modern RF node addresses occupy 0x01..0x0F. The gateway is 0x00.
constexpr uint8_t RF_MAX_NODE_ID = 0x0F;
constexpr uint8_t AGU_LEGACY_MIN_NODE_ID = 1;
constexpr uint8_t AGU_LEGACY_MAX_NODE_ID = 15;
constexpr uint8_t RF_PRODUCTION_MIN_NODE_ID = RF_MIN_NODE_ID;
constexpr uint8_t RF_PRODUCTION_MAX_NODE_ID = RF_MAX_NODE_ID;
constexpr uint8_t PRODUCTION_NODE_COUNT =
    RF_PRODUCTION_MAX_NODE_ID - RF_PRODUCTION_MIN_NODE_ID + 1;
constexpr uint8_t RF_MAX_PROTOCOL_NODE_ID = RF_MAX_NODE_ID;
constexpr uint8_t MAX_NODES = RF_MAX_NODE_ID;
constexpr uint8_t PRODUCTION_MAX_NODES = PRODUCTION_NODE_COUNT;

inline bool isValidNodeId(uint8_t node_id)
{
    return node_id >= RF_MIN_NODE_ID && node_id <= RF_MAX_NODE_ID;
}

// Compatibility name retained for existing modern firmware call sites.
inline bool isProductionNodeId(uint8_t node_id) { return isValidNodeId(node_id); }

constexpr uint8_t RF_GROUP_ADDRESS_1 = 0x10;
constexpr uint8_t RF_GROUP_ADDRESS_2 = 0x14;
constexpr uint8_t RF_GROUP_ADDRESS_3 = 0x18;
constexpr uint8_t RF_GROUP_ADDRESS_4 = 0x1C;

inline bool isValidRfGroupAddress(uint8_t address)
{
    return address == RF_GROUP_ADDRESS_1 || address == RF_GROUP_ADDRESS_2 ||
           address == RF_GROUP_ADDRESS_3 || address == RF_GROUP_ADDRESS_4;
}

inline uint8_t rfGroupIdFromLogical(uint8_t logical_group)
{
    switch (logical_group)
    {
    case 1:
        return RF_GROUP_ADDRESS_1; // 0x10
    case 2:
        return RF_GROUP_ADDRESS_2; // 0x14
    case 3:
        return RF_GROUP_ADDRESS_3; // 0x18
    case 4:
        return RF_GROUP_ADDRESS_4; // 0x1C
    default:
        return 0;
    }
}

inline void getGroupMemberNodes(uint8_t logical_group, uint8_t &min_node, uint8_t &max_node)
{
    switch (logical_group)
    {
    case 1:
        min_node = 1;
        max_node = 3;
        break;
    case 2:
        min_node = 4;
        max_node = 7;
        break;
    case 3:
        min_node = 8;
        max_node = 11;
        break;
    case 4:
        min_node = 12;
        max_node = 15;
        break;
    default:
        min_node = 0;
        max_node = 0;
        break;
    }
}

inline bool isValidSourceAddress(uint8_t address)
{
    return address == RF_GATEWAY_NODE_ID || isValidNodeId(address);
}

inline bool isValidTargetAddress(uint8_t address)
{
    return address == RF_GATEWAY_NODE_ID || isValidNodeId(address);
}

// AGU legacy SCI topology. These are physical RF addresses, not logical
// actuator slots. The legacy client firmware accepts only these four IDs.
constexpr uint32_t AGU_LEGACY_ACK_TIMEOUT_MS = 300;
constexpr uint8_t AGU_LEGACY_MAX_ATTEMPTS = 3;
constexpr uint32_t AGU_LEGACY_RETRY_GUARD_MS = 50;
constexpr uint32_t RF_RADIO_SILENCE_BEFORE_PHASE_MS = 1000;
constexpr uint32_t RF_RADIO_SILENCE_AFTER_PHASE_MS = 1000;
constexpr uint8_t RF_PUMP_OFF_BURST_COUNT = 3;
constexpr uint32_t RF_PUMP_OFF_BURST_GAP_MS = 30;
constexpr uint32_t RF_UNICAST_STAGGER_MS = 300;
static_assert(RF_PUMP_OFF_BURST_GAP_MS >= 25 && RF_PUMP_OFF_BURST_GAP_MS <= 35,
              "Pump OFF burst gap must remain within 25-35 ms");

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
// Periodic NTP re-sync cadence. The DS1307 crystal drifts roughly +/-20 s/day
// vs. the DS3231's +/-2 ppm, so a bounded re-sync window keeps wall-clock and
// RTC reference aligned without hammering the SNTP client.
constexpr uint32_t NTP_RESYNC_INTERVAL_MS = 6UL * 60UL * 60UL * 1000UL; // 6 hours
// After a backend GATEWAY_CLOCK push, suppress the NTP override for one full
// interval so the authoritative time is not immediately re-driven by SNTP.
constexpr uint32_t CLOCK_BACKEND_SUPPRESS_NTP_MS = NTP_RESYNC_INTERVAL_MS;
// Plausibility bounds for an NVS-persisted backend timestamp: accept only
// epochs after a sane modern floor and within a bounded horizon from boot.
constexpr int64_t CLOCK_UNIX_TIME_MIN_VALID = 1600000000LL; // 2020-09-13T00:00:00Z
constexpr int64_t CLOCK_UNIX_TIME_MAX_VALID = 4102444800LL; // 2100-01-01T00:00:00Z

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
// Schedule state contains four complete group profiles and must remain fully
// static/non-fragmenting on the gateway.
constexpr size_t MQTT_TELEMETRY_PAYLOAD_SIZE = 2048;
constexpr size_t MQTT_TOPIC_BUFFER_SIZE = 192;
constexpr uint32_t MQTT_SCHEDULE_STATE_PERIOD_MS = 60000;
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
constexpr const char *MQTT_COMMAND_GROUP_STATE_SUFFIX = "/command/config/group-state";
constexpr const char *MQTT_COMMAND_FLOW_POLICY_SUFFIX = "/command/config/flow-policy";
constexpr const char *MQTT_COMMAND_NODE_OVERRIDE_SUFFIX = "/command/node/";
constexpr const char *MQTT_COMMAND_GROUP_CONTROL_SUFFIX = "/command/group/";
constexpr const char *MQTT_COMMAND_GATEWAY_SUFFIX = "/command/gateway/";
// Authoritative backend time-set downlink. Published by the backend clock sync
// service to aeroponics/device/{device_id}/command/config/clock.
constexpr const char *MQTT_COMMAND_CLOCK_SUFFIX = "/command/config/clock";
// Retained config downlink: full 4-slot → node/group mapping pushed by backend on every slot update.
// Topic: aeroponics/device/<device_id>/config/control_slots
// Payload: { "slots": [{ "idx": 1, "type": "NODE", "id": 4 }, ...] }
constexpr const char *MQTT_CONFIG_CONTROL_SLOTS_SUFFIX = "/config/control_slots";
constexpr const char *MQTT_SCHEDULE_STATE_SUFFIX = "/schedule/state";
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

// Clock / timekeeping persistence. Holds the last backend-provided epoch and
// the UTC offset that was in force, so a gateway that boots with no Wi-Fi and
// an unpowered DS1307 still has a plausible (if stale) reference instead of
// falling straight through to the invalid -> safe-OFF path.
constexpr char CLOCK_NVS_NAMESPACE[] = "aero_clock";
constexpr char NVS_KEY_CLOCK_UNIX[] = "clk_unix";
constexpr char NVS_KEY_CLOCK_TZ_OFFSET[] = "clk_tzoff";
constexpr char NVS_KEY_CLOCK_MAGIC[] = "clk_magic";
// Bumped whenever the persisted clock record layout or trust rules change.
constexpr uint32_t CLOCK_NVS_RECORD_VERSION = 1;

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

// Group Schedule & Node Assignment Persistence Keys (Namespace: "aeroponics")
constexpr char NVS_KEY_GRP_PREFIX[] = "tr_grp";       // tr_grp1 .. tr_grp4
constexpr char NVS_KEY_NODE_ASSIGN[] = "node_assign"; // 15-node mapping table
constexpr uint16_t PERSISTENT_RECORD_MAGIC = 0xA3F1;

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
#define MQTT_DEVICE_ID "AUTO"
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
// HC-12 module baud rate (configured to 38400 for production hardware)
constexpr uint32_t RF_UART_HC12_BAUD_RATE = 38400;

// FreeRTOS Core pinning for UART RX ISR + consumer task
constexpr BaseType_t RF_UART_RX_TASK_CORE = 1;      // Core 1: RF/Application core
constexpr UBaseType_t RF_UART_RX_TASK_PRIORITY = 4; // Above mqtt_task (3)
constexpr uint32_t RF_UART_RX_TASK_STACK_SIZE = 4096;
constexpr const char *RF_UART_RX_TASK_NAME = "rf_uart_rx_task";

// Bounded ring buffer anti-overrun
constexpr size_t RF_UART_RING_BUFFER_SIZE = 512; // bytes, power-of-2 preferred
constexpr size_t RF_UART_RX_QUEUE_DEPTH = 64;    // FreeRTOS queue depth for ISR→task

// Compile-time invariant verifications (static_assert)
static_assert(RF_UART_RING_BUFFER_SIZE >= 256,
              "RF UART ring buffer must hold at least one full AGU burst response");
static_assert(RF_UART_RX_TASK_PRIORITY > MQTT_TASK_PRIORITY,
              "UART RX task must have higher priority than MQTT task to prevent overrun");

// ============================================================================
// SECTION 13: Virtual FSM Safety Timers & Evidence Pipeline Constants
// ============================================================================
// Flow sensor hardware configuration: set to false when physical flow sensors are removed
constexpr bool HARDWARE_FLOW_SENSOR_PRESENT = false;

// Staggered actuation & deterministic retry configuration for multi-node groups
constexpr uint32_t STAGGER_DISPATCH_INTERVAL_MS = 300; // 300ms gap between consecutive node ON dispatches
constexpr uint8_t PUMP_OFF_MAX_RETRIES = 2;            // Up to 2 retries if 0x07 is not ACKed
constexpr uint32_t PUMP_OFF_RETRY_INTERVAL_MS = 100;   // 100ms backoff between PUMP_OFF retries

// Flow settle timeout: max wait after RF_ACK for flow evidence (S2-TIMER-04)
constexpr uint32_t T_FLOW_SETTLE_MS = 2500;

// Cooldown minimum: min pause between consecutive ON commands (S2-TIMER-05)
constexpr uint32_t T_COOLDOWN_MIN_MS = 10000; // 10 seconds

// Polling interval for opcode 0x0E per node (S2-TIMER-04)
constexpr uint32_t T_POLL_0x0E_MS = 1000; // 1 second

// Deadman lease bounds (from interface-wire-contract S3.3, S2-TIMER-03)
constexpr uint32_t RUN_LEASE_MIN_MS = 1000;          // 1 second minimum
constexpr uint32_t RUN_LEASE_MAX_MS = 300000;        // 5 minutes maximum
constexpr uint32_t DEFAULT_DEADMAN_LEASE_MS = 60000; // 60 seconds default

// Command correlation table bounds (S2-TABLE-06)
constexpr size_t COMMAND_TABLE_MAX_ENTRIES = 16;
constexpr uint32_t COMMAND_TABLE_TTL_MS = 2000; // 2 seconds TTL cleanup

// Evidence pipeline timing
constexpr uint32_t AGU_ACK_TIMEOUT_MS = AGU_LEGACY_ACK_TIMEOUT_MS; // 300ms
constexpr uint32_t GATE_FEEDBACK_TIMEOUT_MS = 1000;                // Max wait for gate feedback
constexpr uint32_t CURRENT_DETECT_TIMEOUT_MS = 500;                // Max wait for current

// Flow thresholds (re-aliased for FSM context, from Section 8)
constexpr uint16_t FSM_FLOW_CONFIRMED_MIN_LPM_X100 = 50; // 0.50 L/min
constexpr uint16_t FSM_FLOW_LEAKAGE_MAX_LPM_X100 = 20;   // 0.20 L/min

// Compile-time invariants for FSM safety timers
static_assert(RUN_LEASE_MIN_MS >= 1000,
              "Minimum lease must be >= 1 second (S2-TIMER-03)");
static_assert(RUN_LEASE_MAX_MS <= 300000,
              "Maximum lease must be <= 5 minutes (S2-TIMER-03)");
static_assert(T_FLOW_SETTLE_MS >= 1000,
              "Flow settle must be >= 1 second (S2-TIMER-04)");
static_assert(T_COOLDOWN_MIN_MS >= 10000,
              "Cooldown minimum must be >= 10 seconds (S2-TIMER-05)");
static_assert(COMMAND_TABLE_MAX_ENTRIES <= 32,
              "Command table bounded to 32 entries max (S2-TABLE-06)");

// Legacy 4-Relay Prototype Hardware Pinouts
