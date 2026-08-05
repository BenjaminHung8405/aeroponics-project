#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include "config.h"
#include <ArduinoJson.h>
#include "schedule_manager.h"
#include "core/IRelayOutput.h"

#if defined(ESP_PLATFORM) || defined(ARDUINO)
#include <WiFiClient.h>
#include <PubSubClient.h>
#else
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
 * Security: Uses const char* pointers. Caller manages memory lifetime; no heap std::string allocation.
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
 * telemetries, commands, and LWT for aeroponics firmware.
 */
class MqttClient {
public:
    MqttClient();
    ~MqttClient();

    /**
     * @brief Initialize MqttClient facade with configuration and dependency injection.
     * @param config MqttConfig structure containing broker details and credentials.
     * @param sm Pointer to ScheduleManager instance.
     * @param rc Pointer to RelayController instance.
     * @param rtc Optional pointer to IClock instance.
     * @return true if mandatory dependency pointers and host are non-null.
     */
    bool begin(MqttConfig config, ScheduleManager* sm, IRelayOutput* rc, IClock* rtc = nullptr);

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
     * @brief Publish individual relay state telemetry JSON to broker.
     * @param relay_id Zero-based relay index [0..TOTAL_RELAYS-1] or [1..TOTAL_RELAYS].
     * @param state Reference to RelayRuntimeState snapshot.
     * @return true if published successfully, false otherwise.
     */
    bool publishRelayTelemetry(uint8_t relay_id, const RelayRuntimeState& state);

    /**
     * @brief Check whether MQTT client is currently connected to broker.
     * Must be const as per architectural constraint.
     * @return true if connected, false otherwise.
     */
    bool isConnected() const;

#if !defined(ESP_PLATFORM) && !defined(ARDUINO)
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
#endif

private:

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    WiFiClient _wifi_client;
#endif

    PubSubClient _pubsub;
    MqttConfig _config;
    ScheduleManager* _sm;
    IRelayOutput* _rc;
    IClock* _rtc;
    uint32_t _last_heartbeat_ms;
    bool _is_initialized;

    /**
     * @brief Build LWT JSON payload for offline status using StaticJsonDocument<256>.
     * Template Method pattern helper.
     * @param buffer Output char buffer.
     * @param buffer_size Size of the buffer.
     * @return true if serialization succeeded and fit in buffer.
     */
    bool _buildLwtPayload(char* buffer, size_t buffer_size) const;
    bool _subscribeCommandTopics();
    int _getRssiDbm() const;
    uint32_t _getFreeHeap() const;
    bool _isNtpSynced() const;

    /**
     * @brief Static helper to parse relay_id [1..TOTAL_RELAYS] and command type from MQTT topic string.
     * @param topic Null-terminated topic string.
     * @param out_cmd_type Optional pointer to store extracted command type string pointer ("schedule" or "override").
     * @return 1..TOTAL_RELAYS if valid, or -1 if invalid/unparseable.
     */
    static int8_t _parseRelayId(const char* topic, const char** out_cmd_type = nullptr);

    /**
     * @brief Static callback function for incoming MQTT messages.
     * Avoids floating global callback functions by encapsulating within header & class.
     */
    static void _onMessage(char* topic, uint8_t* payload, unsigned int length);

    bool _buildTopic(char* buffer, size_t buffer_size, const char* suffix) const;
    bool _buildRelayTopic(char* buffer, size_t buffer_size, const char* suffix,
                          uint8_t relay_id) const;
    bool _buildClientId(char* buffer, size_t buffer_size) const;
    bool _getTimestamp(char* buffer, size_t buffer_size) const;
    bool _parseSchedule(JsonDocument& doc, uint8_t relay_id, RelayProfile& profile) const;
    bool _parseOverride(JsonDocument& doc, uint8_t relay_id);

    /**
     * @brief Singleton/instance pointer for static callback routing.
     */
    static MqttClient* _instance;
};
