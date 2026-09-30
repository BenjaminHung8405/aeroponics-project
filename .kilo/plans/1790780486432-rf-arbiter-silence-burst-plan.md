# Phase 2 Implementation Plan: RF Arbiter, Radio Silence Window & Burst/Staggered Transmission

## 1. Mục tiêu và ranh giới

- Loại bỏ tranh chấp UART/RF giữa lệnh cơ cấu chấp hành và PING/GET_PUMP_STATE; không để một lệnh sống còn bị chờ sau telemetry.
- Làm PUMP_OFF chịu nhiễu tốt hơn bằng ba lần phát cùng một frame, cách nhau 25–35 ms.
- Giữ nguyên wire frame CRC16-Modbus hiện tại; không thay đổi `GroupScheduler` hoặc thuật toán tính ON/COOLDOWN trong Phase 2.
- Phạm vi mã chính:
  - `aeroponics-firmware/include/agu_legacy_rf_host.h`
  - `aeroponics-firmware/src/agu_legacy_rf_host.cpp`
  - `aeroponics-firmware/src/main.cpp`, đặc biệt `executeAguPing`, `serviceAguLivenessTick`, `executeAguPump`, `executeGroupPump`/`setGroupPump`.
  - Chỉ mở rộng `agu_legacy_codec.*` nếu cần helper kiểm tra opcode/frame; không sửa layout frame.
  - Bổ sung test native trong test suite firmware.

## 2. Current-state analysis và các vấn đề cần xử lý

1. `AguLegacyRfHost::transact()` hiện serialize logic ở cấp object nhưng chưa có FreeRTOS mutex. Các lời gọi dùng chung `IRfTransport` vẫn có thể xen kẽ ở cấp task/caller.
2. `executeAguPump()` dùng cờ `g_agu_bus_busy` và vòng chờ tối đa 1 s. Đây là cờ không nguyên tử, không có priority inheritance, và có thể tạo delay ngay sát deadline chuyển pha.
3. `executeAguPing()` kiểm tra `g_agu_bus_busy`/`g_rf_bus_locked`, đặt cờ rồi gọi `getPumpState()`. Kiểm tra và chiếm bus không atomic với các caller khác.
4. `serviceAguLivenessTick()` chỉ né `g_rf_bus_locked` và trạng thái FSM; nó không biết thời điểm chuyển pha sắp xảy ra, nên PING/GET_PUMP_STATE có thể chiếm UART trong cửa sổ ±1 s.
5. `executeGroupPump()` đặt `g_rf_bus_locked` nhưng phát một frame duy nhất qua `setGroupPump()`. OFF broadcast hiện không có retry/burst; cờ khóa bus chỉ là policy ở `main.cpp`, không bảo vệ được các đường gọi trực tiếp khác.
6. `executeAguPump()` gọi `setPump()` đồng bộ và xử lý ACK. Với broadcast/group, host hiện return sớm sau một frame; với unicast, retry hiện tại là retry transaction chung, chưa biểu đạt burst OFF 3 frame hoặc staggered fan-out 300 ms.
7. `servicePollTelemetry()` và các đường chẩn đoán/scan/AT còn truy cập transport trực tiếp hoặc chỉ dựa vào `g_agu_bus_busy`; các đường này phải được đưa qua cùng arbiter, nếu không mutex mới vẫn bị bypass.
8. Codec hiện tạo envelope `[Len][payload][CRC_Lo][CRC_Hi]`; PUMP frame là `[Len][0x06/0x07][Target_ID][CRC_Lo][CRC_Hi]`, PING có thêm `0xA5` payload. Giữ nguyên các byte này và CRC coverage hiện tại, không tự ý rút PING về frame 4-byte.

## 3. Thiết kế RF Arbiter

### 3.1 API và ownership

- Mở rộng `AguLegacyRfHost` thành điểm vào độc quyền cho mọi transaction legacy:
  - `beginArbiter()`/khởi tạo lazily trong constructor theo khả năng platform.
  - `acquire(RfTrafficClass class, TickType_t timeout)` và `release()` nội bộ; lớp ngoài không được gọi `IRfTransport` trực tiếp cho AGU transaction.
  - `setRadioSilenceWindow(start_ms, end_ms)` hoặc API tương đương để đặt deadline chuyển pha.
  - `setPump()`/`setGroupPump()` tự chọn policy OFF burst; `pingNode()` và `getPumpState()` bị chặn trong silence.
- Thêm enum traffic class tối thiểu: `PUMP_CRITICAL`, `PUMP_NORMAL`, `PING`, `TELEMETRY`, `DIAGNOSTIC`. Pump được admission trước mọi traffic không sống còn.
- Giữ `IRfTransport` không đổi nếu có thể. Nếu cần testability, chỉ thêm clock/yield abstraction hoặc một helper transport mock; không làm thay đổi frame API.

### 3.2 Mutex/semaphore và priority

- ESP32: tạo FreeRTOS mutex (`xSemaphoreCreateMutex`), không dùng binary semaphore; mutex có priority inheritance.
- Mỗi transaction phải giữ mutex xuyên suốt flush RX, send, chờ ACK, retry và burst để không có frame khác chen vào.
- Pump actuation không được polling `g_agu_bus_busy` rồi spin. Thay bằng acquire với timeout ngắn, có `vTaskDelay`/yield; timeout phải trả `BUS_BUSY`/`TIMEOUT` rõ ràng và không phát dở dang.
- Để bảo đảm “pump ưu tiên tuyệt đối”, ưu tiên admission phải nằm trong arbiter, không chỉ dựa vào priority inheritance:
  - Khi có pump pending/đang đến hạn, PING/telemetry không được bắt đầu transaction mới.
  - Không cho phép ping đang giữ mutex kéo dài quá ACK timeout + retry guard; transaction hiện tại hoàn tất nguyên tử, pump lấy bus ngay sau đó.
  - Nếu kiến trúc task được tách ở bước triển khai, tạo RF worker task riêng và queue bounded; queue pump có lane ưu tiên cao, còn ping/telemetry là lane thấp có thể drop/coalesce. Worker phải có priority cao hơn task liveness.
- Trên `UNIT_TEST_HOST`, dùng adapter mutex/condition variable tương đương, deterministic và không phụ thuộc FreeRTOS; API hành vi phải giống ESP32.
- Cờ `g_agu_bus_busy` và `g_rf_bus_locked` chỉ còn là trạng thái/telemetry tương thích. Không dùng chúng làm synchronization primitive; nếu giữ, cập nhật dưới arbiter và không đọc để quyết định chiếm bus.

### 3.3 Radio Silence Window

- Khai báo cấu hình trong `config.h` hoặc header RF (nếu tránh mở rộng config):
  - `RF_RADIO_SILENCE_BEFORE_PHASE_MS = 1000`
  - `RF_RADIO_SILENCE_AFTER_PHASE_MS = 1000`
  - `RF_PUMP_OFF_BURST_COUNT = 3`
  - `RF_PUMP_OFF_BURST_GAP_MS = 30` (cho phép 25–35 ms)
  - `RF_UNICAST_STAGGER_MS = 300`
  - timeout ACK unicast dùng `AGU_LEGACY_ACK_TIMEOUT_MS` hoặc hằng nghiêm ngặt riêng, không vượt ngưỡng đã duyệt.
- Thêm cờ/trạng thái atomically visible `is_radio_silence_active` và monotonic interval `silence_until_ms`; cờ chỉ phục vụ quan sát, quyết định admission dựa trên deadline.
- Trước khi phát ON/OFF, pump arbiter đặt silence interval `[phase_time - before, phase_time + after]`; phase_time phải lấy từ monotonic clock tại thời điểm dispatch dự kiến. OFF burst phải nằm trong cùng ownership window.
- Khi silence active:
  - `PING (0x05)` và `GET_PUMP_STATE (0x08)` bị từ chối/hoãn, tuyệt đối không xếp trước pump.
  - Telemetry 0x0E và các probe định kỳ bị defer/coalesce; không tạo queue backlog sau silence.
  - PUMP_ON/PUMP_OFF được cấp quyền ngay lập tức, không chờ queue ping.
- `serviceAguLivenessTick()` kiểm tra arbiter trước khi cập nhật cursor/`g_last_agu_ping_ms`; nếu bị silence hoặc có pump pending thì không tiêu thụ slot liveness.
- Khi kết thúc silence, cho phép tối đa một probe pending được phát; không phát dồn toàn bộ backlog.
- Mọi đường gọi trực tiếp transport (RF scan, RF diagnostic, AT/config command, telemetry) phải acquire cùng arbiter hoặc được đánh dấu maintenance mode loại trừ liveness/pump theo policy; không để bypass mutex.

## 4. PUMP_OFF Burst và Unicast Staggered Fan-out

### 4.1 Broadcast/group

- `setGroupPump(group_rf_id, false)` encode frame OFF đúng một lần rồi truyền cùng byte buffer 3 lần giống hệt nhau.
- Khoảng cách giữa thời điểm hoàn tất gửi frame N và bắt đầu frame N+1 là 30 ms (dung sai nghiệm thu 25–35 ms). Không encode lại giữa các burst để tránh khác CRC/frame.
- Broadcast không chờ ACK slave; trả kết quả `ACKED` chỉ sau khi cả 3 `send()` trả đủ byte. Nếu một send lỗi, ghi nhận `TX_ERROR` và báo số burst đã hoàn thành; không giả ACK.
- Không dùng `delay()` trong main loop cho khoảng cách burst. Chọn một trong hai triển khai, ghi rõ trong code:
  1. RF worker task/queue giữ mutex và `vTaskDelay(pdMS_TO_TICKS(30))`; hoặc
  2. state machine burst không blocking được service tick, nhưng mutex ownership phải được giữ/được arbiter đánh dấu độc quyền giữa các frame.
  Khuyến nghị worker task vì bảo đảm khoảng cách và không block scheduler.

### 4.2 Unicast

- Với `setPump(node, false)`, phát burst policy đã thống nhất cho OFF nhưng mỗi attempt phải chờ ACK `0x5A` trong timeout nghiêm ngặt; không phát đồng thời sang node khác.
- Nếu fan-out nhiều node (group được mở rộng sang unicast), tạo danh sách snapshot và gửi từng node cách nhau đúng 300 ms, theo thứ tự node tăng dần hoặc thứ tự hiện hành đã ghi trong plan implementation. Mỗi node:
  - acquire một transaction độc quyền;
  - gửi frame, chờ byte ACK `0x5A` đến timeout;
  - flush/drain RX trước attempt kế tiếp;
  - ghi `ACKED`, `TIMEOUT`, `UNEXPECTED_RESPONSE` hoặc `TX_ERROR`;
  - chỉ sau khi transaction kết thúc mới bắt đầu stagger 300 ms.
- Không dùng stagger để trì hoãn PUMP_OFF sống còn của một node đơn; delay chỉ áp dụng giữa các node fan-out. Khi node lỗi, tiếp tục node sau theo policy bounded và phát audit.
- PUMP_ON vẫn giữ ACK/retry semantics hiện có, nhưng phải đi qua arbiter và silence window; không áp dụng OFF burst nếu không cần.

## 5. Thay đổi theo file/hàm

### `agu_legacy_rf_host.h/.cpp`

1. Thêm traffic class, arbiter state, mutex/queue handle và các hằng policy; tách phần acquire/release khỏi phần encode/decode.
2. Bọc `transact()` và `readRamBurst()` bằng arbiter; ping/GET_STATE phải kiểm tra silence/pump-pending trước acquire.
3. Tách helper `sendFrameLocked`, `receiveAckLocked`, `sendPumpOffBurstLocked`; helper nhận frame đã encode và bảo đảm CRC/frame không đổi.
4. Trả result chi tiết: attempts, burst frames sent, RTT, và lý do bị silence/bus busy (có thể mở rộng enum nếu cần, giữ backward compatibility cho caller).
5. Dùng monotonic clock và sleep abstraction để native test kiểm tra gap 25–35 ms mà không phụ thuộc `Arduino.h`.
6. Xác minh tất cả frame vẫn được codec tạo theo `[Len][Opcode][Target_ID][CRC_Lo][CRC_Hi]` cho pump và giữ payload PING hiện tại.

### `main.cpp`

1. Khởi tạo arbiter cùng lifecycle của `g_agu_legacy_host`; không tạo mutex trong mỗi lời gọi.
2. `executeAguPump()` bỏ vòng chờ cờ busy; gọi API ưu tiên pump, đặt silence quanh phase, xử lý kết quả burst/ACK, và luôn release qua RAII/finally path kể cả lỗi/WDT.
3. `executeGroupPump()` không tự đặt khóa boolean để thay mutex; gọi group actuation API. Cập nhật FSM/timer chỉ sau khi admission thành công theo policy hiện tại, không thay đổi tính toán scheduler.
4. `executeAguPing()` và `serviceAguLivenessTick()` dùng API probe của arbiter; không update cursor/timestamp nếu probe bị defer vì silence/pump pending.
5. `servicePollTelemetry()` dùng cùng API low-priority/defer; coalesce một poll thay vì tích backlog.
6. Audit và chuyển `executeRfScan`, `runRfUartDiagnostic`, `executeAguGetId`, AT commands và các chỗ `g_rf_transport->send/receive/flush` còn lại sang guarded path hoặc maintenance transaction rõ ràng.
7. Giữ nguyên reconciliation ghost-running, lifecycle event và desired-state semantics; chỉ thay transport admission/transmission.

## 6. Mocking và Unit Test native

Bổ sung test group `test_rf_arbiter` và mock `IRfTransport` có:

- log timestamp, frame bytes, send count, receive script, flush count;
- inject rớt frame thứ 1/2, ACK trễ, ACK sai byte, TX short-write, RX noise;
- fake clock/sleep để test deterministic burst gap và silence deadline;
- hai worker/thread hoặc scheduler giả lập pump và ping cạnh tranh mutex.

Các ca bắt buộc:

1. PUMP_OFF broadcast phát đúng 3 frame byte-for-byte giống nhau, opcode `0x07`, đúng target/group, CRC16-Modbus hợp lệ; gap mỗi cặp 25–35 ms.
2. Khi OFF pending/đang burst, PING và GET_STATE không có `send()` nào xen giữa; sau silence chỉ một probe được chạy.
3. PING đang chờ ACK không làm mất pump: pump được ưu tiên ngay sau transaction atomic, không queue delay vượt timeout policy.
4. Hai pump concurrent không interleave byte/frame; mutex release xảy ra trên success, timeout và TX error.
5. Silence trước/sau phase ±1000 ms chặn đúng boundary, xử lý wrap-around monotonic millis và không chặn PUMP_ON/OFF.
6. Unicast ACK `0x5A` thành công; ACK sai/timeout bị phân loại đúng; fan-out có khoảng cách 300 ms giữa node và timeout bounded.
7. Burst khi send #2 hoặc #3 lỗi trả TX_ERROR/partial count, không báo thành công giả.
8. Codec regression: toàn bộ frame PUMP_ON/OFF/GET_STATE/PING hiện tại giữ nguyên bytes và CRC; không chấp nhận frame sai CRC.
9. Probe bị defer không làm tăng `g_last_agu_ping_ms`/cursor và không tạo backlog telemetry.
10. Native test mô phỏng `executeAguPump`/`executeAguPing` concurrency, xác minh không còn race dựa trên boolean busy.

## 7. Verification Plan

1. Chạy unit/regression native:
   ```bash
   cd aeroponics-firmware
   pio test -e native
   ```
   Kết quả phải bao gồm test arbiter mới, codec, FSM và RTC hiện có; không được sửa test để bỏ suite cũ.
2. Build firmware ESP32:
   ```bash
   pio run -e esp32-s3-devkitc-1
   ```
   Kiểm tra không warning nghiêm trọng về FreeRTOS handle, ABI hoặc `UNIT_TEST_HOST` conditional.
3. Nếu có hardware-in-loop ở bước triển khai: dùng logic analyzer/UART tap đo ba frame OFF, khoảng 25–35 ms, không có opcode 0x05/0x08 trong silence, và ACK 0x5A unicast.
4. Stress test tối thiểu gồm liveness tick 1.5 s, telemetry poll, group ON/OFF liên tiếp, manual command và forced safe-OFF; lưu log transaction ID, class, silence interval, queue/admission delay, burst count.
5. Kiểm tra watchdog: burst/stagger không làm watchdog reset và main/scheduler vẫn chạy; worker queue bounded không tăng unbounded.

## 8. Acceptance criteria định lượng

- 100% test burst OFF: đúng 3 frame giống hệt nhau; mỗi gap 25–35 ms; không có frame ping/telemetry xen giữa.
- Trong 1,000 lần mô phỏng cạnh tranh pump-vs-ping, 0 lần frame bị interleave, 0 lần PING/GET_STATE phát trong cửa sổ silence ±1,000 ms.
- PUMP actuation có quyền admission trước traffic low-priority; queue delay của pump do ping/telemetry bằng 0 sau khi transaction hiện tại kết thúc và không vượt timeout transaction đã cấu hình.
- Unicast fan-out: gap giữa node liên tiếp 300 ms ± một tick scheduling đã ghi nhận; ACK `0x5A` được nhận hoặc timeout nghiêm ngặt, không chờ vô hạn.
- `pio test -e native` pass 100%; `pio run -e esp32-s3-devkitc-1` pass.
- Không thay đổi byte-level frame/CRC regression; mọi PUMP frame vẫn đúng `[Len][Opcode][Target_ID][CRC_Lo][CRC_Hi]`, PING giữ payload `0xA5` hiện hành.
- Không có đường gọi AGU UART trực tiếp ngoài arbiter/maintenance guard trong audit mã nguồn.
- Chu kỳ scheduler/FSM Phase 1 không bị sửa; các sai lệch thời lượng còn lại sau khi RF acceptance đạt sẽ được chuyển sang Phase 3.

## 9. Rủi ro và quyết định cần giữ trong review

- Mutex priority inheritance chỉ hoạt động khi caller thực sự là FreeRTOS task; vì vậy plan yêu cầu priority admission hoặc RF worker queue, không coi boolean busy là đủ.
- Giữ ownership mutex trong toàn bộ OFF burst làm tăng thời gian chiếm bus khoảng 60 ms; đây là chủ ý để cấm xen frame và nhỏ hơn cửa sổ silence 2 s.
- Các đường chẩn đoán/AT hiện bypass host là rủi ro lớn nhất; triển khai phải audit và route chúng trước khi tuyên bố bus đã được bảo vệ.
- Không dùng `delay()` trong loop chính và không thay đổi thuật toán `GroupScheduler`; chỉ thay lớp dispatch/truyền RF.
