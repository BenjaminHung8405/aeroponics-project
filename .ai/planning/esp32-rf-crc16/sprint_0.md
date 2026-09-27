# Sprint 0: Baseline Remediation & Test Harness Repair

> **Phụ thuộc:** Không — đây là Phase 0, chạy song song với Sprint 1/2 QA.
> **Mục tiêu:** Làm cho test harness đáng tin cậy, để mọi release gate sau đó là có ý nghĩa.
> **Nguồn gốc:** DEF-01 (test_filter 0 test), DEF-02 (test_production 97 fail + SIGSEGV).

---

## 1. BỐI CẢNH — VÌ SAO SPRINT NÀY LÀ BẮT BUỘC

Release gate hiện tại trong `sprint_5.md` (S5-T5) dùng lệnh:

```bash
pio test -e native && pio test -e native-integration && pio run -e esp32-s3-devkitc-1 && pio run -e atmega8-node-4
```

**Hai lệnh đầu fail/vô nghĩa:**

1. `pio test -e native` — `platformio.ini:50` khai báo `test_filter = test_production, test_fsm, test_crc16` (comma-separated). PlatformIO đọc chuỗi này như **MỘT glob literal** duy nhất, không match test suite nào → **collect 0 test case, exit code 0 (false green)**. Đây là defect nguy hiểm nhất: gate xanh mà không chạy test nào.

2. `pio test -e native-integration` — env này không có `test_filter` và không có test suite → cũng không có ý nghĩa regression.

Ngoài ra, `test_production` (277 RUN_TEST) có **97 failure + SIGSEGV pre-existing** tại baseline `41feec6`. Nếu không triage và đóng dấu baseline, ta không thể phân biệt:
- failure do CRC migration (cần sửa),
- failure pre-existing (ngoài scope),
- failure mới do thay đổi Sprint 2/3/4 (regression — bắt buộc chặn merge).

**Quy tắc của senior IIoT dev:** Không release gate nào đáng tin khi baseline test không được categorize.

---

## 2. PHẠM VI & FILES TÁC ĐỘNG

| File | Hành động | Lý do |
|---|---|---|
| `aeroponics-firmware/platformio.ini` | **Sửa** | Fix `test_filter` comma-as-glob bug (DEF-01) |
| `aeroponics-firmware/test/test_production/test_production.cpp` | **Sửa (mục tiêu)** | Triage + fix fixture CCITT/version-byte stale (chỉ phần thuộc CRC migration) |
| `BASELINE_REPORT.md` | **Tạo mới** | Baseline evidence |
| Git tag `pre-crc16-modbus` | **Tạo** | Rollback anchor |

> ⚠️ Sprint này **KHÔNG** sửa `rf_frame_codec.cpp`, `agu_legacy_codec.cpp`, `Crc16Modbus.*`, `config.h` — chỉ sửa test harness & test fixtures.

---

## 3. MỤC TIÊU CỤ THỂ

- [ ] `pio test -e native` (bare) collect **≥ 300 test case** (hiện tại 0).
- [ ] Triage xong 97 failure của `test_production` — phân loại 4 nhóm (xem §5).
- [ ] Đóng dấu baseline: commit/tag `pre-crc16-modbus` tại HEAD trước khi test fix.
- [ ] `BASELINE_REPORT.md` ghi rõ: tổng test, pass, fail, SIGSEGV point, classified causes.
- [ ] Không phát sinh **failure mới** nào sau khi sửa harness (zero regression trong Sprint 0).

---

## 4. PHÂN RÃ TÁC VỤ

### TRACK A — Test Filter Fix

**TASK S0-T1 `platformio.ini` — Fix `test_filter` comma bug**
- **Hiện tại:** `test_filter = test_production, test_fsm, test_crc16` (line 50) → parsed as single glob → 0 tests.
- **Cách sửa:** PlatformIO `test_filter` nhận **space-separated** glob patterns. Đổi thành:
  ```ini
  test_filter = test_production test_fsm test_crc16
  ```
  (không có dấu phẩy).
- **Hoặc** dùng wildcard: `test_filter = test_*` nếu muốn chạy tất cả.
- **Verification:** `pio test -e native` phải in ra dòng collect ≥300 test case, và exit code phản ánh đúng kết quả (không còn false green).
- **Lưu ý E1 carry-over:** constraint "giữ nguyên `test_production, test_fsm, test_crc16`" trong `sprint_2.md` ám dẫn **danh sách test**, không phải **ký tự phẩy**. Giữ nguyên 3 suite, chỉ đổi separator.

### TRACK B — Baseline Triage

**TASK S0-T2 `test_production.cpp` — Phân loại 97 failure**
- Chạy `pio test -e native -f test_production` (filtered, có ý nghĩa), capture log đầy đủ.
- Phân loại mỗi failure vào **đúng 1 nhóm**:

| Nhóm | Mô tả | Hành động |
|---|---|---|
| **G1 — CRC stale** | Expected `0x29B1` nhưng code trả `0x4B37` (do A1 delegate Modbus) | **Fix trong S0-T3** (đây là "cải thiện", không phải regression) |
| **G2 — Version byte hardcode** | `RF_PROTOCOL_VERSION` bump 0x01→0x02 làm fixture cứng `0x01`/`0x02` fail | **Fix trong S0-T4** |
| **G3 — SIGSEGV C4** | Crash pre-existing tại region test C4 | **Đóng dấu baseline**, ticket riêng |
| **G4 — Other pre-existing** | Failure không liên quan CRC/version | **Đóng dấu baseline**, ngoài scope |

**TASK S0-T3 Fix G1 — Fixture CCITT stale**
- `test_production.cpp:2252` — `test_rf_crc16_ccitt_false_standard_test_vector`: đã được D1 sửa → verify.
- Rà `grep -n "0x29B1\|0x1021\|ccitt\|CCITT" test/test_production/test_production.cpp` — mọi result phải được xử lý (fix hoặc comment lịch sử rõ ràng).
- **KHÔNG sửa vector để khớp code** — vector `0x4B37` là chuẩn độc lập. Nếu code sai → sửa code.

**TASK S0-T4 Fix G2 — Version byte hardcode**
- `test_production.cpp:405,407` — `RfHeader header{{0xAA,0x55}, 0x01, ...}` và `expected_header[]` → dùng `RF_PROTOCOL_VERSION`.
- `test_production.cpp:8073` — vector "Unsupported wire protocol version" dùng `{0xAA,0x55,0x02}`; sau bump, `0x02` hợp lệ → fail. Đổi sang giá trị ≠ `RF_PROTOCOL_VERSION` (ví dụ `0x7F`).
- `test_production.cpp:2476` — `corrupted[2] = 0x02` nhãn "Unsupported version" → dùng version cố ý sai để test có nghĩa.
- **Mục tiêu:** mọi assertion version đọc từ hằng, không literal.

### TRACK C — Evidence

**TASK S0-T5 Git tag**
```bash
git tag -a pre-crc16-modbus -m "Pre-CRC16-MODBUS migration anchor"
```
- Tag tại commit **trước** mọi fix test của Sprint 0 (để rollback về harness gốc nếu cần).

**TASK S0-T6 `BASELINE_REPORT.md` — Tạo mới**
- Nội dung:
  - Toolchain versions (PlatformIO, platform `espressif32`, `atmelavr`).
  - Lệnh build/test chạy và kết quả: pass/fail counts, timestamp.
  - Classification table 97 failure (G1..G4).
  - Resource gate ATmega8 (flash/RAM).
  - Danh sách file liên quan (CRC utility, codec, treatment_manager, config, legacy, tests).
  - Risk items + assumptions.

---

## 5. TIÊU CHUẨN RÀ SOÁT CỨNG

1. **No false green (S0-HARD-01):** Sau S0-T1, `pio test -e native` bare **không được** collect 0 test. Nếu vẫn 0 → gate fail, không được move on.
2. **Zero new regression (S0-HARD-02):** Số failure sau S0-fix ≤ baseline trước fix. Fix G1/G2 làm failure **giảm** là cải thiện; thêm failure mới là regression → dừng.
3. **Classification completeness (S0-HARD-03):** Đủ 97 failure được phân loại vào G1..G4. Không orphan failure.
4. **No production code touched (S0-HARD-04):** Diff của Sprint 0 chỉ gồm `platformio.ini`, `test/test_production/*`, planning files. `rf_frame_codec.*`, `Crc16Modbus.*`, `config.h`, `treatment_manager.h`, `agu_legacy_codec.*` **không đổi**.
5. **SIGSEGV pinning (S0-HARD-05):** G3 SIGSEGV phải được reproduce và ghi lại stack/region vào `BASELINE_REPORT.md`. Không "fix ngầm" nếu chưa hiểu root cause — đó là ticket riêng.

---

## 6. KẾT QUẢ DỰ KIẾN

- `pio test -e native` (bare) → collect ≥300 test, kết quả khớp filtered run.
- 97 failure được phân loại; G1/G2 được fix (failure giảm); G3/G4 đóng dấu baseline.
- `BASELINE_REPORT.md` + tag `pre-crc16-modbus` tồn tại.
- Sprint 2 QA review có baseline tin cậy để đối chiếu regression.
