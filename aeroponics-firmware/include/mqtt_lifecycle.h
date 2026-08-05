#pragma once

#include "mqtt_client.h"

/**
 * @brief Commit MQTT startup only after its owning FreeRTOS task exists.
 * @return true when the task was created; false after rolling back the facade.
 */
inline bool finalizeMqttTaskStartup(MqttClient& client, bool task_created) {
    if (task_created) return true;
    client.reset();
    return false;
}

