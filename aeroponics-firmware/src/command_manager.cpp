#include "command_manager.h"
#include <cstring>

namespace {
constexpr char PSK_NVS_KEYS[][9] = {"rf_psk_0", "rf_psk_1", "rf_psk_2", "rf_psk_3"};
constexpr char BOOT_SESSION_NVS_KEY[] = "rf_boot";

bool isBinaryState(uint8_t value) { return value == 0 || value == 1; }
bool isAckOutcome(uint8_t value) { return value <= static_cast<uint8_t>(AckOutcome::REJECTED_UNKNOWN_NODE); }
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
    if (!storage.isInitialized() || !storage.getU32(BOOT_SESSION_NVS_KEY, previous_session)) return false;
    for (size_t i = 0; i < 4; ++i) {
        if (!storage.getU32(PSK_NVS_KEYS[i], words[i])) return false;
    }
    const uint16_t next_session = static_cast<uint16_t>(previous_session + 1U);
    if (next_session == 0 || !storage.setU32(BOOT_SESSION_NVS_KEY, next_session)) return false;
    std::memcpy(psk_key_, words, sizeof(psk_key_));
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

bool CommandManager::validateAntiReplay(uint8_t src_node, uint16_t session_id, uint16_t sequence) {
    if (src_node == 0 || src_node > MAX_NODES) return false;
    NodeSessionTracker &tracker = session_trackers_[src_node];

    if (!tracker.initialized) {
        tracker.last_boot_session_id = session_id;
        tracker.last_sequence_num = sequence;
        tracker.initialized = true;
        return true;
    }

    if (session_id > tracker.last_boot_session_id) {
        tracker.last_boot_session_id = session_id;
        tracker.last_sequence_num = sequence;
        return true;
    } else if (session_id == tracker.last_boot_session_id) {
        // RFC-style serial arithmetic: only the next half of the uint16 space
        // is newer. It admits normal wrap while rejecting duplicates/stale frames.
        const uint16_t distance = static_cast<uint16_t>(sequence - tracker.last_sequence_num);
        if (distance != 0 && distance < 0x8000U) {
            tracker.last_sequence_num = sequence;
            return true;
        }
    }
    return false; // Replay or stale sequence
}

bool CommandManager::parseFrame(const uint8_t* frame_data, size_t frame_len, RfHeader &out_header,
                                 uint8_t* out_payload, uint8_t &out_payload_len) {
    size_t header_len = sizeof(RfHeader);
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

    // Validate message type and source/target ranges
    if (out_header.message_type < static_cast<uint8_t>(RfMessageType::PING) ||
        out_header.message_type > static_cast<uint8_t>(RfMessageType::FAULT_REPORT)) {
        return false;
    }
    if (out_header.source_node_id > MAX_NODES ||
        (out_header.source_node_id != 0 && out_header.target_node_id != 0)) {
        return false;
    }

    // CRC-16 Check
    size_t crc_check_len = header_len + out_header.payload_len + HMAC_TAG_SIZE;
    uint16_t expected_crc = calculateCrc16(frame_data, crc_check_len);
    uint16_t actual_crc = static_cast<uint16_t>(frame_data[crc_check_len]) |
                         (static_cast<uint16_t>(frame_data[crc_check_len + 1]) << 8);

    if (expected_crc != actual_crc) {
        return false;
    }

    // HMAC-SHA256 Check (Constant Time)
    uint8_t expected_mac[HMAC_TAG_SIZE];
    HmacSha256::calculateTruncated(psk_key_, sizeof(psk_key_), frame_data, header_len + out_header.payload_len, expected_mac);
    const uint8_t* actual_mac = frame_data + header_len + out_header.payload_len;

    if (!constantTimeCompare(expected_mac, actual_mac, HMAC_TAG_SIZE)) {
        return false;
    }

    // Anti-replay Check
    // Source 0 is only accepted by the pure codec for locally generated-frame
    // tests. The incoming-frame dispatcher below rejects it for production RX.
    if (out_header.source_node_id != 0 &&
        !validateAntiReplay(out_header.source_node_id, out_header.boot_session_id, out_header.sequence)) {
        return false;
    }

    out_payload_len = out_header.payload_len;
    if (out_payload_len > 0 && out_payload != nullptr) {
        std::memcpy(out_payload, frame_data + header_len, out_payload_len);
    }

    return true;
}

bool CommandManager::isPending(uint8_t node_id) const {
    if (node_id < 1 || node_id > MAX_NODES) return false;
    return pending_commands_[node_id].active;
}

bool CommandManager::serviceCommandFanout(uint32_t current_time_ms) {
    if (!initialized_ || !isProvisioned() || registry_ == nullptr || transport_ == nullptr) {
        return false;
    }

    bool all_dispatched_successfully = true;
    uint8_t tx_buf[128];

    for (uint8_t node_id = 1; node_id <= MAX_NODES; ++node_id) {
        // Handle pending command retries
        if (pending_commands_[node_id].active) {
            if (current_time_ms - pending_commands_[node_id].last_sent_ms >= RF_RETRY_INTERVAL_MS) {
                if (pending_commands_[node_id].retries < MAX_RF_RETRIES) {
                    uint32_t run_lease_ms = DEFAULT_RUN_LEASE_MS;
                    uint32_t max_on_ms = DEFAULT_MAX_ON_DURATION_MS;
                    getNodeLeasePolicy(node_id, run_lease_ms, max_on_ms);

                    SetPumpPayload p;
                    p.desired_state = static_cast<uint8_t>(pending_commands_[node_id].desired_state);
                    p.run_lease_ms = run_lease_ms;
                    p.max_on_duration_ms = max_on_ms;

                    size_t frame_len = buildFrame(RfMessageType::SET_PUMP, node_id,
                                                  pending_commands_[node_id].command_id,
                                                  reinterpret_cast<const uint8_t*>(&p), sizeof(p),
                                                  tx_buf, sizeof(tx_buf));
                    if (frame_len > 0) {
                        size_t sent_bytes = transport_->send(tx_buf, frame_len);
                        if (sent_bytes == frame_len) {
                            pending_commands_[node_id].retries++;
                            pending_commands_[node_id].last_sent_ms = current_time_ms;
                        } else {
                            all_dispatched_successfully = false;
                        }
                    } else {
                        all_dispatched_successfully = false;
                    }
                } else {
                    // Terminal timeout: mark node FAULT
                    pending_commands_[node_id].active = false;
                    registry_->updateHealthStatus(node_id, NodeHealthStatus::FAULT);
                    all_dispatched_successfully = false;
                }
            }
            continue;
        }

        // Issue new command if desired_state != reported_state
        NodeState state;
        if (!registry_->getNodeState(node_id, state)) continue;
        if (state.group_id == UNASSIGNED_GROUP_ID) continue;

        if (state.desired_state != state.reported_state) {
            uint32_t run_lease_ms = DEFAULT_RUN_LEASE_MS;
            uint32_t max_on_ms = DEFAULT_MAX_ON_DURATION_MS;
            getNodeLeasePolicy(node_id, run_lease_ms, max_on_ms);

            SetPumpPayload p;
            p.desired_state = static_cast<uint8_t>(state.desired_state);
            p.run_lease_ms = run_lease_ms;
            p.max_on_duration_ms = max_on_ms;

            uint32_t cid = next_command_id_++;
            size_t frame_len = buildFrame(RfMessageType::SET_PUMP, node_id, cid,
                                          reinterpret_cast<const uint8_t*>(&p), sizeof(p),
                                          tx_buf, sizeof(tx_buf));
            if (frame_len > 0) {
                size_t sent_bytes = transport_->send(tx_buf, frame_len);
                if (sent_bytes == frame_len) {
                    pending_commands_[node_id].active = true;
                    pending_commands_[node_id].command_id = cid;
                    pending_commands_[node_id].target_node_id = node_id;
                    pending_commands_[node_id].desired_state = state.desired_state;
                    pending_commands_[node_id].sequence = sequence_num_ - 1;
                    pending_commands_[node_id].retries = 1;
                    pending_commands_[node_id].last_sent_ms = current_time_ms;
                } else {
                    all_dispatched_successfully = false;
                }
            } else {
                all_dispatched_successfully = false;
            }
        }
    }
    return all_dispatched_successfully;
}

bool CommandManager::handleIncomingFrame(const uint8_t* frame, size_t len, uint32_t current_time_ms) {
    if (!initialized_ || !isProvisioned() || registry_ == nullptr) return false;

    RfHeader header;
    uint8_t payload[64];
    uint8_t payload_len = 0;

    if (!parseFrame(frame, len, header, payload, payload_len)) {
        return false;
    }

    uint8_t src_node = header.source_node_id;
    if (src_node == 0 || src_node > MAX_NODES || header.target_node_id != 0) return false;
    RfMessageType type = static_cast<RfMessageType>(header.message_type);

    if (type == RfMessageType::COMMAND_ACK) {
        if (payload_len >= sizeof(CommandAckPayload)) {
            CommandAckPayload ack;
            std::memcpy(&ack, payload, sizeof(CommandAckPayload));
            if (!validateAck(header, ack)) return false;
            PendingCommand& pending = pending_commands_[src_node];
            if (ack.ack_outcome != static_cast<uint8_t>(AckOutcome::SUCCESS)) {
                pending.active = false;
                registry_->updateHealthStatus(src_node, NodeHealthStatus::FAULT);
                return true;
            }
            pending.active = false;
            const NodePumpState rep = ack.reported_pump_state == 1 ? NodePumpState::ON : NodePumpState::OFF;
            registry_->updateTelemetry(src_node, rep, ack.driver_feedback, 0, 0, current_time_ms);
            return true;
        }
    } else if (type == RfMessageType::TELEMETRY) {
        if (payload_len >= sizeof(TelemetryPayload)) {
            TelemetryPayload telem;
            std::memcpy(&telem, payload, sizeof(TelemetryPayload));
            if (!isBinaryState(telem.reported_pump_state) || !isBinaryState(telem.driver_feedback)) return false;
            NodePumpState rep = (telem.reported_pump_state == 1) ? NodePumpState::ON : NodePumpState::OFF;

            registry_->updateTelemetry(src_node, rep, telem.driver_feedback,
                                       telem.flow_lpm_x100, telem.delivered_volume_ml, current_time_ms);
            return true;
        }
    } else if (type == RfMessageType::FAULT_REPORT) {
        if (payload_len >= sizeof(FaultReportPayload)) {
            FaultReportPayload fault;
            std::memcpy(&fault, payload, sizeof(FaultReportPayload));
            registry_->updateHealthStatus(src_node, NodeHealthStatus::FAULT);
            return true;
        }
    }
    return false;
}

bool CommandManager::validateAck(const RfHeader& header, const CommandAckPayload& ack) const {
    if (header.source_node_id == 0 || header.source_node_id > MAX_NODES || header.target_node_id != 0 ||
        !isAckOutcome(ack.ack_outcome) || !isBinaryState(ack.reported_pump_state) ||
        !isBinaryState(ack.driver_feedback)) return false;
    const PendingCommand& pending = pending_commands_[header.source_node_id];
    return pending.active && pending.target_node_id == header.source_node_id &&
           pending.command_id == header.command_id && pending.sequence == ack.ack_sequence;
}
