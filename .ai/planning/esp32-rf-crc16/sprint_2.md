# Sprint 2: Migrate `RfFrameCodec` sang CRC16-Modbus (gateway TX/RX + node build)

> **Phụ thuộc:** Sprint 1 hoàn thành (`core/Crc16Modbus.*`, golden vectors PASS).
> **Risk cao nhất:** đổi algorithm CRC ở lớp codec dùng chung cho ESP32 và ATmega8 node → phải đổi **cùng lúc** cả 2 đầu, kèm bump version nếu giữ chain cũ.
> **Nguyên tắc:** thay đổi nhỏ nhất có kiểm soát — giữ nguyên `ParseError::CRC_MISMATCH`, giữ nguyên vị trí 2 byte CRC cuối frame (little-endian `[crc_lo][crc_hi]`), chỉ đổi thuật toán sinh/kiểm tra.
> **Scope boundary S2-ADDR-00:** Sprint 2 **không đổi topology/address validation**. `RfFrameCodec` tiếp tục nhận gateway hoặc production node `4..7`; KHÔNG thêm RF group target `0x10/0x14/0x18/0x1C` vào modern frame trong sprint này. Group address `$14` đã được xác nhận thuộc AGU Legacy SCI (`TestSCI.dpr:340`) và thuộc phạm vi Sprint 3. Mở rộng modern RF address space là task routing riêng, không gộp vào CRC migration.

---

## 1. PHẠM VI & MỤC TIÊU

### 1.1 Modules / Files tác động

| File | Hành động | Tác động chính |
|---|---|---|
| `aeroponics-firmware/include/rf_frame_codec.h` | **Sửa** | `calculateCrc16` giữ signature; bổ sung hằng ghi chú đa thức; (tuỳ chọn) alias `calculateCrc16Modbus` |
| `aeroponics-firmware/src/rf_frame_codec.cpp` | **Sửa** | Thân `calculateCrc16` đổi sang Modbus; `encodeFrame`/`decodeFrameDetailed` giữ logic gọi |
| `aeroponics-firmware/include/config.h` | **Sửa** | Bump `RF_PROTOCOL_VERSION` (0x02); **không** đổi `RF_MAX_NODE_ID` / production address guard trong Sprint 2 |
| `aeroponics-firmware/include/pump_node_controller.h` | **Sửa** | Router `calculateCrc16` chuyển hướng tới Modbus (nếu giữ delegate) |
| `aeroponics-firmware/include/treatment_manager.h` | **Sửa** | Dừng phụ thuộc `RfFrameCodec::calculateCrc16` — coi là internal NVS checksum (xem Task S2-T6) |
| `aeroponics-firmware/test/test_production/test_production.cpp` | **Sửa** | Thay `test_rf_crc16_ccitt_false_standard_test_vector` bằng vector Modbus |
| `aeroponics-firmware/src/atmega8_node_main.cpp` | **Lưu ý** | Không sửa logic — chỉ verify build env `atmega8-node-*` PASS |

### 1.2 Mục tiêu cụ thể

- [ ] `RfFrameCodec::calculateCrc16("123456789")` trả `0x4B37` (thay vì `0x29B1`).
- [ ] `encodeFrame(SET_PUMP)` sinh frame khớp golden: CRC vẫn nằm 2 byte cuối, temp tính trên `[header + payload + HMAC tag]`.
- [ ] `decodeFrameDetailed` vẫn trả `CRC_MISMATCH` khi sửa 1 bit dữ liệu / khi CRC đuôi buffer sai.
- [ ] Với buffer chứa frame hợp lệ, `calculateCrc16(frame, len-2) == readU16Le(frame+len-2)`.
- [ ] Env `[env:native]` + `[env:atmega8-node]` build + test PASS.
- [ ] `treatment_manager` tách khỏi codec RF (không đổi cấu trúc lưu trữ NVS).
- [ ] Address invariant giữ nguyên: modern `RfFrameCodec` chỉ nhận gateway hoặc production node `4..7`; group address chưa được enable.

---

## 2. KIẾN TRÚC & LUỒNG DỮ LIỆU

### 2.1 Dòng gọi hiện tại (trước migration)

```
PumpNodeController::buildFrame / ::verifyCrcAndMac
     └── RfFrameCodec::encodeFrame / decodeFrameDetailed
             ├── encodeHeader()            → header wire (17 bytes)
             ├── encodePayload()           → payload wire (type-specific)
             ├── HmacSha256::calculateTruncated(psk, header+payload) → 12-byte tag
             └── calculateCrc16(header+payload+tag)  ← [SPRINT 2 ĐỔI Ở ĐÂY]
                     ├── cũ: CCITT-FALSE 0x1021 MSB-first
                     └── mới: Modbus 0xA001 LSB-first
             └── frame = [header][payload][hmac_tag(12)][crc_lo][crc_hi]
```

### 2.2 Luồng sau migration (chỉ thay khối thuật toán)

```
encodeFrame:
  signed_len = RF_HEADER_SIZE + wire_payload_len                 // 17 + payload
  crc = calculateCrc16(buffer, signed_len + HMAC_TAG_SIZE)      // đổi sang Modbus
  writeU16Le(buffer + signed_len + HMAC_TAG_SIZE, crc)          // OK giữ nguyên

decodeFrameDetailed:
  if calculateCrc16(frame, signed_len + HMAC_TAG_SIZE)
        != readU16Le(frame + signed_len + HMAC_TAG_SIZE):
       return ParseError::CRC_MISMATCH                          // fail-closed
```

> Chú ý: CRC tính **trên toàn bộ `header+payload+hmac_tag`**, KHÔNG gồm 2 byte CRC — không nhầm với chuỗi ShortString có chứa byte `[len]`. Nhưng thuật toán Modbus vẫn cần **XOR byte thấp trước** (LSB-first) khác với CCITT cũ (MSB-first). Đây là nguồn bug phổ biến khi port — các test vector Sprint 1 sẽ bắt.

### 2.3 Khả năng tương thích ngược / bump version

- Do node ATmega8 firmware và gateway ESP32 cùng source repo, đổi đồng bộ 1 lần.
- `RF_PROTOCOL_VERSION` bump lên `0x02` để chặn node cũ (firmware CCITT) lọt vào gateway mới và ngược lại → tránh trạng thái "gateway mới gửi CRCM mismatch với node cũ".
- TM-Window: 2 bản release có thể tồn tại song song trong quá trình OTA; version gate là bảo vệ quyết định.

---

## 3. PHÂN RÃ CHI TIẾT TÁC VỤ

> Task-id: `S2-T<n>`. Không gộp "đổi algorithm + đổi tests + đổi version + tách storage" vào 1 task.

### TRACK A — Tầng Data (codec)

**TASK S2-T1 `src/rf_frame_codec.cpp` — Hàm `RfFrameCodec::calculateCrc16`**
- Thay toàn bộ thân hàm (dòng 62-73) gọi tới `calculateCrc16Modbus(data, len)` (include `core/Crc16Modbus.h`).
- Giữ null-guard hiện có: `if (data == nullptr && len != 0) return 0;` (kiểm tra xem utility đã trả tương đương chưa — nếu `nullptr,0` khác, cập nhật đồng bộ).
- **KHÔNG** đổi signature trong header — tránh vỡ 3 caller (encodeFrame, decodeFrameDetailed, pump_node_controller).

**TASK S2-T2 `include/rf_frame_codec.h` — Document clamp**
- Bổ sung comment block trên `calculateCrc16` ghi rõ: `@brief CRC-16/MODBUS (init 0xFFFF, poly 0xA001, reflected) — since RF_PROTOCOL_VERSION 0x02`.
- (Tuỳ chọn) thêm `static uint16_t calculateCrc16Modbus(...)` alias để Peel các gọi mới sạch hơn; giữ alias cũ để tương thích.

### TRACK B — Tầng Nghiệp vụ (delegates)

**TASK S2-T3 `include/pump_node_controller.h` — `PumpNodeController::calculateCrc16`**
- Delegate hiện `return RfFrameCodec::calculateCrc16(data,len);` — **giữ nguyên** vì Sprint 2 đã đổi ở codec, tự động theo Modbus.
- Kiểm tra không có chỗ nào trong `pump_node_controller.cpp` build CRC trực tiếp với constant CCITT riêng. Nếu phát hiện (grep `0x1021`) → tách task con.

**TASK S2-T4 `src/pump_node_controller.cpp` — `verifyCrcAndMac` / `buildFrame`**
- Verify `const uint16_t expected_crc = calculateCrc16(frame_data, crc_check_len);` (dòng 242) vẫn tính đúng `crc_check_len = frame_len - 2`.
- Không đổi thứ tự kiểm tra CRC trước HMAC (bảo toàn fail-fast ordering hiện tại).

### TRACK C — Tầng Version/Persistence boundary

**TASK S2-T5 `include/config.h` — bump `RF_PROTOCOL_VERSION`**
- Từ `0x01` lên `0x02`; đảm bảo `decodeFrameDetailed` dùng đúng constant (không hardcode).
- Không sửa `RF_MAX_NODE_ID`, `RF_PRODUCTION_MIN_NODE_ID`, `RF_PRODUCTION_MAX_NODE_ID` hoặc `isProductionNodeId` — thuộc task addressing riêng.

**TASK S2-T6 `include/treatment_manager.h` — tách internal checksum**
- Nguyên tắc: checksum NVS của treatment snapshot KHÔNG thuộc RF wire → **không phép vô tình đổi** vì Sprint 2 đổi codec.
- Thay hàm `computeChecksum()` (dòng 60) dùng helper riêng trong module: `static uint16_t calculateStorageCrc16(...)` (giữ CCITT-FALSE hoặc dùng chính `calculateCrc16Modbus` **nhưng ghi rõ** đây là 1 quyết định — chọn giữ CCITT để bảo toàn snapshot cũ).
- Nếu team quyết định migrate storage luôn: tạo Task riêng (không merge vào Sprint 2) kèm migration script — **ưu tiên giữ nguyên trong sprint này**.

### TRACK D — Tầng Test/QA

**TASK S2-T7 `test/test_production/test_production.cpp` — thay vector CRC**
- Dòng 2252: `test_rf_crc16_ccitt_false_standard_test_vector` → đổi input `"123456789"` kỳ vọng `0x4B37`; đổi tên hàm thành `test_rf_crc16_modbus_standard_vector`.
- Dòng 2259-2264: cập nhật `RfFrameCodec::calculateCrc16(nullptr,0)` và `nullptr,10` theo quy ước đã chốt ở Sprint 1.
- Dòng 6757 (`standard_ascii`) & 8124 (`hmac_corrupt`): chuyển sang compute Modbus trước khi so sánh bug — đảm bảo các frame fixture để encode baseline bị recalc (nếu fixture là hằng số hex, REGENERATE từ `appendCrc16Modbus`).

**TASK S2-T8 `test/test_production/test_production.cpp` — integration fixtures**
- Rà thêm các fixture frame hex tĩnh trong `test_production.cpp` (RF encode/decode roundtrip) — mọi chỗ có `0x29B1` hoặc CRC hex cứng phải được đổi.
- Thêm test: sửa 1 bit trong payload → `decodeFrameDetailed` trả `CRC_MISMATCH`.
- Giữ fixture address ở production node range hiện tại; KHÔNG thêm group target `0x10/0x14/0x18/0x1C` vào Sprint 2.

### TRACK E — Tầng Build (node + filter)

**TASK S2-T9 `platformio.ini`** *(nếu cần)*
- Không thêm env mới; chỉ verify filter `test_production, test_fsm, test_crc16` đã bao gồm module mới.

**TASK S2-T10 Build & smoke**
- `pio run -e esp32-s3-devkitc-1`, `pio run -e atmega8-node-4`, `pio test -e native` — toàn bộ PASS.

---

## 4. TIÊU CHUẨN RÀ SOÁT CỨNG

1. **Bump version đồng bộ (S2-HARD-01):** `RF_PROTOCOL_VERSION` phải tăng khi đổi CRC ở codec dùng chung; không deploy gateway mới cho node cũ chưa update trong cùng window.
2. **Fail-closed decode (S2-HARD-02):** Mọi đường nhận CO THỂ trả `CRC_MISMATCH` và **phải drop frame**; không bao giờ đưa frame CRC sai vào `handleIncomingFrame`.
3. **Bound & null (S2-HARD-03):** Signature `calculateCrc16` giữ null-guard `nullptr` → trả `0`, không deref; `decodeFrameDetailed` vẫn kiểm tra `frame_len` đủ tối thiểu trước khi đọc CRC cuối.
4. **ATmega8 giới hạn (S2-HARD-04):** Dung lượng Flash/SRAM node phải được kiểm tra qua script `post:scripts/check_atmega8_size.py`; thêm include `core/Crc16Modbus.*` vào `build_src_filter` của env `atmega8-node` nếu chưa có.
5. **Không đổi wire-layout (S2-HARD-05):** Thứ tự `[crc_lo][crc_hi]` little-endian ở cuối frame bắt buộc giữ nguyên — test `appendCrc16Modbus` + `readU16Le` roundtrip.
6. **Storage checksum cô lập (S2-HARD-06):** If `treatment_manager` vẫn gọi chung codec → fail review; nó phải dùng helper riêng hoặc đã có quyết định migrate riêng ghi rõ trong PR.
7. **Address scope freeze (S2-ADDR-00):** Sprint 2 chỉ đổi CRC/version. Mọi thay đổi `isValidAddress`, node range, hoặc group target → tách task, không merge Sprint 2.

---

## Kết quả dự kiến sau Sprint

- CRC trên RF wire = Modbus (gateway + node). 
- Trọn bộ `pio test -e native` + `pio test -e native-integration` + `pio run -e atmega8-node-*` PASS.
- Không regression ở `treatment_manager` / NVS persistence.
