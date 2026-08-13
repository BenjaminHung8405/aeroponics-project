#if defined(MQTT_INTEGRATION_TARGET)

#include "integration/ProductionPubSubClient.h"
#include "mqtt_client.h"
#include "nvs_storage.h"
#include "node_registry.h"
#include "command_manager.h"
#include "group_schedule_manager.h"
#include "rf_provisioning.h"
#include "../../test/fakes/FakeClock.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <thread>
#include <chrono>

class LoopbackRfTransport final : public IRfTransport {
public:
    bool begin() override { return true; }
    size_t send(const uint8_t* data, size_t length) override {
        if (length <= sizeof(buffer_)) {
            std::memcpy(buffer_, data, length);
            len_ = length;
            return length;
        }
        return 0;
    }
    size_t receive(uint8_t* buffer, size_t max_length) override {
        size_t read_bytes = (len_ <= max_length) ? len_ : max_length;
        if (read_bytes > 0) {
            std::memcpy(buffer, buffer_, read_bytes);
            len_ -= read_bytes;
        }
        return read_bytes;
    }
    size_t available() override { return len_; }
    void flush() override { len_ = 0; }

private:
    uint8_t buffer_[256] = {};
    size_t len_ = 0;
};

class FileNvsBackend final : public INvsBackend {
public:
    explicit FileNvsBackend(const char* path) : path_(path) { load(); }
    Result flashInit() override { return OK; }
    Result flashErase() override { values_[0] = values_[1] = values_[2] = values_[3] = values_[4] = 0; return save() ? OK : IO_ERROR; }
    bool isOk(Result result) const override { return result == OK; }
    bool isNotFound(Result result) const override { return result == NOT_FOUND; }
    bool requiresFlashErase(Result) const override { return false; }
    const char* errorName(Result) const override { return "FILE_NVS"; }
    Result open(const char*, bool read_only, Handle& handle) override {
        read_only_ = read_only;
        handle = 1;
        return OK;
    }
    Result getU32(Handle, const char* key, uint32_t& value) override {
        const int index = keyIndex(key);
        if (index < 0 || values_[index] == 0) return NOT_FOUND;
        value = values_[index];
        return OK;
    }
    Result setU32(Handle, const char* key, uint32_t value) override {
        const int index = keyIndex(key);
        if (read_only_ || index < 0) return IO_ERROR;
        values_[index] = value;
        return OK;
    }
    Result commit(Handle) override { return read_only_ || save() ? OK : IO_ERROR; }
    Result eraseAll(Handle) override { return flashErase(); }
    void close(Handle) override {}

private:
    static constexpr Result OK = 0;
    static constexpr Result NOT_FOUND = 1;
    static constexpr Result IO_ERROR = -1;
    const char* path_;
    bool read_only_ = false;
    uint32_t values_[5] = {};

    static int keyIndex(const char* key) {
        if (std::strcmp(key, RF_NVS_BOOT_SESSION_KEY) == 0) return 0;
        for (int i = 0; i < 4; ++i) if (std::strcmp(key, RF_NVS_PSK_WORD_KEYS[i]) == 0) return i + 1;
        return -1;
    }
    void load() {
        std::ifstream input(path_, std::ios::binary);
        if (input) input.read(reinterpret_cast<char*>(values_), sizeof(values_));
    }
    bool save() const {
        std::ofstream output(path_, std::ios::binary | std::ios::trunc);
        return output && (output.write(reinterpret_cast<const char*>(values_), sizeof(values_)), output.good());
    }
};

static bool loadConfig(MqttConfig& config) {
    const char* host = std::getenv("MQTT_HOST");
    const char* port = std::getenv("MQTT_PORT");
    const char* device_id = std::getenv("MQTT_DEVICE_ID");
    config = {host ? host : "127.0.0.1",
              static_cast<uint16_t>(std::atoi(port ? port : "1883")),
              std::getenv("MQTT_DEVICE_USER"), std::getenv("MQTT_DEVICE_PASS"),
              device_id ? device_id : "qa-production-device"};
    return config.username && config.password && std::strcmp(config.username, config.device_id) == 0;
}

static bool provisionTestOnlyRf(NvsStorage& storage) {
    const uint32_t test_key_words[4] = {0xA5A5A5A5U, 0x5A5A5A5AU, 0x01234567U, 0x89ABCDEFU};
    if (!storage.setU32(RF_NVS_BOOT_SESSION_KEY, 1)) return false;
    for (size_t i = 0; i < 4; ++i) if (!storage.setU32(RF_NVS_PSK_WORD_KEYS[i], test_key_words[i])) return false;
    return true;
}

int main(int argc, char** argv) {
    const char* mode = argc > 1 ? argv[1] : "heartbeat";
    const char* configured_nvs_path = std::getenv("MQTT_NVS_PATH");
    const char* nvs_path = configured_nvs_path ? configured_nvs_path : "/tmp/aeroponics-production-nvs.bin";
    MqttConfig config{};
    if (!loadConfig(config)) return 2;

    FileNvsBackend backend(nvs_path);
    NvsStorage nvs(&backend);
    FakeClock clock(12, true);
    NodeRegistry registry;
    LoopbackRfTransport rf_transport;
    CommandManager command_mgr;
    GroupScheduleManager group_scheduler;

    if (!nvs.begin() || !provisionTestOnlyRf(nvs) || !registry.init() || !rf_transport.begin() ||
        !command_mgr.begin(&registry, &rf_transport) || !command_mgr.provisionFromNvs(nvs) ||
        !group_scheduler.begin(&clock, &registry)) {
        return 3;
    }

    MqttClient mqtt;
    if (!mqtt.begin(config, &clock, &registry, &command_mgr, &group_scheduler) || !mqtt.connect()) return 4;
    std::puts("PRODUCTION_READY");
    std::fflush(stdout);

    if (std::strcmp(mode, "lwt") == 0) {
        for (;;) { mqtt.loop(); std::this_thread::sleep_for(std::chrono::milliseconds(20)); }
    }
    if (std::strcmp(mode, "heartbeat") == 0) {
        std::this_thread::sleep_for(std::chrono::seconds(10));
        if (!mqtt.publishHeartbeat()) return 5;
        std::puts("PRODUCTION_HEARTBEAT_PUBLISHED");
        return 0;
    }

    // Command verification mode (Gateway Domain: Node Assignment & Override)
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (std::chrono::steady_clock::now() < deadline) {
        mqtt.loop();
        command_mgr.serviceCommandFanout(1000);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        // Assignment ACCEPTED means safe-OFF queued; commit only after the loopback RF ACK.
        uint8_t rx_frame[256] = {};
        const size_t rx_length = rf_transport.receive(rx_frame, sizeof(rx_frame));
        if (rx_length > 0) {
            RfHeader request{};
            uint8_t ignored_payload[64] = {};
            uint8_t ignored_length = 0;
            if (command_mgr.parseFrame(rx_frame, rx_length, request, ignored_payload, ignored_length) &&
                request.message_type == static_cast<uint8_t>(RfMessageType::SET_PUMP)) {
                CommandAckPayload ack{request.sequence, static_cast<uint8_t>(AckOutcome::SUCCESS), 0, 0, {0, 0, 0}};
                uint8_t response[128] = {};
                const size_t response_length = command_mgr.buildFrame(RfMessageType::COMMAND_ACK, 1, request.command_id,
                    reinterpret_cast<const uint8_t*>(&ack), sizeof(ack), response, sizeof(response));
                RfHeader* response_header = reinterpret_cast<RfHeader*>(response);
                response_header->source_node_id = request.target_node_id;
                response_header->target_node_id = 0;
                const uint8_t test_key[16] = {0xA5, 0xA5, 0xA5, 0xA5, 0x5A, 0x5A, 0x5A, 0x5A,
                                              0x67, 0x45, 0x23, 0x01, 0xEF, 0xCD, 0xAB, 0x89};
                const size_t signed_length = sizeof(RfHeader) + sizeof(ack);
                uint8_t mac[HMAC_TAG_SIZE] = {};
                HmacSha256::calculateTruncated(test_key, sizeof(test_key), response, signed_length, mac);
                std::memcpy(response + signed_length, mac, HMAC_TAG_SIZE);
                const uint16_t crc = CommandManager::calculateCrc16(response, signed_length + HMAC_TAG_SIZE);
                response[signed_length + HMAC_TAG_SIZE] = static_cast<uint8_t>(crc);
                response[signed_length + HMAC_TAG_SIZE + 1] = static_cast<uint8_t>(crc >> 8);
                if (response_length > 0) command_mgr.handleIncomingFrame(response, response_length, 1000);
            }
        }
        if (registry.getNodeGroup(1) == 2 || registry.getNodeGroup(3) == 2) {
            std::puts("PRODUCTION_ASSIGNMENT_SAFE_OFF_ACKED");
            return 0;
        }
    }
    return 6;
}

#endif
