#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <atomic>
#if !defined(ESP_PLATFORM) && !defined(ARDUINO)
#include <mutex>
#endif
#include "config.h"
#include <ArduinoJson.h>
#include "node_registry.h"
#include "command_manager.h"
#include "group_schedule_manager.h"
#include "core/IClock.h"
#include "node_fsm.h"

#if defined(MQTT_INTEGRATION_TARGET)
#include "integration/ProductionPubSubClient.h"
#elif defined(ESP_PLATFORM) || defined(ARDUINO)
#include <WiFiClient.h>
#include <PubSubClient.h>
#include <freertos/semphr.h>
#elif !defined(MQTT_INTEGRATION_TARGET)
/**
 * @brief Lightweight mock PubSubClient for host unit test build environment.
 */
class PubSubClient {
public:
    typedef void (*Callback)(char*, uint8_t*, unsigned int);
    PubSubClient() : _connected(false), _callback(nullptr) {}
    explicit PubSubClient(void* client) : _connected(false), _callback(nullptr) {}
    void setClient(void* client) {}
    void setServer(const char* host, uint16_t port) {}
    void setCallback(Callback callback) { _callback = callback; }
    void setBufferSize(uint16_t size) {}
    void setKeepAlive(uint16_t keepAlive) {}
    bool connect(const char* id, const char* user, const char* pass,
                 const char* willTopic, uint8_t willQos, bool willRetain,
                 const char* willMessage) {
        _connected = _connect_result;
        return _connect_result;
    }
    void disconnect() { _connected = false; }
    bool publish(const char* topic, const char* payload, bool retained = false) {
        ++_publish_call_count;
        if (topic) std::strncpy(_last_topic, topic, sizeof(_last_topic) - 1);
        if (payload) std::strncpy(_last_payload, payload, sizeof(_last_payload) - 1);
        _last_topic[sizeof(_last_topic) - 1] = '\0';
        _last_payload[sizeof(_last_payload) - 1] = '\0';
        _last_retained = retained;
        if (_publish_result && topic && _published_topic_count < MAX_PUBLISHED_TOPICS) {
            std::strncpy(_published_topics[_published_topic_count], topic, MQTT_TOPIC_BUFFER_SIZE - 1);
            _published_topics[_published_topic_count][MQTT_TOPIC_BUFFER_SIZE - 1] = '\0';
            if (payload) std::strncpy(_published_payloads[_published_topic_count], payload,
                                      MQTT_HEARTBEAT_PAYLOAD_SIZE - 1);
            _published_payloads[_published_topic_count][MQTT_HEARTBEAT_PAYLOAD_SIZE - 1] = '\0';
            _published_retained[_published_topic_count] = retained;
            ++_published_topic_count;
        }
        return _publish_result;
    }
    bool subscribe(const char* topic, uint8_t qos = 0) {
        (void)qos;
        if (topic && _subscription_count < MAX_SUBSCRIPTIONS) {
            std::strncpy(_subscriptions[_subscription_count], topic, MQTT_TOPIC_BUFFER_SIZE - 1);
            _subscriptions[_subscription_count][MQTT_TOPIC_BUFFER_SIZE - 1] = '\0';
            ++_subscription_count;
        }
        return _subscribe_result;
    }
    bool loop() { return true; }
    bool connected() const { return _connected; }
    int state() const { return 0; }
    void simulateMessage(char* topic, uint8_t* payload, unsigned int length) {
        if (_callback) _callback(topic, payload, length);
    }
    void setConnectResult(bool result) { _connect_result = result; }
    void setPublishResult(bool result) { _publish_result = result; }
    void setSubscribeResult(bool result) { _subscribe_result = result; }

private:
    bool _connected;
    bool _connect_result = true;
    bool _publish_result = true;
    bool _subscribe_result = true;
    Callback _callback;
    char _last_topic[MQTT_TOPIC_BUFFER_SIZE] = {};
    char _last_payload[MQTT_HEARTBEAT_PAYLOAD_SIZE] = {};
    bool _last_retained = false;
    static constexpr size_t MAX_SUBSCRIPTIONS = 8;
    char _subscriptions[MAX_SUBSCRIPTIONS][MQTT_TOPIC_BUFFER_SIZE] = {};
    size_t _subscription_count = 0;
    static constexpr size_t MAX_PUBLISHED_TOPICS = 128;
    char _published_topics[MAX_PUBLISHED_TOPICS][MQTT_TOPIC_BUFFER_SIZE] = {};
    char _published_payloads[MAX_PUBLISHED_TOPICS][MQTT_HEARTBEAT_PAYLOAD_SIZE] = {};
    bool _published_retained[MAX_PUBLISHED_TOPICS] = {};
    size_t _published_topic_count = 0;

public:
    const char* lastPayload() const { return _last_payload; }
    const char* lastTopic() const { return _last_topic; }
    bool wasSubscribedTo(const char* topic) const {
        if (!topic) return false;
        for (size_t i = 0; i < _subscription_count; ++i) {
            if (std::strcmp(_subscriptions[i], topic) == 0) return true;
        }
        return false;
    }
    size_t publishCallCount() const { return _publish_call_count; }
    size_t publishedTopicCount() const { return _published_topic_count; }
    const char* publishedTopic(size_t index) const {
        return index < _published_topic_count ? _published_topics[index] : "";
    }
    const char* publishedPayload(size_t index) const {
        return index < _published_topic_count ? _published_payloads[index] : "";
    }
    bool publishedRetained(size_t index) const {
        return index < _published_topic_count && _published_retained[index];
    }
    size_t _publish_call_count = 0;
};
#endif

/**
 * @brief MQTT configuration structure holding broker connection credentials and device ID.
 */
struct MqttConfig {
    const char* broker_host;
    uint16_t broker_port;
    const char* username;
    const char* password;
    const char* device_id;
};

enum class MqttInboundCommandType : uint8_t {
    ASSIGNMENT = 0, FLOW_POLICY, TREATMENT, NODE_OVERRIDE, GROUP_CONTROL, REJECTION, GATEWAY_SCAN, GATEWAY_CLAIM
};

/** Parsed callback handoff. CommandManager is deliberately not referenced here. */
struct MqttInboundCommand {
    MqttInboundCommandType type = MqttInboundCommandType::ASSIGNMENT;
    char command_id[65] = {};
    uint8_t node_id = 0;
    uint8_t group_id = 0;
    NodePumpState desired_state = NodePumpState::OFF;
    uint32_t values[9] = {};
    char source[16] = {};
    char rejection_reason[80] = {};
    bool ack_reserved = false;
};

/** Fully serialized event handed to the MQTT owner task for publication. */
struct MqttOutboundEvent {
    char topic[MQTT_TOPIC_BUFFER_SIZE] = {};
    char payload[MQTT_TELEMETRY_PAYLOAD_SIZE] = {};
    bool retained = false;
};

struct MqttCommandOutcomeEntry {
    char command_id[65] = {};
    char status[16] = {};
    char reason[80] = {};
    uint32_t timestamp_ms = 0;
    uint8_t node_id = 0;
    bool active = false;
};

class CommandDeduplicationCache {
public:
    static constexpr size_t CAPACITY = 64;
    static constexpr uint32_t TTL_MS = 60000;

    void put(const char* command_id, const char* status, uint8_t node_id, const char* reason, uint32_t now_ms) {
        if (!command_id || command_id[0] == '\0') return;
        for (size_t i = 0; i < CAPACITY; ++i) {
            if (entries_[i].active && std::strncmp(entries_[i].command_id, command_id, sizeof(entries_[i].command_id)) == 0) {
                if (status) {
                    std::strncpy(entries_[i].status, status, sizeof(entries_[i].status) - 1);
                    entries_[i].status[sizeof(entries_[i].status) - 1] = '\0';
                }
                if (reason) {
                    std::strncpy(entries_[i].reason, reason, sizeof(entries_[i].reason) - 1);
                    entries_[i].reason[sizeof(entries_[i].reason) - 1] = '\0';
                }
                if (node_id > 0) entries_[i].node_id = node_id;
                entries_[i].timestamp_ms = now_ms;
                return;
            }
        }
        MqttCommandOutcomeEntry& entry = entries_[tail_];
        entry.active = true;
        std::strncpy(entry.command_id, command_id, sizeof(entry.command_id) - 1);
        entry.command_id[sizeof(entry.command_id) - 1] = '\0';
        if (status) {
            std::strncpy(entry.status, status, sizeof(entry.status) - 1);
            entry.status[sizeof(entry.status) - 1] = '\0';
        } else {
            entry.status[0] = '\0';
        }
        if (reason) {
            std::strncpy(entry.reason, reason, sizeof(entry.reason) - 1);
            entry.reason[sizeof(entry.reason) - 1] = '\0';
        } else {
            entry.reason[0] = '\0';
        }
        entry.node_id = node_id;
        entry.timestamp_ms = now_ms;
        tail_ = (tail_ + 1U) % CAPACITY;
    }

    bool get(const char* command_id, uint32_t now_ms, MqttCommandOutcomeEntry& out_entry) const {
        if (!command_id || command_id[0] == '\0') return false;
        for (size_t i = 0; i < CAPACITY; ++i) {
            if (entries_[i].active && std::strncmp(entries_[i].command_id, command_id, sizeof(entries_[i].command_id)) == 0) {
                if (now_ms - entries_[i].timestamp_ms <= TTL_MS) {
                    out_entry = entries_[i];
                    return true;
                }
                return false;
            }
        }
        return false;
    }

    void clear() {
        for (size_t i = 0; i < CAPACITY; ++i) {
            entries_[i].active = false;
        }
        tail_ = 0;
    }

private:
    MqttCommandOutcomeEntry entries_[CAPACITY] = {};
    size_t tail_ = 0;
};

/**
 * @brief Facade class wrapping PubSubClient and handling MQTT communications,
 * telemetries, commands, and LWT for production aeroponics gateway.
 */
class MqttClient : public ICommandOutcomeSink {
public:
    MqttClient();
    ~MqttClient();

    /**
     * @brief Initialize MqttClient facade with configuration and dependency injection.
     * @param config MqttConfig structure containing broker details and credentials.
     * @param rtc Optional pointer to IClock instance.
     * @param registry Optional pointer to NodeRegistry instance.
     * @return true if mandatory dependency pointers and host are non-null.
     */
    bool begin(MqttConfig config, IClock* rtc = nullptr, NodeRegistry* registry = nullptr,
               CommandManager* command_manager = nullptr,
               GroupScheduleManager* group_scheduler = nullptr);

    /**
     * @brief Establish MQTT connection with LWT, authentication, and topics subscription.
     * @return true if connected and subscribed successfully, false otherwise.
     */
    bool connect();

    /**
     * @brief Process MQTT client background loop (keep-alive, incoming messages).
     */
    void loop();
    /** MQTT task only: drain outbound events and publish through PubSubClient. */
    void serviceOutgoingEvents();
    /** Main loop owns CommandManager mutation and drains parsed MQTT DTOs here. */
    void serviceIncomingCommands();

    /**
     * @brief Publish heartbeat telemetry status JSON to broker.
     * @return true if published successfully, false otherwise.
     */
    bool publishHeartbeat();

    /** MQTT task only: enqueue the periodic heartbeat after a connection. */
    bool publishConnectedHeartbeat();

    /**
     * @brief Publish group telemetry summary to gateway domain topic.
     */
    bool publishGroupTelemetry(uint8_t group_id, uint32_t active_nodes_mask, const char* state_str);

    struct NodeSnapshotContext {
        const char* override_state = "NONE";
        uint32_t override_expiry_ms = 0;
        const char* last_command_id = nullptr;
        const char* last_command_result = nullptr;
        uint32_t last_ping_at = 0;
        bool last_ping_ok = false;
        uint32_t ping_rtt_ms = 0;
        uint16_t consecutive_ping_failures = 0;
        const char* reset_reason = nullptr;
        const char* source = nullptr;
        const char* transition_reason = nullptr;
    };

    /**
     * @brief Publish node state snapshot to gateway domain topic.
     */
    bool publishNodeSnapshot(uint8_t node_id, const NodeState& state, const NodeSnapshotContext* context = nullptr);

    void setResetReason(const char* reason) { _reset_reason = reason; }

    /** Publish exactly one command-admission result to gateway topic ack/{command_id}. */
    bool publishCommandAck(const char* command_id, const char* status, uint8_t node_id = 0, const char* reason = nullptr);
    /** Publish RF lifecycle progression to telemetry/command/{command_id}/event. */
    bool publishCommandEvent(const char* command_id, const char* status, uint8_t node_id, const char* reason);
    /** Publish lifecycle event to per-node topic aeroponics/v1/node/{nodeId}/event. */
    bool publishLifecycleEvent(uint8_t node_id, const char* mqtt_command_id, LifecycleEvent event);
    void publishCommandOutcome(const char* command_id, const char* status,
                               uint8_t node_id, const char* reason) override;
    void publishSafetyAudit(const char* event, const char* reason) override;

    using GatewayCommandHandler = void (*)(const MqttInboundCommand& command);
    void setGatewayCommandHandler(GatewayCommandHandler handler) { _gateway_command_handler = handler; }

    struct DiscoveredRfNodeInfo {
        uint8_t node_id = 0;
        bool online = false;
        uint32_t rtt_ms = 0;
        uint32_t boot_session_id = 0;
        // Legacy SCI: 0=none, 1=timeout, 2=unexpected response,
        // 3=transport error, 4=invalid node, 5=UART not ready.
        uint8_t failure_code = 0;
    };
    bool publishScanResults(const char* scan_id, const DiscoveredRfNodeInfo* nodes, size_t count,
                            uint32_t duration_ms, const char* status = "COMPLETED",
                            const char* error = nullptr);

    /**
     * @brief Disconnect and discard all injected MQTT facade state.
     */
    void reset();

    /**
     * @brief Check whether MQTT client is currently connected to broker.
     * @return true if connected, false otherwise.
     */
    bool isConnected() const;

    /** @brief Return whether begin() completed and has not been rolled back. */
    bool isInitialized() const;

#if defined(UNIT_TEST_HOST) && !defined(MQTT_INTEGRATION_TARGET)
    /**
     * @brief Helper for host unit testing to simulate an incoming MQTT message callback.
     */
    void simulateIncomingMessage(char* topic, uint8_t* payload, unsigned int length) {
        _pubsub.simulateMessage(topic, payload, length);
    }
    void setMockConnectResult(bool result) { _pubsub.setConnectResult(result); }
    void setMockPublishResult(bool result) { _pubsub.setPublishResult(result); }
    void setMockSubscribeResult(bool result) { _pubsub.setSubscribeResult(result); }
    const char* mockLastPublishedPayload() const { return _pubsub.lastPayload(); }
    const char* mockLastPublishedTopic() const { return _pubsub.lastTopic(); }
    size_t mockPublishCallCount() const { return _pubsub.publishCallCount(); }
    size_t mockPublishedTopicCount() const { return _pubsub.publishedTopicCount(); }
    const char* mockPublishedTopic(size_t index) const { return _pubsub.publishedTopic(index); }
    const char* mockPublishedPayload(size_t index) const { return _pubsub.publishedPayload(index); }
    bool mockPublishedRetained(size_t index) const { return _pubsub.publishedRetained(index); }
    bool mockWasSubscribedTo(const char* topic) const { return _pubsub.wasSubscribedTo(topic); }
    void setMockUnixTime(int64_t unix_time) { _mock_unix_time = unix_time; }
#endif

private:
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    WiFiClient _wifi_client;
#endif

    PubSubClient _pubsub;
    MqttConfig _config;
    IClock* _rtc;
    NodeRegistry* _registry;
    CommandManager* _command_manager;
    GroupScheduleManager* _group_scheduler;
    uint32_t _last_heartbeat_ms;
    bool _is_initialized;
    std::atomic<bool> _connected{false};
    const char* _reset_reason = nullptr;
#if defined(UNIT_TEST_HOST) && !defined(MQTT_INTEGRATION_TARGET)
    int64_t _mock_unix_time;
#endif

    bool _buildLwtPayload(char* buffer, size_t buffer_size) const;
    bool _subscribeCommandTopics();
    int _getRssiDbm() const;
    uint32_t _getFreeHeap() const;
    bool _isNtpSynced() const;
    int64_t _currentUnixTime() const;

    static void _onMessage(char* topic, uint8_t* payload, unsigned int length);
    void _parseNodeTopic(const char* ptr, const JsonDocument& doc);
    void _parseGroupTopic(const char* ptr, const JsonDocument& doc);
    void _parseGatewayTopic(const char* ptr, const JsonDocument& doc);

    bool _enqueueGatewayScanCommand(const JsonDocument& doc);
    bool _enqueueGatewayClaimCommand(const JsonDocument& doc);
    bool _enqueueAssignmentCommand(const JsonDocument& doc);
    bool _enqueueFlowPolicyCommand(const JsonDocument& doc);
    bool _enqueueTreatmentCommand(const JsonDocument& doc);
    bool _enqueueNodeOverrideCommand(uint8_t node_id, const JsonDocument& doc);
    bool _enqueueGroupControlCommand(uint8_t group_id, const JsonDocument& doc);
    bool _enqueueInboundCommand(const MqttInboundCommand& command);
    bool _dequeueInboundCommand(MqttInboundCommand& command);
    void _applyInboundCommand(const MqttInboundCommand& command);
    bool _enqueueInboundRejection(const JsonDocument& doc, uint8_t node_id, const char* reason);
    bool _reserveCommandAck();
    bool _enqueueBackpressureRejection(const char* command_id, uint8_t node_id);
    bool _publishReservedCommandAck(const char* command_id, const char* status,
                                    uint8_t node_id, const char* reason);
    bool _hasValidCommandEnvelope(const JsonDocument& doc, const char*& command_id) const;
    bool _enqueueOutboundEvent(const char* topic, const char* payload, bool retained,
                               bool critical = false);
    bool _peekOutboundEvent(MqttOutboundEvent& event, bool& critical, bool& backpressure_failure);
    bool _discardOutboundEvent(bool critical, bool backpressure_failure);
    bool _queueJsonEvent(const char* topic, const JsonDocument& doc, bool retained);
    void _publishQueueOverflowAudit();

    bool _buildTopic(char* buffer, size_t buffer_size, const char* suffix) const;
    bool _buildClientId(char* buffer, size_t buffer_size) const;
    bool _getTimestamp(char* buffer, size_t buffer_size) const;

    MqttInboundCommand _inbound_commands[MQTT_INBOUND_COMMAND_QUEUE_DEPTH] = {};
    size_t _inbound_head = 0;
    size_t _inbound_tail = 0;
    size_t _inbound_count = 0;
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    SemaphoreHandle_t _inbound_mutex = nullptr;
    SemaphoreHandle_t _outbound_mutex = nullptr;
#else
    std::mutex _inbound_mutex;
    std::mutex _outbound_mutex;
#endif
    MqttOutboundEvent _outbound_ack_events[MQTT_OUTBOUND_ACK_QUEUE_DEPTH] = {};
    MqttOutboundEvent _backpressure_failure_events[MQTT_BACKPRESSURE_FAILURE_QUEUE_DEPTH] = {};
    MqttOutboundEvent _outbound_telemetry_events[MQTT_OUTBOUND_TELEMETRY_QUEUE_DEPTH] = {};
    size_t _outbound_ack_head = 0;
    size_t _outbound_ack_tail = 0;
    size_t _outbound_ack_count = 0;
    size_t _reserved_ack_count = 0;
    size_t _backpressure_failure_head = 0;
    size_t _backpressure_failure_tail = 0;
    size_t _backpressure_failure_count = 0;
    size_t _outbound_telemetry_head = 0;
    size_t _outbound_telemetry_tail = 0;
    size_t _outbound_telemetry_count = 0;
    std::atomic<uint32_t> _inbound_rejected{0};
    std::atomic<uint32_t> _outbound_dropped{0};
    uint32_t _last_queue_audit_ms = 0;
    bool _last_outbound_publish_ok = true;

    CommandDeduplicationCache _dedup_cache;
    uint32_t _last_treatment_version[MAX_TIMER_GROUPS + 1] = {};
    uint32_t _last_assignment_version = 0;
    uint32_t _last_policy_version[RF_PRODUCTION_MAX_NODE_ID + 1] = {};
    GatewayCommandHandler _gateway_command_handler = nullptr;

    static MqttClient* _instance;
};
