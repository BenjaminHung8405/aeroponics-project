#if defined(MQTT_INTEGRATION_TARGET)

#include "integration/ProductionPubSubClient.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <vector>

namespace {
void appendString(std::vector<uint8_t>& out, const char* value) {
    const size_t length = value ? strlen(value) : 0;
    out.push_back(static_cast<uint8_t>(length >> 8));
    out.push_back(static_cast<uint8_t>(length));
    out.insert(out.end(), value, value + length);
}

bool sendAll(int fd, const uint8_t* data, size_t length) {
    while (length > 0) {
        const ssize_t written = send(fd, data, length, 0);
        if (written <= 0) return false;
        data += written;
        length -= static_cast<size_t>(written);
    }
    return true;
}

bool readAll(int fd, uint8_t* data, size_t length) {
    while (length > 0) {
        const ssize_t count = recv(fd, data, length, 0);
        if (count <= 0) return false;
        data += count;
        length -= static_cast<size_t>(count);
    }
    return true;
}

bool readRemainingLength(int fd, size_t& length) {
    length = 0;
    size_t multiplier = 1;
    for (int i = 0; i < 4; ++i) {
        uint8_t byte = 0;
        if (!readAll(fd, &byte, 1)) return false;
        length += (byte & 127U) * multiplier;
        if ((byte & 128U) == 0) return true;
        multiplier *= 128;
    }
    return false;
}

bool writePacket(int fd, uint8_t header, const std::vector<uint8_t>& body) {
    std::vector<uint8_t> packet{header};
    size_t remaining = body.size();
    do {
        uint8_t encoded = static_cast<uint8_t>(remaining % 128);
        remaining /= 128;
        if (remaining > 0) encoded |= 128;
        packet.push_back(encoded);
    } while (remaining > 0);
    packet.insert(packet.end(), body.begin(), body.end());
    return sendAll(fd, packet.data(), packet.size());
}

int openSocket(const char* host, uint16_t port) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* result = nullptr;
    char service[8];
    snprintf(service, sizeof(service), "%u", port);
    if (getaddrinfo(host, service, &hints, &result) != 0) return -1;
    int fd = -1;
    for (addrinfo* entry = result; entry != nullptr; entry = entry->ai_next) {
        fd = socket(entry->ai_family, entry->ai_socktype, entry->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, entry->ai_addr, entry->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(result);
    return fd;
}
} // namespace

ProductionPubSubClient::ProductionPubSubClient()
    : socket_(-1), host_(nullptr), port_(1883), callback_(nullptr), keep_alive_s_(30),
      packet_id_(1), connected_(false) {}

ProductionPubSubClient::~ProductionPubSubClient() {
    disconnect();
}

void ProductionPubSubClient::setServer(const char* host, uint16_t port) { host_ = host; port_ = port; }
void ProductionPubSubClient::setCallback(Callback callback) { callback_ = callback; }
void ProductionPubSubClient::setBufferSize(uint16_t) {}
void ProductionPubSubClient::setKeepAlive(uint16_t seconds) { keep_alive_s_ = seconds; }

bool ProductionPubSubClient::connect(const char* id, const char* user, const char* pass,
                                     const char* will_topic, uint8_t will_qos, bool will_retain,
                                     const char* will_message) {
    disconnect();
    socket_ = openSocket(host_, port_);
    if (socket_ < 0) return false;
    std::vector<uint8_t> body{0, 4, 'M', 'Q', 'T', 'T', 4, 0xC2,
                              static_cast<uint8_t>(keep_alive_s_ >> 8),
                              static_cast<uint8_t>(keep_alive_s_)};
    body[7] = static_cast<uint8_t>(0x02 | (user ? 0x80 : 0) | (pass ? 0x40 : 0) |
                                   (will_topic ? 0x04 : 0) | (will_retain ? 0x20 : 0) |
                                   ((will_qos & 3U) << 3));
    appendString(body, id);
    if (will_topic) {
        appendString(body, will_topic);
        appendString(body, will_message);
    }
    if (user) appendString(body, user);
    if (pass) appendString(body, pass);
    if (!writePacket(socket_, 0x10, body)) return false;
    uint8_t header = 0;
    if (!readAll(socket_, &header, 1) || header != 0x20) return false;
    size_t length = 0;
    uint8_t response[4]{};
    if (!readRemainingLength(socket_, length) || length != 2 || !readAll(socket_, response, 2) || response[1] != 0) {
        return false;
    }
    connected_ = true;
    return true;
}

void ProductionPubSubClient::disconnect() {
    if (socket_ >= 0) {
        if (connected_) writePacket(socket_, 0xE0, {});
        close(socket_);
    }
    socket_ = -1;
    connected_ = false;
}

bool ProductionPubSubClient::publish(const char* topic, const char* payload, bool retained) {
    if (!connected_) return false;
    std::vector<uint8_t> body;
    appendString(body, topic);
    body.insert(body.end(), payload, payload + strlen(payload));
    return writePacket(socket_, static_cast<uint8_t>(0x30 | (retained ? 1 : 0)), body);
}

bool ProductionPubSubClient::subscribe(const char* topic, uint8_t qos) {
    if (!connected_) return false;
    std::vector<uint8_t> body{static_cast<uint8_t>(packet_id_ >> 8), static_cast<uint8_t>(packet_id_)};
    const uint16_t id = packet_id_++;
    body[0] = static_cast<uint8_t>(id >> 8);
    body[1] = static_cast<uint8_t>(id);
    appendString(body, topic);
    body.push_back(qos);
    return writePacket(socket_, 0x82, body);
}

bool ProductionPubSubClient::loop() {
    if (!connected_) return false;
    fd_set set;
    FD_ZERO(&set);
    FD_SET(socket_, &set);
    timeval timeout{0, 100000};
    if (select(socket_ + 1, &set, nullptr, nullptr, &timeout) <= 0) return true;
    uint8_t header = 0;
    if (!readAll(socket_, &header, 1)) { connected_ = false; return false; }
    size_t length = 0;
    if (!readRemainingLength(socket_, length)) { connected_ = false; return false; }
    std::vector<uint8_t> body(length);
    if (!readAll(socket_, body.data(), body.size())) { connected_ = false; return false; }
    if ((header & 0xF0) == 0x30 && callback_ && body.size() >= 2) {
        const size_t topic_length = (body[0] << 8) | body[1];
        if (topic_length + 2 <= body.size()) {
            std::vector<char> topic(body.begin() + 2, body.begin() + 2 + topic_length);
            topic.push_back('\0');
            size_t payload_start = 2 + topic_length;
            if ((header & 0x06) != 0) payload_start += 2; // QoS 1/2 packet identifier.
            if (payload_start > body.size()) return false;
            std::vector<uint8_t> payload(body.begin() + payload_start, body.end());
            callback_(topic.data(), payload.data(), static_cast<unsigned int>(payload.size()));
        }
    }
    return true;
}

bool ProductionPubSubClient::connected() const { return connected_; }
int ProductionPubSubClient::state() const { return connected_ ? 0 : -1; }

#endif
