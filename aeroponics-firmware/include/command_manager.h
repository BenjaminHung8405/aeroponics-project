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
constexpr size_t RF_MAX_FRAME_SIZE = 17 + 64 + HMAC_TAG_SIZE + 2;

/** MQTT topic-segment safe command correlation identifier. */
bool isValidMqttCommandId(const char* command_id);

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
    uint32_t boot_session_id; // Gateway/node boot session counter
    uint16_t sequence;        // Sequence number
    uint32_t command_id;      // Command correlation ID
    uint8_t payload_len;      // Payload length (0..64)
};
constexpr size_t RF_HEADER_PAYLOAD_LENGTH_OFFSET = offsetof(RfHeader, payload_len);
static_assert(sizeof(RfHeader) == 17, "RF wire header must remain 17 bytes");

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
    uint32_t last_command_id;
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
    uint32_t command_id;
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
    bool dispatched = false;
    uint32_t command_id = 0;
    uint8_t target_node_id = 0;
    NodePumpState desired_state = NodePumpState::OFF;
    uint16_t sequence = 0;
    uint8_t retries = 0;
    uint32_t last_sent_ms = 0;
    uint32_t node_boot_session_id = 0;
    uint8_t frame[RF_MAX_FRAME_SIZE] = {};
    uint8_t frame_len = 0;
    char mqtt_command_id[65] = {};
    bool reassignment_pending = false;
    uint8_t reassignment_group_id = UNASSIGNED_GROUP_ID;
};

/** Last command whose telemetry/fault feedback is admissible for a node session. */
struct NodeCommandCorrelation {
    uint32_t command_id = 0;
    uint32_t boot_session_id = 0;
    bool active = false;
};

struct NodeSessionTracker {
    uint32_t last_boot_session_id = 0;
    uint16_t last_sequence_num = 0;
    bool initialized = false;
};

enum class AntiReplayResult : uint8_t {
    REJECTED = 0,
    ACCEPTED,
    NEW_SESSION
};

/** Outbound audit port; MQTT is one adapter, not a dependency of RF control. */
class ICommandOutcomeSink {
public:
    virtual ~ICommandOutcomeSink() = default;
    virtual void publishCommandOutcome(const char* command_id, const char* status,
                                       uint8_t node_id, const char* reason) = 0;
    virtual void publishSafetyAudit(const char* event, const char* reason) = 0;
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
    void setOutcomeSink(ICommandOutcomeSink* sink) { outcome_sink_ = sink; }
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
                    uint8_t* out_payload, uint8_t &out_payload_len,
                    bool* out_new_session = nullptr);

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

    /** Queue an authenticated external command while preserving its immutable ID. */
    bool queueExternalNodeCommand(uint8_t node_id, NodePumpState desired, const char* command_id);

    /**
     * Safe reassignment workflow: cancel the old operation, RF-ACK an OFF command,
     * then atomically commit the new cache mapping. Durable assignment history remains
     * owned by the backend before this gateway command is issued.
     */
    bool requestNodeReassignment(uint8_t node_id, uint8_t group_id, const char* command_id);

    /** Cancel any pending command for a specific node (e.g. when stale or fault latched). */
    void cancelNodeCommands(uint8_t node_id);

private:
    NodeRegistry* registry_;
    IRfTransport* transport_;
    uint32_t boot_session_id_;
    uint16_t sequence_num_;
    uint32_t next_command_id_;
    bool initialized_;
    uint8_t psk_key_[16] = {};
    bool psk_provisioned_ = false;
    bool boot_session_provisioned_ = false;

    NodeLeasePolicy node_policies_[MAX_NODES + 1];
    PendingCommand pending_commands_[MAX_NODES + 1];
    NodeCommandCorrelation command_correlations_[MAX_NODES + 1];
    NodeSessionTracker session_trackers_[MAX_NODES + 1];
    ICommandOutcomeSink* outcome_sink_ = nullptr;

    AntiReplayResult validateAntiReplay(uint8_t src_node, uint32_t session_id, uint16_t sequence);
    bool validateFrameEnvelope(const uint8_t* frame_data, size_t frame_len, RfHeader& out_header) const;
    bool verifyCrcAndMac(const uint8_t* frame_data, const RfHeader& header) const;
    bool validateAddressing(const RfHeader& header) const;
    bool validateAck(const RfHeader& header, const CommandAckPayload& ack) const;
    bool handleAckFrame(uint8_t src_node, const RfHeader& header, const uint8_t* payload,
                        uint8_t payload_len, uint32_t current_time_ms);
    bool handleTelemetryFrame(uint8_t src_node, const RfHeader& header, const uint8_t* payload, uint8_t payload_len,
                              uint32_t current_time_ms);
    bool handleHeartbeatFrame(uint8_t src_node, const uint8_t* payload, uint8_t payload_len,
                              uint32_t current_time_ms);
    bool handlePongFrame(uint8_t src_node, const uint8_t* payload, uint8_t payload_len,
                         uint32_t current_time_ms);
    bool handleFaultFrame(uint8_t src_node, const RfHeader& header, const uint8_t* payload, uint8_t payload_len);
    void handleNodeSessionChange(uint8_t node_id);
    uint32_t currentNodeBootSession(uint8_t node_id) const;
    void activatePendingCorrelation(uint8_t node_id);
    bool hasCurrentCorrelation(uint8_t node_id, uint32_t command_id, uint32_t boot_session_id) const;
    bool sendPendingCommand(uint8_t node_id, uint32_t current_time_ms, bool is_retry);
    void completePendingCommand(uint8_t node_id, const char* outcome, const char* reason);
    void latchFault(uint8_t node_id, const char* outcome, const char* reason);
    void publishOutcome(const PendingCommand& pending, const char* outcome, const char* reason);
};
