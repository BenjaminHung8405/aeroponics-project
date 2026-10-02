#include "mqtt_client.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "core/clock_trust.h"

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

namespace
{
    constexpr const char *TAG = "MQTT";

    uint32_t getSystemMillis()
    {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
        return millis();
#else
        using namespace std::chrono;
        static const auto start_time = steady_clock::now();
        return static_cast<uint32_t>(duration_cast<milliseconds>(steady_clock::now() - start_time).count());
#endif
    }

    bool fitsCString(const char *value, size_t capacity)
    {
        return value != nullptr && strnlen(value, capacity) < capacity;
    }

    bool isValidDeviceId(const char *value)
    {
        const size_t length = strnlen(value, MQTT_DEVICE_ID_MAX_LENGTH + 1);
        if (length == 0 || length > MQTT_DEVICE_ID_MAX_LENGTH)
            return false;
        for (size_t i = 0; i < length; ++i)
        {
            const char c = value[i];
            if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                  (c >= '0' && c <= '9') || c == '_' || c == '-'))
                return false;
        }
        return true;
    }

    bool parseBoundedUint(const char *str, uint8_t min_val, uint8_t max_val, uint8_t &out_val)
    {
        if (!str || *str == '\0')
            return false;
        char *endptr = nullptr;
        unsigned long val = std::strtoul(str, &endptr, 10);
        if (endptr == str || *endptr != '\0')
            return false;
        if (val < min_val || val > max_val)
            return false;
        out_val = static_cast<uint8_t>(val);
        return true;
    }

    static const char *lifecycleEventToString(LifecycleEvent event)
    {
        switch (event)
        {
        case LifecycleEvent::NONE:
            return "NONE";
        case LifecycleEvent::ACCEPTED:
            return "ACCEPTED";
        case LifecycleEvent::REJECTED:
            return "REJECTED";
        case LifecycleEvent::RF_ACKED:
            return "RF_ACKED";
        case LifecycleEvent::PUMP_FEEDBACK_ON:
            return "PUMP_FEEDBACK_ON";
        case LifecycleEvent::CURRENT_DETECTED:
            return "CURRENT_DETECTED";
        case LifecycleEvent::FLOW_CONFIRMED:
            return "FLOW_CONFIRMED";
        case LifecycleEvent::COMPLETED:
            return "COMPLETED";
        case LifecycleEvent::RF_TIMEOUT_OR_NACK:
            return "RF_TIMEOUT_OR_NACK";
        case LifecycleEvent::SAFE_OFF_UNCONFIRMED:
            return "SAFE_OFF_UNCONFIRMED";
        case LifecycleEvent::FAULT_LATCHED:
            return "FAULT_LATCHED";
        case LifecycleEvent::LEASE_EXPIRED_SAFE_OFF:
            return "LEASE_EXPIRED_SAFE_OFF";
        case LifecycleEvent::RESET_REJECTED:
            return "RESET_REJECTED";
        default:
            return "UNKNOWN";
        }
    }
} // namespace

bool MqttClient::publishLifecycleEvent(uint8_t node_id,
                                       const char *mqtt_command_id,
                                       LifecycleEvent event)
{
    if (!isConnected())
        return false;

    char topic[MQTT_TOPIC_BUFFER_SIZE];
    const int written = snprintf(topic, sizeof(topic),
                                 "aeroponics/v1/node/%u/event", node_id);
    if (written < 0 || static_cast<size_t>(written) >= sizeof(topic))
        return false;

    char payload[MQTT_TELEMETRY_DOC_SIZE];
    const char *event_str = lifecycleEventToString(event);
    const char *cmd_id = mqtt_command_id ? mqtt_command_id : "";

    const int payload_written = snprintf(payload, sizeof(payload),
                                         "{\"schema_version\":\"1.0\",\"command_id\":\"%s\",\"node_id\":%u,"
                                         "\"event\":\"%s\",\"gateway_timestamp_ms\":%lu}",
                                         cmd_id, node_id, event_str,
                                         static_cast<unsigned long>(getSystemMillis()));
    if (payload_written < 0 || static_cast<size_t>(payload_written) >= sizeof(payload))
        return false;

    // CRITICAL: Non-retained for transactional topics (per contract 7)
    return _enqueueOutboundEvent(topic, payload, false);
}

MqttClient *MqttClient::_instance = nullptr;

MqttClient::MqttClient()
    :
#if defined(ESP_PLATFORM) || defined(ARDUINO)
      _wifi_client(),
      _pubsub(_wifi_client),
#else
      _pubsub(),
#endif
      _config{nullptr, 0, nullptr, nullptr, nullptr},
      _rtc(nullptr), _rtc_telemetry(nullptr),
      _registry(nullptr), _command_manager(nullptr), _group_scheduler(nullptr),
      _last_heartbeat_ms(0), _is_initialized(false)
#if defined(UNIT_TEST_HOST) && !defined(MQTT_INTEGRATION_TARGET)
      ,
      _mock_unix_time(0)
#endif
{
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    _pubsub.setClient(_wifi_client);
    _inbound_mutex = xSemaphoreCreateMutex();
    _outbound_mutex = xSemaphoreCreateMutex();
#endif
    _instance = this;
}

void MqttClient::reset()
{
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
    _dedup_cache.clear();
    std::memset(_last_treatment_version, 0, sizeof(_last_treatment_version));
    _last_assignment_version = 0;
    std::memset(_last_policy_version, 0, sizeof(_last_policy_version));
    _is_initialized = false;
}

MqttClient::~MqttClient()
{
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (_inbound_mutex != nullptr)
        vSemaphoreDelete(_inbound_mutex);
    if (_outbound_mutex != nullptr)
        vSemaphoreDelete(_outbound_mutex);
#endif
    if (_instance == this)
        _instance = nullptr;
}

bool MqttClient::begin(MqttConfig config, IClock *rtc, NodeRegistry *registry, CommandManager *command_manager,
                       GroupScheduleManager *group_scheduler)
{
    if (!config.broker_host || !config.device_id || config.broker_host[0] == '\0' ||
        config.device_id[0] == '\0' || !isValidDeviceId(config.device_id) ||
        !fitsCString(config.broker_host, MQTT_BROKER_HOST_BUFFER_SIZE) ||
        !fitsCString(config.username, MQTT_USERNAME_BUFFER_SIZE) ||
        !fitsCString(config.password, MQTT_PASSWORD_BUFFER_SIZE) ||
        std::strcmp(config.username, config.device_id) != 0)
    {
        ESP_LOGE(TAG, "Invalid MQTT configuration or dependencies");
        reset();
        return false;
    }
    _config = config;
    _rtc = rtc;
    _registry = registry;
    _command_manager = command_manager;
    _group_scheduler = group_scheduler;
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    _pubsub.setClient(_wifi_client);
#endif
    if (_command_manager != nullptr)
        _command_manager->setOutcomeSink(this);
    _is_initialized = true;
    return true;
}

bool MqttClient::_buildTopic(char *buffer, size_t buffer_size, const char *suffix) const
{
    const int written = snprintf(buffer, buffer_size, "%s/%s%s", MQTT_TOPIC_BASE,
                                 _config.device_id, suffix);
    return written >= 0 && static_cast<size_t>(written) < buffer_size;
}

bool MqttClient::_buildClientId(char *buffer, size_t buffer_size) const
{
    const int written = snprintf(buffer, buffer_size, "%s", _config.device_id);
    return written >= 0 && static_cast<size_t>(written) < buffer_size;
}

bool MqttClient::_buildLwtPayload(char *buffer, size_t buffer_size) const
{
    JsonDocument doc;
    doc["status"] = "offline";
    doc["device_id"] = _config.device_id;
    doc["timestamp_utc"] = nullptr;
    const size_t bytes = serializeJson(doc, buffer, buffer_size);
    return bytes > 0 && bytes < buffer_size;
}

bool MqttClient::connect()
{
    if (!_is_initialized)
        return false;
    _connected.store(false);
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (WiFi.status() != WL_CONNECTED)
        return false;
    _pubsub.setClient(_wifi_client);
#endif
    char lwt_payload[MQTT_LWT_DOC_SIZE];
    char lwt_topic[MQTT_TOPIC_BUFFER_SIZE];
    char client_id[MQTT_CLIENT_ID_BUFFER_SIZE];
    if (!_buildLwtPayload(lwt_payload, sizeof(lwt_payload)) ||
        !_buildTopic(lwt_topic, sizeof(lwt_topic), MQTT_STATUS_SUFFIX) ||
        !_buildClientId(client_id, sizeof(client_id)))
    {
        ESP_LOGE(TAG, "MQTT connection fields exceed their configured buffers");
        return false;
    }
    _pubsub.setServer(_config.broker_host, _config.broker_port);
    _pubsub.setCallback(_onMessage);
    _pubsub.setBufferSize(MQTT_BUFFER_SIZE);
    _pubsub.setKeepAlive(MQTT_KEEPALIVE_S);
    if (!_pubsub.connect(client_id, _config.username, _config.password, lwt_topic, MQTT_LWT_QOS,
                         MQTT_LWT_RETAIN, lwt_payload))
    {
        ESP_LOGE(TAG, "PubSubClient connect failed with state: %d", _pubsub.state());
        return false;
    }
    _connected.store(true);
    if (!_subscribeCommandTopics())
    {
        _pubsub.disconnect();
        _connected.store(false);
        return false;
    }
    if (!publishConnectedHeartbeat())
    {
        _pubsub.disconnect();
        _connected.store(false);
        return false;
    }
    return true;
}

bool MqttClient::_subscribeCommandTopics()
{
    char topic_buf[MQTT_TOPIC_BUFFER_SIZE];

    const int treatment_written = snprintf(topic_buf, sizeof(topic_buf), "%s/%s%s",
                                           MQTT_TOPIC_BASE, _config.device_id, MQTT_COMMAND_TREATMENT_SUFFIX);
    if (treatment_written < 0 || static_cast<size_t>(treatment_written) >= sizeof(topic_buf) ||
        !_pubsub.subscribe(topic_buf, MQTT_COMMAND_QOS))
    {
        return false;
    }

    const int assign_written = snprintf(topic_buf, sizeof(topic_buf), "%s/%s%s",
                                        MQTT_TOPIC_BASE, _config.device_id, MQTT_COMMAND_ASSIGNMENT_SUFFIX);
    if (assign_written < 0 || static_cast<size_t>(assign_written) >= sizeof(topic_buf) ||
        !_pubsub.subscribe(topic_buf, MQTT_COMMAND_QOS))
    {
        return false;
    }

    const int group_state_written = snprintf(topic_buf, sizeof(topic_buf), "%s/%s%s",
                                             MQTT_TOPIC_BASE, _config.device_id, MQTT_COMMAND_GROUP_STATE_SUFFIX);
    if (group_state_written < 0 || static_cast<size_t>(group_state_written) >= sizeof(topic_buf) ||
        !_pubsub.subscribe(topic_buf, MQTT_COMMAND_QOS))
        return false;

    const int flow_policy_written = snprintf(topic_buf, sizeof(topic_buf), "%s/%s%s",
                                             MQTT_TOPIC_BASE, _config.device_id,
                                             MQTT_COMMAND_FLOW_POLICY_SUFFIX);
    if (flow_policy_written < 0 || static_cast<size_t>(flow_policy_written) >= sizeof(topic_buf) ||
        !_pubsub.subscribe(topic_buf, MQTT_COMMAND_QOS))
    {
        return false;
    }

    const int node_ovr_written = snprintf(topic_buf, sizeof(topic_buf), "%s/%s%s%s/override",
                                          MQTT_TOPIC_BASE, _config.device_id, MQTT_COMMAND_NODE_OVERRIDE_SUFFIX, MQTT_WILDCARD_SINGLE_LEVEL);
    if (node_ovr_written < 0 || static_cast<size_t>(node_ovr_written) >= sizeof(topic_buf) ||
        !_pubsub.subscribe(topic_buf, MQTT_COMMAND_QOS))
    {
        return false;
    }

    const int grp_ctrl_written = snprintf(topic_buf, sizeof(topic_buf), "%s/%s%s%s/control",
                                          MQTT_TOPIC_BASE, _config.device_id, MQTT_COMMAND_GROUP_CONTROL_SUFFIX, MQTT_WILDCARD_SINGLE_LEVEL);
    if (grp_ctrl_written < 0 || static_cast<size_t>(grp_ctrl_written) >= sizeof(topic_buf) ||
        !_pubsub.subscribe(topic_buf, MQTT_COMMAND_QOS))
    {
        return false;
    }

    const int gw_cmd_written = snprintf(topic_buf, sizeof(topic_buf), "%s/%s%s%s",
                                        MQTT_TOPIC_BASE, _config.device_id, MQTT_COMMAND_GATEWAY_SUFFIX, MQTT_WILDCARD_SINGLE_LEVEL);
    if (gw_cmd_written < 0 || static_cast<size_t>(gw_cmd_written) >= sizeof(topic_buf) ||
        !_pubsub.subscribe(topic_buf, MQTT_COMMAND_QOS))
    {
        return false;
    }

    const int clock_written = snprintf(topic_buf, sizeof(topic_buf), "%s/%s%s",
                                       MQTT_TOPIC_BASE, _config.device_id, MQTT_COMMAND_CLOCK_SUFFIX);
    if (clock_written < 0 || static_cast<size_t>(clock_written) >= sizeof(topic_buf) ||
        !_pubsub.subscribe(topic_buf, MQTT_COMMAND_QOS))
    {
        return false;
    }

    _pubsub.subscribe("aeroponics/command/node/+/override", MQTT_COMMAND_QOS);

    // Subscribe to retained config/control_slots topic so firmware learns the
    // dynamic slot→node/group mapping configured in the Web UI.
    // The retained flag on the broker ensures delivery on every reconnect.
    char slot_cfg_topic[MQTT_TOPIC_BUFFER_SIZE];
    const int slot_written = snprintf(slot_cfg_topic, sizeof(slot_cfg_topic), "%s/%s%s",
                                      MQTT_TOPIC_BASE, _config.device_id, MQTT_CONFIG_CONTROL_SLOTS_SUFFIX);
    if (slot_written < 0 || static_cast<size_t>(slot_written) >= sizeof(slot_cfg_topic) ||
        !_pubsub.subscribe(slot_cfg_topic, MQTT_COMMAND_QOS))
        return false;

    return true;
}

void MqttClient::loop()
{
    // PubSubClient ownership is restricted to this MQTT task. No other
    // thread may call loop(), publish(), subscribe(), or disconnect().
    if (_pubsub.connected())
        _pubsub.loop();
    _connected.store(_pubsub.connected());
}

void MqttClient::serviceOutgoingEvents()
{
    if (!_pubsub.connected())
        return;
    _publishQueueOverflowAudit();
    MqttOutboundEvent event{};
    size_t processed = 0;
    bool critical = false;
    bool backpressure_failure = false;
    while (processed < MQTT_OUTBOUND_DRAIN_BUDGET && _peekOutboundEvent(event, critical, backpressure_failure))
    {
        // Peek before publish. A socket failure therefore leaves the event in
        // its lane for the next MQTT tick instead of silently losing ACKs.
        if (!_pubsub.publish(event.topic, event.payload, event.retained))
        {
            _last_outbound_publish_ok = false;
            _outbound_dropped.fetch_add(1);
            break;
        }
        _discardOutboundEvent(critical, backpressure_failure);
        _last_outbound_publish_ok = true;
        ++processed;
    }
}

void MqttClient::_publishQueueOverflowAudit()
{
    const uint32_t now = getSystemMillis();
    if (now - _last_queue_audit_ms < MQTT_QUEUE_AUDIT_INTERVAL_MS)
        return;
    const uint32_t inbound_rejected = _inbound_rejected.load();
    const uint32_t outbound_dropped = _outbound_dropped.load();
    if (inbound_rejected == 0 && outbound_dropped == 0)
        return;
    char topic[MQTT_TOPIC_BUFFER_SIZE];
    if (!_buildTopic(topic, sizeof(topic), "/telemetry/audit"))
        return;
    char payload[MQTT_TELEMETRY_PAYLOAD_SIZE];
    const int written = snprintf(payload, sizeof(payload),
                                 "{\"event\":\"MQTT_QUEUE_OVERFLOW\",\"inbound_rejected\":%u,\"outbound_dropped\":%u}",
                                 static_cast<unsigned>(inbound_rejected), static_cast<unsigned>(outbound_dropped));
    if (written > 0 && static_cast<size_t>(written) < sizeof(payload) &&
        _enqueueOutboundEvent(topic, payload, MQTT_PUBLISH_RETAIN, true))
    {
        _inbound_rejected.fetch_sub(inbound_rejected);
        _outbound_dropped.fetch_sub(outbound_dropped);
        _last_queue_audit_ms = now;
    }
}

bool MqttClient::_enqueueInboundCommand(const MqttInboundCommand &command)
{
    if (!_reserveCommandAck())
        return _enqueueBackpressureRejection(command.command_id, command.node_id);
    MqttInboundCommand reserved_command = command;
    reserved_command.ack_reserved = true;
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (_inbound_mutex == nullptr || xSemaphoreTake(_inbound_mutex, portMAX_DELAY) != pdTRUE)
    {
        return _publishReservedCommandAck(command.command_id, "REJECTED", command.node_id,
                                          "Inbound command lock unavailable");
    }
#else
    std::lock_guard<std::mutex> lock(_inbound_mutex);
#endif
    if (_inbound_count == MQTT_INBOUND_COMMAND_QUEUE_DEPTH)
    {
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

bool MqttClient::_enqueueBackpressureRejection(const char *command_id, uint8_t node_id)
{
    if (!isValidMqttCommandId(command_id))
        return false;
    JsonDocument doc;
    doc["command_id"] = command_id;
    doc["status"] = "REJECTED";
    if (node_id > 0)
        doc["node_id"] = node_id;
    doc["reason"] = "Command ACK admission lane full";
    char topic[MQTT_TOPIC_BUFFER_SIZE] = {};
    char payload[256] = {};
    const int written = snprintf(topic, sizeof(topic), "%s/%s%s%s", MQTT_TOPIC_BASE,
                                 _config.device_id, MQTT_ACK_PREFIX_SUFFIX, command_id);
    const size_t bytes = serializeJson(doc, payload, sizeof(payload));
    if (written < 0 || static_cast<size_t>(written) >= sizeof(topic) || bytes == 0 || bytes >= sizeof(payload))
        return false;
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (_outbound_mutex == nullptr || xSemaphoreTake(_outbound_mutex, portMAX_DELAY) != pdTRUE)
        return false;
#else
    std::lock_guard<std::mutex> lock(_outbound_mutex);
#endif
    if (_backpressure_failure_count == MQTT_BACKPRESSURE_FAILURE_QUEUE_DEPTH)
    {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
        xSemaphoreGive(_outbound_mutex);
#endif
        return false;
    }
    MqttOutboundEvent &event = _backpressure_failure_events[_backpressure_failure_tail];
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

bool MqttClient::_dequeueInboundCommand(MqttInboundCommand &command)
{
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (_inbound_mutex == nullptr || xSemaphoreTake(_inbound_mutex, pdMS_TO_TICKS(10)) != pdTRUE)
        return false;
#else
    std::lock_guard<std::mutex> lock(_inbound_mutex);
#endif
    if (_inbound_count == 0)
    {
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

bool MqttClient::isConnected() const
{
    return _connected.load();
}

bool MqttClient::_enqueueOutboundEvent(const char *topic, const char *payload, bool retained,
                                       bool critical)
{
    if (!topic || !payload || std::strlen(topic) >= MQTT_TOPIC_BUFFER_SIZE ||
        std::strlen(payload) >= MQTT_TELEMETRY_PAYLOAD_SIZE)
        return false;
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (_outbound_mutex == nullptr || xSemaphoreTake(_outbound_mutex, portMAX_DELAY) != pdTRUE)
    {
        _outbound_dropped.fetch_add(1);
        return false;
    }
#else
    std::unique_lock<std::mutex> lock(_outbound_mutex);
#endif
    MqttOutboundEvent *events = critical ? _outbound_ack_events : _outbound_telemetry_events;
    size_t &head = critical ? _outbound_ack_head : _outbound_telemetry_head;
    size_t &tail = critical ? _outbound_ack_tail : _outbound_telemetry_tail;
    size_t &count = critical ? _outbound_ack_count : _outbound_telemetry_count;
    const size_t depth = critical ? MQTT_OUTBOUND_ACK_QUEUE_DEPTH : MQTT_OUTBOUND_TELEMETRY_QUEUE_DEPTH;
    if (count == depth || (critical && count + _reserved_ack_count == depth))
    {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
        xSemaphoreGive(_outbound_mutex);
#endif
        _outbound_dropped.fetch_add(1);
        return false;
    }
    MqttOutboundEvent &event = events[tail];
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

bool MqttClient::_reserveCommandAck()
{
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (_outbound_mutex == nullptr || xSemaphoreTake(_outbound_mutex, portMAX_DELAY) != pdTRUE)
        return false;
#else
    std::lock_guard<std::mutex> lock(_outbound_mutex);
#endif
    if (_outbound_ack_count + _reserved_ack_count == MQTT_OUTBOUND_ACK_QUEUE_DEPTH)
    {
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

bool MqttClient::_publishReservedCommandAck(const char *command_id, const char *status,
                                            uint8_t node_id, const char *reason)
{
    if (!isValidMqttCommandId(command_id))
        return false;
    JsonDocument doc;
    doc["command_id"] = command_id;
    doc["status"] = status;
    if (node_id > 0)
        doc["node_id"] = node_id;
    if (reason)
        doc["reason"] = reason;
    char topic[MQTT_TOPIC_BUFFER_SIZE] = {};
    char payload[256] = {};
    const int written = snprintf(topic, sizeof(topic), "%s/%s%s%s", MQTT_TOPIC_BASE,
                                 _config.device_id, MQTT_ACK_PREFIX_SUFFIX, command_id);
    const size_t bytes = serializeJson(doc, payload, sizeof(payload));
    if (written < 0 || static_cast<size_t>(written) >= sizeof(topic) || bytes == 0 || bytes >= sizeof(payload))
        return false;
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (_outbound_mutex == nullptr || xSemaphoreTake(_outbound_mutex, portMAX_DELAY) != pdTRUE)
        return false;
#else
    std::lock_guard<std::mutex> lock(_outbound_mutex);
#endif
    if (_reserved_ack_count == 0 || _outbound_ack_count == MQTT_OUTBOUND_ACK_QUEUE_DEPTH)
    {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
        xSemaphoreGive(_outbound_mutex);
#endif
        return false;
    }
    --_reserved_ack_count;
    MqttOutboundEvent &event = _outbound_ack_events[_outbound_ack_tail];
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
    if (command_id && isValidMqttCommandId(command_id))
    {
        _dedup_cache.put(command_id, status, node_id, reason, getSystemMillis());
    }
    return true;
}

bool MqttClient::_peekOutboundEvent(MqttOutboundEvent &event, bool &critical, bool &backpressure_failure)
{
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (_outbound_mutex == nullptr || xSemaphoreTake(_outbound_mutex, pdMS_TO_TICKS(10)) != pdTRUE)
        return false;
#else
    std::lock_guard<std::mutex> lock(_outbound_mutex);
#endif
    critical = _outbound_ack_count > 0;
    backpressure_failure = !critical && _backpressure_failure_count > 0;
    const size_t count = critical ? _outbound_ack_count : backpressure_failure ? _backpressure_failure_count
                                                                               : _outbound_telemetry_count;
    if (count == 0)
    {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
        xSemaphoreGive(_outbound_mutex);
#endif
        return false;
    }
    event = critical               ? _outbound_ack_events[_outbound_ack_head]
            : backpressure_failure ? _backpressure_failure_events[_backpressure_failure_head]
                                   : _outbound_telemetry_events[_outbound_telemetry_head];
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    xSemaphoreGive(_outbound_mutex);
#endif
    return true;
}

bool MqttClient::_discardOutboundEvent(bool critical, bool backpressure_failure)
{
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (_outbound_mutex == nullptr || xSemaphoreTake(_outbound_mutex, pdMS_TO_TICKS(10)) != pdTRUE)
        return false;
#else
    std::lock_guard<std::mutex> lock(_outbound_mutex);
#endif
    size_t &head = critical ? _outbound_ack_head : backpressure_failure ? _backpressure_failure_head
                                                                        : _outbound_telemetry_head;
    size_t &count = critical ? _outbound_ack_count : backpressure_failure ? _backpressure_failure_count
                                                                          : _outbound_telemetry_count;
    const size_t depth = critical ? MQTT_OUTBOUND_ACK_QUEUE_DEPTH : backpressure_failure ? MQTT_BACKPRESSURE_FAILURE_QUEUE_DEPTH
                                                                                         : MQTT_OUTBOUND_TELEMETRY_QUEUE_DEPTH;
    if (count == 0)
    {
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

bool MqttClient::_queueJsonEvent(const char *topic, const JsonDocument &doc, bool retained)
{
    char payload[MQTT_TELEMETRY_PAYLOAD_SIZE];
    const size_t bytes = serializeJson(doc, payload, sizeof(payload));
    return bytes > 0 && bytes < sizeof(payload) && _enqueueOutboundEvent(topic, payload, retained);
}

bool MqttClient::_getTimestamp(char *buffer, size_t buffer_size) const
{
    // Prefer the telemetry facade, which reports whether *any* reference
    // (DS1307, SNTP, or backend push) currently holds valid time. Without the
    // facade, keep the legacy RTC-valid AND system-NTP-synced gate so existing
    // host tests that inject only an IClock are unaffected.
    bool have_time = false;
    if (_rtc_telemetry)
    {
        const TimeTelemetry telemetry = _rtc_telemetry->getTimeTelemetry();
        have_time = telemetry.rtc_valid || telemetry.ntp_synced ||
                    telemetry.source == TimeSourceKind::BACKEND;
    }
    else
    {
        have_time = _rtc && _rtc->getTime().is_valid && _isNtpSynced();
    }
    if (!have_time)
        return false;
    const time_t now_sec = static_cast<time_t>(_currentUnixTime());
    struct tm timeinfo;
    gmtime_r(&now_sec, &timeinfo);
    return strftime(buffer, buffer_size, "%Y-%m-%dT%H:%M:%SZ", &timeinfo) > 0;
}

bool MqttClient::publishHeartbeat()
{
    if (!isConnected())
        return false;
    JsonDocument doc;
    char timestamp[32] = {};
    const uint32_t now = getSystemMillis();
    doc["status"] = "online";
    doc["device_id"] = _config.device_id;
    doc["uptime_s"] = now / 1000U;
    doc["rssi_dbm"] = _getRssiDbm();
    doc["free_heap_b"] = _getFreeHeap();
    TimeTelemetry telemetry{};
    bool have_telemetry = false;
    if (_rtc_telemetry)
    {
        telemetry = _rtc_telemetry->getTimeTelemetry();
        have_telemetry = true;
    }
    doc["ntp_synced"] = have_telemetry ? telemetry.ntp_synced : _isNtpSynced();
    doc["rtc_valid"] = have_telemetry ? telemetry.rtc_valid : (_rtc && _rtc->getTime().is_valid);
    if (have_telemetry)
    {
        doc["time_source"] = timeSourceKindToString(telemetry.source);
        doc["last_sync_unix_time_utc"] = telemetry.last_sync_unix_time_utc;
    }
    doc["timestamp_utc"] = _getTimestamp(timestamp, sizeof(timestamp)) ? timestamp : nullptr;
    if (_reset_reason && _reset_reason[0] != '\0')
    {
        doc["reset_reason"] = _reset_reason;
    }
    char topic[MQTT_TOPIC_BUFFER_SIZE];
    if (!_buildTopic(topic, sizeof(topic), MQTT_STATUS_SUFFIX))
        return false;
    char payload[MQTT_HEARTBEAT_PAYLOAD_SIZE];
    const size_t bytes = serializeJson(doc, payload, sizeof(payload));
    if (!bytes || bytes >= sizeof(payload))
        return false;
    if (!_enqueueOutboundEvent(topic, payload, MQTT_PUBLISH_RETAIN))
        return false;
    _last_heartbeat_ms = now;
#if defined(UNIT_TEST_HOST) && !defined(MQTT_INTEGRATION_TARGET)
    return _last_outbound_publish_ok;
#else
    return true;
#endif
}

bool MqttClient::publishConnectedHeartbeat()
{
    return publishHeartbeat();
}

bool MqttClient::publishScheduleState()
{
    if (!isConnected() || _schedule_state_provider == nullptr ||
        _group_scheduler == nullptr || _config.device_id == nullptr)
    {
        return false;
    }

    ScheduleStateSnapshot snapshot{};
    if (!_schedule_state_provider(snapshot))
        return false;

    JsonDocument doc;
    doc["device_id"] = _config.device_id;
    doc["timestamp"] = _currentUnixTime();
    doc["slots_reconciled"] = snapshot.slots_reconciled;
    JsonArray active_slots = doc["active_slots"].to<JsonArray>();
    JsonArray assignments = doc["assignments"].to<JsonArray>();
    JsonArray groups = doc["groups"].to<JsonArray>();

    doc["assignment_version"] = snapshot.assignment_version;

    for (size_t slot_index = 0; slot_index < 4; ++slot_index)
    {
        const ScheduleStateSlot &slot = snapshot.slots[slot_index];
        // Report all four positions, including empty slots, so the backend can
        // compare the complete control-slot tuple rather than inferring that a
        // missing entry means an empty slot.
        JsonObject active = active_slots.add<JsonObject>();
        active["idx"] = slot.idx != 0 ? slot.idx : static_cast<uint8_t>(slot_index + 1);
        if (slot.type == 1 && slot.id >= 1 && slot.id <= MAX_NODES)
        {
            active["type"] = "NODE";
            active["id"] = slot.id;
        }
        else if (slot.type == 2 && slot.id >= 1 && slot.id <= MAX_TIMER_GROUPS)
        {
            active["type"] = "GROUP";
            active["id"] = slot.id;
        }
        else
        {
            active["type"] = nullptr;
            active["id"] = nullptr;
        }
    }

    for (size_t i = 0; i < snapshot.assignment_count && i < MAX_NODES; ++i) {
        JsonObject assignment = assignments.add<JsonObject>();
        assignment["node_id"] = snapshot.assignments[i].node_id;
        assignment["group_id"] = snapshot.assignments[i].group_id;
    }

    for (uint8_t gid = 1; gid <= MAX_TIMER_GROUPS; ++gid)
    {
        GroupRuntimeState runtime{};
        const bool has_runtime = _group_scheduler->getGroupRuntimeState(gid, runtime);
        JsonObject group = groups.add<JsonObject>();
        group["group_id"] = gid;
        group["state"] = has_runtime
                             ? (runtime.assignment_state == GroupAssignmentState::ACTIVE   ? "ACTIVE"
                                : runtime.assignment_state == GroupAssignmentState::PAUSED ? "PAUSED"
                                                                                           : "UNASSIGNED")
                             : "UNASSIGNED";
        group["phase"] = has_runtime
                             ? (runtime.current_phase == GroupPhase::PHASE_SPRAYING ? "SPRAYING" : "COOLING_DOWN")
                             : "SPRAYING";
        group["phase_remaining_s"] = has_runtime ? runtime.phase_remaining_s : 0;

        JsonObject profile = group["profile"].to<JsonObject>();
        JsonObject active = profile["active"].to<JsonObject>();
        active["treatment_version"] = has_runtime ? runtime.treatment_version : 0;
        active["spray_day_s"] = has_runtime ? runtime.profile.spray_day_s : 0;
        active["cooldown_day_s"] = has_runtime ? runtime.profile.cooldown_day_s : 0;
        active["spray_night_s"] = has_runtime ? runtime.profile.spray_night_s : 0;
        active["cooldown_night_s"] = has_runtime ? runtime.profile.cooldown_night_s : 0;

        PublishedTreatmentAssignment pending{};
        const bool has_pending = has_runtime &&
                                 _group_scheduler->getPendingSchedule(gid, pending);
        JsonObject pending_profile = profile["pending"].to<JsonObject>();
        pending_profile["has_pending"] = has_pending;
        pending_profile["treatment_version"] = has_pending ? pending.version : 0;
        pending_profile["spray_day_s"] = has_pending ? pending.profile.spray_day_s : 0;
        pending_profile["cooldown_day_s"] = has_pending ? pending.profile.cooldown_day_s : 0;
        pending_profile["spray_night_s"] = has_pending ? pending.profile.spray_night_s : 0;
        pending_profile["cooldown_night_s"] = has_pending ? pending.profile.cooldown_night_s : 0;
    }

    char topic[MQTT_TOPIC_BUFFER_SIZE];
    if (!_buildTopic(topic, sizeof(topic), MQTT_SCHEDULE_STATE_SUFFIX))
        return false;
    static char payload[MQTT_TELEMETRY_PAYLOAD_SIZE];
    const size_t bytes = serializeJson(doc, payload, sizeof(payload));
    if (!bytes || bytes >= sizeof(payload))
        return false;
    const bool queued = _enqueueOutboundEvent(topic, payload, false);
#if defined(UNIT_TEST_HOST) && !defined(MQTT_INTEGRATION_TARGET)
    return queued && _last_outbound_publish_ok;
#else
    return queued;
#endif
}

void MqttClient::serviceScheduleState(uint32_t now_ms)
{
    const bool due = _last_schedule_state_ms == 0 ||
                     now_ms - _last_schedule_state_ms >= MQTT_SCHEDULE_STATE_PERIOD_MS;
    if (!_schedule_state_publish_pending.exchange(false) && !due)
        return;
    if (!isConnected() || _schedule_state_provider == nullptr)
    {
        _schedule_state_publish_pending.store(true);
        return;
    }
    if (publishScheduleState())
    {
        _last_schedule_state_ms = now_ms;
    }
    else
    {
        _schedule_state_publish_pending.store(true);
    }
}

bool MqttClient::publishGroupTelemetry(uint8_t group_id, uint32_t active_nodes_mask, const char *state_str)
{
    if (!isConnected())
        return false;
    JsonDocument doc;
    doc["group_id"] = group_id;
    doc["active_nodes_mask"] = active_nodes_mask;
    doc["state"] = state_str ? state_str : "IDLE";
    char topic[MQTT_TOPIC_BUFFER_SIZE];
    const int written = snprintf(topic, sizeof(topic), "%s/%s%s%u", MQTT_TOPIC_BASE, _config.device_id, MQTT_TELEMETRY_GROUP_SUFFIX, group_id);
    if (written < 0 || static_cast<size_t>(written) >= sizeof(topic))
        return false;
    char payload[256];
    size_t bytes = serializeJson(doc, payload, sizeof(payload));
    return bytes > 0 && bytes < sizeof(payload) &&
           _enqueueOutboundEvent(topic, payload, MQTT_PUBLISH_RETAIN);
}

bool MqttClient::publishNodeSnapshot(uint8_t node_id, const NodeState &state, const NodeSnapshotContext *context)
{
    if (!isConnected())
        return false;
    JsonDocument doc;
    doc["node_id"] = state.node_id;
    doc["group_id"] = state.group_id;
    doc["desired_state"] = state.desired_state == NodePumpState::ON ? "ON" : "OFF";
    doc["reported_state"] = state.reported_state == NodePumpState::ON ? "ON" : "OFF";
    doc["driver_feedback"] = state.driver_feedback;
    doc["flow_lpm"] = state.flow_lpm_x100 / 100.0f;
    doc["delivered_volume_ml"] = state.delivered_volume_ml;
    doc["health_status"] = state.health == NodeHealthStatus::ONLINE ? "ONLINE" : state.health == NodeHealthStatus::STALE ? "STALE"
                                                                             : state.health == NodeHealthStatus::FAULT   ? "FAULT"
                                                                                                                         : "OFFLINE";
    doc["boot_session_id"] = state.boot_session_id;

    if (context)
    {
        doc["override_state"] = context->override_state ? context->override_state : "NONE";
        doc["override_expiry_ms"] = context->override_expiry_ms;
        if (context->last_command_id && context->last_command_id[0] != '\0')
        {
            doc["last_command_id"] = context->last_command_id;
        }
        if (context->last_command_result && context->last_command_result[0] != '\0')
        {
            doc["last_command_result"] = context->last_command_result;
        }
        doc["last_ping_at"] = context->last_ping_at;
        doc["last_ping_ok"] = context->last_ping_ok;
        doc["ping_rtt_ms"] = context->ping_rtt_ms;
        doc["consecutive_ping_failures"] = context->consecutive_ping_failures;
        if (context->reset_reason && context->reset_reason[0] != '\0')
        {
            doc["reset_reason"] = context->reset_reason;
        }
        else if (_reset_reason && _reset_reason[0] != '\0')
        {
            doc["reset_reason"] = _reset_reason;
        }
        if (context->source && context->source[0] != '\0')
        {
            doc["source"] = context->source;
        }
        if (context->transition_reason && context->transition_reason[0] != '\0')
        {
            doc["transition_reason"] = context->transition_reason;
        }
        if (context->schedule_state && context->schedule_state[0] != '\0')
        {
            doc["schedule_state"] = context->schedule_state;
        }
    }
    else if (_reset_reason && _reset_reason[0] != '\0')
    {
        doc["reset_reason"] = _reset_reason;
    }

    char topic[MQTT_TOPIC_BUFFER_SIZE];
    const int written = snprintf(topic, sizeof(topic), "%s/%s%s%u/snapshot", MQTT_TOPIC_BASE, _config.device_id, MQTT_TELEMETRY_NODE_SUFFIX, node_id);
    if (written < 0 || static_cast<size_t>(written) >= sizeof(topic))
        return false;
    char payload[512];
    size_t bytes = serializeJson(doc, payload, sizeof(payload));
    return bytes > 0 && bytes < sizeof(payload) &&
           _enqueueOutboundEvent(topic, payload, MQTT_PUBLISH_RETAIN);
}

bool MqttClient::publishScanResults(const char *scan_id, const DiscoveredRfNodeInfo *nodes, size_t count,
                                    uint32_t duration_ms, const char *status, const char *error)
{
    if (!isConnected())
        return false;
    JsonDocument doc;
    doc["scan_id"] = scan_id ? scan_id : "rf_scan";
    doc["device_id"] = _config.device_id;
    doc["duration_ms"] = duration_ms;
    doc["status"] = status ? status : "COMPLETED";
    if (error)
        doc["error"] = error;
    JsonObject range = doc["range"].to<JsonObject>();
    range["min_node_id"] = RF_PRODUCTION_MIN_NODE_ID;
    range["max_node_id"] = RF_PRODUCTION_MAX_NODE_ID;
    JsonArray arr = doc["nodes"].to<JsonArray>();
    for (size_t i = 0; i < count; ++i)
    {
        JsonObject node_obj = arr.add<JsonObject>();
        node_obj["node_id"] = nodes[i].node_id;
        node_obj["online"] = nodes[i].online;
        if (nodes[i].online)
            node_obj["rtt_ms"] = nodes[i].rtt_ms;
        else
            node_obj["failure_reason"] = nodes[i].failure_code == 1 ? "TIMEOUT" : nodes[i].failure_code == 2 ? "UNEXPECTED_RESPONSE"
                                                                              : nodes[i].failure_code == 3   ? "TRANSPORT_ERROR"
                                                                              : nodes[i].failure_code == 4   ? "INVALID_NODE_ID"
                                                                              : nodes[i].failure_code == 5   ? "UART_NOT_READY"
                                                                                                             : "LEGACY_ERROR";
        node_obj["protocol"] = "AGU_LEGACY_SCI";
    }
    char topic[MQTT_TOPIC_BUFFER_SIZE];
    const int written = snprintf(topic, sizeof(topic), "%s/%s%s", MQTT_TOPIC_BASE, _config.device_id, MQTT_TELEMETRY_GATEWAY_SCAN_RESULTS_SUFFIX);
    if (written < 0 || static_cast<size_t>(written) >= sizeof(topic))
        return false;
    char payload[768];
    size_t bytes = serializeJson(doc, payload, sizeof(payload));
    return bytes > 0 && bytes < sizeof(payload) &&
           _enqueueOutboundEvent(topic, payload, false);
}

bool MqttClient::publishCommandAck(const char *command_id, const char *status, uint8_t node_id, const char *reason)
{
    if (!isConnected() || !isValidMqttCommandId(command_id))
        return false;
    _dedup_cache.put(command_id, status, node_id, reason, getSystemMillis());
    JsonDocument doc;
    doc["command_id"] = command_id;
    doc["status"] = status ? status : "COMPLETED";
    if (node_id > 0)
        doc["node_id"] = node_id;
    if (reason)
        doc["reason"] = reason;
    char topic[MQTT_TOPIC_BUFFER_SIZE];
    const int written = snprintf(topic, sizeof(topic), "%s/%s%s%s", MQTT_TOPIC_BASE, _config.device_id, MQTT_ACK_PREFIX_SUFFIX, command_id);
    if (written < 0 || static_cast<size_t>(written) >= sizeof(topic))
        return false;
    char payload[256];
    size_t bytes = serializeJson(doc, payload, sizeof(payload));
    return bytes > 0 && bytes < sizeof(payload) &&
           _enqueueOutboundEvent(topic, payload, MQTT_PUBLISH_RETAIN, true);
}

bool MqttClient::publishCommandEvent(const char *command_id, const char *status,
                                     uint8_t node_id, const char *reason)
{
    if (!isConnected() || !isValidMqttCommandId(command_id))
        return false;
    JsonDocument doc;
    doc["command_id"] = command_id;
    doc["status"] = status ? status : "UNKNOWN";
    if (node_id > 0)
        doc["node_id"] = node_id;
    if (reason)
        doc["reason"] = reason;
    char topic[MQTT_TOPIC_BUFFER_SIZE];
    const int written = snprintf(topic, sizeof(topic), "%s/%s%s%s%s", MQTT_TOPIC_BASE, _config.device_id,
                                 MQTT_COMMAND_EVENT_PREFIX_SUFFIX, command_id, MQTT_COMMAND_EVENT_SUFFIX);
    if (written < 0 || static_cast<size_t>(written) >= sizeof(topic))
        return false;
    char payload[256];
    const size_t bytes = serializeJson(doc, payload, sizeof(payload));
    return bytes > 0 && bytes < sizeof(payload) &&
           _enqueueOutboundEvent(topic, payload, MQTT_PUBLISH_RETAIN);
}

void MqttClient::publishCommandOutcome(const char *command_id, const char *status,
                                       uint8_t node_id, const char *reason)
{
    if (command_id && isValidMqttCommandId(command_id))
    {
        _dedup_cache.put(command_id, status, node_id, reason, getSystemMillis());
    }
    publishCommandEvent(command_id, status, node_id, reason);
    if (status && (std::strcmp(status, "COMPLETED") == 0 || std::strncmp(status, "FAULT", 5) == 0))
    {
        publishCommandAck(command_id, status, node_id, reason);
    }
}

void MqttClient::publishSafetyAudit(const char *event, const char *reason)
{
    if (!isConnected() || !event)
        return;
    JsonDocument doc;
    doc["event"] = event;
    if (reason)
        doc["reason"] = reason;
    char topic[MQTT_TOPIC_BUFFER_SIZE];
    if (!_buildTopic(topic, sizeof(topic), "/telemetry/audit"))
        return;
    char payload[256];
    const size_t bytes = serializeJson(doc, payload, sizeof(payload));
    if (bytes > 0 && bytes < sizeof(payload))
        _enqueueOutboundEvent(topic, payload, MQTT_PUBLISH_RETAIN);
}

bool MqttClient::_enqueueInboundRejection(const JsonDocument &doc, uint8_t node_id, const char *reason)
{
    const char *candidate = doc["command_id"].as<const char *>();
    const char *command_id = isValidMqttCommandId(candidate) ? candidate : "invalid-command";
    _inbound_rejected.fetch_add(1);
    const bool queued = _reserveCommandAck() && _publishReservedCommandAck(command_id, "REJECTED", node_id,
                                                                           reason ? reason : "Invalid command");
#if defined(UNIT_TEST_HOST) && !defined(MQTT_INTEGRATION_TARGET)
    serviceOutgoingEvents();
#endif
    return queued;
}

bool MqttClient::_hasValidCommandEnvelope(const JsonDocument &doc, const char *&command_id) const
{
    command_id = doc["command_id"];
    return isValidMqttCommandId(command_id) &&
           doc["version"].is<uint16_t>() && doc["version"].as<uint16_t>() > 0;
}

bool MqttClient::_enqueueAssignmentCommand(const JsonDocument &doc)
{
    const char *cmd_id = nullptr;
    if (!_hasValidCommandEnvelope(doc, cmd_id) || !doc["node_id"].is<uint8_t>() || !doc["group_id"].is<uint8_t>())
        return false;
    const uint16_t version = doc["version"].as<uint16_t>();
    const uint32_t active_ver = _group_scheduler ? _group_scheduler->getActiveAssignmentVersion() : _last_assignment_version;
    if (active_ver > 0 && version <= active_ver)
    {
        _enqueueInboundRejection(doc, doc["node_id"].as<uint8_t>(), "Stale assignment version");
        return true;
    }
    MqttInboundCommand command{};
    command.type = MqttInboundCommandType::ASSIGNMENT;
    command.node_id = doc["node_id"].as<uint8_t>();
    command.group_id = doc["group_id"].as<uint8_t>();
    command.values[0] = version;
    std::strncpy(command.command_id, cmd_id, sizeof(command.command_id) - 1);
    return _enqueueInboundCommand(command);
}

bool MqttClient::_enqueueFlowPolicyCommand(const JsonDocument &doc)
{
    const char *cmd_id = nullptr;
    if (!_hasValidCommandEnvelope(doc, cmd_id) ||
        !doc["node_id"].is<uint8_t>() || !doc["policy_version"].is<uint32_t>() ||
        !doc["treatment_version_id"].is<uint32_t>() || !doc["calibration_id"].is<uint32_t>() ||
        !doc["min_flow_lpm_x100"].is<uint16_t>() || !doc["max_off_flow_lpm_x100"].is<uint16_t>() ||
        !doc["max_flow_lpm_x100"].is<uint16_t>() || !doc["flow_start_timeout_ms"].is<uint32_t>() ||
        !doc["run_lease_ms"].is<uint32_t>() || !doc["max_on_duration_ms"].is<uint32_t>())
    {
        return false;
    }
    const uint8_t node_id = doc["node_id"].as<uint8_t>();
    const uint32_t policy_ver = doc["policy_version"].as<uint32_t>();
    if (isValidNodeId(node_id))
    {
        NodeLeasePolicy lease{};
        NodeFlowPolicy flow{};
        const uint32_t active_ver = (_command_manager && _command_manager->getNodeControlPolicy(node_id, lease, flow) && flow.flow_policy_provisioned)
                                        ? flow.provenance.policy_version
                                        : _last_policy_version[node_id];
        if (active_ver > 0 && policy_ver <= active_ver)
        {
            _enqueueInboundRejection(doc, node_id, "Stale flow policy version");
            return true;
        }
    }
    MqttInboundCommand command{};
    command.type = MqttInboundCommandType::FLOW_POLICY;
    command.node_id = node_id;
    command.values[0] = doc["run_lease_ms"].as<uint32_t>();
    command.values[1] = doc["max_on_duration_ms"].as<uint32_t>();
    command.values[2] = doc["min_flow_lpm_x100"].as<uint16_t>();
    command.values[3] = doc["max_off_flow_lpm_x100"].as<uint16_t>();
    command.values[4] = doc["max_flow_lpm_x100"].as<uint16_t>();
    command.values[5] = doc["flow_start_timeout_ms"].as<uint32_t>();
    command.values[6] = policy_ver;
    command.values[7] = doc["treatment_version_id"].as<uint32_t>();
    command.values[8] = doc["calibration_id"].as<uint32_t>();
    std::strncpy(command.command_id, cmd_id, sizeof(command.command_id) - 1);
    return _enqueueInboundCommand(command);
}

bool MqttClient::_enqueueTreatmentCommand(const JsonDocument &doc)
{
    const char *cmd_id = nullptr;
    if (!_hasValidCommandEnvelope(doc, cmd_id) ||
        !doc["group_id"].is<uint8_t>() || !doc["season_id"].is<uint32_t>() ||
        !doc["treatment_version_id"].is<uint32_t>() || !doc["treatment_version"].is<uint32_t>() ||
        !doc["treatment_status"].is<const char *>())
    {
        return false;
    }
    const char *status = doc["treatment_status"];
    if (strcmp(status, "PUBLISHED") != 0)
        return false;
    if (!doc["schedule"]["spray_day_s"].is<uint32_t>() || !doc["schedule"]["cooldown_day_s"].is<uint32_t>() ||
        !doc["schedule"]["spray_night_s"].is<uint32_t>() || !doc["schedule"]["cooldown_night_s"].is<uint32_t>())
    {
        return false;
    }
    const uint8_t group_id = doc["group_id"].as<uint8_t>();
    const uint32_t treatment_ver = doc["treatment_version"].as<uint32_t>();
    if (group_id >= 1 && group_id <= MAX_TIMER_GROUPS)
    {
        GroupRuntimeState current_grp{};
        const uint32_t active_ver = (_group_scheduler && _group_scheduler->getGroupState(group_id, current_grp))
                                        ? current_grp.treatment_version
                                        : _last_treatment_version[group_id];
        if (active_ver > 0 && treatment_ver <= active_ver)
        {
            _enqueueInboundRejection(doc, 0, "Stale configuration version");
            return true;
        }
    }
    MqttInboundCommand command{};
    command.type = MqttInboundCommandType::TREATMENT;
    command.group_id = group_id;
    command.values[0] = doc["season_id"].as<uint32_t>();
    command.values[1] = doc["treatment_version_id"].as<uint32_t>();
    command.values[2] = treatment_ver;
    command.values[3] = doc["schedule"]["spray_day_s"].as<uint32_t>();
    command.values[4] = doc["schedule"]["cooldown_day_s"].as<uint32_t>();
    command.values[5] = doc["schedule"]["spray_night_s"].as<uint32_t>();
    command.values[6] = doc["schedule"]["cooldown_night_s"].as<uint32_t>();
    std::strncpy(command.command_id, cmd_id, sizeof(command.command_id) - 1);
    return _enqueueInboundCommand(command);
}

bool MqttClient::_enqueueGroupStateCommand(const JsonDocument &doc)
{
    const char *cmd_id = nullptr;
    if (!_hasValidCommandEnvelope(doc, cmd_id) || !doc["group_id"].is<uint8_t>() ||
        !doc["active"].is<bool>())
        return false;
    MqttInboundCommand command{};
    command.type = MqttInboundCommandType::GROUP_STATE;
    command.group_id = doc["group_id"].as<uint8_t>();
    command.values[0] = doc["active"].as<bool>() ? 1U : 0U;
    std::strncpy(command.command_id, cmd_id, sizeof(command.command_id) - 1);
    return _enqueueInboundCommand(command);
}

bool MqttClient::_enqueueNodeOverrideCommand(uint8_t node_id, const JsonDocument &doc)
{
    const char *cmd_id = nullptr;
    if (!_hasValidCommandEnvelope(doc, cmd_id))
    {
        return false;
    }
    const char *state_str = doc["desired_state"] | doc["state"];
    if (!state_str)
    {
        return false;
    }
    bool is_on = (strcmp(state_str, "ON") == 0 || strcmp(state_str, "on") == 0);
    bool is_off = (strcmp(state_str, "OFF") == 0 || strcmp(state_str, "off") == 0);
    if (!is_on && !is_off)
    {
        return false;
    }
    const char *src = doc["source"].as<const char *>();
    if (!src || (strcmp(src, "MANUAL_OVERRIDE") != 0 && strcmp(src, "FAIL_SAFE") != 0))
        return false;
    if (is_on && (!doc["run_lease_ms"].is<uint32_t>() || doc["run_lease_ms"].as<uint32_t>() == 0))
    {
        return false;
    }
    if (doc["run_lease_ms"].is<uint32_t>())
    {
        uint32_t lease = doc["run_lease_ms"].as<uint32_t>();
        if (lease == 0 || lease > DEFAULT_MAX_ON_DURATION_MS)
        {
            return false;
        }
    }
    if (is_off && (!doc["override_duration_ms"].is<uint32_t>() ||
                   doc["override_duration_ms"].as<uint32_t>() == 0))
    {
        return false;
    }
    if (doc["override_duration_ms"].is<uint32_t>())
    {
        uint32_t override_dur = doc["override_duration_ms"].as<uint32_t>();
        if (override_dur == 0 || override_dur > 86400000U)
        {
            return false;
        }
    }
    MqttInboundCommand command{};
    command.type = MqttInboundCommandType::NODE_OVERRIDE;
    command.node_id = node_id;
    command.desired_state = is_on ? NodePumpState::ON : NodePumpState::OFF;
    std::strncpy(command.source, src, sizeof(command.source) - 1);
    if (doc["run_lease_ms"].is<uint32_t>())
    {
        command.values[0] = doc["run_lease_ms"].as<uint32_t>();
    }
    if (is_off && (!doc["override_duration_ms"].is<uint32_t>() ||
                   doc["override_duration_ms"].as<uint32_t>() == 0))
    {
        return false;
    }
    if (doc["override_duration_ms"].is<uint32_t>())
    {
        command.values[1] = doc["override_duration_ms"].as<uint32_t>();
    }
    std::strncpy(command.command_id, cmd_id, sizeof(command.command_id) - 1);
    return _enqueueInboundCommand(command);
}

bool MqttClient::_enqueueGroupControlCommand(uint8_t group_id, const JsonDocument &doc)
{
    const char *cmd_id = nullptr;
    if (!_hasValidCommandEnvelope(doc, cmd_id))
    {
        return false;
    }
    const char *action_str = doc["action"] | doc["state"];
    if (!action_str)
    {
        return false;
    }
    bool is_on = (strcmp(action_str, "ON") == 0 || strcmp(action_str, "on") == 0);
    bool is_off = (strcmp(action_str, "OFF") == 0 || strcmp(action_str, "off") == 0);
    if (!is_on && !is_off)
    {
        return false;
    }
    const char *src = doc["source"].as<const char *>();
    if (!src || (strcmp(src, "MANUAL_OVERRIDE") != 0 && strcmp(src, "FAIL_SAFE") != 0))
        return false;
    if (is_on && (!doc["run_lease_ms"].is<uint32_t>() || doc["run_lease_ms"].as<uint32_t>() == 0))
    {
        return false;
    }
    if (is_off && (!doc["override_duration_ms"].is<uint32_t>() ||
                   doc["override_duration_ms"].as<uint32_t>() == 0))
    {
        return false;
    }
    MqttInboundCommand command{};
    command.type = MqttInboundCommandType::GROUP_CONTROL;
    command.group_id = group_id;
    command.desired_state = is_on ? NodePumpState::ON : NodePumpState::OFF;
    std::strncpy(command.source, src, sizeof(command.source) - 1);
    if (doc["run_lease_ms"].is<uint32_t>())
    {
        const uint32_t lease = doc["run_lease_ms"].as<uint32_t>();
        if (lease == 0 || lease > DEFAULT_MAX_ON_DURATION_MS)
            return false;
        command.values[0] = lease;
    }
    if (doc["override_duration_ms"].is<uint32_t>())
    {
        const uint32_t duration = doc["override_duration_ms"].as<uint32_t>();
        if (duration == 0 || duration > 86400000U)
            return false;
        command.values[1] = duration;
    }
    std::strncpy(command.command_id, cmd_id, sizeof(command.command_id) - 1);
    return _enqueueInboundCommand(command);
}

void MqttClient::_parseNodeTopic(const char *ptr, const JsonDocument &doc)
{
    const char *slash = strchr(ptr, '/');
    if (!slash || strcmp(slash, "/override") != 0)
        return;
    char id_buf[16] = {};
    size_t id_len = slash - ptr;
    if (id_len > 0 && id_len < sizeof(id_buf))
    {
        std::memcpy(id_buf, ptr, id_len);
        uint8_t node_id = 0;
        if (parseBoundedUint(id_buf, RF_PRODUCTION_MIN_NODE_ID, RF_PRODUCTION_MAX_NODE_ID, node_id))
        {
            ESP_LOGI(TAG, "Received node override topic for node=%u", node_id);
            if (!_enqueueNodeOverrideCommand(node_id, doc))
            {
                ESP_LOGW(TAG, "Rejected node override command for node=%u during MQTT parsing", node_id);
                _enqueueInboundRejection(doc, node_id, "Invalid command or inbound queue full");
            }
        }
        else
        {
            _enqueueInboundRejection(doc, 0, "Invalid node_id in topic");
        }
    }
}

void MqttClient::_parseGroupTopic(const char *ptr, const JsonDocument &doc)
{
    const char *slash = strchr(ptr, '/');
    if (!slash || strcmp(slash, "/control") != 0)
        return;
    char id_buf[16] = {};
    size_t id_len = slash - ptr;
    if (id_len > 0 && id_len < sizeof(id_buf))
    {
        std::memcpy(id_buf, ptr, id_len);
        uint8_t group_id = 0;
        if (parseBoundedUint(id_buf, 1, 4, group_id))
        {
            if (!_enqueueGroupControlCommand(group_id, doc))
            {
                _enqueueInboundRejection(doc, 0, "Invalid command or inbound queue full");
            }
        }
        else
        {
            _enqueueInboundRejection(doc, 0, "Invalid group_id in topic");
        }
    }
}

void MqttClient::_parseGatewayTopic(const char *ptr, const JsonDocument &doc)
{
    if (!ptr)
        return;
    if (strcmp(ptr, "scan") == 0 || strcmp(ptr, "scan_rf") == 0)
    {
        const char *command_id = nullptr;
        if (!_hasValidCommandEnvelope(doc, command_id))
        {
            _enqueueInboundRejection(doc, 0, "Invalid scan command envelope: command_id/version required");
        }
        else if (!_enqueueGatewayScanCommand(doc))
        {
            _enqueueInboundRejection(doc, 0, "Invalid scan command or inbound queue full");
        }
    }
    else if (strcmp(ptr, "claim") == 0 || strcmp(ptr, "claim_node") == 0)
    {
        if (!_enqueueGatewayClaimCommand(doc))
        {
            _enqueueInboundRejection(doc, 0, "Invalid claim command or inbound queue full");
        }
    }
}

bool MqttClient::_enqueueGatewayScanCommand(const JsonDocument &doc)
{
    const char *cmd_id = nullptr;
    if (!_hasValidCommandEnvelope(doc, cmd_id))
    {
        return false;
    }
    MqttInboundCommand command{};
    command.type = MqttInboundCommandType::GATEWAY_SCAN;
    std::strncpy(command.command_id, cmd_id, sizeof(command.command_id) - 1);
    command.command_id[sizeof(command.command_id) - 1] = '\0';
    ESP_LOGI(TAG, "Accepted gateway scan command: command_id=%s version=%u",
             command.command_id, static_cast<unsigned>(doc["version"].as<uint16_t>()));
    return _enqueueInboundCommand(command);
}

bool MqttClient::_enqueueGatewayClockCommand(const JsonDocument &doc)
{
    // The clock downlink has no treatment/assignment version to compare, so it
    // only needs a valid command_id for ack correlation and deduplication.
    const char *cmd_id = doc["command_id"].as<const char *>();
    if (!isValidMqttCommandId(cmd_id))
    {
        _enqueueInboundRejection(doc, 0, "Invalid clock command envelope");
        return false;
    }

    MqttInboundCommand command{};
    command.type = MqttInboundCommandType::GATEWAY_CLOCK;
    std::strncpy(command.command_id, cmd_id, sizeof(command.command_id) - 1);
    command.command_id[sizeof(command.command_id) - 1] = '\0';

    // Accept either a JSON number or a decimal string (backends differ).
    bool parsed_ok = false;
    if (doc["unix_time_utc"].is<int64_t>())
    {
        command.clock.unix_time_utc = doc["unix_time_utc"].as<int64_t>();
        parsed_ok = true;
    }
    else if (const char *time_str = doc["unix_time_utc"].as<const char *>())
    {
        char *endptr = nullptr;
        command.clock.unix_time_utc = std::strtoll(time_str, &endptr, 10);
        parsed_ok = (endptr != time_str);
    }
    if (doc["tz_offset_s"].is<int32_t>())
    {
        command.clock.tz_offset_s = doc["tz_offset_s"].as<int32_t>();
    }
    const char *local_time = doc["local_time"].as<const char *>();
    if (local_time && local_time[0] != '\0')
    {
        std::strncpy(command.clock.local_time, local_time, sizeof(command.clock.local_time) - 1);
        command.clock.local_time[sizeof(command.clock.local_time) - 1] = '\0';
    }
    // Reject implausible epochs at the edge so a malformed or hostile push can
    // never drive the DS1307 into a year-0 or far-future state.
    command.clock.valid = parsed_ok &&
                          command.clock.unix_time_utc >= CLOCK_UNIX_TIME_MIN_VALID &&
                          command.clock.unix_time_utc <= CLOCK_UNIX_TIME_MAX_VALID;

    return _enqueueInboundCommand(command);
}

bool MqttClient::_enqueueGatewayClaimCommand(const JsonDocument &doc)
{
    const char *cmd_id = nullptr;
    if (!_hasValidCommandEnvelope(doc, cmd_id))
    {
        cmd_id = "claim_cmd";
    }
    uint8_t from_id = 0;
    uint8_t to_id = 0;
    if (doc["from_node_id"].is<uint8_t>())
    {
        from_id = doc["from_node_id"].as<uint8_t>();
    }
    else if (doc["from_id"].is<uint8_t>())
    {
        from_id = doc["from_id"].as<uint8_t>();
    }
    if (doc["to_node_id"].is<uint8_t>())
    {
        to_id = doc["to_node_id"].as<uint8_t>();
    }
    else if (doc["to_id"].is<uint8_t>())
    {
        to_id = doc["to_id"].as<uint8_t>();
    }
    if (!isAguLegacyNodeId(to_id))
    {
        return false;
    }
    MqttInboundCommand command{};
    command.type = MqttInboundCommandType::GATEWAY_CLAIM;
    command.node_id = from_id;
    command.values[0] = to_id;
    std::strncpy(command.command_id, cmd_id, sizeof(command.command_id) - 1);
    return _enqueueInboundCommand(command);
}

void MqttClient::_onMessage(char *topic, uint8_t *payload, unsigned int length)
{
    if (!_instance || !topic || !payload || length >= MQTT_BUFFER_SIZE)
        return;

    JsonDocument doc;
    if (deserializeJson(doc, payload, length))
        return;

    // Control-slot configuration intentionally lives outside /command/ and is
    // retained by the backend. Handle it before command-topic normalization.
    char slot_topic[MQTT_TOPIC_BUFFER_SIZE];
    const int slot_written = snprintf(slot_topic, sizeof(slot_topic), "%s/%s%s",
                                      MQTT_TOPIC_BASE, _instance->_config.device_id,
                                      MQTT_CONFIG_CONTROL_SLOTS_SUFFIX);
    if (slot_written >= 0 && static_cast<size_t>(slot_written) < sizeof(slot_topic) &&
        strcmp(topic, slot_topic) == 0)
    {
        if (_instance->_control_slots_handler)
            _instance->_control_slots_handler(doc);
        return;
    }

    char prefix[128];
    const int written = snprintf(prefix, sizeof(prefix), "%s/%s/command/", MQTT_TOPIC_BASE, _instance->_config.device_id);
    if (written < 0 || static_cast<size_t>(written) >= sizeof(prefix))
        return;

    const size_t prefix_len = strlen(prefix);
    const char *sub_topic = nullptr;
    if (strncmp(topic, prefix, prefix_len) == 0)
    {
        sub_topic = topic + prefix_len;
    }
    else if (strncmp(topic, "aeroponics/command/", 19) == 0)
    {
        sub_topic = topic + 19;
    }
    else
    {
        return;
    }

    // 60-second sliding-window deduplication check: return cached outcome without actuation
    const char *candidate_id = doc["command_id"].as<const char *>();
    if (candidate_id && isValidMqttCommandId(candidate_id))
    {
        MqttCommandOutcomeEntry cached_entry{};
        const uint32_t now = getSystemMillis();
        if (_instance->_dedup_cache.get(candidate_id, now, cached_entry))
        {
            _instance->publishCommandAck(cached_entry.command_id, cached_entry.status,
                                         cached_entry.node_id, cached_entry.reason);
            return;
        }
    }

    if (strcmp(sub_topic, "config/treatment") == 0)
    {
        if (!_instance->_enqueueTreatmentCommand(doc))
        {
            _instance->_enqueueInboundRejection(doc, 0, "Invalid command or inbound queue full");
        }
    }
    else if (strcmp(sub_topic, "config/assignment") == 0)
    {
        if (!_instance->_enqueueAssignmentCommand(doc))
        {
            _instance->_enqueueInboundRejection(doc, 0, "Invalid command or inbound queue full");
        }
    }
    else if (strcmp(sub_topic, "config/group-state") == 0)
    {
        if (!_instance->_enqueueGroupStateCommand(doc))
        {
            _instance->_enqueueInboundRejection(doc, 0, "Invalid group state command or inbound queue full");
        }
    }
    else if (strcmp(sub_topic, "config/flow-policy") == 0)
    {
        if (!_instance->_enqueueFlowPolicyCommand(doc))
        {
            _instance->_enqueueInboundRejection(doc, 0, "Invalid command or inbound queue full");
        }
    }
    else if (strcmp(sub_topic, "config/clock") == 0)
    {
        if (!_instance->_enqueueGatewayClockCommand(doc))
        {
            _instance->_enqueueInboundRejection(doc, 0, "Invalid clock command or inbound queue full");
        }
    }
    else if (strncmp(sub_topic, "node/", 5) == 0)
    {
        _instance->_parseNodeTopic(sub_topic + 5, doc);
    }
    else if (strncmp(sub_topic, "group/", 6) == 0)
    {
        _instance->_parseGroupTopic(sub_topic + 6, doc);
    }
    else if (strncmp(sub_topic, "gateway/", 8) == 0)
    {
        _instance->_parseGatewayTopic(sub_topic + 8, doc);
    }
}

void MqttClient::_applyInboundCommand(const MqttInboundCommand &command)
{
    switch (command.type)
    {
    case MqttInboundCommandType::GATEWAY_SCAN:
    case MqttInboundCommandType::GATEWAY_CLAIM:
    {
        if (_gateway_command_handler)
        {
            _gateway_command_handler(command);
        }
        return;
    }
    case MqttInboundCommandType::ASSIGNMENT:
    {
#if AUTONOMOUS_FALLBACK_ENABLED && FALLBACK_LOCK_FROM_MQTT_OVERWRITE
        ESP_LOGW(TAG, "[AUTONOMOUS] Ignoring MQTT node assignment overwrite for node %u",
                 static_cast<unsigned>(command.node_id));
        _publishReservedCommandAck(command.command_id, "REJECTED", command.node_id,
                                   "Autonomous fallback owns node assignments");
        return;
#else
        const bool accepted = _command_manager &&
                              _command_manager->requestNodeReassignment(command.node_id, command.group_id, command.command_id);
        if (accepted)
        {
            _last_assignment_version = command.values[0];
        }
        _publishReservedCommandAck(command.command_id, accepted ? "ACCEPTED" : "REJECTED", command.node_id,
                                   accepted ? "Safe-off queued; mapping commits after RF OFF ACK" : "Group assignment mutation failed");
        return;
#endif
    }
    case MqttInboundCommandType::FLOW_POLICY:
    {
        const FlowPolicyProvenance source{command.values[6], command.values[7], command.values[8]};
        const bool accepted = _command_manager && _command_manager->provisionNodeControlPolicy(
                                                      command.node_id, command.values[0], command.values[1], static_cast<uint16_t>(command.values[2]),
                                                      static_cast<uint16_t>(command.values[3]), static_cast<uint16_t>(command.values[4]), command.values[5], source);
        if (accepted && isProductionNodeId(command.node_id))
        {
            _last_policy_version[command.node_id] = command.values[6];
        }
        _publishReservedCommandAck(command.command_id, accepted ? "ACCEPTED" : "REJECTED", command.node_id,
                                   accepted ? "Authenticated flow policy provisioned" : "Invalid flow policy limits");
        return;
    }
    case MqttInboundCommandType::TREATMENT:
    {
#if AUTONOMOUS_FALLBACK_ENABLED && FALLBACK_LOCK_FROM_MQTT_OVERWRITE
        ESP_LOGW(TAG, "[AUTONOMOUS] Ignoring MQTT treatment overwrite for group %u",
                 static_cast<unsigned>(command.group_id));
        _publishReservedCommandAck(command.command_id, "REJECTED", 0,
                                   "Autonomous fallback owns treatment schedules");
        return;
#else
        PublishedTreatmentAssignment assignment{};
        assignment.season_id = command.values[0];
        assignment.treatment_version_id = command.values[1];
        assignment.version = command.values[2];
        assignment.profile = GroupProfile{command.values[3], command.values[4], command.values[5], command.values[6]};
        const bool accepted = _group_scheduler && _group_scheduler->applyPublishedTreatment(command.group_id, assignment);
        if (accepted)
            requestScheduleStatePublish();
        if (accepted && command.group_id <= MAX_TIMER_GROUPS)
        {
            _last_treatment_version[command.group_id] = command.values[2];
        }
        _publishReservedCommandAck(command.command_id, accepted ? "ACCEPTED" : "REJECTED", 0,
                                   "Published treatment assignment validation result");
        return;
#endif
    }
    case MqttInboundCommandType::GROUP_STATE:
    {
#if AUTONOMOUS_FALLBACK_ENABLED && FALLBACK_LOCK_FROM_MQTT_OVERWRITE
        ESP_LOGW(TAG, "[AUTONOMOUS] Ignoring MQTT group-state overwrite for group %u",
                 static_cast<unsigned>(command.group_id));
        _publishReservedCommandAck(command.command_id, "REJECTED", 0,
                                   "Autonomous fallback owns group state");
        return;
#else
        const bool accepted = _group_scheduler &&
                              (command.values[0] != 0
                                   ? _group_scheduler->setGroupActive(command.group_id, true)
                                   : _group_scheduler->unassignGroup(command.group_id, "BACKEND_GROUP_STATE"));
        if (accepted)
            requestScheduleStatePublish();
        _publishReservedCommandAck(command.command_id, accepted ? "ACCEPTED" : "REJECTED", 0,
                                   accepted ? "Group state applied" : "Group state mutation failed");
        return;
#endif
    }
    case MqttInboundCommandType::NODE_OVERRIDE:
    {
        ESP_LOGI(TAG, "Applying node override command_id=%s node=%u state=%s",
                 command.command_id, command.node_id,
                 command.desired_state == NodePumpState::ON ? "ON" : "OFF");
        if (_gateway_command_handler)
        {
            _gateway_command_handler(command);
            return;
        }
        const ExternalOverridePolicy policy{command.source, command.values[0], command.values[1]};
        const bool accepted = _command_manager &&
                              _command_manager->queueExternalNodeCommand(command.node_id, command.desired_state, command.command_id, &policy);
        _publishReservedCommandAck(command.command_id, accepted ? "ACCEPTED" : "REJECTED", command.node_id,
                                   accepted ? "Node override accepted and queued" : "Node override mutation failed");
        return;
    }
    case MqttInboundCommandType::GROUP_CONTROL:
    {
#if AUTONOMOUS_FALLBACK_ENABLED && FALLBACK_LOCK_FROM_MQTT_OVERWRITE
        ESP_LOGW(TAG, "[AUTONOMOUS] Ignoring MQTT group-control overwrite for group %u",
                 static_cast<unsigned>(command.group_id));
        _publishReservedCommandAck(command.command_id, "REJECTED", 0,
                                   "Autonomous fallback owns group control");
        return;
#else
        const ExternalOverridePolicy policy{command.source, command.values[0], command.values[1]};
        const bool accepted = _command_manager &&
                              _command_manager->queueExternalGroupCommand(command.group_id, command.desired_state, command.command_id, &policy);
        _publishReservedCommandAck(command.command_id, accepted ? "ACCEPTED" : "REJECTED", 0,
                                   accepted ? "Group control accepted and queued" : "Group prepare failed; no node queued");
        return;
#endif
    }
    case MqttInboundCommandType::GATEWAY_CLOCK:
    {
        // Authoritative backend time-set. Only an out-of-range or absent
        // epoch is rejected; everything else is applied through the
        // injected handler so the MQTT layer stays free of NVS/RTClib.
        if (!command.clock.valid || _clock_adjust_handler == nullptr)
        {
            _publishReservedCommandAck(command.command_id, "REJECTED", 0,
                                       "Invalid or unavailable backend clock reference");
            return;
        }
        _clock_adjust_handler(command.clock.unix_time_utc, command.clock.tz_offset_s);
        _publishReservedCommandAck(command.command_id, "ACCEPTED", 0,
                                   "Backend clock applied to DS1307 and NVS");
        return;
    }
    case MqttInboundCommandType::REJECTION:
        return;
    }
}

void MqttClient::serviceIncomingCommands()
{
    MqttInboundCommand command{};
    while (_dequeueInboundCommand(command))
        _applyInboundCommand(command);
#if defined(UNIT_TEST_HOST) && !defined(MQTT_INTEGRATION_TARGET)
    // Native tests have no dedicated MQTT task; emulate its bounded drain so
    // existing facade tests can observe publication without changing the
    // production ownership contract.
    serviceOutgoingEvents();
#endif
}

int MqttClient::_getRssiDbm() const
{
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    return WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
#else
    return 0;
#endif
}

uint32_t MqttClient::_getFreeHeap() const
{
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    return ESP.getFreeHeap();
#else
    return 0;
#endif
}

bool MqttClient::_isNtpSynced() const
{
    return _currentUnixTime() > 1600000000L;
}

int64_t MqttClient::_currentUnixTime() const
{
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

bool MqttClient::isInitialized() const
{
    return _is_initialized;
}
