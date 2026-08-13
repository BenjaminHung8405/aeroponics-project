#include "mqtt_client.h"

#include <cstdio>
#include <cstdlib>
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

bool parseBoundedUint(const char* str, uint8_t min_val, uint8_t max_val, uint8_t& out_val) {
    if (!str || *str == '\0') return false;
    char* endptr = nullptr;
    unsigned long val = std::strtoul(str, &endptr, 10);
    if (endptr == str || *endptr != '\0') return false;
    if (val < min_val || val > max_val) return false;
    out_val = static_cast<uint8_t>(val);
    return true;
}
} // namespace

MqttClient* MqttClient::_instance = nullptr;

MqttClient::MqttClient()
    : _pubsub(), _config{nullptr, 0, nullptr, nullptr, nullptr},
      _rtc(nullptr), _registry(nullptr), _command_manager(nullptr), _group_scheduler(nullptr), _last_heartbeat_ms(0), _is_initialized(false)
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
    _command_manager = nullptr;
    _group_scheduler = nullptr;
    _last_heartbeat_ms = 0;
    _is_initialized = false;
}

MqttClient::~MqttClient() {
    if (_instance == this) _instance = nullptr;
}

bool MqttClient::begin(MqttConfig config, IClock* rtc, NodeRegistry* registry, CommandManager* command_manager,
                       GroupScheduleManager* group_scheduler) {
    if (!config.broker_host || !config.device_id || config.broker_host[0] == '\0' ||
        config.device_id[0] == '\0' || !isValidDeviceId(config.device_id) ||
        !fitsCString(config.broker_host, MQTT_BROKER_HOST_BUFFER_SIZE) ||
        !fitsCString(config.username, MQTT_USERNAME_BUFFER_SIZE) ||
        !fitsCString(config.password, MQTT_PASSWORD_BUFFER_SIZE) ||
        std::strcmp(config.username, config.device_id) != 0) {
        ESP_LOGE(TAG, "Invalid MQTT configuration or dependencies");
        reset();
        return false;
    }
    _config = config;
    _rtc = rtc;
    _registry = registry;
    _command_manager = command_manager;
    _group_scheduler = group_scheduler;
    if (_command_manager != nullptr) _command_manager->setOutcomeSink(this);
    _is_initialized = true;
    return true;
}

bool MqttClient::_buildTopic(char* buffer, size_t buffer_size, const char* suffix) const {
    const int written = snprintf(buffer, buffer_size, "%s/%s%s", MQTT_TOPIC_BASE,
                                 _config.device_id, suffix);
    return written >= 0 && static_cast<size_t>(written) < buffer_size;
}

bool MqttClient::_buildClientId(char* buffer, size_t buffer_size) const {
    const int written = snprintf(buffer, buffer_size, "%s", _config.device_id);
    return written >= 0 && static_cast<size_t>(written) < buffer_size;
}

bool MqttClient::_buildLwtPayload(char* buffer, size_t buffer_size) const {
    JsonDocument doc;
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

    const int flow_policy_written = snprintf(topic_buf, sizeof(topic_buf), "%s/%s%s",
                                              MQTT_TOPIC_BASE, _config.device_id,
                                              MQTT_COMMAND_FLOW_POLICY_SUFFIX);
    if (flow_policy_written < 0 || static_cast<size_t>(flow_policy_written) >= sizeof(topic_buf) ||
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

bool MqttClient::isConnected() const {
    return const_cast<PubSubClient&>(_pubsub).connected();
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
    JsonDocument doc;
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
    JsonDocument doc;
    doc["group_id"] = group_id;
    doc["active_nodes_mask"] = active_nodes_mask;
    doc["state"] = state_str ? state_str : "IDLE";
    char topic[MQTT_TOPIC_BUFFER_SIZE];
    const int written = snprintf(topic, sizeof(topic), "%s/%s%s%u", MQTT_TOPIC_BASE, _config.device_id, MQTT_TELEMETRY_GROUP_SUFFIX, group_id);
    if (written < 0 || static_cast<size_t>(written) >= sizeof(topic)) return false;
    char payload[256];
    size_t bytes = serializeJson(doc, payload, sizeof(payload));
    return bytes > 0 && bytes < sizeof(payload) && _pubsub.publish(topic, payload, MQTT_PUBLISH_RETAIN);
}

bool MqttClient::publishNodeSnapshot(uint8_t node_id, const NodeState& state) {
    if (!isConnected()) return false;
    JsonDocument doc;
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
    const int written = snprintf(topic, sizeof(topic), "%s/%s%s%u/snapshot", MQTT_TOPIC_BASE, _config.device_id, MQTT_TELEMETRY_NODE_SUFFIX, node_id);
    if (written < 0 || static_cast<size_t>(written) >= sizeof(topic)) return false;
    char payload[512];
    size_t bytes = serializeJson(doc, payload, sizeof(payload));
    return bytes > 0 && bytes < sizeof(payload) && _pubsub.publish(topic, payload, MQTT_PUBLISH_RETAIN);
}

bool MqttClient::publishCommandAck(const char* command_id, const char* status, uint8_t node_id, const char* reason) {
    if (!isConnected() || !isValidMqttCommandId(command_id)) return false;
    JsonDocument doc;
    doc["command_id"] = command_id;
    doc["status"] = status ? status : "COMPLETED";
    if (node_id > 0) doc["node_id"] = node_id;
    if (reason) doc["reason"] = reason;
    char topic[MQTT_TOPIC_BUFFER_SIZE];
    const int written = snprintf(topic, sizeof(topic), "%s/%s%s%s", MQTT_TOPIC_BASE, _config.device_id, MQTT_ACK_PREFIX_SUFFIX, command_id);
    if (written < 0 || static_cast<size_t>(written) >= sizeof(topic)) return false;
    char payload[256];
    size_t bytes = serializeJson(doc, payload, sizeof(payload));
    return bytes > 0 && bytes < sizeof(payload) && _pubsub.publish(topic, payload, MQTT_PUBLISH_RETAIN);
}

bool MqttClient::publishCommandEvent(const char* command_id, const char* status,
                                     uint8_t node_id, const char* reason) {
    if (!isConnected() || !isValidMqttCommandId(command_id)) return false;
    JsonDocument doc;
    doc["command_id"] = command_id;
    doc["status"] = status ? status : "UNKNOWN";
    if (node_id > 0) doc["node_id"] = node_id;
    if (reason) doc["reason"] = reason;
    char topic[MQTT_TOPIC_BUFFER_SIZE];
    const int written = snprintf(topic, sizeof(topic), "%s/%s%s%s%s", MQTT_TOPIC_BASE, _config.device_id,
                                 MQTT_COMMAND_EVENT_PREFIX_SUFFIX, command_id, MQTT_COMMAND_EVENT_SUFFIX);
    if (written < 0 || static_cast<size_t>(written) >= sizeof(topic)) return false;
    char payload[256];
    const size_t bytes = serializeJson(doc, payload, sizeof(payload));
    return bytes > 0 && bytes < sizeof(payload) && _pubsub.publish(topic, payload, MQTT_PUBLISH_RETAIN);
}

void MqttClient::publishCommandOutcome(const char* command_id, const char* status,
                                       uint8_t node_id, const char* reason) {
    publishCommandEvent(command_id, status, node_id, reason);
}

void MqttClient::publishSafetyAudit(const char* event, const char* reason) {
    if (!isConnected() || !event) return;
    JsonDocument doc;
    doc["event"] = event;
    if (reason) doc["reason"] = reason;
    char topic[MQTT_TOPIC_BUFFER_SIZE];
    if (!_buildTopic(topic, sizeof(topic), "/telemetry/audit")) return;
    char payload[256];
    const size_t bytes = serializeJson(doc, payload, sizeof(payload));
    if (bytes > 0 && bytes < sizeof(payload)) _pubsub.publish(topic, payload, MQTT_PUBLISH_RETAIN);
}

bool MqttClient::_hasValidCommandEnvelope(const JsonDocument& doc, const char*& command_id) const {
    command_id = doc["command_id"];
    return isValidMqttCommandId(command_id) &&
           doc["version"].is<uint16_t>() && doc["version"].as<uint16_t>() > 0;
}

void MqttClient::_handleAssignmentCommand(const JsonDocument& doc) {
    const char* cmd_id = nullptr;
    if (!_hasValidCommandEnvelope(doc, cmd_id)) {
        publishCommandAck("unknown", "REJECTED", 0, "Missing command_id or version");
        return;
    }
    if (!_registry || !_command_manager || !doc["node_id"].is<uint8_t>() || !doc["group_id"].is<uint8_t>()) {
        publishCommandAck(cmd_id, "REJECTED", 0, "Invalid payload fields or command manager missing");
        return;
    }
    uint8_t node_id = doc["node_id"].as<uint8_t>();
    uint8_t group_id = doc["group_id"].as<uint8_t>();
    if (_command_manager->requestNodeReassignment(node_id, group_id, cmd_id)) {
        publishCommandAck(cmd_id, "ACCEPTED", node_id, "Safe-off queued; mapping commits after RF OFF ACK");
    } else {
        publishCommandAck(cmd_id, "REJECTED", node_id, "Group assignment mutation failed");
    }
}

void MqttClient::_handleFlowPolicyCommand(const JsonDocument& doc) {
    const char* cmd_id = nullptr;
    if (!_hasValidCommandEnvelope(doc, cmd_id) || !_command_manager ||
        !doc["node_id"].is<uint8_t>() || !doc["policy_version"].is<uint32_t>() ||
        !doc["treatment_version_id"].is<uint32_t>() || !doc["calibration_id"].is<uint32_t>() ||
        !doc["min_flow_lpm_x100"].is<uint16_t>() || !doc["max_off_flow_lpm_x100"].is<uint16_t>() ||
        !doc["max_flow_lpm_x100"].is<uint16_t>() || !doc["flow_start_timeout_ms"].is<uint32_t>() ||
        !doc["run_lease_ms"].is<uint32_t>() || !doc["max_on_duration_ms"].is<uint32_t>()) {
        publishCommandAck(cmd_id ? cmd_id : "unknown", "REJECTED", 0, "Invalid flow policy provenance or limits");
        return;
    }
    const uint8_t node_id = doc["node_id"].as<uint8_t>();
    const uint32_t lease_ms = doc["run_lease_ms"].as<uint32_t>();
    const uint32_t max_on_ms = doc["max_on_duration_ms"].as<uint32_t>();
    const FlowPolicyProvenance provenance{doc["policy_version"].as<uint32_t>(),
                                          doc["treatment_version_id"].as<uint32_t>(),
                                          doc["calibration_id"].as<uint32_t>()};
    const bool valid = _command_manager->provisionNodeControlPolicy(
        node_id, lease_ms, max_on_ms, doc["min_flow_lpm_x100"], doc["max_off_flow_lpm_x100"],
        doc["max_flow_lpm_x100"], doc["flow_start_timeout_ms"], provenance);
    publishCommandAck(cmd_id, valid ? "ACCEPTED" : "REJECTED", node_id,
                      valid ? "Authenticated flow policy provisioned" : "Invalid flow policy limits");
}

void MqttClient::_handleTreatmentCommand(const JsonDocument& doc) {
    const char* cmd_id = nullptr;
    if (!_hasValidCommandEnvelope(doc, cmd_id) || !_group_scheduler ||
        !doc["group_id"].is<uint8_t>() || !doc["season_id"].is<uint32_t>() ||
        !doc["treatment_version_id"].is<uint32_t>() || !doc["treatment_version"].is<uint32_t>() ||
        !doc["treatment_status"].is<const char*>()) {
        publishCommandAck(cmd_id ? cmd_id : "unknown", "REJECTED", 0, "Invalid published treatment assignment");
        return;
    }
    const char* status = doc["treatment_status"];
    if (strcmp(status, "PUBLISHED") != 0) {
        publishCommandAck(cmd_id, "REJECTED", 0, "Treatment version is not PUBLISHED");
        return;
    }
    PublishedTreatmentAssignment assignment;
    assignment.season_id = doc["season_id"].as<uint32_t>();
    assignment.treatment_version_id = doc["treatment_version_id"].as<uint32_t>();
    assignment.version = doc["treatment_version"].as<uint32_t>();
    if (!doc["schedule"]["spray_day_s"].is<uint32_t>() || !doc["schedule"]["cooldown_day_s"].is<uint32_t>() ||
        !doc["schedule"]["spray_night_s"].is<uint32_t>() || !doc["schedule"]["cooldown_night_s"].is<uint32_t>()) {
        publishCommandAck(cmd_id, "REJECTED", 0, "Missing treatment schedule");
        return;
    }
    assignment.profile = GroupProfile{doc["schedule"]["spray_day_s"], doc["schedule"]["cooldown_day_s"],
                                      doc["schedule"]["spray_night_s"], doc["schedule"]["cooldown_night_s"]};
    const uint8_t group_id = doc["group_id"].as<uint8_t>();
    publishCommandAck(cmd_id, _group_scheduler->applyPublishedTreatment(group_id, assignment) ? "ACCEPTED" : "REJECTED",
                      0, "Published treatment assignment validation result");
}

void MqttClient::_handleNodeOverrideCommand(uint8_t node_id, const JsonDocument& doc) {
    const char* cmd_id = nullptr;
    if (!_hasValidCommandEnvelope(doc, cmd_id)) {
        publishCommandAck("unknown", "REJECTED", node_id, "Missing command_id or version");
        return;
    }
    if (!_registry || !_command_manager) {
        publishCommandAck(cmd_id, "REJECTED", node_id, "RF command manager unavailable");
        return;
    }
    const char* state_str = doc["desired_state"] | doc["state"];
    if (!state_str) {
        publishCommandAck(cmd_id, "REJECTED", node_id, "Missing desired_state string");
        return;
    }
    bool is_on = (strcmp(state_str, "ON") == 0 || strcmp(state_str, "on") == 0);
    bool is_off = (strcmp(state_str, "OFF") == 0 || strcmp(state_str, "off") == 0);
    if (!is_on && !is_off) {
        publishCommandAck(cmd_id, "REJECTED", node_id, "Invalid desired_state enum value");
        return;
    }
    NodePumpState desired = is_on ? NodePumpState::ON : NodePumpState::OFF;
    if (_command_manager->queueExternalNodeCommand(node_id, desired, cmd_id)) {
        publishCommandAck(cmd_id, "ACCEPTED", node_id, "Node override accepted and queued");
    } else {
        publishCommandAck(cmd_id, "REJECTED", node_id, "Node override mutation failed");
    }
}

void MqttClient::_handleGroupControlCommand(uint8_t group_id, const JsonDocument& doc) {
    const char* cmd_id = nullptr;
    if (!_hasValidCommandEnvelope(doc, cmd_id)) {
        publishCommandAck("unknown", "REJECTED", 0, "Missing command_id or version");
        return;
    }
    if (!_registry || !_command_manager) {
        publishCommandAck(cmd_id, "REJECTED", 0, "RF command manager unavailable");
        return;
    }
    const char* action_str = doc["action"] | doc["state"];
    if (!action_str) {
        publishCommandAck(cmd_id, "REJECTED", 0, "Missing action string");
        return;
    }
    bool is_on = (strcmp(action_str, "ON") == 0 || strcmp(action_str, "on") == 0);
    bool is_off = (strcmp(action_str, "OFF") == 0 || strcmp(action_str, "off") == 0);
    if (!is_on && !is_off) {
        publishCommandAck(cmd_id, "REJECTED", 0, "Invalid action enum value");
        return;
    }
    NodePumpState desired = is_on ? NodePumpState::ON : NodePumpState::OFF;
    bool accepted = true;
    for (uint8_t node_id = 1; node_id <= MAX_NODES; ++node_id) {
        if (_registry->getNodeGroup(node_id) == group_id &&
            !_command_manager->queueExternalNodeCommand(node_id, desired, cmd_id)) accepted = false;
    }
    publishCommandAck(cmd_id, accepted ? "ACCEPTED" : "REJECTED", 0,
                      accepted ? "Group control accepted and queued" : "Group contains unavailable or faulted node");
}

void MqttClient::_parseNodeTopic(const char* ptr, const JsonDocument& doc) {
    const char* slash = strchr(ptr, '/');
    if (!slash || strcmp(slash, "/override") != 0) return;
    char id_buf[16] = {};
    size_t id_len = slash - ptr;
    if (id_len > 0 && id_len < sizeof(id_buf)) {
        std::memcpy(id_buf, ptr, id_len);
        uint8_t node_id = 0;
        if (parseBoundedUint(id_buf, 1, 12, node_id)) {
            _handleNodeOverrideCommand(node_id, doc);
        }
    }
}

void MqttClient::_parseGroupTopic(const char* ptr, const JsonDocument& doc) {
    const char* slash = strchr(ptr, '/');
    if (!slash || strcmp(slash, "/control") != 0) return;
    char id_buf[16] = {};
    size_t id_len = slash - ptr;
    if (id_len > 0 && id_len < sizeof(id_buf)) {
        std::memcpy(id_buf, ptr, id_len);
        uint8_t group_id = 0;
        if (parseBoundedUint(id_buf, 1, 4, group_id)) {
            _handleGroupControlCommand(group_id, doc);
        }
    }
}

void MqttClient::_onMessage(char* topic, uint8_t* payload, unsigned int length) {
    if (!_instance || !topic || !payload || length >= MQTT_BUFFER_SIZE) return;

    JsonDocument doc;
    if (deserializeJson(doc, payload, length)) return;

    char prefix[128];
    const int written = snprintf(prefix, sizeof(prefix), "%s/%s/command/", MQTT_TOPIC_BASE, _instance->_config.device_id);
    if (written < 0 || static_cast<size_t>(written) >= sizeof(prefix)) return;

    const size_t prefix_len = strlen(prefix);
    if (strncmp(topic, prefix, prefix_len) != 0) return;

    const char* sub_topic = topic + prefix_len;

    if (strcmp(sub_topic, "config/treatment") == 0) {
        _instance->_handleTreatmentCommand(doc);
    } else if (strcmp(sub_topic, "config/assignment") == 0) {
        _instance->_handleAssignmentCommand(doc);
    } else if (strcmp(sub_topic, "config/flow-policy") == 0) {
        _instance->_handleFlowPolicyCommand(doc);
    } else if (strncmp(sub_topic, "node/", 5) == 0) {
        _instance->_parseNodeTopic(sub_topic + 5, doc);
    } else if (strncmp(sub_topic, "group/", 6) == 0) {
        _instance->_parseGroupTopic(sub_topic + 6, doc);
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
