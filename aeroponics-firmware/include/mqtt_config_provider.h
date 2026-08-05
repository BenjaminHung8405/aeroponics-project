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
     * @brief Load MQTT configuration from secrets.h or config_secret.h.
     * @return MqttConfig filled with broker_host, broker_port, username, password, device_id.
     */
    static MqttConfig load();
};
