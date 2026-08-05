#pragma once

#include <cstdint>
#include <cstddef>
#include "mqtt_client.h"
#include "nvs_storage.h"

/**
 * @brief Configuration provider for loading MQTT broker settings and credentials.
 * Credentials loaded from NVS namespace or git-ignored secrets headers.
 */
class MqttConfigProvider {
public:
    /**
     * @brief Load MQTT configuration using NVS storage or fallback secrets.
     * @param nvs Pointer to NvsStorage instance (optional).
     * @return MqttConfig filled with broker_host, broker_port, username, password, device_id.
     */
    static MqttConfig load(NvsStorage* nvs = nullptr);
};
