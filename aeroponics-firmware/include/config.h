#pragma once

#include <cstdint>
#include <freertos/FreeRTOS.h>

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

// ----------------------------------------------------------------------------
// Time & NTP Network Sync Configurations
// ----------------------------------------------------------------------------
constexpr int32_t TIMEZONE_OFFSET_S = 25200; // UTC+7 (7 * 3600 seconds)
constexpr int32_t DAYLIGHT_OFFSET_S = 0;
constexpr const char* NTP_SERVER_PRIMARY = "pool.ntp.org";
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 30000;

// ----------------------------------------------------------------------------
// Optional Wi-Fi Placeholder Credentials (Guarded)
// ----------------------------------------------------------------------------
#ifndef WIFI_SSID
#define WIFI_SSID "CHANGE_ME"
#endif

#ifndef WIFI_PASS
#define WIFI_PASS "CHANGE_ME"
#endif
