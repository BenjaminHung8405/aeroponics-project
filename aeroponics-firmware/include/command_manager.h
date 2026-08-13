#pragma once

#include <cstdint>
#include <cstddef>
#include "config.h"
#include "core/IRfTransport.h"
#include "rf_frame_codec.h"
#include "node_registry.h"
#include "nvs_storage.h"

constexpr uint8_t MAX_RF_RETRIES = 3;
constexpr uint32_t RF_RETRY_INTERVAL_MS = 1000;
constexpr uint32_t RF_INTER_BYTE_TIMEOUT_MS = 50;

/** MQTT topic-segment safe command correlation identifier. */
bool isValidMqttCommandId(const char* command_id);

constexpr size_t RF_HEADER_PAYLOAD_LENGTH_OFFSET = 16;

struct NodeLeasePolicy {
    uint32_t run_lease_ms = DEFAULT_RUN_LEASE_MS;
    uint32_t max_on_duration_ms = DEFAULT_MAX_ON_DURATION_MS;

    NodeLeasePolicy() = default;
    NodeLeasePolicy(uint32_t lease, uint32_t max_on)
        : run_lease_ms(lease), max_on_duration_ms(max_on) {}
};

/** Approved flow thresholds for one commissioned node/treatment/calibration. */
struct NodeFlowPolicy {
    uint16_t min_flow_lpm_x100 = DEFAULT_MIN_FLOW_LPM_X100;
    uint16_t max_off_flow_lpm_x100 = DEFAULT_MAX_OFF_FLOW_LPM_X100;
    uint16_t max_flow_lpm_x100 = DEFAULT_MAX_FLOW_LPM_X100;
    uint32_t flow_start_timeout_ms = DEFAULT_FLOW_START_TIMEOUT_MS;

    NodeFlowPolicy() = default;
    NodeFlowPolicy(uint16_t min_flow, uint16_t max_off_flow, uint16_t max_flow, uint32_t start_timeout)
        : min_flow_lpm_x100(min_flow), max_off_flow_lpm_x100(max_off_flow),
          max_flow_lpm_x100(max_flow), flow_start_timeout_ms(start_timeout) {}
};

/** Explicit command lifecycle; ACK receipt is never pump-state evidence. */
enum class PendingCommandPhase : uint8_t {
    AWAITING_ACK = 0,
    AWAITING_PUMP_FEEDBACK,
    AWAITING_FLOW_CONFIRMATION
};

struct PendingCommand {
    bool active = false;
    bool dispatched = false;
    PendingCommandPhase phase = PendingCommandPhase::AWAITING_ACK;
    uint32_t command_id = 0;
    uint8_t target_node_id = 0;
    NodePumpState desired_state = NodePumpState::OFF;
    uint16_t sequence = 0;
    uint8_t retries = 0;
    uint32_t last_sent_ms = 0;
    uint32_t feedback_wait_started_ms = 0;
    uint32_t flow_wait_started_ms = 0;
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

    /** Configure independently approved flow thresholds for one node. */
    bool setNodeFlowPolicy(uint8_t node_id, uint16_t min_flow_lpm_x100,
                           uint16_t max_off_flow_lpm_x100, uint16_t max_flow_lpm_x100,
                           uint32_t flow_start_timeout_ms);

    /**
     * @brief Calculate CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF).
     */
    static uint16_t calculateCrc16(const uint8_t* data, size_t len) {
        return RfFrameCodec::calculateCrc16(data, len);
    }

    /** Decode a canonical 17-byte wire header into a host DTO. */
    static bool decodeHeader(const uint8_t* wire, size_t wire_len, RfHeader& out_header) {
        return RfFrameCodec::decodeHeader(wire, wire_len, out_header);
    }

    /** Encode a host DTO as the canonical 17-byte little-endian wire header. */
    static void encodeHeader(const RfHeader& header, uint8_t* out_wire) {
        (void) RfFrameCodec::encodeHeader(header, out_wire, RF_HEADER_SIZE);
    }

    /** Encode/decode a typed payload without exposing C++ layout on the wire. */
    static bool encodePayload(RfMessageType msg_type, const uint8_t* native_payload,
                              uint8_t native_payload_len, uint8_t* out_wire,
                              uint8_t& out_wire_len) {
        return RfFrameCodec::encodePayload(msg_type, native_payload, native_payload_len,
                                           out_wire, out_wire_len);
    }
    static bool decodePayload(RfMessageType msg_type, const uint8_t* wire_payload,
                              uint8_t wire_payload_len, uint8_t* out_native,
                              uint8_t& out_native_len) {
        if (!RfFrameCodec::decodePayload(msg_type, wire_payload, wire_payload_len,
                                         out_native, RF_MAX_PAYLOAD_SIZE)) return false;
        out_native_len = wire_payload_len;
        return true;
    }

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
    NodeFlowPolicy node_flow_policies_[MAX_NODES + 1];
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
    bool isRetryDue(uint8_t node_id, uint32_t current_time_ms) const;
    bool isPendingDeadlineExpired(uint8_t node_id, uint32_t current_time_ms) const;
    bool buildPendingFrame(uint8_t node_id);
    bool dispatchPendingFrame(uint8_t node_id, uint32_t current_time_ms, bool is_retry);
    bool sendPendingCommand(uint8_t node_id, uint32_t current_time_ms, bool is_retry);
    bool queueInternalSafeOff(uint8_t node_id);
    bool telemetryConfirmsPumpFeedback(uint8_t node_id, uint32_t command_id,
                                       NodePumpState reported, uint8_t driver_feedback) const;
    bool isFlowWithinRange(uint8_t node_id, uint16_t flow_lpm_x100) const;
    bool validateTelemetrySafety(uint8_t node_id, const TelemetryPayload& telemetry) const;
    bool handlePendingTelemetry(uint8_t node_id, const TelemetryPayload& telemetry,
                                NodePumpState reported, uint32_t current_time_ms);
    void handlePendingDeadline(uint8_t node_id);
    void latchFlowFaultAndQueueSafeOff(uint8_t node_id, const char* outcome, const char* reason);
    void completePendingCommand(uint8_t node_id, const char* outcome, const char* reason);
    void latchFault(uint8_t node_id, const char* outcome, const char* reason);
    void publishOutcome(const PendingCommand& pending, const char* outcome, const char* reason);
};
