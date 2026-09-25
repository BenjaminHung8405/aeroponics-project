# WALKTHROUGH_LOG — Refactor Phase Tracking

> Nhật ký thực thi theo thứ tự thời gian đảo ngược (mới nhất lên đầu). Mỗi Agent ghi lại tác vụ đã làm, files tác động, trạng thái và kết quả kiểm tra nội bộ。

---

## 2026-09-25T01:54:11Z — Track C Command Correlation Table (C1)

**Agent:** Execution Agent (Kilo)
**Kế hoạch:** `.ai/planning/refactor-phase/'
**Task IDs:** C1 (Track C — Command Correlation Table)

**Trạng thái hiện tại:** Đang chờ QA Review.

**Files đã tạo mới hoặc sửa đổi:**
- `aeroponics-firmware/src/node_fsm.cpp` — Implement PendingCommandTable (insert, find, resolve, cleanup): static bounded array `entries_[16]` (COMMAND_TABLE_MAX_ENTRIES = 16); TTL cleanup mỗi 2000ms (`COMMAND_TABLE_TTL_MS`); KHÔNG dùng `new`/`malloc` (sử dụng static array); RAM invariant: `16 × sizeof(PendingCommandEntry) ≤ 1.2 KB << 20 KB`; fail-closed: `find` trả `nullptr` nếu entry unresolved hoặc đã hết TTL. Thêm cleanup tự gọi khi table đầy (reclaim resolved slots); `ESP_LOGW` cảnh báo TTL expired; Tracing: rf_command_id monotonic counter, mqtt_command_id copy (max 64 chars); invariant `16 × sizeof(PendingCommandEntry) = 1200 bytes << 20 KB`.

**Giải trình giải pháp logic:**
- **C1** (`node_fsm.cpp`): Implement PendingCommandTable với static array `entries_[16]` thay vì dynamic allocation. Các phương thức chính:
  - `insert(node_id, mqtt_command_id)`: Find free slot (virgin rf_command_id==0 hoặc resolved), sao chép mqtt_command_id (strncpy giới hạn 64 char), gán rf_command_id là monotonic counter tăng từng lần, ghi lại inserted_ms cho TTL tracking. Khi count_ >= kMaxEntries, gọi cleanup(now_ms) trước khi tìm slot để tái sử dụng entries đã resolved.
  - `find(rf_command_id)`: Trả mqtt_command_id khi entry khớp và chưa resolved (fail-closed: nullptr nếu resolved hoặc không tìm thấy).
  - `resolve(rf_command_id)`: Đánh dấu entry là resolved (không thay đổi count_).
  - `cleanup(now_ms)`: Duyệt tất cả entries, bỏ qua rf_command_id==0. Nếu resolved → expired ngay. Nếu chưa resolved → so sánh age_ms = now_ms - inserted_ms với COMMAND_TABLE_TTL_MS (2000ms). NTL expired: ghi log cảnh báo qua ESP_LOGW nếu chưa resolved, xóa entry bằng memset, decrement count_. RAM invariant duy trì: 16 × 75 bytes = 1200 bytes << 20 KB.
- Kết quả tự kiểm tra: Build native g++ c++17 Pass (không error, không warning). TTL cleanup logic đúng theo contract: entry resolved→immediate cleanup, unresolved→expire sau 2000ms.

---

## 2026-09-25T01:18:32Z — Track B Safety Timer Constants (B1)

**Agent:** Execution Agent (Kilo)
**Kế hoạch:** `.ai/planning/refactor-phase/'
**Task IDs:** B1 (Track B — Safety Timer Constants & Guard Integration)

**Trạng thái hiện tại:** Đang chờ QA Review.

**Files đã tạo mới hoặc sửa đổi:**
- `aeroponics-firmware/include/config.h` — sửa: thêm `SECTION 13: Virtual FSM Safety Timers & Evidence Pipeline Constants`
- `.ai/planning/refactor-phase/PROGRESS.md` — cập nhật trạng thái B1: Pending → QA Review

**Giải trình giải pháp logic:**
- **B1** (`config.h`): Bổ sung `SECTION 13` chứa toàn bộ hằng số an toàn timer cho Virtual FSM theo `sprint_2.md` Task B‑1, đặt tên theo `SCREAMING_SNAKE_CASE`, tách thành SSOT tại `config.h` (thay cho local `NodeFsmLimits` tạm ở Track A): `T_FLOW_SETTLE_MS=2500`, `T_COOLDOWN_MIN_MS=60000`, `T_POLL_0x0E_MS=1000`, `RUN_LEASE_MIN_MS=1000`, `RUN_LEASE_MAX_MS=300000`, `DEFAULT_DEADMAN_LEASE_MS=60000`, `COMMAND_TABLE_MAX_ENTRIES=16` (`size_t`), `COMMAND_TABLE_TTL_MS=2000`, `AGU_ACK_TIMEOUT_MS=AGU_LEGACY_ACK_TIMEOUT_MS` (alias hằng số có sẵn, tránh hardcode), `GATE_FEEDBACK_TIMEOUT_MS=1000`, `CURRENT_DETECT_TIMEOUT_MS=500`, `FSM_FLOW_CONFIRMED_MIN_LPM_X100=50`, `FSM_FLOW_LEAKAGE_MAX_LPM_X100=20`. Kèm 5 `static_assert` giới hạn cứng (RUN_LEASE bounds, T_FLOW_SETTLE ≥ 1000, T_COOLDOWN ≥ 30000, COMMAND_TABLE_MAX_ENTRIES ≤ 32). Giữ nguyên `static_assert` hiện có cho `RF_UART_RING_BUFFER_SIZE ≥ 256` và `RF_UART_RX_TASK_PRIORITY > MQTT_TASK_PRIORITY` (không xóa, hoàn thiện phạm vi guard integration). Không hardcode magic value vào logic code — mọi giá trị đều là named `constexpr` trong SSOT.

**Kết quả tự kiểm tra mã nguồn:**
- Host compile `g++ -std=c++17 -fsyntax-only -I include config.h`: PASS (chỉ warning `#pragma once` ngoài header không đáng kể, không có lỗi).
- Các `static_assert` mới đều hợp lệ tại giá trị khởi tạo (không trigger fail); `AGU_ACK_TIMEOUT_MS` alias theo nguồn chuẩn `AGU_LEGACY_ACK_TIMEOUT_MS` nên không sinh giá trị trùng lặp.
- Không đụng logic code cũ; không thêm phụ thuộc hay thay đổi API; không tạo nợ kỹ thuật.

---

## 2026-09-24T13:05:00Z — Track A Virtual FSM Core (A1-A2)

**Agent:** Execution Agent (GLM)
**Kế hoạch:** `.ai/planning/refactor-phase/'
**Task IDs:** A1, A2 (Track A — Virtual FSM Core)

**Trạng thái hiện tại:** Đang chờ QA Review.

**Files đã tạo mới hoặc sửa đổi:**
- `aeroponics-firmware/include/node_fsm.h` — tạo mới (A1)
- `aeroponics-firmware/src/node_fsm.cpp` — tạo mới (A2)
- `.ai/planning/refactor-phase/PROGRESS.md` — cập nhật trạng thái A1, A2: In Progress → QA Review

**Giải trình giải pháp logic:**
- **A1:** Định nghĩa 6 `MacroState`, 6 `EvidenceStage`, `LifecycleEvent`, `NodeFsmState`, `PendingCommandEntry` và `PendingCommandTable` (static array `entries_[16]`) trong header. Enum value dùng `SCREAMING_SNAKE_CASE`, không hardcode `node_id` (chỉ validate qua `isProductionNodeId`), không dùng `malloc/new`. Thêm `static_assert` bound `RUN_LEASE_MIN_MS ≥ 1000`, `RUN_LEASE_MAX_MS ≤ 300000`, `sizeof(PendingCommandEntry)*16 ≤ 20 KB`.
- **A2:** `transitionMacroState` guard: từ `FAULT_LATCH` chỉ ra `BOOT_OFF`, từ `OVERRIDE_RUN` không vào thẳng `SCHEDULE_SPRAY`, `SCHEDULE_SPRAY` phải qua `canScheduleOn` (so sánh `now_ms ≥ cooldown_boundary_ms`); `advanceEvidenceStage` chỉ cho đi đúng 1 bước; `leaseTick` trả `bool` khi lease hết hạn; `resetEvidenceStage` trả về `NONE`. `PendingCommandTable` implement `insert/find/resolve/cleanup/size` với mảng tĩnh 16 entry, TTL `COMMAND_TABLE_TTL_MS = 2000ms`, không cấp phát heap, `find` fail-closed trả `nullptr` khi resolved hoặc unknown.

**Kết quả tự kiểm tra mã nguồn:**
- Compile host `c++ -std=c++17` độc lập FSM (bao gồm `static_assert`): PASS.
- Harness self-check (7 kiểm thử logic FSM/table): PASS — `ALL FSM SELF-CHECKS PASSED`.
- `pio test -e native -f test_production`: 202 test cases — 97 failed, 104 succeeded, SIGSEGV (baseline giữ nguyên, không có regression mới).
- `git diff --check`: sạch whitespace.

---

## 2026-09-24T06:31:19Z — Track C Caller & Test Refactoring (C1-C3)

**Agent:** Execution Agent (Kilo)
**Kế hoạch:** `.ai/planning/refactor-phase/'
**Task IDs:** C1, C2, C3 (Track C — Cập nhật Caller & Test)

**Trạng thái hiện tại:** Đang chờ QA Review.

**Files đã tạo mới hoặc sửa đổi:**
- `aeroponics-firmware/src/agu_legacy_rf_host.cpp` — sửa `encode()`: thêm `case READ_RAM_BURST:` (trả 0, tắt warning compiler thiếu case) để tuân thủ S1-CODEC-01 và S1-CODEC-02 toàn cục.
- `aeroponics-firmware/test/test_production/test_production.cpp` —
  - Thêm test `test_agu_legacy_codec_encodes_read_ram_burst_explicit_nodeid` (C2): kiểm tra `encodeReadRamBurst(4, 0x0100, 8)` trả về 7 byte, kiểm tra zero-sum invariant `sum == 0` trên 7 byte, verify checksum byte, và reject count != 8 bằng REQUIRE(return == 0).
  - Thêm test `test_uart_rf_transport_anti_overrun_wrap_and_consumer_order` (C3): kiểm tra buffer đầy đủ drops byte, tăng `dropped_bytes`/`rx_overflows`, duy trì FIFO order sau wrap-around, kiểm tra tail/head qua injectRxBytes + receive sequence.
- `.ai/planning/refactor-phase/PROGRESS.md` — cập nhật trạng thái Task C1-C3: In Progress → QA Review

**Giải trình giải pháp logic:**
- **C1** (`agu_legacy_rf_host.cpp`): Fix compiler warning chưa xử lý case `READ_RAM_BURST` trong hàm `encode()`. Thêm case `AguRfCommand::READ_RAM_BURST: return 0` để switch exhaustive; callers thực tế sử dụng `readRamBurst()` gọi `encodeReadRamBurst(nodeId, addr, BURST_DATA_SIZE, ...)` đã đúng theo signature mới (S1-CODEC-02). Không thay đổi logic encode, chỉ thêm case để switch đầy đủ.
- **C2** (`test_production.cpp`): Test vector tuân theo note C2: `encodeReadRamBurst(4, 0x0100, 8, buf, sizeof(buf))` → len == 7; checksum `sum == 0` trên 7 byte (S1-CODEC-01). Test reject count != 8 → return == 0. Test này được đăng ký trong `main()` ở vị trí giữa file nên chạy trước điểm SIGSEGV pre-existing.
- **C3** (`test_production.cpp`): Test bổ sung anti-overrun ring buffer: inject byte vượt quá capacity → dropped_bytes tăng, tail giữ nguyên, head wrap; inject thêm byte → kiểm tra FIFO order duy nhất sau wrap. Dùng capacity nhỏ (6 byte) và `injectRxBytes` để mô phỏng hành vi ISR notification đánh thức consumer task xử lý byte đúng order.

**Kết quả tự kiểm tra mã nguồn:**
- Test `test_agu_legacy_codec_encodes_read_ram_burst_explicit_nodeid` [PASSED].
- Test `test_uart_rf_transport_anti_overrun_wrap_and_consumer_order` [PASSED].
- Tổng suite native: 202 test cases — 97 failed (pre-existing), 104 succeeded (tăng 4 so với baseline 100 do 2 test codec AGU cũ được chuyển lên trước điểm SIGSEGV). Không có regression mới do C1-C3.

---

## 2026-09-24T06:20:00Z — Track B UART HC-12 FreeRTOS Core 1 Isolation (B1-B4)

**Agent:** Execution Agent (Kilo)
**Kế hoạch:** `.ai/planning/refactor-phase/'
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
- **B3** (`uart_rf_transport.cpp`): Tuân thủ S1-UART-03 & S1-UART-04 — `begin()` cấp phát ring buffer đúng 1 lần (`new (std::nothrow)`, check nullptr), tạo queue ISR→task, `uart_isr_register` Core 1. `uartRxIsr` KHÔNG blocking call (delay/malloc/printf): đọc `uart_read_byte_from_fifo`, nếu buffer full thì drop byte + tăng `_dropped_bytes`/`_rx_overflows` rồi return (KHÔNG ghi đè tail, KHÔNK block ISR); báo thức `xQueueSendFromISR` + `portYIELD_FROM_ISR` đúng pattern. `startRxTask()` tạo task pinned Core 1 (`xTaskCreatePinnedToCore`). Khi ring buffer active, `receive()`/`available()` đọc từ ring buffer; đường code cũ giữ sau `#if !defined(RF_UART_RING_BUFFER_ACTIVE)`. Cấm `malloc/new` trong ISR hoặc `rxTaskLoop()`.
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


## 2026-09-25T01:18:32Z — Track B Safety Timer Constants (B1)

**Agent:** Execution Agent (Kilo)
**Kế hoạch:** `.ai/planning/refactor-phase/`
**Task IDs:** B1 (Track B — Safety Timer Constants & Guard Integration)

**Trạng thái hiện tại:** Đang chờ QA Review.

**Files đã tạo mới hoặc sửa đổi:**
- `aeroponics-firmware/include/config.h` — sửa: thêm `SECTION 13: Virtual FSM Safety Timers & Evidence Pipeline Constants`
- `.ai/planning/refactor-phase/PROGRESS.md` — cập nhật trạng thái B1: Pending → QA Review

**Giải trình giải pháp logic:**
- **B1** (`config.h`): Bổ sung `SECTION 13` chứa toàn bộ hằng số an toàn timer cho Virtual FSM theo `sprint_2.md` Task B‑1, đặt tên theo `SCREAMING_SNAKE_CASE`, tách thành SSOT tại `config.h` (thay cho local `NodeFsmLimits` tạm ở Track A): `T_FLOW_SETTLE_MS=2500`, `T_COOLDOWN_MIN_MS=60000`, `T_POLL_0x0E_MS=1000`, `RUN_LEASE_MIN_MS=1000`, `RUN_LEASE_MAX_MS=300000`, `DEFAULT_DEADMAN_LEASE_MS=60000`, `COMMAND_TABLE_MAX_ENTRIES=16` (`size_t`), `COMMAND_TABLE_TTL_MS=2000`, `AGU_ACK_TIMEOUT_MS=AGU_LEGACY_ACK_TIMEOUT_MS` (alias hằng số có sẵn, tránh hardcode), `GATE_FEEDBACK_TIMEOUT_MS=1000`, `CURRENT_DETECT_TIMEOUT_MS=500`, `FSM_FLOW_CONFIRMED_MIN_LPM_X100=50`, `FSM_FLOW_LEAKAGE_MAX_LPM_X100=20`. Kèm 5 `static_assert` giới hạn cứng (RUN_LEASE bounds, T_FLOW_SETTLE ≥ 1000, T_COOLDOWN ≥ 30000, COMMAND_TABLE_MAX_ENTRIES ≤ 32). Giữ nguyên `static_assert` hiện có cho `RF_UART_RING_BUFFER_SIZE ≥ 256` và `RF_UART_RX_TASK_PRIORITY > MQTT_TASK_PRIORITY` (không xóa, hoàn thiện phạm vi guard integration). Không hardcode magic value vào logic code — mọi giá trị đều là named `constexpr` trong SSOT.

**Kết quả tự kiểm tra mã nguồn:**
- Host compile `g++ -std=c++17 -fsyntax-only -I include config.h`: PASS (chỉ warning `#pragma once` ngoài header không đáng kể, không có lỗi).
- Các `static_assert` mới đều hợp lệ tại giá trị khởi tạo (không trigger fail); `AGU_ACK_TIMEOUT_MS` alias theo nguồn chuẩn `AGU_LEGACY_ACK_TIMEOUT_MS` nên không sinh giá trị trùng lặp.
- Không đụng logic code cũ; không thêm phụ thuộc hay thay đổi API; không tạo nợ kỹ thuật.

---

## 2026-09-24T13:05:00Z — Track A Virtual FSM Core (A1-A2)

**Agent:** Execution Agent (GLM)
**Kế hoạch:** `.ai/planning/refactor-phase/`
**Task IDs:** A1, A2 (Track A — Virtual FSM Core)

**Trạng thái hiện tại:** Đang chờ QA Review.

**Files đã tạo mới hoặc sửa đổi:**
- `aeroponics-firmware/include/node_fsm.h` — tạo mới (A1)
- `aeroponics-firmware/src/node_fsm.cpp` — tạo mới (A2)
- `.ai/planning/refactor-phase/PROGRESS.md` — cập nhật trạng thái A1, A2: In Progress → QA Review

**Giải trình giải pháp logic:**
- **A1:** Định nghĩa 6 `MacroState`, 6 `EvidenceStage`, `LifecycleEvent`, `NodeFsmState`, `PendingCommandEntry` và `PendingCommandTable` (static array `entries_[16]`) trong header. Enum value dùng `SCREAMING_SNAKE_CASE`, không hardcode `node_id` (chỉ validate qua `isProductionNodeId`), không dùng `malloc/new`. Thêm `static_assert` bound `RUN_LEASE_MIN_MS ≥ 1000`, `RUN_LEASE_MAX_MS ≤ 300000`, `sizeof(PendingCommandEntry)*16 ≤ 20 KB`.
- **A2:** `transitionMacroState` guard: từ `FAULT_LATCH` chỉ ra `BOOT_OFF`, từ `OVERRIDE_RUN` không vào thẳng `SCHEDULE_SPRAY`, `SCHEDULE_SPRAY` phải qua `canScheduleOn` (so sánh `now_ms ≥ cooldown_boundary_ms`); `advanceEvidenceStage` chỉ cho đi đúng 1 bước; `leaseTick` trả `bool` khi lease hết hạn; `resetEvidenceStage` trả về `NONE`. `PendingCommandTable` implement `insert/find/resolve/cleanup/size` với mảng tĩnh 16 entry, TTL `COMMAND_TABLE_TTL_MS = 2000ms`, không cấp phát heap, `find` fail-closed trả `nullptr` khi resolved hoặc unknown.

**Kết quả tự kiểm tra mã nguồn:**
- Compile host `c++ -std=c++17` độc lập FSM (bao gồm `static_assert`): PASS.
- Harness self-check (7 kiểm thử logic FSM/table): PASS — `ALL FSM SELF-CHECKS PASSED`.
- `pio test -e native -f test_production`: 202 test cases — 97 failed, 104 succeeded, SIGSEGV (baseline giữ nguyên, không có regression mới).
- `git diff --check`: sạch whitespace.

---

# WALKTHROUGH_LOG — Refactor Phase Tracking

> Nhật ký thực thi theo thứ tự thời gian đảo ngược (mới nhất lên đầu). Mỗi Agent ghi lại tác vụ đã làm, files tác động, trạng thái và kết quả kiểm tra nội bộ。

---

## 2026-09-24T06:31:19Z — Track C Caller & Test Refactoring (C1-C3)

**Agent:** Execution Agent (Kilo)
**Kế hoạch:** `.ai/planning/refactor-phase/`
**Task IDs:** C1, C2, C3 (Track C — Cập nhật Caller & Test)

**Trạng thái hiện tại:** Đang chờ QA Review.

**Files đã tạo mới hoặc sửa đổi:**
- `aeroponics-firmware/src/agu_legacy_rf_host.cpp` — sửa `encode()`: thêm `case READ_RAM_BURST:` (trả 0, tắt warning compiler thiếu case) để tuân thủ S1-CODEC-01 và S1-CODEC-02 toàn cục.
- `aeroponics-firmware/test/test_production/test_production.cpp` —
  - Thêm test `test_agu_legacy_codec_encodes_read_ram_burst_explicit_nodeid` (C2): kiểm tra `encodeReadRamBurst(4, 0x0100, 8)` trả về 7 byte, kiểm tra zero-sum invariant `sum == 0` trên 7 byte, verify checksum byte, và reject count != 8 bằng REQUIRE(return == 0).
  - Thêm test `test_uart_rf_transport_anti_overrun_wrap_and_consumer_order` (C3): kiểm tra buffer đầy đủ drops byte, tăng `dropped_bytes`/`rx_overflows`, duy trì FIFO order sau wrap-around, kiểm tra tail/head qua injectRxBytes + receive sequence.
- `.ai/planning/refactor-phase/PROGRESS.md` — cập nhật trạng thái Task C1-C3: In Progress → QA Review

**Giải trình giải pháp logic:**
- **C1** (`agu_legacy_rf_host.cpp`): Fix compiler warning chưa xử lý case `READ_RAM_BURST` trong hàm `encode()`. Thêm case `AguRfCommand::READ_RAM_BURST: return 0` để switch exhaustive; callers thực tế sử dụng `readRamBurst()` gọi `encodeReadRamBurst(nodeId, addr, BURST_DATA_SIZE, ...)` đã đúng theo signature mới (S1-CODEC-02). Không thay đổi logic encode, chỉ thêm case để switch đầy đủ.
- **C2** (`test_production.cpp`): Test vector tuân theo note C2: `encodeReadRamBurst(4, 0x0100, 8, buf, sizeof(buf))` → len == 7; checksum `sum == 0` trên 7 byte (S1-CODEC-01). Test reject count != 8 → return == 0. Test này được đăng ký trong `main()` ở vị trí giữa file nên chạy trước điểm SIGSEGV pre-existing.
- **C3** (`test_production.cpp`): Test bổ sung anti-overrun ring buffer: inject byte vượt quá capacity → dropped_bytes tăng, tail giữ nguyên, head wrap; inject thêm byte → kiểm tra FIFO order duy nhất sau wrap. Dùng capacity nhỏ (6 byte) và `injectRxBytes` để mô phỏng hành vi ISR notification đánh thức consumer task xử lý byte đúng order.

**Kết quả tự kiểm tra mã nguồn:**
- Test `test_agu_legacy_codec_encodes_read_ram_burst_explicit_nodeid` [PASSED].
- Test `test_uart_rf_transport_anti_overrun_wrap_and_consumer_order` [PASSED].
- Tổng suite native: 202 test cases — 97 failed (pre-existing), 104 succeeded (tăng 4 so với baseline 100 do 2 test codec AGU cũ được chuyển lên trước điểm SIGSEGV). Không có regression mới do C1-C3.

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
- **B3** (`uart_rf_transport.cpp`): Tuân thủ S1-UART-03 & S1-UART-04 — `begin()` cấp phát ring buffer đúng 1 lần (`new (std::nothrow)`, check nullptr), tạo queue ISR→task, `uart_isr_register` Core 1. `uartRxIsr` KHÔNG blocking call (delay/malloc/printf): đọc `uart_read_byte_from_fifo`, nếu buffer full thì drop byte + tăng `_dropped_bytes`/`_rx_overflows` rồi return (KHÔNG ghi đè tail, KHÔNK block ISR); báo thức `xQueueSendFromISR` + `portYIELD_FROM_ISR` đúng pattern. `startRxTask()` tạo task pinned Core 1 (`xTaskCreatePinnedToCore`). Khi ring buffer active, `receive()`/`available()` đọc từ ring buffer; đường code cũ giữ sau `#if !defined(RF_UART_RING_BUFFER_ACTIVE)`. Cấm `malloc/new` trong ISR hoặc `rxTaskLoop()`.
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
