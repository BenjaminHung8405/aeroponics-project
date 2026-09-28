#include "pump_node_controller.h"
#include "rf_provisioning.h"
#include <cstring>
#include <cstdio>

namespace {

bool isBinaryState(uint8_t value) { return value == 0 || value == 1; }
bool isAckOutcome(uint8_t value) { return value <= static_cast<uint8_t>(AckOutcome::REJECTED_UNKNOWN_NODE); }
bool isValidFlowPolicy(uint16_t min_flow_lpm_x100, uint16_t max_off_flow_lpm_x100,
                       uint16_t max_flow_lpm_x100, uint32_t flow_start_timeout_ms,
                       const FlowPolicyProvenance& provenance) {
    return min_flow_lpm_x100 > 0 && min_flow_lpm_x100 <= FLOW_SENSOR_MAX_LPM_X100 &&
           max_off_flow_lpm_x100 <= FLOW_SENSOR_MAX_LPM_X100 &&
           max_flow_lpm_x100 <= FLOW_SENSOR_MAX_LPM_X100 && min_flow_lpm_x100 <= max_flow_lpm_x100 &&
           max_off_flow_lpm_x100 <= max_flow_lpm_x100 && flow_start_timeout_ms > 0 &&
           provenance.policy_version > 0 && provenance.treatment_version_id > 0 &&
           provenance.calibration_id > 0;
}
}

bool isValidMqttCommandId(const char* command_id) {
    if (command_id == nullptr) return false;
    const size_t length = strnlen(command_id, 65);
    if (length == 0 || length > 64) return false;
    for (size_t i = 0; i < length; ++i) {
        const char c = command_id[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    }
    return true;
}

PumpNodeController::PumpNodeController()
    : registry_(nullptr), transport_(nullptr), boot_session_id_(1), sequence_num_(0),
      next_command_id_(1000), initialized_(false), max_retries_(DEFAULT_MAX_RF_RETRIES),
      retry_interval_ms_(DEFAULT_RF_RETRY_INTERVAL_MS) {
    for (size_t i = 0; i <= MAX_NODES; ++i) {
        node_policies_[i] = NodeLeasePolicy{};
        node_flow_policies_[i] = NodeFlowPolicy{};
        pending_commands_[i] = PendingCommand{};
        command_correlations_[i] = NodeCommandCorrelation{};
        session_trackers_[i] = NodeSessionTracker{};
    }
}

PumpNodeController::~PumpNodeController() {}

bool PumpNodeController::begin(NodeRegistry* registry, IRfTransport* transport) {
    if (registry == nullptr || transport == nullptr) {
        return false;
    }
    registry_ = registry;
    transport_ = transport;
    // Provisioning is deliberately a separate secure boundary. Until it succeeds,
    // every RF command and received RF frame is rejected fail-closed.
    initialized_ = true;
    return true;
}

bool PumpNodeController::setPskKey(const uint8_t* psk, size_t len) {
#if defined(UNIT_TEST_HOST)
    if (psk == nullptr || len != sizeof(psk_key_)) return false;
    std::memcpy(psk_key_, psk, sizeof(psk_key_));
    psk_provisioned_ = true;
    boot_session_provisioned_ = true;
    return true;
#else
    (void)psk;
    (void)len;
    return false;
#endif
}

bool PumpNodeController::provisionFromNvs(NvsStorage& storage) {
    uint32_t words[4] = {};
    uint32_t previous_session = 0;
    if (!storage.isInitialized() || !storage.getU32(RF_NVS_BOOT_SESSION_KEY, previous_session)) return false;
    for (size_t i = 0; i < 4; ++i) {
        if (!storage.getU32(RF_NVS_PSK_WORD_KEYS[i], words[i])) return false;
    }

    // Exhaustion requires explicit credential rotation/factory reset; silently
    // wrapping this persisted uint32 session would weaken anti-replay.
    if (previous_session == 0 || previous_session == UINT32_MAX) return false;
    uint8_t candidate_psk[sizeof(psk_key_)] = {};
    std::memcpy(candidate_psk, words, sizeof(candidate_psk));
    const uint32_t next_session = previous_session + 1U;

    // NvsStorage::setU32 commits before returning. Do not mutate live RF state
    // until that durable commit succeeds, so a failed provision remains closed.
    if (!storage.setU32(RF_NVS_BOOT_SESSION_KEY, next_session)) return false;
    std::memcpy(psk_key_, candidate_psk, sizeof(psk_key_));
    boot_session_id_ = next_session;
    sequence_num_ = 0;
    psk_provisioned_ = true;
    boot_session_provisioned_ = true;
    return true;
}

bool PumpNodeController::provisionNodeLeasePolicy(uint8_t node_id, uint32_t run_lease_ms, uint32_t max_on_duration_ms) {
    if (!isProductionNodeId(node_id)) return false;
    if (run_lease_ms == 0 || max_on_duration_ms < run_lease_ms) return false;
    node_policies_[node_id].run_lease_ms = run_lease_ms;
    node_policies_[node_id].max_on_duration_ms = max_on_duration_ms;
    node_policies_[node_id].provisioned = true;
    return true;
}

bool PumpNodeController::getNodeLeasePolicy(uint8_t node_id, uint32_t &out_run_lease_ms, uint32_t &out_max_on_duration_ms) const {
    if (!isProductionNodeId(node_id)) return false;
    out_run_lease_ms = node_policies_[node_id].run_lease_ms;
    out_max_on_duration_ms = node_policies_[node_id].max_on_duration_ms;
    return true;
}

bool PumpNodeController::provisionNodeControlPolicy(uint8_t node_id, uint32_t run_lease_ms,
                                                uint32_t max_on_duration_ms, uint16_t min_flow_lpm_x100,
                                                uint16_t max_off_flow_lpm_x100, uint16_t max_flow_lpm_x100,
                                                uint32_t flow_start_timeout_ms,
                                                const FlowPolicyProvenance& provenance) {
    const bool valid_lease = isProductionNodeId(node_id) && run_lease_ms > 0 &&
                             max_on_duration_ms >= run_lease_ms;
    const bool valid_flow = isValidFlowPolicy(min_flow_lpm_x100, max_off_flow_lpm_x100,
                                              max_flow_lpm_x100, flow_start_timeout_ms, provenance);
    if (!valid_lease || !valid_flow) return false;

    const NodeLeasePolicy lease{run_lease_ms, max_on_duration_ms};
    const NodeFlowPolicy flow{min_flow_lpm_x100, max_off_flow_lpm_x100, max_flow_lpm_x100,
                              flow_start_timeout_ms, provenance};
    node_policies_[node_id] = lease;
    node_flow_policies_[node_id] = flow;
    FlowSafetyProvenance prov{provenance.policy_version, provenance.treatment_version_id, provenance.calibration_id};
    FlowSafetyConfig cfg{min_flow_lpm_x100, max_off_flow_lpm_x100, max_flow_lpm_x100,
                         flow_start_timeout_ms, 200, 3000, prov};
    flow_evaluators_.configureNode(node_id, cfg);
    return true;
}

bool PumpNodeController::getNodeControlPolicy(uint8_t node_id, NodeLeasePolicy& out_lease,
                                          NodeFlowPolicy& out_flow) const {
    if (!isProductionNodeId(node_id)) return false;
    out_lease = node_policies_[node_id];
    out_flow = node_flow_policies_[node_id];
    return true;
}

bool PumpNodeController::provisionNodeFlowPolicy(uint8_t node_id, uint16_t min_flow_lpm_x100,
                                             uint16_t max_off_flow_lpm_x100, uint16_t max_flow_lpm_x100,
                                             uint32_t flow_start_timeout_ms,
                                             const FlowPolicyProvenance& provenance) {
    if (!isProductionNodeId(node_id) ||
        !isValidFlowPolicy(min_flow_lpm_x100, max_off_flow_lpm_x100, max_flow_lpm_x100,
                           flow_start_timeout_ms, provenance)) return false;
    node_flow_policies_[node_id] = NodeFlowPolicy{min_flow_lpm_x100, max_off_flow_lpm_x100,
                                                   max_flow_lpm_x100, flow_start_timeout_ms, provenance};
    FlowSafetyProvenance prov{provenance.policy_version, provenance.treatment_version_id, provenance.calibration_id};
    FlowSafetyConfig cfg{min_flow_lpm_x100, max_off_flow_lpm_x100, max_flow_lpm_x100,
                         flow_start_timeout_ms, 200, 3000, prov};
    flow_evaluators_.configureNode(node_id, cfg);
    return true;
}

bool PumpNodeController::hasProvisionedNodeFlowPolicy(uint8_t node_id) const {
    return isProductionNodeId(node_id) && node_flow_policies_[node_id].flow_policy_provisioned;
}

size_t PumpNodeController::buildFrame(RfMessageType msg_type, uint8_t target_node_id, uint32_t command_id,
                                  const uint8_t* payload, uint8_t payload_len, uint8_t* out_buffer, size_t buffer_size) {
    if (!isProvisioned()) return 0;
    const RfFrameMetadata metadata{0, target_node_id, boot_session_id_, sequence_num_, command_id};
    const size_t length = RfFrameCodec::encodeFrame(metadata, msg_type, payload, payload_len,
                                                     psk_key_, sizeof(psk_key_), out_buffer, buffer_size);
    if (length != 0) ++sequence_num_;
    return length;
}

AntiReplayResult PumpNodeController::validateAntiReplay(uint8_t src_node, uint32_t session_id, uint16_t sequence) {
    if (!isProductionNodeId(src_node)) return AntiReplayResult::REJECTED;
    NodeSessionTracker &tracker = session_trackers_[src_node];

    if (!tracker.initialized) {
        tracker.last_boot_session_id = session_id;
        tracker.last_sequence_num = sequence;
        tracker.initialized = true;
        // First authenticated enrollment is already fail-safe OFF by registry
        // default; only a subsequent boot-session transition is a reboot.
        return AntiReplayResult::ACCEPTED;
    }

    if (session_id > tracker.last_boot_session_id) {
        tracker.last_boot_session_id = session_id;
        tracker.last_sequence_num = sequence;
        return AntiReplayResult::NEW_SESSION;
    } else if (session_id == tracker.last_boot_session_id) {
        // RFC-style serial arithmetic: only the next half of the uint16 space
        // is newer. It admits normal wrap while rejecting duplicates/stale frames.
        const uint16_t distance = static_cast<uint16_t>(sequence - tracker.last_sequence_num);
        if (distance != 0 && distance < 0x8000U) {
            tracker.last_sequence_num = sequence;
            return AntiReplayResult::ACCEPTED;
        }
    }
    return AntiReplayResult::REJECTED; // Replay or stale sequence
}

bool PumpNodeController::validateFrameEnvelope(const uint8_t* frame_data, size_t frame_len,
                                           RfHeader& out_header) const {
    const size_t header_len = RF_HEADER_SIZE;
    if (!isProvisioned() || frame_data == nullptr || frame_len < header_len + HMAC_TAG_SIZE + 2) {
        return false;
    }
    if (!decodeHeader(frame_data, frame_len, out_header)) return false;
    if (out_header.sof[0] != RF_SOF_BYTE_1 || out_header.sof[1] != RF_SOF_BYTE_2 ||
        out_header.version != RF_PROTOCOL_VERSION) {
        return false;
    }

    if (out_header.payload_len > 64) {
        return false;
    }

    if (frame_len != header_len + out_header.payload_len + HMAC_TAG_SIZE + 2) {
        return false;
    }

    return true;
}

bool PumpNodeController::validateAddressing(const RfHeader& header) const {
    if (header.message_type < static_cast<uint8_t>(RfMessageType::PING) ||
        header.message_type > static_cast<uint8_t>(RfMessageType::FAULT_REPORT)) {
        return false;
    }
    return isValidSourceAddress(header.source_node_id) &&
           isValidTargetAddress(header.target_node_id) &&
           header.source_node_id != header.target_node_id &&
           (header.source_node_id == RF_GATEWAY_NODE_ID ||
            header.target_node_id == RF_GATEWAY_NODE_ID);
}

bool PumpNodeController::verifyCrcAndMac(const uint8_t* frame_data, size_t frame_len,
                                         const RfHeader& header) const {
    const size_t header_len = RF_HEADER_SIZE;
    if (frame_data == nullptr || frame_len < 2) return false;
    const size_t crc_check_len = header_len + header.payload_len + HMAC_TAG_SIZE;
    if (crc_check_len + 2 != frame_len) return false;
    const uint16_t expected_crc = calculateCrc16(frame_data, crc_check_len);
    const uint16_t actual_crc = readU16Le(frame_data + crc_check_len);
    if (expected_crc != actual_crc) return false;

    uint8_t expected_mac[HMAC_TAG_SIZE] = {};
    return HmacSha256::calculateTruncated(psk_key_, sizeof(psk_key_), frame_data,
                                          header_len + header.payload_len, expected_mac) &&
           constantTimeCompare(expected_mac, frame_data + header_len + header.payload_len, HMAC_TAG_SIZE);
}

bool PumpNodeController::parseFrame(const uint8_t* frame_data, size_t frame_len, RfHeader &out_header,
                                 uint8_t* out_payload, uint8_t &out_payload_len, bool* out_new_session) {
    if (!validateFrameEnvelope(frame_data, frame_len, out_header) ||
        !validateAddressing(out_header) || !verifyCrcAndMac(frame_data, frame_len, out_header)) {
        return false;
    }

    // Anti-replay Check
    // Source 0 is only accepted by the pure codec for locally generated-frame
    // tests. The incoming-frame dispatcher below rejects it for production RX.
    if (out_new_session != nullptr) *out_new_session = false;
    if (out_header.source_node_id != 0) {
        const AntiReplayResult anti_replay = validateAntiReplay(out_header.source_node_id,
                                                                 out_header.boot_session_id,
                                                                 out_header.sequence);
        if (anti_replay == AntiReplayResult::REJECTED) return false;
        if (out_new_session != nullptr) *out_new_session = anti_replay == AntiReplayResult::NEW_SESSION;
    }

    if (out_header.payload_len == 0) {
        out_payload_len = 0;
        return true;
    }
    if (out_payload == nullptr || !decodePayload(static_cast<RfMessageType>(out_header.message_type),
                                                  frame_data + RF_HEADER_SIZE, out_header.payload_len,
                                                  out_payload, out_payload_len)) return false;

    return true;
}

bool PumpNodeController::isPending(uint8_t node_id) const {
    if (!isProductionNodeId(node_id)) return false;
    return pending_commands_[node_id].active;
}

bool PumpNodeController::queueExternalNodeCommand(uint8_t node_id, NodePumpState desired, const char* command_id,
                                              const ExternalOverridePolicy* policy) {
    if (!initialized_ || registry_ == nullptr || !isValidMqttCommandId(command_id)) {
        return false;
    }
    if (!isProductionNodeId(node_id)) return false;
    if (policy == nullptr) return false;
    const bool valid_source = policy->source != nullptr &&
        (std::strcmp(policy->source, "MANUAL_OVERRIDE") == 0 ||
         std::strcmp(policy->source, "FAIL_SAFE") == 0);
    const bool valid_duration = policy->override_duration_ms <= 86400000U &&
        (desired != NodePumpState::OFF || policy->override_duration_ms > 0);
    // OFF requires an explicit bounded duration; ON requires an explicit
    // positive lease in the external command policy.
    const bool valid_lease = policy->run_lease_ms <= DEFAULT_MAX_ON_DURATION_MS &&
        (desired != NodePumpState::ON || policy->run_lease_ms > 0);
    if (!valid_source || !valid_duration || !valid_lease) return false;

    // Idempotent duplicate check
    if (pending_commands_[node_id].active) {
        if (strncmp(pending_commands_[node_id].mqtt_command_id, command_id, sizeof(PendingCommand::mqtt_command_id)) == 0) {
            return true;
        }
        return false;
    }

    NodeState state;
    if (!registry_->getNodeState(node_id, state) ||
        (desired == NodePumpState::ON && !canDispatchPumpOn(node_id, state))) {
        return false;
    }
    if (!registry_->setDesiredState(node_id, desired)) return false;

    initializeExternalPending(node_id, desired, command_id, policy);
    return true;
}

void PumpNodeController::initializeExternalPending(uint8_t node_id, NodePumpState desired, const char* command_id,
                                               const ExternalOverridePolicy* policy) {
    PendingCommand& pending = pending_commands_[node_id];
    pending = PendingCommand{};
    pending.active = true;
    pending.phase = PendingCommandPhase::AWAITING_ACK;
    pending.command_id = next_command_id_++;
    pending.target_node_id = node_id;
    pending.desired_state = desired;
    pending.node_boot_session_id = currentNodeBootSession(node_id);
    std::strncpy(pending.mqtt_command_id, command_id, sizeof(pending.mqtt_command_id) - 1);
    pending.mqtt_command_id[sizeof(pending.mqtt_command_id) - 1] = '\0';
    if (policy != nullptr) {
        std::strncpy(pending.override_source, policy->source, sizeof(pending.override_source) - 1);
        pending.override_source[sizeof(pending.override_source) - 1] = '\0';
        pending.override_run_lease_ms = policy->run_lease_ms;
        pending.override_duration_ms = policy->override_duration_ms;
    }
}

bool PumpNodeController::queueExternalGroupCommand(uint8_t group_id, NodePumpState desired, const char* command_id,
                                               const ExternalOverridePolicy* policy) {
    if (!initialized_ || registry_ == nullptr || group_id < 1 || group_id > MAX_TIMER_GROUPS ||
        !isValidMqttCommandId(command_id)) return false;
    if (policy == nullptr) return false;
    const bool valid_source = policy->source != nullptr &&
        (std::strcmp(policy->source, "MANUAL_OVERRIDE") == 0 ||
         std::strcmp(policy->source, "FAIL_SAFE") == 0);
    const bool valid_duration = policy->override_duration_ms <= 86400000U &&
        (desired != NodePumpState::OFF || policy->override_duration_ms > 0);
    const bool valid_lease = policy->run_lease_ms <= DEFAULT_MAX_ON_DURATION_MS &&
        (desired != NodePumpState::ON || policy->run_lease_ms > 0);
    if (!valid_source || !valid_duration || !valid_lease) return false;

    uint16_t target_mask = 0;
    for (uint8_t node_id = RF_PRODUCTION_MIN_NODE_ID; node_id <= RF_PRODUCTION_MAX_NODE_ID; ++node_id) {
        if (registry_->getNodeGroup(node_id) != group_id) continue;
        target_mask |= static_cast<uint16_t>(1U) << (node_id - 1U);
        const PendingCommand& pending = pending_commands_[node_id];
        if (pending.active) {
            if (pending.desired_state != desired ||
                std::strncmp(pending.mqtt_command_id, command_id, sizeof(pending.mqtt_command_id)) != 0) return false;
            continue;
        }
        NodeState state{};
        if (!registry_->getNodeState(node_id, state) ||
            (desired == NodePumpState::ON && !canDispatchPumpOn(node_id, state))) return false;
    }
    if (target_mask == 0) return false;

    // Commit only after every node/slot/policy passed prepare. The registry
    // applies this mask under one lock, preventing a partial desired-state fan-out.
    if (!registry_->setDesiredStateForMask(target_mask, desired)) return false;
    for (uint8_t node_id = RF_PRODUCTION_MIN_NODE_ID; node_id <= RF_PRODUCTION_MAX_NODE_ID; ++node_id) {
        if ((target_mask & (static_cast<uint16_t>(1U) << (node_id - 1U))) != 0 &&
            !pending_commands_[node_id].active) {
            initializeExternalPending(node_id, desired, command_id, policy);
        }
    }
    return true;
}

bool PumpNodeController::getPendingOverridePolicy(uint8_t node_id, char* source, size_t source_size,
                                              uint32_t& run_lease_ms, uint32_t& override_duration_ms) const {
    if (!isProductionNodeId(node_id) || source == nullptr || source_size == 0 ||
        !pending_commands_[node_id].active) return false;
    const PendingCommand& pending = pending_commands_[node_id];
    std::strncpy(source, pending.override_source, source_size - 1);
    source[source_size - 1] = '\0';
    run_lease_ms = pending.override_run_lease_ms;
    override_duration_ms = pending.override_duration_ms;
    return true;
}

bool PumpNodeController::requestNodeReassignment(uint8_t node_id, uint8_t group_id, const char* command_id) {
    if (!initialized_ || registry_ == nullptr || !isProductionNodeId(node_id) ||
        group_id > MAX_TIMER_GROUPS || !isValidMqttCommandId(command_id)) return false;
    NodeState state;
    if (!registry_->getNodeState(node_id, state)) return false;
    if (state.group_id == group_id && !pending_commands_[node_id].active) return true;
    if (pending_commands_[node_id].active) cancelNodeCommands(node_id);

    // AGU Legacy RF nodes (1..15) operate over SCI frames and do not route
    // through the modern CRC16/HMAC pending command queue. Directly assign
    // the node to the target group in the registry.
    if (isAguLegacyNodeId(node_id)) {
        registry_->assignNodeToGroup(node_id, group_id);
        if (outcome_sink_ != nullptr) {
            outcome_sink_->publishSafetyAudit("LEGACY_NODE_ASSIGNED", command_id);
        }
        return true;
    }

    static const ExternalOverridePolicy reassignment_policy{"FAIL_SAFE", 0, 1};
    if (!registry_->setDesiredState(node_id, NodePumpState::OFF) ||
        !queueExternalNodeCommand(node_id, NodePumpState::OFF, command_id,
                                     &reassignment_policy)) return false;
    pending_commands_[node_id].reassignment_pending = true;
    pending_commands_[node_id].reassignment_group_id = group_id;
    if (outcome_sink_ != nullptr) outcome_sink_->publishSafetyAudit("REASSIGNMENT_SAFE_OFF_PENDING", command_id);
    return true;
}

void PumpNodeController::cancelNodeCommands(uint8_t node_id) {
    if (isProductionNodeId(node_id)) {
        if (pending_commands_[node_id].active) {
            completePendingCommand(node_id, "CANCELED", "NODE_STALE_OR_FAULT_SAFE_OFF");
        }
    }
}

void PumpNodeController::cancelCommand(uint8_t node_id, const char* reason) {
    if (isProductionNodeId(node_id)) {
        if (pending_commands_[node_id].active) {
            completePendingCommand(node_id, "CANCELED", reason != nullptr ? reason : "MANUAL_CANCEL");
        }
    }
}

void PumpNodeController::publishOutcome(const PendingCommand& pending, const char* outcome, const char* reason) {
    if (outcome_sink_ != nullptr && pending.mqtt_command_id[0] != '\0') {
        outcome_sink_->publishCommandOutcome(pending.mqtt_command_id, outcome, pending.target_node_id, reason);
    }
}

void PumpNodeController::completePendingCommand(uint8_t node_id, const char* outcome, const char* reason) {
    PendingCommand pending = pending_commands_[node_id];
    pending_commands_[node_id] = PendingCommand{};
    // The only legal reassignment commit is correlated telemetry confirming the exact OFF command.
    const bool safe_off_acked = pending.reassignment_pending &&
        pending.desired_state == NodePumpState::OFF && std::strcmp(outcome, "COMPLETED") == 0;
    if (pending.reassignment_pending && !safe_off_acked) {
        registry_->latchFaultSafeOff(node_id);
        if (outcome_sink_) outcome_sink_->publishSafetyAudit("REASSIGNMENT_ABORTED_SAFE_OFF", reason);
        publishOutcome(pending, outcome, reason);
        return;
    }
    if (safe_off_acked && !registry_->assignNodeToGroup(node_id, pending.reassignment_group_id)) {
        registry_->latchFaultSafeOff(node_id);
        if (outcome_sink_) outcome_sink_->publishSafetyAudit("GATEWAY_DEGRADED_SAFE_OFF", "REASSIGNMENT_COMMIT_FAILED");
        publishOutcome(pending, "REJECTED", "REASSIGNMENT_COMMIT_FAILED_SAFE_OFF");
        return;
    }
    publishOutcome(pending, outcome, reason);
}

void PumpNodeController::latchFault(uint8_t node_id, const char* outcome, const char* reason) {
    PendingCommand pending = pending_commands_[node_id];
    pending_commands_[node_id] = PendingCommand{};
    command_correlations_[node_id] = NodeCommandCorrelation{};
    if (!registry_->latchFaultSafeOff(node_id) && outcome_sink_ != nullptr) {
        outcome_sink_->publishSafetyAudit("GATEWAY_DEGRADED_SAFE_OFF", "NODE_REGISTRY_LOCK_TIMEOUT");
    }
    publishOutcome(pending, outcome, reason);
}

uint32_t PumpNodeController::currentNodeBootSession(uint8_t node_id) const {
    if (!isProductionNodeId(node_id) || !session_trackers_[node_id].initialized) return 0;
    return session_trackers_[node_id].last_boot_session_id;
}

void PumpNodeController::activatePendingCorrelation(uint8_t node_id) {
    PendingCommand& pending = pending_commands_[node_id];
    NodeCommandCorrelation& correlation = command_correlations_[node_id];
    correlation.command_id = pending.command_id;
    correlation.boot_session_id = pending.node_boot_session_id;
    correlation.active = true;
}

bool PumpNodeController::hasCurrentCorrelation(uint8_t node_id, uint32_t command_id,
                                           uint32_t boot_session_id) const {
    if (!isProductionNodeId(node_id) || command_id == 0) return false;
    const NodeCommandCorrelation& correlation = command_correlations_[node_id];
    return correlation.active && correlation.command_id == command_id &&
           correlation.boot_session_id != 0 && correlation.boot_session_id == boot_session_id;
}

bool PumpNodeController::isRetryDue(uint8_t node_id, uint32_t current_time_ms) const {
    return current_time_ms - pending_commands_[node_id].last_sent_ms >= retry_interval_ms_;
}

bool PumpNodeController::isPendingDeadlineExpired(uint8_t node_id, uint32_t current_time_ms) const {
    const PendingCommand& pending = pending_commands_[node_id];
    if (pending.phase == PendingCommandPhase::AWAITING_PUMP_FEEDBACK) {
        return current_time_ms - pending.feedback_wait_started_ms >= RF_FEEDBACK_DEADLINE_MS;
    }
    return pending.phase == PendingCommandPhase::AWAITING_FLOW_CONFIRMATION &&
           current_time_ms - pending.flow_wait_started_ms >= node_flow_policies_[node_id].flow_start_timeout_ms;
}

bool PumpNodeController::hasProvisionedNodeLeasePolicy(uint8_t node_id) const {
    return isProductionNodeId(node_id) && node_policies_[node_id].provisioned;
}

bool PumpNodeController::canDispatchPumpOn(uint8_t node_id, const NodeState& state) const {
    return canAcceptPumpOn(state) && hasProvisionedNodeLeasePolicy(node_id) &&
           hasProvisionedNodeFlowPolicy(node_id);
}

bool PumpNodeController::buildPendingFrame(uint8_t node_id) {
    PendingCommand& pending = pending_commands_[node_id];
    if (pending.desired_state == NodePumpState::ON && !hasProvisionedNodeLeasePolicy(node_id)) return false;
    // The node protocol has one bounded lifetime field. For an OFF override
    // without an explicit lease, carry the override duration through that
    // field so the node—not the gateway—owns expiry and schedule resume.
    const uint32_t run_lease_ms = pending.override_run_lease_ms > 0
        ? pending.override_run_lease_ms
        : pending.override_duration_ms;
    const uint32_t max_on_ms = node_policies_[node_id].max_on_duration_ms;

    const SetPumpPayload payload{static_cast<uint8_t>(pending.desired_state), run_lease_ms, max_on_ms};
    const size_t frame_len = buildFrame(RfMessageType::SET_PUMP, node_id, pending.command_id,
                                        reinterpret_cast<const uint8_t*>(&payload), sizeof(payload),
                                        pending.frame, sizeof(pending.frame));
    if (frame_len == 0 || frame_len > UINT8_MAX) return false;

    pending.frame_len = static_cast<uint8_t>(frame_len);
    RfHeader header{};
    if (!decodeHeader(pending.frame, pending.frame_len, header)) return false;
    pending.sequence = header.sequence;
    if (pending.node_boot_session_id == 0) pending.node_boot_session_id = currentNodeBootSession(node_id);
    activatePendingCorrelation(node_id);
    return true;
}

bool PumpNodeController::dispatchPendingFrame(uint8_t node_id, uint32_t current_time_ms, bool is_retry) {
    PendingCommand& pending = pending_commands_[node_id];
    if (pending.frame_len == 0) return false;

    const size_t sent_bytes = transport_->send(pending.frame, pending.frame_len);
    if (sent_bytes != pending.frame_len) {
        latchFault(node_id, "TRANSPORT_ERROR",
                   is_retry ? "RF_RETRY_SEND_FAILED_SAFE_OFF" : "RF_SEND_FAILED_SAFE_OFF");
        return false;
    }
    pending.dispatched = true;
    pending.retries++;
    pending.last_sent_ms = current_time_ms;
    if (!is_retry) {
        if (hasProvisionedNodeFlowPolicy(node_id)) {
            FlowFaultEvaluator* eval = flow_evaluators_.getEvaluator(node_id);
            if (eval != nullptr) {
                eval->onCommandDispatched(current_time_ms, pending.command_id, pending.desired_state == NodePumpState::ON);
            }
        }
        if (pending.mqtt_command_id[0] != '\0') publishOutcome(pending, "QUEUED", "RF_DISPATCHED");
    }
    return true;
}

bool PumpNodeController::sendPendingCommand(uint8_t node_id, uint32_t current_time_ms, bool is_retry) {
    if (is_retry) {
        if (!isRetryDue(node_id, current_time_ms)) return true;
        if (pending_commands_[node_id].retries >= max_retries_) {
            latchFault(node_id, "TIMED_OUT", "RF_COMMAND_TIMEOUT_SAFE_OFF");
            if (outcome_sink_ != nullptr) {
                outcome_sink_->publishSafetyAudit("RF_TIMEOUT", "RF_RETRY_EXHAUSTED_SAFE_OFF");
            }
            return false;
        }
    }
    if (!is_retry && !buildPendingFrame(node_id)) return false;
    return dispatchPendingFrame(node_id, current_time_ms, is_retry);
}

bool PumpNodeController::queueInternalSafeOff(uint8_t node_id) {
    if (!isProductionNodeId(node_id) || pending_commands_[node_id].active) return false;
    const uint32_t command_id = next_command_id_++;
    PendingCommand& pending = pending_commands_[node_id];
    pending.active = true;
    pending.phase = PendingCommandPhase::AWAITING_ACK;
    pending.command_id = command_id;
    pending.target_node_id = node_id;
    pending.desired_state = NodePumpState::OFF;
    pending.node_boot_session_id = currentNodeBootSession(node_id);
    std::snprintf(pending.mqtt_command_id, sizeof(pending.mqtt_command_id), "sys_fault_off_%u", command_id);
    return true;
}

bool PumpNodeController::telemetryConfirmsPumpFeedback(uint8_t node_id, uint32_t command_id,
                                                   NodePumpState reported, uint8_t driver_feedback) const {
    const PendingCommand& pending = pending_commands_[node_id];
    if (!pending.active || pending.command_id != command_id) return false;
    const uint8_t expected_driver = pending.desired_state == NodePumpState::ON ? 1 : 0;
    return reported == pending.desired_state && driver_feedback == expected_driver;
}

bool PumpNodeController::isFlowWithinRange(uint8_t node_id, uint16_t flow_lpm_x100) const {
    return hasProvisionedNodeFlowPolicy(node_id) &&
           flow_lpm_x100 <= node_flow_policies_[node_id].max_flow_lpm_x100;
}

bool PumpNodeController::validateTelemetrySafety(uint8_t node_id, const TelemetryPayload& telemetry) const {
    if (!isBinaryState(telemetry.reported_pump_state) || !isBinaryState(telemetry.driver_feedback) ||
        telemetry.fault_flags != 0) return false;
    // Uncommissioned nodes may report authenticated telemetry, but no policy-free
    // flow inference (including FLOW_CONFIRMED) is permitted for them.
    if (!hasProvisionedNodeFlowPolicy(node_id)) return true;
    if (!isFlowWithinRange(node_id, telemetry.flow_lpm_x100)) return false;
    const bool is_off = telemetry.reported_pump_state == static_cast<uint8_t>(NodePumpState::OFF);
    return !is_off || telemetry.flow_lpm_x100 <= node_flow_policies_[node_id].max_off_flow_lpm_x100;
}

bool PumpNodeController::handlePendingTelemetry(uint8_t node_id, const TelemetryPayload& telemetry,
                                            NodePumpState reported, uint32_t current_time_ms) {
    PendingCommand& pending = pending_commands_[node_id];
    if (!pending.active || telemetry.last_command_id != pending.command_id) return true;
    if (pending.phase == PendingCommandPhase::AWAITING_PUMP_FEEDBACK) {
        if (!telemetryConfirmsPumpFeedback(node_id, telemetry.last_command_id, reported, telemetry.driver_feedback)) {
            latchFault(node_id, "NACK", "PUMP_FEEDBACK_MISMATCH_SAFE_OFF");
            return false;
        }
        if (pending.desired_state == NodePumpState::OFF) {
            completePendingCommand(node_id, "COMPLETED", "CORRELATED_OFF_TELEMETRY_CONFIRMED");
            return true;
        }
        pending.phase = PendingCommandPhase::AWAITING_FLOW_CONFIRMATION;
        pending.flow_wait_started_ms = current_time_ms;
        publishOutcome(pending, "PUMP_FEEDBACK_ON", "AWAITING_FLOW_CONFIRMATION");
    }
    if (pending.phase != PendingCommandPhase::AWAITING_FLOW_CONFIRMATION) return true;
    if (isPendingDeadlineExpired(node_id, current_time_ms)) {
        latchFlowFaultAndQueueSafeOff(node_id, "NO_FLOW_FAULT", "NO_FLOW_FAULT_SAFE_OFF");
        return false;
    }
    if (telemetry.flow_lpm_x100 >= node_flow_policies_[node_id].min_flow_lpm_x100) {
        completePendingCommand(node_id, "COMPLETED", "FLOW_CONFIRMED");
    }
    return true;
}

void PumpNodeController::latchFlowFaultAndQueueSafeOff(uint8_t node_id, const char* outcome, const char* reason) {
    latchFault(node_id, outcome, reason);
    if (hasProvisionedNodeFlowPolicy(node_id)) {
        FlowFaultEvaluator* eval = flow_evaluators_.getEvaluator(node_id);
        if (eval != nullptr && !eval->isFaultLatched()) {
            FlowFaultType fault_type = FlowFaultType::FAULT_UNEXPECTED_FLOW;
            if (outcome != nullptr) {
                if (std::strcmp(outcome, "NO_FLOW_FAULT") == 0) {
                    fault_type = FlowFaultType::FAULT_NO_FLOW;
                } else if (std::strcmp(outcome, "OVER_RANGE_FLOW") == 0) {
                    fault_type = FlowFaultType::FAULT_OVER_RANGE_FLOW;
                } else if (std::strcmp(outcome, "DRIVER_FEEDBACK_MISMATCH") == 0 ||
                           std::strcmp(outcome, "PUMP_FEEDBACK_MISMATCH") == 0) {
                    fault_type = FlowFaultType::FAULT_DRIVER_FEEDBACK_MISMATCH;
                }
            }
            eval->latchFault(0, fault_type, reason);
        }
    }
    const bool off_queued = queueInternalSafeOff(node_id);
    if (outcome_sink_ != nullptr) {
        outcome_sink_->publishSafetyAudit(reason, off_queued ? "EXPLICIT_OFF_QUEUED" : "EXPLICIT_OFF_ALREADY_QUEUED");
    }
}

void PumpNodeController::handlePendingDeadline(uint8_t node_id) {
    const NodePumpState desired_state = pending_commands_[node_id].desired_state;
    const PendingCommandPhase phase = pending_commands_[node_id].phase;
    if (desired_state == NodePumpState::ON && phase == PendingCommandPhase::AWAITING_FLOW_CONFIRMATION) {
        latchFlowFaultAndQueueSafeOff(node_id, "NO_FLOW_FAULT", "NO_FLOW_FAULT_SAFE_OFF");
        return;
    }
    latchFault(node_id, "TIMED_OUT", "RF_PUMP_FEEDBACK_TIMEOUT_SAFE_OFF");
    if (desired_state == NodePumpState::ON && queueInternalSafeOff(node_id) && outcome_sink_ != nullptr) {
        outcome_sink_->publishSafetyAudit("RF_PUMP_FEEDBACK_TIMEOUT_SAFE_OFF", "EXPLICIT_OFF_QUEUED");
    }
}

bool PumpNodeController::serviceCommandFanout(uint32_t current_time_ms) {
    if (!initialized_ || !isProvisioned() || registry_ == nullptr || transport_ == nullptr) {
        return false;
    }

    bool all_dispatched_successfully = true;
    for (uint8_t node_id = RF_PRODUCTION_MIN_NODE_ID; node_id <= RF_PRODUCTION_MAX_NODE_ID; ++node_id) {
        const bool dispatched = pending_commands_[node_id].active
            ? servicePendingCommand(node_id, current_time_ms)
            : serviceDesiredStateDivergence(node_id, current_time_ms);
        if (!dispatched) all_dispatched_successfully = false;
    }
    return all_dispatched_successfully;
}

bool PumpNodeController::servicePendingCommand(uint8_t node_id, uint32_t current_time_ms) {
    if (isPendingDeadlineExpired(node_id, current_time_ms)) {
        handlePendingDeadline(node_id);
        return true;
    }
    const PendingCommandPhase phase = pending_commands_[node_id].phase;
    if (phase == PendingCommandPhase::AWAITING_PUMP_FEEDBACK ||
        phase == PendingCommandPhase::AWAITING_FLOW_CONFIRMATION) return true;
    return sendPendingCommand(node_id, current_time_ms, pending_commands_[node_id].dispatched);
}

bool PumpNodeController::serviceDesiredStateDivergence(uint8_t node_id, uint32_t current_time_ms) {
    NodeState state;
    if (!registry_->getNodeState(node_id, state) || state.desired_state == state.reported_state) return true;
    if (state.desired_state == NodePumpState::ON && !canDispatchPumpOn(node_id, state)) {
        registry_->setDesiredState(node_id, NodePumpState::OFF);
        return true;
    }
    PendingCommand& pending = pending_commands_[node_id];
    const uint32_t command_id = next_command_id_++;
    pending.active = true;
    pending.command_id = command_id;
    pending.target_node_id = node_id;
    pending.desired_state = state.desired_state;
    pending.node_boot_session_id = currentNodeBootSession(node_id);
    std::snprintf(pending.mqtt_command_id, sizeof(pending.mqtt_command_id), "sys_auto_%u", command_id);
    return sendPendingCommand(node_id, current_time_ms, false);
}

bool PumpNodeController::handleAckFrame(uint8_t src_node, const RfHeader& header, const uint8_t* payload,
                                    uint8_t payload_len, uint32_t current_time_ms) {
    if (payload_len != sizeof(CommandAckPayload)) return false;
    CommandAckPayload ack;
    std::memcpy(&ack, payload, sizeof(ack));
    if (!validateAck(header, ack)) return false;
    PendingCommand& pending = pending_commands_[src_node];
    if (pending.node_boot_session_id != 0 && pending.node_boot_session_id != header.boot_session_id) {
        return false;
    }
    pending.node_boot_session_id = header.boot_session_id;
    activatePendingCorrelation(src_node);
#if !defined(ATMEGA8_NODE_BUILD)
    duplicate_cache_.put(src_node, header.boot_session_id, header.sequence, header.command_id,
                         header.message_type, payload, payload_len, current_time_ms);
#endif
    if (hasProvisionedNodeFlowPolicy(src_node)) {
        FlowFaultEvaluator* eval = flow_evaluators_.getEvaluator(src_node);
        if (eval != nullptr) {
            eval->onRfAckReceived(current_time_ms, header.sequence, ack.ack_outcome);
        }
    }
    if (ack.ack_outcome != static_cast<uint8_t>(AckOutcome::SUCCESS)) {
        latchFault(src_node, "NACK", "RF_NODE_REJECTED_SAFE_OFF");
        return true;
    }
    // ACK only confirms command receipt/outcome. Pump state, feedback and flow
    // are updated exclusively by correlated TELEMETRY frames.
    if (!registry_->refreshLiveness(src_node, current_time_ms)) return false;
    // A duplicate ACK is liveness evidence only. It must never extend the
    // feedback deadline after the immutable command has entered that phase.
    if (pending.phase != PendingCommandPhase::AWAITING_ACK) return true;
    pending.phase = PendingCommandPhase::AWAITING_PUMP_FEEDBACK;
    pending.feedback_wait_started_ms = current_time_ms;
    publishOutcome(pending, "RF_ACKED", "RF_ACK_SUCCESS_AWAITING_PUMP_FEEDBACK");
    return true;
}

bool PumpNodeController::handleTelemetryFrame(uint8_t src_node, const RfHeader& header, const uint8_t* payload, uint8_t payload_len,
                                          uint32_t current_time_ms) {
    if (payload_len != sizeof(TelemetryPayload)) return false;
    TelemetryPayload telemetry;
    std::memcpy(&telemetry, payload, sizeof(telemetry));
    if (telemetry.last_command_id != 0 &&
        !hasCurrentCorrelation(src_node, telemetry.last_command_id, header.boot_session_id)) {
        return false;
    }
    if (!validateTelemetrySafety(src_node, telemetry)) {
        const bool unexpected_flow = telemetry.reported_pump_state == static_cast<uint8_t>(NodePumpState::OFF) &&
            telemetry.flow_lpm_x100 > node_flow_policies_[src_node].max_off_flow_lpm_x100;
        if (unexpected_flow) {
            latchFlowFaultAndQueueSafeOff(src_node, "UNEXPECTED_FLOW_FAULT", "UNEXPECTED_FLOW_FAULT_SAFE_OFF");
            return false;
        }
        latchFlowFaultAndQueueSafeOff(src_node, "NACK", "INVALID_OR_FAULT_TELEMETRY_SAFE_OFF");
        return false;
    }
    const NodePumpState reported = telemetry.reported_pump_state == 1 ? NodePumpState::ON : NodePumpState::OFF;
    if (!registry_->updateTelemetryDetailed(src_node, reported, telemetry.driver_feedback,
                                            0, 0, telemetry.flow_lpm_x100,
                                            telemetry.pulse_count, telemetry.delivered_volume_ml,
                                            current_time_ms, current_time_ms,
                                            telemetry.last_command_id, telemetry.fault_flags)) {
        return false;
    }
    PendingCommand& pending = pending_commands_[src_node];
    if (pending.active && (telemetry.last_command_id == 0 || telemetry.last_command_id == pending.command_id)) {
        if (hasProvisionedNodeFlowPolicy(src_node)) {
            FlowFaultEvaluator* eval = flow_evaluators_.getEvaluator(src_node);
            if (eval != nullptr) {
                eval->evaluateTelemetry(current_time_ms, telemetry.last_command_id,
                                       telemetry.reported_pump_state, telemetry.driver_feedback,
                                       0, telemetry.flow_lpm_x100, telemetry.pulse_count,
                                       telemetry.fault_flags);
                if (eval->isFaultLatched()) {
                    latchFlowFaultAndQueueSafeOff(src_node, FlowFaultEvaluator::getFaultTypeString(eval->getLatchedFault()),
                                                 eval->getLastAuditRecord().reason_phrase);
                    return false;
                }
            }
        }
    }
    return handlePendingTelemetry(src_node, telemetry, reported, current_time_ms);
}

bool PumpNodeController::resetNodeFault(uint8_t node_id, uint32_t now_ms) {
    if (!isProductionNodeId(node_id)) return false;
    if (registry_ != nullptr) {
        registry_->resetFault(node_id);
    }
    FlowFaultEvaluator* eval = flow_evaluators_.getEvaluator(node_id);
    if (eval != nullptr) {
        eval->clearLatchedFault(now_ms);
    }
    if (outcome_sink_ != nullptr) {
        char reason[48];
        std::snprintf(reason, sizeof(reason), "Node %u fault latch cleared", node_id);
        outcome_sink_->publishSafetyAudit("FAULT_RESET", reason);
    }
    return true;
}

bool PumpNodeController::handleHeartbeatFrame(uint8_t src_node, const uint8_t* payload, uint8_t payload_len,
                                          uint32_t current_time_ms) {
    if (payload_len != sizeof(HeartbeatPayload)) return false;
    HeartbeatPayload heartbeat;
    std::memcpy(&heartbeat, payload, sizeof(heartbeat));
    const bool is_battery_powered = heartbeat.battery_percent <= 100;
    const bool is_ac_powered = heartbeat.battery_percent == 255;
    if ((!is_battery_powered && !is_ac_powered) || heartbeat.rssi_dbm > 0 || heartbeat.rssi_dbm < -127) return false;
    return registry_->refreshLiveness(src_node, current_time_ms);
}

bool PumpNodeController::handlePongFrame(uint8_t src_node, const uint8_t* payload, uint8_t payload_len,
                                     uint32_t current_time_ms) {
    if (payload_len != sizeof(PongPayload)) return false;
    return registry_->refreshLiveness(src_node, current_time_ms);
}

void PumpNodeController::handleNodeSessionChange(uint8_t node_id) {
    // A node session boundary invalidates every feedback/fault correlation from
    // the previous boot, including a previously sent safe-OFF command.
    command_correlations_[node_id] = NodeCommandCorrelation{};
    if (pending_commands_[node_id].active) {
        completePendingCommand(node_id, "CANCELED", "NODE_REBOOT_SESSION_CHANGED_SAFE_OFF");
    }
    if (!registry_->latchFaultSafeOff(node_id) && outcome_sink_ != nullptr) {
        outcome_sink_->publishSafetyAudit("GATEWAY_DEGRADED_SAFE_OFF", "NODE_REBOOT_LOCK_FAILURE");
    }
    bool reboot_detected = false;
    if (registry_ != nullptr) {
        registry_->updateBootSession(node_id, session_trackers_[node_id].last_boot_session_id, reboot_detected);
    }
#if !defined(ATMEGA8_NODE_BUILD)
    duplicate_cache_.invalidateNode(node_id);
#endif
    const uint32_t command_id = next_command_id_++;
    PendingCommand& pending = pending_commands_[node_id];
    pending.active = true;
    pending.phase = PendingCommandPhase::AWAITING_ACK;
    pending.command_id = command_id;
    pending.target_node_id = node_id;
    pending.desired_state = NodePumpState::OFF;
    pending.node_boot_session_id = currentNodeBootSession(node_id);
    std::snprintf(pending.mqtt_command_id, sizeof(pending.mqtt_command_id), "sys_reboot_off_%u", command_id);
    if (outcome_sink_ != nullptr) outcome_sink_->publishSafetyAudit("NODE_REBOOT_SAFE_OFF_QUEUED", pending.mqtt_command_id);
}

bool PumpNodeController::handleFaultFrame(uint8_t src_node, const RfHeader& header, const uint8_t* payload,
                                      uint8_t payload_len) {
    if (payload_len != sizeof(FaultReportPayload)) return false;
    FaultReportPayload fault;
    std::memcpy(&fault, payload, sizeof(fault));
    if (fault.command_id != 0 && !hasCurrentCorrelation(src_node, fault.command_id, header.boot_session_id)) {
        return false;
    }
    latchFault(src_node, "NACK", "RF_FAULT_REPORT_SAFE_OFF");
    return true;
}

bool PumpNodeController::handleIncomingFrame(const uint8_t* frame, size_t len, uint32_t current_time_ms) {
    if (!initialized_ || !isProvisioned() || registry_ == nullptr) return false;
    RfHeader header;
    uint8_t payload[64];
    uint8_t payload_len = 0;
    bool new_session = false;
    if (!parseFrame(frame, len, header, payload, payload_len, &new_session) || header.source_node_id == 0 ||
        header.target_node_id != 0) return false;

    if (new_session) handleNodeSessionChange(header.source_node_id);

    switch (static_cast<RfMessageType>(header.message_type)) {
        case RfMessageType::COMMAND_ACK:
            return handleAckFrame(header.source_node_id, header, payload, payload_len, current_time_ms);
        case RfMessageType::TELEMETRY:
            return handleTelemetryFrame(header.source_node_id, header, payload, payload_len, current_time_ms);
        case RfMessageType::HEARTBEAT:
            return handleHeartbeatFrame(header.source_node_id, payload, payload_len, current_time_ms);
        case RfMessageType::PONG:
            return handlePongFrame(header.source_node_id, payload, payload_len, current_time_ms);
        case RfMessageType::FAULT_REPORT:
            return handleFaultFrame(header.source_node_id, header, payload, payload_len);
        default:
            return false;
    }
}

bool PumpNodeController::validateAck(const RfHeader& header, const CommandAckPayload& ack) const {
    if (!isProductionNodeId(header.source_node_id) || header.target_node_id != 0 ||
        !isAckOutcome(ack.ack_outcome) || !isBinaryState(ack.reported_pump_state) ||
        !isBinaryState(ack.driver_feedback)) return false;
    const PendingCommand& pending = pending_commands_[header.source_node_id];
    return pending.active && pending.dispatched && pending.target_node_id == header.source_node_id &&
           pending.command_id == header.command_id && pending.sequence == ack.ack_sequence;
}
