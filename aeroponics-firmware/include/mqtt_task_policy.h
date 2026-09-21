#pragma once

#include <cstdint>
#include <algorithm>

#include "config.h"

/**
 * @brief State and deterministic transitions used by the MQTT FreeRTOS task.
 *
 * Keeping the timing policy independent from Arduino/FreeRTOS APIs makes the
 * reconnect and heartbeat lifecycle testable with an injected clock while the
 * production task continues to use millis().
 */
struct MqttTaskState {
    uint32_t backoff_s = MQTT_RECONNECT_BASE_S;
    uint32_t last_connect_attempt_ms = 0;
    uint32_t last_heartbeat_ms = 0;
    bool was_connected = false;
    bool has_connect_attempt = false;
    uint32_t consecutive_failures = 0;
};

inline bool mqttReconnectDue(const MqttTaskState& state, uint32_t now_ms) {
    return !state.has_connect_attempt ||
           now_ms - state.last_connect_attempt_ms >= state.backoff_s * 1000U;
}

inline void mqttRecordReconnectAttempt(MqttTaskState& state, uint32_t now_ms) {
    state.last_connect_attempt_ms = now_ms;
    state.has_connect_attempt = true;
}

inline void mqttRecordReconnectFailure(MqttTaskState& state) {
    state.consecutive_failures++;
    state.backoff_s = std::min(state.backoff_s * 2U, MQTT_RECONNECT_MAX_S);
}

inline void mqttRecordReconnectSuccess(MqttTaskState& state, uint32_t now_ms) {
    state.backoff_s = MQTT_RECONNECT_BASE_S;
    state.was_connected = true;
    state.last_heartbeat_ms = now_ms;
    state.consecutive_failures = 0;
}

inline void mqttRecordWifiLoss(MqttTaskState& state) {
    state.was_connected = false;
    state.backoff_s = MQTT_RECONNECT_BASE_S;
    state.last_connect_attempt_ms = 0;
    state.has_connect_attempt = false;
    state.consecutive_failures = 0;
}

inline bool mqttHeartbeatDue(const MqttTaskState& state, uint32_t now_ms) {
    return now_ms - state.last_heartbeat_ms >= MQTT_HEARTBEAT_INTERVAL_MS;
}

// The task owns the deadline transition; the publisher result is deliberately
// injected so this failure path remains unit-testable without a broker.
template <typename PublishHeartbeat>
inline bool mqttServiceHeartbeat(MqttTaskState& state, uint32_t now_ms,
                                 PublishHeartbeat&& publish_heartbeat) {
    if (!mqttHeartbeatDue(state, now_ms)) return false;
    if (!publish_heartbeat()) return false;
    state.last_heartbeat_ms = now_ms;
    return true;
}
