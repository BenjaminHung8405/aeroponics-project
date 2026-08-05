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
// Aeroponics Lean Firmware — Single Source of Truth Configuration Constants
// ============================================================================

// ----------------------------------------------------------------------------
// Hardware Pinout Definitions
// ----------------------------------------------------------------------------
constexpr uint8_t RELAY_PIN_1 = 1;
constexpr uint8_t RELAY_PIN_2 = 2;
constexpr uint8_t RELAY_PIN_3 = 3;
constexpr uint8_t RELAY_PIN_4 = 4;

constexpr uint8_t RTC_SDA_PIN = 21;
constexpr uint8_t RTC_SCL_PIN = 22;

// TODO: confirm with hardware
// constexpr uint8_t LED_STATUS_PIN = 13;

constexpr uint8_t TOTAL_RELAYS = 4;

// ----------------------------------------------------------------------------
// Schedule Default Configurations (Seconds & Hours)
// ----------------------------------------------------------------------------
constexpr uint32_t DEFAULT_SPRAY_DAY_S = 30;
constexpr uint32_t DEFAULT_COOLDOWN_DAY_S = 300;
constexpr uint32_t DEFAULT_SPRAY_NIGHT_S = 30;
constexpr uint32_t DEFAULT_COOLDOWN_NIGHT_S = 600;

constexpr uint8_t DAY_START_HOUR = 6;     // 06:00
constexpr uint8_t NIGHT_START_HOUR = 18;   // 18:00

// ----------------------------------------------------------------------------
// NVS & Manual Override Validation Limits
// ----------------------------------------------------------------------------
constexpr uint32_t MIN_SPRAY_DURATION_S = 5;
constexpr uint32_t MAX_SPRAY_DURATION_S = 300;
constexpr uint32_t MIN_COOLDOWN_DURATION_S = 30;
constexpr uint32_t MAX_COOLDOWN_DURATION_S = 7200;

constexpr uint32_t MIN_OVERRIDE_DURATION_S = 1;
constexpr uint32_t MAX_OVERRIDE_DURATION_S = 3600;

// ----------------------------------------------------------------------------
// FreeRTOS Task & Watchdog Constants
// ----------------------------------------------------------------------------
constexpr uint32_t RELAY_TASK_STACK_SIZE = 8192;
constexpr UBaseType_t RELAY_TASK_PRIORITY = 3;
constexpr BaseType_t RELAY_TASK_CORE = 1;
constexpr uint32_t WDT_TIMEOUT_S = 30;
constexpr uint32_t WDT_TIMEOUT_MS = WDT_TIMEOUT_S * 1000U;

// ----------------------------------------------------------------------------
// Time & NTP Network Sync Configurations
// ----------------------------------------------------------------------------
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

// ----------------------------------------------------------------------------
// Synchronization & Scheduler Timing
// ----------------------------------------------------------------------------
constexpr uint32_t RELAY_MUTEX_TIMEOUT_MS = 100;
constexpr uint32_t SCHEDULER_STATE_MUTEX_TIMEOUT_MS = 100;
constexpr uint32_t RELAY_TASK_TICK_INTERVAL_MS = 1000;
constexpr uint32_t RELAY_TASK_STARTUP_TIMEOUT_MS = 1000;
constexpr uint32_t RELAY_TASK_CALLBACK_EXIT_TIMEOUT_MS = WDT_TIMEOUT_MS;

static_assert(SERIAL_COMMAND_BUFFER_SIZE > 1,
              "Serial command buffer must reserve one byte for the terminator");
static_assert(RELAY_TASK_STARTUP_TIMEOUT_MS > 0,
              "Relay task startup timeout must be finite and non-zero");

// ----------------------------------------------------------------------------
// MQTT Client & Task Configuration Constants (SSOT)
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
constexpr const char* MQTT_CLIENT_ID_PREFIX = "aero-";
constexpr uint8_t MQTT_LWT_QOS = 1;
constexpr bool MQTT_LWT_RETAIN = true;
constexpr uint8_t MQTT_COMMAND_QOS = 1;
constexpr uint8_t MQTT_PUBLISH_QOS = 0;
constexpr bool MQTT_PUBLISH_RETAIN = false;
constexpr const char* MQTT_STATUS_SUFFIX = "/status";
constexpr const char* MQTT_COMMAND_SUFFIX = "/command/relay/";
constexpr const char* MQTT_SCHEDULE_SUFFIX = "/schedule";
constexpr const char* MQTT_OVERRIDE_SUFFIX = "/override";
constexpr const char* MQTT_TELEMETRY_SUFFIX = "/telemetry/relay/";
constexpr uint32_t MQTT_TASK_TICK_INTERVAL_MS = 100;
constexpr const char* MQTT_TASK_NAME = "mqtt_task";

constexpr uint32_t MQTT_TASK_STACK_SIZE = 8192;
constexpr UBaseType_t MQTT_TASK_PRIORITY = 2;
constexpr BaseType_t MQTT_TASK_CORE = 0;

constexpr size_t MQTT_HEARTBEAT_DOC_SIZE = 512;
constexpr size_t MQTT_COMMAND_DOC_SIZE = 1024;

constexpr const char* MQTT_TOPIC_BASE = "aeroponics/device";

static_assert(MQTT_BUFFER_SIZE >= 1024, "MQTT_BUFFER_SIZE quá nhỏ");
static_assert(MQTT_RECONNECT_MAX_S >= MQTT_RECONNECT_BASE_S * 2, "Backoff config vô nghĩa");

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
