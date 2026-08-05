#include "mqtt_config_provider.h"
#include <cstring>
#include <cstdio>

#if defined(ESP_PLATFORM) || defined(ARDUINO)
#include <esp_log.h>
#define LOG_E(...) ESP_LOGE("MQTT_CFG", __VA_ARGS__)
#define LOG_I(...) ESP_LOGI("MQTT_CFG", __VA_ARGS__)
#else
#define LOG_E(...) do {} while (false)
#define LOG_I(...) do {} while (false)
#endif

// Git-ignored secrets headers or fallback definitions
#if __has_include("secrets.h")
#include "secrets.h"
#endif

#if __has_include("config_secret.h")
#include "config_secret.h"
#endif

#ifndef MQTT_HOST
#define MQTT_HOST ""
#endif

#ifndef MQTT_PORT
#define MQTT_PORT 1883
#endif

#ifndef MQTT_USER
#define MQTT_USER ""
#endif

#ifndef MQTT_PASS
#define MQTT_PASS ""
#endif

#ifndef MQTT_DEVICE_ID
#define MQTT_DEVICE_ID ""
#endif

namespace {
    static char s_broker_host[128] = {0};
    static uint16_t s_broker_port = MQTT_PORT;
    static char s_username[64] = {0};
    static char s_password[64] = {0};
    static char s_device_id[64] = {0};
}

MqttConfig MqttConfigProvider::load(NvsStorage* nvs) {
    (void)nvs; // Reserved for potential NVS key reads if provisioned

    // 1. Initialize static buffers from git-ignored secrets/defines
    snprintf(s_broker_host, sizeof(s_broker_host), "%s", MQTT_HOST);
    s_broker_port = static_cast<uint16_t>(MQTT_PORT);
    snprintf(s_username, sizeof(s_username), "%s", MQTT_USER);
    snprintf(s_password, sizeof(s_password), "%s", MQTT_PASS);
    snprintf(s_device_id, sizeof(s_device_id), "%s", MQTT_DEVICE_ID);

    // Fallback default device ID if not specified
    if (strlen(s_device_id) == 0) {
        snprintf(s_device_id, sizeof(s_device_id), "esp32-01");
    }

    // 2. Anti-debt check: Log ERROR if broker_host is empty
    if (strlen(s_broker_host) == 0) {
        LOG_E("MQTT broker_host is empty! MQTT client cannot establish connection until host is provisioned.");
    } else {
        LOG_I("Loaded MQTT config for broker: %s:%u (device_id: %s)", s_broker_host, s_broker_port, s_device_id);
    }

    MqttConfig config;
    config.broker_host = s_broker_host;
    config.broker_port = s_broker_port;
    config.username = s_username;
    config.password = s_password;
    config.device_id = s_device_id;

    return config;
}
