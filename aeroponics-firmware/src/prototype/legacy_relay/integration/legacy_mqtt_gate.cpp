#if defined(MQTT_INTEGRATION_TARGET) && defined(LEGACY_RELAY_SUPPORT)

#include "integration/ProductionPubSubClient.h"
#include "mqtt_client.h"
#include "nvs_backend.h"
#include "prototype/legacy_relay/legacy_relay_profile_repository.h"
#include "prototype/legacy_relay/schedule_manager.h"
#include "fakes/FakeClock.h"
#include "test_prototype/fakes/FakeRelayOutput.h"
#include "test_prototype/fakes/FakeTaskRunner.h"
#include "fakes/FakeWatchdog.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <thread>
#include <chrono>

class FileNvsBackend final : public INvsBackend {
public:
    explicit FileNvsBackend(const char* path) : path_(path) {}
    Result flashInit() override { return OK; }
    Result flashErase() override { std::remove(path_); return OK; }
    bool isOk(Result result) const override { return result == OK; }
    bool isNotFound(Result result) const override { return result == NOT_FOUND; }
    bool requiresFlashErase(Result) const override { return false; }
    const char* errorName(Result) const override { return "FILE_NVS"; }
    Result open(const char*, bool read_only, Handle& handle) override {
        read_only_ = read_only;
        std::ifstream in(path_, std::ios::binary);
        if (in.good()) in.read(reinterpret_cast<char*>(values_), sizeof(values_));
        handle = 1;
        return OK;
    }
    Result getU32(Handle, const char* key, uint32_t& value) override {
        const int field = fieldFor(key);
        if (field < 0 || !fileExists()) return NOT_FOUND;
        value = values_[field];
        return OK;
    }
    Result setU32(Handle, const char* key, uint32_t value) override {
        const int field = fieldFor(key);
        if (field < 0 || read_only_) return IO_ERROR;
        values_[field] = value;
        return OK;
    }
    Result commit(Handle) override {
        if (read_only_) return OK;
        std::ofstream out(path_, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(values_), sizeof(values_));
        return out.good() ? OK : IO_ERROR;
    }
    Result eraseAll(Handle) override { std::memset(values_, 0, sizeof(values_)); return OK; }
    void close(Handle) override {}

private:
    static constexpr Result OK = 0;
    static constexpr Result NOT_FOUND = 1;
    static constexpr Result IO_ERROR = -1;
    static int fieldFor(const char* key) {
        if (std::strncmp(key, "sd_", 3) == 0) return 0;
        if (std::strncmp(key, "cd_", 3) == 0) return 1;
        if (std::strncmp(key, "sn_", 3) == 0) return 2;
        if (std::strncmp(key, "cn_", 3) == 0) return 3;
        return -1;
    }
    bool fileExists() const { std::ifstream in(path_, std::ios::binary); return in.good(); }
    const char* path_;
    bool read_only_ = false;
    uint32_t values_[4] = {DEFAULT_SPRAY_DAY_S, DEFAULT_COOLDOWN_DAY_S,
                           DEFAULT_SPRAY_NIGHT_S, DEFAULT_COOLDOWN_NIGHT_S};
};

static bool loadConfig(MqttConfig& config) {
    const char* host = std::getenv("MQTT_HOST");
    const char* port = std::getenv("MQTT_PORT");
    const char* device_id = std::getenv("MQTT_DEVICE_ID");
    config = {host ? host : "127.0.0.1",
              static_cast<uint16_t>(std::atoi(port ? port : "1883")),
              std::getenv("MQTT_DEVICE_USER"), std::getenv("MQTT_DEVICE_PASS"),
              device_id ? device_id : "qa-prototype-device"};
    return config.username && config.password;
}

int main(int argc, char** argv) {
    const char* mode = argc > 1 ? argv[1] : "heartbeat";
    const char* configured_nvs_path = std::getenv("MQTT_NVS_PATH");
    const char* nvs_path = configured_nvs_path ? configured_nvs_path : "/tmp/aeroponics-prototype-nvs.bin";
    MqttConfig config{};
    if (!loadConfig(config)) return 2;

    FileNvsBackend backend(nvs_path);
    LegacyRelayProfileRepository repo(&backend);
    FakeClock clock(12, true);
    FakeRelayOutput relay;
    FakeWatchdog watchdog;
    FakeTaskRunner runner;
    ScheduleManager schedule;
    if (!repo.begin() || !schedule.begin(&repo, &clock, &relay, &watchdog, &runner)) return 3;

    MqttClient mqtt;
    if (!mqtt.begin(config, &clock) || !mqtt.connect()) return 4;
    std::puts("PROTOTYPE_READY");
    std::fflush(stdout);
    if (std::strcmp(mode, "lwt") == 0) {
        for (;;) { mqtt.loop(); std::this_thread::sleep_for(std::chrono::milliseconds(20)); }
    }
    if (std::strcmp(mode, "heartbeat") == 0) {
        std::this_thread::sleep_for(std::chrono::seconds(10));
        if (!mqtt.publishHeartbeat()) return 5;
        std::puts("PROTOTYPE_HEARTBEAT_PUBLISHED");
        return 0;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (std::chrono::steady_clock::now() < deadline) {
        mqtt.loop();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        RelayProfile profile{};
        if (repo.loadProfile(0, profile) && profile.spray_day_s == 25) {
            std::printf("PROTOTYPE_COMMAND_PERSISTED spray_day_s=%u\n", profile.spray_day_s);
            return 0;
        }
    }
    return 6;
}

#endif
