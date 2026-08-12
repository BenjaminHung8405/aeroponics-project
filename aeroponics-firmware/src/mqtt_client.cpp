#include "mqtt_client.h"

#include <cstdio>
#include <cstring>
#include <ctime>

#if defined(ESP_PLATFORM) || defined(ARDUINO)
#include <esp_log.h>
#include <WiFi.h>
#else
#include <chrono>
#ifndef ESP_LOGI
#define ESP_LOGI(tag, fmt, ...) printf("[INFO][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) printf("[WARN][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGE(tag, fmt, ...) printf("[ERROR][%s] " fmt "\n", tag, ##__VA_ARGS__)
#endif
#endif

namespace {
constexpr const char* TAG = "MQTT";

uint32_t getSystemMillis() {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    return millis();
#else
    using namespace std::chrono;
    static const auto start_time = steady_clock::now();
    return static_cast<uint32_t>(duration_cast<milliseconds>(steady_clock::now() - start_time).count());
#endif
}

bool fitsCString(const char* value, size_t capacity) {
    return value != nullptr && strnlen(value, capacity) < capacity;
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
} // namespace

MqttClient* MqttClient::_instance = nullptr;

MqttClient::MqttClient()
    : _pubsub(), _config{nullptr, 0, nullptr, nullptr, nullptr},
      _rtc(nullptr), _registry(nullptr), _last_heartbeat_ms(0), _is_initialized(false)
#if defined(UNIT_TEST_HOST) && !defined(MQTT_INTEGRATION_TARGET)
      , _mock_unix_time(0)
#endif
{
    _instance = this;
}

void MqttClient::reset() {
    _pubsub.disconnect();
    _config = MqttConfig{nullptr, 0, nullptr, nullptr, nullptr};
    _rtc = nullptr;
    _registry = nullptr;
    _last_heartbeat_ms = 0;
    _is_initialized = false;
}

MqttClient::~MqttClient() {
    if (_instance == this) _instance = nullptr;
}

bool MqttClient::begin(MqttConfig config, IClock* rtc, NodeRegistry* registry) {
    if (!config.broker_host || !config.device_id || config.broker_host[0] == '\0' ||
        config.device_id[0] == '\0' || !isValidDeviceId(config.device_id) ||
        !fitsCString(config.broker_host, MQTT_BROKER_HOST_BUFFER_SIZE) ||
        !fitsCString(config.username, MQTT_USERNAME_BUFFER_SIZE) ||
        !fitsCString(config.password, MQTT_PASSWORD_BUFFER_SIZE)) {
        ESP_LOGE(TAG, "Invalid MQTT configuration or dependencies");
        reset();
        return false;
    }
    _config = config;
    _rtc = rtc;
    _registry = registry;
    _is_initialized = true;
    return true;
}

bool MqttClient::_buildTopic(char* buffer, size_t buffer_size, const char* suffix) const {
    const int written = snprintf(buffer, buffer_size, "%s/%s%s", MQTT_TOPIC_BASE,
                                 _config.device_id, suffix);
    return written >= 0 && static_cast<size_t>(written) < buffer_size;
}

bool MqttClient::_buildClientId(char* buffer, size_t buffer_size) const {
    const int written = snprintf(buffer, buffer_size, "%s%s", MQTT_CLIENT_ID_PREFIX,
                                 _config.device_id);
    return written >= 0 && static_cast<size_t>(written) < buffer_size;
}

bool MqttClient::_buildLwtPayload(char* buffer, size_t buffer_size) const {
    StaticJsonDocument<MQTT_LWT_DOC_SIZE> doc;
    doc["status"] = "offline";
    doc["device_id"] = _config.device_id;
    doc["timestamp_utc"] = nullptr;
    const size_t bytes = serializeJson(doc, buffer, buffer_size);
    return bytes > 0 && bytes < buffer_size;
}

bool MqttClient::connect() {
    if (!_is_initialized) return false;
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (WiFi.status() != WL_CONNECTED) return false;
#endif
    char lwt_payload[MQTT_LWT_DOC_SIZE];
    char lwt_topic[MQTT_TOPIC_BUFFER_SIZE];
    char client_id[MQTT_CLIENT_ID_BUFFER_SIZE];
    if (!_buildLwtPayload(lwt_payload, sizeof(lwt_payload)) ||
        !_buildTopic(lwt_topic, sizeof(lwt_topic), MQTT_STATUS_SUFFIX) ||
        !_buildClientId(client_id, sizeof(client_id))) {
        ESP_LOGE(TAG, "MQTT connection fields exceed their configured buffers");
        return false;
    }
    _pubsub.setServer(_config.broker_host, _config.broker_port);
    _pubsub.setCallback(_onMessage);
    _pubsub.setBufferSize(MQTT_BUFFER_SIZE);
    _pubsub.setKeepAlive(MQTT_KEEPALIVE_S);
    if (!_pubsub.connect(client_id, _config.username, _config.password, lwt_topic, MQTT_LWT_QOS,
                         MQTT_LWT_RETAIN, lwt_payload)) {
        ESP_LOGE(TAG, "PubSubClient connect failed with state: %d", _pubsub.state());
        return false;
    }
    if (!publishHeartbeat() || !_subscribeCommandTopics()) {
        _pubsub.disconnect();
        return false;
    }
    return true;
}

bool MqttClient::_subscribeCommandTopics() {
    char topic_buf[MQTT_TOPIC_BUFFER_SIZE];

    // Gateway Production Domain topics (Sprint 2 / Sprint 1.5 contract)
    const int treatment_written = snprintf(topic_buf, sizeof(topic_buf), "%s/%s%s",
                                           MQTT_TOPIC_BASE, _config.device_id, MQTT_COMMAND_TREATMENT_SUFFIX);
    if (treatment_written < 0 || static_cast<size_t>(treatment_written) >= sizeof(topic_buf) ||
        !_pubsub.subscribe(topic_buf, MQTT_COMMAND_QOS)) {
        return false;
    }

    const int assign_written = snprintf(topic_buf, sizeof(topic_buf), "%s/%s%s",
                                        MQTT_TOPIC_BASE, _config.device_id, MQTT_COMMAND_ASSIGNMENT_SUFFIX);
    if (assign_written < 0 || static_cast<size_t>(assign_written) >= sizeof(topic_buf) ||
        !_pubsub.subscribe(topic_buf, MQTT_COMMAND_QOS)) {
        return false;
    }

    const int node_ovr_written = snprintf(topic_buf, sizeof(topic_buf), "%s/%s%s%s/override",
                                          MQTT_TOPIC_BASE, _config.device_id, MQTT_COMMAND_NODE_OVERRIDE_SUFFIX, MQTT_WILDCARD_SINGLE_LEVEL);
    if (node_ovr_written < 0 || static_cast<size_t>(node_ovr_written) >= sizeof(topic_buf) ||
        !_pubsub.subscribe(topic_buf, MQTT_COMMAND_QOS)) {
        return false;
    }

    const int grp_ctrl_written = snprintf(topic_buf, sizeof(topic_buf), "%s/%s%s%s/control",
                                          MQTT_TOPIC_BASE, _config.device_id, MQTT_COMMAND_GROUP_CONTROL_SUFFIX, MQTT_WILDCARD_SINGLE_LEVEL);
    if (grp_ctrl_written < 0 || static_cast<size_t>(grp_ctrl_written) >= sizeof(topic_buf) ||
        !_pubsub.subscribe(topic_buf, MQTT_COMMAND_QOS)) {
        return false;
    }

    return true;
}

void MqttClient::loop() {
    if (isConnected()) _pubsub.loop();
}

bool MqttClient::_getTimestamp(char* buffer, size_t buffer_size) const {
    if (!_rtc || !_rtc->getTime().is_valid || !_isNtpSynced()) return false;
    const time_t now_sec = static_cast<time_t>(_currentUnixTime());
    struct tm timeinfo;
    gmtime_r(&now_sec, &timeinfo);
    return strftime(buffer, buffer_size, "%Y-%m-%dT%H:%M:%SZ", &timeinfo) > 0;
}

bool MqttClient::publishHeartbeat() {
    if (!isConnected()) return false;
    StaticJsonDocument<MQTT_HEARTBEAT_DOC_SIZE> doc;
    char timestamp[32] = {};
    const uint32_t now = getSystemMillis();
    doc["status"] = "online";
    doc["device_id"] = _config.device_id;
    doc["uptime_s"] = now / 1000U;
    doc["rssi_dbm"] = _getRssiDbm();
    doc["free_heap_b"] = _getFreeHeap();
    doc["ntp_synced"] = _isNtpSynced();
    doc["rtc_valid"] = _rtc && _rtc->getTime().is_valid;
    doc["timestamp_utc"] = _getTimestamp(timestamp, sizeof(timestamp)) ? timestamp : nullptr;
    char topic[MQTT_TOPIC_BUFFER_SIZE];
    if (!_buildTopic(topic, sizeof(topic), MQTT_STATUS_SUFFIX)) return false;
    char payload[MQTT_HEARTBEAT_PAYLOAD_SIZE];
    const size_t bytes = serializeJson(doc, payload, sizeof(payload));
    if (!bytes || bytes >= sizeof(payload)) return false;
    if (!_pubsub.publish(topic, payload, MQTT_PUBLISH_RETAIN)) return false;
    _last_heartbeat_ms = now;
    return true;
}

bool MqttClient::publishGroupTelemetry(uint8_t group_id, uint32_t active_nodes_mask, const char* state_str) {
    if (!isConnected()) return false;
    StaticJsonDocument<256> doc;
    doc["group_id"] = group_id;
    doc["active_nodes_mask"] = active_nodes_mask;
    doc["state"] = state_str ? state_str : "IDLE";
    char topic[MQTT_TOPIC_BUFFER_SIZE];
    snprintf(topic, sizeof(topic), "%s/%s%s%u", MQTT_TOPIC_BASE, _config.device_id, MQTT_TELEMETRY_GROUP_SUFFIX, group_id);
    char payload[256];
    size_t bytes = serializeJson(doc, payload, sizeof(payload));
    return bytes > 0 && bytes < sizeof(payload) && _pubsub.publish(topic, payload, MQTT_PUBLISH_RETAIN);
}

bool MqttClient::publishNodeSnapshot(uint8_t node_id, const NodeState& state) {
    if (!isConnected()) return false;
    StaticJsonDocument<512> doc;
    doc["node_id"] = state.node_id;
    doc["group_id"] = state.group_id;
    doc["desired_state"] = state.desired_state == NodePumpState::ON ? "ON" : "OFF";
    doc["reported_state"] = state.reported_state == NodePumpState::ON ? "ON" : "OFF";
    doc["driver_feedback"] = state.driver_feedback;
    doc["flow_lpm"] = state.flow_lpm_x100 / 100.0f;
    doc["delivered_volume_ml"] = state.delivered_volume_ml;
    doc["health_status"] = state.health == NodeHealthStatus::ONLINE ? "ONLINE" :
                           state.health == NodeHealthStatus::STALE ? "STALE" :
                           state.health == NodeHealthStatus::FAULT ? "FAULT" : "OFFLINE";
    char topic[MQTT_TOPIC_BUFFER_SIZE];
    snprintf(topic, sizeof(topic), "%s/%s%s%u/snapshot", MQTT_TOPIC_BASE, _config.device_id, MQTT_TELEMETRY_NODE_SUFFIX, node_id);
    char payload[512];
    size_t bytes = serializeJson(doc, payload, sizeof(payload));
    return bytes > 0 && bytes < sizeof(payload) && _pubsub.publish(topic, payload, MQTT_PUBLISH_RETAIN);
}

bool MqttClient::publishCommandAck(const char* command_id, const char* status, uint8_t node_id, const char* reason) {
    if (!isConnected() || !command_id) return false;
    StaticJsonDocument<256> doc;
    doc["command_id"] = command_id;
    doc["status"] = status ? status : "COMPLETED";
    if (node_id > 0) doc["node_id"] = node_id;
    if (reason) doc["reason"] = reason;
    char topic[MQTT_TOPIC_BUFFER_SIZE];
    snprintf(topic, sizeof(topic), "%s/%s%s%s", MQTT_TOPIC_BASE, _config.device_id, MQTT_ACK_PREFIX_SUFFIX, command_id);
    char payload[256];
    size_t bytes = serializeJson(doc, payload, sizeof(payload));
    return bytes > 0 && bytes < sizeof(payload) && _pubsub.publish(topic, payload, MQTT_PUBLISH_RETAIN);
}

bool MqttClient::isConnected() const {
    return const_cast<PubSubClient&>(_pubsub).connected();
}

void MqttClient::_onMessage(char* topic, uint8_t* payload, unsigned int length) {
    if (!_instance || !topic || !payload || length >= MQTT_BUFFER_SIZE) return;

    StaticJsonDocument<MQTT_COMMAND_DOC_SIZE> doc;
    const DeserializationError error = deserializeJson(doc, payload, length);
    if (error) return;

    // Gateway Production Domain topics
    if (strstr(topic, MQTT_COMMAND_ASSIGNMENT_SUFFIX)) {
        if (_instance->_registry && doc["node_id"].is<uint8_t>() && doc["group_id"].is<uint8_t>()) {
            uint8_t node_id = doc["node_id"].as<uint8_t>();
            uint8_t group_id = doc["group_id"].as<uint8_t>();
            const char* cmd_id = doc["command_id"] | "cmd-assignment";
            _instance->_registry->assignNodeToGroup(node_id, group_id);
            _instance->publishCommandAck(cmd_id, "COMPLETED", node_id, "Group assignment updated");
        }
        return;
    }

    if (strstr(topic, MQTT_COMMAND_NODE_OVERRIDE_SUFFIX)) {
        if (_instance->_registry) {
            const char* ptr = strstr(topic, MQTT_COMMAND_NODE_OVERRIDE_SUFFIX);
            if (ptr) {
                ptr += strlen(MQTT_COMMAND_NODE_OVERRIDE_SUFFIX);
                uint8_t node_id = static_cast<uint8_t>(atoi(ptr));
                const char* state_str = doc["desired_state"] | doc["state"] | "OFF";
                const char* cmd_id = doc["command_id"] | "cmd-node-override";
                NodePumpState desired = (strcmp(state_str, "ON") == 0 || strcmp(state_str, "on") == 0) ? NodePumpState::ON : NodePumpState::OFF;
                _instance->_registry->setDesiredState(node_id, desired);
                _instance->publishCommandAck(cmd_id, "ACCEPTED", node_id, "Node override accepted and queued");
            }
        }
        return;
    }

    if (strstr(topic, MQTT_COMMAND_GROUP_CONTROL_SUFFIX)) {
        if (_instance->_registry) {
            const char* ptr = strstr(topic, MQTT_COMMAND_GROUP_CONTROL_SUFFIX);
            if (ptr) {
                ptr += strlen(MQTT_COMMAND_GROUP_CONTROL_SUFFIX);
                uint8_t group_id = static_cast<uint8_t>(atoi(ptr));
                const char* action_str = doc["action"] | doc["state"] | "OFF";
                const char* cmd_id = doc["command_id"] | "cmd-group-control";
                NodePumpState desired = (strcmp(action_str, "ON") == 0 || strcmp(action_str, "on") == 0) ? NodePumpState::ON : NodePumpState::OFF;
                _instance->_registry->updateDesiredStateForGroup(group_id, desired);
                _instance->publishCommandAck(cmd_id, "ACCEPTED", 0, "Group control accepted and queued");
            }
        }
        return;
    }
}

int MqttClient::_getRssiDbm() const {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    return WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
#else
    return 0;
#endif
}

uint32_t MqttClient::_getFreeHeap() const {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    return ESP.getFreeHeap();
#else
    return 0;
#endif
}

bool MqttClient::_isNtpSynced() const {
    return _currentUnixTime() > 1600000000L;
}

int64_t MqttClient::_currentUnixTime() const {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    time_t now_sec = 0;
    time(&now_sec);
    return static_cast<int64_t>(now_sec);
#elif defined(UNIT_TEST_HOST) && !defined(MQTT_INTEGRATION_TARGET)
    return _mock_unix_time;
#else
    return 0;
#endif
}

bool MqttClient::isInitialized() const {
    return _is_initialized;
}
