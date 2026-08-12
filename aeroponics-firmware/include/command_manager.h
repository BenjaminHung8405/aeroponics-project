#pragma once

#include <cstdint>
#include <cstddef>
#include "config.h"
#include "core/IRfTransport.h"
#include "node_registry.h"

constexpr uint8_t RF_SOF_BYTE_1 = 0xAA;
constexpr uint8_t RF_SOF_BYTE_2 = 0x55;
constexpr uint8_t RF_PROTOCOL_VERSION = 0x01;

enum class RfMessageType : uint8_t {
    PING        = 0x01,
    PONG        = 0x02,
    SET_PUMP    = 0x03,
    COMMAND_ACK = 0x04,
    TELEMETRY   = 0x05,
    HEARTBEAT   = 0x06
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
#pragma pack(pop)

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

struct NodeLeasePolicy {
    uint32_t run_lease_ms = DEFAULT_RUN_LEASE_MS;
    uint32_t max_on_duration_ms = DEFAULT_MAX_ON_DURATION_MS;

    NodeLeasePolicy() = default;
    NodeLeasePolicy(uint32_t lease, uint32_t max_on)
        : run_lease_ms(lease), max_on_duration_ms(max_on) {}
};

/**
 * @brief Command Manager responsible for RF frame encoding, CRC-16 calculation, command dispatching and response handling.
 */
class CommandManager {
public:
    CommandManager();
    ~CommandManager();

    bool begin(NodeRegistry* registry, IRfTransport* transport);

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
     * @brief Build a complete RF frame including header, payload, and trailing CRC-16.
     * @return Total frame length in bytes, or 0 on error.
     */
    size_t buildFrame(RfMessageType msg_type, uint8_t target_node_id, uint32_t command_id,
                      const uint8_t* payload, uint8_t payload_len, uint8_t* out_buffer, size_t buffer_size);

    /**
     * @brief Parse raw byte buffer into header and payload after validating SOF, version, and CRC-16.
     */
    bool parseFrame(const uint8_t* frame_data, size_t frame_len, RfHeader &out_header,
                    uint8_t* out_payload, uint8_t &out_payload_len);

    /**
     * @brief Scan NodeRegistry for desired != reported states and dispatch SET_PUMP commands via IRfTransport.
     * Verifies that transport_->send() transmits the full frame; returns false if TX frame transmission is incomplete.
     */
    bool serviceCommandFanout(uint32_t current_time_ms);

    /**
     * @brief Process incoming frame received via RF transport.
     */
    bool handleIncomingFrame(const uint8_t* frame, size_t len, uint32_t current_time_ms);

private:
    NodeRegistry* registry_;
    IRfTransport* transport_;
    uint16_t boot_session_id_;
    uint16_t sequence_num_;
    uint32_t next_command_id_;
    bool initialized_;
    NodeLeasePolicy node_policies_[MAX_NODES + 1];
};
