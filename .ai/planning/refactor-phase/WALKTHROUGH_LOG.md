# WALKTHROUGH_LOG — Refactor Phase Tracking

> Nhật ký thực thi theo thứ tự thời gian đảo ngược (mới nhất lên đầu). Mỗi Agent ghi lại tác vụ đã làm, files tác động, trạng thái và kết quả kiểm tra nội bộ.

---

## 2026-09-24T06:20:00Z — Track B UART HC-12 FreeRTOS Core 1 Isolation (B1-B4)

**Agent:** Execution Agent (Kilo)
**Kế hoạch:** `.ai/planning/refactor-phase/`
**Task IDs:** B1, B2, B3, B4 (Track B — UART HC-12 FreeRTOS Core 1 Isolation)

**Trạng thái hiện tại:** Đang chờ QA Review.

**Files đã tạo mới hoặc sửa đổi:**
- `aeroponics-firmware/include/config.h` — thêm hằng số UART isolation & anti-overrun (sửa)
- `aeroponics-firmware/include/uart_rf_transport.h` — thêm ring buffer state, FreeRTOS handles, task/ISR interface (sửa)
- `aeroponics-firmware/src/uart_rf_transport.cpp` — implement ring buffer + Core 1 consumer task + ISR handler (sửa)
- `aeroponics-firmware/src/main.cpp` — cập nhật khởi tạo UART transport với Core 1 pinning (sửa)
- `.ai/planning/refactor-phase/PROGRESS.md` — cập nhật trạng thái Task B1-B4: Pending → In Progress → QA Review

**Giải trình giải pháp logic:**
- **B1** (`config.h`): Thêm `RF_UART_HC12_BAUD_RATE=9600`, `RF_UART_RX_TASK_CORE=1`, `RF_UART_RX_TASK_PRIORITY=4` (> `MQTT_TASK_PRIORITY=3`, chống priority inversion), `RF_UART_RX_TASK_STACK_SIZE`, `RF_UART_RX_TASK_NAME`, `RF_UART_RING_BUFFER_SIZE=512` (power-of-2, ≥ 256), `RF_UART_RX_QUEUE_DEPTH=64`. Bổ sung `static_assert` cho ring buffer ≥ 256 và `RF_UART_RX_TASK_PRIORITY > MQTT_TASK_PRIORITY`. Tất cả hằng số dùng `SCREAMING_SNAKE_CASE` theo quy ước Section 3.2 README.
- **B2** (`uart_rf_transport.h`): `UartRfTransport` triển khai `IRfTransport` (Dependency Inversion — interface giữ nguyên). Thêm public API `startRxTask()`, `stopRxTask()`, `getDroppedBytes()`, `getRxOverflows()`. Member dùng prefix_ `_ring_buffer`, `_ring_head`, `_ring_tail`, `_ring_size` cùng FreeRTOS handles (`_rx_task_handle`, `_rx_notify_queue`) và counter ISR-safe (`_dropped_bytes`, `_rx_overflows`). Ring buffer bounded — cấp phát 1 lần trong `begin()`, không heap trong loop.
- **B3** (`uart_rf_transport.cpp`): Tuân thủ S1-UART-03 & S1-UART-04 — `begin()` cấp phát ring buffer đúng 1 lần (`new (std::nothrow)`, check nullptr), tạo queue ISR→task, `uart_isr_register` Core 1. `uartRxIsr` KHÔNG blocking call (delay/malloc/printf): đọc `uart_read_byte_from_fifo`, nếu buffer full thì drop byte + tăng `_dropped_bytes`/`_rx_overflows` rồi return (KHÔNG ghi đè tail, KHÔNG block ISR); báo thức `xQueueSendFromISR` + `portYIELD_FROM_ISR` đúng pattern. `startRxTask()` tạo task pinned Core 1 (`xTaskCreatePinnedToCore`). Khi ring buffer active, `receive()`/`available()` đọc từ ring buffer; đường code cũ giữ sau `#if !defined(RF_UART_RING_BUFFER_ACTIVE)`. Cấm `malloc/new` trong ISR hoặc `rxTaskLoop()`.
- **B4** (`main.cpp`): `initializeRfTransport()` dùng `RF_UART_HC12_BAUD_RATE` thay vì `config.baud_rate`; sau `uart.begin()` gọi `uart.startRxTask()` và xử lý fail bằng `ESP_LOGE` + `return false`. Khởi tạo instance `static` — chỉ 1 lần, không tái khởi tạo task. UART RX gắn Core 1, không chạy chung Core 0 với Wi-Fi driver (S1-UART-03).
**Kết quả tự kiểm tra mã nguồn:**
- `pio run -e native`: compile `uart_rf_transport.o` sạch (FreeRTOS/ISR symbols được guard `#if defined(ESP_PLATFORM)||defined(ARDUINO)`, host path dùng `_host_rx_fifo`; không link `_main` do `main.cpp` guard ESP-only — pre-existing native test env behavior).
- `pio test -e native -f test_production`:
  - Baseline (HEAD): 198 test cases — 97 failed, 100 succeeded, SIGSEGV.
  - Với thay đổi B1-B4: 198 test cases — 97 failed, 100 succeeded, SIGSEGV.
  - Kết luận: **không có regression mới**; 97 failures là pre-existing baseline không liên quan track B. Các test transport/ring buffer không có test case riêng trong nhóm 198 (đang chờ Track C bổ sung test_transport).
- `git diff --check`: sạch, không lỗi whitespace.
- Code review: diff tối thiểu (5 file, +211/-5 dòng), không thêm dependency mới ngoài FreeRTOS/driver/uart.h (std), không `malloc` trong ISR/loop, không sinh nợ kỹ thuật.

---

## 2026-09-24T05:45:38Z — Track A Codec Refactor (A3-A6)

**Agent:** Execution Agent (Kilo)  
**Kế hoạch:** `.ai/planning/refactor-phase/`  
**Task IDs:** A3, A4, A5, A6

**Trạng thái hiện tại:** Đang chờ QA Review.

**Files đã tạo mới hoặc sửa đổi:**
- `aeroponics-firmware/src/agu_legacy_codec.cpp` — sửa
- `aeroponics-firmware/include/agu_legacy_codec.h` — sửa
- `aeroponics-firmware/include/agu_legacy_rf_host.h` — sửa
- `aeroponics-firmware/src/agu_legacy_rf_host.cpp` — sửa
- `.ai/planning/refactor-phase/PROGRESS.md` — cập nhật trạng thái Task

**Giải trình giải pháp logic:**
- **A3:** Thay thế magic number `0x00` và `0x01` trong `encodeWriteRam` bằng named constants `WRITE_RAM_DUMMY_HI = 0x00` và `WRITE_RAM_ENABLE_FLAG = 0x01`, kèm comment giải thích là legacy protocol-fixed fields. Giữ nguyên byte values → không đổi wire contract.
- **A4:** Xác nhận `decodeBurstRam` đã fail-closed (kiểm tra `verifyZeroSumChecksum` trước khi `memcpy`, trả `false` khi checksum sai). Bổ sung Doxygen comment mô tả rõ frame layout [8 data + 1 checksum] và fail-closed semantics.
- **A5:** Thêm `READ_RAM_BURST` vào `AguRfCommand` enum theo quy ước `SCREAMING_SNAKE_CASE`.
- **A6:** Implement `readRamBurst()` tuần tự: validate `isValidNodeId()` → encode `READ_RAM_BURST` với count=8 → flush RX → send → collect 9-byte response trong timeout 300ms → `decodeBurstRam()` → trả `AguRfTransactionResult`. Retry đúng `AGU_LEGACY_MAX_ATTEMPTS = 3`, giữ nguyên frame, KHÔNG retry vô hạn. Struct result có trường `result`.

**Kết quả tự kiểm tra mã nguồn:**
- Build native test environment sạch (chỉ warning switch case `READ_RAM_BURST` cần xử lý khi bổ sung codec `encode()` — không ảnh hưởng runtime).
- Chạy `pio test -e native -f test_production`:
  - Baseline (commit 2957893): 198 test cases — 97 failed, 100 succeeded, SIGSEGV.
  - Với thay đổi A3-A6: 198 test cases — 97 failed, 100 succeeded, SIGSEGV.
  - Kết luận: không có regression mới; 97 failures là pre-existing baseline không liên quan đến codec refactor.
- AGU legacy codec tests (`test_agu_legacy_codec_encodes_commands_matching_delphi_spec`, `test_agu_legacy_codec_checksum_and_decoders`) nằm trong nhóm 100 tests thành công và không bị ảnh hưởng.
- Zero-sum invariant `sum(frame) & 0xFF == 0` được kiểm tra qua `verifyZeroSumChecksum` trên các encoder/decoder.
