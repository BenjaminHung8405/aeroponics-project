# Sprint 3: Migrate AGU Legacy SCI Codec sang CRC16-Modbus (Theo SendComCRC16)

> **Phụ thuộc:** Sprint 1 (Crc16Modbus utility) & Sprint 2 (RfFrameCodec).
> **Phạm vi trọng tâm:** Những hàm encode/decode trong `agu_legacy_codec.cpp` (tạo mảng lệnh `[Len][Cmd][Params...][Checksum]`), host `agu_legacy_rf_host.cpp`, và test mock trong `test/test_prototype/test_legacy_relay.cpp`.
> **Lưu ý quan trọng:** mã gốc zero-sum (tổng modulo 256 = 0) là 1 byte, CRC16-Modbus theo SendComCRC16 là 2 byte `[crc_lo][crc_hi]` sau envelope `[len=payloadLen+2]`. Kích thước frame đổi nên cần cập nhật `expectedResponseSize` trong host & decoder.
> **Addressing note (quan trọng):** Legacy SCI dùng **cùng một byte address** cho cả node lẫn group target. Legacy Delphi xác nhận `PUMP_ON + $14` nghĩa là `gid == $14 [4,5,6,7]` (`TestSCI.dpr:340`) — tức group address thật sự tồn tại trên wire legacy. Mapping: `groupID = 0x10 | (nodeID & 0x0C)`. Sprint 3 **không** suy diễn semantics này sang modern `RfFrameCodec`.

---

## 1. PHẠM VI & MỤC TIÊU

### 1.1 Modules / Files tác động

| File | Hành động | Lý do |
|---|---|---|
| `aeroponics-firmware/include/agu_legacy_codec.h` | **Sửa** | Thay đổi comment "zero-sum checksum", bổ sung `@brief CRC16-Modbus (SendComCRC16)`, khai báo hàm `calculateCrc16Modbus`, `appendCrc16ToSendCom`, `verifyCrc16SendCom` |
| `aeroponics-firmware/src/agu_legacy_codec.cpp` | **Sửa** | Thay `calculateZeroSumChecksum`/`verifyZeroSumChecksum` bằng `calculateCrc16Modbus`/`appendCrc16ToSendCom`/`verifyCrc16SendCom`. Cập nhật `encode*` để dùng 2 byte CRC. |
| `aeroponics-firmware/src/agu_legacy_rf_host.cpp` | **Sửa** | Cập nhật `expected response size` (9 byte -> 11 byte cho burst response envelope `[len][8 data][crc_lo][crc_hi]`); đệm đọc đủ 11 byte. |
| `aeroponics-firmware/test/test_prototype/test_legacy_relay.cpp` | **Sửa** | Cập nh kỳ vọng size response, hex test vector bytes CRC (F3 A7/F2 37 thay vì byte checksum). |
| `aeroponics-firmware/include/agu_legacy_rf_host.h` | **Sửa** | Thêm kích thước frame mới constant. |
| `aeroponics-firmware/test/test_crc16/test_crc16.cpp` | **Lưu ý** | Vì hàm test vector CRC Modbus đã có ở Sprint 1, nhưng task này chỉ dùng choverify phần decode mới — trùng trùng không được merge tính năng CRC (cấm). |
| `AGU-Aeroponics/TestSCI.dpr` | **Tham chiếu read-only** | Dòng 340 chứng minh `gid == $14` ↔ nodes `4..7`; không sửa tool Delphi trong sprint firmware. |

> ⚠️ **Mục tiêu:** Mỗi `encode*` và `decode*` phải tạo ra / nhận envelope chuẩn `SendComCRC16` — ví dụ PUMP_ON node 9 → frame `[0x04, 0x06, 0x09, 0xF3, 0xA7]` (len=04, payloadLen=2).

### 1.2 Mục tiêu cụ thể

- [ ] Hàm `formatSendComPacket` cũ -> `formatSendComCrc16Packet` trả frame `[len=payloadLen+2][payload...][crc_lo][crc_hi]` (tổng dài `payloadLen + 3` byte).
- [ ] `AguLegacyCodec::encodePumpOn(nodeId=9)` encode ra frame `04 06 09 F3 A7` (5 byte) — payload 2 byte: byte len = 2+2 = 4, frame tổng = 1 + 2 + 2 = 5 byte (dài hơn mẫu zero-sum cũ đúng 1 byte).
- [ ] `AguLegacyCodec::decodeBurstRam` sau migration nhận 11 byte `[len][8 data][crc_lo][crc_hi]` (chốt quyết định: envelope LUÔN kèm byte `[len]` cho khớp `SendComCRC16`), thay cho 9 byte cũ `[8 data][1 checksum]`.
- [ ] `AguLegacyRfHost::transact` khi đọc burst response sẽ đệm đủ 11 byte (1 len + 8 data + 2 CRC) và verify CRC trước khi nhận dữ liệu; ACK 1-byte `0x5A` cho PING/PUMP giữ nguyên.
- [ ] Golden vector `0xA7F3` / `0x37F2` khớp với phần CRC Modbus ở Sprint 1.
- [ ] Address byte giữ nguyên vị trí và giá trị trên wire; fixture group target `$14` chỉ thêm **sau khi** chốt ACK/broadcast policy.
- [ ] Build `atmega8-node-*` vẫn OK (không áp đặt changes vào node modbus).

---

## 2. KIẾN TRÚC & LUỒNG DỮ LIỆU

### 2.1 Envelope structure cũ vs mới

| Phần | Cũ (zero-sum) | Mới (SendComCRC16) |
|---|---|---|
| Frame bytes | `[len_byte][payload...][chksum_1byte]` | `[len_byte=crc_len+2][payload...][crc_lo][crc_hi]` |
| LEN byte | `payload_len + 1` | `payload_len + 2` |
| Checksum count | 1 byte (two's complement zero-sum) | 2 bytes (CRC16-Modbus, little-endian) |
| Exemplo (PUMP_ON node 9)  | `[0x03, 0x06, 0x09, 0x??]` | `[0x04, 0x06, 0x09, 0xF3, 0xA7]` |
| Tổng byte (payload 2; frame gồm len + payload + checksum) | 4 byte | 5 byte |

### 2.2 Luồng dữ liệu encode (mới)

```
encodeSendComCrc16Packet(payload, payload_len):
    // payload là [opcode, node_id...] không kèm byte length
    len_byte = static_cast<uint8_t>(payload_len + 2);  // +2 cho 2 CRC byte
    out[0] = len_byte;
    for i in 0..payload_len-1: out[1+i] = payload[i];
    // Tính CRC trên [len_byte ... payload_last_byte], sau đó append
    crc = calculateCrc16Modbus(out, 1 + payload_len); // XOR qua len+payload, bỏ qua 2 CRC byte đã dự tính
    out[1 + payload_len] = lo(crc);
    out[1 + payload_len + 1] = hi(crc);
    return payload_len + 3;  // total = 1 len + payload + 2 crc
```

### 2.3 Luồng dữ liệu decode (mới)

```
decodeBurstRam(inBuf, inSize, outData8):
    if inSize < 11 return false; // 1 len + 8 data + 2 CRC
    // Kiểm tra CRC
    if inBuf[0] != 8 + 2          // len byte phải = data_len(8) + crc(2)
       return false;
    if !verifyCrc16SendCom(inBuf, inSize) return false;
    // Trích xuất 8 dữ liệu
    memcpy(outData8, inBuf+1, 8); // skip len byte
    return true;
```

### 2.4 Host transact & response parsing

- `AguLegacyRfHost::transact`: sau khi gửi frame CRC16, đợi nhận response khớp command:
  - PING/PUMP_ON/PUMP_OFF → ACK 1 byte `0x5A` (không thêm CRC trên ACK — giữ tương thích handshake cũ).
  - READ_RAM_BURST → 11 byte `[len][8 data][crc_lo][crc_hi]`.
- Cần map `AguRfCommand -> expected response size` để tính đúng độ dài đệm đọc.

### 2.5 Không ảnh hưởng ATmega8 node

- Mặc dù ATmega8 node dùng chung `rf_frame_codec.cpp`, nhưng `agu_legacy_codec` là code riêng cho ESP32 gateway/legacy node; ATmega8 sẽ không build `agu_legacy_codec` (check `ATMEGA8_NODE_BUILD`). Hãy chắc chắn không cờ đặt tích cực xung đột.

---

## 3. PHÂN RÃ CHI TIẾT TÁC VỤ

> Task-id: `S3-T<n>`. Không gộp "encode + decode + host + test + constant" vào 1 task.

### TRACK A — Tầng Data (codec)

**TASK S3-T1 `include/agu_legacy_codec.h` — Cập nhật declaration**
- Thay hàm `calculateZeroSumChecksum`/`verifyZeroSumChecksum` bằng:
  - `uint16_t calculateCrc16Modbus(const uint8_t* data, size_t len);` (đã có ở Sprint 1, thêm import ở đây).
  - `size_t appendCrc16ToSendCom(uint8_t* outBuf, size_t outSize, const uint8_t* payload, size_t payloadLen);` — trả frame theo mẫu SendComCRC16: `[len][payload][crc_lo][crc_hi]`.
  - `bool verifyCrc16SendCom(const uint8_t* frame, size_t frameSize);` — đệm CRC trên toàn frame `+2 byte` xem chờ kết quả 0.

**TASK S3-T2 `src/agu_legacy_codec.cpp` — Thay thế logic checksum**
- `formatSendComPacket` cũ (tổng mod 0xFF) -> `formatSendComCrc16Packet` mới dùng `appendCrc16ToSendCom`.
- Ghi đè hoặc xóa `calculateZeroSumChecksum` / `verifyZeroSumChecksum` (lưu file nhưng comment dùng hàm cũ là ký niệm, code không gọi).
- `AguLegacyCodec::encodePumpOn`:
  - payload = [0x06, nodeId]; payloadLen = 2.
  - Call `formatSendComCrc16Packet(payload, 2, outBuf, outSize)` => result frame `[0x04, 0x06, nodeId, lo(crc), hi(crc)]`.
  - Frame cũ 4 byte `[0x03,0x06,nodeId,checksum]` (total = payloadLen + 2); frame mới 5 byte `[0x04,0x06,nodeId,crc_lo,crc_hi]` (total = payloadLen + 3). **Return value của `encode*` phải đổi thành tổng byte thực sự đã ghi** (payloadLen + 3), mọi caller đọc theo return value.
- `AguLegacyCodec::encodePumpOff`: tương tự.
- `AguLegacyCodec::encodePing`: payload [0xA5, value, nodeId]; payloadLen = 3 -> frame len 1+3+2 = 6: `[0x05,0xA5,value,nodeId, loCRC, hiCRC]`.
- `AguLegacyCodec::encodeReadEeprom`, `encodeWriteEeprom`, `encodeReadRamBurst`, `encodeWriteRam`, `encodeGetId`, `encodeSetId`: đều áp dụng pattern `appendCrc16ToSendCom` tương tự.

**TASK S3-T2A `agu_legacy_codec` — Address byte & group target (characterization TRƯỚC implementation)**
- **Bằng chứng đã chốt:** `TestSCI.dpr:340` → `sci.sendcom(#$06+chr($14)); // gid==14 [4,5,6,7]`. Byte address trong payload `PUMP_ON` nhận **cả** node ID lẫn group ID. Công thức: `groupID = 0x10 | (nodeID & 0x0C)`.
- CRC phải bao phủ `[len][opcode][address/params]` trước khi append `[crc_lo][crc_hi]` — address byte nằm trong CRC scope, nên đổi target từ node `0x06` sang group `0x14` **bắt buộc** sinh CRC khác.
- Khai báo 2 predicate tách biệt ở boundary: `isLegacyNodeId(0x01..0x0F)` và `isLegacyGroupId({0x10,0x14,0x18,0x1C})`. **Cấm** dùng `isAguLegacyNodeId(4..7)` để validate group address.
- Chỉ cho phép group target cho opcode có semantics broadcast (`PUMP_ON`/`PUMP_OFF`) và **chỉ khi** có evidence firmware node thực sự xử lý `$14`. `PING`, `READ_RAM_BURST`, `DEVICE_ID`, `READ/WRITE_EEPROM` vẫn chỉ nhận node address — read/config KHÔNG được broadcast.
- **Chốt ACK policy trước khi code:** `AguLegacyRfHost::transact` hiện coi 1 byte `0x5A` là `ACKED` cho cả PING lẫn PUMP. Với group target, **một** ACK không chứng minh **4** node đã thực thi. Bắt buộc chọn rõ một trong hai:
  - **(A) An toàn, khuyến nghị:** giữ unicast fan-out. Group command → gateway gửi từng frame `PUMP_ON` cho từng node thuộc group, thu ACK riêng, fail-closed nếu bất kỳ node nào fail.
  - **(B) Broadcast:** gửi 1 frame `PUMP_ON` với address `$14`, coi kết quả là *best-effort* — **không** được set `ACKED` cho group nếu không có cơ chế xác nhận per-node; phải chờ telemetry phản hồi từng node mới xác nhận.
- Nếu chưa chốt được policy khi bắt đầu Sprint 3 → **chọn (A)** và ghi lý do; group broadcast là feature riêng, không blocking CRC migration.

- `AguLegacyCodec::decodeBurstRam`: sửa `if (!verifyZeroSumChecksum(...))` thành `if (!verifyCrc16SendCom(...))`. Cần kích thước inBuf tối thiểu là 11 byte (1 len + 8 data + 2 CRC). Tương tự, `decodeFramedId` không ảnh hưởng.

### TRACK B — Tầng Host (AguLegacyRfHost)

**TASK S3-T3 `include/agu_legacy_rf_host.h` — Cập nhật constant**
- Thêm `constexpr size_t kAguLegacyResponseSizeWithCrc16 = 11;` (1 len + 8 data + 2 CRC).
- Thêm `constexpr size_t kAguLegacyPingResponseSizeWithCrc16 = 4;` (1 len + 1 data + 2 CRC) nếu cần.
- Giữ các constant cũ (kích thước legacy) nhưng đánh dấu `DEPRECATED`.

**TASK S3-T4 `src/agu_legacy_rf_host.cpp` — Cập nhật transact**
- Đọc response: thay vòng lặp `while (nowMs() - start < AGU_LEGACY_ACK_TIMEOUT_MS)` nhận bytes hiện đại:
  - Chỉnh sửa vòng lặp `while (nowMs() - start < AGU_LEGACY_ACK_TIMEOUT_MS)` để đệm đúng `kAguLegacyResponseSizeWithCrc16` bytes cho các lệnh PUMP/PUMP_OFF.
  - Đọc 1 byte cho PING (nếu ping đổi).
- Cập nhật log ESP_LOGI: in `frame[0]` vẫn chính là LEN byte; còn `frame[1]..frame[8]` là data 8 bytes; `frame[9]` và `frame[10]` là CRC_Lo / CRC_Hi.
- Điền `result.response_byte = resp[0]` vẫn giữ nguyên (sử dụng byte đầu tiên).
- Kiểm tra `resp[9]` (CRC_Lo) + `resp[10]` (CRC_Hi) bằng `verifyCrc16SendCom(resp, resp_len)`.
- `transact()` hiện chặn ngoài `AGU_LEGACY_MIN_NODE_ID..MAX_NODE_ID` (`4..7`) và trả `INVALID_NODE_ID`. **Không nới guard mù** — phải tách nhánh `target_is_group` và chỉ route group cho opcode đã được S3-T2A cho phép.
- Với policy (A): group command không đi qua `transact()`; gateway fan-out qua nhiều lần `setPump(node_id, ...)` hiện có, giữ nguyên per-node ACK/RTT accounting.
- Với policy (B): `result.response_byte` không được dùng để suy ra trạng thái của từng node; phải ghi rõ kết quả là broadcast-level, không phải per-node `ACKED`.

### TRACK C — Tầng Test (Legacy relay)

**TASK S3-T5 `test/test_prototype/test_legacy_relay.cpp` — Cập nh kỳ vọng hex**
- Test hex vector checksum cũ (ví dụ `TEST_ASSERT_TRUE(verifyZeroSumChecksum(buf, len - 1, buf[len - 1]))`) -> sửa thành `TEST_ASSERT_TRUE(verifyCrc16SendCom(buf, len))`.
- Vector PUMP_ON đã thấy output `04 06 09 F3 A7` → task thêm hex comparison: `TEST_ASSERT_EQUAL_HEX8_ARRAY((uint8_t[]){0x04,0x06,0x09,0xF3,0xA7}, buf, 5);` (nếu len đúng 5).
- Vector PUMP_OFF hex `04 07 09 F2 37` -> tương tự.

### TRACK D — Tầng Doc

**TASK S3-T6 `docs/interface-wire-contract.md` — cập nhật phần checksum AGU**
- Thay dòng `checksum = (-sum([Length][Opcode][Params])) mod 256` bằng mô tả CRC16-Modbus `Two-byte CRC-16/MODBUS (0xFFFF init, poly 0xA001, reflected) appended as [crc_lo][crc_hi] after [Length][Opcode][Params]`.
- Mention that `Length = payloadLen + 2` per SendCom CRC16 spec.

---

## 4. TIÊU CHUẨN RÀ SOÁT CỨNG

1. **Frame size tăng 1 byte (S3-HARD-01):** Envelope size chuyển từ `payloadLen + 2` (cũ: len byte + 1 checksum byte) sang `payloadLen + 3` (mới: len byte + 2 CRC bytes). Task đảm bảo `expectedResponseSize` trong host cập nhật tương ứng (burst response mới 11 byte `[len][8 data][crc_lo][crc_hi]` so với 9 byte cũ). Vị trí `crc_lo` / `crc_hi` ở byte thứ 10, 11 của burst response.
2. **Không đổi format opcode/len opcode (S3-HARD-02):** Mặc dù frame thêm byte, nhưng vị trí opcode và node ID vẫn giữ nguyên (index 1, 2 trong frame) — không được lệch vị trí.
3. **CRC không thay đổi opcode meaning (S3-HARD-03):** Checksum CRC chỉ phát hiện lỗi truyền; không thay đổi nghĩa opcode hay tham số dữ liệu.
4. **Attest vector golden (S3-HARD-04):** Test hex vector `PUMP_ON` / `PUMP_OFF` / `PING` phải khớp `0xA7F3` / `0x37F2` / vector khác tương ứng từ Sprint 1 test_crc16 (tạo lại từ codebase utility, không nhập tay).
5. **Fail-close decode (S3-HARD-05):** `decodeBurstRam` trả `false` ngay cả khi `inSize < 11` hoặc CRC không khớp; không bao giờ "sửa CRC để bằng 0" để thu nhận.
6. **ATmega8 node (S3-HARD-06):** Giữ nguyên code ATmega8 không thay đổi (không đc include `agu_legacy_codec.*` khi build `ATMEGA8_NODE_BUILD`). Yêu cầu kiểm tra `grep -R "agu_legacy"` trên source ATmega8.
7. **Không merging với zero-sum internal (S3-HARD-07):** Dù Sprint 1 đã chuẩn hóa utility CRC Modbus, nhưng legacy codec bên trong `agu_legacy_codec.cpp` tự mua hàm tích hợp riêng. Không được thay đổi hàm internal kiểm tra checksum không phải CRC Modbus cho `decodeBurstRam` ngoài sự kiện Sprint này.
8. **Group address safety (S3-ADDR-01):** `$14` chỉ là group target cho opcode/policy được cho phép; không broadcast read/config, không giả ACK, không làm mất per-node observability. Khi chưa có evidence ACK semantics, giữ unicast fan-out.

---

## Kết quả dự kiến sau Sprint

- `AguLegacyCodec::encodePumpOn(nodeId)` tạo frame `[len=0x04][0x06][nodeId][crc_lo][crc_hi]` (5 byte) — với nodeId=9 trả về `04 06 09 F3 A7`, khớp golden vector.
- `AguLegacyCodec::decodeBurstRam(resp, 11, outData8)` đọc frame CRC16-Modbus hợp lệ.
- `AguLegacyRfHost::transact` đọc đúng 11 byte response (8 data + 2 CRC) và xác thực CRC thành công.
- `test_legacy_relay.cpp` green test vector hex CRC.
- Code ATmega8 node không bị ảnh hưởng.
- Sprint 4 tiếp tục QA tổng thể.
