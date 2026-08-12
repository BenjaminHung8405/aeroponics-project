#pragma once

#include "config.h"

// Hardware Pinout Definitions (Legacy 4-Relay Prototype)
constexpr uint8_t RELAY_PIN_1 = 1;
constexpr uint8_t RELAY_PIN_2 = 2;
constexpr uint8_t RELAY_PIN_3 = 3;
constexpr uint8_t RELAY_PIN_4 = 4;
constexpr uint8_t TOTAL_RELAYS = 4;

// Schedule Default Configurations (Legacy)
constexpr uint32_t DEFAULT_SPRAY_DAY_S = 30;
constexpr uint32_t DEFAULT_COOLDOWN_DAY_S = 300;
constexpr uint32_t DEFAULT_SPRAY_NIGHT_S = 30;
constexpr uint32_t DEFAULT_COOLDOWN_NIGHT_S = 600;

// NVS & Manual Override Validation Limits (Legacy)
constexpr uint32_t MIN_SPRAY_DURATION_S = 5;
constexpr uint32_t MAX_SPRAY_DURATION_S = 300;
constexpr uint32_t MIN_COOLDOWN_DURATION_S = 30;
constexpr uint32_t MAX_COOLDOWN_DURATION_S = 7200;

constexpr uint32_t MIN_OVERRIDE_DURATION_S = 1;
constexpr uint32_t MAX_OVERRIDE_DURATION_S = 3600;

// FreeRTOS Task & Synchronization Constants (Legacy Relay Scheduler)
constexpr uint32_t RELAY_TASK_STACK_SIZE = 8192;
constexpr UBaseType_t RELAY_TASK_PRIORITY = 3;
constexpr BaseType_t RELAY_TASK_CORE = 1;
constexpr uint32_t RELAY_MUTEX_TIMEOUT_MS = 100;
constexpr uint32_t SCHEDULER_STATE_MUTEX_TIMEOUT_MS = 100;
constexpr uint32_t RELAY_TASK_TICK_INTERVAL_MS = 1000;
constexpr uint32_t RELAY_TASK_STARTUP_TIMEOUT_MS = 1000;
constexpr uint32_t RELAY_TASK_CALLBACK_EXIT_TIMEOUT_MS = WDT_TIMEOUT_MS;

// Legacy Relay MQTT Topics & Tokens
constexpr const char* MQTT_COMMAND_SUFFIX = "/command/relay/";
constexpr const char* MQTT_TELEMETRY_SUFFIX = "/telemetry/relay/";
constexpr const char* MQTT_SCHEDULE_SUFFIX = "/schedule";
constexpr const char* MQTT_OVERRIDE_SUFFIX = "/override";
constexpr const char* MQTT_SCHEDULE_TOKEN = "schedule";
constexpr const char* MQTT_OVERRIDE_TOKEN = "override";
constexpr const char* MQTT_RELAY_ID_KEY = "relay_id";
constexpr const char* MQTT_SPRAY_DAY_KEY = "spray_day_s";
constexpr const char* MQTT_COOLDOWN_DAY_KEY = "cooldown_day_s";
constexpr const char* MQTT_SPRAY_NIGHT_KEY = "spray_night_s";
constexpr const char* MQTT_COOLDOWN_NIGHT_KEY = "cooldown_night_s";
constexpr const char* MQTT_OVERRIDE_ACTION_KEY = "action";
constexpr const char* MQTT_OVERRIDE_STATE_KEY = "state";
constexpr const char* MQTT_OVERRIDE_DURATION_KEY = "duration_s";
constexpr const char* MQTT_ACTION_START = "START";
constexpr const char* MQTT_ACTION_CANCEL = "CANCEL";
constexpr const char* MQTT_ACTION_CLEAR = "CLEAR";
constexpr const char* MQTT_ACTION_ON = "on";
constexpr const char* MQTT_ACTION_OFF = "off";
constexpr const char* MQTT_STATE_ON = "ON";
constexpr const char* MQTT_STATE_OFF = "OFF";
