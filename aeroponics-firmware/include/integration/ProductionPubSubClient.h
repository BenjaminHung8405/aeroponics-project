#pragma once

#include <cstdint>

/**
 * Minimal MQTT 3.1.1 transport used only by the native production integration
 * target. It implements the PubSubClient surface consumed by MqttClient while
 * speaking to the real Mosquitto broker over a TCP socket.
 */
class ProductionPubSubClient {
public:
    using Callback = void (*)(char*, uint8_t*, unsigned int);

    ProductionPubSubClient();
    ~ProductionPubSubClient();

    void setServer(const char* host, uint16_t port);
    void setCallback(Callback callback);
    void setBufferSize(uint16_t size);
    void setKeepAlive(uint16_t seconds);
    bool connect(const char* id, const char* user, const char* pass,
                 const char* will_topic, uint8_t will_qos, bool will_retain,
                 const char* will_message);
    void disconnect();
    bool publish(const char* topic, const char* payload, bool retained = false);
    bool subscribe(const char* topic, uint8_t qos = 0);
    bool loop();
    bool connected() const;
    int state() const;

private:
    int socket_;
    const char* host_;
    uint16_t port_;
    Callback callback_;
    uint16_t keep_alive_s_;
    uint16_t packet_id_;
    bool connected_;
};

using PubSubClient = ProductionPubSubClient;
