#pragma once

#include <cstddef>
#include <cstdint>
#include "core/hmac_sha256.h"

constexpr uint8_t RF_SOF_BYTE_1 = 0xAA;
constexpr uint8_t RF_SOF_BYTE_2 = 0x55;
constexpr uint8_t RF_PROTOCOL_VERSION = 0x01;
constexpr size_t RF_HEADER_SIZE = 17;
constexpr size_t RF_MAX_PAYLOAD_SIZE = 64;
constexpr size_t RF_MAX_FRAME_SIZE = RF_HEADER_SIZE + RF_MAX_PAYLOAD_SIZE + HMAC_TAG_SIZE + 2;

inline void writeU16Le(uint8_t* out, uint16_t value) {
    out[0] = static_cast<uint8_t>(value & 0xFFU);
    out[1] = static_cast<uint8_t>((value >> 8) & 0xFFU);
}
inline void writeU32Le(uint8_t* out, uint32_t value) {
    out[0] = static_cast<uint8_t>(value & 0xFFU);
    out[1] = static_cast<uint8_t>((value >> 8) & 0xFFU);
    out[2] = static_cast<uint8_t>((value >> 16) & 0xFFU);
    out[3] = static_cast<uint8_t>((value >> 24) & 0xFFU);
}
inline uint16_t readU16Le(const uint8_t* in) {
    return static_cast<uint16_t>(in[0]) | (static_cast<uint16_t>(in[1]) << 8);
}
inline uint32_t readU32Le(const uint8_t* in) {
    return static_cast<uint32_t>(in[0]) | (static_cast<uint32_t>(in[1]) << 8) |
           (static_cast<uint32_t>(in[2]) << 16) | (static_cast<uint32_t>(in[3]) << 24);
}

enum class RfMessageType : uint8_t {
    PING = 0x01, PONG = 0x02, SET_PUMP = 0x03, COMMAND_ACK = 0x04,
    TELEMETRY = 0x05, HEARTBEAT = 0x06, FAULT_REPORT = 0x07
};

constexpr uint8_t RF_GATEWAY_NODE_ID = 0;
constexpr uint8_t RF_MIN_NODE_ID = 1;
// Protocol address space supports up to 12 nodes for future expansion.
// PRODUCTION ACCEPTANCE SCOPE (Sprint 1.5 / Baseline 2026-08-22):
// Only Node IDs 1..4 (4 MEGA8 autonomous nodes) are production-accepted.
// Node IDs 5..12 are reserved/backlog — must NOT be accepted in production
// codepaths until an explicit Sprint gate approval is recorded.
constexpr uint8_t RF_MAX_NODE_ID = 12;            // Protocol capacity (backlog)
constexpr uint8_t RF_PRODUCTION_MAX_NODE_ID = 4;  // Production acceptance scope
constexpr uint8_t RF_MAX_PROTOCOL_NODE_ID = RF_MAX_NODE_ID;
constexpr size_t RF_MAX_RX_BUFFER_SIZE = 256;
constexpr uint32_t RF_INTER_BYTE_TIMEOUT_MS = 50;
constexpr uint32_t RF_HEARTBEAT_INTERVAL_MS = 5000;
constexpr uint32_t RF_STALE_THRESHOLD_MS = 15000;
constexpr uint16_t RF_SEQUENCE_WRAP_WINDOW = 32767;

enum class AckOutcome : uint8_t {
    SUCCESS = 0x00, REJECTED_INVALID_LEASE = 0x01, REJECTED_AUTH_FAIL = 0x02,
    FAULT_LOCKOUT = 0x03, REJECTED_UNKNOWN_NODE = 0x04
};

#pragma pack(push, 1)
struct RfHeader {
    uint8_t sof[2] = {RF_SOF_BYTE_1, RF_SOF_BYTE_2};
    uint8_t version = RF_PROTOCOL_VERSION;
    uint8_t message_type = 0;
    uint8_t target_node_id = 0;
    uint8_t source_node_id = 0;
    uint32_t boot_session_id = 0;
    uint16_t sequence = 0;
    uint32_t command_id = 0;
    uint8_t payload_len = 0;
};

struct RfFrameMetadata {
    uint8_t source_node_id = 0;
    uint8_t target_node_id = 0;
    uint32_t boot_session_id = 0;
    uint16_t sequence = 0;
    uint32_t command_id = 0;

    RfFrameMetadata() = default;
    RfFrameMetadata(uint8_t source, uint8_t target, uint32_t session, uint16_t seq, uint32_t command)
        : source_node_id(source), target_node_id(target), boot_session_id(session), sequence(seq), command_id(command) {}
};

struct SetPumpPayload { uint8_t desired_state; uint32_t run_lease_ms; uint32_t max_on_duration_ms; };
struct CommandAckPayload { uint16_t ack_sequence; uint8_t ack_outcome; uint8_t reported_pump_state; uint8_t driver_feedback; uint8_t reserved[3]; };
struct TelemetryPayload { uint8_t reported_pump_state; uint8_t driver_feedback; uint16_t flow_lpm_x100; uint32_t delivered_volume_ml; uint32_t pulse_count; uint8_t fault_flags; uint32_t last_command_id; };
struct PingPayload { uint32_t ping_timestamp_ms; };
struct PongPayload { uint32_t echo_timestamp_ms; };
struct HeartbeatPayload { uint32_t uptime_s; int8_t rssi_dbm; uint8_t battery_percent; };
struct FaultReportPayload { uint8_t fault_code; uint32_t timestamp_ms; uint8_t reserved; uint32_t command_id; };
#pragma pack(pop)

/** Pure C++ RF framing boundary shared by gateway and node firmware. */
class RfFrameCodec {
public:
    static uint16_t calculateCrc16(const uint8_t* data, size_t len);
    // Production address validation. ID 0 is the gateway, not an actuator node.
    static bool isValidProductionRemoteNodeId(uint8_t node_id);
    static bool isValidAddress(uint8_t node_id);
    // Kept as a compatibility alias; production callers must use the explicit APIs.
    static bool isValidNodeId(uint8_t node_id) { return isValidAddress(node_id); }
    static bool isValidMessageType(RfMessageType type);
    static uint16_t calculateSequenceDistance(uint16_t new_seq, uint16_t last_seq);
    static bool isSequenceAdvanceValid(uint16_t new_seq, uint16_t last_seq);
    static bool encodeHeader(const RfHeader& header, uint8_t* out_wire, size_t out_len);
    static bool decodeHeader(const uint8_t* wire, size_t wire_len, RfHeader& out_header);
    static bool encodePayload(RfMessageType type, const void* payload, size_t payload_len,
                              uint8_t* out_wire, uint8_t& out_wire_len);
    static bool decodePayload(RfMessageType type, const uint8_t* wire, size_t wire_len,
                              void* out_payload, size_t out_payload_len);
    static size_t payloadSize(RfMessageType type);
    static size_t nativePayloadSize(RfMessageType type);
    static size_t encodeFrame(const RfFrameMetadata& metadata, RfMessageType type,
                              const void* payload, size_t payload_len,
                              const uint8_t* psk, size_t psk_len,
                              uint8_t* out_frame, size_t out_size);
    static bool decodeFrame(const uint8_t* frame, size_t frame_len,
                            const uint8_t* psk, size_t psk_len,
                            RfHeader& out_header, void* out_payload,
                            size_t out_payload_size);
};
