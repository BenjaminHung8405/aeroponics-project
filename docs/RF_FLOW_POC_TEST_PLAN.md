# Aeroponics Sprint 1.5 RF + Flow Proof-of-Concept Test Plan

> **Document Status:** Official Pre-Bench Test Plan & Verification Matrix
> **Scope:** 1 ESP32 RF Gateway ↔ 1 Remote Pump Node (433 MHz Transceiver, Flow Sensor, Actuator)

---

## 1. Test Strategy & Pre-Bench Approval Gate

All acceptance criteria and sample sizes MUST be fixed and approved before bench testing starts. Test results cannot retroactively alter pass/fail thresholds.

### 1.1 Acceptance Thresholds
- **Frame Validation & Auth Rejection:** 100% of malformed CRC, length, version, or bad HMAC tags MUST be rejected without side effects.
- **Idempotency & Duplicate Protection:** Replayed or duplicate `command_id`/sequence MUST NOT actuate the pump twice or alter state.
- **End-to-End Command Success:** `SET_PUMP(ON)` is considered successful ONLY after `RF_ACKED -> PUMP_FEEDBACK_ON -> FLOW_CONFIRMED`.
- **Node Lease Deadman Timeout:** Node pump MUST force OFF within $\le 500\text{ ms}$ after lease expiration if gateway connection is lost.
- **Flow Fault Latching:** `NO_FLOW` or `UNEXPECTED_FLOW` MUST latch node fault and force safe-off within 3 seconds.
- **Stale Link Fail-Safe:** Disconnecting node power/RF for $>15\text{ seconds}$ MUST transition gateway node status to `STALE`, latch fault, force `desired_state = OFF`, and cancel pending commands.

---

## 2. Verification Matrix (Mapped to Sprint 1.5 Gate Requirements)

| Case ID | Feature / Component | Procedure | Expected Outcome | Pass/Fail Criteria |
|---|---|---|---|---|
| **TP-RF-01** | Bounded Framing & Framing Resync | Inject buffer containing 2 valid frames and noise preamble bytes in a single RX tick. | `serviceRfRx()` parses and handles both frames in a single tick. | Both frames processed; no buffer overflow or lost frame. |
| **TP-RF-02** | HMAC-SHA256 & CRC Integrity | Transmit frame with corrupted HMAC byte or corrupted CRC-16. | Frame discarded; no state update or ACK generated. | Zero state mutation; drop counters incremented. |
| **TP-RF-03** | Anti-Replay & Sequence Wrap | Transmit frame with `boot_session_id` lower than current or sequence number already processed. | Frame rejected. | Anti-replay filter logs rejection. |
| **TP-RF-04** | Idempotent Command Processing | Transmit duplicate `SET_PUMP` command with identical `command_id`. | Second command receives duplicate ACK outcome without re-triggering actuator. | Single physical pulse/relay actuation recorded. |
| **TP-SAFE-01**| Node Lease Deadman Timeout | Issue `SET_PUMP(ON)` with 5000 ms lease. Disconnect gateway RF transmitter immediately after ACK. | Node pump auto-stops at lease expiry; logs `LEASE_EXPIRED_SAFE_OFF`. | Node output LOW $\le 500\text{ ms}$ after deadline. |
| **TP-SAFE-02**| Gateway Disconnection & Stale Safe-Off | Power off node while pump is ON. Wait 15 seconds. | Gateway marks node `STALE`, sets `desired_state=OFF`, latches fault, publishes `STALE_SAFE_OFF`. | Audit log published; schedule cannot auto re-ON node. |
| **TP-SAFE-03**| Node Reconnect Recovery | Power node back on after stale evaluation. | Node boots with output OFF. Gateway sends `SET_PUMP(OFF)` to enforce safe-off. | Node remains OFF until explicit fault reset. |
| **TP-FLOW-01**| Flow Confirmation (`FLOW_CONFIRMED`) | Issue `SET_PUMP(ON)`. Water flows at $5.0\text{ L/min}$. | Gateway transitions node state `RF_ACKED -> PUMP_FEEDBACK_ON -> FLOW_CONFIRMED`. | `flow_lpm` between $4.5$ and $5.5\text{ L/min}$. |
| **TP-FLOW-02**| No-Flow Fault Latching | Issue `SET_PUMP(ON)` with dry pipe / closed valve. | Flow fails to reach threshold; node latches `NO_FLOW_FAULT` and forces OFF. | Pump stopped within $3000\text{ ms}$; fault latched. |
| **TP-FLOW-03**| Unexpected Flow Fault | Pump is OFF; inject flow sensor pulses manually. | Node detects flow while OFF, latches `UNEXPECTED_FLOW_FAULT`. | Fault latched; warning published. |
| **TP-HW-01**  | Pump Switching EMI Decoupling | Trigger pump ON/OFF 50 consecutive cycles under full load. | RF transceiver remains connected without UART framing errors or brownouts. | 50/50 cycles succeed without CPU reset or RF packet drop. |
