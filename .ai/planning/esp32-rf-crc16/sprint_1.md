# Sprint 1: Chuẩn hóa thuật toán CRC16-Modbus + Golden Vectors

> **Phụ thuộc:** Không (Sprint nền tảng của Plan).
> **Output kiểm chứng được trước tiên:** CRC16-Modbus được định nghĩa như một pure utility, có golden vectors khớp 100% với Delphi `TSCI.CalCRC16/CheckCRC16` (ví dụ `04 06 09 F3 A7`, `04 07 09 F2 37`) và vector chuẩn `"123456789" = 0x4B37` — chưa đụng vào codec chính.

---

## 1. PHẠM VI & MỤC TIÊU

### 1.1 Modules / Files tác động

| File | Hành động | Lý do |
|---|---|---|
| `aeroponics-firmware/include/core/Crc16Modbus.h` | **Tạo mới** | Khai báo 3 hàm CRC thuần: `calculateCrc16Modbus`, `appendCrc16Modbus`, `verifyCrc16Modbus` |
| `aeroponics-firmware/src/core/Crc16Modbus.cpp` | **Tạo mới** | Cài đặt CRC16-Modbus init `0xFFFF`, đa thức `0xA001`, bitwise LSB-first |
| `aeroponics-firmware/test/test_crc16/test_crc16.cpp` | **Tạo mới** | Toàn bộ unit test CRC (golden vectors + edge cases) |
| `aeroponics-firmware/test/test_crc16/test_crc16_runner.c` | **Tạo mới** | `TEST_MAIN` Unity runner cho riêng suite CRC |
| `aeroponics-firmware/platformio.ini` | **Sửa** | Thêm `test_crc16` vào cụm `test_filter` của env `[env:native]` |
| `aeroponics-firmware/src/core/hmac_sha256.cpp` | **KHÔNG** | HMAC giữ nguyên — chỉ dùng làm chuẩn cấu trúc module tham khảo |

> ⚠️ Sprint này **nghiêm cấm** sửa `rf_frame_codec.cpp` hoặc `agu_legacy_codec.cpp`.

### 1.2 Mục tiêu cụ thể

- [ ] Định nghĩa duy nhất chuẩn CRC16-Modbus: `init=0xFFFF`, đa thức `(crc>>1)^0xA001` khi LSB=1 (reflected, không dùng XOR-out).
- [ ] Vector `"123456789"` → `0x4B37` PASS.
- [ ] Golden vector từ Big Plan: `[0x04,0x06,0x09]` → `0xA7F3` (append lo `F3`, hi `A7`); toàn frame `[04 06 09 F3 A7]` → verify = `0x0000`.
- [ ] Golden vector off-pump: `[0x04,0x07,0x09]` → `0x37F2` (lo `F2`, hi `37`); whole-frame modbus = `0x0000`.
- [ ] Edge cases: `nullptr` đầu vào, `len=0` → kết quả `0xFFFF` (biên quy ước), data rỗng không crash.
- [ ] Giới hạn frame: đảm bảo hàm hỗ trợ độ dài tối đa `255 byte` (khớp ShortString envelope `len ≤ 253 + 2`).
- [ ] Suite riêng chạy được trên env `[env:native]` không cần phần cứng.

---

## 2. KIẾN TRÚC & LUỒNG DỮ LIỆU

### 2.1 Vị trí utility trong Clean Architecture

```
[Domain layer]  core/Crc16Modbus.h/.cpp     ← Tinh khiết, không phụ thuộc HW/RTOS
      ▲
[App layer]     RfFrameCodec (Sprint 2 sẽ gọi) · AguLegacyCodec (Sprint 3 sẽ gọi)
      ▲
[Test layer]    test/test_crc16/ → Unity runner (native host)
```

### 2.2 Luồng dữ liệu của utility

```
INPUT  : bytes[] (con trỏ uint8_t, len bytes)
         │
         ▼
crc = 0xFFFF
         │
         ▼
for each byte b:
    crc ^= b                 (XOR byte thấp LSB-first)
    for bit in 8:
        if crc & 0x0001:  crc = (crc >> 1) ^ 0xA001
        else:             crc = crc >> 1
         │
         ▼
OUTPUT : uint16_t crc  (16-bit word)

appendCrc16Modbus(frame, len):
    crc = calculateCrc16Modbus(frame, len)
    frame[len]     = lo(crc)     // CRC_Lo
    frame[len + 1] = hi(crc)     // CRC_Hi
    return len + 2

verifyCrc16Modbus(frame, len /*gồm cả 2 byte CRC*/):
    return calculateCrc16Modbus(frame, len) == 0x0000
```

> Đặc tính Modbus: tính lại CRC trên **toàn bộ** gói `[data][crc_lo][crc_hi]` tự triệt tiêu về `0x0000` khi dữ liệu toàn vẹn (khớp `TSCI.CheckCRC16` và Assembly `CheckCRC16` 80x86).

### 2.3 Quan hệ with Big Plan reference

| Tham chiếu | Điểm tương ứng |
|---|---|
| Delphi `CalCRC16` — `crc := $FFFF; if (crc and $0001)<>0 then crc := (crc shr 1) xor $A001` | Đúng chuẩn bitwise này |
| Delphi `CheckCRC16` — duyệt `i := 0 to Length(s)` rồi `crc = 0` | Đúng kiểu remainder-zero |
| AVR assembly `CalCRC16` — `mov al,0xFF; mov ah,0xFF; xor al,bl; shr ah,1; xor ah,0xA0; xor al,0x01` | Cùng đa thức, cùng trật tự hi/lo đuôi |
| AVR assembly `CheckCRC16` — `mov cl,@di; inc cl` | Nhắc lại: cần tính luôn byte `[len]` khi biết wire format |

---

## 3. PHÂN RÃ CHI TIẾT TÁC VỤ

> Quy ước task-id: `S1-T<n>`. Mỗi task nhỏ, 1 hàm.

### TRACK A — Tầng Dữ liệu (Domain utility)

**TASK S1-T1 `include/core/Crc16Modbus.h` — Tạo mới**
- Khai báo hằng:
  - `constexpr uint16_t kCrc16ModbusInitialValue = 0xFFFF;`
  - `constexpr uint16_t kCrc16ModbusPolynomial = 0xA001;`
- Khai báo hàm:
  - `uint16_t calculateCrc16Modbus(const uint8_t* data, size_t len);` — null-safe.
  - `size_t appendCrc16Modbus(uint8_t* frame, size_t data_len, size_t capacity);` — trả `0` nếu `capacity < data_len + 2`.
  - `bool verifyCrc16Modbus(const uint8_t* frame, size_t len);` — trả `false` nếu `frame==nullptr`.
- Guard include `#pragma once`; `#include <cstddef>`, `<cstdint>`.

**TASK S1-T2 `src/core/Crc16Modbus.cpp` — Tạo mới**
- Hàm `calculateCrc16Modbus`:
  - Null-check: `if (data == nullptr && len != 0) return 0;` (khớp convention `rf_frame_codec.cpp:63`).
  - Split đa thức thành 2 phép XOR 8-bit `0xA0`/`0x01` hoặc gộp `0xA001` qua `uint16_t` — LSB-first.
- Hàm `appendCrc16Modbus`: ghi `lo(crc)` tại `frame[data_len]`, `hi(crc)` tại `frame[data_len+1]` (little-endian, khớp `SendComCRC16` append `Chr(Lo)` rồi `Chr(Hi)`).
- Hàm `verifyCrc16Modbus`: `return calculateCrc16Modbus(frame, len) == 0;`
- Không cấp phát heap; không `#include` platform/Arduino API — file gọn nhất có thể build ở cả native, ESP32 và AVR.

### TRACK B — Tầng Test (Unity native)

**TASK S1-T3 `test/test_crc16/test_crc16.cpp` — Tạo mới**
- `test_crc16_modbus_standard_vector_4b37`:
  - `const uint8_t input[] = "123456789";`
  - `assert(calculateCrc16Modbus(input, 9) == 0x4B37);`
- `test_crc16_modbus_bigplan_pump_on_vector`:
  - `const uint8_t t[] = {0x04, 0x06, 0x09};` → `0xA7F3`.
  - Verify dry-run: append `0xF3,0xA7` rồi `verifyCrc16Modbus(frame, 5) == true`.
- `test_crc16_modbus_bigplan_pump_off_vector`:
  - `{0x04, 0x07, 0x09}` → `0x37F2`; whole frame → `verifyCrc16Modbus == true`.
- `test_crc16_modbus_append_layout`:
  - `appendCrc16Modbus(buf, 3, cap)` → buf[3]=0x??  buf[4]=0x?? và return 5; `buf[3]==lo`, `buf[4]==hi`.
- `test_crc16_modbus_null_and_zero`:
  - `calculateCrc16Modbus(nullptr, 0)` → khớp quy ước `RfFrameCodec::calculateCrc16(nullptr,0)` (**cho Sprint 2 quyết định trị trả về**; ghi chú trong code test).
  - `calculateCrc16Modbus(nullptr, 10) == 0` (fail-closed).
  - `verifyCrc16Modbus(nullptr, 8) == false`.
- `test_crc16_modbus_tamper_detection`:
  - Set `buffer[1] ^= 0x01` → `verifyCrc16Modbus == false` (dò lỗi 1 bit).
- `test_crc16_modbus_max_length_255`:
  - Mảng 255 byte, không crash, roundtrip append/verify thành công.

**TASK S1-T4 `test/test_crc16/test_crc16_runner.c` — Tạo mới**
- `UNITY_BEGIN(); RUN_TEST(...)` cho toàn bộ test block trên.
- `return UNITY_END();`

### TRACK C — Tầng Build/CI (platformio.ini)

**TASK S1-T5 `platformio.ini` — Sửa cụm `[env:native]`**
- `test_filter = test_production, test_fsm, test_crc16`.

### TRACK D — Tầng Nghiệp vụ (boundary check)

**TASK S1-T6 (Chỉ kiểm chứng, không sửa) `docs/interface-wire-contract.md:312`**
- Soi lại dòng ghi chú `CRC16 ASCII "123456789" = 0x29B1` (CCITT cũ) — đánh dấu sẽ cập nhật trong Sprint 5.
- Tạo bảng mapping cũ → mới trong plan (không commit vào doc ở sprint này).

---

## 4. TIÊU CHUẨN RÀ SOÁT CỨNG

1. **Nhất quán golden vector (S1-HARD-01):** Mọi test vector trong `test_crc16` phải khớp chính xác với vector độc lập từ Big Plan (`0x4B37`, `0xA7F3`, `0x37F2`) và không được phép "sửa vector theo code" — code là thứ phải sửa.
2. **Fail-closed null/bound (S1-HARD-02):** Hàm trả `0`/`false` khi input null với `len>0`; `appendCrc16Modbus` phải từ chối khi `capacity < data_len+2` (trả về 0, không ghi nhớ sang vùng nhớ).
3. **Zero-allocation (S1-HARD-03):** CRC core không được phép gọi `new`/`malloc`/`calloc`; đảm bảo build trên cả native, ESP32 và AVR (`-Os`, `-fno-exceptions`, `-fno-rtti`).
4. **Thuần khiết Domain (S1-HARD-04):** File `core/Crc16Modbus.*` KHÔNG include header Arduino/ESP-IDF/FreeRTOS; nếu vi phạm sẽ fail review.
5. **Hiệu năng vòng lặp (S1-HARD-05):** Không lạm dụng lookup-table 256 phần tử nếu `SRAM` node ATmega8 hạn chế; bitmap đơn giản đủ tốt — đánh dấu `// could be table-based` nếu cần tối ưu sau.

---

## Kết quả dự kiến sau Sprint

- `pio test -e native -f test_crc16` → toàn bộ PASS (0 failure).
- SPR: 0 module nghiệp vụ bị đổi — có bản `Crc16Modbus.*` sẵn sàng cho Sprint 2/3.