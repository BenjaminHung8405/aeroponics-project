#pragma once

#include <cstdint>
#include <cstddef>
#include "mqtt_client.h"
#include "nvs_storage.h"

/**
 * @brief Configuration provider for git-ignored provisioned MQTT settings.
 * This provider is intentionally secret-header only: NvsStorage currently stores
 * relay profiles, not MQTT credentials. Missing broker or device ID fails closed.
 */
class MqttConfigProvider {
public:
    /**
     * @brief Load MQTT configuration from secrets.h, config_secret.h, or hardware MAC.
     * @return MqttConfig filled with broker_host, broker_port, username, password, device_id.
     */
    static MqttConfig load();

    /**
     * @brief Query the hardware Wi-Fi STA MAC address formatted as aero_s3_<12hex>.
     * On UNIT_TEST_HOST, returns deterministic test fallback "esp32_device".
     * @param out_buf Buffer to store the resulting null-terminated device_id.
     * @param buf_size Size of out_buf (must be at least 21 bytes).
     * @return true if successfully resolved and copied into out_buf.
     */
    static bool getHardwareMacDeviceId(char* out_buf, size_t buf_size);
};

