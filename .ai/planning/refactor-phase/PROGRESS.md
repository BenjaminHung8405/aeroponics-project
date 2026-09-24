# PROGRESS — Refactor Phase Tracking

> Tài liệu theo dõi tiến độ thực thi (Progress Register) cho kế hoạch Refactor được khởi tạo bởi **Gemini**. Mọi Agent thực thi phải cập nhật Status tại đây sau mỗi tác vụ theo đúng quy ước markdown checkbox (Pending / In Progress / QA Review / Done). Không được sửa cột Note trừ khi có thay đổi hướng dẫn kỹ thuật được phê duyệt ở `.ai/planning/refactor-phase/README.md`.

---

## 1. Started

| Trường | Giá trị |
|---|---|
| **Thời điểm khởi tạo** | `2026-09-24T05:03:22Z` (UTC) |
| **Execution Agent** | **Gemini** |
| **Vai trò** | Kỹ sư thực thi (Execution Agent) theo kế hoạch Sprint |
| **Baseline Agent** | Đã khởi tạo master plan `README.md` ngày `2026-09-24` (không thay đổi trong phạm vi refactor) |

---

## 2. Reference Plan

| Trường | Giá trị |
|---|---|
| **Thư mục kế hoạch** | `.ai/planning/refactor-phase/` |
| **Master Planning Context** | `.ai/planning/refactor-phase/README.md` (Single Source of Truth — bắt buộc đọc trước khi bắt đầu bất kỳ Sprint nào) |
| **Sprint hiện tại (đang tham chiếu)** | `.ai/planning/refactor-phase/sprint_1.md` — **Sprint 1: Gateway Transport & Codec Refactoring (ESP32)** |
| **Thứ tự Sprint roadmap** | `Sprint 1 (Codec ESP32)` → `Sprint 2 (Virtual FSM & Safety Timers)` → `Sprint 3 (Backend NestJS & TimescaleDB)` → `Sprint 4 (Dashboard Next.js & Nginx)` |
| **Golden Baseline tham chiếu Sprint 1** | `docs/interface-wire-contract.md` §3–§5 (Codec & Wire Contract Rev 3), `docs/STATE_MACHINE_MATRIX.md` §6 |
| **Phụ thuộc Sprint 1** | Không có (Bottom-up đầu tiên — RF Wire Codec) |
| **Output bàn giao Sprint 1** | Codec AGU legacy chuẩn hóa `nodeId` + zero-sum canonical, UART HC-12 cô lập trên FreeRTOS Core 1, 273/273 native unit tests PASS |

> Ghi chú: PROGRESS.md đặt tại gốc của plan `refactor-phase`; mỗi Sprint sẽ tham chiếu một `sprint_N.md` riêng trong cùng thư mục. Khi Sprint 1 đạt 100% Done (mọi row `[x]`), cập nhật Reference Plan sang `sprint_2.md` và tạo các bảng Track tương ứng.

---

## 3. Addition Plan (Yêu cầu phát sinh)

**Chưa có yêu cầu phát sinh nào được bổ sung.** Mọi yêu cầu mới vượt phạm vi Golden Baseline hoặc Sprint hiện tại phải được ghi vào bảng dưới đây trước khi triển khai, và phải được phê duyệt tại `.ai/planning/refactor-phase/README.md`.

| ID | Yêu cầu phát sinh | Trạng thái | Ghi chú |
|---|---|---|---|
| (trống) | Chưa có | — | — |

---

## 4. Track Status — Sprint 1

> Quy ước Status (bắt buộc, không thay đổi ký hiệu):
> - `[ ] Pending` — Task chưa chạm vào.
> - `[ ] In Progress` — Execution Agent đang viết code.
> - `[ ] QA Review` — Code đã viết xong, đang chờ rà soát chất lượng.
> - `[x] Done` — Đã qua vòng review nghiêm ngặt và được duyệt.
>
> **Tiêu chuẩn rà soát cứng (bắt buộc đối chiếu ở Section 4 của `sprint_1.md`):** S1-CODEC-01, S1-CODEC-02, S1-UART-03, S1-UART-04, S1-UART-05, S1-TEST-06. Mọi task phải PASS toàn bộ rule liên quan trước khi đánh `[x] Done`.

### 4.1 TRACK A — Codec Refactoring (Zero-Sum & nodeId)

| Task ID | Mô tả Task | Status | Note (chỉ thị kỹ thuật bắt buộc) |
|---|---|---|---|
| A1 | `include/agu_legacy_codec.h` — Cập nhật interface: thêm tham số `nodeId` bắt buộc vào `encodeReadRamBurst`, chuẩn hóa `calculateZeroSumChecksum`/`verifyZeroSumChecksum` | [ ] QA Review | Doxygen/JSDoc bổ sung trên mọi API codec public (zero-sum + 2 overload `encodeReadRamBurst`); frame `[0x06,0x0E,addr_lo,addr_hi,count,nodeId,checksum]` giữ nguyên; KHÔNG hardcode `0x01` ở production callers; native suite 198 tests không đổi so baseline. |
 | A2 | `src/agu_legacy_codec.cpp` — Chuẩn hóa zero-sum checksum trong `formatSendComPacket` | [ ] QA Review | Hidden – tuân thủ S1-CODEC-01: dùng Two's complement rõ ràng `checksum = ~sum + 1` (tương đương `(0x100 - sum) & 0xFF`); mọi encoder phải thỏa invariant `sum(frame[0..len-1]) & 0xFF == 0`. Tính `sum` trên `[Length][Opcode][Params]`, không tính checksum byte. Trả `0` khi encode fail và để caller check `frame_size == 0` trước khi `send()`. |
| A3 | `src/agu_legacy_codec.cpp` — Loại bỏ byte hardcode rác trong `encodeWriteRam` | [ ] QA Review | Hidden – đọc `docs/interface-wire-contract.md` §5.2 để xác minh ý nghĩa từng byte. Nếu là legacy protocol requirement thì thay magic number bằng constant có tên (`WRITE_RAM_DUMMY_HI`, `WRITE_RAM_ENABLE_FLAG`) kèm comment giải thích; nếu không có justification thì loại bỏ. Cấm để byte rác không có nghĩa trong frame (nợ kỹ thuật). |
| A4 | `src/agu_legacy_codec.cpp` — Cập nhật `decodeBurstRam` | [ ] QA Review | Hidden – kiểm tra `verifyZeroSumChecksum(inBuf, 9, inBuf[8])` trước khi giải mã; nếu fail trả `false` để caller KHÔNG update telemetry. Đọc 8 byte RAM + 1 byte checksum đúng thứ tự theo contract. Fail-closed: không bao giờ trả dữ liệu hợp lệ từ frame checksum sai. |
| A5 | `include/agu_legacy_rf_host.h` — Thêm `READ_RAM_BURST` vào enum `AguRfCommand` | [ ] QA Review | Hidden – enum value dùng `SCREAMING_SNAKE_CASE` (quy ước tên firmware Section 3.2 README). Chỉ thêm vào enum, không sửa điểm dùng khác trong task này (SRP, tách tác vụ). |
| A6 | `src/agu_legacy_rf_host.cpp` — Implement `readRamBurst(node_id, addr, out_data8)` | [ ] QA Review | Hidden – luồng bắt buộc: validate `isValidNodeId()` → encode qua `encodeReadRamBurst(nodeId, addr, 8, ...)` → flush RX → send → chờ response (timeout 300ms) → nhận 9 byte → `decodeBurstRam()` → trả `AguRfTransactionResult` (ACKED / TIMEOUT / UNEXPECTED_RESPONSE). Bắt buộc retry đúng `AGU_LEGACY_MAX_ATTEMPTS = 3`, giữ nguyên frame khi retry, KHÔNG retry vô hạn. Trả struct có trường `result`, cấm hàm `void` quan trọng. |

### 4.2 TRACK B — UART HC-12 FreeRTOS Core 1 Isolation

| Task ID | Mô tả Task | Status | Note (chỉ thị kỹ thuật bắt buộc) |
|---|---|---|---|
| B1 | `include/config.h` — Thêm hằng số UART isolation & anti-overrun (Section 2 + 4) | [ ] Pending | Hidden – thêm toàn bộ constant theo sprint_1.md Task B-1 (`RF_UART_HC12_BAUD_RATE=9600`, `RF_UART_RX_TASK_CORE=1`, `RF_UART_RX_TASK_PRIORITY=4`, `RF_UART_RING_BUFFER_SIZE=512` power-of-2, `RF_UART_RX_QUEUE_DEPTH=64`); bắt buộc `static_assert` cho ring buffer ≥ 256 và `RF_UART_RX_TASK_PRIORITY > MQTT_TASK_PRIORITY` (chống priority inversion). Hằng số dùng `SCREAMING_SNAKE_CASE`. |
| B2 | `include/uart_rf_transport.h` — Thêm ring buffer state, FreeRTOS handles, task/ISR interface | [ ] Pending | Hidden – `UartRfTransport` phải triển khai `IRfTransport` (Dependency Inversion). Thêm `startRxTask()`, `stopRxTask()`, `getDroppedBytes()`, `getRxOverflows()`; member dùng prefix_ `_ring_buffer`, `_ring_head`, `_ring_tail` (quy ước Section 3.2). Ring buffer phải bounded, không heap trong loop. |
| B3 | `src/uart_rf_transport.cpp` — Implement ring buffer + Core 1 consumer task + ISR handler | [ ] Pending | Hidden – tuân thủ S1-UART-03 & S1-UART-04: ISR chạy trên Core 1, KHÔNG blocking call (delay/malloc/printf) trong ISR; buffer full thì drop byte + tăng `_dropped_bytes`/`_rx_overflows`, KHÔNG ghi đè tail, KHÔNG block ISR. Cấm `malloc/new` trong ISR hoặc `rxTaskLoop()` (cấp 1 lần trong `begin()`, phải check nullptr). `xQueueSendFromISR` + `portYIELD_FROM_ISR` đúng pattern. Sau khi ring buffer active, `receive()`/`available()` phải đọc từ ring buffer; giữ đường cũ sau `#if !defined(RF_UART_RING_BUFFER_ACTIVE)`. |
| B4 | `src/main.cpp` — Cập nhật khởi tạo UART transport với Core 1 pinning | [ ] Pending | Hidden – trong `initializeRfTransport()`, dùng `RF_UART_HC12_BAUD_RATE`; gọi `uart.begin()` → `uart.startRxTask()` và xử lý fail (log `ESP_LOGE` + return false). Khởi tạo chỉ 1 lần, không tái khởi tạo task nhiều lần. Không chạy UART RX trên Core 0 cùng Wi-Fi driver (S1-UART-03). |

### 4.3 TRACK C — Cập nhật Caller & Test

| Task ID | Mô tả Task | Status | Note (chỉ thị kỹ thuật bắt buộc) |
|---|---|---|---|
| C1 | `src/main.cpp` — Đồng bộ gọi `encodeReadRamBurst` trong `executeAguPump`/`executeAguPing` | [ ] Pending | Hidden – caller phải gọi `AguLegacyCodec::encodeReadRamBurst(node_id, addr, 8, buf, size)` thay vì overload cũ chưa có nodeId (S1-CODEC-02). Đưa đúng argument thứ 1 là node_id thực tế từ caller, không hardcode 0x01. |
| C2 | `test/test_codec/test_codec.cpp` — Cập nhật unit test theo signature mới | [ ] Pending | Hidden – test vector: `encodeReadRamBurst(4, 0x0100, 8, buf, sizeof(buf))` → REQUIRE(len == 7); checksum `sum == 0` trên 7 byte. Test reject count != 8 → REQUIRE(return == 0). Kiểm tra 273/273 test vẫn PASS. |
| C3 | `test/test_transport/test_transport.cpp` — Test anti-overrun ring buffer | [ ] Pending | Hidden – test buffer full khi inject byte past capacity: dropped_bytes increment, tail unchanged, head wrap. Test ISR notification đánh thức consumer task xử lý byte đúng order. Kiểm tra 273/273 test PASS. |

> File PROGRESS.md đã được khởi tạo tại `.ai/planning/refactor-phase/PROGRESS.md` kèm theo cấu trúc định dạng Markdown chuẩn, Track A/B/C từ Sprint 1 đã được chuyển hóa thành các bảng 4 cột (Task ID / Mô tả Task / Status / Note chỉ thị kỹ thuật bắt buộc) và tất cả Status khởi tạo là `[ ] Pending`.
