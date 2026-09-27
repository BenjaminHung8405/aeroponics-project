# Sprint 3A: Legacy AGU Wire Characterization (Evidence Freeze)

> **FINAL EVIDENCE UPDATE (2026-09-26): COMPLETE / SUPERSEDES THE DRAFT BELOW.**
> Authoritative Delphi `TSCI.SendComCRC16`/`CalCRC16` and AVR assembly evidence
> establish CRC16-Modbus for the AGU legacy SCI wire: init `0xFFFF`, reflected
> polynomial `0xA001`, two CRC bytes in little-endian order, and a length byte
> counting payload plus both CRC bytes. The implementation is now migrated in
> `AguLegacyCodec`; old one-byte zero-sum frames are rejected. The historical
> dual-path/default-zero-sum design in this planning document is superseded and
> must not be used as an implementation requirement.

## Final Evidence Result

- `PUMP_ON` node 9: `04 06 09 F3 A7`.
- `PUMP_OFF` node 9: `04 07 09 F2 37`.
- Read burst response: `[0A][8 data bytes][crc_lo][crc_hi]`; sample vector
  `0A 0A 14 1E 28 32 3C 46 50 3F 7E`.
- Whole-frame CRC remainder is `0x0000`; a legacy one-byte zero-sum response is
  invalid and is rejected by `decodeBurstRam`.
- ACK (`0x5A`) and framed-ID (`FF 5A ID`) behavior remains unchanged.

> **Phụ thuộc:** Sprint 2 approved (`RF_PROTOCOL_VERSION 0x02` trên modern RF path).
> **Phạm vi:** **READ-ONLY + feature flag.** Không thay đổi hành vi production. Không deploy.
> **Mục tiêu:** Đóng băng bằng chứng về **hành vi wire THẬT** của ATmega8 node preloaded trước khi chạm vào `agu_legacy_codec.cpp`.

---

## 1. VÌ SAO SPRINT NÀY TỒN TẠI (BLOCKER CỦA SPRINT 3B)

Theo `docs/ATMEGA8_INTEGRATION_BOUNDARY.md`:

> *"ATmega8 nodes are preloaded legacy devices. Source is unavailable, firmware is immutable and version is UNKNOWN."*

`agu_legacy_codec.cpp` trên ESP32 là **model** của legacy protocol, không phải bằng chứng node thật sự dùng model đó. Hiện codec dùng **zero-sum 1 byte**:

```cpp
// src/agu_legacy_codec.cpp:29
const uint8_t checksum = static_cast<uint8_t>((~sum + 1) & 0xFF);
```

Trong khi Big Plan (Delphi `TSCI.CalCRC16`, AVR assembly) nói legacy dùng **CRC-16/MODBUS 2 byte**. Đây là **mâu thuẫn chưa được giải quyết bằng bằng chứng thực đo**.

**Nếu ta sửa codec sang CRC16 mà node thật vẫn dùng zero-sum:** mọi lệnh PUMP_ON/PUMP_OFF/EEPROM sẽ **chết âm thầm** — không chỉ lệnh bị từ chối, mà node có thể **nhận frame dài hơn mong đợi**, đọc lệch opcode, và hành động sai. Đây là risk **an toàn**, không chỉ risk chức năng (bơm không tắt được).

> **BLOCKING RULE:** Sprint 3B (code migration) **không được bắt đầu** cho tới khi Sprint 3A cung cấp bằng chứng wire rõ ràng.

---

## 2. PHẠM VI & FILES TÁC ĐỘNG

| File | Hành động | Ghi chú |
|---|---|---|
| `aeroponics-firmware/include/agu_legacy_codec.h` | **Sửa (declarations only)** | Thêm `AguLegacyCrcMode` enum + `configureCrcMode()` — **default `ZERO_SUM`** (giữ nguyên hành vi) |
| `aeroponics-firmware/src/agu_legacy_codec.cpp` | **Sửa (dual-path, default = cũ)** | Nhánh `CRC16_MODBUS` compile được nhưng **không active** |
| `aeroponics-firmware/include/config.h` | **Sửa** | Thêm `AGU_LEGACY_CRC_MODE_MODBUS` compile-time flag (default `false`) |
| `aeroponics-firmware/platformio.ini` | **Sửa** | Thêm `-DAGU_LEGACY_CRC_MODE_MODBUS=1` cho prototype env (không cho atmega8-node) |
| `docs/legacy_wire_captures/` | **Tạo mới** | Binary/logic-analyzer captures |
| `docs/LEGACY_WIRE_EVIDENCE.md` | **Tạo mới** | Báo cáo phân tích wire |
| `aeroponics-firmware/test/test_production/test_production.cpp` | **Sửa (thêm)** | Test dual-mode: cùng input → 2 output khác nhau, verify đúng |

> ⚠️ **Scope boundary:** Không sửa `rf_frame_codec.*`, `Crc16Modbus.*` (chỉ dùng), `treatment_manager.*`, `config.h` version. Không thay đổi bất kỳ hành vi production nào.

---

## 3. MỤC TIÊU CỤ THỂ

- [ ] Xác minh checksum thật trên wire: **zero-sum 1 byte** hay **CRC-16/MODBUS 2 byte** (hoặc cái khác).
- [ ] Capture đầy đủ 4 nhóm lệnh: `PUMP_ON`, `PUMP_OFF`, `PING`, `READ_RAM_BURST`.
- [ ] Xác minh ACK behavior cho **unicast** node `0x04..0x07` và **group** `$14`.
- [ ] Xác minh `expectedResponseSize` thực tế cho từng lệnh.
- [ ] Compile-time switch hoạt động: build native với cả 2 mode, output frame khác nhau, cả 2 verify pass.
- [ ] **Default production = `ZERO_SUM`** (không đổi hành vi cho tới khi Sprint 3B).
- [ ] Báo cáo `LEGACY_WIRE_EVIDENCE.md` nêu rõ kết luận + khuyến nghị cho Sprint 3B.

---

## 4. KIẾN TRÚC & THIẾT KẾ

### 4.1 Dual-path CRC mode

```cpp
// include/agu_legacy_codec.h
namespace AguLegacy {

enum class CrcMode : uint8_t {
    ZeroSum = 0,   // LEGACY CURRENT: 1-byte two's complement zero-sum
    Crc16Modbus = 1, // TARGET: 2-byte CRC-16/MODBUS (SendComCRC16)
};

// Compile-time default. Override via -DAGU_LEGACY_CRC_MODE_MODBUS=1
#ifndef AGU_LEGACY_CRC_MODE_MODBUS
#define AGU_LEGACY_CRC_MODE_MODBUS 0
#endif

constexpr CrcMode kDefaultCrcMode =
    AGU_LEGACY_CRC_MODE_MODBUS ? CrcMode::Crc16Modbus : CrcMode::ZeroSum;

CrcMode activeCrcMode();

}
```

```cpp
// src/agu_legacy_codec.cpp
size_t formatSendComPacket(const uint8_t* payload, size_t payloadLen,
                           uint8_t* outBuf, size_t outSize) {
    switch (activeCrcMode()) {
        case CrcMode::Crc16Modbus:
            return formatSendComCrc16Packet(payload, payloadLen, outBuf, outSize);
        case CrcMode::ZeroSum:
        default:
            return formatSendComZeroSumPacket(payload, payloadLen, outBuf, outSize);
    }
}
```

**Điểm quan trọng:** `formatSendComPacket` giữ nguyên **tên và signature** → không phá caller hiện có (`encodePumpOn/Off/Ping/...`).

### 4.2 Compile-time vs runtime mode

| Aspect | Compile-time (`-D` flag) | Runtime (`configureCrcMode`) |
|---|---|---|
| Use case | **Benchmark A/B**, build matrix, CI | **Unit test**, defensive default |
| Risk thấp | Cao (explicit rebuild) | Thấp hơn nếu default đúng |
| Bắt buộc | **YES** cho production builds | Chỉ cho test |

Khuyến nghị: production build **chỉ** dùng compile-time flag. Runtime override chỉ tồn tại để test matrix, và phải có `static_assert` chặn nếu build production mà flag lệch.

---

## 5. PHÂN RÃ TÁC VỤ

### TRACK A — Bằng chứng wire (Field)

**TASK S3A-T1 — Capture RF wire traces**
- Thiết bị: ESP32-S3 gateway + ATmega8 node thật + logic analyzer (Saleae/8-channel) trên dòng UART, hoặc `uart_rf_transport` debug log hex dump.
- Capture 4 nhóm lệnh, mỗi lệnh ≥5 lần:
  - `PUMP_ON` → node 4,5,6,7
  - `PUMP_OFF` → node 4,5,6,7
  - `PING` → node 4
  - `READ_RAM_BURST(8 bytes)` → node 4
- Lưu raw bytes (hex) vào `docs/legacy_wire_captures/<command>_<node>_<seq>.hex`.
- **Ghi lên file:** UART baud, RF module type (HC-12 / E32), khoảng cách, công suất, RSSI.

**TASK S3A-T2 — Xác minh checksum type**
- Với mỗi capture, tính **cả hai** ứng viên:
  - Zero-sum: `(~sum(bytes[0..n-1]) + 1) & 0xFF == bytes[n-1]`?
  - CRC16-Modbus: `verifyCrc16Modbus(frame, n) == true` với `[crc_lo][crc_hi]` ở cuối?
- **Kết luận bắt buộc** (chọn đúng 1):
  - **(A) Zero-sum confirmed** → Sprint 3B migration **đúng kế hoạch** (chuyển sang CRC16 chỉ khi có quyết định product + firmware node mới).
  - **(B) CRC16 confirmed** → `agu_legacy_codec.cpp` hiện tại **SAI**; Sprint 3B scope = **bug fix**, ưu tiên cao nhất (đang chạy sai protocol).
  - **(C) Cả hai fail / không xác định** → **BLOCKED**: node có checksum thứ ba (CRC8? sum thuần? XOR?) hoặc frame còn trường ẩn. Phải đào sâu thêm trước khi đụng code.

**TASK S3A-T3 — ACK behavior cho group `$14`**
- Gửi `PUMP_ON` với address `$14` (theo `TestSCI.dpr:340` → gid 4..7).
- **Đo:** node nào thực sự bật relay? Bao nhiêu ACK về? Nội dung ACK byte?
- **So sánh** với unicast fan-out 4 lệnh riêng.
- **Kết luận bắt buộc:** chọn policy (A) unicast fan-out (khuyến nghị) hoặc (B) broadcast best-effort — như Sprint 3 §3 S3-T2A yêu cầu. Sprint 3A **cung cấp evidence**, Sprint 3B **thực thi policy**.

**TASK S3A-T4 — `expectedResponseSize` validation table**
| Command | Kỳ vọng (plan) | Thực tế (capture) | Khớp? |
|---|---|---|---|
| `PUMP_ON` | 1 byte `0x5A` | ? | ? |
| `PUMP_OFF` | 1 byte `0x5A` | ? | ? |
| `PING` | 1 byte | ? | ? |
| `READ_RAM_BURST` | 9 byte (8 data + 1 cksum) | ? | ? |
| `READ_EEPROM` | ? | ? | ? |
| `GET_ID` | ? | ? | ? |

### TRACK B — Dual-path implementation (Code, non-behavioral)

**TASK S3A-T5 — `CrcMode` enum + compile-time flag**
- Thêm `enum class CrcMode` + `kDefaultCrcMode` + `activeCrcMode()` vào `agu_legacy_codec.h/.cpp`.
- **Default = `ZeroSum`** → production behavior **không đổi** (byte-for-byte identical output).
- `static_assert` chặn build production nếu `AGU_LEGACY_CRC_MODE_MODBUS` set mà không có explicit opt-in.

**TASK S3A-T6 — Tách 2 hàm encode**
- `formatSendComZeroSumPacket(...)` — thân code hiện tại, **nguyên vẹn**.
- `formatSendComCrc16Packet(...)` — mới, theo `SendComCRC16`:
  ```text
  len_byte = payloadLen + 2
  crc = CRC16-Modbus([len_byte][payload...])
  frame = [len_byte][payload...][crc_lo][crc_hi]   // total = payloadLen + 3
  ```
- `formatSendComPacket(...)` là dispatcher, giữ signature cũ.

**TASK S3A-T7 — Tương đương verify path**
- `verifyZeroSumChecksum` giữ nguyên, dùng khi mode = ZeroSum.
- Thêm `verifyCrc16SendCom(frame, len)` — dùng `verifyCrc16Modbus` từ `core/Crc16Modbus.h`.
- `decodeBurstRam` dispatcher theo mode; **ZeroSum path giữ nguyên** 9-byte contract.

**TASK S3A-T8 — `platformio.ini` — build matrix**
| Env | Flag | Mode | Mục đích |
|---|---|---|---|
| `esp32-s3-devkitc-1` (default) | — | ZeroSum | **Production unchanged** |
| `native` | — | ZeroSum | Test mặc định |
| `native-prototype` | `AGU_LEGACY_CRC_MODE_MODBUS=1` | Crc16Modbus | A/B benchmark |
| `atmega8-node-*` | — | N/A | **Phải KHÔNG build `agu_legacy_codec.*`** |

### TRACK C — Test matrix

**TASK S3A-T9 — Unit test dual-mode**
- Test: cùng input `PUMP_ON node 9`:
  - ZeroSum mode → `03 06 09 XX` (4 byte, checksum zero-sum)
  - Crc16Modbus mode → `04 06 09 F3 A7` (5 byte, golden vector từ Sprint 1)
- Test: `verifyZeroSumChecksum` pass trên ZeroSum output, **fail** trên Crc16 output.
- Test: `verifyCrc16SendCom` pass trên Crc16 output, **fail** trên ZeroSum output.
- Test: **default = ZeroSum** — assert `activeCrcMode() == CrcMode::ZeroSum` khi không set flag.
- Test: `decodeBurstRam` 9-byte path (ZeroSum) **vẫn PASS** — regression guard cho hành vi hiện tại.

**TASK S3A-T10 — `docs/LEGACY_WIRE_EVIDENCE.md`**
- Bảng capture, phân tích checksum, kết luận (A/B/C ở S3A-T2), ACK policy evidence.
- **Khuyến nghị tường minh cho Sprint 3B.**

---

## 6. TIÊU CHUẨN RÀ SOÁT CỨNG

1. **Zero behavior change (S3A-HARD-01):** Build production (`esp32-s3-devkitc-1`) phải cho **output byte-for-byte identical** với trước Sprint 3A. Verify bằng cách so golden vectors của `test_production.cpp` legacy tests (dòng 10194-10349) — **không được sửa 1 test nào ở Sprint 3A**.
2. **No deployment (S3A-HARD-02):** Sprint 3A **không flash** bất kỳ firmware nào lên thiết bị thật. Chỉ capture + build host.
3. **Evidence mandatory (S3A-HARD-03):** Không ai được bắt đầu Sprint 3B nếu chưa có file capture thật trong `docs/legacy_wire_captures/`. Review Agent kiểm tra file, không tin mô tả.
4. **ATmega8 isolation (S3A-HARD-04):** `grep -R "agu_legacy" src/atmega8_node_main.cpp` → **rỗng**. `platformio.ini` `env:atmega8-node*` `build_src_filter` không chứa `agu_legacy_codec.cpp`.
5. **Default safe (S3A-HARD-05):** `kDefaultCrcMode == CrcMode::ZeroSum`. Đổi default = vi phạm hard gate, phải mở task riêng + review an toàn.
6. **No dead code (S3A-HARD-06):** Cả 2 nhánh đều **compile được và có test**. Nhánh Crc16Modbus phải được test chứ không chỉ viết ra — code chết không được chấp nhận.
7. **Single source of truth (S3A-HARD-07):** CRC-16/MODBUS **chỉ lấy từ `core/Crc16Modbus.h`**. Cấm chép lại thuật toán trong `agu_legacy_codec.cpp`.

---

## 7. GATE & EXIT CRITERIA

| Gate ID | Description | Status |
|---|---|---|
| S3A-GATE-01 | Wire capture thật tồn tại (≥4 command types × ≥5 lần) | [ ] |
| S3A-GATE-02 | Checksum type kết luận rõ (A / B / C) với bằng chứng tính toán | [ ] |
| S3A-GATE-03 | ACK group `$14` behavior đo & quyết định policy | [ ] |
| S3A-GATE-04 | `expectedResponseSize` table hoàn chỉnh, đối chiếu plan | [ ] |
| S3A-GATE-05 | Dual-mode build PASS (ZeroSum + Crc16Modbus) | [ ] |
| S3A-GATE-06 | Legacy tests hiện tại **không đổi**, vẫn PASS | [ ] |
| S3A-GATE-07 | `LEGACY_WIRE_EVIDENCE.md` review + ký | [ ] |

**Sprint 3B chỉ được bắt đầu khi S3A-GATE-01..07 đều `[x]`.**

---

## 8. KẾT QUẢ DỰ KIẾN

- Bằng chứng wire thật, có thể reproduce, đính kèm commit.
- `agu_legacy_codec` hỗ trợ 2 mode với **default không đổi hành vi**.
- Test matrix chứng minh cả 2 nhánh đều đúng.
- Sprint 3B có đủ căn cứ để quyết định: migrate / fix bug / block.
