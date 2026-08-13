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
2. The rehearsal explicitly verifies all four operational event tables have `season_id NOT NULL`, season/node time indexes, and the current group-treatment/node-assignment partial unique indexes.
3. Rollback rehearsal follows the restore-from-snapshot procedure in `database/001_production_domain_rollback.md`; no destructive SQL down migration is permitted.
4. `health-check.sh` validates 11 production tables, 5 hypertables, `pgcrypto`, and the calibration, active-assignment, and event season-attribution constraints.

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
| **VAC-R1-01** | Versioned inventory document exists and maps all legacy components. | File `docs/SPRINT_0_1_LEGACY_REPLACEMENT.md` | DRAFT |
| **VAC-R6-01** | Production Gateway build excludes all legacy relay sources and symbols. | `pio run -e esp32-s3-devkitc-1` | NOT RUN IN THIS REMEDIATION |
| **VAC-R6-02** | Production native test suite passes 100% without legacy headers. | `pio test -e native` | NOT RUN IN THIS REMEDIATION |
| **VAC-R6-03** | Legacy prototype test suite passes 100% via prototype adapter. | `pio test -e native-prototype` | NOT RUN IN THIS REMEDIATION |
| **VAC-R6-04** | Integration gate verifies gateway domain topics against real Mosquitto broker. | `python3 scripts/mqtt_integration_gate.py` | NOT RUN IN THIS REMEDIATION |
| **VAC-R6-05** | Grep check (`rg`) confirms zero legacy relay references in production paths. | Source inspection clean | NOT RUN IN THIS REMEDIATION |
| **VAC-R6-06** | Disposable DB migration rehearsal verifies legacy preservation, 11 regular tables, 5 hypertables, partial unique assignment index and season-attribution guard fixture. | `bash scripts/rehearse_production_migration.sh` | READY FOR QA RUN |

---

*Document pending review by independent QA Auditor.*
