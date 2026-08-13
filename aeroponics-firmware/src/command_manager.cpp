#include "command_manager.h"
#include "rf_provisioning.h"
#include <cstring>
#include <cstdio>

namespace {

bool isBinaryState(uint8_t value) { return value == 0 || value == 1; }
bool isAckOutcome(uint8_t value) { return value <= static_cast<uint8_t>(AckOutcome::REJECTED_UNKNOWN_NODE); }
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

CommandManager::CommandManager()
    : registry_(nullptr), transport_(nullptr), boot_session_id_(1), sequence_num_(0),
      next_command_id_(1000), initialized_(false) {
    for (size_t i = 0; i <= MAX_NODES; ++i) {
        node_policies_[i] = NodeLeasePolicy{DEFAULT_RUN_LEASE_MS, DEFAULT_MAX_ON_DURATION_MS};
        pending_commands_[i] = PendingCommand{};
        session_trackers_[i] = NodeSessionTracker{};
    }
}

CommandManager::~CommandManager() {}

bool CommandManager::begin(NodeRegistry* registry, IRfTransport* transport) {
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

bool CommandManager::setPskKey(const uint8_t* psk, size_t len) {
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

bool CommandManager::provisionFromNvs(NvsStorage& storage) {
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

bool CommandManager::setNodeLeasePolicy(uint8_t node_id, uint32_t run_lease_ms, uint32_t max_on_duration_ms) {
    if (node_id < 1 || node_id > MAX_NODES) return false;
    if (run_lease_ms == 0 || max_on_duration_ms < run_lease_ms) return false;
    node_policies_[node_id].run_lease_ms = run_lease_ms;
    node_policies_[node_id].max_on_duration_ms = max_on_duration_ms;
    return true;
}

bool CommandManager::getNodeLeasePolicy(uint8_t node_id, uint32_t &out_run_lease_ms, uint32_t &out_max_on_duration_ms) const {
    if (node_id < 1 || node_id > MAX_NODES) return false;
    out_run_lease_ms = node_policies_[node_id].run_lease_ms;
    out_max_on_duration_ms = node_policies_[node_id].max_on_duration_ms;
    return true;
}

uint16_t CommandManager::calculateCrc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= (static_cast<uint16_t>(data[i]) << 8);
        for (uint8_t j = 0; j < 8; ++j) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

size_t CommandManager::buildFrame(RfMessageType msg_type, uint8_t target_node_id, uint32_t command_id,
                                  const uint8_t* payload, uint8_t payload_len, uint8_t* out_buffer, size_t buffer_size) {
    size_t header_len = sizeof(RfHeader);
    size_t total_len = header_len + payload_len + HMAC_TAG_SIZE + 2;
    if (!isProvisioned() || buffer_size < total_len || payload_len > 64 ||
        (payload_len > 0 && payload == nullptr)) {
        return 0;
    }

    RfHeader header;
    header.sof[0] = RF_SOF_BYTE_1;
    header.sof[1] = RF_SOF_BYTE_2;
    header.version = RF_PROTOCOL_VERSION;
    header.message_type = static_cast<uint8_t>(msg_type);
    header.target_node_id = target_node_id;
    header.source_node_id = 0; // Gateway is node 0
    header.boot_session_id = boot_session_id_;
    header.sequence = sequence_num_++;
    header.command_id = command_id;
    header.payload_len = payload_len;

    std::memcpy(out_buffer, &header, header_len);
    if (payload_len > 0 && payload != nullptr) {
        std::memcpy(out_buffer + header_len, payload, payload_len);
    }

    // Calculate truncated HMAC-SHA256 over Header + Payload
    uint8_t mac_tag[HMAC_TAG_SIZE];
    HmacSha256::calculateTruncated(psk_key_, sizeof(psk_key_), out_buffer, header_len + payload_len, mac_tag);
    std::memcpy(out_buffer + header_len + payload_len, mac_tag, HMAC_TAG_SIZE);

    // Calculate CRC-16 over Header + Payload + MAC
    uint16_t crc = calculateCrc16(out_buffer, header_len + payload_len + HMAC_TAG_SIZE);
    out_buffer[header_len + payload_len + HMAC_TAG_SIZE] = static_cast<uint8_t>(crc & 0xFF);
    out_buffer[header_len + payload_len + HMAC_TAG_SIZE + 1] = static_cast<uint8_t>((crc >> 8) & 0xFF);

    return total_len;
}

AntiReplayResult CommandManager::validateAntiReplay(uint8_t src_node, uint32_t session_id, uint16_t sequence) {
    if (src_node == 0 || src_node > MAX_NODES) return AntiReplayResult::REJECTED;
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

bool CommandManager::validateFrameEnvelope(const uint8_t* frame_data, size_t frame_len,
                                           RfHeader& out_header) const {
    const size_t header_len = sizeof(RfHeader);
    if (!isProvisioned() || frame_data == nullptr || frame_len < header_len + HMAC_TAG_SIZE + 2) {
        return false;
    }
    std::memcpy(&out_header, frame_data, header_len);
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

bool CommandManager::validateAddressing(const RfHeader& header) const {
    if (header.message_type < static_cast<uint8_t>(RfMessageType::PING) ||
        header.message_type > static_cast<uint8_t>(RfMessageType::FAULT_REPORT)) {
        return false;
    }
    return header.source_node_id <= MAX_NODES &&
           (header.source_node_id == 0 || header.target_node_id == 0);
}

bool CommandManager::verifyCrcAndMac(const uint8_t* frame_data, const RfHeader& header) const {
    const size_t header_len = sizeof(RfHeader);
    const size_t crc_check_len = header_len + header.payload_len + HMAC_TAG_SIZE;
    const uint16_t expected_crc = calculateCrc16(frame_data, crc_check_len);
    const uint16_t actual_crc = static_cast<uint16_t>(frame_data[crc_check_len]) |
                                (static_cast<uint16_t>(frame_data[crc_check_len + 1]) << 8);
    if (expected_crc != actual_crc) return false;

    uint8_t expected_mac[HMAC_TAG_SIZE];
    HmacSha256::calculateTruncated(psk_key_, sizeof(psk_key_), frame_data,
                                  header_len + header.payload_len, expected_mac);
    return constantTimeCompare(expected_mac, frame_data + header_len + header.payload_len, HMAC_TAG_SIZE);
}

bool CommandManager::parseFrame(const uint8_t* frame_data, size_t frame_len, RfHeader &out_header,
                                 uint8_t* out_payload, uint8_t &out_payload_len, bool* out_new_session) {
    if (!validateFrameEnvelope(frame_data, frame_len, out_header) ||
        !validateAddressing(out_header) || !verifyCrcAndMac(frame_data, out_header)) {
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

    out_payload_len = out_header.payload_len;
    if (out_payload_len > 0 && out_payload != nullptr) {
        std::memcpy(out_payload, frame_data + sizeof(RfHeader), out_payload_len);
    }

    return true;
}

bool CommandManager::isPending(uint8_t node_id) const {
    if (node_id < 1 || node_id > MAX_NODES) return false;
    return pending_commands_[node_id].active;
}

bool CommandManager::queueExternalNodeCommand(uint8_t node_id, NodePumpState desired, const char* command_id) {
    if (!initialized_ || registry_ == nullptr || !isValidMqttCommandId(command_id)) {
        return false;
    }
    if (node_id < 1 || node_id > MAX_NODES) return false;

    // Idempotent duplicate check
    if (pending_commands_[node_id].active) {
        if (strncmp(pending_commands_[node_id].mqtt_command_id, command_id, sizeof(PendingCommand::mqtt_command_id)) == 0) {
            return true;
        }
        return false;
    }

    NodeState state;
    if (!registry_->getNodeState(node_id, state) ||
        (desired == NodePumpState::ON && !canAcceptPumpOn(state))) {
        return false;
    }
    if (!registry_->setDesiredState(node_id, desired)) return false;

    uint32_t cid = next_command_id_++;
    pending_commands_[node_id].active = true;
    pending_commands_[node_id].dispatched = false;
    pending_commands_[node_id].command_id = cid;
    pending_commands_[node_id].target_node_id = node_id;
    pending_commands_[node_id].desired_state = desired;
    pending_commands_[node_id].sequence = 0;
    pending_commands_[node_id].retries = 0;
    pending_commands_[node_id].last_sent_ms = 0;
    std::strncpy(pending_commands_[node_id].mqtt_command_id, command_id,
                 sizeof(pending_commands_[node_id].mqtt_command_id) - 1);
    pending_commands_[node_id].mqtt_command_id[sizeof(pending_commands_[node_id].mqtt_command_id) - 1] = '\0';
    return true;
}

bool CommandManager::requestNodeReassignment(uint8_t node_id, uint8_t group_id, const char* command_id) {
    if (!initialized_ || registry_ == nullptr || node_id < 1 || node_id > MAX_NODES ||
        group_id > MAX_TIMER_GROUPS || !isValidMqttCommandId(command_id)) return false;
    NodeState state;
    if (!registry_->getNodeState(node_id, state)) return false;
    if (state.group_id == group_id && !pending_commands_[node_id].active) return true;
    if (pending_commands_[node_id].active) cancelNodeCommands(node_id);
    if (!registry_->setDesiredState(node_id, NodePumpState::OFF) ||
        !queueExternalNodeCommand(node_id, NodePumpState::OFF, command_id)) return false;
    pending_commands_[node_id].reassignment_pending = true;
    pending_commands_[node_id].reassignment_group_id = group_id;
    if (outcome_sink_ != nullptr) outcome_sink_->publishSafetyAudit("REASSIGNMENT_SAFE_OFF_PENDING", command_id);
    return true;
}

void CommandManager::cancelNodeCommands(uint8_t node_id) {
    if (node_id >= 1 && node_id <= MAX_NODES) {
        if (pending_commands_[node_id].active) {
            completePendingCommand(node_id, "CANCELED", "NODE_STALE_OR_FAULT_SAFE_OFF");
        }
    }
}

void CommandManager::publishOutcome(const PendingCommand& pending, const char* outcome, const char* reason) {
    if (outcome_sink_ != nullptr && pending.mqtt_command_id[0] != '\0') {
        outcome_sink_->publishCommandOutcome(pending.mqtt_command_id, outcome, pending.target_node_id, reason);
    }
}

void CommandManager::completePendingCommand(uint8_t node_id, const char* outcome, const char* reason) {
    PendingCommand pending = pending_commands_[node_id];
    pending_commands_[node_id] = PendingCommand{};
    // The only legal reassignment commit is a verified SUCCESS ACK for the exact OFF command.
    const bool safe_off_acked = pending.reassignment_pending &&
        pending.desired_state == NodePumpState::OFF && std::strcmp(outcome, "RF_ACKED") == 0;
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

void CommandManager::latchFault(uint8_t node_id, const char* outcome, const char* reason) {
    PendingCommand pending = pending_commands_[node_id];
    pending_commands_[node_id] = PendingCommand{};
    if (!registry_->latchFaultSafeOff(node_id) && outcome_sink_ != nullptr) {
        outcome_sink_->publishSafetyAudit("GATEWAY_DEGRADED_SAFE_OFF", "NODE_REGISTRY_LOCK_TIMEOUT");
    }
    publishOutcome(pending, outcome, reason);
}

bool CommandManager::sendPendingCommand(uint8_t node_id, uint32_t current_time_ms, bool is_retry) {
    PendingCommand& pending = pending_commands_[node_id];

    if (is_retry) {
        if (current_time_ms - pending.last_sent_ms < RF_RETRY_INTERVAL_MS) {
            return true;
        }
        if (pending.retries >= MAX_RF_RETRIES) {
            latchFault(node_id, "TIMED_OUT", "RF_COMMAND_TIMEOUT_SAFE_OFF");
            return false;
        }
    }

    if (!is_retry) {
        uint32_t run_lease_ms = DEFAULT_RUN_LEASE_MS;
        uint32_t max_on_ms = DEFAULT_MAX_ON_DURATION_MS;
        getNodeLeasePolicy(node_id, run_lease_ms, max_on_ms);

        SetPumpPayload payload;
        payload.desired_state = static_cast<uint8_t>(pending.desired_state);
        payload.run_lease_ms = run_lease_ms;
        payload.max_on_duration_ms = max_on_ms;

        const size_t frame_len = buildFrame(RfMessageType::SET_PUMP, node_id, pending.command_id,
                                            reinterpret_cast<const uint8_t*>(&payload), sizeof(payload),
                                            pending.frame, sizeof(pending.frame));
        if (frame_len == 0 || frame_len > UINT8_MAX) return false;
        pending.frame_len = static_cast<uint8_t>(frame_len);
        RfHeader header{};
        std::memcpy(&header, pending.frame, sizeof(header));
        pending.sequence = header.sequence;
    }

    if (pending.frame_len == 0) return false;
    const size_t sent_bytes = transport_->send(pending.frame, pending.frame_len);
    if (sent_bytes == pending.frame_len) {
        pending.dispatched = true;
        pending.retries++;
        pending.last_sent_ms = current_time_ms;
        if (!is_retry && pending.mqtt_command_id[0] != '\0') {
            publishOutcome(pending, "QUEUED", "RF_DISPATCHED");
        }
        return true;
    } else {
        latchFault(node_id, "TRANSPORT_ERROR", is_retry ? "RF_RETRY_SEND_FAILED_SAFE_OFF" : "RF_SEND_FAILED_SAFE_OFF");
        return false;
    }
}

bool CommandManager::serviceCommandFanout(uint32_t current_time_ms) {
    if (!initialized_ || !isProvisioned() || registry_ == nullptr || transport_ == nullptr) {
        return false;
    }

    bool all_dispatched_successfully = true;

    for (uint8_t node_id = 1; node_id <= MAX_NODES; ++node_id) {
        if (pending_commands_[node_id].active) {
            if (!sendPendingCommand(node_id, current_time_ms, pending_commands_[node_id].dispatched)) {
                all_dispatched_successfully = false;
            }
            continue;
        }

        // Auto-queue internal command if desired_state != reported_state
        NodeState state;
        if (!registry_->getNodeState(node_id, state)) continue;
        if (state.desired_state == NodePumpState::ON && !canAcceptPumpOn(state)) continue;

        if (state.desired_state != state.reported_state) {
            uint32_t cid = next_command_id_++;
            pending_commands_[node_id].active = true;
            pending_commands_[node_id].dispatched = false;
            pending_commands_[node_id].command_id = cid;
            pending_commands_[node_id].target_node_id = node_id;
            pending_commands_[node_id].desired_state = state.desired_state;
            pending_commands_[node_id].sequence = 0;
            pending_commands_[node_id].retries = 0;
            pending_commands_[node_id].last_sent_ms = 0;
            snprintf(pending_commands_[node_id].mqtt_command_id,
                     sizeof(pending_commands_[node_id].mqtt_command_id),
                     "sys_auto_%u", cid);

            if (!sendPendingCommand(node_id, current_time_ms, false)) {
                all_dispatched_successfully = false;
            }
        }
    }
    return all_dispatched_successfully;
}

bool CommandManager::handleAckFrame(uint8_t src_node, const RfHeader& header, const uint8_t* payload,
                                    uint8_t payload_len, uint32_t current_time_ms) {
    if (payload_len < sizeof(CommandAckPayload)) return false;
    CommandAckPayload ack;
    std::memcpy(&ack, payload, sizeof(ack));
    if (!validateAck(header, ack)) return false;
    if (ack.ack_outcome != static_cast<uint8_t>(AckOutcome::SUCCESS)) {
        latchFault(src_node, "NACK", "RF_NODE_REJECTED_SAFE_OFF");
        return true;
    }
    const NodePumpState reported = ack.reported_pump_state == 1 ? NodePumpState::ON : NodePumpState::OFF;
    if (!registry_->updateTelemetry(src_node, reported, ack.driver_feedback, 0, 0, current_time_ms)) return false;
    completePendingCommand(src_node, "RF_ACKED", "RF_ACK_SUCCESS");
    return true;
}

bool CommandManager::handleTelemetryFrame(uint8_t src_node, const uint8_t* payload, uint8_t payload_len,
                                          uint32_t current_time_ms) {
    if (payload_len < sizeof(TelemetryPayload)) return false;
    TelemetryPayload telemetry;
    std::memcpy(&telemetry, payload, sizeof(telemetry));
    if (!isBinaryState(telemetry.reported_pump_state) || !isBinaryState(telemetry.driver_feedback) ||
        telemetry.flow_lpm_x100 > 600 || telemetry.fault_flags != 0) {
        latchFault(src_node, "NACK", "INVALID_OR_FAULT_TELEMETRY_SAFE_OFF");
        return false;
    }
    const NodePumpState reported = telemetry.reported_pump_state == 1 ? NodePumpState::ON : NodePumpState::OFF;
    return registry_->updateTelemetry(src_node, reported, telemetry.driver_feedback,
                                      telemetry.flow_lpm_x100, telemetry.delivered_volume_ml, current_time_ms);
}

bool CommandManager::handleHeartbeatFrame(uint8_t src_node, const uint8_t* payload, uint8_t payload_len,
                                          uint32_t current_time_ms) {
    if (payload_len != sizeof(HeartbeatPayload)) return false;
    HeartbeatPayload heartbeat;
    std::memcpy(&heartbeat, payload, sizeof(heartbeat));
    if (heartbeat.battery_percent > 100 || heartbeat.rssi_dbm > 0 || heartbeat.rssi_dbm < -127) return false;
    return registry_->refreshLiveness(src_node, current_time_ms);
}

bool CommandManager::handlePongFrame(uint8_t src_node, const uint8_t* payload, uint8_t payload_len,
                                     uint32_t current_time_ms) {
    if (payload_len != sizeof(PongPayload)) return false;
    return registry_->refreshLiveness(src_node, current_time_ms);
}

void CommandManager::handleNodeSessionChange(uint8_t node_id) {
    const bool pending_safe_off = pending_commands_[node_id].active &&
                                  pending_commands_[node_id].desired_state == NodePumpState::OFF;
    if (pending_commands_[node_id].active && !pending_safe_off) {
        completePendingCommand(node_id, "CANCELED", "NODE_REBOOT_SESSION_CHANGED_SAFE_OFF");
    }
    if (!registry_->latchFaultSafeOff(node_id) && outcome_sink_ != nullptr) {
        outcome_sink_->publishSafetyAudit("GATEWAY_DEGRADED_SAFE_OFF", "NODE_REBOOT_LOCK_FAILURE");
    }
    // An already-dispatched OFF is an explicit safe-OFF for this new session;
    // preserve its correlation so its authenticated ACK can complete the flow.
    if (pending_safe_off) {
        if (outcome_sink_ != nullptr) outcome_sink_->publishSafetyAudit("NODE_REBOOT_SAFE_OFF_PENDING", pending_commands_[node_id].mqtt_command_id);
        return;
    }
    const uint32_t command_id = next_command_id_++;
    PendingCommand& pending = pending_commands_[node_id];
    pending.active = true;
    pending.command_id = command_id;
    pending.target_node_id = node_id;
    pending.desired_state = NodePumpState::OFF;
    std::snprintf(pending.mqtt_command_id, sizeof(pending.mqtt_command_id), "sys_reboot_off_%u", command_id);
    if (outcome_sink_ != nullptr) outcome_sink_->publishSafetyAudit("NODE_REBOOT_SAFE_OFF_QUEUED", pending.mqtt_command_id);
}

bool CommandManager::handleFaultFrame(uint8_t src_node, uint8_t payload_len) {
    if (payload_len < sizeof(FaultReportPayload)) return false;
    latchFault(src_node, "NACK", "RF_FAULT_REPORT_SAFE_OFF");
    return true;
}

bool CommandManager::handleIncomingFrame(const uint8_t* frame, size_t len, uint32_t current_time_ms) {
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
            return handleTelemetryFrame(header.source_node_id, payload, payload_len, current_time_ms);
        case RfMessageType::HEARTBEAT:
            return handleHeartbeatFrame(header.source_node_id, payload, payload_len, current_time_ms);
        case RfMessageType::PONG:
            return handlePongFrame(header.source_node_id, payload, payload_len, current_time_ms);
        case RfMessageType::FAULT_REPORT:
            return handleFaultFrame(header.source_node_id, payload_len);
        default:
            return false;
    }
}

bool CommandManager::validateAck(const RfHeader& header, const CommandAckPayload& ack) const {
    if (header.source_node_id == 0 || header.source_node_id > MAX_NODES || header.target_node_id != 0 ||
        !isAckOutcome(ack.ack_outcome) || !isBinaryState(ack.reported_pump_state) ||
        !isBinaryState(ack.driver_feedback)) return false;
    const PendingCommand& pending = pending_commands_[header.source_node_id];
    return pending.active && pending.dispatched && pending.target_node_id == header.source_node_id &&
           pending.command_id == header.command_id && pending.sequence == ack.ack_sequence;
}
