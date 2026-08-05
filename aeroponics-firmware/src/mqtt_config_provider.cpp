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
    static char s_broker_host[MQTT_BROKER_HOST_BUFFER_SIZE] = {0};
    static uint16_t s_broker_port = MQTT_PORT;
    static char s_username[MQTT_USERNAME_BUFFER_SIZE] = {0};
    static char s_password[MQTT_PASSWORD_BUFFER_SIZE] = {0};
    static char s_device_id[MQTT_DEVICE_ID_BUFFER_SIZE] = {0};

    bool copyProvisionedValue(char* target, size_t target_size, const char* source) {
        if (!source) return false;
        const int written = snprintf(target, target_size, "%s", source);
        return written >= 0 && static_cast<size_t>(written) < target_size;
    }

    bool isValidDeviceId(const char* value) {
        const size_t length = strnlen(value, MQTT_DEVICE_ID_MAX_LENGTH + 1);
        if (length == 0 || length > MQTT_DEVICE_ID_MAX_LENGTH) return false;
        for (size_t i = 0; i < length; ++i) {
            const char c = value[i];
            if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                  (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
        }
        return true;
    }

    void clearConfig() {
        s_broker_host[0] = '\0';
        s_username[0] = '\0';
        s_password[0] = '\0';
        s_device_id[0] = '\0';
    }
}

MqttConfig MqttConfigProvider::load() {
    s_broker_port = static_cast<uint16_t>(MQTT_PORT);
    if (!copyProvisionedValue(s_broker_host, sizeof(s_broker_host), MQTT_HOST) ||
        !copyProvisionedValue(s_username, sizeof(s_username), MQTT_USER) ||
        !copyProvisionedValue(s_password, sizeof(s_password), MQTT_PASS) ||
        !copyProvisionedValue(s_device_id, sizeof(s_device_id), MQTT_DEVICE_ID) ||
        !isValidDeviceId(s_device_id)) {
        clearConfig();
        LOG_E("Invalid or truncated MQTT provisioning; MQTT remains disabled.");
    }

    if (strlen(s_broker_host) == 0 || strlen(s_device_id) == 0) {
        LOG_E("MQTT broker_host or device_id is missing; MQTT remains disabled.");
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
