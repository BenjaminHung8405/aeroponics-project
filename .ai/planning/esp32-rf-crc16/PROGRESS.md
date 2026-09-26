# PROGRESS.md - ESP32 RF CRC16 Project

## Started

- **Timestamp:** 2026-09-26 12:05:12 (Asia/Tokyo)
- **Execution Agent:** Gemini (Planner)
- **Reviewer Agent:** GPT-5.3 (Implementation Architect)

---

## Reference Plan

- **Project Root:** `.ai/planning/esp32-rf-crc16/`
- **Current Sprint:** Sprint 1 (Chuẩn hóa thuật toán CRC16-Modbus + Golden Vectors)
- **Sprint File:** `.ai/planning/esp32-rf-crc16/sprint_1.md`
- **Objective:** Implement CRC16-Modbus as a pure utility, with golden vectors matching 100% Delphi TSCI.CalCRC16/CheckCRC16

---

## Addition Plan

- **Status:** Chưa có yêu cầu phát sinh
- **Notes:** TBD

---

## Track A — Domain Layer (Pure Utility)

| Task ID | Description | Status | Technical Notes |
|---------|-------------|--------|-----------------|
| **A1** | `include/core/Crc16Modbus.h` — Create header file | [ ] QA Review | - Use `#pragma once` guard<br>- Include `<cstddef>`, `<cstdint>` only<br>- Define constants: `kCrc16ModbusInitialValue = 0xFFFF`, `kCrc16ModbusPolynomial = 0xA001`<br>- Declare 3 functions: `calculateCrc16Modbus`, `appendCrc16Modbus`, `verifyCrc16Modbus`<br>- **NO** Arduino/ESP-IDF/FreeRTOS includes (S1-HARD-04) |
| **A2** | `src/core/Crc16Modbus.cpp` — Implement CRC16-Modbus | [ ] QA Review | - **Design Pattern:** Pure function, zero state (S1-HARD-03)<br>- **Security:** Null-check required; `data == nullptr && len != 0` → return 0 (fail-closed)<br>- **Algorithm:** Init `0xFFFF`, polynomial `0xA001`, LSB-first reflected<br>- **Anti-technical-debt:** No heap allocation (`new`/`malloc`/`calloc` forbidden)<br>- **Memory:** Must compile with `-Os`, `-fno-exceptions`, `-fno-rtti`<br>- **Boundary:** `appendCrc16Modbus` returns 0 if `capacity < data_len + 2`<br>- **Byte Order:** Little-endian (CRC_Lo at `[len]`, CRC_Hi at `[len+1]`) |

---

## Track B — Test Layer (Unity Native)

| Task ID | Description | Status | Technical Notes |
|---------|-------------|--------|-----------------|
| **B1** | `test/test_crc16/test_crc16.cpp` — Implement test cases | [ ] QA Review | - **Test-Driven Development:** All vectors from S1.2.2 must PASS<br>- **Golden Vectors (S1-HARD-01):**<br>  • `"123456789"` → `0x4B37`<br>  • `{0x04,0x06,0x09}` → `0xA7F3`<br>  • `{0x04,0x07,0x09}` → `0x37F2`<br>- **Edge Cases:** `nullptr`, `len=0`, `len=255`<br>- **Anti-tamper:** Single-bit flip detection test<br>- **Test Strategy:** Assert exact values; never "fix vector to match code" |
| **B2** | `test/test_crc16/test_crc16_runner.c` — Unity test runner | [ ] QA Review | - Use `UNITY_BEGIN()`, `RUN_TEST()` macro for each test<br>- Return `UNITY_END()`<br>- **Build Target:** Native host only (no hardware dependency) |

---

## Track C — Build/CI Layer

| Task ID | Description | Status | Technical Notes |
|---------|-------------|--------|-----------------|
| **C1** | `platformio.ini` — Add test_crc16 to test_filter | [ ] QA Review | - Modify `[env:native]` section<br>- Set `test_filter = test_production, test_fsm, test_crc16`<br>- **Verification:** `pio test -e native -f test_crc16` must pass |

---

## Track D — Business Layer Boundary (Review Only)

| Task ID | Description | Status | Technical Notes |
|---------|-------------|--------|-----------------|
| **D1** | `docs/interface-wire-contract.md:312` — Review CRC16 CCITT reference | [ ] QA Review | - **DO NOT MODIFY** this file in Sprint 1<br>- Document discrepancy: Line 312 shows `CRC16 ASCII "123456789" = 0x29B1` (CCITT old)<br>- Correct value: `0x4B37` (CRC16-Modbus), verified via independent CRC computation (CCITT-FALSE → 0x29B1, Modbus → 0x4B37)<br>- **Action:** Mapping table entry created in PROGRESS.md for Sprint 5 update; doc not modified in this sprint |

---

## Discrepancy Tracking (Track D - Deferred)

| Original Value (CCITT) | New Value (Modbus) | File | Line | Status |
|------------------------|--------------------|----|------|--------|
| `0x29B1` | `0x4B37` | `docs/interface-wire-contract.md` | 312 | Deferred to Sprint 5 |

---

## Quality Gates (Sprint 1)

| Gate ID | Description | Status |
|---------|-------------|--------|
| **S1-HARD-01** | Golden vectors match Big Plan exactly | [ ] QA Review |
| **S1-HARD-02** | Fail-closed null/boundary checks | [ ] QA Review |
| **S1-HARD-03** | Zero heap allocation (no `new`/`malloc`/`calloc`) | [ ] QA Review |
| **S1-HARD-04** | No Arduino/ESP-IDF/FreeRTOS includes in core | [ ] QA Review |
| **S1-HARD-05** | Loop efficiency (no 256-byte LUT for ATmega8) | [ ] QA Review |

---

## Quality Gate Evidence (measured 2026-09-26, awaiting independent audit)

| Gate ID | Evidence |
|---------|----------|
| **S1-HARD-01** | `pio test -e native -f test_crc16` → 9/9 PASS. Independent table-driven reference (separate 256-entry LUT program, not the firmware bit loop) reproduces `0x4B37`, `0xA7F3`, `0x37F2` and whole-frame remainder `0x0000`. Fuzz over lengths 0..255 x 64 random payloads: bitwise core == table reference on every case. |
| **S1-HARD-02** | Fail-closed contract: `calculateCrc16Modbus(nullptr, 0) == 0xFFFF`; `(nullptr, 1/10/255) == 0`; `verifyCrc16Modbus(nullptr, *) == false`; `appendCrc16Modbus(nullptr, ...) == 0`; `capacity < data_len + 2` → `0`; `data_len > capacity` → `0` (no unsigned underflow). Canary check: on rejection, not one buffer byte is written. |
| **S1-HARD-03** | `grep -E 'new\|malloc\|calloc\|realloc\|free('` over `include/core/Crc16Modbus.h` + `src/core/Crc16Modbus.cpp` → no matches. `nm -u` on the compiled object shows no undefined heap symbol. |
| **S1-HARD-04** | Includes in core are exactly `"core/Crc16Modbus.h"`, `<cstddef>`, `<cstdint>`. No Arduino/ESP-IDF/FreeRTOS header anywhere in the module. |
| **S1-HARD-05** | No 256-byte table: the only `[256]`/LUT hit in the module is the comment explaining its absence. Bitwise loop is 8 iterations/byte using one 16-bit register. Builds with the ATmega8 profile flags `-Os -fno-exceptions -fno-rtti`. |

### Regression comparison against pre-Sprint-1 baseline (`41feec6`)

| Suite | Baseline (`41feec6`) | After Sprint 1 | Verdict |
|-------|----------------------|----------------|---------|
| `test_crc16` | n/a (new) | 9/9 PASS | New suite green |
| `test_fsm` | 21/21 PASS | 21/21 PASS | No change |
| `test_production` | 97 failed / 104 succeeded (ERRORED) | 97 failed / 104 succeeded (ERRORED) | Pre-existing, **identical** — not caused by Sprint 1 |
| `pio test -e native` (bare) | 0 test cases collected | 0 test cases collected | Pre-existing: the comma-separated `test_filter` value matches no glob; unchanged by Sprint 1 |

> **Observations for the Review Agent (not fixed in this pass, out of Sprint 1 scope):**
> 1. `test_production` fails 97 cases at the pre-Sprint-1 baseline too. Sprint 1 adds no new failures, but the suite is not green and should be triaged separately.
> 2. `pio test -e native` without `-f` runs nothing because `test_filter = test_production, test_fsm, test_crc16` is parsed as a single literal pattern. Sprint 1 acceptance is defined against `pio test -e native -f test_crc16`, which passes. Worth a separate C1 follow-up if CI relies on the bare command.

## Sprint 1 Acceptance Criteria

- [x] `pio test -e native -f test_crc16` → All PASS (0 failures)
- [ ] All 6 tasks (A1-A2, B1-B2, C1, D1) Done — all currently in QA Review, awaiting independent Review Agent sign-off (not marked Done per workflow)
- [x] All 5 quality gates verified (self-check complete; status: QA Review, pending independent audit)
- [x] No changes to `rf_frame_codec.cpp` or `agu_legacy_codec.cpp`

---

**Last Updated:** 2026-09-26 14:40:00 (Asia/Tokyo)
**Current Phase:** All 5 quality gates (S1-HARD-01..05) verified and awaiting independent audit; no source change required — all gates PASS as implemented.
