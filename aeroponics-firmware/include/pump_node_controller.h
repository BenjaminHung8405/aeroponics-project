#pragma once

#include <cstdint>
#include <cstddef>
#include "config.h"
#include "core/IRfTransport.h"
#include "rf_frame_codec.h"
#include "node_registry.h"
#include "nvs_storage.h"
#include "flow_fault_evaluator.h"

constexpr uint8_t DEFAULT_MAX_RF_RETRIES = 3;
constexpr uint32_t DEFAULT_RF_RETRY_INTERVAL_MS = 1000;
constexpr uint8_t MAX_RF_RETRIES = DEFAULT_MAX_RF_RETRIES;
constexpr uint32_t RF_RETRY_INTERVAL_MS = DEFAULT_RF_RETRY_INTERVAL_MS;

/** MQTT topic-segment safe command correlation identifier. */
bool isValidMqttCommandId(const char* command_id);

constexpr size_t RF_HEADER_PAYLOAD_LENGTH_OFFSET = 16;

struct NodeLeasePolicy {
    uint32_t run_lease_ms = 0;
    uint32_t max_on_duration_ms = 0;
    bool provisioned = false;

    NodeLeasePolicy() = default;
    NodeLeasePolicy(uint32_t lease, uint32_t max_on)
        : run_lease_ms(lease), max_on_duration_ms(max_on), provisioned(true) {}
};

/** Immutable identifiers binding a flow policy to its approved control-plane source. */
struct FlowPolicyProvenance {
    uint32_t policy_version = 0;
    uint32_t treatment_version_id = 0;
    uint32_t calibration_id = 0;

    constexpr FlowPolicyProvenance() = default;
    constexpr FlowPolicyProvenance(uint32_t policy, uint32_t treatment, uint32_t calibration)
        : policy_version(policy), treatment_version_id(treatment), calibration_id(calibration) {}
};

/** Approved flow thresholds for one commissioned node/treatment/calibration. */
struct NodeFlowPolicy {
    uint16_t min_flow_lpm_x100 = 0;
    uint16_t max_off_flow_lpm_x100 = 0;
    uint16_t max_flow_lpm_x100 = 0;
    uint32_t flow_start_timeout_ms = 0;
    FlowPolicyProvenance provenance{};
    bool flow_policy_provisioned = false;

    NodeFlowPolicy() = default;
    NodeFlowPolicy(uint16_t min_flow, uint16_t max_off_flow, uint16_t max_flow, uint32_t start_timeout,
                   const FlowPolicyProvenance& source)
        : min_flow_lpm_x100(min_flow), max_off_flow_lpm_x100(max_off_flow),
          max_flow_lpm_x100(max_flow), flow_start_timeout_ms(start_timeout), provenance(source),
          flow_policy_provisioned(true) {}
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
    char override_source[16] = {};
    uint32_t override_run_lease_ms = 0;
    uint32_t override_duration_ms = 0;
    bool reassignment_pending = false;
    uint8_t reassignment_group_id = UNASSIGNED_GROUP_ID;
};

/** Bounded provenance and lifetime metadata for an external temporary override. */
struct ExternalOverridePolicy {
    const char* source = nullptr;
    uint32_t run_lease_ms = 0;
    uint32_t override_duration_ms = 0;

    ExternalOverridePolicy() = default;
    ExternalOverridePolicy(const char* source_value, uint32_t lease, uint32_t duration)
        : source(source_value), run_lease_ms(lease), override_duration_ms(duration) {}
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
 * @brief Production Pump Node Controller managing command dispatch, retry engine,
 * ACK correlation, duplicate response caching, and fail-safe lease deadman enforcement.
 */
class PumpNodeController {
public:
    PumpNodeController();
    virtual ~PumpNodeController();

    bool begin(NodeRegistry* registry, IRfTransport* transport);
    void setOutcomeSink(ICommandOutcomeSink* sink) { outcome_sink_ = sink; }

    /** Load the 16-byte PSK and rotate the local boot session through NVS. */
    bool provisionFromNvs(NvsStorage& storage);
    bool isProvisioned() const { return psk_provisioned_ && boot_session_provisioned_; }

    /** Provision Pre-Shared Key (PSK) for HMAC authentication. */
    bool setPskKey(const uint8_t* psk, size_t len);

    /** Configure retry limits and intervals (non-blocking). */
    void setMaxRetries(uint8_t retries) { max_retries_ = retries; }
    uint8_t getMaxRetries() const { return max_retries_; }
    void setRetryIntervalMs(uint32_t interval_ms) { retry_interval_ms_ = interval_ms; }
    uint32_t getRetryIntervalMs() const { return retry_interval_ms_; }

    /** Configure node safety/lease policy parameters per node. */
    bool provisionNodeLeasePolicy(uint8_t node_id, uint32_t run_lease_ms, uint32_t max_on_duration_ms);

    /** Get node safety/lease policy parameters. */
    bool getNodeLeasePolicy(uint8_t node_id, uint32_t &out_run_lease_ms, uint32_t &out_max_on_duration_ms) const;

    /** Validate and atomically commit the complete node control policy. */
    bool provisionNodeControlPolicy(uint8_t node_id, uint32_t run_lease_ms, uint32_t max_on_duration_ms,
                                    uint16_t min_flow_lpm_x100, uint16_t max_off_flow_lpm_x100,
                                    uint16_t max_flow_lpm_x100, uint32_t flow_start_timeout_ms,
                                    const FlowPolicyProvenance& provenance);
    bool getNodeControlPolicy(uint8_t node_id, NodeLeasePolicy& out_lease,
                              NodeFlowPolicy& out_flow) const;

    /** Apply an authenticated control-plane flow policy with calibration provenance. */
    bool provisionNodeFlowPolicy(uint8_t node_id, uint16_t min_flow_lpm_x100,
                                 uint16_t max_off_flow_lpm_x100, uint16_t max_flow_lpm_x100,
                                 uint32_t flow_start_timeout_ms,
                                 const FlowPolicyProvenance& provenance);
    bool hasProvisionedNodeFlowPolicy(uint8_t node_id) const;

    /** CRC-16 calculation delegate. */
    static uint16_t calculateCrc16(const uint8_t* data, size_t len) {
        return RfFrameCodec::calculateCrc16(data, len);
    }

    /** Header encoding/decoding delegates. */
    static bool decodeHeader(const uint8_t* wire, size_t wire_len, RfHeader& out_header) {
        return RfFrameCodec::decodeHeader(wire, wire_len, out_header);
    }
    static void encodeHeader(const RfHeader& header, uint8_t* out_wire) {
        (void) RfFrameCodec::encodeHeader(header, out_wire, RF_HEADER_SIZE);
    }

    /** Typed payload encoding/decoding delegates. */
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

    /** Build a complete RF frame including header, payload, MAC tag, and trailing CRC-16. */
    size_t buildFrame(RfMessageType msg_type, uint8_t target_node_id, uint32_t command_id,
                      const uint8_t* payload, uint8_t payload_len, uint8_t* out_buffer, size_t buffer_size);

    /** Parse raw byte buffer verifying SOF, version, payload length, HMAC-SHA256, anti-replay, and CRC-16. */
    bool parseFrame(const uint8_t* frame_data, size_t frame_len, RfHeader &out_header,
                    uint8_t* out_payload, uint8_t &out_payload_len,
                    bool* out_new_session = nullptr);

    /** Process pending commands and retry fan-out with non-blocking backoff. */
    bool serviceCommandFanout(uint32_t current_time_ms);

    /** Process incoming frame received via RF transport. */
    bool handleIncomingFrame(const uint8_t* frame, size_t len, uint32_t current_time_ms);

    /** Check if a node has an active pending command. */
    bool isPending(uint8_t node_id) const;

    /** Queue an authenticated external command with idempotency verification. */
    bool queueExternalNodeCommand(uint8_t node_id, NodePumpState desired, const char* command_id,
                                  const ExternalOverridePolicy* policy = nullptr);

    /** Prepare every group member, then atomically commit all desired states and pending slots. */
    bool queueExternalGroupCommand(uint8_t group_id, NodePumpState desired, const char* command_id,
                                   const ExternalOverridePolicy* policy = nullptr);

    /** Read the immutable metadata retained for a pending external override. */
    bool getPendingOverridePolicy(uint8_t node_id, char* source, size_t source_size,
                                  uint32_t& run_lease_ms, uint32_t& override_duration_ms) const;

    /** Safe reassignment workflow. */
    bool requestNodeReassignment(uint8_t node_id, uint8_t group_id, const char* command_id);

    /** Cancel any pending command for a specific node (e.g. when stale, fault, or user abort). */
    void cancelNodeCommands(uint8_t node_id);
    void cancelCommand(uint8_t node_id, const char* reason = "MANUAL_CANCEL");

    /** FlowEvaluator safety FSM accessors and fault recovery */
    FlowFaultEvaluatorRegistry& getFlowEvaluatorRegistry() { return flow_evaluators_; }
    const FlowFaultEvaluatorRegistry& getFlowEvaluatorRegistry() const { return flow_evaluators_; }
    FlowFaultEvaluator* getFlowEvaluator(uint8_t node_id) { return flow_evaluators_.getEvaluator(node_id); }
    const FlowFaultEvaluator* getFlowEvaluator(uint8_t node_id) const { return flow_evaluators_.getEvaluator(node_id); }
    bool resetNodeFault(uint8_t node_id, uint32_t now_ms = 0);

#if !defined(ATMEGA8_NODE_BUILD)
    DuplicateResponseCache& getDuplicateCache() { return duplicate_cache_; }
    const DuplicateResponseCache& getDuplicateCache() const { return duplicate_cache_; }
#endif

private:
    NodeRegistry* registry_;
    IRfTransport* transport_;
    FlowFaultEvaluatorRegistry flow_evaluators_;
    uint32_t boot_session_id_;
    uint16_t sequence_num_;
    uint32_t next_command_id_;
    bool initialized_;
    uint8_t psk_key_[16] = {};
    bool psk_provisioned_ = false;
    bool boot_session_provisioned_ = false;
    uint8_t max_retries_ = DEFAULT_MAX_RF_RETRIES;
    uint32_t retry_interval_ms_ = DEFAULT_RF_RETRY_INTERVAL_MS;

    NodeLeasePolicy node_policies_[MAX_NODES + 1];
    NodeFlowPolicy node_flow_policies_[MAX_NODES + 1];
    PendingCommand pending_commands_[MAX_NODES + 1];
    NodeCommandCorrelation command_correlations_[MAX_NODES + 1];
    NodeSessionTracker session_trackers_[MAX_NODES + 1];
    ICommandOutcomeSink* outcome_sink_ = nullptr;

#if !defined(ATMEGA8_NODE_BUILD)
    DuplicateResponseCache duplicate_cache_;
#endif

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
    bool hasProvisionedNodeLeasePolicy(uint8_t node_id) const;
    bool canDispatchPumpOn(uint8_t node_id, const NodeState& state) const;
    void initializeExternalPending(uint8_t node_id, NodePumpState desired, const char* command_id,
                                   const ExternalOverridePolicy* policy);
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
    bool servicePendingCommand(uint8_t node_id, uint32_t current_time_ms);
    bool serviceDesiredStateDivergence(uint8_t node_id, uint32_t current_time_ms);
    void latchFlowFaultAndQueueSafeOff(uint8_t node_id, const char* outcome, const char* reason);
    void completePendingCommand(uint8_t node_id, const char* outcome, const char* reason);
    void latchFault(uint8_t node_id, const char* outcome, const char* reason);
    void publishOutcome(const PendingCommand& pending, const char* outcome, const char* reason);
};
