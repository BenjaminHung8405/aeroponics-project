# State Machine Matrix: Hybrid Gateway Adapter (Option C)

> **Document ID:** `SPEC-FSM-HYBRID-001`  
> **Status:** Normative implementation specification  
> **Version:** 1.0.0  
> **Scope:** 1 ESP32 Gateway, 4 ATmega8 legacy field nodes, MQTT control plane  
> **Related:** [`RF_PROTOCOL.md`](RF_PROTOCOL.md), [`RF_FLOW_POC_FMEA.md`](RF_FLOW_POC_FMEA.md), [`RF_FLOW_POC_PUMP_FEEDBACK.md`](RF_FLOW_POC_PUMP_FEEDBACK.md)

## 1. Purpose and Scope

This document is the state-machine authority for the approved **Option C - Hybrid Protocol Bridge**. It defines the state transitions, evidence pipeline, safety guards, failure fallbacks, and translation boundary between production semantics and the deployed AGU Legacy protocol.

The ESP32 Gateway is a **stateful safety proxy**. It owns the virtual FSM for each node, the in-RAM deadman lease, the 300 ms transaction deadline, up to three attempts, correlation IDs, and the production MQTT contract. The ATmega8 is a **simple actuator**: it executes the six original AGU Legacy control/configuration opcodes plus the deployed `0x0E` runtime RAM-read opcode and returns a byte-level result. It does not persist a production fault latch or production lease in EEPROM.

This document supersedes any earlier assumption that the legacy ATmega8 firmware autonomously owns the production lease or fault latch. Local schedule parameters may be persisted through the explicit EEPROM commands, but safety state is runtime state held by the Gateway and is reconstructed fail-closed after a restart.

## 2. Responsibilities and Authority

| Layer | Owns | Must not own |
|---|---|---|
| Backend/UI | Crop, recipe, irrigation group, authorization, command/correlation ID, production FSM projection | Raw RF bytes or direct relay assumptions |
| ESP32 Gateway | Per-node virtual FSM, lease timer in RAM, serialization, timeout/retry, ACK correlation, feedback evidence, fault latch, safe-off | Business scheduling policy and unauthenticated actuator bypass |
| ATmega8 Legacy Node | Decode AGU frame, relay output, ACK/response, schedule EEPROM read/write, polled RAM telemetry | HMAC, production correlation, persistent production fault latch, lease policy |
| Hardware | 10 kOhm pull-down and relay/MOSFET default LOW | Software recovery from a physical safety hazard |

### 2.1 Production and Legacy Protocols

Production MQTT/RF semantics include `command_id`, `desired_state`, `run_lease_ms`, retries, and authenticated correlation. The AGU Legacy wire path is deliberately smaller:

```text
Production MQTT -> ESP32 virtual FSM -> AGU Legacy frame -> ATmega8 relay
                                      <- one-byte/typed legacy response <-
```

An ACK from the ATmega8 proves only that the legacy transaction was accepted. It does **not** prove driver feedback, current, or flow. The Gateway must not publish `RUNNING` until the evidence pipeline reaches `FLOW_CONFIRMED`.

## 3. State Model

### 3.1 Six Macro States

| Macro state | Meaning | Actuator policy | Entry evidence | Exit events |
|---|---|---|---|---|
| `BOOT_OFF` | Gateway/node boot, resynchronization, or unknown state | Force relay OFF; no ON dispatch | Boot, node reboot, Gateway restart, stale recovery | Valid discovery/health and schedule/override decision |
| `SCHEDULE_SPRAY` | Scheduled irrigation is permitted and active | ON only through the evidence pipeline and active lease | Valid schedule window and pre-flight pass | Spray duration elapsed, fault, stale, override |
| `SCHEDULE_COOLDOWN` | Scheduled pause between sprays | OFF; reject/queue conflicting ON | Spray completed or override lease expired | Next schedule boundary, explicit override, fault |
| `OVERRIDE_RUN` | Manual/authorized temporary ON | ON only while Gateway RAM lease is valid | `OVERRIDE_ON` accepted and flow confirmed | Lease extension, explicit OFF, fault, stale |
| `OVERRIDE_HOLD_OFF` | Manual temporary OFF suppressing scheduled ON | OFF; preserve schedule profile | `OVERRIDE_OFF` accepted | Hold-off expiry, explicit release, fault reset |
| `FAULT_LATCH` | Safety fault or unsafe evidence | Hard safe-off; reject ON | Failed guard, timeout, bad feedback, stale, FMEA fault | Only explicit `FAULT_RESET` after pre-flight pass |

`BOOT_OFF` is the safe synchronization state, not proof that the relay is physically off. The Gateway must issue and verify a legacy `PUMP_OFF` transaction whenever the physical state is uncertain.

### 3.2 Evidence Pipeline Substates

The following substates are per-command evidence, orthogonal to the six macro states:

```text
COMMAND_DISPATCHED
        |
        | valid 0x5A response within 300 ms
        v
RF_ACKNOWLEDGED
        |
        | correlated gate/driver feedback = ON
        v
GATE_FEEDBACK_ON
        |
        | current is in the configured valid load range
        v
CURRENT_DETECTED
        |
        | flow >= min_flow for the configured start timeout
        v
FLOW_CONFIRMED
```

Failure at any stage forces an OFF transaction (where transport is available), records an audit event, and enters `FAULT_LATCH`. `RF_ACKNOWLEDGED` alone is never rendered as `RUNNING`. A stale node, faulted node, or node without current/flow evidence is not running in the UI.

The evidence stage is orthogonal to the macro state. During startup, the macro state remains `SCHEDULE_SPRAY` or `OVERRIDE_RUN` with a pending stage (for example, `OVERRIDE_RUN [PENDING_FLOW]`); `CURRENT_DETECTED` is never a macro state. The state tuple is therefore `(macro_state, evidence_stage)`, such as `(OVERRIDE_RUN, CURRENT_DETECTED)`.

## 4. Transition Matrix

The matrix uses `Guard`, `Action`, and `Fallback` as normative fields. `t_ack = 300 ms`; `N_retry = 3` total attempts, including the first transmission.

| ID / use case | Event and source | Macro transition | Evidence progression | Guard | Action | Fallback on failure |
|---|---|---|---|---|---|
| UC-ACT-01 | `SCHEDULE_WINDOW_OPEN` from scheduler | `SCHEDULE_COOLDOWN` -> `SCHEDULE_SPRAY` | `NONE` -> `COMMAND_DISPATCHED` | Recipe valid; node `ONLINE`; no fault/stale/hold-off; RTC/control policy valid; pre-flight pass | Create correlation ID; dispatch ON with bounded lease | Keep OFF; audit `SCHEDULE_INHIBITED`; remain cooldown or latch if safety data invalid |
| UC-ACT-02 | `OVERRIDE_ON` from Backend/UI | `BOOT_OFF`, `SCHEDULE_COOLDOWN`, `OVERRIDE_HOLD_OFF` -> `OVERRIDE_RUN` | `NONE` -> `COMMAND_DISPATCHED` | Authorized source; target node valid; `1000 <= run_lease_ms <= 300000`; no fault/stale; pre-flight pass | Persist command metadata in RAM; dispatch AGU `PUMP_ON`; start lease only after valid ACK | Reject before RF on bad input; RF loss uses blind safe-off and enters `FAULT_LATCH` |
| UC-ACT-03 | `LEASE_EXTEND` / heartbeat | `OVERRIDE_RUN` -> `OVERRIDE_RUN` | Unchanged; preserve current stage | Same command owner/correlation; node online; new lease bounded; no fault | Atomically replace RAM expiry; do not extend on duplicate/late command | Keep original expiry; audit rejection; safe-off at original expiry |
| UC-ACT-04 | `OVERRIDE_OFF` | `OVERRIDE_RUN`, `SCHEDULE_SPRAY` -> `OVERRIDE_HOLD_OFF` or `SCHEDULE_COOLDOWN` | Any -> `NONE` after OFF request | Correlation authorized; node not in emergency stop | Dispatch AGU `PUMP_OFF`; cancel lease; preserve schedule profile | Retry OFF up to 3 times; if unverified, `FAULT_LATCH` and report `SAFE_OFF_UNCONFIRMED` |
| UC-ACT-05 | `SPRAY_DURATION_ELAPSED` | `SCHEDULE_SPRAY` -> `SCHEDULE_COOLDOWN` | Any -> `NONE` after OFF request | Active command belongs to schedule; no higher-priority override | Dispatch OFF; clear evidence; start cooldown timer | Same OFF failure path as UC-ACT-04 |
| UC-ACT-06 | `FLOW_CONFIRMED` | No macro change: `SCHEDULE_SPRAY` or `OVERRIDE_RUN` -> itself | `CURRENT_DETECTED` -> `FLOW_CONFIRMED` | Correlated command; current and flow thresholds valid; no fault flags | Publish `FLOW_CONFIRMED`; UI may show `RUNNING`; continue lease supervision | Any contradictory evidence causes OFF and `FAULT_LATCH` |
| UC-ACT-07 | `FAULT_DETECTED` / FMEA event | Any active state -> `FAULT_LATCH` | Any -> `NONE` after OFF request | No special permission; safety event is fail-closed | Cancel pending ON; dispatch OFF; record fault code/evidence; publish alarm | If OFF cannot be transmitted, mark actuator state `UNKNOWN`, retain latch, escalate blind safe-off |
| UC-ACT-08 | `FAULT_RESET` | `FAULT_LATCH` -> `BOOT_OFF` | `NONE` after reset; no ON evidence accepted | Explicit operator action; relay commanded OFF; no current; flow <= off-leak threshold; node online or bounded recovery path; no E-stop; valid safety policy; no active lease | Run pre-flight check; clear RAM latch only on pass; issue OFF/readback; resynchronize node | Remain `FAULT_LATCH`; publish `RESET_REJECTED` with failed predicate |
| UC-ACT-09 | `SCHEDULE_WINDOW_CLOSE` | `SCHEDULE_COOLDOWN` -> `SCHEDULE_COOLDOWN` | Unchanged | None | No actuator command; retain schedule | None |
| UC-GW-10 | Gateway boot/reboot or node boot session change | Any state -> `BOOT_OFF` | Any -> `NONE` | Boot session initialized; NVS/session validity check | Invalidate pending correlation; force relay OFF; discover/ping; rebuild state from evidence | Keep all nodes safe-off; mark `STALE`/`FAULT` and require explicit resync |
| UC-GW-11 | `LEASE_EXPIRED` | `OVERRIDE_RUN` -> `SCHEDULE_COOLDOWN` | Any -> `NONE` after OFF request | Monotonic RAM timer expired and no accepted extension; no higher-priority fault | Send exactly one logical AGU `PUMP_OFF`; clear lease; suppress immediate re-ON until cooldown boundary; publish `LEASE_EXPIRED_SAFE_OFF` | Retry OFF up to 3 attempts; if not acknowledged, enter `FAULT_LATCH`; never resume ON automatically |

### 4.1 Required Transition Ordering

For an ON request, the only valid successful order is:

```text
macro state entry
  -> COMMAND_DISPATCHED
  -> RF_ACKNOWLEDGED
  -> GATE_FEEDBACK_ON
  -> CURRENT_DETECTED
  -> FLOW_CONFIRMED
```

The Gateway may publish `ACCEPTED` when the command is queued and `RF_ACKED` only after the AGU response is validated. It may publish `PUMP_FEEDBACK_ON`, `CURRENT_DETECTED`, and `FLOW_CONFIRMED` only from correlated evidence. A command ACK, desired state, or stale telemetry cannot skip a stage.

## 5. Safety Invariants

These invariants are mandatory and override normal scheduling behavior:

1. **Fail-closed hardware:** A 10 kOhm pull-down and initialization order guarantee relay/MOSFET LOW at boot, reset, watchdog, brownout, and application startup. `BOOT_OFF` always begins with a physical safe-off attempt.
2. **Gateway deadman lease:** Every production ON command has a bounded `run_lease_ms`. The lease is held in ESP32 RAM and expires to `OVERRIDE_RUN -> SCHEDULE_COOLDOWN`, never directly to a new ON state. This cooldown boundary prevents water shock and prevents an expired manual command from immediately colliding with the scheduler.
3. **No persistent production fault latch in ATmega8 EEPROM:** `FAULT_LATCH` is Gateway runtime safety state. The ATmega8 may persist only schedule/configuration bytes through `WRITE_EEPROM`; rebooting a legacy node clears no Gateway decision because a reboot causes `BOOT_OFF` and resynchronization.
4. **Pre-flight before `FAULT_RESET`:** Reset requires explicit operator intent plus verified OFF relay/driver, no current, acceptable off-flow, valid policy/calibration, online or controlled recovery, and no E-stop/global stop. No telemetry packet can auto-clear a latch.
5. **No ghost running:** `RUNNING` requires `FLOW_CONFIRMED`. `FAULT`, `STALE`, `BOOT_OFF`, timeout, unverified OFF, or missing evidence must be represented as non-running.
6. **Retry idempotence:** Retries reuse the same logical correlation and are serialized per node. No retry may extend a lease or cause duplicate schedule ownership. A legacy ON retry is permitted only while the original command remains valid; OFF has priority over every pending ON.
7. **Feedback separation:** Commanded state, AGU ACK, gate feedback, current, and flow are independent evidence tiers. An ACK is not driver/current/flow confirmation.

## 6. Protocol Translation Matrix

### 6.1 Production Event to AGU Legacy Frame

AGU frame checksum is the zero-sum byte that makes the complete frame sum equal to zero modulo 256. `Length` is the protocol-defined frame length; implementations must use the codec rather than hand-editing bytes.

| Production event | AGU Legacy opcode | Canonical frame shape | Expected response | Gateway interpretation |
|---|---:|---|---|---|
| `PING` health check | `0x05` | `[0x04, 0x05, EchoNonce, NodeID, Checksum]` | Echo byte | Matching echo = `ONLINE`; after 3 failed attempts = `STALE` and safe-off policy |
| `OVERRIDE_ON`, schedule ON | `0x06` | `[0x03, 0x06, NodeID, Checksum]` | `0x5A` | Transaction ACK only; start/continue evidence pipeline and lease supervision |
| `OVERRIDE_OFF`, lease expiry, safe-off | `0x07` | `[0x03, 0x07, NodeID, Checksum]` | `0x5A` | Actuation request accepted; still verify OFF feedback/current/flow |
| `POLL_TELEMETRY` | `0x0E` | `[0x06, 0x0E, AddrLo, AddrHi, 0x08, NodeID, Checksum]` | 8-byte RAM block plus checksum | Decode opto/gate, current, flow pulse and fault fields for the addressed node; update evidence only from a valid response |
| Discovery / `GET_ID` (read) | `0x0A` | `[0x03, 0x0A, 0x00, Checksum]` | NodeID/GroupID | Claim only validated, unique, allowed IDs; do not actuate |
| Set Node ID (write) | `0x0A` | `[0x04, 0x0A, 0x01, NewNodeID, Checksum]` | `0x5A` | Change identity only in an explicit provisioning flow; re-discover after write |
| Schedule read | `0x08` | `[0x04, 0x08, AddrHi, AddrLo, Checksum]` | One EEPROM byte | Parse bounds; compare with Backend recipe; mismatch is configuration failure |
| Schedule write | `0x09` | `[0x05, 0x09, AddrHi, AddrLo, Value, Checksum]` | `0x5A` | ACK then mandatory read-after-write verification; no success until exact match |

The frame-length byte is the payload length plus checksum (`formatSendComPacket`), so the values above are normative for the deployed codec. The PING frame is five bytes total and therefore carries length `0x04`; `0x0E` uses little-endian address order (`AddrLo`, then `AddrHi`), requests eight bytes, and terminates its payload with the addressed `NodeID`. The deployed codec must not hardcode `0x01` when polling nodes 2..4. A production command must never bypass codec validation. The ATmega8 legacy node is a slave and does not unsolicited-push telemetry; the Gateway must poll `0x0E`.

`0x08 READ_EEPROM` and `0x09 WRITE_EEPROM` have no node-address field in the Delphi legacy frame. They are therefore **not node-selective** at the wire layer and must be treated as non-addressed/broadcast-risk configuration operations: serialize the transaction, quiesce unrelated RF traffic, collect responses defensively, and verify each intended node separately with addressed `0x0E` polling (or a deployment-proven node-specific procedure). A single `0x5A` is not proof that every intended node was updated.

### 6.2 Legacy Response to Production Event

| Legacy response | Valid context | Production event/state |
|---|---|---|
| Matching PING echo | Outstanding `0x05` transaction | `NODE_ONLINE`; update `last_seen`, do not infer pump state |
| `0x5A` | Outstanding `0x06`, `0x07`, or `0x09` | `RF_ACKNOWLEDGED` for ON/OFF; configuration ACK for write; never `FLOW_CONFIRMED` |
| Node ID payload | Outstanding `0x0A` | Discovery result; claim only after allow-list and collision checks |
| 8-byte RAM block plus zero-sum byte | Outstanding addressed `0x0E` | Decode only after checksum validation and NodeID correlation; update gate/current/pulse evidence and evaluate the current macro/evidence tuple |
| Timeout, malformed byte, wrong echo/ACK | Any transaction | Retry until `N_retry`; terminal result `RF_TIMEOUT_OR_NACK`; safe-off and `FAULT_LATCH` for an active ON or unverified OFF |

## 7. Gateway Timing and Retry Contract

| Parameter | Normative value | Behavior |
|---|---:|---|
| AGU response deadline | `300 ms` | Start at frame transmission; late response belongs to no active transaction |
| Maximum attempts | `3` | Initial send plus at most two retransmissions; serialize per node |
| Flow settle window (`T_flow_settle`) | `2500 ms` | Hydraulic pressure stabilization after ON; do not classify zero flow as `NO_FLOW` during this window |
| Minimum cooldown (`T_cooldown_min`) | `60 s` | Force relay LOW after lease expiry or fault; block automatic re-ON during dwell time |
| RF polling cadence (`T_poll`) | `1000 ms` | One serialized `0x0E` runtime telemetry poll cadence; do not overlap transactions on the half-duplex bus |
| Lease storage | ESP32 RAM | Monotonic timer; no lease extension from duplicate messages |
| Lease expiry action | Exactly one logical OFF | `OVERRIDE_RUN -> SCHEDULE_COOLDOWN`; retry transport as needed |
| Node stale threshold | Project RF contract | Mark non-running, cancel pending ON, force safe-off; no ghost RUNNING |
| EEPROM read/write addressing | No NodeID in `0x08`/`0x09` | Serialize as non-addressed/broadcast-risk operations; never infer per-node success from one ACK |
| EEPROM write verification | Read-after-write | A write ACK alone is not configuration success; verify each target node sequentially |

Late ACKs and responses from an old correlation are discarded. A Gateway reboot, node identity/session change, or stale transition invalidates all old evidence and requires explicit OFF/resynchronization.

## 8. Guard and Fallback Rules by Failure Class

| Failure class | Detection | Immediate action | Recovery |
|---|---|---|---|
| RF timeout/NACK | No valid response within 300 ms after 3 attempts | Cancel ON evidence; issue/confirm OFF; latch active node | Health check, inspect RF path, explicit reset after pre-flight |
| Driver mismatch | Gate feedback differs from requested state | OFF and latch | Physical repair, verified OFF/current, explicit reset |
| No current/open load/stall | Current outside configured operating envelope | OFF and latch; do not retry ON | Physical inspection and explicit reset |
| No flow/stale sensor/over-range flow | Flow threshold or pulse timeout violation | OFF and latch; escalate node-only or group-stop per FMEA | Correct hydraulic/sensor cause, pre-flight, explicit reset |
| Unexpected flow while OFF | Flow exceeds off-leak threshold after settling | Keep OFF and latch; escalate flooding hazard | Isolate branch, inspect valve/plumbing, explicit reset |
| Node/Gateway reboot | Boot/session change or unknown actuator state | `BOOT_OFF`, invalidate correlations, explicit OFF transaction | Discovery/ping, evidence rebuild |
| Global E-stop/common supply | Hardware interlock or common hazard | Group safe-off; block all automatic ON | Release E-stop, inspect system, controlled reset |

Localized faults stop only the affected node. Common supply, reservoir, E-stop, or multi-node hydraulic hazards may trigger group-stop as defined in the FMEA. The Gateway must publish the selected scope and reason; the UI must not display healthy nodes as faulted when only one node is isolated.

### 8.1 Blind Safe-Off When RF Is Lost

After all three `0x07` attempts time out, any additional OFF transmission is **best-effort blind safe-off**, not evidence that the relay is OFF. The Gateway must:

1. Mark the physical actuator state `UNKNOWN`, never `OFF` or `RUNNING`.
2. Keep the node in `FAULT_LATCH` (or system `GROUP-STOP` when the hazard scope requires it), cancel every pending ON, and block automatic re-ON for `T_cooldown_min`.
3. Record `SAFE_OFF_UNCONFIRMED` with the failed correlation and last valid telemetry timestamp.
4. Rely on the ATmega8 hardware pull-down/default LOW and any separately implemented local deadman behavior as the remaining safety boundary. The Gateway must not claim that this legacy node provides a persistent production lease unless that behavior is independently verified in the deployed firmware.
5. Require a successful addressed `PING`/`0x0E` poll and the `FAULT_RESET` pre-flight before returning the node to service.

## 9. MQTT and Audit Projection

The Gateway publishes state changes with `node_id`, `command_id`, macro state, evidence substate, `fault_code`, `reason`, and gateway/node timestamps. At minimum, implementations must distinguish:

```text
ACCEPTED -> RF_ACKED -> GATE_FEEDBACK_ON -> CURRENT_DETECTED -> FLOW_CONFIRMED
REJECTED | RF_TIMEOUT_OR_NACK | SAFE_OFF_UNCONFIRMED | FAULT_LATCHED
```

Raw AGU frames are transient transport data and must not be stored as business telemetry. Audit records must retain the decoded opcode/result and safety decision, including whether fallback was node-only or group-stop.

## 10. Acceptance Checklist

- [ ] All UC-ACT-01 through UC-ACT-09 and UC-GW-10/11 transitions implement the listed guard, action, and fallback.
- [ ] `OVERRIDE_ON` cannot become `RUNNING` before `FLOW_CONFIRMED`.
- [ ] Lease expiry sends safe OFF and enters `SCHEDULE_COOLDOWN`, not immediate schedule spray.
- [ ] `FAULT_RESET` executes pre-flight and cannot be triggered by telemetry alone.
- [ ] No production fault latch or lease is written to ATmega8 EEPROM.
- [ ] ATmega8 boot output is LOW and only the six original legacy opcodes plus deployed `0x0E` telemetry read are accepted.
- [ ] `encodeReadRamBurst` accepts a target `NodeID`; no multi-node telemetry poll uses a hardcoded node address.
- [ ] PING, ACK, `0x0E` telemetry polling, timeout, retry, discovery, EEPROM write, and read-after-write paths are tested.
- [ ] `0x08`/`0x09` non-addressed configuration operations are serialized and their ACKs are never interpreted as per-node completion.
- [ ] Stale, faulted, rebooting, and unverified nodes never appear as `RUNNING`.
- [ ] The 16 FMEA modes map to node-only or group-stop behavior and produce an audit event.
