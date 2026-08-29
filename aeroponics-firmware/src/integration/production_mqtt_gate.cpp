#if defined(MQTT_INTEGRATION_TARGET)

#include "integration/ProductionPubSubClient.h"
#include "mqtt_client.h"
#include "nvs_storage.h"
#include "node_registry.h"
#include "command_manager.h"
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

struct GateContext {
    explicit GateContext(const char* nvs_path) : backend(nvs_path), nvs(&backend), clock(12, true) {}
    FileNvsBackend backend;
    NvsStorage nvs;
    FakeClock clock;
    NodeRegistry registry;
    LoopbackRfTransport rf_transport;
    CommandManager command_mgr;
    MqttClient mqtt;
};

static bool initializeGate(GateContext& context, const MqttConfig& config) {
    if (!context.nvs.begin() || !provisionTestOnlyRf(context.nvs) || !context.registry.init() ||
        !context.rf_transport.begin() || !context.command_mgr.begin(&context.registry, &context.rf_transport) ||
        !context.command_mgr.provisionFromNvs(context.nvs)) {
        return false;
    }
    return context.mqtt.begin(config, &context.clock, &context.registry, &context.command_mgr, nullptr) &&
           context.mqtt.connect();
}

static int runHeartbeat(GateContext& context) {
    std::this_thread::sleep_for(std::chrono::seconds(10));
    if (!context.mqtt.publishHeartbeat()) return 5;
    std::puts("PRODUCTION_HEARTBEAT_PUBLISHED");
    return 0;
}

static void sendLoopbackNodeAck(GateContext& context, const RfHeader& request) {
    CommandAckPayload ack{request.sequence, static_cast<uint8_t>(AckOutcome::SUCCESS), 0, 0, {0, 0, 0}};
    uint8_t response[128] = {};
    const uint8_t test_key[16] = {0xA5, 0xA5, 0xA5, 0xA5, 0x5A, 0x5A, 0x5A, 0x5A,
                                  0x67, 0x45, 0x23, 0x01, 0xEF, 0xCD, 0xAB, 0x89};
    const RfFrameMetadata node_metadata{request.target_node_id, 0, request.boot_session_id, 1, request.command_id};
    const size_t response_length = RfFrameCodec::encodeFrame(node_metadata, RfMessageType::COMMAND_ACK,
        &ack, sizeof(ack), test_key, sizeof(test_key), response, sizeof(response));
    if (response_length != 0) context.command_mgr.handleIncomingFrame(response, response_length, 1000);

    TelemetryPayload telemetry{0, 0, 0, 0, 0, 0, request.command_id};
    const RfFrameMetadata telemetry_metadata{request.target_node_id, 0, request.boot_session_id, 2,
                                              request.command_id};
    const size_t telemetry_length = RfFrameCodec::encodeFrame(telemetry_metadata, RfMessageType::TELEMETRY,
        &telemetry, sizeof(telemetry), test_key, sizeof(test_key), response, sizeof(response));
    if (telemetry_length != 0) context.command_mgr.handleIncomingFrame(response, telemetry_length, 1001);
}

static int runCommandVerification(GateContext& context) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (std::chrono::steady_clock::now() < deadline) {
        context.mqtt.loop();
        context.mqtt.serviceIncomingCommands();
        context.command_mgr.serviceCommandFanout(1000);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        uint8_t rx_frame[256] = {};
        const size_t rx_length = context.rf_transport.receive(rx_frame, sizeof(rx_frame));
        RfHeader request{};
        uint8_t ignored_payload[64] = {};
        uint8_t ignored_length = 0;
        if (rx_length > 0 && context.command_mgr.parseFrame(rx_frame, rx_length, request, ignored_payload, ignored_length) &&
            request.message_type == static_cast<uint8_t>(RfMessageType::SET_PUMP)) {
            sendLoopbackNodeAck(context, request);
        }
        if (context.registry.getNodeGroup(1) == 2 || context.registry.getNodeGroup(3) == 2) {
            std::puts("PRODUCTION_ASSIGNMENT_SAFE_OFF_ACKED");
            return 0;
        }
    }
    return 6;
}

static int runPolicyProvisioningVerification(GateContext& context) {
    if (!context.registry.assignNodeToGroup(1, 1) ||
        !context.registry.updateTelemetry(1, NodePumpState::OFF, 0, 0, 0, 1)) {
        return 7;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (std::chrono::steady_clock::now() < deadline) {
        context.mqtt.loop();
        context.mqtt.serviceIncomingCommands();
        context.command_mgr.serviceCommandFanout(1000);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return 0;
}

int main(int argc, char** argv) {
    const char* mode = argc > 1 ? argv[1] : "heartbeat";
    const char* nvs_path = std::getenv("MQTT_NVS_PATH");
    MqttConfig config{};
    if (!loadConfig(config)) return 2;
    GateContext context(nvs_path ? nvs_path : "/tmp/aeroponics-production-nvs.bin");
    if (!initializeGate(context, config)) return 3;
    if (std::strcmp(mode, "policy") == 0 && !context.mqtt.connect()) return 4;
    std::puts("PRODUCTION_READY");
    std::fflush(stdout);
    if (std::strcmp(mode, "lwt") == 0) {
        for (;;) { context.mqtt.loop(); std::this_thread::sleep_for(std::chrono::milliseconds(20)); }
    }
    if (std::strcmp(mode, "heartbeat") == 0) return runHeartbeat(context);
    if (std::strcmp(mode, "policy") == 0) return runPolicyProvisioningVerification(context);
    return runCommandVerification(context);
}

#endif
