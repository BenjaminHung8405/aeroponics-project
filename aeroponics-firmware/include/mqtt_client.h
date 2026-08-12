#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include "config.h"
#include <ArduinoJson.h>
#include "node_registry.h"
#include "command_manager.h"
#include "core/IClock.h"

#if defined(MQTT_INTEGRATION_TARGET)
#include "integration/ProductionPubSubClient.h"
#elif defined(ESP_PLATFORM) || defined(ARDUINO)
#include <WiFiClient.h>
#include <PubSubClient.h>
#elif !defined(MQTT_INTEGRATION_TARGET)
/**
 * @brief Lightweight mock PubSubClient for host unit test build environment.
 */
class PubSubClient {
public:
    typedef void (*Callback)(char*, uint8_t*, unsigned int);
    PubSubClient() : _connected(false), _callback(nullptr) {}
    explicit PubSubClient(void* client) : _connected(false), _callback(nullptr) {}
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
        if (topic) std::strncpy(_last_topic, topic, sizeof(_last_topic) - 1);
        if (payload) std::strncpy(_last_payload, payload, sizeof(_last_payload) - 1);
        _last_topic[sizeof(_last_topic) - 1] = '\0';
        _last_payload[sizeof(_last_payload) - 1] = '\0';
        _last_retained = retained;
        return _publish_result;
    }
    bool subscribe(const char* topic, uint8_t qos = 0) { return _subscribe_result; }
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

public:
    const char* lastPayload() const { return _last_payload; }
    const char* lastTopic() const { return _last_topic; }
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
               CommandManager* command_manager = nullptr);

    /**
     * @brief Establish MQTT connection with LWT, authentication, and topics subscription.
     * @return true if connected and subscribed successfully, false otherwise.
     */
    bool connect();

    /**
     * @brief Process MQTT client background loop (keep-alive, incoming messages).
     */
    void loop();

    /**
     * @brief Publish heartbeat telemetry status JSON to broker.
     * @return true if published successfully, false otherwise.
     */
    bool publishHeartbeat();

    /**
     * @brief Publish group telemetry summary to gateway domain topic.
     */
    bool publishGroupTelemetry(uint8_t group_id, uint32_t active_nodes_mask, const char* state_str);

    /**
     * @brief Publish node state snapshot to gateway domain topic.
     */
    bool publishNodeSnapshot(uint8_t node_id, const NodeState& state);

    /**
     * @brief Publish command acknowledgment to gateway domain topic ack/{command_id}.
     */
    bool publishCommandAck(const char* command_id, const char* status, uint8_t node_id = 0, const char* reason = nullptr);
    void publishCommandOutcome(const char* command_id, const char* status,
                               uint8_t node_id, const char* reason) override;
    void publishSafetyAudit(const char* event, const char* reason) override;

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
    uint32_t _last_heartbeat_ms;
    bool _is_initialized;
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

    void _handleAssignmentCommand(const JsonDocument& doc);
    void _handleNodeOverrideCommand(uint8_t node_id, const JsonDocument& doc);
    void _handleGroupControlCommand(uint8_t group_id, const JsonDocument& doc);
    bool _hasValidCommandEnvelope(const JsonDocument& doc, const char*& command_id) const;

    bool _buildTopic(char* buffer, size_t buffer_size, const char* suffix) const;
    bool _buildClientId(char* buffer, size_t buffer_size) const;
    bool _getTimestamp(char* buffer, size_t buffer_size) const;

    static MqttClient* _instance;
};
