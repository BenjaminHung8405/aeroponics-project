#pragma once

#include <cstddef>
#include <cstdint>

#if defined(ESP_PLATFORM) || defined(ARDUINO)
#include <freertos/FreeRTOS.h>
#else
using TickType_t = uint32_t;
using UBaseType_t = uint32_t;
using BaseType_t = int32_t;
#endif

// ============================================================================
// Aeroponics Lean Firmware — Single Source of Truth Production Configuration
// ============================================================================

// ----------------------------------------------------------------------------
// Hardware Pinout Definitions
// ----------------------------------------------------------------------------
constexpr uint8_t RTC_SDA_PIN = 21;
constexpr uint8_t RTC_SCL_PIN = 22;

// TODO: confirm with hardware
// constexpr uint8_t LED_STATUS_PIN = 13;

// ----------------------------------------------------------------------------
// RF UART Interface (Separate from USB Debug Serial)
// ----------------------------------------------------------------------------
// Candidate hardware defaults for Sprint 1.5 POC (HC-12 / EBYTE E32)
#ifndef CONFIG_RF_UART_NUM
#define CONFIG_RF_UART_NUM 1
#endif
#ifndef CONFIG_RF_UART_TX_PIN
#define CONFIG_RF_UART_TX_PIN 17
#endif
#ifndef CONFIG_RF_UART_RX_PIN
#define CONFIG_RF_UART_RX_PIN 16
#endif
#ifndef CONFIG_RF_UART_BAUD_RATE
#define CONFIG_RF_UART_BAUD_RATE 9600
#endif

// ----------------------------------------------------------------------------
// FreeRTOS Task, Watchdog & RF Safety Policy Constants
// ----------------------------------------------------------------------------
constexpr uint32_t WDT_TIMEOUT_S = 30;
constexpr uint32_t WDT_TIMEOUT_MS = WDT_TIMEOUT_S * 1000U;

constexpr uint32_t DEFAULT_RUN_LEASE_MS = 60000;         // 60-second lease
constexpr uint32_t DEFAULT_MAX_ON_DURATION_MS = 300000;  // 5-minute max safety cap


// ----------------------------------------------------------------------------
// Time & NTP Network Sync Configurations
// ----------------------------------------------------------------------------
constexpr uint8_t DAY_START_HOUR = 6;     // 06:00
constexpr uint8_t NIGHT_START_HOUR = 18;   // 18:00

constexpr int32_t TIMEZONE_OFFSET_S = 25200; // UTC+7 (7 * 3600 seconds)
constexpr int32_t DAYLIGHT_OFFSET_S = 0;
constexpr const char* NTP_SERVER_PRIMARY = "pool.ntp.org";
constexpr uint32_t NTP_POLL_INTERVAL_MS = 500;
constexpr uint32_t NTP_SYNC_TIMEOUT_MS = 10000;
constexpr uint32_t SYSTEM_TIME_READ_TIMEOUT_MS = 10;
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 30000;
constexpr uint32_t WIFI_CONNECT_POLL_INTERVAL_MS = 500;
constexpr uint32_t WIFI_RECONNECT_CHECK_INTERVAL_MS = 60000;

// ----------------------------------------------------------------------------
// Main Loop & Serial Service Timing/Work Budgets
// ----------------------------------------------------------------------------
constexpr uint32_t SERIAL_BAUD_RATE = 115200;
constexpr uint32_t BOOT_FAILURE_SAFE_STATE_INTERVAL_MS = 1000;
constexpr size_t MAX_SERIAL_BYTES_PER_TICK = 64;
constexpr size_t SERIAL_COMMAND_BUFFER_SIZE = 128;

static_assert(SERIAL_COMMAND_BUFFER_SIZE > 1,
              "Serial command buffer must reserve one byte for the terminator");

// ----------------------------------------------------------------------------
// MQTT Client & Task Configuration Constants (Production Gateway Domain SSOT)
// ----------------------------------------------------------------------------
constexpr uint32_t MQTT_HEARTBEAT_INTERVAL_MS = 10000;
constexpr uint32_t MQTT_RECONNECT_BASE_S = 1;
constexpr uint32_t MQTT_RECONNECT_MAX_S = 60;
constexpr size_t MQTT_BUFFER_SIZE = 2048;
constexpr uint16_t MQTT_KEEPALIVE_S = 30;
constexpr size_t MQTT_LWT_DOC_SIZE = 256;
constexpr size_t MQTT_HEARTBEAT_PAYLOAD_SIZE = 512;
constexpr size_t MQTT_TELEMETRY_DOC_SIZE = 512;
constexpr size_t MQTT_TELEMETRY_PAYLOAD_SIZE = 512;
constexpr size_t MQTT_TOPIC_BUFFER_SIZE = 192;
constexpr size_t MQTT_CLIENT_ID_BUFFER_SIZE = 96;
constexpr size_t MQTT_BROKER_HOST_BUFFER_SIZE = 128;
constexpr size_t MQTT_USERNAME_BUFFER_SIZE = 64;
constexpr size_t MQTT_PASSWORD_BUFFER_SIZE = 64;
constexpr size_t MQTT_DEVICE_ID_BUFFER_SIZE = 64;
constexpr size_t MQTT_DEVICE_ID_MAX_LENGTH = MQTT_DEVICE_ID_BUFFER_SIZE - 1;
constexpr const char* MQTT_CLIENT_ID_PREFIX = "aero-";
constexpr uint8_t MQTT_LWT_QOS = 1;
constexpr bool MQTT_LWT_RETAIN = true;
constexpr uint8_t MQTT_COMMAND_QOS = 1;
constexpr bool MQTT_PUBLISH_RETAIN = false;
constexpr const char* MQTT_STATUS_SUFFIX = "/status";

// Gateway Production Domain Suffixes (Sprint 2 / Sprint 1.5 contract)
constexpr const char* MQTT_TELEMETRY_GROUP_SUFFIX = "/telemetry/group/";
constexpr const char* MQTT_TELEMETRY_NODE_SUFFIX = "/telemetry/node/";
constexpr const char* MQTT_COMMAND_TREATMENT_SUFFIX = "/command/config/treatment";
constexpr const char* MQTT_COMMAND_ASSIGNMENT_SUFFIX = "/command/config/assignment";
constexpr const char* MQTT_COMMAND_NODE_OVERRIDE_SUFFIX = "/command/node/";
constexpr const char* MQTT_COMMAND_GROUP_CONTROL_SUFFIX = "/command/group/";
constexpr const char* MQTT_ACK_PREFIX_SUFFIX = "/ack/";

constexpr const char* MQTT_WILDCARD_SINGLE_LEVEL = "+";
constexpr uint32_t MQTT_TASK_TICK_INTERVAL_MS = 100;
constexpr const char* MQTT_TASK_NAME = "mqtt_task";

constexpr uint32_t MQTT_TASK_STACK_SIZE = 8192;
constexpr UBaseType_t MQTT_TASK_PRIORITY = 2;
constexpr BaseType_t MQTT_TASK_CORE = 0;

constexpr size_t MQTT_HEARTBEAT_DOC_SIZE = 512;
constexpr size_t MQTT_COMMAND_DOC_SIZE = 1024;

constexpr const char* MQTT_TOPIC_BASE = "aeroponics/device";

static_assert(MQTT_BUFFER_SIZE >= 1024, "MQTT_BUFFER_SIZE too small");
static_assert(MQTT_DEVICE_ID_MAX_LENGTH < MQTT_CLIENT_ID_BUFFER_SIZE,
              "MQTT client ID buffer must accommodate the provisioned device ID");
static_assert(MQTT_RECONNECT_MAX_S >= MQTT_RECONNECT_BASE_S * 2, "Backoff config invalid");

// ----------------------------------------------------------------------------
// Wi-Fi Credentials Configuration (Git-ignored secrets.h or build environment)
// ----------------------------------------------------------------------------
#if __has_include("secrets.h")
#include "secrets.h"
#endif

#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif

#ifndef WIFI_PASS
#define WIFI_PASS ""
#endif
