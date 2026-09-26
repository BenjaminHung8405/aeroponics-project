### [2026-09-26 04:49:28 UTC] Task B1 & B2 — CRC-16/Modbus Unity Native Test Suite (Sprint 1: Chuẩn hóa thuật toán CRC16-Modbus + Golden Vectors), chờ QA Review

- **Thời gian thực hiện:** 2026-09-26 04:49:28 UTC
- **Task ID:** **B1, B2** (Track B — Test Layer (Unity Native), Sprint 1)
- **Trạng thái hiện tại:** **Đang chờ QA Review** (`[ ] QA Review`)
- **Danh sách file đã tạo mới / sửa đổi:**
  - `[CREATED]` `aeroponics-firmware/test/test_crc16/test_crc16.cpp` (Suite unit test Unity: 9 test case gồm golden vectors, append layout, fail-closed null/boundary, anti-tamper single-bit flip toàn bộ vị trí, và roundtrip max frame 255 byte)
  - `[CREATED]` `aeroponics-firmware/test/test_crc16/test_crc16_runner.c` (Unity runner native: `UNITY_BEGIN()` → 9 × `RUN_TEST()` → `return UNITY_END();`, khai báo `extern "C"` linkage với các test body, không phụ thuộc phần cứng)
  - `[MODIFIED]` `.ai/planning/esp32-rf-crc16/PROGRESS.md` (Cập nhật Task B1, B2: `Pending` → `In Progress` → `QA Review`; cập nhật Last Updated / Current Phase)
- **Giải trình ngắn gọn về giải pháp logic đã viết và kết quả tự kiểm tra mã nguồn:**
  1. **B1 — Test cases (`test_crc16.cpp`):** Test theo chiến lược TDD với giá trị độc lập từ Big Plan, khẳng định chính xác từng byte — không "sửa vector cho khớp code" (S1-HARD-01):
     - Golden vectors: `"123456789"` → `0x4B37`; `{0x04,0x06,0x09}` → `0xA7F3` (append LE `{0xF3,0xA7}` + verify true); `{0x04,0x07,0x09}` → `0x37F2` (append LE `{0xF2,0x37}` + verify true).
     - Append layout: `appendCrc16Modbus` trả `data_len + 2`, ghi `frame[3]=lo`, `frame[4]=hi`.
     - Edge cases (S1-HARD-02): `calculateCrc16Modbus(nullptr,0) == 0xFFFF` (quy ước empty), `(nullptr,10) == 0` (fail-closed), `verifyCrc16Modbus(nullptr,8) == false`; `verify` từ chối `len < 2`; `append` từ chối `capacity < data_len + 2` (kể cả `data_len > capacity` không underflow) và không ghi chìm guard byte.
     - Anti-tamper: duyệt đủ `5 × 8 = 40` phép lật 1-bit trên frame chuẩn, mọi trường hợp đều `verify == false`.
     - Boundary `len=255`: payload 253 + CRC 2 roundtrip append/verify PASS; payload rỗng append `{0xFF,0xFF}` cũng verify PASS.
  2. **B2 — Runner (`test_crc16_runner.c`):** Khai báo 9 prototype `void test_...(void)` với C linkage (test body dùng `extern "C"` trong `.cpp`), `main(void)` gọi `UNITY_BEGIN()`, `RUN_TEST()` cho cả 9 case, trả về `UNITY_END()` — host native, không chạy trên phần cứng.
  3. **Kết quả tự kiểm tra mã nguồn:**
     - `pio test -e native -f test_crc16` → **9/9 PASSED** (0 failure), build 0 warning.
     - Không sửa `platformio.ini`, `rf_frame_codec.cpp`, hay `agu_legacy_codec.cpp` (Task C1 và Sprint constraint còn nguyên vẹn).
     - `git status`: chỉ có 2 file mới `test/test_crc16/` + 1 file planning đã sửa (`PROGRESS.md`); không có thay đổi file production nào.
- **Lưu ý:** Task C1 (`test_filter` của `[env:native]`) chưa chạy vì thuộc Track C; suite `test_crc16` chỉ được chạy bằng `-f` filter riêng trong lần kiểm chứng này.

### [2026-09-26 04:16:31 UTC] Task A1 & A2 — CRC-16/Modbus Pure Domain Utility (Sprint 1: Chuẩn hóa thuật toán CRC16-Modbus + Golden Vectors), chờ QA Review

- **Thời gian thực hiện:** 2026-09-26 04:16:31 UTC
- **Task ID:** **A1, A2** (Track A — Domain Layer (Pure Utility), Sprint 1)
- **Trạng thái hiện tại:** **Đang chờ QA Review** (`[ ] QA Review`)
- **Danh sách file đã tạo mới / sửa đổi:**
  - `[CREATED]` `aeroponics-firmware/include/core/Crc16Modbus.h` (Header khai báo 3 hàm pure utility: `calculateCrc16Modbus`, `appendCrc16Modbus`, `verifyCrc16Modbus`; constexpr constants `kCrc16ModbusInitialValue = 0xFFFF`, `kCrc16ModbusPolynomial = 0xA001`; `#pragma once`; chỉ include `<cstddef>` và `<cstdint>`)
  - `[CREATED]` `aeroponics-firmware/src/core/Crc16Modbus.cpp` (Cài đặt bitwise LSB-first reflected CRC-16/Modbus; fail-closed null/boundary checks; little-endian append; verify dùng Modbus remainder-zero property; không heap allocation; không platform-specific includes)
  - `[MODIFIED]` `.ai/planning/esp32-rf-crc16/PROGRESS.md` (Cập nhật Task A1, A2: `Pending` → `In Progress` → `QA Review`)
- **Giải trình ngắn gọn về giải pháp logic đã viết và kết quả tự kiểm tra mã nguồn:**
  1. **A1 — Header (`Crc16Modbus.h`):** Khai báo hằng `kCrc16ModbusInitialValue = 0xFFFF`, `kCrc16ModbusPolynomial = 0xA001`, `kCrc16ModbusSize = 2` và 3 hàm thuần (pure functions, zero state). Toàn bộ doxygen params/return/invariant rõ ràng. Không có Arduino/ESP-IDF/FreeRTOS include — thỏa S1-HARD-04.
  2. **A2 — Implementation (`Crc16Modbus.cpp`):**
     - `calculateCrc16Modbus`: Fail-closed `data == nullptr && len != 0 → return 0`; init `0xFFFF`, XOR byte → loop 8 bit: `(crc & 1) ? ((crc >> 1) ^ 0xA001) : (crc >> 1)`. Zero heap allocation (S1-HARD-03). Loop bit-by-bit không dùng 256-byte LUT (S1-HARD-05).
     - `appendCrc16Modbus`: `frame == nullptr || data_len > capacity || capacity - data_len < 2 → return 0`. Ghi CRC_Lo tại `frame[data_len]`, CRC_Hi tại `frame[data_len+1]` (little-endian). Return `data_len + 2` trên success.
     - `verifyCrc16Modbus`: `frame == nullptr || len < 2 → false`. Dùng Modbus remainder-zero property: `calculateCrc16Modbus(frame, len) == 0` — khớp Delphi `TSCI.CheckCRC16`.
  3. **Golden vectors verified (S1-HARD-01):**
     - `"123456789"` → `0x4B37` ✓
     - `{0x04, 0x06, 0x09}` → `0xA7F3` (append LE `{0xF3, 0xA7}`) ✓
     - `{0x04, 0x07, 0x09}` → `0x37F2` (append LE `{0xF2, 0x37}`) ✓
  4. **Kết quả tự kiểm tra mã nguồn:**
     - 11/11 unit tests独立 PASS: golden vectors x3, empty buffer, null+nonzero fail-closed, append LE layout, append insufficient capacity, verify good frame, verify single-bit flip, verify nullptr, verify len<3.
     - Max payload roundtrip 253+2=255 bytes PASS; tamper detection 13/13 sampled positions PASS.
     - Compile strict: `-std=c++17 -Os -fno-exceptions -fno-rtti -Wall -Wextra -Werror` → 0 errors, 0 warnings (both ATMEGA8-profile and native profiles).
     - Không đụng vào `rf_frame_codec.cpp` hoặc `agu_legacy_codec.cpp` (sprint constraint).
