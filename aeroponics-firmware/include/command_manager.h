#pragma once

#include <cstdint>
#include <cstddef>
#include "config.h"
#include "core/IRfTransport.h"
#include "core/hmac_sha256.h"
#include "node_registry.h"
#include "nvs_storage.h"

constexpr uint8_t RF_SOF_BYTE_1 = 0xAA;
constexpr uint8_t RF_SOF_BYTE_2 = 0x55;
constexpr uint8_t RF_PROTOCOL_VERSION = 0x01;
constexpr uint8_t MAX_RF_RETRIES = 3;
constexpr uint32_t RF_RETRY_INTERVAL_MS = 1000;
constexpr uint32_t RF_INTER_BYTE_TIMEOUT_MS = 50;

enum class RfMessageType : uint8_t {
    PING         = 0x01,
    PONG         = 0x02,
    SET_PUMP     = 0x03,
    COMMAND_ACK  = 0x04,
    TELEMETRY    = 0x05,
    HEARTBEAT    = 0x06,
    FAULT_REPORT = 0x07
};

enum class AckOutcome : uint8_t {
    SUCCESS = 0x00,
    REJECTED_INVALID_LEASE = 0x01,
    REJECTED_AUTH_FAIL = 0x02,
    FAULT_LOCKOUT = 0x03,
    REJECTED_UNKNOWN_NODE = 0x04
};

#pragma pack(push, 1)
struct RfHeader {
    uint8_t sof[2];           // 0xAA 0x55
    uint8_t version;          // 0x01
    uint8_t message_type;     // RfMessageType
    uint8_t target_node_id;   // 0 (Gateway) or 1..12
    uint8_t source_node_id;   // 0 (Gateway) or 1..12
    uint16_t boot_session_id; // Session counter
    uint16_t sequence;        // Sequence number
    uint32_t command_id;      // Command correlation ID
    uint8_t payload_len;      // Payload length (0..64)
};
constexpr size_t RF_HEADER_PAYLOAD_LENGTH_OFFSET = offsetof(RfHeader, payload_len);
static_assert(sizeof(RfHeader) == 15, "RF wire header must remain 15 bytes");

struct SetPumpPayload {
    uint8_t desired_state;     // 0 = OFF, 1 = ON
    uint32_t run_lease_ms;     // Lease duration ms
    uint32_t max_on_duration_ms;
};

struct CommandAckPayload {
    uint16_t ack_sequence;
    uint8_t ack_outcome;
    uint8_t reported_pump_state;
    uint8_t driver_feedback;
    uint8_t reserved[3];
};

struct TelemetryPayload {
    uint8_t reported_pump_state;
    uint8_t driver_feedback;
    uint16_t flow_lpm_x100;
    uint32_t delivered_volume_ml;
    uint32_t pulse_count;
    uint8_t fault_flags;
};

struct PingPayload {
    uint32_t ping_timestamp_ms;
};

struct PongPayload {
    uint32_t echo_timestamp_ms;
};

struct HeartbeatPayload {
    uint32_t uptime_s;
    int8_t rssi_dbm;
    uint8_t battery_percent;
};

struct FaultReportPayload {
    uint8_t fault_code;
    uint32_t timestamp_ms;
    uint8_t reserved;
};
#pragma pack(pop)

struct NodeLeasePolicy {
    uint32_t run_lease_ms = DEFAULT_RUN_LEASE_MS;
    uint32_t max_on_duration_ms = DEFAULT_MAX_ON_DURATION_MS;

    NodeLeasePolicy() = default;
    NodeLeasePolicy(uint32_t lease, uint32_t max_on)
        : run_lease_ms(lease), max_on_duration_ms(max_on) {}
};

struct PendingCommand {
    bool active = false;
    uint32_t command_id = 0;
    uint8_t target_node_id = 0;
    NodePumpState desired_state = NodePumpState::OFF;
    uint16_t sequence = 0;
    uint8_t retries = 0;
    uint32_t last_sent_ms = 0;
    char mqtt_command_id[65] = {};
};

struct NodeSessionTracker {
    uint16_t last_boot_session_id = 0;
    uint16_t last_sequence_num = 0;
    bool initialized = false;
};

/**
 * @brief Command Manager responsible for RF frame encoding/decoding, HMAC-SHA256 authentication,
 * CRC-16 check, anti-replay verification, pending command orchestration and retry policy.
 */
class CommandManager {
public:
    CommandManager();
    ~CommandManager();

    bool begin(NodeRegistry* registry, IRfTransport* transport);
    /** Load the 16-byte PSK and rotate the local boot session through NVS. */
    bool provisionFromNvs(NvsStorage& storage);
    bool isProvisioned() const { return psk_provisioned_ && boot_session_provisioned_; }

    /**
     * @brief Provision Pre-Shared Key (PSK) for HMAC authentication.
     */
    bool setPskKey(const uint8_t* psk, size_t len);

    /**
     * @brief Configure node safety/lease policy parameters per node.
     */
    bool setNodeLeasePolicy(uint8_t node_id, uint32_t run_lease_ms, uint32_t max_on_duration_ms);

    /**
     * @brief Get node safety/lease policy parameters.
     */
    bool getNodeLeasePolicy(uint8_t node_id, uint32_t &out_run_lease_ms, uint32_t &out_max_on_duration_ms) const;

    /**
     * @brief Calculate CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF).
     */
    static uint16_t calculateCrc16(const uint8_t* data, size_t len);

    /**
     * @brief Build a complete RF frame including header, payload, MAC tag, and trailing CRC-16.
     * @return Total frame length in bytes, or 0 on error.
     */
    size_t buildFrame(RfMessageType msg_type, uint8_t target_node_id, uint32_t command_id,
                      const uint8_t* payload, uint8_t payload_len, uint8_t* out_buffer, size_t buffer_size);

    /**
     * @brief Parse raw byte buffer after verifying SOF, version, payload length, HMAC-SHA256, anti-replay, and CRC-16.
     */
    bool parseFrame(const uint8_t* frame_data, size_t frame_len, RfHeader &out_header,
                    uint8_t* out_payload, uint8_t &out_payload_len);

    /**
     * @brief Process pending commands and retry fan-out with backoff. Marks node FAULT on terminal timeout.
     */
    bool serviceCommandFanout(uint32_t current_time_ms);

    /**
     * @brief Process incoming frame received via RF transport.
     */
    bool handleIncomingFrame(const uint8_t* frame, size_t len, uint32_t current_time_ms);

    /**
     * @brief Check if a node has an active pending command.
     */
    bool isPending(uint8_t node_id) const;

private:
    NodeRegistry* registry_;
    IRfTransport* transport_;
    uint16_t boot_session_id_;
    uint16_t sequence_num_;
    uint32_t next_command_id_;
    bool initialized_;
    uint8_t psk_key_[16] = {};
    bool psk_provisioned_ = false;
    bool boot_session_provisioned_ = false;

    NodeLeasePolicy node_policies_[MAX_NODES + 1];
    PendingCommand pending_commands_[MAX_NODES + 1];
    NodeSessionTracker session_trackers_[MAX_NODES + 1];

    bool validateAntiReplay(uint8_t src_node, uint16_t session_id, uint16_t sequence);
    bool validateAck(const RfHeader& header, const CommandAckPayload& ack) const;
    bool sendPendingCommand(uint8_t node_id, uint32_t current_time_ms, bool is_retry);
};
