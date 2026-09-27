# Execution Master Plan: ESP32 RF CRC-16 Migration

> **Author:** Senior IIoT Developer (10+ years embedded RF/industrial control)
> **Created:** 2026-09-26
> **Status:** ACTIVE — Sprint 2 QA Review in progress
> **Scope:** Full lifecycle from baseline remediation through field rollout

---

## 0. Executive Summary

This plan migrates the entire ESP32 ↔ ATmega8 433 MHz RF chain from **CRC-16/CCITT-FALSE** (poly `0x1021`, MSB-first) to **CRC-16/MODBUS** (poly `0xA001`, LSB-first), then expands the RF address space from 4 production nodes (`4..7`) to 15 nodes (`0x1..0xF`) with 4 group addresses (`0x10/0x14/0x18/0x1C`), and finally hardens the deployment for production field rollout.

**Why:** The legacy ATmega8 firmware uses CRC-16/MODBUS natively (Delphi `TSCI.CalCRC16` / AVR assembly `CalCRC16` / ESP32 reference flow). The gateway legacy codec is now aligned to the same wire algorithm. The modern ESP32 model path independently uses CRC16-Modbus as protocol v2. Treatment/NVS integrity checks remain separate storage contracts.

---

## 1. Factual Baseline (Verified 2026-09-26)

| Item | Value | Source |
|---|---|---|
| `RF_PROTOCOL_VERSION` | `0x02` (already bumped in Sprint 2) | `include/config.h:150` |
| CRC utility (Sprint 1) | `core/Crc16Modbus.*` — 3 functions, 9/9 tests PASS | Sprint 1 WALKTHROUGH_LOG |
| `RfFrameCodec::calculateCrc16` | Delegates to `calculateCrc16Modbus` (Sprint 2 A1) | `src/rf_frame_codec.cpp:66` |
| `treatment_manager` storage CRC | Frozen to CCITT-FALSE via `calculateStorageCrc16` (Sprint 2 C2) | `include/treatment_manager.h:12` |
| `HMAC_TAG_SIZE` | **16 bytes** (truncated SHA-256) | `include/core/hmac_sha256.h:8` |
| `RF_HEADER_SIZE` | 17 bytes | `include/config.h:151` |
| `RF_MAX_FRAME_SIZE` | `17 + 64 + 16 + 2 = 99 bytes` | `include/config.h:154` |
| `RF_MAX_NODE_ID` | 12 (production 4..7) | `include/config.h:119,122-123` |
| Legacy AGU codec | CRC16-Modbus `[len][payload][crc_lo][crc_hi]` | `src/agu_legacy_codec.cpp` |
| `test_crc16` | 9/9 PASS | Sprint 1 |
| `test_fsm` | 21/21 PASS | Baseline stable |
| `test_production` | 277 RUN_TEST, **97 failures + SIGSEGV pre-existing** | Baseline at `41feec6` |
| `test_prototype` | 23 tests (relay logic, no CRC tests) | `test/test_prototype/test_legacy_relay.cpp` |
| Bare `pio test -e native` | **0 test cases collected** (comma-as-glob bug) | `platformio.ini:50` |
| ESP32 gateway build | **PASS** (after UART ISR fix in Sprint 2 E2) | Sprint 2 WALKTHROUGH_LOG |
| ATmega8 node build | PASS, flash 6436/7000, RAM 301/900 | Sprint 2 E2 |

### 1.1 Known Defects (Must Address Before Release)

| ID | Severity | Description | Target Phase |
|---|---|---|---|
| DEF-01 | **Critical** | `test_filter` comma parsed as single glob — bare `pio test -e native` collects 0 tests; every release gate using bare command is meaningless | Sprint 0 |
| DEF-02 | **High** | `test_production` 97 failures + SIGSEGV at test C4 — blocks any "all tests PASS" release gate | Sprint 0 |
| DEF-03 | **Medium** | `HMAC_TAG_SIZE` misreported as 12 in plan docs (actually 16) — correction in this master plan | Corrected here |
| DEF-04 | **High** | `RF_PROTOCOL.md` §1/§2 still documents CCITT-FALSE and HMAC as deployed contract — misleading for field engineers | Sprint 5 |
| DEF-05 | **Low** | `docs/interface-wire-contract.md` checksum section references legacy zero-sum | Sprint 5 |

---

## 2. Sprint Dependency Graph

```
Phase 0 (Baseline)
    │
    ├── Sprint 0: Baseline Remediation ─────────────────────┐
    │   Fix test_filter, triage test_production failures,   │
    │   tag pre-crc16-modbus, capture evidence              │
    │                                                        │
    ├── Sprint 1: CRC16-Modbus Utility ───── [DONE, QA] ──►│
    │                                                        │
    ├── Sprint 2: RfFrameCodec Migration ── [QA Review] ──►│
    │                                                        │
    ├── Sprint 3A: Legacy AGU Characterization ─────────────┤
    │   Evidence freeze, wire capture, compile-time switch   │
    │                                                        │
    ├── Sprint 3B: Legacy AGU CRC Migration ────────────────┤
    │   Only after 3A bench evidence + feature flag          │
    │                                                        │
    ├── Sprint 4: Integration Verify + Fuzz + Benchmark ───┤
    │                                                        │
    ├── Sprint 5: Docs + Release Gate + Rollback ──────────┤
    │                                                        │
    └── Sprint 6: Field Rollout + Observability ────────────┘

Track F (Address Expansion) — DEFERRED after Sprint 5
    Sprint F1: Address constants + predicates
    Sprint F2: Codec validation + routing
    Sprint F3: Golden frames + tests
    Sprint F4: Node registry + MQTT mapping
    Sprint F5: Mixed unicast/group integration
```

### 2.1 Execution Order (Mandatory)

```
S0 (parallel with S1-S2) ──► S2-APPROVE ──► S3A ──► S3B ──► S4 ──► S5 ──► S6
                                        │
                                 S1-APPROVE ─┘
```

**Evidence update:** The user supplied the authoritative Delphi `SendComCRC16`/`CalCRC16` flow and matching AVR routine. This establishes the intended legacy wire algorithm and authorizes codec alignment. Physical UART/logic-analyzer captures, ACK/group behavior, and response-size observations remain separate hardware gates and are still required before field release.

---

## 3. Phase-by-Phase Specification

### Phase 0 — Baseline Remediation (`sprint_0.md`)

**Objective:** Make the test harness trustworthy so every subsequent release gate is meaningful.

| Task | Action | Acceptance |
|---|---|---|
| S0-T1 | Fix `platformio.ini` `test_filter` comma bug — change `,` to space-separated list or proper PlatformIO syntax | `pio test -e native` collects **≥300 test cases** (not 0) |
| S0-T2 | Triage `test_production` 97 failures — categorize into: (a) CRC CCITT stale fixtures, (b) version byte hardcoded, (c) SIGSEGV at C4, (d) other | Triage report with classification |
| S0-T3 | Fix trivial stale fixtures attributable to CRC migration (0x29B1 → 0x4B37) — these are "improvements" not regressions | Failure count drops by ≥1 |
| S0-T4 | Fix version byte hardcodes (dòng 405, 407, 8073 in test_production.cpp) | No hardcoded `0x01` or `0x02` in test assertions |
| S0-T5 | Tag `pre-crc16-modbus` on current HEAD | Git tag exists, no code changes |
| S0-T6 | Capture baseline evidence: `pio test -e native`, `pio run -e esp32-s3-devkitc-1`, `pio run -e atmega8-node-4` | `BASELINE_REPORT.md` with pass/fail counts |

**Exit criteria:** Bare `pio test -e native` collects ≥300 tests. Failure count in `test_production` is documented and stable (no new failures from S0 fixes).

---

### Phase 1 — CRC16-Modbus Utility (`sprint_1.md`) ✅ DONE

**Objective:** Pure utility, no codec dependency.

| Deliverable | Status |
|---|---|
| `include/core/Crc16Modbus.h` | ✅ Created |
| `src/core/Crc16Modbus.cpp` | ✅ Created |
| `test/test_crc16/test_crc16.cpp` | ✅ 9/9 PASS |
| `test/test_crc16/test_crc16_runner.c` | ✅ Created |
| `platformio.ini` test_filter update | ✅ `test_crc16` added |
| Quality Gates S1-HARD-01..05 | ✅ Verified (QA Review pending) |

---

### Phase 2 — RfFrameCodec Migration (`sprint_2.md`) — QA Review

**Objective:** Switch gateway + node CRC algorithm on the RF wire.

| Track | Tasks | Status |
|---|---|---|
| A — Data (Codec) | A1: delegate `calculateCrc16` → Modbus; A2: doc | QA Review |
| B — Delegates | B1: PumpNodeController verification; B2: frame-length guard | QA Review |
| C — Version/Persistence | C1: bump `0x02`; C2: isolate treatment_manager storage CRC | QA Review |
| D — Test/QA | D1: golden vectors; D2: integration fixtures + 1-bit flip | QA Review |
| E — Build | E1: build_src_filter; E2: gateway + node + native builds | QA Review |
| F — Address (DEFERRED) | Node/Group address expansion | Deferred |

**Key corrections in this master plan:**
- `HMAC_TAG_SIZE` is **16 bytes**, not 12 as stated in some sprint docs. Frame layout: `[header(17)][payload(0..64)][HMAC(16)][CRC(2)]` = max 99 bytes.
- Wire CRC covers `[header + payload + HMAC(16)]`, NOT including the 2 CRC trailer bytes.

---

### Phase 3A — Legacy AGU Characterization (evidence update — `sprint_3a.md`)

**Objective:** Freeze and document the deployed wire behavior. Algorithm evidence is now supplied by the authoritative Delphi/AVR implementation; physical capture and ACK behavior remain open.

> **WHY THIS PHASE EXISTS:** The ATmega8 node firmware is preloaded, source-unavailable, immutable (`ATMEGA8_INTEGRATION_BOUNDARY.md`). The `agu_legacy_codec.cpp` on ESP32 is the gateway's model of the legacy protocol — but it may NOT match what the deployed node actually sends/receives. Changing the codec without evidence of deployed behavior risks silent command loss.

| Task | Action | Evidence Required |
|---|---|---|
| S3A-T1 | Capture RF wire traces of legacy commands (PUMP_ON, PUMP_OFF, PING, READ_RAM_BURST) using logic analyzer or UART sniffer on actual hardware | Binary captures stored in `docs/legacy_wire_captures/` |
| S3A-T2 | Verify checksum type: authoritative implementation identifies CRC16-Modbus; correlate against captured frames when hardware is available | Source evidence recorded; physical capture pending |
| S3A-T3 | Document ACK behavior: does node respond to group address `$14`? How many ACKs? | Test log with timestamps |
| S3A-T4 | Verify `expectedResponseSize` for each command type against actual node responses | Size validation table |
| S3A-T5 | Align legacy codec to the authoritative CRC16-Modbus flow | Codec vectors and native production tests pass |
| S3A-T6 | Update `sprint_3.md` with evidence-based corrections if wire capture contradicts plan assumptions | Updated sprint doc |

**Exit criteria:** Source-level algorithm evidence recorded, legacy codec vectors pass, and physical captures/ACK evidence are still pending before field acceptance.

**Risk gate:** Source evidence confirms CRC16. The legacy codec has been aligned; do not claim field acceptance until physical capture and response behavior are verified.

---

### Phase 3B — Legacy AGU CRC Migration (`sprint_3.md` adjustments)

**Objective:** Align `agu_legacy_codec.cpp` with the confirmed AGU CRC16-Modbus wire contract.

| Task | Action | Guard |
|---|---|---|
| S3B-T1 | Switch `AGU_LEGACY_CRC_MODE` default to `CRC16_MODBUS` | 3A evidence attached to commit |
| S3B-T2 | Update `formatSendComPacket` → `formatSendComCrc16Packet` | Frame size = `payloadLen + 3` |
| S3B-T3 | Update `decodeBurstRam` to expect 11 bytes `[len][8 data][crc_lo][crc_hi]` | Guard: `inSize < 11 → false` |
| S3B-T4 | Update `agu_legacy_rf_host.cpp` expected response sizes | Per-command size table |
| S3B-T5 | Update `test/test_production/test_production.cpp` legacy codec tests (dòng 10194-10349) | Vector hex updated |
| S3B-T6 | Update `test/test_prototype/test_legacy_relay.cpp` if applicable | Currently no CRC tests — add them |
| S3B-T7 | Feature flag: `AGU_LEGACY_CRC_MODE` in `platformio.ini` build_flags for atmega8-node env | Default OFF for node builds |

**Critical safety rule:** The ATmega8 node build MUST NOT include `agu_legacy_codec.*` (verify via `grep -R "agu_legacy" src/atmega8_node_main.cpp`). Legacy codec is ESP32 gateway-only.

---

### Phase 4 — Integration Verify + Fuzz + Benchmark (`sprint_4.md`)

**Objective:** End-to-end RF chain validation with CRC16-Modbus on both modern and legacy paths.

| Track | Tasks |
|---|---|
| A — PumpNodeController | T1-T3: review buildFrame/parseFrame/handleIncomingFrame CRC flow |
| B — Test/QA | T4-T6: roundtrip chain, 2500-iteration fuzz, fixture sync |
| C — Build/Benchmark | T7-T8: benchmark CRC path, full build gate |

**New requirements for this plan:**
- Fuzz must cover BOTH modern frame (`RfFrameCodec`) AND legacy frame (`AguLegacyCodec`) paths
- Benchmark must run on BOTH ESP32-S3 AND native (ATmega8 timing is different — note in report)
- Add explicit test: legacy frame with CRC mode switch → decode passes in both modes

---

### Phase 5 — Docs + Release Gate + Rollback (`sprint_5.md`)

**Objective:** Documentation alignment, release readiness, rollback safety.

| Track | Tasks | Critical Fix |
|---|---|---|
| A — Docs | T1-T2: wire-contract, QA report | **T0: Update `docs/RF_PROTOCOL.md` §1/§2** (DEF-04) |
| B — Test/QA | T3-T4: cleanup CCITT refs, benchmark log | Zero legacy CRC test vectors |
| C — Release | T5-T6: gate commands, rollback plan | Fix release gate to use filtered `pio test -e native -f test_crc16,test_fsm,test_production` |

**Critical correction to sprint_5.md:** The release gate command `pio test -e native` (S5-T5) is broken until S0-T1 fixes the test_filter. The corrected gate MUST use `-f` filter until DEF-01 is resolved.

---

### Phase 6 — Field Rollout + Observability (NEW — `sprint_6.md`)

**Objective:** Safe production deployment with rollback capability and monitoring.

| Task | Action |
|---|---|
| S6-T1 | OTA deployment strategy: gateway-first, then nodes (version gate blocks mismatched pairs) |
| S6-T2 | Dual-version window: document max time gateway can run v0x02 with nodes on v0x01 (answer: 0 — fail-closed) |
| S6-T3 | Rollback procedure: test on bench, document steps, timing estimate |
| S6-T4 | Post-deploy observability: CRC error rate telemetry, stale threshold alerts |
| S6-T5 | RF link budget verification: measure RSSI, packet error rate in production enclosure |
| S6-T6 | EMC/regulatory: confirm 433 MHz 25mW compliance (Thông tư 08/2021/TT-BTTTT) in deployed environment |
| S6-T7 | FMEA update: CRC change does not affect safety-critical paths (pump off, stale safe-off, lease expiry remain unchanged) |

---

## 4. Cross-Cutting Concerns

### 4.1 RF Address Expansion (Track F — Deferred)

| Gate | Description |
|---|---|
| F-ADR-01 | Node space `0x1..0xF`, gateway `0x0` |
| F-ADR-02 | 4 RF group addresses: `0x10, 0x14, 0x18, 0x1C` |
| F-ADR-03 | Group only in target, never in source |
| F-ADR-04 | Logical group `1..4` unchanged in backend/MQTT |
| F-ADR-05 | CRC covers address bytes — one-bit flip in address → `CRC_MISMATCH` |

**Decision:** Track F is deferred AFTER Sprint 5 because it is a **routing feature**, not a CRC migration dependency. The current gateway already fans out group commands via unicast iteration (`queueExternalGroupCommand`). Multicast is a performance optimization, not a correctness requirement.

### 4.2 NVS Storage Integrity

The `treatment_manager::computeChecksum()` was deliberately frozen to CCITT-FALSE in Sprint 2 C2 to preserve existing NVS snapshots. This is correct and must NOT be changed until a dedicated migration task with backup script is created.

**Migration rule:** If/when storage CRC migration is desired:
1. Backup all NVS treatment snapshots
2. Create migration script that re-checksums existing records
3. Deploy migration as separate commit with rollback test
4. NEVER merge storage migration into wire CRC migration

### 4.3 Regulatory Compliance

| Parameter | Value | Reference |
|---|---|---|
| Frequency | 433.175 MHz | Thông tư 08/2021/TT-BTTTT |
| Max TX power | 14 dBm / 25 mW | Regulatory limit |
| Modulation | FSK (HC-12) / LoRa (E32) | Hardware decision ADR-HW-001 |

CRC change does NOT affect RF physical layer parameters. No re-certification required for CRC migration alone.

---

## 5. Risk Register

| ID | Risk | Likelihood | Impact | Mitigation | Owner |
|---|---|---|---|---|---|
| R1 | Legacy node firmware uses different checksum than ESP32 `agu_legacy_codec` model | **High** | **Critical** | Sprint 3A wire capture before any code change | Firmware Lead |
| R2 | `test_production` SIGSEGV masks real failures | **High** | **High** | Sprint 0 triage + fix before release gate | QA Lead |
| R3 | Mixed fleet (v0x01 nodes + v0x02 gateway) deployed simultaneously | **Medium** | **Critical** | Version gate fail-closed; OTA must upgrade gateway AND nodes in same window | DevOps |
| R4 | NVS snapshot corruption if storage CRC accidentally changed | **Medium** | **High** | C2 isolation already done; grep audit in every sprint | Firmware Lead |
| R5 | ATmega8 Flash/SRAM overflow after adding `Crc16Modbus.cpp` | **Low** | **High** | `check_atmega8_size.py` post-script; current: 6436/7000 flash, 301/900 RAM | Firmware Lead |
| R6 | RF address expansion breaks existing node routing | **Low** | **Medium** | Track F deferred with feature flag; unicast fallback always works | Architect |
| R7 | `HMAC_TAG_SIZE` confusion (16 bytes vs 12 in docs) causes frame parsing errors | **Low** | **High** | Corrected in this master plan; verify in every test fixture | QA Lead |

---

## 6. Quality Gate Matrix

| Gate ID | Phase | Description | Verification |
|---|---|---|---|
| S0-GATE-01 | S0 | Test harness collects ≥300 tests | `pio test -e native` output |
| S0-GATE-02 | S0 | Baseline failure count documented | `BASELINE_REPORT.md` |
| S1-GATE-01 | S1 | CRC16-Modbus utility PASS | `pio test -e native -f test_crc16` → 9/9 |
| S2-GATE-01 | S2 | `RF_PROTOCOL_VERSION == 0x02` | `grep RF_PROTOCOL_VERSION include/config.h` |
| S2-GATE-02 | S2 | `calculateCrc16("123456789") == 0x4B37` | Unit test |
| S2-GATE-03 | S2 | Fail-closed: CRC error → drop | 1-bit flip test |
| S2-GATE-04 | S2 | Storage CRC isolated | `grep calculateStorageCrc16 include/treatment_manager.h` |
| S2-GATE-05 | S2 | ATmega8 build + size gate | `pio run -e atmega8-node-4` + `check_atmega8_size.py` |
| S2-GATE-06 | S2 | Wire layout `[crc_lo][crc_hi]` preserved | Roundtrip `appendCrc16Modbus` + `readU16Le` |
| S3A-GATE-01 | S3A | Physical wire capture evidence stored | Files in `docs/legacy_wire_captures/` (pending hardware) |
| S3A-GATE-02 | S3A | Legacy checksum classified from authoritative reference | CRC16-Modbus confirmed by Delphi/AVR source; see `docs/LEGACY_WIRE_EVIDENCE.md` |
| S3A-GATE-03 | S3A | ACK / group `$14` behavior measured | Pending hardware capture |
| S3A-GATE-04 | S3A | Actual response sizes measured | Pending hardware capture |
| S3B-GATE-01 | S3B | Legacy encode vectors match golden | `04 06 09 F3 A7` for PUMP_ON node 9 |
| S3B-GATE-02 | S3B | ATmega8 build unaffected | `grep -R "agu_legacy" src/atmega8_node_main.cpp` → empty |
| S4-GATE-01 | S4 | 2500 fuzz iterations, 100% fail-closed | Fuzz test output |
| S4-GATE-02 | S4 | Benchmark CRC_ERRORS == 0 | `rf_benchmark_runner` output |
| S5-GATE-01 | S5 | Zero CCITT references in tests | `grep -rn "0x29B1\|0x1021" test/` → only comments |
| S5-GATE-02 | S5 | Docs aligned to Modbus | `docs/RF_PROTOCOL.md` updated |
| S5-GATE-03 | S5 | All envs build + test PASS | Release gate commands |
| S6-GATE-01 | S6 | Rollback procedure tested on bench | Rollback test log |
| S6-GATE-02 | S6 | CRC error rate < 0.1% in production | Telemetry dashboard |

---

## 7. Rollback Procedure

### 7.1 Code Rollback (Pre-Deployment)

```bash
# 1. Revert to pre-migration tag
git checkout pre-crc16-modbus

# 2. Or targeted revert of specific sprints
git revert <S2-commit-hash>  # Revert codec migration
git revert <S3B-commit-hash> # Revert legacy migration
```

### 7.2 Field Rollback (Post-Deployment)

1. Flash gateway with `RF_PROTOCOL_VERSION = 0x01` + CCITT-FALSE CRC
2. Flash nodes with matching firmware (if source available)
3. Verify: gateway rejects v0x02 frames from any un-updated node
4. Monitor: CRC error rate drops to 0 within stale threshold (15s)

### 7.3 NVS Recovery

```bash
# Treatment snapshots frozen to CCITT-FALSE are always readable
# No migration needed for rollback — storage CRC was never changed
# If storage migration was done separately:
# 1. Restore NVS from backup
# 2. Re-run migration script with CCITT-FALSE flag
```

---

## 8. Commit Strategy

| Commit | Content | Revertable? |
|---|---|---|
| C0 | S0: test harness fixes, baseline report, git tag | Yes |
| C1 | S1: CRC16Modbus utility + tests | Yes (independent) |
| C2 | S2: treatment_manager isolation (C2 prerequisite) | Yes |
| C3 | S2: codec delegation + version bump + tests | Yes (pair with C2) |
| C4 | S3A: compile-time switch + wire capture docs | Yes |
| C5 | S3B: legacy codec migration + tests | Yes |
| C6 | S4: integration verify + fuzz + benchmark | Yes |
| C7 | S5: docs + release gate | Yes |
| C8 | S6: field rollout artifacts | Yes |

**Rule:** C2 + C3 MUST be in the same commit (or C2 must be committed first). Splitting them risks NVS snapshot corruption between commits.

---

## 9. Timeline Estimate

| Phase | Duration | Dependencies |
|---|---|---|
| Sprint 0 | 1 day | None |
| Sprint 1 | ✅ Done | — |
| Sprint 2 | ✅ Code done, 1 day QA | Sprint 1 |
| Sprint 3A | 2-3 days (hardware bench time) | Sprint 2 approval |
| Sprint 3B | 1 day | Sprint 3A evidence |
| Sprint 4 | 2 days | Sprint 3B |
| Sprint 5 | 1 day | Sprint 4 |
| Sprint 6 | 3-5 days (field deployment) | Sprint 5 gate |
| **Total** | **~10-13 working days** | |

---

## 10. Files in This Plan

| File | Purpose | Status |
|---|---|---|
| `README.md` | Baseline plan overview | ✅ Exists |
| `EXECUTION_MASTER_PLAN.md` | This file — master orchestration | ✅ Created |
| `PROGRESS.md` | Sprint-level task tracking | ✅ Exists |
| `WALKTHROUGH_LOG.md` | Execution evidence log | ✅ Exists |
| `sprint_0.md` | Baseline remediation | ✅ Created |
| `sprint_1.md` | CRC16-Modbus utility | ✅ Done |
| `sprint_2.md` | RfFrameCodec migration | ✅ QA Review |
| `sprint_3a.md` | Legacy AGU characterization | ✅ Created |
| `sprint_3.md` | Legacy AGU CRC migration | ✅ Exists (needs update after 3A) |
| `sprint_4.md` | Integration verify + fuzz | ✅ Exists |
| `sprint_5.md` | Docs + release gate | ✅ Exists (needs corrections) |
| `sprint_6.md` | Field rollout + observability | ✅ Created |
| `node_group_scheme.md` | Address expansion design | ✅ Exists (deferred) |
