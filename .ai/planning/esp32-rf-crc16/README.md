# Kế hoạch: ESP32 RF CRC16 Migration

> **Tên Plan:** `esp32-rf-crc16`
> **Trạng thái:** Implementation complete (2026-09-27). Field/hardware release gates pending.
> **Nguồn tham chiếu chi tiết:** Code hiện tại tại [`rf_frame_codec.cpp`](../../../aeroponics-firmware/src/rf_frame_codec.cpp), [`agu_legacy_codec.cpp`](../../../aeroponics-firmware/src/agu_legacy_codec.cpp); tham chiếu CRC16-Modbus chuẩn từ Big Plan (Delphi `TSCI.CalCRC16/CheckCRC16/SendComCRC16` + Assembly AVR/80x86) được mô tả đầy đủ trong `sprint_1.md` – `sprint_3.md`.

---

## 1. MỤC TIÊU KỸ THUẬT TỔNG QUÁT

Thay thế toàn bộ logic kiểm tra tính toàn vẹn (checksum / CRC) trong dự án **ESP32 control RF** sang **CRC16-Modbus (init `0xFFFF`, đa thức `0xA001`)**, khớp chuẩn với:

- Hàm `TSCI.CalCRC16` / `TSCI.CheckCRC16` / `TSCI.SendComCRC16` trong Delphi (`TestSCI.dpr`);
- Thuật toán Assembly `CalCRC16` (AVR MEGA8) và `CheckCRC16` (80x86) trong Big Plan.

Phạm vi bao gồm **toàn bộ chuỗi RF của hệ thống aeroponics**:

| Tầng | Mô tả | Checksum hiện tại | Mục tiêu sau migration |
|---|---|---|---|
| **Modern RF Frame Codec (ESP32 gateway/model)** | Header + payload + `HMAC-SHA256 (16-byte tag)` + CRC16 | Pre-v2 CRC-16/CCITT-FALSE (poly `0x1021`) | CRC16-Modbus (init `0xFFFF`, poly `0xA001`) — giữ nguyên vị trí 2 byte cuối |
| **RF Node (ATmega8) build/model** | Firmware node dùng chung `rf_frame_codec.cpp` khi build `ATMEGA8_NODE_BUILD` | Pre-migration CRC-16/CCITT-FALSE | CRC16-Modbus; không phải bằng chứng node legacy deployed đã đổi wire |
| **AGU Legacy SCI Codec** | Gói tin `[Length][Opcode][Params][crc_lo][crc_hi]` | **Đã migrate sang CRC16-Modbus** theo Delphi `SendComCRC16` / AVR assembly. `Length = payloadLen + 2`, CRC phủ `Length` + payload, trailer little-endian. Vectors: `04 06 09 F3 A7` (PUMP_ON node 9), `04 07 09 F2 37` (PUMP_OFF node 9). Zero-sum 1-byte bị reject. Xem `docs/LEGACY_WIRE_EVIDENCE.md` |
| **Debug/Test harness** | `test_production.cpp`, `test_legacy_relay.cpp`, benchmark | Bám theo codec hiện tại | Bám theo CRC16-Modbus |

**Nguyên tắc phạm vi (Scope rules):**
- Không thay đổi cấu trúc khung frame: header SOF/version/type/address/session/sequence/command ID, HMAC tag, thứ tự byte CRC (little-endian: `[crc_lo][crc_hi]`) đều **giữ nguyên** — chỉ thay đổi **thuật toán sinh/kiểm tra CRC**.
- Không thay đổi các checksum **nội bộ lưu trữ** được dùng cho mục đích khác (VD: `checksum_crc32` của calibration profile, EEPROM schedule record checksum) — nằm ngoài phạm vi "control RF".
- Migration đòi hỏi **bump version giao thức** nếu gateway và node phải đổi đồng bộ (xem constraint trong mỗi Sprint).

### 1.1. Quy hoạch NodeID / GroupID RF (bổ sung)

Địa chỉ RF phải được chốt độc lập với `group_id` nghiệp vụ của backend:

| Phạm vi | Giá trị chuẩn | Ý nghĩa |
|---|---|---|
| `nodeID` | Hex `0x1..0xF` (`[1,2,3,4,5,6,7,8,9,A,B,C,D,E,F]`) | 15 node vật lý; `0x0` dành riêng cho gateway |
| `groupID` trên RF | Hex `[0x10, 0x14, 0x18, 0x1C]` | Group address: high nibble `0x1`, low nibble là base của block 4 địa chỉ |
| `group_id` control-plane | Decimal `[1..4]` | Logical group ID trong MQTT/API/database; không serialize trực tiếp thành RF group address |

Mapping baseline — formula đã xác nhận bằng legacy Delphi (`AGU-Aeroponics/TestSCI.dpr:340`: `gid == $14 [4,5,6,7]`):

```text
groupID = 0x10 | (nodeID & 0x0C)
```

| Logical group | RF `groupID` | Formula | Node members | Legacy proof |
|---|---:|---|---|---|
| 1 | `0x10` | `0x10 \| (nodeID & 0x0C)` | `0x1..0x3` |  |
| 2 | `0x14` | `0x10 \| (nodeID & 0x0C)` | `0x4..0x7` | `TestSCI.dpr:340` gid `$14` = nodes 4..7 |
| 3 | `0x18` | `0x10 \| (nodeID & 0x0C)` | `0x8..0xB` |  |
| 4 | `0x1C` | `0x10 \| (nodeID & 0x0C)` | `0xC..0xF` |  |

**Quy tắc triển khai:**

- `nodeID` là địa chỉ đơn node; `groupID` RF là địa chỉ multicast/broadcast, không được dùng như node ID.
- `RfFrameCodec` phải phân biệt `source_node_id` (chỉ gateway/node) và `target_node_id` (gateway/node/group address); không cho phép node gửi với source là group address.
- CRC16 tính trên wire bytes sau khi địa chỉ đã được encode, vì vậy mọi thay đổi address validation/routing phải có golden frame và test CRC round-trip tương ứng.
- Backend/API vẫn giữ logical `group_id` `1..4`; chỉ gateway/firmware map logical group sang RF `groupID` khi tạo frame.
- **Không** phải phép nhân `nodeID × $F8`; `$F8` không xuất hiện trong legacy Delphi source. Mask đúng cho block là `0x0C`, ghép với prefix `0x10`.

---

## 2. TECKSTACK CỐT LÕI

| Lớp | Công nghệ |
|---|---|
| **Language (Firmware)** | C++17 (dual-target: ESP-IDF/Arduino cho ESP32-S3; AVR-GCC cho ATmega8) |
| **Embedded Platform** | PlatformIO (`platformio.ini`): `espressif32@^6.5.0`, `atmelavr`/`ATmega8`, Arduino framework |
| **Firmware Frameworks** | FreeRTOS, Arduino `HardwareSerial` cho UART RF, ESP32 NVS `nvs_flash` |
| **RF / UART Transport** | `UartRfTransport`, `IRfTransport` abstraction |
| **Auth layer** | `core/hmac_sha256.cpp` (HMAC-SHA256, `HMAC_TAG_SIZE=16`) |
| **Thư viện CRC mục tiêu** | CRC16-Modbus: poly `0xA001`, init `0xFFFF`, reflected, không XOR-out |
| **Test / QA** | Unity (PlatformIO native), `pio test -e native`, host-native `test_filter=test_production,test_fsm` |
| **Debug harness** | `SCIDebugStr` tương đương trong FW (log TX frame hex), `rf_benchmark_runner.cpp` |
| **Backend (out-of-scope migration)** | NestJS (TS) — chỉ xác nhận ingestion policy "bỏ byte checksum sau khi parse", không cần đổi |

**Modern wire invariant:** `RF_PROTOCOL_VERSION=0x02`; layout is `[SOF/header][payload][HMAC_TAG 16B][CRC_LO][CRC_HI]`. CRC16-Modbus covers header + payload + HMAC tag, excluding the two-byte trailer. CCITT-FALSE remains only for treatment-storage compatibility and explicitly labeled legacy history.

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
| `EXECUTION_MASTER_PLAN.md` | Master orchestration: baseline facts, dependency graph, risk register, quality gates, commit strategy và timeline |
| `sprint_0.md` | Phase 0: Baseline remediation — sửa `test_filter`, triage 97 failure `test_production`, tạo rollback anchor |
| `sprint_1.md` | Sprint 1: Standardize thuật toán CRC16-Modbus + vector test & golden reference |
| `sprint_2.md` | Sprint 2: Migrate `RfFrameCodec` (gateway TX/RX + node build) |
| `sprint_3a.md` | Sprint 3A: Characterize legacy AGU wire bằng capture thật trước khi đổi codec |
| `sprint_3.md` | Sprint 3B: Migrate AGU legacy SCI codec (`agu_legacy_codec`, `agu_legacy_rf_host`, legacy relay test) |
| `sprint_4.md` | Sprint 4: PumpNodeController + integration verify + regression |
| `sprint_5.md` | Sprint 5: Docs, benchmark, release gate & rollback |
| `sprint_6.md` | Sprint 6: Field rollout, observability, rollback binary và post-deploy verification |
| `node_group_scheme.md` | Thiết kế địa chỉ RF NodeID/GroupID, mapping và migration checklist |

### 4.1 Thứ tự triển khai

```text
Sprint 0 (baseline) ─┐
Sprint 1 (utility)   ─┼─> Sprint 2 (codec) -> Sprint 3A (evidence) -> Sprint 3B (legacy code)
Sprint 0 song song   ─┘                                             |
                                                                    v
                                          Sprint 4 (integration) -> Sprint 5 (release gate)
                                                                    |
                                                                    v
                                                    Sprint 6 (field rollout & observability)

Track F (NodeID/GroupID expansion) - DEFERRED sau Sprint 5, xem node_group_scheme.md
```
