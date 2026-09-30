#include <unity.h>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <vector>

#include "agu_legacy_rf_host.h"

namespace {
class MockTransport final : public IRfTransport {
public:
    bool begin() override { return true; }
    size_t send(const uint8_t* data, size_t length) override {
        frames.emplace_back(data, data + length);
        send_times.push_back(std::chrono::steady_clock::now());
        return length;
    }
    size_t receive(uint8_t* data, size_t length) override {
        if (rx.empty()) return 0;
        const size_t n = length < rx.size() ? length : rx.size();
        std::memcpy(data, rx.data(), n);
        rx.erase(rx.begin(), rx.begin() + static_cast<std::ptrdiff_t>(n));
        return n;
    }
    size_t available() override { return rx.size(); }
    void flush() override { ++flushes; }

    std::vector<std::vector<uint8_t>> frames;
    std::vector<std::chrono::steady_clock::time_point> send_times;
    std::vector<uint8_t> rx;
    unsigned flushes = 0;
};
}

void setUp(void) {}
void tearDown(void) {}

void test_group_off_is_three_identical_bursts_with_gap(void) {
    MockTransport transport;
    AguLegacyRfHost host(&transport);
    const AguRfTransactionResult result = host.setGroupPump(RF_GROUP_ADDRESS_1, false);
    TEST_ASSERT_EQUAL(AguRfResult::ACKED, result.result);
    TEST_ASSERT_EQUAL_UINT8(3, result.burst_frames_sent);
    TEST_ASSERT_EQUAL_UINT(3, transport.frames.size());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AguLegacy::Opcode::PUMP_OFF), transport.frames[0][1]);
    TEST_ASSERT_TRUE(transport.frames[0] == transport.frames[1]);
    TEST_ASSERT_TRUE(transport.frames[1] == transport.frames[2]);
    for (size_t i = 1; i < transport.send_times.size(); ++i) {
        const auto gap = std::chrono::duration_cast<std::chrono::milliseconds>(
            transport.send_times[i] - transport.send_times[i - 1]).count();
        TEST_ASSERT_TRUE(gap >= 25 && gap <= 60);
    }
}

void test_silence_defers_probe_but_allows_pump(void) {
    MockTransport transport;
    AguLegacyRfHost host(&transport);
    host.setRadioSilenceWindow(0, 1000000);
    const AguRfTransactionResult probe = host.getPumpState(1);
    TEST_ASSERT_EQUAL(AguRfResult::SILENCE_DEFERRED, probe.result);
    transport.rx.push_back(AguLegacy::ACK_BYTE);
    const AguRfTransactionResult pump = host.setPump(1, true);
    TEST_ASSERT_NOT_EQUAL(AguRfResult::SILENCE_DEFERRED, pump.result);
}
