#pragma once

#include <cstddef>
#include <cstdint>
#include "config.h"
#include "rf_frame_codec.h"
#include "core/IRfTransport.h"

/**
 * @brief Audit sink interface for node-side safety events.
 */
class INodeAuditSink {
public:
    virtual ~INodeAuditSink() = default;
    virtual void logSafetyEvent(const char* event, const char* reason) = 0;
};

/**
 * @brief Hardware abstraction interface for node pump actuator and feedback sensing.
 */
class IPumpActuatorDriver {
public:
    virtual ~IPumpActuatorDriver() = default;
    virtual void setPumpOutput(bool level) = 0;
    virtual bool readDriverSense() = 0;
    virtual uint16_t readFlowLpmX100() { return 0; }
    virtual uint32_t readDeliveredVolumeMl() { return 0; }
    virtual uint32_t readPulseCount() { return 0; }
};

/**
 * @brief Default simple GPIO / mock actuator driver.
 */
class SimplePumpActuatorDriver : public IPumpActuatorDriver {
public:
    SimplePumpActuatorDriver() : output_level_(false), sense_level_(false) {}
    ~SimplePumpActuatorDriver() override = default;

    void setPumpOutput(bool level) override {
        output_level_ = level;
        sense_level_ = level; // Default driver feedback follows output
    }
    bool readDriverSense() override { return sense_level_; }
    void setSenseLevel(bool level) { sense_level_ = level; }

    uint16_t readFlowLpmX100() override { return flow_lpm_x100_; }
    void setFlowLpmX100(uint16_t flow) { flow_lpm_x100_ = flow; }

    uint32_t readDeliveredVolumeMl() override { return volume_ml_; }
    void setDeliveredVolumeMl(uint32_t vol) { volume_ml_ = vol; }

    uint32_t readPulseCount() override { return pulses_; }
    void setPulseCount(uint32_t pulses) { pulses_ = pulses; }

    bool getOutputLevel() const { return output_level_; }

private:
    bool output_level_;
    bool sense_level_;
    uint16_t flow_lpm_x100_ = 0;
    uint32_t volume_ml_ = 0;
    uint32_t pulses_ = 0;
};

/**
 * @brief Node-side Command Processor and Safety Lease Deadman Engine.
 * 
 * Enforces:
 * - Boot-safe pump output forced OFF prior to RF / application initialization.
 * - HMAC-SHA256 authentication and CRC-16 check.
 * - Anti-replay and boot session verification.
 * - Idempotency: duplicate commands receive cached ACK without re-actuation or lease extension.
 * - Node-side lease deadman timer: forces pump OFF and latches LEASE_EXPIRED_SAFE_OFF on timeout.
 * - Fault lockout on latched faults.
 */
class NodeCommandProcessor {
public:
    NodeCommandProcessor();
    ~NodeCommandProcessor();

    /**
     * @brief Initialize the node command processor.
     * Guarantees that the physical pump output is driven LOW immediately upon boot.
     */
    bool begin(uint8_t node_id, IRfTransport* transport, IPumpActuatorDriver* driver,
               const uint8_t* psk, size_t psk_len, uint32_t boot_session_id);

    void setAuditSink(INodeAuditSink* sink) { audit_sink_ = sink; }

    /**
     * @brief Parse and execute an incoming RF frame from the gateway.
     */
    bool processIncomingFrame(const uint8_t* frame_data, size_t frame_len, uint32_t current_time_ms);

    /**
     * @brief Periodic background service task for lease deadman, telemetry, and heartbeats.
     */
    bool service(uint32_t current_time_ms);

    /**
     * @brief Reset latched faults on the node.
     */
    bool resetFault();

    /**
     * @brief Force the node into a fault state.
     */
    void latchFault(uint8_t fault_code, uint32_t current_time_ms, const char* reason = nullptr);

    // Query state
    uint8_t getNodeId() const { return node_id_; }
    uint32_t getBootSessionId() const { return boot_session_id_; }
    uint8_t getReportedPumpState() const { return reported_pump_state_; }
    uint8_t getDriverFeedback() const { return driver_feedback_; }
    bool isLeaseActive() const { return lease_active_; }
    uint32_t getLeaseRemainingMs(uint32_t current_time_ms) const;
    bool isFaultLatched() const { return fault_latched_; }
    uint8_t getFaultCode() const { return fault_code_; }
    uint8_t getFaultFlags() const { return fault_flags_; }
    uint32_t getCurrentCommandId() const { return current_command_id_; }
    uint32_t getLastGatewaySessionId() const { return last_gw_boot_session_id_; }
    uint16_t getLastGatewaySequence() const { return last_gw_sequence_; }

    // Telemetry / frame transmission helpers
    bool sendTelemetry(uint32_t current_time_ms);
    bool sendHeartbeat(uint32_t current_time_ms);
    bool sendFaultReport(uint8_t fault_code, uint32_t current_time_ms, uint32_t command_id);

private:
    uint8_t node_id_;
    IRfTransport* transport_;
    IPumpActuatorDriver* driver_;
    INodeAuditSink* audit_sink_;
    uint32_t boot_session_id_;
    uint16_t tx_sequence_;
    bool initialized_;

    uint8_t psk_key_[16];
    bool psk_valid_;

    // Anti-replay gateway session tracking
    uint32_t last_gw_boot_session_id_;
    uint16_t last_gw_sequence_;
    bool gw_session_initialized_;

    // Idempotency cache for duplicate command retransmissions
    bool cached_ack_valid_;
    uint32_t cached_gw_boot_session_id_;
    uint16_t cached_gw_sequence_;
    uint32_t cached_gw_command_id_;
    CommandAckPayload cached_ack_payload_;

    // Node actuator & lease state
    uint8_t reported_pump_state_;
    uint8_t driver_feedback_;
    bool lease_active_;
    uint32_t lease_start_ms_;
    uint32_t lease_duration_ms_;
    uint32_t max_on_duration_ms_;
    uint32_t current_command_id_;

    // Fault state
    bool fault_latched_;
    uint8_t fault_code_;
    uint8_t fault_flags_;

    // Periodic timing
    uint32_t last_telemetry_ms_;
    uint32_t last_heartbeat_ms_;

    bool transmitFrame(RfMessageType msg_type, uint32_t command_id, const void* payload, size_t payload_len);
    bool handleSetPump(const RfHeader& header, const SetPumpPayload& payload, uint32_t current_time_ms);
    bool handlePing(const RfHeader& header, const PingPayload& payload);
    void forceSafeOff(const char* reason);
};
