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
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    _inbound_mutex = xSemaphoreCreateMutex();
    _outbound_mutex = xSemaphoreCreateMutex();
#endif
    _instance = this;
}

void MqttClient::reset() {
    // Disconnect is called only by the MQTT owner task in production. Reset is
    // also used by startup rollback before that task exists.
    _pubsub.disconnect();
    _connected.store(false);
    _config = MqttConfig{nullptr, 0, nullptr, nullptr, nullptr};
    _rtc = nullptr;
    _registry = nullptr;
    _command_manager = nullptr;
    _group_scheduler = nullptr;
    _last_heartbeat_ms = 0;
    _inbound_head = _inbound_tail = _inbound_count = 0;
    _outbound_ack_head = _outbound_ack_tail = _outbound_ack_count = 0;
    _reserved_ack_count = 0;
    _backpressure_failure_head = _backpressure_failure_tail = _backpressure_failure_count = 0;
    _outbound_telemetry_head = _outbound_telemetry_tail = _outbound_telemetry_count = 0;
    _is_initialized = false;
}

MqttClient::~MqttClient() {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (_inbound_mutex != nullptr) vSemaphoreDelete(_inbound_mutex);
    if (_outbound_mutex != nullptr) vSemaphoreDelete(_outbound_mutex);
#endif
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
    _connected.store(false);
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
    _connected.store(true);
    if (!_subscribeCommandTopics()) {
        _pubsub.disconnect();
        _connected.store(false);
        return false;
    }
    if (!publishConnectedHeartbeat()) {
        _pubsub.disconnect();
        _connected.store(false);
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
    // PubSubClient ownership is restricted to this MQTT task. No other
    // thread may call loop(), publish(), subscribe(), or disconnect().
    if (_pubsub.connected()) _pubsub.loop();
    _connected.store(_pubsub.connected());
}

void MqttClient::serviceOutgoingEvents() {
    if (!_pubsub.connected()) return;
    _publishQueueOverflowAudit();
    MqttOutboundEvent event{};
    size_t processed = 0;
    bool critical = false;
    bool backpressure_failure = false;
    while (processed < MQTT_OUTBOUND_DRAIN_BUDGET && _peekOutboundEvent(event, critical, backpressure_failure)) {
        // Peek before publish. A socket failure therefore leaves the event in
        // its lane for the next MQTT tick instead of silently losing ACKs.
        if (!_pubsub.publish(event.topic, event.payload, event.retained)) {
            _last_outbound_publish_ok = false;
            _outbound_dropped.fetch_add(1);
            break;
        }
        _discardOutboundEvent(critical, backpressure_failure);
        _last_outbound_publish_ok = true;
        ++processed;
    }
}

void MqttClient::_publishQueueOverflowAudit() {
    const uint32_t now = getSystemMillis();
    if (now - _last_queue_audit_ms < MQTT_QUEUE_AUDIT_INTERVAL_MS) return;
    const uint32_t inbound_rejected = _inbound_rejected.load();
    const uint32_t outbound_dropped = _outbound_dropped.load();
    if (inbound_rejected == 0 && outbound_dropped == 0) return;
    char topic[MQTT_TOPIC_BUFFER_SIZE];
    if (!_buildTopic(topic, sizeof(topic), "/telemetry/audit")) return;
    char payload[MQTT_TELEMETRY_PAYLOAD_SIZE];
    const int written = snprintf(payload, sizeof(payload),
        "{\"event\":\"MQTT_QUEUE_OVERFLOW\",\"inbound_rejected\":%u,\"outbound_dropped\":%u}",
        static_cast<unsigned>(inbound_rejected), static_cast<unsigned>(outbound_dropped));
    if (written > 0 && static_cast<size_t>(written) < sizeof(payload) &&
        _enqueueOutboundEvent(topic, payload, MQTT_PUBLISH_RETAIN, true)) {
        _inbound_rejected.fetch_sub(inbound_rejected);
        _outbound_dropped.fetch_sub(outbound_dropped);
        _last_queue_audit_ms = now;
    }
}

bool MqttClient::_enqueueInboundCommand(const MqttInboundCommand& command) {
    if (!_reserveCommandAck()) return _enqueueBackpressureRejection(command.command_id, command.node_id);
    MqttInboundCommand reserved_command = command;
    reserved_command.ack_reserved = true;
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (_inbound_mutex == nullptr || xSemaphoreTake(_inbound_mutex, portMAX_DELAY) != pdTRUE) {
        return _publishReservedCommandAck(command.command_id, "REJECTED", command.node_id,
                                          "Inbound command lock unavailable");
    }
#else
    std::lock_guard<std::mutex> lock(_inbound_mutex);
#endif
    if (_inbound_count == MQTT_INBOUND_COMMAND_QUEUE_DEPTH) {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
        xSemaphoreGive(_inbound_mutex);
#endif
        return _publishReservedCommandAck(command.command_id, "REJECTED", command.node_id,
                                          "Inbound command queue full");
    }
    _inbound_commands[_inbound_tail] = reserved_command;
    _inbound_tail = (_inbound_tail + 1U) % MQTT_INBOUND_COMMAND_QUEUE_DEPTH;
    ++_inbound_count;
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    xSemaphoreGive(_inbound_mutex);
#endif
    return true;
}

bool MqttClient::_enqueueBackpressureRejection(const char* command_id, uint8_t node_id) {
    if (!isValidMqttCommandId(command_id)) return false;
    JsonDocument doc;
    doc["command_id"] = command_id;
    doc["status"] = "REJECTED";
    if (node_id > 0) doc["node_id"] = node_id;
    doc["reason"] = "Command ACK admission lane full";
    char topic[MQTT_TOPIC_BUFFER_SIZE] = {};
    char payload[256] = {};
    const int written = snprintf(topic, sizeof(topic), "%s/%s%s%s", MQTT_TOPIC_BASE,
                                 _config.device_id, MQTT_ACK_PREFIX_SUFFIX, command_id);
    const size_t bytes = serializeJson(doc, payload, sizeof(payload));
    if (written < 0 || static_cast<size_t>(written) >= sizeof(topic) || bytes == 0 || bytes >= sizeof(payload)) return false;
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (_outbound_mutex == nullptr || xSemaphoreTake(_outbound_mutex, portMAX_DELAY) != pdTRUE) return false;
#else
    std::lock_guard<std::mutex> lock(_outbound_mutex);
#endif
    if (_backpressure_failure_count == MQTT_BACKPRESSURE_FAILURE_QUEUE_DEPTH) {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
        xSemaphoreGive(_outbound_mutex);
#endif
        return false;
    }
    MqttOutboundEvent& event = _backpressure_failure_events[_backpressure_failure_tail];
    std::strncpy(event.topic, topic, sizeof(event.topic) - 1);
    std::strncpy(event.payload, payload, sizeof(event.payload) - 1);
    event.retained = true;
    _backpressure_failure_tail = (_backpressure_failure_tail + 1U) % MQTT_BACKPRESSURE_FAILURE_QUEUE_DEPTH;
    ++_backpressure_failure_count;
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    xSemaphoreGive(_outbound_mutex);
#endif
    _inbound_rejected.fetch_add(1);
    return true;
}

bool MqttClient::_dequeueInboundCommand(MqttInboundCommand& command) {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (_inbound_mutex == nullptr || xSemaphoreTake(_inbound_mutex, pdMS_TO_TICKS(10)) != pdTRUE) return false;
#else
    std::lock_guard<std::mutex> lock(_inbound_mutex);
#endif
    if (_inbound_count == 0) {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
        xSemaphoreGive(_inbound_mutex);
#endif
        return false;
    }
    command = _inbound_commands[_inbound_head];
    _inbound_head = (_inbound_head + 1U) % MQTT_INBOUND_COMMAND_QUEUE_DEPTH;
    --_inbound_count;
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    xSemaphoreGive(_inbound_mutex);
#endif
    return true;
}

bool MqttClient::isConnected() const {
    return _connected.load();
}

bool MqttClient::_enqueueOutboundEvent(const char* topic, const char* payload, bool retained,
                                       bool critical) {
    if (!topic || !payload || std::strlen(topic) >= MQTT_TOPIC_BUFFER_SIZE ||
        std::strlen(payload) >= MQTT_TELEMETRY_PAYLOAD_SIZE) return false;
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (_outbound_mutex == nullptr || xSemaphoreTake(_outbound_mutex, portMAX_DELAY) != pdTRUE) {
        _outbound_dropped.fetch_add(1);
        return false;
    }
#else
    std::unique_lock<std::mutex> lock(_outbound_mutex);
#endif
    MqttOutboundEvent* events = critical ? _outbound_ack_events : _outbound_telemetry_events;
    size_t& head = critical ? _outbound_ack_head : _outbound_telemetry_head;
    size_t& tail = critical ? _outbound_ack_tail : _outbound_telemetry_tail;
    size_t& count = critical ? _outbound_ack_count : _outbound_telemetry_count;
    const size_t depth = critical ? MQTT_OUTBOUND_ACK_QUEUE_DEPTH : MQTT_OUTBOUND_TELEMETRY_QUEUE_DEPTH;
    if (count == depth || (critical && count + _reserved_ack_count == depth)) {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
        xSemaphoreGive(_outbound_mutex);
#endif
        _outbound_dropped.fetch_add(1);
        return false;
    }
    MqttOutboundEvent& event = events[tail];
    std::strncpy(event.topic, topic, sizeof(event.topic) - 1);
    std::strncpy(event.payload, payload, sizeof(event.payload) - 1);
    event.retained = retained;
    tail = (tail + 1U) % depth;
    ++count;
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    xSemaphoreGive(_outbound_mutex);
#endif
#if defined(UNIT_TEST_HOST) && !defined(MQTT_INTEGRATION_TARGET)
    lock.unlock();
    serviceOutgoingEvents();
#endif
    return true;
}

bool MqttClient::_reserveCommandAck() {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (_outbound_mutex == nullptr || xSemaphoreTake(_outbound_mutex, portMAX_DELAY) != pdTRUE) return false;
#else
    std::lock_guard<std::mutex> lock(_outbound_mutex);
#endif
    if (_outbound_ack_count + _reserved_ack_count == MQTT_OUTBOUND_ACK_QUEUE_DEPTH) {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
        xSemaphoreGive(_outbound_mutex);
#endif
        return false;
    }
    ++_reserved_ack_count;
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    xSemaphoreGive(_outbound_mutex);
#endif
    return true;
}

bool MqttClient::_publishReservedCommandAck(const char* command_id, const char* status,
                                            uint8_t node_id, const char* reason) {
    if (!isValidMqttCommandId(command_id)) return false;
    JsonDocument doc;
    doc["command_id"] = command_id;
    doc["status"] = status;
    if (node_id > 0) doc["node_id"] = node_id;
    if (reason) doc["reason"] = reason;
    char topic[MQTT_TOPIC_BUFFER_SIZE] = {};
    char payload[256] = {};
    const int written = snprintf(topic, sizeof(topic), "%s/%s%s%s", MQTT_TOPIC_BASE,
                                 _config.device_id, MQTT_ACK_PREFIX_SUFFIX, command_id);
    const size_t bytes = serializeJson(doc, payload, sizeof(payload));
    if (written < 0 || static_cast<size_t>(written) >= sizeof(topic) || bytes == 0 || bytes >= sizeof(payload)) return false;
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (_outbound_mutex == nullptr || xSemaphoreTake(_outbound_mutex, portMAX_DELAY) != pdTRUE) return false;
#else
    std::lock_guard<std::mutex> lock(_outbound_mutex);
#endif
    if (_reserved_ack_count == 0 || _outbound_ack_count == MQTT_OUTBOUND_ACK_QUEUE_DEPTH) {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
        xSemaphoreGive(_outbound_mutex);
#endif
        return false;
    }
    --_reserved_ack_count;
    MqttOutboundEvent& event = _outbound_ack_events[_outbound_ack_tail];
    std::strncpy(event.topic, topic, sizeof(event.topic) - 1);
    std::strncpy(event.payload, payload, sizeof(event.payload) - 1);
    // Retain the terminal admission decision per command_id. A client that
    // retries after a disconnect/reboot can reconcile using this idempotency
    // key instead of treating a duplicate socket write as a new command.
    event.retained = true;
    _outbound_ack_tail = (_outbound_ack_tail + 1U) % MQTT_OUTBOUND_ACK_QUEUE_DEPTH;
    ++_outbound_ack_count;
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    xSemaphoreGive(_outbound_mutex);
#endif
    return true;
}

bool MqttClient::_peekOutboundEvent(MqttOutboundEvent& event, bool& critical, bool& backpressure_failure) {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (_outbound_mutex == nullptr || xSemaphoreTake(_outbound_mutex, pdMS_TO_TICKS(10)) != pdTRUE) return false;
#else
    std::lock_guard<std::mutex> lock(_outbound_mutex);
#endif
    critical = _outbound_ack_count > 0;
    backpressure_failure = !critical && _backpressure_failure_count > 0;
    const size_t count = critical ? _outbound_ack_count :
                         backpressure_failure ? _backpressure_failure_count : _outbound_telemetry_count;
    if (count == 0) {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
        xSemaphoreGive(_outbound_mutex);
#endif
        return false;
    }
    event = critical ? _outbound_ack_events[_outbound_ack_head]
          : backpressure_failure ? _backpressure_failure_events[_backpressure_failure_head]
          : _outbound_telemetry_events[_outbound_telemetry_head];
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    xSemaphoreGive(_outbound_mutex);
#endif
    return true;
}

bool MqttClient::_discardOutboundEvent(bool critical, bool backpressure_failure) {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (_outbound_mutex == nullptr || xSemaphoreTake(_outbound_mutex, pdMS_TO_TICKS(10)) != pdTRUE) return false;
#else
    std::lock_guard<std::mutex> lock(_outbound_mutex);
#endif
    size_t& head = critical ? _outbound_ack_head :
                   backpressure_failure ? _backpressure_failure_head : _outbound_telemetry_head;
    size_t& count = critical ? _outbound_ack_count :
                    backpressure_failure ? _backpressure_failure_count : _outbound_telemetry_count;
    const size_t depth = critical ? MQTT_OUTBOUND_ACK_QUEUE_DEPTH :
                         backpressure_failure ? MQTT_BACKPRESSURE_FAILURE_QUEUE_DEPTH : MQTT_OUTBOUND_TELEMETRY_QUEUE_DEPTH;
    if (count == 0) {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
        xSemaphoreGive(_outbound_mutex);
#endif
        return false;
    }
    head = (head + 1U) % depth;
    --count;
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    xSemaphoreGive(_outbound_mutex);
#endif
    return true;
}

bool MqttClient::_queueJsonEvent(const char* topic, const JsonDocument& doc, bool retained) {
    char payload[MQTT_TELEMETRY_PAYLOAD_SIZE];
    const size_t bytes = serializeJson(doc, payload, sizeof(payload));
    return bytes > 0 && bytes < sizeof(payload) && _enqueueOutboundEvent(topic, payload, retained);
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
    if (!_enqueueOutboundEvent(topic, payload, MQTT_PUBLISH_RETAIN)) return false;
    _last_heartbeat_ms = now;
#if defined(UNIT_TEST_HOST) && !defined(MQTT_INTEGRATION_TARGET)
    return _last_outbound_publish_ok;
#else
    return true;
#endif
}

bool MqttClient::publishConnectedHeartbeat() {
    return publishHeartbeat();
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
    return bytes > 0 && bytes < sizeof(payload) &&
           _enqueueOutboundEvent(topic, payload, MQTT_PUBLISH_RETAIN);
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
    return bytes > 0 && bytes < sizeof(payload) &&
           _enqueueOutboundEvent(topic, payload, MQTT_PUBLISH_RETAIN);
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
    return bytes > 0 && bytes < sizeof(payload) &&
           _enqueueOutboundEvent(topic, payload, MQTT_PUBLISH_RETAIN, true);
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
    return bytes > 0 && bytes < sizeof(payload) &&
           _enqueueOutboundEvent(topic, payload, MQTT_PUBLISH_RETAIN);
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
    if (bytes > 0 && bytes < sizeof(payload)) _enqueueOutboundEvent(topic, payload, MQTT_PUBLISH_RETAIN);
}

bool MqttClient::_enqueueInboundRejection(const JsonDocument& doc, uint8_t node_id, const char* reason) {
    const char* command_id = doc["command_id"] | "invalid-command";
    _inbound_rejected.fetch_add(1);
    const bool queued = _reserveCommandAck() && _publishReservedCommandAck(command_id, "REJECTED", node_id,
                                                                             reason ? reason : "Invalid command");
#if defined(UNIT_TEST_HOST) && !defined(MQTT_INTEGRATION_TARGET)
    serviceOutgoingEvents();
#endif
    return queued;
}

bool MqttClient::_hasValidCommandEnvelope(const JsonDocument& doc, const char*& command_id) const {
    command_id = doc["command_id"];
    return isValidMqttCommandId(command_id) &&
           doc["version"].is<uint16_t>() && doc["version"].as<uint16_t>() > 0;
}

bool MqttClient::_enqueueAssignmentCommand(const JsonDocument& doc) {
    const char* cmd_id = nullptr;
    if (!_hasValidCommandEnvelope(doc, cmd_id) || !doc["node_id"].is<uint8_t>() || !doc["group_id"].is<uint8_t>()) return false;
    MqttInboundCommand command{};
    command.type = MqttInboundCommandType::ASSIGNMENT;
    command.node_id = doc["node_id"].as<uint8_t>();
    command.group_id = doc["group_id"].as<uint8_t>();
    std::strncpy(command.command_id, cmd_id, sizeof(command.command_id) - 1);
    return _enqueueInboundCommand(command);
}

bool MqttClient::_enqueueFlowPolicyCommand(const JsonDocument& doc) {
    const char* cmd_id = nullptr;
    if (!_hasValidCommandEnvelope(doc, cmd_id) ||
        !doc["node_id"].is<uint8_t>() || !doc["policy_version"].is<uint32_t>() ||
        !doc["treatment_version_id"].is<uint32_t>() || !doc["calibration_id"].is<uint32_t>() ||
        !doc["min_flow_lpm_x100"].is<uint16_t>() || !doc["max_off_flow_lpm_x100"].is<uint16_t>() ||
        !doc["max_flow_lpm_x100"].is<uint16_t>() || !doc["flow_start_timeout_ms"].is<uint32_t>() ||
        !doc["run_lease_ms"].is<uint32_t>() || !doc["max_on_duration_ms"].is<uint32_t>()) {
        return false;
    }
    MqttInboundCommand command{};
    command.type = MqttInboundCommandType::FLOW_POLICY;
    command.node_id = doc["node_id"].as<uint8_t>();
    command.values[0] = doc["run_lease_ms"].as<uint32_t>();
    command.values[1] = doc["max_on_duration_ms"].as<uint32_t>();
    command.values[2] = doc["min_flow_lpm_x100"].as<uint16_t>();
    command.values[3] = doc["max_off_flow_lpm_x100"].as<uint16_t>();
    command.values[4] = doc["max_flow_lpm_x100"].as<uint16_t>();
    command.values[5] = doc["flow_start_timeout_ms"].as<uint32_t>();
    command.values[6] = doc["policy_version"].as<uint32_t>();
    command.values[7] = doc["treatment_version_id"].as<uint32_t>();
    command.values[8] = doc["calibration_id"].as<uint32_t>();
    std::strncpy(command.command_id, cmd_id, sizeof(command.command_id) - 1);
    return _enqueueInboundCommand(command);
}

bool MqttClient::_enqueueTreatmentCommand(const JsonDocument& doc) {
    const char* cmd_id = nullptr;
    if (!_hasValidCommandEnvelope(doc, cmd_id) ||
        !doc["group_id"].is<uint8_t>() || !doc["season_id"].is<uint32_t>() ||
        !doc["treatment_version_id"].is<uint32_t>() || !doc["treatment_version"].is<uint32_t>() ||
        !doc["treatment_status"].is<const char*>()) {
        return false;
    }
    const char* status = doc["treatment_status"];
    if (strcmp(status, "PUBLISHED") != 0) return false;
    if (!doc["schedule"]["spray_day_s"].is<uint32_t>() || !doc["schedule"]["cooldown_day_s"].is<uint32_t>() ||
        !doc["schedule"]["spray_night_s"].is<uint32_t>() || !doc["schedule"]["cooldown_night_s"].is<uint32_t>()) {
        return false;
    }
    MqttInboundCommand command{};
    command.type = MqttInboundCommandType::TREATMENT;
    command.group_id = doc["group_id"].as<uint8_t>();
    command.values[0] = doc["season_id"].as<uint32_t>();
    command.values[1] = doc["treatment_version_id"].as<uint32_t>();
    command.values[2] = doc["treatment_version"].as<uint32_t>();
    command.values[3] = doc["schedule"]["spray_day_s"].as<uint32_t>();
    command.values[4] = doc["schedule"]["cooldown_day_s"].as<uint32_t>();
    command.values[5] = doc["schedule"]["spray_night_s"].as<uint32_t>();
    command.values[6] = doc["schedule"]["cooldown_night_s"].as<uint32_t>();
    std::strncpy(command.command_id, cmd_id, sizeof(command.command_id) - 1);
    return _enqueueInboundCommand(command);
}

bool MqttClient::_enqueueNodeOverrideCommand(uint8_t node_id, const JsonDocument& doc) {
    const char* cmd_id = nullptr;
    if (!_hasValidCommandEnvelope(doc, cmd_id)) {
        return false;
    }
    const char* state_str = doc["desired_state"] | doc["state"];
    if (!state_str) {
        return false;
    }
    bool is_on = (strcmp(state_str, "ON") == 0 || strcmp(state_str, "on") == 0);
    bool is_off = (strcmp(state_str, "OFF") == 0 || strcmp(state_str, "off") == 0);
    if (!is_on && !is_off) {
        return false;
    }
    const char* src = doc["source"].as<const char*>();
    if (!src || (strcmp(src, "MANUAL_OVERRIDE") != 0 && strcmp(src, "FAIL_SAFE") != 0)) return false;
    if (is_on && (!doc["run_lease_ms"].is<uint32_t>() || doc["run_lease_ms"].as<uint32_t>() == 0)) {
        return false;
    }
    if (doc["run_lease_ms"].is<uint32_t>()) {
        uint32_t lease = doc["run_lease_ms"].as<uint32_t>();
        if (lease == 0 || lease > DEFAULT_MAX_ON_DURATION_MS) {
            return false;
        }
    }
    if (doc["override_duration_ms"].is<uint32_t>()) {
        uint32_t override_dur = doc["override_duration_ms"].as<uint32_t>();
        if (override_dur == 0 || override_dur > 86400000U) {
            return false;
        }
    }
    MqttInboundCommand command{};
    command.type = MqttInboundCommandType::NODE_OVERRIDE;
    command.node_id = node_id;
    command.desired_state = is_on ? NodePumpState::ON : NodePumpState::OFF;
    std::strncpy(command.source, src, sizeof(command.source) - 1);
    if (doc["run_lease_ms"].is<uint32_t>()) {
        command.values[0] = doc["run_lease_ms"].as<uint32_t>();
    }
    if (doc["override_duration_ms"].is<uint32_t>()) {
        command.values[1] = doc["override_duration_ms"].as<uint32_t>();
    }
    std::strncpy(command.command_id, cmd_id, sizeof(command.command_id) - 1);
    return _enqueueInboundCommand(command);
}

bool MqttClient::_enqueueGroupControlCommand(uint8_t group_id, const JsonDocument& doc) {
    const char* cmd_id = nullptr;
    if (!_hasValidCommandEnvelope(doc, cmd_id)) {
        return false;
    }
    const char* action_str = doc["action"] | doc["state"];
    if (!action_str) {
        return false;
    }
    bool is_on = (strcmp(action_str, "ON") == 0 || strcmp(action_str, "on") == 0);
    bool is_off = (strcmp(action_str, "OFF") == 0 || strcmp(action_str, "off") == 0);
    if (!is_on && !is_off) {
        return false;
    }
    const char* src = doc["source"].as<const char*>();
    if (!src || (strcmp(src, "MANUAL_OVERRIDE") != 0 && strcmp(src, "FAIL_SAFE") != 0)) return false;
    if (is_on && (!doc["run_lease_ms"].is<uint32_t>() || doc["run_lease_ms"].as<uint32_t>() == 0)) {
        return false;
    }
    MqttInboundCommand command{};
    command.type = MqttInboundCommandType::GROUP_CONTROL;
    command.group_id = group_id;
    command.desired_state = is_on ? NodePumpState::ON : NodePumpState::OFF;
    std::strncpy(command.source, src, sizeof(command.source) - 1);
    if (doc["run_lease_ms"].is<uint32_t>()) {
        const uint32_t lease = doc["run_lease_ms"].as<uint32_t>();
        if (lease == 0 || lease > DEFAULT_MAX_ON_DURATION_MS) return false;
        command.values[0] = lease;
    }
    if (doc["override_duration_ms"].is<uint32_t>()) {
        const uint32_t duration = doc["override_duration_ms"].as<uint32_t>();
        if (duration == 0 || duration > 86400000U) return false;
        command.values[1] = duration;
    }
    std::strncpy(command.command_id, cmd_id, sizeof(command.command_id) - 1);
    return _enqueueInboundCommand(command);
}

void MqttClient::_parseNodeTopic(const char* ptr, const JsonDocument& doc) {
    const char* slash = strchr(ptr, '/');
    if (!slash || strcmp(slash, "/override") != 0) return;
    char id_buf[16] = {};
    size_t id_len = slash - ptr;
    if (id_len > 0 && id_len < sizeof(id_buf)) {
        std::memcpy(id_buf, ptr, id_len);
        uint8_t node_id = 0;
        if (parseBoundedUint(id_buf, 1, PRODUCTION_MAX_NODES, node_id)) {
            if (!_enqueueNodeOverrideCommand(node_id, doc)) {
                _enqueueInboundRejection(doc, node_id, "Invalid command or inbound queue full");
            }
        } else {
            _enqueueInboundRejection(doc, 0, "Invalid node_id in topic");
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
            if (!_enqueueGroupControlCommand(group_id, doc)) {
                _enqueueInboundRejection(doc, 0, "Invalid command or inbound queue full");
            }
        } else {
            _enqueueInboundRejection(doc, 0, "Invalid group_id in topic");
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
        if (!_instance->_enqueueTreatmentCommand(doc)) {
            _instance->_enqueueInboundRejection(doc, 0, "Invalid command or inbound queue full");
        }
    } else if (strcmp(sub_topic, "config/assignment") == 0) {
        if (!_instance->_enqueueAssignmentCommand(doc)) {
            _instance->_enqueueInboundRejection(doc, 0, "Invalid command or inbound queue full");
        }
    } else if (strcmp(sub_topic, "config/flow-policy") == 0) {
        if (!_instance->_enqueueFlowPolicyCommand(doc)) {
            _instance->_enqueueInboundRejection(doc, 0, "Invalid command or inbound queue full");
        }
    } else if (strncmp(sub_topic, "node/", 5) == 0) {
        _instance->_parseNodeTopic(sub_topic + 5, doc);
    } else if (strncmp(sub_topic, "group/", 6) == 0) {
        _instance->_parseGroupTopic(sub_topic + 6, doc);
    }
}

void MqttClient::_applyInboundCommand(const MqttInboundCommand& command) {
    switch (command.type) {
        case MqttInboundCommandType::ASSIGNMENT: {
            const bool accepted = _command_manager &&
                _command_manager->requestNodeReassignment(command.node_id, command.group_id, command.command_id);
            _publishReservedCommandAck(command.command_id, accepted ? "ACCEPTED" : "REJECTED", command.node_id,
                              accepted ? "Safe-off queued; mapping commits after RF OFF ACK" : "Group assignment mutation failed");
            return;
        }
        case MqttInboundCommandType::FLOW_POLICY: {
            const FlowPolicyProvenance source{command.values[6], command.values[7], command.values[8]};
            const bool accepted = _command_manager && _command_manager->provisionNodeControlPolicy(
                command.node_id, command.values[0], command.values[1], static_cast<uint16_t>(command.values[2]),
                static_cast<uint16_t>(command.values[3]), static_cast<uint16_t>(command.values[4]), command.values[5], source);
            _publishReservedCommandAck(command.command_id, accepted ? "ACCEPTED" : "REJECTED", command.node_id,
                              accepted ? "Authenticated flow policy provisioned" : "Invalid flow policy limits");
            return;
        }
        case MqttInboundCommandType::TREATMENT: {
            PublishedTreatmentAssignment assignment{};
            assignment.season_id = command.values[0];
            assignment.treatment_version_id = command.values[1];
            assignment.version = command.values[2];
            assignment.profile = GroupProfile{command.values[3], command.values[4], command.values[5], command.values[6]};
            const bool accepted = _group_scheduler && _group_scheduler->applyPublishedTreatment(command.group_id, assignment);
            _publishReservedCommandAck(command.command_id, accepted ? "ACCEPTED" : "REJECTED", 0,
                              "Published treatment assignment validation result");
            return;
        }
        case MqttInboundCommandType::NODE_OVERRIDE: {
            const ExternalOverridePolicy policy{command.source, command.values[0], command.values[1]};
            const bool accepted = _command_manager &&
                _command_manager->queueExternalNodeCommand(command.node_id, command.desired_state, command.command_id, &policy);
            _publishReservedCommandAck(command.command_id, accepted ? "ACCEPTED" : "REJECTED", command.node_id,
                              accepted ? "Node override accepted and queued" : "Node override mutation failed");
            return;
        }
        case MqttInboundCommandType::GROUP_CONTROL: {
            const ExternalOverridePolicy policy{command.source, command.values[0], command.values[1]};
            const bool accepted = _command_manager &&
                _command_manager->queueExternalGroupCommand(command.group_id, command.desired_state, command.command_id, &policy);
            _publishReservedCommandAck(command.command_id, accepted ? "ACCEPTED" : "REJECTED", 0,
                              accepted ? "Group control accepted and queued" : "Group prepare failed; no node queued");
            return;
        }
        case MqttInboundCommandType::REJECTION: return;
    }
}

void MqttClient::serviceIncomingCommands() {
    MqttInboundCommand command{};
    while (_dequeueInboundCommand(command)) _applyInboundCommand(command);
#if defined(UNIT_TEST_HOST) && !defined(MQTT_INTEGRATION_TARGET)
    // Native tests have no dedicated MQTT task; emulate its bounded drain so
    // existing facade tests can observe publication without changing the
    // production ownership contract.
    serviceOutgoingEvents();
#endif
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
