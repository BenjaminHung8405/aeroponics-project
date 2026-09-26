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
| **B1** | `test/test_crc16/test_crc16.cpp` — Implement test cases | [ ] Pending | - **Test-Driven Development:** All vectors from S1.2.2 must PASS<br>- **Golden Vectors (S1-HARD-01):**<br>  • `"123456789"` → `0x4B37`<br>  • `{0x04,0x06,0x09}` → `0xA7F3`<br>  • `{0x04,0x07,0x09}` → `0x37F2`<br>- **Edge Cases:** `nullptr`, `len=0`, `len=255`<br>- **Anti-tamper:** Single-bit flip detection test<br>- **Test Strategy:** Assert exact values; never "fix vector to match code" |
| **B2** | `test/test_crc16/test_crc16_runner.c` — Unity test runner | [ ] Pending | - Use `UNITY_BEGIN()`, `RUN_TEST()` macro for each test<br>- Return `UNITY_END()`<br>- **Build Target:** Native host only (no hardware dependency) |

---

## Track C — Build/CI Layer

| Task ID | Description | Status | Technical Notes |
|---------|-------------|--------|-----------------|
| **C1** | `platformio.ini` — Add test_crc16 to test_filter | [ ] Pending | - Modify `[env:native]` section<br>- Set `test_filter = test_production, test_fsm, test_crc16`<br>- **Verification:** `pio test -e native -f test_crc16` must pass |

---

## Track D — Business Layer Boundary (Review Only)

| Task ID | Description | Status | Technical Notes |
|---------|-------------|--------|-----------------|
| **D1** | `docs/interface-wire-contract.md:312` — Review CRC16 CCITT reference | [ ] Pending | - **DO NOT MODIFY** this file in Sprint 1<br>- Document discrepancy: Line 312 shows `CRC16 ASCII "123456789" = 0x29B1` (CCITT old)<br>- Correct value: `0x4B37` (CRC16-Modbus)<br>- **Action:** Create mapping table in this PROGRESS.md for Sprint 5 update |

---

## Discrepancy Tracking (Track D - Deferred)

| Original Value (CCITT) | New Value (Modbus) | File | Line | Status |
|------------------------|--------------------|----|------|--------|
| `0x29B1` | `0x4B37` | `docs/interface-wire-contract.md` | 312 | Deferred to Sprint 5 |

---

## Quality Gates (Sprint 1)

| Gate ID | Description | Status |
|---------|-------------|--------|
| **S1-HARD-01** | Golden vectors match Big Plan exactly | [ ] Pending |
| **S1-HARD-02** | Fail-closed null/boundary checks | [ ] Pending |
| **S1-HARD-03** | Zero heap allocation (no `new`/`malloc`/`calloc`) | [ ] Pending |
| **S1-HARD-04** | No Arduino/ESP-IDF/FreeRTOS includes in core | [ ] Pending |
| **S1-HARD-05** | Loop efficiency (no 256-byte LUT for ATmega8) | [ ] Pending |

---

## Sprint 1 Acceptance Criteria

- [ ] `pio test -e native -f test_crc16` → All PASS (0 failures)
- [ ] All 6 tasks (A1-A2, B1-B2, C1, D1) completed
- [ ] All 5 quality gates verified
- [ ] No changes to `rf_frame_codec.cpp` or `agu_legacy_codec.cpp`

---

**Last Updated:** 2026-09-26 12:16:31 (Asia/Tokyo)
**Current Phase:** Track A Complete — A1/A2 awaiting QA Review
