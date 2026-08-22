# Sprint 0 & 1 Legacy Replacement & Migration Plan

> **Document Version:** 1.0.2
> **Date:** 2026-08-12
> **Status:** Draft / Review Pending (Awaiting Independent QA Verification)
> **Target Scope:** Track R Remediation (R1–R6) before Sprint 1.5 RF + Flow Proof-of-Concept.

---

## 1. Context & Objectives

During initial development (Sprint 0–1), a 4-channel GPIO direct relay controller prototype (`RelayController`, `ScheduleManager`, `relay_profiles`, `relay_events`) was implemented for initial validation.

Under the updated system architecture (`PROJECT_ALIGNMENT_2026-08-10.md`), direct GPIO relay driving on the ESP32 Gateway is **deprecated and prohibited in production paths**. The Gateway acts exclusively as a **Production RF 433 MHz Gateway** controlling up to **12 wireless nodes** organized into season, treatment, timer group, and node domains.

This document serves as the mandatory **Inventory Mapping, Isolation Sequence, Rollback Strategy, and Acceptance Contract** for replacing the legacy runtime with successor components while retaining core reusable infrastructure primitives (NVS abstraction, RTC manager, FreeRTOS task abstractions, Watchdog wrappers, and TimescaleDB 3-service infrastructure).

---

## 2. Complete Inventory Mapping (Legacy → Successor)

| Category | Legacy Artifact / Symbol | Status in Production | Successor Component | Replacement / Isolation Plan |
|---|---|---|---|---|
| **Firmware Source** | `aeroponics-firmware/src/relay_controller.cpp` | **REMOVED from Production** | RF Actuator Nodes (Sprint 1.5 Track C / Sprint 2) | Isolated to `src/prototype/legacy_relay/relay_controller.cpp`. Compiled only under `native-prototype`. |
| **Firmware Source** | `aeroponics-firmware/src/schedule_manager.cpp` | **REMOVED from Production** | `GroupScheduleManager` & `CommandManager` | Replaced by `group_schedule_manager.cpp` & `command_manager.cpp`. Isolated to `src/prototype/legacy_relay/`. |
| **Firmware Source** | `aeroponics-firmware/src/FreeRTOSTaskRunner.cpp` | **REMOVED from Production** | Event-driven FreeRTOS tasks / RF Task | Isolated to `src/prototype/legacy_relay/FreeRTOSTaskRunner.cpp`. |
| **Firmware Header** | `include/nvs_storage.h` (`RelayProfile` methods) | **CLEANED** | `NvsStorage` (Generic NVS API) + `LegacyRelayProfileRepository` | Removed `RelayProfile` and `IProfileRepository` from `NvsStorage`. Prototype adapter handles legacy profiles. |
| **Firmware Header** | `include/config.h` (`TOTAL_RELAYS=4`) | **REPLACED** | `MAX_NODES=12`, `MAX_TIMER_GROUPS=4` | Removed `TOTAL_RELAYS` from production gateway headers. |
| **Firmware Test** | `test/test_legacy_relay.cpp` | **ISOLATED** | `test/test_production/test_production.cpp` | Moved legacy unit tests to `test/test_prototype/test_legacy_relay.cpp`. |
| **Integration Test** | `src/integration/production_mqtt_gate.cpp` | **REWRITTEN** | Genuine Gateway Domain Gate | Stripped all `ScheduleManager`/fake relay headers. Uses `NodeRegistry`, `CommandManager`, `MqttClient`. |
| **MQTT Topics** | `aeroponics/device/{id}/command/relay/{id}/schedule` | **DEPRECATED** | `aeroponics/device/{id}/command/config/assignment`, `node/{id}/override`, `group/{id}/control` | `mqtt_client.cpp` handles new gateway domain topics with full-match topic parsing and mandatory `command_id` tracking. |
| **DB Tables** | `relay_profiles`, `relay_events`, `sensor_readings` | **DEPRECATED** | `seasons`, `treatment_versions`, `timer_groups`, `node_registry`, `sensor_calibrations`, `pump_commands`, `flow_events` | Fresh production schema (`schema.sql`) and idempotent migration (`001_production_domain_migration.sql`) create 11 regular tables + 5 hypertables + `pgcrypto`. |
| **Health Script** | `scripts/health-check.sh` | **UPDATED** | Production domain contract check with fail-closed security | Validates 11 production tables, 5 hypertables, `pgcrypto`, calibration/assignment/season constraints, and rejects missing/placeholder secrets. |

---

## 3. Isolation & Removal Sequence

### Phase 1: Build System Isolation (Completed)
1. Updated `platformio.ini`:
   - Production Gateway environment `[env:esp32-s3-devkitc-1]` and default host test environment `[env:native]` exclude `-<integration/>` and `-<prototype/>`.
   - Prototype environment `[env:native-prototype]` isolates legacy tests under `test/test_prototype/`.

### Phase 2: Primitive & Boundary Cleanup (Task R6 Remediation)
1. **NVS Storage Primitive Cleaning:**
   - Stripped `IProfileRepository` inheritance and `RelayProfile` dependencies from `NvsStorage`.
   - Created `LegacyRelayProfileRepository` adapter in `src/prototype/legacy_relay/` to service prototype tests without polluting production primitives.
2. **Gateway Composition Root & Wire Protocol Security:**
   - Wired `NodeRegistry`, `GroupScheduleManager`, `CommandManager`, `MqttClient` into `main.cpp` composition root.
   - Enforces a 16-byte (128-bit) HMAC-SHA256 authentication tag, constant-time verification, NVS-loaded provisioning and anti-replay session/sequence check in `CommandManager`. At-rest RF PSK protection is not asserted: production RF remains fail-closed pending independent security sign-off.
   - Replaced unaligned pointer casting with byte-wise decoding into packed structs.
   - Implemented bounded pending command table with max 3 retries, fixed 1000 ms retry interval, and terminal fault transition.
3. **MQTT Ack Semantics & Topic Validation:**
   - Full-match topic parsing in `mqtt_client.cpp` for assignment, node override, and group control.
   - Mandated non-empty `command_id` and bounds checking on numeric inputs (`node_id` 1..12, `group_id` 1..4).
   - Only return `ACCEPTED` / `COMPLETED` on successful mutation; send explicit NACK with reason on failure/invalid input.

### Phase 3: DB Schema & Backend Verification
1. **Reproducible rehearsal:** `scripts/rehearse_production_migration.sh` creates a disposable TimescaleDB instance, loads `database/rehearsal/legacy_fixture.sql`, runs `001_production_domain_migration.sql`, and asserts the additive migration preserves `relay_profiles`, `relay_events`, and `sensor_readings` unchanged.
### Phase 4: Baseline 2026-08-22 Remediation — 4 MEGA8 Node Schedule Ownership & Gateway Separation (Task R3-M)
1. **Schedule Ownership (Source of Truth on MEGA8 Nodes):**
   - Each of the **04 remote ATmega8 nodes** (`node_id` 1..4) acts as the autonomous scheduler and actuator owner, executing local spray and cooldown cycles independently.
   - The **ESP32-S3 Gateway is NOT a periodic tick scheduler**: it does not generate periodic timer fan-out ticks to drive physical pumps.
2. **Temporary Override & Schedule Resume Semantics:**
   - Gateway `SET_PUMP(OFF)` commands act strictly as **Temporary Overrides** with bounded duration/lease.
   - The autonomous schedule profile stored in MEGA8 memory is **NEVER erased or overwritten** by temporary override commands.
   - Upon override expiration or reaching the resume boundary, the MEGA8 node automatically and deterministically **resumes its local autonomous schedule**.
   - Gateway `SET_PUMP(ON)` commands require an explicit bounded `run_lease_ms`. If the gateway or RF link is lost, the node's independent **Lease Deadman Engine** forces pump Safe-OFF (`LEASE_EXPIRED_SAFE_OFF`) and latches a fault lockout.
3. **Boot-Safe & RF Loss Guarantees:**
   - Physical pump actuator output is driven `LOW` (OFF) immediately at hardware boot before UART/RF stack initialization.
   - Node reboot or RF transport loss will never cause unintentional pump activation.

### Phase 5: Re-validation of MQTT / Command Contract & Normalized Telemetry Baseline 4 MEGA8 (Task R4-M)
1. **Bounded Command DTO Validation:**
   - Inbound MQTT commands for configuration, node override, and group control enforce strict validation on `command_id` (safe string), positive integer `version`, bounded node IDs (`1..4` baseline, up to `12`), bounded group IDs (`1..4`), `desired_state` (`ON`/`OFF`), bounded lease (`run_lease_ms <= 300000`), bounded override duration (`override_duration_ms <= 86400000`), and authorized command sources (`MANUAL_OVERRIDE`, `FAIL_SAFE`, `MANUAL`, `SCHEDULE`).
   - Invalid payloads or topic addresses are rejected fail-closed with retained `REJECTED` admission ACKs on `ack/{command_id}` without mutating node state or dispatching RF frames.
2. **Decoupled MQTT Callback & Non-Blocking Execution:**
   - The MQTT subscriber callback (`_onMessage`) strictly parses, validates envelopes, and queues command DTOs to the thread-safe FIFO queue with pre-reserved ACK capacity.
   - The callback **never directly drives GPIO pins, physical actuators, or blocks on RF communication**.
   - Command dispatch and RF transmission are executed exclusively by the main thread during `serviceIncomingCommands()` and `serviceCommandFanout()`.
3. **Normalized Telemetry & Zero Raw RF Persistence:**
   - Gateway publishes only parsed, structured JSON snapshots (`publishNodeSnapshot`, `publishGroupTelemetry`, `publishHeartbeat`) to telemetry topics.
   - Raw RF wire frames (SOF bytes `0xAA 0x55`, preamble, raw MAC tags, CRC bytes) are never persisted in the database or published as raw telemetry.
   - MQTT topic hierarchy and database schema do not represent the ESP32 gateway as an actuator owner; ESP32 acts solely as an RF telemetry and command gateway.
4. **Admission Lane Reservation & Backpressure Defense:**
   - Dedicated admission ACK lane prevents telemetry bursts from starving command ACKs.
   - Separate backpressure rejection lane ensures client requests always receive an idempotent retained ACK even under buffer saturation.

---

## 4. Rollback Strategy & Risk Mitigation

If a critical regression is discovered in the successor RF Gateway implementation:
1. **Source Safety:** Legacy relay logic is preserved under `src/prototype/legacy_relay/` and verified by `pio test -e native-prototype`.
2. **Build Switch:** Re-enabling `LEGACY_RELAY_SUPPORT` in a target build environment reinstates prototype relay capabilities for hardware rig debugging.
3. **Database Rollback:** Migration 001 is additive; rollback is restore-from-verified-snapshot, documented in `database/001_production_domain_rollback.md`. This avoids deleting post-migration production data.

---

## 5. Verification & Acceptance Criteria (Pending QA Review)

| Criteria ID | Description | Validation Command / Evidence | Status |
|---|---|---|---|
| **VAC-R1-01** | Versioned inventory document exists and maps all legacy components. | File `docs/SPRINT_0_1_LEGACY_REPLACEMENT.md` | IMPLEMENTED — pending independent QA review |
| **VAC-R6-01** | Production Gateway build excludes all legacy relay sources and symbols. | `pio run -e esp32-s3-devkitc-1` | PASS — 2026-08-22 |
| **VAC-R6-02** | Production native test suite passes 100% without legacy headers. | `pio test -e native` | PASS — 123/123, 2026-08-22 |
| **VAC-R6-03** | Legacy prototype test suite passes 100% via prototype adapter. | `pio test -e native-prototype` | PASS — 23/23, 2026-08-22 |
| **VAC-R6-04** | Integration gate verifies gateway domain topics against real Mosquitto broker. | `pio run -e native-integration`; `python3 scripts/mqtt_integration_gate.py` | BUILD PASS — native gate compiled 2026-08-22 |
| **VAC-R6-05** | Grep check (`rg`) confirms zero legacy relay references in production paths. | Source inspection clean | PASS — production paths inspected 2026-08-22 |
| **VAC-R6-06** | Disposable DB migration rehearsal verifies legacy preservation, 11 regular tables, 5 hypertables, partial unique assignment index and season-attribution guard fixture. | `bash scripts/rehearse_production_migration.sh` | PASS — 2026-08-22 |
| **VAC-R3M-01** | MEGA8 autonomous schedule operates as independent Source of Truth. | `pio test -e native` (`test_r3m_node_schedule_autonomous_source_of_truth`) | PASS — 2026-08-22 |
| **VAC-R3M-02** | Temporary OFF override expires and automatically resumes schedule without erasing profile. | `pio test -e native` (`test_r3m_temporary_off_override_expiry_and_schedule_resume`) | PASS — 2026-08-22 |
| **VAC-R3M-03** | Temporary ON override enforces lease deadman and safe-off independently of gateway. | `pio test -e native` (`test_r3m_temporary_on_override_with_lease_deadman_safe_off`) | PASS — 2026-08-22 |
| **VAC-R3M-04** | Node boot and session recovery force actuator LOW and reject stale replays. | `pio test -e native` (`test_r3m_node_reboot_and_rf_loss_fail_safe_guarantee`) | PASS — 2026-08-22 |
| **VAC-R3M-05** | Gateway composition root does not fan-out periodic ticks and respects 4-node boundary. | `pio test -e native` (`test_r3m_gateway_does_not_fanout_periodic_relay_ticks`, `test_r3m_baseline_4_mega8_nodes_boundary_and_registry`) | PASS — 2026-08-22 |
| **VAC-R4M-01** | Bounded command DTO validation rejects out-of-range parameters, invalid source, and topic node overflow. | `pio test -e native` (`test_r4m_mqtt_command_dto_bounded_validation_and_rejection`) | PASS — 2026-08-22 |
| **VAC-R4M-02** | MQTT callback operates asynchronously without GPIO direct control or blocking I/O. | `pio test -e native` (`test_r4m_mqtt_callback_no_gpio_control_and_deferred_execution`) | PASS — 2026-08-22 |
| **VAC-R4M-03** | Temporary override command with source attribution and lease policy passes admission check. | `pio test -e native` (`test_r4m_mqtt_temporary_override_command_with_source_and_lease_policy`) | PASS — 2026-08-22 |
| **VAC-R4M-04** | Telemetry publishes strictly normalized JSON data without raw RF frame persistence. | `pio test -e native` (`test_r4m_normalized_telemetry_no_raw_rf_frame_persistence`) | PASS — 2026-08-22 |
| **VAC-R4M-05** | Admission ACK reservation and backpressure failure FIFO protect command auditability during overload. | `pio test -e native` (`test_r4m_mqtt_backpressure_and_ack_reservation_contract`) | PASS — 2026-08-22 |

---

*Document pending review by independent QA Auditor.*
