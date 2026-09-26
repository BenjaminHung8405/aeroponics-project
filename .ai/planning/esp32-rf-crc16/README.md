# Kế hoạch: ESP32 RF CRC16 Migration

> **Tên Plan:** `esp32-rf-crc16`
> **Trạng thái:** KHỞI TẠO — Baseline Planning (chưa triển khai code)
> **Nguồn tham chiếu chi tiết:** Code hiện tại tại [`rf_frame_codec.cpp`](../../../aeroponics-firmware/src/rf_frame_codec.cpp), [`agu_legacy_codec.cpp`](../../../aeroponics-firmware/src/agu_legacy_codec.cpp); tham chiếu CRC16-Modbus chuẩn từ Big Plan (Delphi `TSCI.CalCRC16/CheckCRC16/SendComCRC16` + Assembly AVR/80x86) được mô tả đầy đủ trong `sprint_1.md` – `sprint_3.md`.

---

## 1. MỤC TIÊU KỸ THUẬT TỔNG QUÁT

Thay thế toàn bộ logic kiểm tra tính toàn vẹn (checksum / CRC) trong dự án **ESP32 control RF** sang **CRC16-Modbus (init `0xFFFF`, đa thức `0xA001`)**, khớp chuẩn với:

- Hàm `TSCI.CalCRC16` / `TSCI.CheckCRC16` / `TSCI.SendComCRC16` trong Delphi (`TestSCI.dpr`);
- Thuật toán Assembly `CalCRC16` (AVR MEGA8) và `CheckCRC16` (80x86) trong Big Plan.

Phạm vi bao gồm **toàn bộ chuỗi RF của hệ thống aeroponics**:

| Tầng | Mô tả | Checksum hiện tại | Mục tiêu sau migration |
|---|---|---|---|
| **RF Frame Codec (ESP32 ↔ ATmega8)** | Header + payload + `HMAC-SHA256 (12-byte tag)` + CRC16 | CRC-16/CCITT-FALSE (poly `0x1021`) | CRC16-Modbus (poly `0xA001`) — giữ nguyên vị trí 2 byte cuối |
| **RF Node (ATmega8)** | Firmware node dùng chung `rf_frame_codec.cpp` khi build `ATMEGA8_NODE_BUILD` | CRC-16/CCITT-FALSE | CRC16-Modbus |
| **AGU Legacy SCI Codec** | Gói tin `[Length][Opcode][Params][Checksum]` | Two's-complement zero-sum (1 byte) | CRC16-Modbus đuôi 2 byte theo mẫu `SendComCRC16` |
| **Debug/Test harness** | `test_production.cpp`, `test_legacy_relay.cpp`, benchmark | Bám theo codec hiện tại | Bám theo CRC16-Modbus |

**Nguyên tắc phạm vi (Scope rules):**
- Không thay đổi cấu trúc khung frame: header SOF/version/type/address/session/sequence/command ID, HMAC tag, thứ tự byte CRC (little-endian: `[crc_lo][crc_hi]`) đều **giữ nguyên** — chỉ thay đổi **thuật toán sinh/kiểm tra CRC**.
- Không thay đổi các checksum **nội bộ lưu trữ** được dùng cho mục đích khác (VD: `checksum_crc32` của calibration profile, EEPROM schedule record checksum) — nằm ngoài phạm vi "control RF".
- Migration đòi hỏi **bump version giao thức** nếu gateway và node phải đổi đồng bộ (xem constraint trong mỗi Sprint).

---

## 2. TECKSTACK CỐT LÕI

| Lớp | Công nghệ |
|---|---|
| **Language (Firmware)** | C++17 (dual-target: ESP-IDF/Arduino cho ESP32-S3; AVR-GCC cho ATmega8) |
| **Embedded Platform** | PlatformIO (`platformio.ini`): `espressif32@^6.5.0`, `atmelavr`/`ATmega8`, Arduino framework |
| **Firmware Frameworks** | FreeRTOS, Arduino `HardwareSerial` cho UART RF, ESP32 NVS `nvs_flash` |
| **RF / UART Transport** | `UartRfTransport`, `IRfTransport` abstraction |
| **Auth layer** | `core/hmac_sha256.cpp` (HMAC-SHA256, 12-byte truncated tag) |
| **Thư viện CRC mục tiêu** | CRC16-Modbus: poly `0xA001`, init `0xFFFF`, reflected, không XOR-out |
| **Test / QA** | Unity (PlatformIO native), `pio test -e native`, host-native `test_filter=test_production,test_fsm` |
| **Debug harness** | `SCIDebugStr` tương đương trong FW (log TX frame hex), `rf_benchmark_runner.cpp` |
| **Backend (out-of-scope migration)** | NestJS (TS) — chỉ xác nhận ingestion policy "bỏ byte checksum sau khi parse", không cần đổi |

---

## 3. QUY TẮC VIẾT CODE TOÀN CỤC (CODING CONVENTIONS)

Bắt buộc đối với **MỌI Agent thực thi Sprint** kế tiếp.

### 3.1 Kiến trúc & SOLID
- **Clean Architecture theo module**: Layer Dependency Rule — `app layer (PumpNodeController)` → `domain layer (RfFrameCodec, AguLegacyCodec)` → `infrastructure layer (UartRfTransport)`; cấm dependency ngược.
- **Single Responsibility**: Mỗi hàm CRC làm đúng 1 việc. Tách riêng 3 hàm: `calculateCrc16(bytes)` (tính), `appendCrc16(frame)` (đóng gói), `verifyCrc16(frame)` (kèm returned boolean) — không gộp.
- **Open/Closed**: Ưu tiên thay thế nội bộ hàm `calculateCrc16` đã có để gọi lại kiểm tra; tạo `*_legacy` giữ nguyên hành vi cũ nếu cần rollback, không sửa interface công khai gây vỡ caller.
- **Interface segregation**: Tầng test không phụ thuộc codec cụ thể; dùng fakes (`FakeRfTransport.h`) thay vì hardcode hardware.
- **Fail-closed**: Khi CRC không khớp, trả lỗi `ParseError::CRC_MISMATCH` (hoặc `false`) — KHÔNG bao giờ fallback "coi như OK".

### 3.2 Đặt tên
- **Hàm:** `calculateCrc16`, `verifyCrc16`, `appendCrc16Trailer`, `SendComCRC16` (Pascal legacy giữ nguyên tên gốc nếu port).
- **Biến:** snake_case cho firmware C++; `crc`, `crc_lo`, `crc_hi`, `frame_len`, `wire_len`.
- **Hằng đa thức:** khai báo tập trung `constexpr uint16_t kModbusPoly = 0xA001; constexpr uint16_t kModbusInit = 0xFFFF;` — cấm magic number rải rác.
- **Enum lỗi:** giữ `ParseError::CRC_MISMATCH`; không thêm enum trùng nghĩa.
- **Test:** `test_<thuật_toán>_<trường_hợp>` (VD: `test_crc16_modbus_standard_vector`, `test_crc16_modbus_null_input`).

### 3.3 Xử lý lỗi & an toàn
- **Không bao giờ dùng CRC như authentication** — CRC chỉ phát hiện lỗi truyền dẫn. HMAC tag là nguồn duy nhất xác thực. (Nhất quán với `docs/QA_ACCEPTANCE_REPORT_4_NODES.md`.)
- **Tất cả tham số con trỏ phải null-check:** `data == nullptr && len != 0` → trả `0`/`false`; không dereference null.
- **Bound-check trước khi ghi:** kiểm tra `len <= 253` cho ShortString-style envelope (giới hạn để tránh tràn byte chiều dài `0xFF`).
- **Không log CRC dạng plaintext khi không cần**; dùng log hex track chuẩn như `SCIDebugStr` format.
- **Message có CRC sai phải bị drop** và đếm vào `UartTransportStats::crc_errors` (không im lặng).

### 3.4 Quy tắc chip / vòng lặp
- Không dùng code dành riêng cho native (glibc) lọt vào source build `ATMEGA8_NODE_BUILD`.
- Tránh cấp phát heap trong hot path giải mã. CRC phải dùng stack/int.
- Thêm guard `#if defined(ESP_PLATFORM) || defined(ARDUINO)` khi cần API nền tảng.

---

## 4. NHỮNG FILE/CẤU TRÚC DỰ KIẾN TRONG KHÔNG GIAN NÀY

| File | Mục đích |
|---|---|
| `README.md` | File này — baseline kế hoạch |
| `sprint_1.md` | Sprint 1: Standardize thuật toán CRC16-Modbus + vector test & golden reference |
| `sprint_2.md` | Sprint 2: Migrate `RfFrameCodec` (gateway TX/RX + node build) |
| `sprint_3.md` | Sprint 3: Migrate AGU legacy SCI codec (`agu_legacy_codec`, `agu_legacy_rf_host`, legacy relay test) |
| `sprint_4.md` | Sprint 4: PumpNodeController + integration verify + regression |
| `sprint_5.md` | Sprint 5: Docs, benchmark, release gate & rollback |
