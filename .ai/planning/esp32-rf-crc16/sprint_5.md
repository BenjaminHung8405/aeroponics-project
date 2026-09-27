# Sprint 5: Docs, Benchmark QA, Release Gate & Rollback Plan

> **Phụ thuộc:** Sprint 1 (Crc16Modbus utility), Sprint 2 (codec), Sprint 3 (legacy codec), Sprint 4 (integration verify), Sprint 0 (baseline remediation — `DEF-01`/`DEF-02`).
> **✅ Release gate correction (DEF-01 resolved):** `platformio.ini` `test_filter` đã được sửa (newline-separated) trong commit `434c80c`; bare `pio test -e native` chạy **315/315 PASS**. **KHÔNG** dùng dạng CLI comma `-f test_crc16,test_fsm,test_production` — PlatformIO parse thành một glob duy nhất và báo **0 test case, exit 0** (false green). Dùng bare, hoặc một `-f <suite>` cho mỗi lần chạy.
> **Mục đích cuối cùng:** đồng bộ toàn bộ tài liệu (wire contract, README), xác nhận benchmark & test coverage, đóng gói release gate, và có kế hoạch rollback nếu cần phải quay về CRC cũ do bất kỳ bug nghiêm trọng nào.

---

## 1. PHẠM VI & MỤC TIÊU

### 1.1 Modules / Files tác động

| File | Hành động | Mục đích |
|---|---|---|
| `.ai/planning/esp32-rf-crc16/README.md` | **Rà soát thêm** (file này đã có phần baseline) | Kiểm tra/update các vector golden, bảng mapping cũ→mới, tech stack. |
| `docs/interface-wire-contract.md` | **Sửa** | Cập nhật phần checksum AGU và table phần mạch CRC-Modbus trên RF wire. |
| `docs/QA_ACCEPTANCE_REPORT_4_NODES.md` | **Sửa** | Ghi chú policy legacy checksum đã bị hủy bỏ. |
| `aeroponics-firmware/test/test_production/test_production.cpp` | **Sửa** | Re-check test names/hex vectors đã được Sprint 1-4 đổi — đảm bảo 100% mới. |
| `aeroponics-firmware/src/rf_benchmark_runner.cpp` | **Sửa** | Ghi nhận kết quả benchmark cuối cùng (CRC error count, throughput). |
| `.ai/planning/esp32-rf-crc16/sprint_5.md` | **Giữ** | Kế hoạch releases & rollback (tài liệu này). |
| `WALKTHROUGH_LOG.md` (cùng project) | **Post** | Ghi lại thực trạng sprint 1-5 như một bước milestones (tùy bước nhanh). |

### 1.2 Mục tiêu cụ thể

- [ ] `docs/interface-wire-contract.md` chapter 3 checksum field: mô tả định dạng wire `CRC16 Modbus` chính xác và bảng hex vector ví dụ (lấy từ test vectors của Sprint 1-3).
- [ ] `docs/QA_ACCEPTANCE_REPORT_4_NODES.md` ghi chú S1.5-RF-01 (checksum legacy đã drop).
- [ ] `test/test_production/test_production.cpp` 0 test case về CRC/CCITT cũ tồn tại (tất cả là Modbus).
- [ ] Benchmark `rf_benchmark_runner.cpp` ghi nhận kết quả cuối cùng (thời gian tính toán CRC Modbus, count CRC_ERRORs, kết quả so sánh so sánh 'sạch' và 'tampered', không exception).
- [ ] Release gate: `pio test -e native && pio run -e esp32-s3-devkitc-1 && pio run -e atmega8-node-4` -> tất cả PASS. (Bare `native` đã gồm 4 suite: `test_crc16`, `test_fsm`, `test_production`, `test_rf_address` = 315 test.)
- [ ] Rollback plan (fallback): nếu cần quay về CRC cũ, chỉ cần thay đổi `#define RF_PROTOCOL_VERSION 0x02` -> `0x01` và sửa hàm `calculateCrc16` quay lại CCITT-FALSE; file `treatment_manager` giữ nguyên.

---

## 2. KIẾN TRÚC & LUỒNG DỮ LIỆU

### 2.1 Mapping doc CRC cũ vs mới

| Đặc tính | CRC cũ (CCITT-FALSE) | CRC mới (Modbus) |
|---|---|---|
| Khởi tạo | 0xFFFF | 0xFFFF |
| Đa thức | 0x1021 (MSB-first, không phản chiếu) | 0xA001 (LSB-first, phản chiếu) |
| Byte đầu vào | `crc ^= data[i] << 8` (MSB) | `crc ^= data[i]` (LSB từng bit) |
| Vòng lặp bit | 8 vòng dịch trái; nếu cờ bit 15 → XOR đa thức | 8 vòng kiểm tra LSB `crc & 0x0001`; dịch phải; XOR nếu bit = 1 |
| Vị trí appended | 2 byte LE tại cuối frame (sau HMAC tag) | 2 byte LE tại cuối frame (sau HMAC tag) — vị trí giữ nguyên |
| Kết quả verify | `crc == readU16Le(frame+...)` → fail nếu khác 0 | `crc == 0x0000` khi dữ liệu toàn vẹn (theo chuẩn Modbus) |
| Ví dụ test (Sprint 1) | `CRC16 ASCII "123456789" = 0x29B1` | `CRC16 Modbus "123456789" = 0x4B37` |

### 2.2 QA_ACCEPTANCE_REPORT_4_NODES.md cập nhật

| Section | Content cũ | Content mới |
|---|---|---|
| **S1.5-RF-01** | `Parser reject CRC/length/version sai; duplicate sequence không kích pump lần hai.` | Giữ nguyên, nhưng chú thích: `CRC hiện tại dùng chuẩn CRC16-Modbus (Sprint 1–4). Các payload cũ dùng zero-sum legacy đã được hủy bỏ ngay sau parse (kiểm tra policy ingestion).` |
| **S1.5-RF-02** | `CRC mismatch detection: test_rf_frame_codec_*` | Cũng không đổi về code nhưng config `RF_PROTOCOL_VERSION 0x02` khóa chế độ mới. |

### 2.3 Benchmark kết quả ghi lại

- `rf_benchmark_runner` ghi lại:
  - Tốc độ tính toán CRC16-Modbus trên 1 MB payload: ~`X µs` (đo trên CPU ESP32-S3, ví dụ 12 µs).
  - Đếm CRC_ERRORs trên 10.000 frame rand → 0.
  - Frame hợp lệ TX→RX roundtrip: decode `CRC_MISMATCH` nếu thay đổi 1 bit bất cứ đâu, `HMAC_AUTH_FAIL` nếu thay đổi HMAC, `OK` nếu chính xác.
  - Lưu ý: qua Sprint 3, thêm throughput legacy codec modbus: ~`Y µs` trên payload 8 byte.

### 2.4 Rollback / fallback plan

- **Lý do rollback** (bất kỳ trường hợp nào): bug nghiêm trọng CRC Modbus làm timeout node / mất lệnh, hoặc rollback yêu cầu tương thích với hệ thống cũ đang chạy CCITT.
- **Bước quay về CRC cũ:**
  1. Xoá `#define RF_PROTOCOL_VERSION 0x02` trong `config.h` → đổi về `0x01`.
  2. Đổi `calculateCrc16` trong `src/rf_frame_codec.cpp` quay lại bản cũ (CCITT-FALSE poly `0x1021`, init 0xFFFF, MSB-first). Có thể lưu bản cũ tại git tag trước khi Sprint 2 (ví dụ `git stash` hoặc tag `pre-crc16-modbus`).
  3. Cập nhật lại `treatment_manager::computeChecksum` nếu đã thay đổi (trở về `static uint16_t calculateCrc16Ccitt(...)` nếu đã tách).
  4. Giảm `RF_PROTOCOL_VERSION` → gateway cũ sẽ chấp nhận node cũ, nhưng note: `treatment_manager` internal checksum cũ vẫn còn hiệu lực nếu không migrate.
- **Kiểm soát rollback bằng biến tính:** Để tránh rollback vô tình, bổ sung `static const bool kCrc16ModbusEnabled = true;` trong config.h, và kèm `static_assert(kCrc16ModbusEnabled, "CRC16 Modbus enabled for RF protocol v2")`.
- **Backup dữ liệu NVS:** Trước khi thực hiện migration vĩnh viễn, chạy script backup config NVS `treatment` snapshot (kể cả bản cũ) để khôi phục nếu cần.

---

## 3. PHÂN RÃ CHI TIẾT TÁC VỤ

> Task-id: `S5-T<n>`. Gồm các bước admin/doc/check/check.

### TRACK A — Tài liệu (Docs)

**TASK S5-T1 `docs/interface-wire-contract.md` — Chapter 3 (CRC / checksum)**

- Khai báo: `@brief Two-byte CRC-16/MODBUS checksum appended to all RF frames as `[crc_lo][crc_hi]` little-endian after HMAC-SHA256 (16-byte tag). Init 0xFFFF, poly 0xA001, reflected LSB-first. Length byte of envelope: `payload_len + 2`.`
- Bảng ví dụ hex (taken from Sprint 1-3 test vectors):
  | Command | Payload bytes | Frame hex (len + payload + CRC) |
  |---|---|---|
  | PUMP_ON, node 9 | `{0x06, 0x09}` | `04 06 09 F3 A7` |
  | PUMP_OFF, node 9 | `{0x07, 0x09}` | `04 07 09 F2 37` |
  | Set Pump, extra params | 9-byte payload | ... (vector mới). |
- Footer: `Legacy AGU checksum contract is CRC16-Modbus on the SendComCRC16 envelope; the old one-byte zero-sum frame is rejected after parse (see QA Acceptance Report).`

**TASK S5-T2 `docs/QA_ACCEPTANCE_REPORT_4_NODES.md` — Ghi chú S1.5-RF-01**
- Cập nhật chú thích `legacy checksum` => `dropped after parse`.
- Ghi chú: `Per the confirmed legacy evidence, the AGU wire now emits CRC16-Modbus on the SendComCRC16 envelope.`

### TRACK B — Tầng Test/QA

**TASK S5-T3 `test/test_production/test_production.cpp` — Cleanup CRC references**
- Xoá toàn bộ hàm `test_rf_crc16_ccitt_false_standard_test_vector` và các test vector CCITT (trong khi giữ lại các test Modbus đã ở Sprint 1).
- Kiểm tra grepping: `grep -n "0x29B1\|0x1021\|calculateCrc16\|CRC_CCITT"` → chỉ xuất hiện ở comment/documentation, không phải test kỳ vọng.
- Thay tên hàm testCRCModbus theo chuẩn: `test_rf_crc16_modbus_standard_vector`, `test_rf_crc16_modbus_null_input`, `test_rf_crc16_modbus_flip_bit_anywhere`.

**TASK S5-T4 `rf_benchmark_runner.cpp` — Ghi nhận kết quả cuối cùng**
- Đảm bảo hàm `runBenchmark` ghi lại:
  - Kết quả `crc_error_count == 0`.
  - Thời gian trung bình `crc_calc_ms` trên 10.000 frame.
  - `benchmark_result` struct lưu về file `benchmark_crc16_modbus.log` (nếu cần debug sau này).
- (Tuỳ chọn) in dòng `CRC MODBUS enabled v2` vào output log start-up.

### TRACK C — Tầng Release (Gate & Rollback)

**TASK S5-T5 Release gate — lệnh kiểm tra**
- Chạy: `pio test -e native` (4 suite host: `test_crc16`, `test_fsm`, `test_production`, `test_rf_address` = 315 test), `pio test -e native-prototype` (23 test), `pio run -e native-integration` (**build** harness Mosquitto thật — env này không có test suite nên `pio test -e native-integration` sẽ lỗi undefined symbols), `pio run -e esp32-s3-devkitc-1` (build gateway), `pio run -e atmega8-node-4` (build node).
- **Không** dùng dạng CLI comma `-f a,b,c`: PlatformIO parse thành một glob duy nhất → `0 test cases`, exit 0 (false green).
- Kiểm tra exit code = 0; nếu không -> chặn merge/pull request.
- Ghi chú kết quả trong `WALKTHROUGH_LOG.md`.

**TASK S5-T6 Rollback plan / documentation (backup)**
- Xoá hoặc đóng dấu file `scripts/rollback_crc16.sh` (không bắt buộc tạo, nhưng có thể nêu trong WALKTHROUGH_LOG).
- Ghi chú bước rollback tại Sprint 5 kết thúc: "Nếu cần rollback về CRC CCITT-FALSE, xem `git tag pre-crc16-modbus` và thay đổi config.h + rf_frame_codec.cpp như quy trình đã định sẵn".
- Lưu bản snapshot git tag `rf-crc16-migration-sprint5-final` sau khi tất cả đều green.

---

## 4. TIÊU CHUẨN RÀ SOÁT CỨNG

1. **Doc đồng bộ (S5-HARD-01):** Mọi tài liệu mô tả checksum trên wire phải khớp với chuẩn Modbus (không có tham chiếu CCITT-FALSE giá trị thực tế bên ngoài phần lịch sử/tracer).
2. **Zero regression (S5-HARD-02):** Kết quả benchmark/fuzz trong Sprint 4 phải duy trì; không có test mới fail khi chạy lại Sprint 5.
3. **Test coverage 100% (S5-HARD-03):** `test_production.cpp` không chứa bất kỳ unit test nào legacy CRC / CCITT-FALSE giá trị cứng — tất cả Modbus hoặc null case.
4. **Gate qua môi trường đa nền tảng (S5-HARD-04):** Build trên 4 env (native host, native-integration, esp32-s3, atmega8-node-4) đều PASS trước khi merge.
5. **Rollback an toàn (S5-HARD-05):** Quy trình rollback (xoá `0x02`, sửa hàm `calculateCrc16` về CCITT-FALSE) phải được ghi rõ tại WALKTHROUGH_LOG.md; không `git reset --hard` vô lý.
6. **Không break legacy codec/internal storage contracts (S5-HARD-06):** Nếu `treatment_manager`/calibration/EEPROM storage checksum chưa được migrate, Sprint 3/4 thay đổi wire CRC không ảnh hưởng storage — ví dụ đảm bảo `NVS checksum cũ vẫn read/write được` (test riêng) trước release.

---

## Kết quả dự kiến sau Sprint

- Tài liệu (wire contract + QA report) khớp chuẩn CRC16-Modbus bản chất.
- Test suit 100% clean (0 legacy CRC reference), toàn bộ env build PASS.
- Benchmark số liệu ghi lại, release gate xanh.
- Có kế hoạch rõ ràng để rollback nếu cần.
- Kế hoạch kế thừa (handoff) hoàn chỉnh.

> **Kết thúc Plan: `esp32-rf-crc16`.** Tất cả 5 Sprint hoàn tất.
