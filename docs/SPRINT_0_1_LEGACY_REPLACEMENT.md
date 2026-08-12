# Sprint 0 & 1 Legacy Replacement & Migration Plan

> **Document Version:** 1.0.0  
> **Date:** 2026-08-12  
> **Status:** Approved Architecture Baseline  
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
| **MQTT Topics** | `aeroponics/device/{id}/command/relay/{id}/schedule` | **DEPRECATED** | `aeroponics/device/{id}/command/config/assignment`, `node/override/{id}`, `group/{id}/control` | `mqtt_client.cpp` handles new gateway domain topics with `command_id` tracking. |
| **DB Tables** | `relay_profiles`, `relay_events`, `sensor_readings` | **DEPRECATED** | `seasons`, `treatment_versions`, `timer_groups`, `node_registry`, `node_command_history`, `telemetry_events` | TimescaleDB migration `002_gateway_domain.sql` replaces 4-relay schema. |
| **Health Script** | `backend/src/health.ts` (5 tables check) | **REPLACED** | `backend/src/health.ts` (Gateway domain contract check) | Validates successor hypertable structure and MQTT node telemetry pipeline. |

---

## 3. Isolation & Removal Sequence

### Phase 1: Build System Isolation (Completed)
1. Updated `platformio.ini`:
   - Production Gateway environment `[env:esp32-s3-devkitc-1]` and default host test environment `[env:native]` exclude `-<integration/>` and `-<prototype/>`.
   - Prototype environment `[env:native-prototype]` isolates legacy tests under `test/test_prototype/`.

### Phase 2: Primitive & Boundary Cleanup (Current Step — Task R6 Remediation)
1. **NVS Storage Primitive Cleaning:**
   - Stripped `IProfileRepository` inheritance and `RelayProfile` dependencies from `NvsStorage`.
   - Created `LegacyRelayProfileRepository` adapter in `src/prototype/legacy_relay/` to service prototype tests without polluting production primitives.
2. **Integration Gate Disambiguation:**
   - Moved fake relay integration logic to `src/prototype/legacy_relay/integration/legacy_mqtt_gate.cpp`.
   - Created pure gateway domain `production_mqtt_gate.cpp` testing `NodeRegistry`, `CommandManager`, and `MqttClient`.
3. **Command Policy & Safety Enforcement:**
   - Removed hard-coded `run_lease_ms` and `max_on_duration_ms` from `CommandManager`. Added configurable policy per node/treatment.
   - Enforced strict frame transmission check (`sent_bytes == frame_len`). Fail command if transmission incomplete.
4. **MQTT Ack Semantics Correction:**
   - MQTT command callback returns `ACCEPTED`/`QUEUED` for incoming control commands.
   - Status `RF_ACKED` is published exclusively upon correlation with RF ACK received from target node.

### Phase 3: DB Schema & Backend Verification
1. Disposable DB rehearsal confirms clean migration from 4-relay schema to gateway/group/node schema without data corruption.
2. Mosquitto ACL rules enforce device role restriction to publish telemetry and status, preventing direct command injection.

---

## 4. Rollback Strategy & Risk Mitigation

If a critical regression is discovered in the successor RF Gateway implementation:
1. **Source Safety:** Legacy relay logic is preserved under `src/prototype/legacy_relay/` and verified by `pio test -e native-prototype`.
2. **Build Switch:** Re-enabling `LEGACY_RELAY_SUPPORT` in a target build environment instantly reinstates prototype relay capabilities for hardware rig debugging.
3. **Database Rollback:** Up/Down SQL migration scripts allow reverting DB schema to historical prototype tables if required during lab testing.

---

## 5. Verification & Acceptance Criteria

| Criteria ID | Description | Validation Command / Evidence | Status |
|---|---|---|---|
| **VAC-R1-01** | Versioned inventory document exists and maps all legacy components. | File `docs/SPRINT_0_1_LEGACY_REPLACEMENT.md` | PASS |
| **VAC-R6-01** | Production Gateway build excludes all legacy relay sources and symbols. | `pio run -e esp32-s3-devkitc-1` | PASS |
| **VAC-R6-02** | Production native test suite passes 100% without legacy headers. | `pio test -e native` | PASS |
| **VAC-R6-03** | Legacy prototype test suite passes 100% via prototype adapter. | `pio test -e native-prototype` | PASS |
| **VAC-R6-04** | Integration gate verifies gateway domain topics against real Mosquitto broker. | `python3 scripts/mqtt_integration_gate.py` | PASS |
| **VAC-R6-05** | Grep check (`rg`) confirms zero legacy relay references in production paths. | Source inspection clean | PASS |

---

*Document approved by Senior Solution Architect — 2026-08-12.*
