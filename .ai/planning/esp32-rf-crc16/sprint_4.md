# Sprint 4: PumpNodeController + Integration Verify + Regression/Fuzz

> **Phụ thuộc:** Sprint 2 (codec RF), Sprint 3 (legacy codec).
> **Mục đích:** kiểm chứng **toàn hệ thống RF** (gateway → node → CRC trả về) với CRC16-Modbus; bao phủ cả `PumpNodeController` (phía app), `atmega8_node_main` (node), benchmark cục bộ và fuzz fail-closed.

---

## 1. PHẠM VI & MỤC TIÊU

### 1.1 Modules / Files tác động

| File | Hành động | Mục tiêu |
|---|---|---|
| `aeroponics-firmware/src/pump_node_controller.cpp` | **Rà soát + hoàn thiện** | `buildFrame`, `parseFrame`, `handleIncomingFrame` verify CRC16-Modbus tương thích 2 đầu |
| `aeroponics-firmware/include/pump_node_controller.h` | **Rà soát** | delegate `calculateCrc16` đúng chuẩn Modbus; không còn CCITT |
| `aeroponics-firmware/src/rf_benchmark_runner.cpp` | **Sửa/kiểm** | benchmark round-trip CRC16 (TX/RX) đo throughput/error với frame hợp lệ |
| `aeroponics-firmware/test/test_production/test_production.cpp` | **Sửa/kiểm** | Fuzz; integration 4 nodes; trailers |
| `aeroponics-firmware/test/fakes/FakeRfTransport.h` | **Rà soát + sửa** | inject byte mẫu frame CRC Modbus hợp lệ |
| `aeroponics-firmware/test/fakes/IntegrationTestRfFixture.h` | **Rà soát** | fixture frame CRC Modbus |
| `aeroponics-firmware/platformio.ini` | **Sửa** | test_filter bổ sung suite benchmark/fuzz nếu cần |
| `aeroponics-firmware/src/core/Crc16Modbus.h/.cpp` | **KHÔNG sửa** trong sprint này (caffeine đã chuẩn) | nếu phát hiện sai → Sprint 1 fix |

> **Lưu ý:** Sprint này **không** thay đổi thuật toán; chỉ **kiểm chứng và vá** những chỗ giả định vẫn lấy checksum cũ.

### 1.2 Mục tiêu cụ thể

- [ ] `PumpNodeController::buildFrame` tạo frame hợp lệ: `[header | payload | HMAC(12) | CRC(2)]` với CRC Modbus. Roundtrip qua `FakeRfTransport` → `parseFrame` PASS.
- [ ] `parseFrame` nhận frame có CRC Modbus đúng, kết hợp HMAC → `AntiReplayResult::ACCEPTED` bình thường.
- [ ] `handleIncomingFrame` với: CRC đúng + HMAC đúng → stateful process; CRC sai → drop; CRC đúng HMAC sai → drop (fail-closed cả CRC lẫn HMAC).
- [ ] Fuzz ≥ 2.500 frame ngẫu nhiên (như yêu cầu QA REPORT) → 0 frame lọt chút lỗi; 100% fail-closed.
- [ ] Benchmark: TX/RX roundtrip OK với frame CRC Modbus; so sánh với kết quả pre-migration (báo cáo trong Sprint 5).
- [ ] Build & run full: `pio test -e native`, `pio test -e native-integration`, `pio run -e esp32-s3-devkitc-1`, `pio run -e atmega8-node-4`.

---

## 2. KIẾN TRÚC & LUỒNG DỮ LIỆU

### 2.1 Luồng hoàn chỉnh điều khiển (đã chuyển Modbus)

```
Gateway (ESP32-S3)  PUMP ON
─────────────────────────────────────────────
queueExternalNodeCommand(node, ON, cmd_id, lease)
      │
      ▼
buildPendingFrame(node_id)
      └── API: buildFrame(SET_PUMP, node, cmd, payload, len, out, cap)
              ├── RfFrameCodec::encodeFrame(...)
              │      ├── header: SOF/version/meta/sequence/command
              │      ├── payload wire
              │      ├── HMAC-SHA256(psk, header+payload) → 12 bytes
              │      └── CRC16-Modbus(header+payload+HMAC) → 2 bytes LE   ★
              └── send() qua UartRfTransport
      │
      ▼
ATmega8 node X
    └── uartReceive(g_rx) → process()
            ├── RfFrameCodec::decodeFrame(..., psk)
            │       ├── decodeHeader → SOF/version/type/address ✓
            │       ├── CRC16-Modbus check (MAU)        ★
            │       └── HMAC verify                     ★
            ├── nếu SET_PUMP hợp lệ → pump(1), sendAck(...)
            │
            ▼
Gateway nhận COMMAND_ACK
    └── decodeFrame → CRC Modbus + HMAC ✓
    └── validateAck → handleAckFrame → publishOutcome(SUCCESS)
```

### 2.2 Checkpoints validate

| Điểm | Kiểm tra | Trả về |
|---|---|---|
| `decodeFrameDetailed` | CRC | `ParseError::CRC_MISMATCH` nếu sai |
| `decodeFrameDetailed` | HMAC | `ParseError::HMAC_AUTH_FAIL` nếu sai |
| `PumpNodeController::validateFrameEnvelope` | SOF/length/CRC nhanh | false |
| `PumpNodeController::verifyCrcAndMac` | CRC → HMAC order | false nếu bất kỳ fail |
| `validateAntiReplay` | session/sequence | `REJECTED`/`NEW_SESSION` |

### 2.3 Không dùng lại hàm cũ

- Grep toàn repo `rf_frame_codec` gọi trực tiếp `0x1021` → fail review. Mọi phép tính CRC frame phải qua `Crc16Modbus`.

---

## 3. PHÂN RÃ CHI TIẾT TÁC VỤ

> Task-id: `S4-T<n>`.

### TRACK A — Tầng App (PumpNodeController)

**TASK S4-T1 `src/pump_node_controller.cpp` — `buildFrame` review**
- Kiểm tra gọi `encodeFrame` với correct buffer size; xác nhận CRC đã Modbus (đọc 2 byte cuối frame và so `readU16Le == calculateCrc16(frame, len-2)`).
- Nếu phát hiện branch hardening về size → 1 commit nhỏ; không trộn.

**TASK S4-T2 `src/pump_node_controller.cpp` — `parseFrame` / `validateFrameEnvelope`**
- Context: CRC check ở `validateFrameEnvelope` hay `verifyCrcAndMac` (double check flow). Đảm bảo CRC tính trên `[header+payload+HMAC]` (không kể 2 byte CRC đuôi).
- Fix typo nếu có đoạn tính trên `frame_len - 2` sai data range.

**TASK S4-T3 `src/pump_node_controller.cpp` — `handleIncomingFrame` side-effect gating**
- Đảm bảo mọi lệnh pump/driver chỉ xuất hiện SAU khi `decodeFrameDetailed == OK`.
- Thống kê: khi CRC fail → `transport_->recordCrcError()` + log `ParseError`. Không tạo pending mới.

### TRACK B — Tầng Test (regression/fuzz)

**TASK S4-T4 `test/test_production/test_production.cpp` — roundtrip chain**
- Test: `buildFrame(SET_PUMP)` → `decodeFrameDetailed` → `OK`.
- Test mới: CRC đuôi bị flip 1 bit → `CRC_MISMATCH`, không `HMAC_AUTH_FAIL` (đúng thứ tự).
- Test HMAC flip → sau CRC OK → `HMAC_AUTH_FAIL`.

**TASK S4-T5 Fuzz suite (có trong test_production hoặc test/fuzz mới)**
- Vòng lặp `for (i=0; i<2500; ++i)`: sinh header/payload ngẫu nhiên với seed, set CRC đúng, flip random 1 bit trong vùng khác nhau; decode phải trả `CRC_MISMATCH` (hoặc `HMAC_AUTH_FAIL`) — không bao giờ `OK`.
- Trường hợp valid: sinh frame hợp lệ có CRC0 đúng thì đảm bảo 1 case PASS để test không "luôn luôn fail".

**TASK S4-T6 `test/fakes/IntegrationTestRfFixture.h` — fixture sync**
- Mọi fixture frame tĩnh (hex string trong integration test) phải recalc bằng Modbus trước khi dùng (dùng `appendCrc16Modbus` hoặc fixture generator). Không để CCITT cũ sót.

### TRACK C — Tầng Build/Benchmark

**TASK S4-T7 `src/rf_benchmark_runner.cpp` — benchmark CRC path**
- Đảm bảo `runBenchmark` encode/decode frame Modbus (final trailer); thống kê: CRC_ERRORS = 0 trên đường truyền sạch.
- Ghi lại baseline: thời gian CRC/1MB; so sánh CCITT vs Modbus deviation (chỉ báo cáo, không tối ưu hóa vội).

**TASK S4-T8 `platformio.ini` — build gate**
- `pio run -e esp32-s3-devkitc-1`, `pio run -e atmega8-node-4`, `pio test -e native`, `pio test -e native-integration`, `pio test -e native-prototype` → 0 lỗi.

---

## 4. TIÊU CHUẨN RÀ SOÁT CỨNG

1. **Fail-closed toàn diện (S4-HARD-01):** Frame có CRC sai bị drop ở tầng codec trước; không lọt vào HMAC layer lẫn business logic. Ngược lại, HMAC sai cũng drop (mặc dù CRC OK). Không "OK một phần".
2. **Đếm lỗi (S4-HARD-02):** Mọi CRC failure phải tăng `UartTransportStats::crc_errors` (không im lặng), log chỉ level debug khi rate-limited (tránh spam UART).
3. **Coverage benchmark ngăn regression (S4-HARD-03):** Kết quả benchmark Sprint 4 phải ghi nhận `CRC_ERRORS == 0` ở môi trường sạch; nếu có CRC error trong benchmark → chặn release (không merge).
4. **Fuzz deterministic (S4-HARD-04):** Fuzz dùng seed cố định (log seed trong output) để reproduce; 2.500 ca trở lên, 100% expected fail-closed.
5. **Không sửa algorithm trong sprint này (S4-HARD-05):** Nếu phát hiện bug thuật toán → không "hotfix" tạm bợ; tạo Task riêng quay lại Sprint 1 rồi revert. Mục đích Sprint 4 chỉ node integrate/verify.
6. **Giữ thứ tự verify CRC→HMAC (S4-HARD-06):** Không đổi thứ tự phases trong `decodeFrameDetailed` (CRC trước, HMAC sau) trừ khi có lý do bảo mật được document — thay đổi repeat phải review input size/constant-time.

---

## Kết quả dự kiến sau Sprint

- Toàn hệ control RF gửi/nhận với CRC16-Modbus khớp giữa gateway và node.
- Regression/fuzz PASS, benchmark trắng (0 CRC error), mọi env build PASS.
- Sẵn sàng cho Sprint 5: docs, capstone QA & release gate.