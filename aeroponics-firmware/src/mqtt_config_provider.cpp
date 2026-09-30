#include "mqtt_config_provider.h"
#include <cstring>
#include <cstdio>

#if defined(ESP_PLATFORM) || defined(ARDUINO)
#include <esp_log.h>
#include <esp_system.h>
#if __has_include(<esp_mac.h>)
#include <esp_mac.h>
#endif
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

bool MqttConfigProvider::getHardwareMacDeviceId(char* out_buf, size_t buf_size) {
    if (!out_buf || buf_size < 21) {
        return false;
    }
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    uint8_t mac[6] = {0};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
        const int written = snprintf(out_buf, buf_size, "aero_s3_%02x%02x%02x%02x%02x%02x",
                                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        return written > 0 && static_cast<size_t>(written) < buf_size;
    }
    LOG_E("Failed to read hardware Wi-Fi STA MAC address!");
    return false;
#elif defined(UNIT_TEST_HOST)
    const int written = snprintf(out_buf, buf_size, "%s", "esp32_device");
    return written > 0 && static_cast<size_t>(written) < buf_size;
#else
    return false;
#endif
}

MqttConfig MqttConfigProvider::load() {
    s_broker_port = static_cast<uint16_t>(MQTT_PORT);

    // 1. Resolve Device ID (hardware MAC by default, or compile-time override)
    const bool is_auto_device_id = (strlen(MQTT_DEVICE_ID) == 0 ||
                                    strcmp(MQTT_DEVICE_ID, "AUTO") == 0 ||
                                    strcmp(MQTT_DEVICE_ID, "auto") == 0);

    if (is_auto_device_id) {
        if (!getHardwareMacDeviceId(s_device_id, sizeof(s_device_id))) {
            LOG_E("Fallback to default device_id 'esp32_device'");
            copyProvisionedValue(s_device_id, sizeof(s_device_id), "esp32_device");
        }
    } else {
        copyProvisionedValue(s_device_id, sizeof(s_device_id), MQTT_DEVICE_ID);
    }

    // 2. Resolve Username (Mosquitto ACL pattern 'aeroponics/device/%u/#' requires username == device_id)
    const bool is_auto_user = (strlen(MQTT_USER) == 0 ||
                               strcmp(MQTT_USER, "AUTO") == 0 ||
                               strcmp(MQTT_USER, "auto") == 0 ||
                               (is_auto_device_id && strcmp(MQTT_USER, "esp32_device") == 0));

    if (is_auto_user) {
        copyProvisionedValue(s_username, sizeof(s_username), s_device_id);
    } else {
        copyProvisionedValue(s_username, sizeof(s_username), MQTT_USER);
    }

    // 3. Resolve Broker Host & Password
    if (!copyProvisionedValue(s_broker_host, sizeof(s_broker_host), MQTT_HOST) ||
        !copyProvisionedValue(s_password, sizeof(s_password), MQTT_PASS) ||
        !isValidDeviceId(s_device_id)) {
        clearConfig();
        LOG_E("Invalid or truncated MQTT provisioning; MQTT remains disabled.");
    }

    if (strlen(s_broker_host) == 0 || strlen(s_device_id) == 0) {
        LOG_E("MQTT broker_host or device_id is missing; MQTT remains disabled.");
    } else {
        LOG_I("Loaded MQTT config for broker: %s:%u (device_id: %s, user: %s)",
              s_broker_host, s_broker_port, s_device_id, s_username);
    }

    MqttConfig config;
    config.broker_host = s_broker_host;
    config.broker_port = s_broker_port;
    config.username = s_username;
    config.password = s_password;
    config.device_id = s_device_id;

    return config;
}
