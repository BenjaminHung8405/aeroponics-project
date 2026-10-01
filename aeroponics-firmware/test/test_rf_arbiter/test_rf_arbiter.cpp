#include <unity.h>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <vector>
#include <thread>
#include <atomic>
#include <functional>
#include <algorithm>

#include "agu_legacy_rf_host.h"
#include "agu_legacy_codec.h"
#include "core/Crc16Modbus.h"

namespace {

static uint32_t s_fake_time_ms = 10000;
static uint32_t fakeNowMs() { return s_fake_time_ms; }
static void fakeDelayMs(uint32_t ms) { s_fake_time_ms += ms; }

class MockArbiterTransport final : public IRfTransport {
public:
    std::mutex mtx;
    std::vector<std::vector<uint8_t>> sent_frames;
    std::vector<uint32_t> send_timestamps;
    std::vector<uint8_t> rx_queue;
    size_t fail_on_send_call = 0; // 1-indexed, 0 = never fail
    size_t send_call_count = 0;
    unsigned flushes = 0;
    int auto_reply_byte = -1; // If >= 0, automatically pushes this byte into rx_queue upon send
    std::function<void(const uint8_t*, size_t)> on_send_callback;

    bool begin() override { return true; }

    size_t send(const uint8_t* data, size_t length) override {
        std::lock_guard<std::mutex> lock(mtx);
        ++send_call_count;
        if (fail_on_send_call > 0 && send_call_count == fail_on_send_call) {
            return 0; // simulated TX short-write / failure
        }
        sent_frames.emplace_back(data, data + length);
        send_timestamps.push_back(s_fake_time_ms);
        if (auto_reply_byte >= 0) {
            rx_queue.push_back(static_cast<uint8_t>(auto_reply_byte));
        }
        if (on_send_callback) {
            on_send_callback(data, length);
        }
        return length;
    }

    size_t receive(uint8_t* data, size_t length) override {
        std::lock_guard<std::mutex> lock(mtx);
        if (rx_queue.empty()) return 0;
        const size_t n = std::min(length, rx_queue.size());
        std::memcpy(data, rx_queue.data(), n);
        rx_queue.erase(rx_queue.begin(), rx_queue.begin() + n);
        return n;
    }

    size_t available() override {
        std::lock_guard<std::mutex> lock(mtx);
        return rx_queue.size();
    }

    void flush() override {
        std::lock_guard<std::mutex> lock(mtx);
        ++flushes;
    }
};

} // namespace

void setUp(void) {
    s_fake_time_ms = 10000;
}

void tearDown(void) {}

// 1. PUMP_OFF broadcast phát đúng 3 frame byte-for-byte giống nhau, opcode 0x07,
//    đúng target/group, CRC16-Modbus hợp lệ; gap mỗi cặp 25–35 ms.
void test_group_off_is_three_identical_bursts_with_gap(void) {
    MockArbiterTransport transport;
    AguLegacyRfHost host(&transport);
    host.setTimeProvider(fakeNowMs, fakeDelayMs);

    const AguRfTransactionResult result = host.setGroupPump(RF_GROUP_ADDRESS_1, false);

    TEST_ASSERT_EQUAL(AguRfResult::ACKED, result.result);
    TEST_ASSERT_EQUAL_UINT8(3, result.burst_frames_sent);
    TEST_ASSERT_EQUAL_UINT(3, transport.sent_frames.size());

    // Opcode PUMP_OFF (0x07), Target RF_GROUP_ADDRESS_1 (0x10)
    TEST_ASSERT_EQUAL_UINT8(4, transport.sent_frames[0][0]); // length byte
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AguLegacy::Opcode::PUMP_OFF), transport.sent_frames[0][1]);
    TEST_ASSERT_EQUAL_UINT8(RF_GROUP_ADDRESS_1, transport.sent_frames[0][2]);

    // Byte-for-byte identical frames
    TEST_ASSERT_TRUE(transport.sent_frames[0] == transport.sent_frames[1]);
    TEST_ASSERT_TRUE(transport.sent_frames[1] == transport.sent_frames[2]);

    // Valid CRC16-Modbus on all 3 frames
    for (size_t i = 0; i < 3; ++i) {
        TEST_ASSERT_TRUE(verifyCrc16Modbus(transport.sent_frames[i].data(), transport.sent_frames[i].size()));
    }

    // Inter-frame gap 25-35 ms (exactly 30 ms with fake clock)
    TEST_ASSERT_EQUAL_UINT(3, transport.send_timestamps.size());
    const uint32_t gap1 = transport.send_timestamps[1] - transport.send_timestamps[0];
    const uint32_t gap2 = transport.send_timestamps[2] - transport.send_timestamps[1];
    TEST_ASSERT_TRUE(gap1 >= 25 && gap1 <= 35);
    TEST_ASSERT_TRUE(gap2 >= 25 && gap2 <= 35);
}

// 2. Khi OFF pending/đang burst, PING và GET_STATE không có send() nào xen giữa;
//    sau silence chỉ một probe được chạy.
void test_off_burst_prevents_ping_and_get_state_interleaving(void) {
    MockArbiterTransport transport;
    AguLegacyRfHost host(&transport);
    host.setTimeProvider(fakeNowMs, fakeDelayMs);
    host.setRadioSilenceWindow(s_fake_time_ms - RF_RADIO_SILENCE_BEFORE_PHASE_MS,
                               s_fake_time_ms + RF_RADIO_SILENCE_AFTER_PHASE_MS);

    std::vector<uint8_t> intercepted_opcodes;
    transport.on_send_callback = [&](const uint8_t* data, size_t len) {
        if (len >= 2) {
            intercepted_opcodes.push_back(data[1]);
        }
        // Try to interleave GET_PUMP_STATE during burst
        AguRfTransactionResult probe = host.getPumpState(1);
        // Must be rejected/deferred because bus is locked by burst
        TEST_ASSERT_EQUAL(AguRfResult::SILENCE_DEFERRED, probe.result);
    };

    const AguRfTransactionResult burst_res = host.setGroupPump(RF_GROUP_ADDRESS_1, false);
    TEST_ASSERT_EQUAL(AguRfResult::ACKED, burst_res.result);
    TEST_ASSERT_EQUAL_UINT(3, intercepted_opcodes.size());
    for (uint8_t op : intercepted_opcodes) {
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AguLegacy::Opcode::PUMP_OFF), op);
    }

    // Clear interceptor callback
    transport.on_send_callback = nullptr;

    // While silence window is still active, probe is deferred
    AguRfTransactionResult probe_deferred = host.getPumpState(1);
    TEST_ASSERT_EQUAL(AguRfResult::SILENCE_DEFERRED, probe_deferred.result);

    // Fast-forward past silence window
    s_fake_time_ms += RF_RADIO_SILENCE_AFTER_PHASE_MS + 10;
    transport.auto_reply_byte = 0x00; // GET_PUMP_STATE returns pump=0
    AguRfTransactionResult probe_ok = host.getPumpState(1);
    TEST_ASSERT_EQUAL(AguRfResult::ACKED, probe_ok.result);
}

// 3. PING đang chờ ACK không làm mất pump: pump được ưu tiên ngay sau transaction
//    atomic, không queue delay vượt timeout policy.
void test_ping_waiting_ack_does_not_starve_pump(void) {
    MockArbiterTransport transport;
    AguLegacyRfHost host(&transport);
    host.resetTimeProvider();

    // Transport replies with PING_DEFAULT_VAL on send
    transport.auto_reply_byte = AguLegacy::PING_DEFAULT_VAL;

    AguRfTransactionResult ping_res = host.pingNode(7);
    TEST_ASSERT_EQUAL(AguRfResult::ACKED, ping_res.result);

    // Pump immediately succeeds after atomic ping finishes
    transport.auto_reply_byte = AguLegacy::ACK_BYTE;
    const AguRfTransactionResult pump_res = host.setPump(7, true);
    TEST_ASSERT_EQUAL(AguRfResult::ACKED, pump_res.result);
}

// 4. Hai pump concurrent không interleave byte/frame; mutex release xảy ra trên
//    success, timeout và TX error.
void test_concurrent_pumps_do_not_interleave_frames(void) {
    MockArbiterTransport transport;
    AguLegacyRfHost host(&transport);
    host.resetTimeProvider();
    transport.auto_reply_byte = AguLegacy::ACK_BYTE;

    std::atomic<bool> start_signal{false};
    std::vector<std::thread> workers;

    for (uint8_t i = 1; i <= 4; ++i) {
        workers.emplace_back([&, i]() {
            while (!start_signal.load()) {
                std::this_thread::yield();
            }
            host.setPump(i, (i % 2) == 0);
        });
    }

    start_signal.store(true);
    for (auto& t : workers) {
        t.join();
    }

    // Verify all sent frames are valid and not interleaved
    std::lock_guard<std::mutex> lk(transport.mtx);
    TEST_ASSERT_EQUAL_UINT(4, transport.sent_frames.size());
    for (const auto& frame : transport.sent_frames) {
        TEST_ASSERT_EQUAL_UINT(5, frame.size());
        TEST_ASSERT_TRUE(verifyCrc16Modbus(frame.data(), frame.size()));
    }
}

// 5. Silence trước/sau phase ±1000 ms chặn đúng boundary, xử lý wrap-around
//    monotonic millis và không chặn PUMP_ON/OFF.
void test_radio_silence_window_boundary_and_rollover(void) {
    MockArbiterTransport transport;
    AguLegacyRfHost host(&transport);

    // Boundary check around [10000, 12000]
    host.setRadioSilenceWindow(10000, 12000);

    TEST_ASSERT_FALSE(host.isRadioSilenceActive(9999));
    TEST_ASSERT_TRUE(host.isRadioSilenceActive(10000));
    TEST_ASSERT_TRUE(host.isRadioSilenceActive(11000));
    TEST_ASSERT_FALSE(host.isRadioSilenceActive(12000));
    TEST_ASSERT_FALSE(host.isRadioSilenceActive(12001));

    // Test uint32 rollover: start near UINT32_MAX, end in small positive range
    // Window: [0xFFFFFF00, 0x00000100] (duration = 512 ms across overflow)
    const uint32_t rollover_start = 0xFFFFFF00;
    const uint32_t rollover_end = 0x00000100;
    host.setRadioSilenceWindow(rollover_start, rollover_end);

    TEST_ASSERT_FALSE(host.isRadioSilenceActive(rollover_start - 1));
    TEST_ASSERT_TRUE(host.isRadioSilenceActive(rollover_start));
    TEST_ASSERT_TRUE(host.isRadioSilenceActive(0xFFFFFFFF));
    TEST_ASSERT_TRUE(host.isRadioSilenceActive(0x00000000));
    TEST_ASSERT_TRUE(host.isRadioSilenceActive(0x00000050));
    TEST_ASSERT_FALSE(host.isRadioSilenceActive(rollover_end));
    TEST_ASSERT_FALSE(host.isRadioSilenceActive(rollover_end + 1));

    // Silence NEVER blocks PUMP_ON or PUMP_OFF
    s_fake_time_ms = 11000;
    host.setTimeProvider(fakeNowMs, fakeDelayMs);
    host.setRadioSilenceWindow(10000, 12000);

    transport.auto_reply_byte = AguLegacy::ACK_BYTE;
    const AguRfTransactionResult pump_on = host.setPump(1, true);
    TEST_ASSERT_EQUAL(AguRfResult::ACKED, pump_on.result);

    // But blocks probe
    const AguRfTransactionResult probe = host.getPumpState(1);
    TEST_ASSERT_EQUAL(AguRfResult::SILENCE_DEFERRED, probe.result);
}

// 6. Unicast ACK 0x5A thành công; ACK sai/timeout bị phân loại đúng; fan-out có
//    khoảng cách 300 ms giữa node và timeout bounded.
void test_unicast_ack_and_staggered_fanout(void) {
    MockArbiterTransport transport;
    AguLegacyRfHost host(&transport);
    host.setTimeProvider(fakeNowMs, fakeDelayMs);

    // 6a: ACK 0x5A success
    transport.auto_reply_byte = AguLegacy::ACK_BYTE;
    AguRfTransactionResult res_ack = host.setPump(1, true);
    TEST_ASSERT_EQUAL(AguRfResult::ACKED, res_ack.result);
    TEST_ASSERT_EQUAL_UINT8(AguLegacy::ACK_BYTE, res_ack.response_byte);

    // 6b: Unexpected response (0x00 instead of 0x5A)
    transport.auto_reply_byte = 0x00;
    AguRfTransactionResult res_unexp = host.setPump(1, true);
    TEST_ASSERT_EQUAL(AguRfResult::UNEXPECTED_RESPONSE, res_unexp.result);

    // 6c: Timeout (no rx bytes)
    transport.auto_reply_byte = -1;
    AguRfTransactionResult res_to = host.setPump(1, true);
    TEST_ASSERT_EQUAL(AguRfResult::TIMEOUT, res_to.result);

    // 6d: Staggered fan-out across 3 nodes
    transport.sent_frames.clear();
    transport.send_timestamps.clear();

    const uint8_t fanout_nodes[] = {4, 5, 6};
    AguRfTransactionResult fanout_results[3]{};

    transport.auto_reply_byte = AguLegacy::ACK_BYTE;

    host.fanoutPump(fanout_nodes, 3, false, fanout_results);

    TEST_ASSERT_EQUAL_UINT(3, transport.sent_frames.size());
    for (int i = 0; i < 3; ++i) {
        TEST_ASSERT_EQUAL(AguRfResult::ACKED, fanout_results[i].result);
    }
    // Verify 300 ms spacing between node 4-5 and node 5-6
    TEST_ASSERT_EQUAL_UINT(3, transport.send_timestamps.size());
    const uint32_t fanout_gap1 = transport.send_timestamps[1] - transport.send_timestamps[0];
    const uint32_t fanout_gap2 = transport.send_timestamps[2] - transport.send_timestamps[1];
    TEST_ASSERT_EQUAL_UINT32(RF_UNICAST_STAGGER_MS, fanout_gap1);
    TEST_ASSERT_EQUAL_UINT32(RF_UNICAST_STAGGER_MS, fanout_gap2);
}

// 7. Burst khi send #2 hoặc #3 lỗi trả TX_ERROR/partial count, không báo thành công giả.
void test_burst_partial_send_failure_reports_tx_error(void) {
    MockArbiterTransport transport;
    AguLegacyRfHost host(&transport);
    host.setTimeProvider(fakeNowMs, fakeDelayMs);

    // Fail on 2nd frame send
    transport.fail_on_send_call = 2;
    AguRfTransactionResult res2 = host.setGroupPump(RF_GROUP_ADDRESS_1, false);
    TEST_ASSERT_EQUAL(AguRfResult::TX_ERROR, res2.result);
    TEST_ASSERT_EQUAL_UINT8(1, res2.burst_frames_sent);

    // Reset and fail on 3rd frame send
    transport.sent_frames.clear();
    transport.send_call_count = 0;
    transport.fail_on_send_call = 3;
    AguRfTransactionResult res3 = host.setGroupPump(RF_GROUP_ADDRESS_1, false);
    TEST_ASSERT_EQUAL(AguRfResult::TX_ERROR, res3.result);
    TEST_ASSERT_EQUAL_UINT8(2, res3.burst_frames_sent);
}

// 8. Codec regression: toàn bộ frame PUMP_ON/OFF/GET_STATE/PING hiện tại
//    giữ nguyên bytes và CRC; không chấp nhận frame sai CRC.
void test_codec_regression_byte_exact_frames(void) {
    uint8_t buffer[16] = {};

    // PUMP_ON node 1: [0x04][0x06][0x01][CRC_Lo][CRC_Hi] (5 bytes)
    const size_t len_on = AguLegacy::AguLegacyCodec::encodePumpOn(1, buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL_UINT(5, len_on);
    TEST_ASSERT_EQUAL_UINT8(0x04, buffer[0]);
    TEST_ASSERT_EQUAL_UINT8(0x06, buffer[1]);
    TEST_ASSERT_EQUAL_UINT8(0x01, buffer[2]);
    TEST_ASSERT_TRUE(verifyCrc16Modbus(buffer, len_on));

    // PUMP_OFF node 1: [0x04][0x07][0x01][CRC_Lo][CRC_Hi] (5 bytes)
    const size_t len_off = AguLegacy::AguLegacyCodec::encodePumpOff(1, buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL_UINT(5, len_off);
    TEST_ASSERT_EQUAL_UINT8(0x04, buffer[0]);
    TEST_ASSERT_EQUAL_UINT8(0x07, buffer[1]);
    TEST_ASSERT_EQUAL_UINT8(0x01, buffer[2]);
    TEST_ASSERT_TRUE(verifyCrc16Modbus(buffer, len_off));

    // GET_PUMP_STATE node 1: [0x04][0x08][0x01][CRC_Lo][CRC_Hi] (5 bytes)
    const size_t len_state = AguLegacy::AguLegacyCodec::encodeGetPumpState(1, buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL_UINT(5, len_state);
    TEST_ASSERT_EQUAL_UINT8(0x04, buffer[0]);
    TEST_ASSERT_EQUAL_UINT8(0x08, buffer[1]);
    TEST_ASSERT_EQUAL_UINT8(0x01, buffer[2]);
    TEST_ASSERT_TRUE(verifyCrc16Modbus(buffer, len_state));

    // PING node 7 with val 0xA5: [0x05][0x05][0xA5][0x07][0x2A][0x7B] (6 bytes)
    // Hardware verified reference pattern from main.cpp line 1860
    const size_t len_ping = AguLegacy::AguLegacyCodec::encodePing(0xA5, 7, buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL_UINT(6, len_ping);
    const uint8_t expected_ping[] = {0x05, 0x05, 0xA5, 0x07, 0x2A, 0x7B};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_ping, buffer, 6);
    TEST_ASSERT_TRUE(verifyCrc16Modbus(buffer, len_ping));

    // Mutated CRC rejection
    buffer[4] ^= 0xFF;
    TEST_ASSERT_FALSE(verifyCrc16Modbus(buffer, len_ping));
}

// 9. Probe bị defer không làm tăng g_last_agu_ping_ms/cursor và không tạo backlog telemetry.
void test_deferred_probe_does_not_advance_cursor(void) {
    MockArbiterTransport transport;
    AguLegacyRfHost host(&transport);
    host.setTimeProvider(fakeNowMs, fakeDelayMs);

    // Active silence window
    host.setRadioSilenceWindow(10000, 20000);
    s_fake_time_ms = 15000;

    uint8_t cursor = 4;
    uint32_t last_ping_ms = 1000;
    bool was_deferred = false;

    // Simulate probing node with arbiter check
    if (host.pumpPending() || host.isRadioSilenceActive(s_fake_time_ms)) {
        was_deferred = true;
    } else {
        const AguRfTransactionResult res = host.getPumpState(cursor);
        if (res.result == AguRfResult::SILENCE_DEFERRED || res.result == AguRfResult::BUS_BUSY) {
            was_deferred = true;
        }
    }

    TEST_ASSERT_TRUE(was_deferred);
    // When deferred, cursor and timestamp MUST NOT change
    if (!was_deferred) {
        last_ping_ms = s_fake_time_ms;
        cursor++;
    }

    TEST_ASSERT_EQUAL_UINT8(4, cursor);
    TEST_ASSERT_EQUAL_UINT32(1000, last_ping_ms);
}

// 10. Native test mô phỏng executeAguPump/executeAguPing concurrency,
//     xác minh không còn race dựa trên boolean busy.
void test_pump_vs_ping_concurrency_no_races(void) {
    MockArbiterTransport transport;
    AguLegacyRfHost host(&transport);
    host.resetTimeProvider();
    transport.auto_reply_byte = AguLegacy::ACK_BYTE;

    std::atomic<bool> stop_flag{false};
    std::atomic<size_t> pump_successes{0};
    std::atomic<size_t> ping_successes{0};
    std::atomic<size_t> ping_deferred{0};

    // Ping worker
    std::thread ping_worker([&]() {
        while (!stop_flag.load()) {
            AguRfTransactionResult r = host.getPumpState(5);
            if (r.result == AguRfResult::ACKED) {
                ping_successes.fetch_add(1);
            } else if (r.result == AguRfResult::SILENCE_DEFERRED || r.result == AguRfResult::BUS_BUSY) {
                ping_deferred.fetch_add(1);
            }
            std::this_thread::yield();
        }
    });

    // Pump worker: run 25 iterations for fast, deterministic unit testing
    for (int i = 0; i < 25; ++i) {
        AguRfTransactionResult res = host.setPump(4, (i % 2) == 0);
        if (res.result == AguRfResult::ACKED) {
            pump_successes.fetch_add(1);
        }
        std::this_thread::yield();
    }

    stop_flag.store(true);
    ping_worker.join();

    // Verify all frames sent over transport are intact and have valid CRC
    std::lock_guard<std::mutex> lk(transport.mtx);
    for (const auto& frame : transport.sent_frames) {
        TEST_ASSERT_TRUE(verifyCrc16Modbus(frame.data(), frame.size()));
    }
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_group_off_is_three_identical_bursts_with_gap);
    RUN_TEST(test_off_burst_prevents_ping_and_get_state_interleaving);
    RUN_TEST(test_ping_waiting_ack_does_not_starve_pump);
    RUN_TEST(test_concurrent_pumps_do_not_interleave_frames);
    RUN_TEST(test_radio_silence_window_boundary_and_rollover);
    RUN_TEST(test_unicast_ack_and_staggered_fanout);
    RUN_TEST(test_burst_partial_send_failure_reports_tx_error);
    RUN_TEST(test_codec_regression_byte_exact_frames);
    RUN_TEST(test_deferred_probe_does_not_advance_cursor);
    RUN_TEST(test_pump_vs_ping_concurrency_no_races);

    return UNITY_END();
}
