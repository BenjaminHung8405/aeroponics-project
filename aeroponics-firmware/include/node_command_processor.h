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
    virtual bool readLoadSense() { return false; }
    virtual uint16_t readCurrentMa() { return 0; }
    virtual uint8_t readPumpFeedbackState() { return 0; }
    virtual bool getOutputLevel() const { return false; }
    virtual uint16_t readFlowLpmX100() { return 0; }
    virtual uint32_t readDeliveredVolumeMl() { return 0; }
    virtual uint32_t readPulseCount() { return 0; }
    // Integer-only hook used by resource-constrained nodes.  Flow is encoded as
    // litres/minute multiplied by 100, matching the RF payload representation.
    virtual void updateFeedbackFixedPoint(uint32_t current_time_ms, uint16_t flow_lpm_x100) {
        (void)current_time_ms;
        (void)flow_lpm_x100;
    }
    virtual bool isActuatorFaultLatched() const { return false; }
    virtual uint8_t getActuatorFaultCode() const { return 0; }
    virtual void resetActuatorFault() {}
};

/**
 * @brief Default simple GPIO / mock actuator driver.
 */
class SimplePumpActuatorDriver : public IPumpActuatorDriver {
public:
    SimplePumpActuatorDriver()
        : output_level_(false),
          sense_level_(false),
          load_sense_level_(false),
          current_ma_(0),
          flow_lpm_x100_(0),
          volume_ml_(0),
          pulses_(0),
          fault_latched_(false),
          fault_code_(0) {}
    ~SimplePumpActuatorDriver() override = default;

    void setPumpOutput(bool level) override {
        if (fault_latched_) {
            output_level_ = false;
            sense_level_ = false;
            load_sense_level_ = false;
            current_ma_ = 0;
            return;
        }
        output_level_ = level;
        sense_level_ = level; // Default driver feedback follows output unless overridden
        load_sense_level_ = level;
        if (level && current_ma_ == 0) {
            current_ma_ = 2000;
        } else if (!level) {
            current_ma_ = 0;
        }
    }
    bool readDriverSense() override { return sense_level_; }
    void setSenseLevel(bool level) { sense_level_ = level; }

    bool readLoadSense() override { return load_sense_level_; }
    void setLoadSenseLevel(bool level) { load_sense_level_ = level; }

    uint16_t readCurrentMa() override { return current_ma_; }
    void setCurrentMa(uint16_t ma) {
        current_ma_ = ma;
        load_sense_level_ = (ma >= 150);
    }

    uint16_t readFlowLpmX100() override { return flow_lpm_x100_; }
    void setFlowLpmX100(uint16_t flow) { flow_lpm_x100_ = flow; }

    uint32_t readDeliveredVolumeMl() override { return volume_ml_; }
    void setDeliveredVolumeMl(uint32_t vol) { volume_ml_ = vol; }

    uint32_t readPulseCount() override { return pulses_; }
    void setPulseCount(uint32_t pulses) { pulses_ = pulses; }

    bool getOutputLevel() const override { return output_level_; }

    bool isActuatorFaultLatched() const override { return fault_latched_; }
    uint8_t getActuatorFaultCode() const override { return fault_code_; }
    void setActuatorFault(uint8_t code) {
        fault_latched_ = (code != 0);
        fault_code_ = code;
        if (fault_latched_) {
            output_level_ = false;
            sense_level_ = false;
            load_sense_level_ = false;
            current_ma_ = 0;
        }
    }
    void resetActuatorFault() override {
        fault_latched_ = false;
        fault_code_ = 0;
    }

private:
    bool output_level_;
    bool sense_level_;
    bool load_sense_level_;
    uint16_t current_ma_;
    uint16_t flow_lpm_x100_;
    uint32_t volume_ml_;
    uint32_t pulses_;
    bool fault_latched_;
    uint8_t fault_code_;
};

enum class NodeSchedulePhase : uint8_t {
    PHASE_SPRAYING     = 0x00,
    PHASE_COOLING_DOWN = 0x01
};

enum class NodeOverrideState : uint8_t {
    NONE         = 0x00,
    OVERRIDE_OFF = 0x01,
    OVERRIDE_ON  = 0x02
};

struct NodeScheduleProfile {
    uint32_t spray_duration_ms    = 30000;   // Default 30s spray
    uint32_t cooldown_duration_ms = 600000;  // Default 10m cooldown
    bool schedule_enabled         = false;
};

/** Persistent node-local schedule boundary (ATmega8 EEPROM or an equivalent NVS adapter). */
class INodeScheduleStorage {
public:
    virtual ~INodeScheduleStorage() = default;
    virtual bool load(uint8_t node_id, NodeScheduleProfile& profile) = 0;
    virtual bool save(uint8_t node_id, const NodeScheduleProfile& profile) = 0;
};

/**
 * @brief Node-side Command Processor and Safety Lease Deadman Engine.
 * 
 * Enforces 2026-08-22 Baseline Architecture:
 * - Autonomous Schedule Source of Truth on MEGA8 node: node runs independent spray/cooldown cycle.
 * - Gateway does NOT act as a periodic schedule ticker or fan-out master.
 * - Temporary Overrides: SET_PUMP(OFF/ON) overrides active schedule temporarily with expiry/lease.
 * - Schedule Resume: Expiry of temporary OFF override automatically resumes autonomous schedule.
 * - Boot-safe pump output forced OFF prior to RF / application initialization.
 * - Safe Reboot & RF Loss: node reboot/RF loss does not auto-resume ON without valid state.
 * - Target Node ID 1..4 (Baseline 4 MEGA8 nodes).
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
               const uint8_t* psk, size_t psk_len, uint32_t boot_session_id,
               INodeScheduleStorage* schedule_storage = nullptr);

    void setAuditSink(INodeAuditSink* sink) { audit_sink_ = sink; }

    /**
     * @brief Configure local autonomous schedule on MEGA8 node (Source of Truth).
     */
    bool configureAutonomousSchedule(uint32_t spray_duration_ms, uint32_t cooldown_duration_ms, bool enabled);

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
    uint8_t getLoadFeedback() const { return driver_ ? (driver_->readLoadSense() ? 1 : 0) : 0; }
    uint16_t getCurrentMa() const { return driver_ ? driver_->readCurrentMa() : 0; }
    uint8_t getPumpFeedbackState() const { return driver_ ? driver_->readPumpFeedbackState() : 0; }
    IPumpActuatorDriver* getActuatorDriver() const { return driver_; }
    bool isLeaseActive() const { return lease_active_; }
    uint32_t getLeaseRemainingMs(uint32_t current_time_ms) const;
    bool isFaultLatched() const { return fault_latched_; }
    uint8_t getFaultCode() const { return fault_code_; }
    uint8_t getFaultFlags() const { return fault_flags_; }
    uint32_t getCurrentCommandId() const { return current_command_id_; }
    uint32_t getLastGatewaySessionId() const { return last_gw_boot_session_id_; }
    uint16_t getLastGatewaySequence() const { return last_gw_sequence_; }

    // Autonomous Schedule & Override Query
    bool isScheduleEnabled() const { return schedule_profile_.schedule_enabled; }
    NodeSchedulePhase getSchedulePhase() const { return current_phase_; }
    NodeOverrideState getOverrideState() const { return override_state_; }
    bool isOverrideActive() const { return override_state_ != NodeOverrideState::NONE; }
    uint32_t getOverrideRemainingMs(uint32_t current_time_ms) const;
    const NodeScheduleProfile& getScheduleProfile() const { return schedule_profile_; }

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

    // Autonomous Schedule & Override State on Node (MEGA8 SSOT)
    NodeScheduleProfile schedule_profile_;
    INodeScheduleStorage* schedule_storage_;
    NodeSchedulePhase current_phase_;
    uint32_t phase_start_ms_;
    bool phase_initialized_;
    NodeOverrideState override_state_;
    uint32_t override_start_ms_;
    uint32_t override_duration_ms_;

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
