# Aeroponics Sprint 1.5 FMEA & Fail-Safe Policy Specification

> **Document Status:** Official Safety Architecture & Failure Mode Analysis
> **Target Scope:** ESP32 RF Gateway & 12 Remote Pump Nodes

---

## 1. Safety Architecture Principles

1. **Fail-Closed Default:** All actuators MUST hardware-default to `OFF` on boot, power loss, brownout, or CPU reset before software initialization.
2. **Autonomous Node Lease:** Nodes run an internal deadman lease timer (`run_lease_ms`). If gateway link drops during irrigation, node forces pump `OFF` independently.
3. **Stale Node Isolation:** If telemetry/heartbeat is missing for $>15\text{ seconds}$, gateway marks node `STALE`, sets `desired_state = OFF`, cancels pending commands, latches fault, and publishes `STALE_SAFE_OFF`.
4. **Explicit Recovery Requirement:** A latched fault or stale state MUST NOT automatically clear or resume irrigation without explicit `resetFault` / operator authorization.

---

## 2. Failure Mode and Effects Analysis (FMEA) Matrix

| Fault Code / Mode | Cause / Trigger | Node Action | Gateway Action | Escalation & Audit | Recovery Semantics |
|---|---|---|---|---|---|
| **FMEA-01: RF Link Lost (Stale)** | No RF frames received from node for $>15\text{s}$. | Auto pump OFF on lease expiry ($\le 500\text{ms}$). | Sets node `STALE`, `desired_state = OFF`, latches fault, cancels pending commands. | Publishes `STALE_SAFE_OFF` audit event to MQTT. | Requires node reconnect + `SET_PUMP(OFF)` enforcement + explicit reset. |
| **FMEA-02: No-Flow Fault** | `SET_PUMP(ON)` executed, but flow rate $<0.5\text{ L/min}$ for $>3\text{s}$. | Immediately turns pump `OFF`, sets `fault_flags |= NO_FLOW`. | Receives `FAULT_REPORT` / telemetry fault, updates registry to `FAULT`. | Publishes `NO_FLOW_FAULT` safety audit. | Requires pipe inspection & explicit `reset_fault` MQTT command. |
| **FMEA-03: Unexpected Flow** | Pump is `OFF`, but flow sensor detects $>0.2\text{ L/min}$. | Keeps driver `OFF`, sets `fault_flags |= UNEXPECTED_FLOW`. | Updates registry to `FAULT`, flags stuck relay/leak. | Publishes `UNEXPECTED_FLOW_FAULT` safety audit. | Requires solenoid valve / driver check & manual reset. |
| **FMEA-04: Driver Mismatch** | Node sets driver high, but driver sense feedback remains LOW. | Immediately turns driver `OFF`. | Sets node state to `FAULT`. | Publishes `HARDWARE_MISMATCH` audit. | Hardware repair required. |
| **FMEA-05: Gateway Reboot** | Gateway power failure or main loop crash. | Continues current lease safely, then auto-taps `OFF`. | Boots, restores boot session from NVS, polls all nodes. | Publishes `GATEWAY_REBOOT` status. | Gateway syncs node states; forces `OFF` if unconfirmed. |
| **FMEA-06: Invalid RTC** | Gateway RTC battery drained or clock un-synced. | Continues independent node safety timers. | Disables all group schedule spraying, forces node `desired_state = OFF`. | Publishes `RTC_INVALID_SAFE_OFF` audit. | Requires NTP sync or manual RTC time set. |
