# Aeroponics Sprint 1.5 RF + Flow Proof-of-Concept Test Plan

> **Document Status:** Official Pre-Bench Test Plan & Verification Matrix (`SPEC-TEST-001`)  
> **Scope:** 1 ESP32 RF Gateway ↔ 1 Remote Pump Node (433 MHz Transceiver, Flow Sensor, Multi-Tier Feedback Actuator)  
> **Aligned with:** `docs/RF_FLOW_POC_PUMP_FEEDBACK.md`, `docs/RF_FLOW_POC_FMEA.md`, `docs/RF_PROTOCOL.md`  

---

## 1. Test Strategy & Pre-Bench Approval Gate

All acceptance criteria and sample sizes MUST be fixed and approved before bench testing starts. Test results cannot retroactively alter pass/fail thresholds.

### 1.1 Acceptance Thresholds
- **Frame Validation & Auth Rejection:** 100% of malformed CRC, length, version, or bad HMAC tags MUST be rejected without side effects.
- **Idempotency & Duplicate Protection:** Replayed or duplicate `command_id`/sequence MUST NOT actuate the pump twice or alter state.
- **End-to-End Command Success:** `SET_PUMP(ON)` is considered successful ONLY after $\text{RF\_ACKED} \longrightarrow \text{DRIVER\_SENSE\_ON} \longrightarrow \text{LOAD\_CURRENT\_CONFIRMED} \longrightarrow \text{FLOW\_CONFIRMED}$.
- **Node Lease Deadman Timeout:** Node pump MUST force OFF within $\le 500\text{ ms}$ after lease expiration if gateway connection is lost.
- **Multi-Tier Fault Latching:** Any driver mismatch, open load, stall overcurrent, stuck relay, dry run, no-flow, or unexpected flow MUST latch node fault and force safe-off.
- **Stale Link Fail-Safe:** Disconnecting node power/RF for $>15\text{ seconds}$ MUST transition gateway node status to `STALE`, latch fault, force `desired_state = OFF`, and cancel pending commands.

---

## 2. Verification Matrix (Mapped to Sprint 1.5 Gate Requirements)

| Case ID | Feature / Component | Procedure | Expected Outcome | Pass/Fail Criteria |
|---|---|---|---|---|
| **TP-RF-01** | Bounded Framing & Framing Resync | Inject buffer containing 2 valid frames and noise preamble bytes in a single RX tick. | `serviceRfRx()` parses and handles both frames in a single tick. | Both frames processed; no buffer overflow or lost frame. |
| **TP-RF-02** | HMAC-SHA256 & CRC Integrity | Transmit frame with corrupted HMAC byte or corrupted CRC-16. | Frame discarded; no state update or ACK generated. | Zero state mutation; drop counters incremented. |
| **TP-RF-03** | Anti-Replay & Sequence Wrap | Transmit frame with `boot_session_id` lower than current or sequence number already processed. | Frame rejected. | Anti-replay filter logs rejection. |
| **TP-RF-04** | Idempotent Command Processing | Transmit duplicate `SET_PUMP` command with identical `command_id`. | Second command receives duplicate ACK outcome without re-triggering actuator. | Single physical pulse/relay actuation recorded. |
| **TP-RF-05** | RF Latency Breakdown & Percentiles | Measure round-trip time across $N=100$ trials; calculate UART, airtime, node proc, and flow confirmation breakdown. | Determine empirical p50, p90, p95, p99; RTT nominal $\le 200\text{ ms}$, total with flow $\le 600\text{ ms}$. | p99 network latency $<350\text{ ms}$; no memory leaks. |
| **TP-RF-06** | Distance & Wet Foliage Attenuation | Transmit 100 frames at LOS (10m, 30m, 50m, 100m) and through wet greenhouse foliage canopy (-18 dB attenuation). | LoRa maintains $\ge 99\%$ PDR; FSK maintains $\ge 90\%$ PDR with bounded backoff retries. | Zero unhandled timeouts; all retries bounded. |
| **TP-RF-07** | Inductive Pump Switching EMI Immunity | Trigger 50 consecutive pump switching cycles under full inductive motor load. | Zero MCU resets, zero UART framing crashes, retries successfully recover corrupted bursts. | 100% command delivery without state desync. |
| **TP-RF-08** | Power-Cycle Reconnect & Session Sync | Power-cycle node during active network operation; measure time to session re-establishment. | Node boots safe-OFF, syncs new `boot_session_id`, and answers `PING/PONG` within $\le 1500\text{ ms}$. | Reconnect $<1500\text{ ms}$ ($\ll 15\text{s}$ stale timeout). |
| **TP-PROTO-01**| CRC-16/CCITT-FALSE Standard Vector | Verify CRC calculation against standard ASCII `"123456789"` test vector. | CRC-16 value equals `0x29B1` (CCITT-FALSE). | 100% deterministic bit accuracy. |
| **TP-PROTO-02**| Header & Payload Serialization Boundaries | Encode/decode all 7 message types (`PING`, `PONG`, `SET_PUMP`, `ACK`, `TELEMETRY`, `HEARTBEAT`, `FAULT_REPORT`) and boundary fields. | All fields encoded in Little-Endian format and round-trip decoded cleanly; bad lengths fail-closed. | 100% field bit accuracy, zero buffer overflow. |
| **TP-PROTO-03**| Node ID & Message Type Validation | Encode frames with invalid source/target IDs ($>12$), identical source/target ($0=0, 2=2$), or invalid enum types. | Codec returns 0 / false fail-closed before signing or transmission. | Zero illegal frames generated or admitted. |
| **TP-PROTO-04**| Malformed Frame Fuzzing & Bit Flips | Inject single-bit flips across every byte of valid frame and test all truncated lengths ($0 \le L < L_{\text{full}}$). | `decodeFrame()` rejects 100% of mutated and truncated frames without crashing or memory corruption. | 100% fail-closed rejection rate. |
| **TP-PROTO-05**| Monotonic Sequence Distance & Wrap Math | Test distance calculation $(new\_seq - last\_seq) \pmod{65536}$ across $65535 \to 0$ wrap and duplicate/replay windows. | Distances $1..32767$ accepted; $0$ (duplicate) and $>32767$ (replay) rejected. | Zero replayed frames admitted. |
| **TP-SAFE-01**| Node Lease Deadman Timeout | Issue `SET_PUMP(ON)` with 5000 ms lease. Disconnect gateway RF transmitter immediately after ACK. | Node pump auto-stops at lease expiry; logs `LEASE_EXPIRED_SAFE_OFF`. | Node output LOW $\le 500\text{ ms}$ after deadline. |
| **TP-SAFE-02**| Gateway Disconnection & Stale Safe-Off | Power off node while pump is ON. Wait 15 seconds. | Gateway marks node `STALE`, sets `desired_state=OFF`, latches fault, publishes `STALE_SAFE_OFF`. | Audit log published; schedule cannot auto re-ON node. |
| **TP-SAFE-03**| Node Reconnect Recovery | Power node back on after stale evaluation. | Node boots with output OFF. Gateway sends `SET_PUMP(OFF)` to enforce safe-off. | Node remains OFF until explicit fault reset. |
| **TP-FEEDBACK-01**| Driver Sense Mismatch Detection | Command ON/OFF while forcing driver sense pin to opposite logic for $>30\text{ms}$. | Node detects mismatch, forces safe-off, and latches `FEEDBACK_FAULT_DRIVER_MISMATCH`. | Fault latched within $\le 30\text{ms}$; output disabled. |
| **TP-FEEDBACK-02**| Inrush Blanking & Overcurrent Stall Trip | Inject $6.0\text{A}$ spike for $40\text{ms}$ (inrush) $\to$ verify no trip; then inject $4.5\text{A}$ for $>50\text{ms}$ at $t=100\text{ms}$. | Inrush spike is masked; sustained stall trips `FEEDBACK_FAULT_OVERCURRENT_STALL` in $\le 50\text{ms}$. | Safe-off triggered; stall fault latched. |
| **TP-FEEDBACK-03**| Open Load / Broken Wire Detection | Command ON with motor disconnected ($I < 150\text{mA}$) for $>150\text{ms}$. | Node detects open load, transitions to safe-off, and latches `FEEDBACK_FAULT_OPEN_LOAD`. | Fault latched at $t \ge 150\text{ms}$. |
| **TP-FEEDBACK-04**| Stuck-ON Relay / Shorted FET Detection | Command OFF while load current continues flowing ($I > 50\text{mA}$) for $>150\text{ms}$. | Node detects current during OFF state, latches `FEEDBACK_FAULT_STUCK_ON`. | Critical alarm latched; audit logged. |
| **TP-FEEDBACK-05**| Dry Run / No-Water Differentiation | Command ON with dry pump ($I = 800\text{mA}$, Flow $<0.5\text{L/min}$) for $>3000\text{ms}$. | Node distinguishes dry run from clogged nozzle, latches `FEEDBACK_FAULT_DRY_RUN`. | Correct `DRY_RUN` fault code assigned. |
| **TP-FEEDBACK-06**| Over-Range Flow Detection | Inject flow pulses equivalent to $7.2\text{ L/min}$ ($>6.5\text{ L/min}$). | Node detects pipe burst/sensor error, latches `FEEDBACK_FAULT_OVER_RANGE_FLOW`. | Safe-off engaged immediately. |
| **TP-FEEDBACK-07**| Fault Latching & Explicit Reset Integrity | Simulate intermittent telemetry recovery after fault latching without sending reset. | Node remains strictly in `FAULT_LATCHED` state until explicit `resetFault()` API call. | Zero automated re-actuation permitted. |
| **TP-FLOW-01**| Flow Confirmation (`FLOW_CONFIRMED`) | Issue `SET_PUMP(ON)`. Water flows at $5.0\text{ L/min}$. | Gateway transitions node state `RF_ACKED -> PUMP_FEEDBACK_ON -> FLOW_CONFIRMED`. | `flow_lpm` between $4.5$ and $5.5\text{ L/min}$. |
| **TP-FLOW-02**| No-Flow Fault Latching | Issue `SET_PUMP(ON)` with dry pipe / closed valve. | Flow fails to reach threshold; node latches `NO_FLOW_FAULT` and forces OFF. | Pump stopped within $3000\text{ ms}$; fault latched. |
| **TP-FLOW-03**| Unexpected Flow Fault | Pump is OFF; inject flow sensor pulses manually. | Node detects flow while OFF, latches `UNEXPECTED_FLOW_FAULT`. | Fault latched; warning published. |
| **TP-HW-01**  | Pump Switching EMI Decoupling | Trigger pump ON/OFF 50 consecutive cycles under full load. | RF transceiver remains connected without UART framing errors or brownouts. | 50/50 cycles succeed without CPU reset or RF packet drop. |
| **TP-CAL-01** | Multi-Point Calibration Traceability | Run 5-point flow calibration ($0.35, 1.2, 2.5, 4.0, 5.5\text{ L/min}$) with $\ge 5$ trials per point against Class A reference vessel. | Generate versioned calibration profile with monotonic frequencies and valid CRC32. | Repeatability $E_{\text{rep}} \le 1.0\%$, Post-cal accuracy $E_{\text{acc}} \le \pm 1.5\%$. |
| **TP-CAL-02** | Piecewise Interpolation Accuracy | Inject pulse frequencies between calibration brackets (e.g. $56.5\text{ Hz}$). | Engine calculates interpolated $K$-factor proportionally. | Interpolated $K$-factor within $\pm 2$ pulses/L of theoretical linear interpolation. |
| **TP-CAL-03** | Low Flow Cutoff & Zero-Leak Safety | Inject 1 pulse per 2000 ms ($< 0.20\text{ L/min}$). | Engine clamps flow rate to $0.00\text{ L/min}$ (`FLOW_ZERO_OR_CUTOFF`). | Zero ghost volume or false flow confirmation. |
| **TP-CAL-04** | Density & Temperature Compensation | Calculate water density from $15.0^\circ\text{C}$ to $35.0^\circ\text{C}$. | Density adheres to Tanaka approximation equation. | Computed density within $\pm 0.05\%$ of reference table. |
| **TP-CAL-05** | Safety Isolation & E-Stop Bench Test | Trigger low water float switch and press mechanical E-Stop during active pump run. | Motor power isolated in $\le 30\text{ms}$; secondary containment tray remains dry. | $100\%$ power cutoff without MCU brownout or fire hazard. |

---
*Senior Solution Architect — Kế hoạch kiểm thử mở rộng hoàn tất ngày 2026-08-17.*
