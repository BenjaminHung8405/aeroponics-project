### [2026-09-26 08:41:18 UTC] Track E — Task E2 (Build & smoke test: gateway + node + native CRC, size gate, fail-closed, wire layout), chờ QA Review

- **Thời gian thực hiện:** 2026-09-26 08:27:36Z → 08:41:18Z (Asia/Ho_Chi_Minh 15:27:36 → 15:41:18)
- **Task ID:** **E2** (Track E — Tầng Build (Node + Filter), Sprint 2 — *track đầu tiên còn Task `[ ] Pending`*; E1 đã ở `QA Review` từ lượt trước nên không được chọn lại)
- **Trạng thái hiện tại:** **Đang chờ QA Review** (`[ ] QA Review`) — chưa đánh dấu `[x] Done`.
- **Danh sách file đã tạo mới / sửa đổi:**
  - `[MODIFIED]` `aeroponics-firmware/src/uart_rf_transport.cpp` — sửa lỗi compile pre-existing chặn build gateway: thay hàm ma `uart_read_byte_from_fifo` (không tồn tại trong ESP-IDF) bằng cặp API low-level ISR-safe `uart_ll_get_rxfifo_len()` + `uart_ll_read_rxfifo()` qua `UART_LL_GET_HW`; thêm include `hal/uart_ll.h` (chỉ khi `ESP_PLATFORM`). Giữ nguyên semantics drop-byte/overflow.
  - `[MODIFIED]` `.ai/planning/esp32-rf-crc16/PROGRESS.md` — E2 `Pending` → `In Progress` → `QA Review`; cập nhật `Last Updated` + `Current Phase`.
  - `[MODIFIED]` `.ai/planning/esp32-rf-crc16/WALKTHROUGH_LOG.md` — bản ghi này, chèn đầu file theo thứ tự thời gian đảo ngược.
  - `[NOT MODIFIED]` `platformio.ini`, `rf_frame_codec.cpp/.h`, `pump_node_controller.cpp/.h`, `core/Crc16Modbus.*`, `test/*` — E2 là task build/smoke, không sinh thay đổi logic CRC/version/storage. `test_filter` giữ nguyên `test_production, test_fsm, test_crc16` theo yêu cầu E1.
- **Giải trình ngắn gọn về giải pháp logic đã viết và kết quả tự kiểm tra mã nguồn:**
  1. **Vì sao phải sửa `uart_rf_transport.cpp` (ngoài phạm vi CRC nhưng bắt buộc cho E2):** E2 yêu cầu cả 3 lệnh `exit code = 0`, nhưng `pio run -e esp32-s3-devkitc-1` **FAIL** vì `src/uart_rf_transport.cpp:359` gọi hàm ma `uart_read_byte_from_fifo` — hàm này **không tồn tại** trong ESP-IDF (`driver/uart.h` chỉ có `uart_read_bytes` / `uart_get_buffered_data_len`), và cũng không được định nghĩa ở bất kỳ đâu trong repo (chỉ đúng 1 reference, chính dòng đó). Đây là lỗi compile **pre-existing**, do commit `71c0776 feat(uart): isolate RF RX on Core 1` mang vào (commit nằm ngoài chuỗi CRC; lần sửa cuối của file là `71c0776`, không liên quan migration). Vì E2 là task *build & smoke*, không build được thì task vô nghĩa — nên đã gỡ blocker tối thiểu.
     > **Lưu ý phạm vi cho Review Agent:** đây là fix lỗi build **không liên quan CRC**, thực hiện vì E2 không thể đạt exit-0 nếu không sửa. Diff giới hạn trong 1 hàm ISR + 1 include; **không** chạm CRC/version/address/storage.
  2. **Vì sao chọn `uart_ll_*` chứ không dùng `uart_read_bytes`:** ISR này được đăng ký bằng `uart_isr_register`, **thay thế** ISR mặc định của ESP-IDF driver. Vì vậy không có ring buffer nào của driver để đọc — byte nằm trực tiếp trong **hardware RX FIFO**. `uart_read_bytes` đọc từ ring buffer của driver (và có thể block với tick chờ), nên **sai kiến trúc** ở đây. Đúng API cho custom ISR là tầng HAL: `uart_ll_get_rxfifo_len(hw)` (đếm byte trong FIFO) + `uart_ll_read_rxfifo(hw, &byte, 1)` (đọc 1 byte), với `hw = UART_LL_GET_HW(_uart_num)`. Cả hai là thao tác thanh ghi, **không block, không malloc, không printf** — đúng ràng buộc "ISR không được block" của file. Include `hal/uart_ll.h` được bọc trong `#if defined(ESP_PLATFORM)` để **không ảnh hưởng** build native/ATmega8.
  3. **Bảo toàn semantics gốc:** vòng lặp mới vẫn "drain hết byte đang có trong FIFO", vẫn check ring buffer đầy trước khi ghi (drop byte + tăng `_dropped_bytes`/`_rx_overflows` + `return` sớm), vẫn signal consumer qua `xQueueSendFromISR` + `portYIELD_FROM_ISR`. Không thay đổi bất kỳ hành vi quan sát được nào ngoài việc *đọc đúng byte từ FIFO thay vì gọi hàm không tồn tại*.
  4. **Kết quả tự kiểm tra (3 lệnh bắt buộc — tất cả `exit code = 0`):**
      - `pio test -e native -f test_crc16` → **9/9 PASS** (exit 0). 9 test: standard vector `0x4B37`, bigplan pump on/off, append layout, null & zero, append capacity, verify short frame, tamper, max length 255.
      - `pio run -e esp32-s3-devkitc-1` → **SUCCESS** (exit 0, trước đó FAIL). RAM `121244/327680` (37.0%), Flash `918529/1966080` (46.7%); `firmware.bin` tạo thành công. Biên dịch sạch toàn bộ TU tiêu thụ CRC (`rf_frame_codec.cpp`, `pump_node_controller.cpp`, `main.cpp`, …) với `RF_PROTOCOL_VERSION = 0x02` + CRC16-Modbus. `uart_rf_transport.cpp` compile **không warning/không error**.
      - `pio run -e atmega8-node-4` → **SUCCESS** (exit 0). `check_atmega8_size.py` (post-script) chạy tự động: **`ATmega8 resource gate: flash=6436/7000 bytes, RAM=301/900 bytes`** — trong ngưỡng. Giống hệt số đo lượt A1/B2/C1, xác nhận việc thêm utility CRC vào node build không phình code.
  5. **S2-HARD-02 (fail-closed) — xác nhận bằng review + test:**
      - `RfFrameCodec::decodeFrameDetailed` (`rf_frame_codec.cpp:210`) so `calculateCrc16(frame, signed_len + HMAC_TAG_SIZE)` với `readU16Le(frame + signed_len + HMAC_TAG_SIZE)`; lệch → `return ParseError::CRC_MISMATCH` **trước** HMAC (fail-fast, đúng thứ tự CRC-trước-HMAC bắt buộc ở B2).
      - Đường nhận thật: `handleIncomingFrame` → `parseFrame` → `verifyCrcAndMac` (`pump_node_controller.cpp:245-247`); CRC sai → `return false` → `parseFrame` false → `handleIncomingFrame` **return false ngay dòng 894-895, trước mọi dispatch** (`handleAckFrame`/`handleTelemetryFrame`/…). Frame CRC sai **không bao giờ** vào handler ⇒ fail-closed đúng spec.
      - Test D2 `test_rf_crc16_payload_bit_flip_returns_crc_mismatch` xác nhận flip 1 bit payload → **đúng** `CRC_MISMATCH` (không phải `HMAC_AUTH_FAIL`, không crash, không `OK`).
  6. **S2-HARD-05 (wire layout `[crc_lo][crc_hi]`) — xác nhận:**
      - `verifyCrcAndMac` đọc `readU16Le(frame_data + crc_check_len)` với `crc_check_len = header_len + payload_len + HMAC_TAG_SIZE = frame_len - 2`; `readU16Le` là little-endian ⇒ byte wire là `[crc_lo][crc_hi]`. Bound-check `frame_len < 2 → false` (dòng 242) và invariant `crc_check_len + 2 != frame_len → false` (dòng 244) chặn đọc 2 byte cuối khi buffer ngắn.
      - Test D2 `test_rf_crc16_frame_wire_value_matches_calculated_value` xác nhận `calculateCrc16(frame, frame_len - 2) == readU16Le(frame + frame_len - 2)`.
  7. **Regression watch (không được xấu đi):**
      - `pio test -e native -f test_fsm` → **21/21 PASS** (bất kỳ fail nào ở đây = regression; **không có**).
      - `pio test -e native -f test_production` → kết thúc ở **SIGSEGV pre-existing** (khu vực test C4) như baseline; **không phát sinh failure mới** từ thay đổi E2. Sửa đổi của E2 nằm hoàn toàn trong `#if defined(ESP_PLATFORM)` và file này **không** nằm trong `build_src_filter` của `atmega8-node-*`, nên **native/ATmega8 byte-for-byte không đổi** so với lượt D1/D2.
      - `git diff --check` → sạch (không whitespace lỗi).
  8. **Ghi chú cho Review Agent:** (a) diff duy nhất của E2 là fix UART ISR ở `uart_rf_transport.cpp` — hãy audit kỹ tính ISR-safe của `uart_ll_read_rxfifo`/`uart_ll_get_rxfifo_len` (đều là đọc thanh ghi, không block) và việc giữ nguyên drop/overflow semantics; (b) fix này **không** thuộc migration CRC, nên nên tách commit riêng nếu team muốn giữ lịch sử sạch; (c) E2 **chưa** đánh dấu `[x] Done` — chờ kiểm toán độc lập; (d) các quality gate S2-HARD-01..06 giờ đã **có bằng chứng build thực tế** cho lần đầu tiên trong Sprint 2 (trước đó ESP32 không build được nên gate S2-HARD-04/05 chỉ mới có bằng chứng một phía).

### [2026-09-26 08:24:15 UTC] Track D — Tasks D1 & D2 (Modbus vectors, dynamic fixtures, CRC fail-closed tests), chờ QA Review

- **Thời gian thực hiện:** 2026-09-26 08:11:17Z → 08:24:15Z (Asia/Ho_Chi_Minh 15:11:17 → 15:24:15)
- **Task ID:** **D1, D2** (Track D — Tầng Test / QA, Sprint 2; toàn bộ Pending đầu tiên theo thứ tự track)
- **Trạng thái hiện tại:** **Đang chờ QA Review** (`[ ] QA Review`) — chưa đánh dấu `[x] Done`.
- **Danh sách file đã tạo mới / sửa đổi:**
  - `[MODIFIED]` `aeroponics-firmware/test/test_production/test_production.cpp` — đổi golden vector và tên test sang CRC-16/MODBUS (`0x4B37`), giữ null contract, thêm payload one-bit-flip assertion `ParseError::CRC_MISMATCH`, thêm wire CRC invariant, include `core/Crc16Modbus.h`, và đổi fixture `hmac_corrupt` sang `appendCrc16Modbus`.
  - `[MODIFIED]` `.ai/planning/esp32-rf-crc16/PROGRESS.md` — D1/D2 chuyển `Pending` → `In Progress` → `QA Review`; cập nhật `Last Updated` và `Current Phase`.
  - `[MODIFIED]` `.ai/planning/esp32-rf-crc16/WALKTHROUGH_LOG.md` — bản ghi này, chèn đầu file theo thứ tự thời gian đảo ngược.
  - `[NOT CREATED]` Không tạo file mã nguồn mới; mọi fixture CRC tiếp tục dùng utility Modbus hiện hữu, không hardcode CRC.
- **Giải trình ngắn gọn về giải pháp logic đã viết và kết quả tự kiểm tra mã nguồn:**
  1. **D1 — chuẩn hóa golden vector:** đổi `test_rf_crc16_ccitt_false_standard_test_vector` thành `test_rf_crc16_modbus_standard_vector`, cập nhật `RUN_TEST`, đổi ASCII `123456789` thành kỳ vọng độc lập `0x4B37`, và cập nhật bản sao trong ma trận D1. Hai edge case bắt buộc vẫn giữ nguyên: `(nullptr, 0) == 0xFFFF` và `(nullptr, 10) == 0`.
  2. **D2 — fixture và fail-closed coverage:** fixture HMAC bị làm hỏng được tái tạo CRC bằng `appendCrc16Modbus` (little-endian, không viết tay byte CRC). Test mới xác nhận frame hợp lệ trả `OK`, cùng frame sau khi flip một bit payload trả chính xác `CRC_MISMATCH`; test wire kiểm tra `calculateCrc16(frame, len - 2) == readU16Le(frame + len - 2)`.
  3. **Rà soát nợ kỹ thuật:** quét `test_production.cpp` không còn `0x29B1`, `0x1021`, `ccitt` hoặc `CCITT`; không còn phép ghi CRC thủ công trong fixture. Tên test cũ không còn trong code/call-site.
  4. **Kết quả tự kiểm tra:**
     - `pio test -e native -f test_crc16` → **9/9 PASS**.
     - `pio test -e native -f test_fsm` → **21/21 PASS**.
     - `pio test -e native -f test_production` → các test D1/D2 mới chạy được đều PASS; so với baseline sạch trước thay đổi, failure giảm từ **98** xuống **97** (failure CCITT cũ được loại bỏ), thêm 2 test mới đều PASS. Suite vẫn kết thúc tại **SIGSEGV pre-existing** ở khu vực test C4, không phát sinh failure mới.
     - `pio run -e esp32-s3-devkitc-1` → **FAIL pre-existing** tại `src/uart_rf_transport.cpp:359` (`uart_read_byte_from_fifo` chưa khai báo); đã kiểm tra lại khi tạm tháo thay đổi test, lỗi vẫn giữ nguyên.
     - `git diff --check` → **PASS**.
- **Lưu ý cho Review Agent:** D1/D2 chỉ sửa test và fixture theo phạm vi Track D. Các failure/crash production suite và lỗi compile ESP32 nêu trên tồn tại trước lượt này; không tự ý sửa vì nằm ngoài scope.

### [2026-09-26 08:09:10 UTC] Track C — Task C1 (RF_PROTOCOL_VERSION bump 0x01 → 0x02), chờ QA Review

- **Thời gian thực hiện:** 2026-09-26 08:05:50Z → 08:09:10Z (Asia/Ho_Chi_Minh 15:05:50 → 15:09:10)
- **Task ID:** **C1** (Track C — Version / Persistence Boundary, Sprint 2 — *track đầu tiên còn Task `[ ] Pending`*; C2 trong cùng track đã ở `QA Review` từ lượt trước nên không được chọn lại)
- **Trạng thái hiện tại:** **Đang chờ QA Review** (`[ ] QA Review`) — chưa đánh dấu `[x] Done`
- **Danh sách file đã tạo mới / sửa đổi:**
  - `[MODIFIED]` `aeroponics-firmware/include/config.h` — dòng 150: `constexpr uint8_t RF_PROTOCOL_VERSION = 0x01;` → `0x02`. **Đây là thay đổi production duy nhất của task C1.**
  - `[MODIFIED]` `.ai/planning/esp32-rf-crc16/PROGRESS.md` — C1 `Pending` → `In Progress` → `QA Review`; cập nhật `Last Updated` + `Current Phase`.
  - `[MODIFIED]` `.ai/planning/esp32-rf-crc16/WALKTHROUGH_LOG.md` — bản ghi này, chèn đầu file theo thứ tự thời gian đảo ngược.
  - `[NOT MODIFIED]` `src/rf_frame_codec.cpp`, `src/pump_node_controller.cpp`, `include/rf_frame_codec.h`, `platformio.ini` — không cần sửa: cả hai call-site check version (`rf_frame_codec.cpp:186`, `pump_node_controller.cpp:215`) vốn đã đọc từ hằng `RF_PROTOCOL_VERSION`, không hardcode literal.
- **Giải trình ngắn gọn về giải pháp logic đã viết và kết quả tự kiểm tra mã nguồn:**
  1. **Vì sao bump version là bắt buộc (S2-HARD-01):** Sau A1, CRC trên wire đã là Modbus nhưng header vẫn ghi version `0x01` — đúng trạng thái nguy hiểm mà plan cảnh báo: **hai đầu đều decode được header nhưng CRC luôn mismatch**, lệnh chết âm thầm, không log, không cảnh báo. Bump `0x01 → 0x02` đóng lại đúng lỗ hổng đó: header của node cũ (CRC CCITT) bị `decodeFrameDetailed` chặn ở `UNSUPPORTED_VERSION` **trước cả khi** tới bước kiểm CRC, và ngược lại gateway mới cũng bị node cũ chặn. Đây là hàng rào fail-closed, không phải tối ưu hoá.
  2. **Một nguồn sự thật duy nhất (anti-technical-debt):** đã quét toàn bộ firmware — mọi logic check version đều đọc hằng, không có literal: `rf_frame_codec.cpp:186` (`out_header.version != RF_PROTOCOL_VERSION` → `ParseError::UNSUPPORTED_VERSION`), `pump_node_controller.cpp:215` (`parseFrame` reject frame lệch version), `rf_frame_codec.h:39` (default initializer của `RfHeader::version` nên mọi frame encode mới tự mang `0x02`). **Không** tạo hằng thứ hai, **không** viết `static_assert` trùng lặp giá trị — giá trị nằm đúng một chỗ trong `config.h:150`, node build và gateway build cùng include một file đó.
  3. **Phạm vi giữ nguyên (S2-ADDR-00):** diff chỉ chạm đúng một dòng hằng số. `RF_MAX_NODE_ID`, `RF_PRODUCTION_MIN/MAX_NODE_ID`, `isProductionNodeId`, `isValidAddress` **không** bị đụng tới; `agu_legacy_codec` (SCI legacy, version `0x01` riêng) **không** bị đụng tới — legacy path không đi qua `RF_PROTOCOL_VERSION` nên giữ nguyên hành vi.
  4. **Kết quả tự kiểm tra:**
      - `pio test -e native -f test_crc16` → **9/9 PASS** (không hồi quy so với Sprint 1/A1).
      - `pio test -e native -f test_fsm` → **21/21 PASS** (bất kỳ fail nào ở đây = regression; không có).
      - `pio run -e atmega8-node-4` → **SUCCESS**; `check_atmega8_size.py` chạy tự động: `flash=6436/7000 bytes, RAM=301/900 bytes` — **y hệt số đo của lượt A1/B2**, xác nhận bump version chỉ đổi 1 byte hằng, không phình code trên node (`ATMEGA8_NODE_BUILD` dùng chung `config.h` với gateway).
      - `pio run -e esp32-s3-devkitc-1` → **FAIL**, nhưng tại `src/uart_rf_transport.cpp:359` `'uart_read_byte_from_fifo' was not declared in this scope` — **lỗi pre-existing đã được chứng minh ở lượt A1 trên worktree sạch `HEAD 80399b6`**, nằm ngoài phạm vi Sprint 2 CRC. Quan trọng: build đã **biên dịch thành công** `rf_frame_codec.cpp`, `pump_node_controller.cpp`, `node_command_processor.cpp`, `treatment_manager.cpp`, `main.cpp` — tức toàn bộ translation unit tiêu thụ `RF_PROTOCOL_VERSION` ở phía gateway đều compile với hằng `0x02`. Không sửa âm thầm lỗi ngoài scope; cần task riêng.
      - `pio test -e native -f test_production` → **98 failed / 103 succeeded**, kết thúc `SIGSEGV` — **đúng bằng** kết quả chuyển tiếp sau A1/B2 (98F/103P), không tăng thêm failure nào. Failure CCITT `test_rf_crc16_ccitt_false_standard_test_vector: Expected 0x29B1 Was 0x4B37` vẫn là nợ mở của **D1**. `git diff --check` sạch.
  5. **Phát hiện cần Track D xử lý (chưa sửa ở lượt này — nằm ngoài phạm vi C1):** bump version làm lộ 3 fixture cứng byte version trong `test_production.cpp` mà **hiện chưa quan sát được** vì suite SIGSEGV trước khi tới đó:
      - `test_production.cpp:405,407` — `RfHeader header{{0xAA,0x55}, 0x01, ...}` và `expected_header[]` gắn cứng `0x01`; nên dùng `RF_PROTOCOL_VERSION` để không tá lỗi mỗi lần bump.
      - `test_production.cpp:8073` — vector "Unsupported wire protocol version" dùng `{0xAA,0x55,0x02}`; sau bump, `0x02` **trở thành version hợp lệ** nên vector này sẽ trả sai `ParseError` và fail. Cần đổi sang giá trị khác `RF_PROTOCOL_VERSION`.
      - `test_production.cpp:2476` — `corrupted[2] = 0x02` (nhãn "Unsupported version"); hiện chỉ assert `FALSE` nên vẫn xanh (fail do CRC), nhưng **mất ý nghĩa kiểm thử** — nên dùng version cố ý sai.
      > Đây là lý do D1/D2 còn `Pending` là đúng thứ tự; Review Agent nên xác nhận các mục này được đóng trong Track D chứ không gộp vào C1.
  6. **Rủi ro vận hành còn lại:** từ giờ wire là `version 0x02 + CRC Modbus`. **Không flash deploy** gateway/node lệch phiên bản cho tới khi D1/D2 và E2 xong; node cũ đang chạy `0x01` sẽ bị từ chối ở tầng version — đây là hành vi **đúng theo thiết kế**, chỉ là cần ghi vào release window.
- **Lưu ý lệch phạm vi (Review Agent cần audit):** C1 chỉ gồm thay đổi 1 dòng hằng số; **không** sửa fixture test nào (mặc dù đã phát hiện 3 fixture cứng version byte) vì D1/D2 là track được giao riêng cho việc rà fixture. E2 vẫn `Pending` vì build ESP32 còn bị chặn bởi lỗi pre-existing `uart_read_byte_from_fifo`.

### [2026-09-26 08:03:17 UTC] Track B — Task B1 & B2 (PumpNodeController CRC Delegation and Frame-Length Guard), chờ QA Review

- **Thời gian thực hiện:** 2026-09-26 07:56:15Z → 08:03:17Z (Asia/Ho_Chi_Minh 14:56:15 → 15:03:17)
- **Task ID:** **B1, B2** (Track B — Tầng Nghiệp vụ (Delegates), Sprint 2)
- **Trạng thái hiện tại:** **Đang chờ QA Review** (`[ ] QA Review`) — chưa đánh dấu `[x] Done`
- **Danh sách file đã tạo mới / sửa đổi:**
  - `[MODIFIED]` `aeroponics-firmware/include/pump_node_controller.h` — truyền `frame_len` vào private `verifyCrcAndMac` để kiểm tra bounds tại đúng lớp đọc CRC.
  - `[MODIFIED]` `aeroponics-firmware/src/pump_node_controller.cpp` — thêm null/short guard và invariant fail-closed `crc_check_len + 2 == frame_len` trước `readU16Le`; cập nhật call-site trong `parseFrame`.
  - `[MODIFIED]` `.ai/planning/esp32-rf-crc16/PROGRESS.md` — B1/B2 chuyển `Pending` → `In Progress` → `QA Review`.
  - `[MODIFIED]` `.ai/planning/esp32-rf-crc16/WALKTHROUGH_LOG.md` — chèn bản ghi này ở đầu file theo thứ tự thời gian đảo ngược.
- **Giải trình ngắn gọn về giải pháp logic đã viết và kết quả tự kiểm tra mã nguồn:**
  1. **B1 — verification-only:** giữ nguyên delegate `PumpNodeController::calculateCrc16(data, len)` → `RfFrameCodec::calculateCrc16(data, len)`; sau A1, codec facade tự động dùng CRC16-Modbus. Kiểm tra `grep -n "0x1021" src/pump_node_controller.cpp include/pump_node_controller.h` trả về rỗng; không tạo wrapper/delegate thừa.
  2. **B2 — invariant và fail-fast:** `verifyCrcAndMac` nhận `frame_len`, từ chối buffer null/ngắn hơn 2 byte, rồi yêu cầu chính xác `crc_check_len + 2 == frame_len` trước khi đọc `[crc_lo][crc_hi]`. CRC vẫn được xác minh trước HMAC, giữ nguyên thứ tự fail-fast; CRC sai return `false` trước khi chạy HMAC.
  3. **Kết quả tự kiểm tra:** `pio test -e native -f test_crc16` → **9/9 PASS**; `pio test -e native -f test_fsm` → **21/21 PASS**; `pio run -e atmega8-node-4` → **SUCCESS**, Flash `6436/7808` bytes, RAM `301/1024` bytes; `test_production` → **98 failed / 103 succeeded**, đúng baseline chuyển tiếp sau A1 (không phát sinh failure mới; failure CRC CCITT cũ thuộc D1). `git diff --check` sạch.

### [2026-09-26 07:46:28 UTC] Track A — Task A1 & A2 (RfFrameCodec CRC16-Modbus Delegation + Doc), kèm C2 bắt buộc và build-filter fix, chờ QA Review

- **Thời gian thực hiện:** 2026-09-26 07:34:10Z → 07:46:28Z (Asia/Ho_Chi_Minh 14:34:10 → 14:46:28)
- **Task ID:** **A1, A2** (Track A — Tầng Data (Codec), Sprint 2) — *kéo theo* **C2** (dependency bắt buộc) và phần `build_src_filter` của **E1** (do chính note A1 bắt buộc)
- **Trạng thái hiện tại:** **Đang chờ QA Review** (`[ ] QA Review`) — chưa đánh dấu `[x] Done`
- **Danh sách file đã tạo mới / sửa đổi:**
  - `[MODIFIED]` `aeroponics-firmware/src/rf_frame_codec.cpp` — thân `calculateCrc16` xoá sạch vòng lặp CCITT `0x1021`, delegate sang `calculateCrc16Modbus`; thêm `#include "core/Crc16Modbus.h"`.
  - `[MODIFIED]` `aeroponics-firmware/include/rf_frame_codec.h` — thêm Doxygen `@brief` + 3 contract line cho `calculateCrc16`; **không** xoá/đổi tên declaration.
  - `[MODIFIED]` `aeroponics-firmware/include/treatment_manager.h` — **C2**: *Extract Function* tách `static uint16_t calculateStorageCrc16(...)` (giữ CCITT-FALSE `0x1021`); `computeChecksum()` gọi helper này; gỡ `#include "rf_frame_codec.h"`.
  - `[MODIFIED]` `aeroponics-firmware/platformio.ini` — thêm `+<core/Crc16Modbus.cpp>` vào `build_src_filter` của `env:atmega8-node`, `env:native-integration`, `env:native-prototype-integration`; `test_filter` giữ nguyên.
  - `[MODIFIED]` `.ai/planning/esp32-rf-crc16/PROGRESS.md` — A1, A2: `Pending` → `In Progress` → `QA Review`; C2 và phần filter của E1 cũng chuyển `QA Review` kèm ghi chú lệch phạm vi.
  - `[MODIFIED]` `.ai/planning/esp32-rf-crc16/WALKTHROUGH_LOG.md` — bản ghi này, chèn đầu file theo thứ tự thời gian đảo ngược.
- **Giải trình ngắn gọn về giải pháp logic đã viết và kết quả tự kiểm tra mã nguồn:**
  1. **A1 (Adapter/Delegation — một nguồn sự thật duy nhất):** giữ nguyên signature `RfFrameCodec::calculateCrc16`; thân hàm chỉ còn null-guard + `return calculateCrc16Modbus(data, len);`. Không chép lại thuật toán Modbus, không giữ dead code CCITT `0x1021`. Null-guard `if (data == nullptr && len != 0) return 0;` giữ **nguyên vẹn** — khớp chính xác `(nullptr,0) → 0xFFFF`, `(nullptr,10) → 0`, nên delegate là **behavior-preserving** về guard; chỉ mảng CRC thu được thay đổi. Wire layout `[crc_lo][crc_hi]` và phạm vi tính `[header + payload + hmac_tag]` **không đổi** (S2-HARD-05). Hai call-site `encodeFrame` và `decodeFrameDetailed` không chỉnh sửa.
  2. **A2 (Open/Closed — doc-only):** thêm `@brief CRC-16/MODBUS (init 0xFFFF, poly 0xA001, reflected LSB-first) — since RF_PROTOCOL_VERSION 0x02` cùng 3 contract line: (1) CRC không phải authentication, HMAC mới là; (2) CRC phủ `[header + payload + hmac_tag]`, không gồm 2 byte CRC đuôi; (3) wire order `[crc_lo][crc_hi]` little-endian. Không xoá/đổi tên declaration. **Không** thêm alias `calculateCrc16Modbus` vào header (alias là tuỳ chọn; thêm sẽ tạo tầng delegate thứ hai, vi phạm anti-technical-debt "1 tầng delegate duy nhất"). Magic `0xA001` chỉ xuất hiện trong doc; hằng thật là `kCrc16ModbusPolynomial` trong `core/Crc16Modbus.h`.
  3. **C2 — bắt buộc kéo lên làm prerequisite (S2-HARD-06):** `PROGRESS.md` đặt `C2 ──► A1` và cảnh báo "giao commit giữa chừng = snapshot NVS hỏng giữa phiên". Vì `TreatmentSnapshot::computeChecksum()` gọi `RfFrameCodec::calculateCrc16`, chỉ làm A1 sẽ khiến **toàn bộ snapshot treatment trong NVS fail verify**. Đã tách `static uint16_t calculateStorageCrc16(const uint8_t*, size_t)` ngay trong `treatment_manager.h`, giữ thuật toán CCITT-FALSE (`0x1021`) để dữ liệu cũ verify được, kèm comment `@brief Internal NVS integrity checksum — deliberately NOT the RF wire CRC (deliberately frozen to CCITT-FALSE to preserve stored snapshots)`. Không đổi cấu trúc NVS, không viết migration script.
  4. **Build filter (thuộc phạm vi E1, do note A1 bắt buộc):** lần build đầu `pio run -e atmega8-node-4` **FAIL** với `undefined reference to calculateCrc16Modbus(...)` — đúng cảnh báo của A1/E1 vì `env:atmega8-node` dùng allow-list nên `core/Crc16Modbus.cpp` không được biên dịch. Đã thêm `+<core/Crc16Modbus.cpp>` vào 3 env allow-list; `env:native` và `env:esp32-s3-devkitc-1` dùng `+<*>` nên đã có sẵn. `test_filter` giữ nguyên `test_production, test_fsm, test_crc16` (đúng cấm của E1).
  5. **Kết quả tự kiểm tra:**
     - `pio test -e native -f test_crc16` → **9/9 PASS**.
     - `pio test -e native -f test_fsm` → **21/21 PASS**. (Lần chạy song song đầu tiên báo `ar: unity.o: No such file or directory` do 3 tiến trình `pio test` tranh nhau thư mục build `native` chung — không phải lỗi mã nguồn; chạy lại tuần tự → PASS.)
     - `pio run -e atmega8-node-4` → **SUCCESS**, `check_atmega8_size.py` chạy tự động: `flash=6436/7000 bytes, RAM=301/900 bytes` (trong ngưỡng — S2-HARD-04 OK), không phát sinh heap allocation trong đường CRC.
     - `pio run -e esp32-s3-devkitc-1` → **FAIL** tại `src/uart_rf_transport.cpp:359: 'uart_read_byte_from_fifo' was not declared in this scope`. **Đã chứng minh là lỗi pre-existing:** build cùng lệnh trên worktree sạch tại `HEAD (80399b6)` cho **cùng lỗi, cùng dòng**. Lỗi nằm ngoài phạm vi Track A → không sửa âm thầm, cần task riêng (không gộp vào Sprint 2 CRC).
     - `test_production`: **97F/104P** (baseline `HEAD 80399b6`) → **98F/103P**. Đã diff từng dòng hai danh sách `[FAILED]`: chênh lệch duy nhất là `test_rf_crc16_ccitt_false_standard_test_vector: Expected 0x29B1 Was 0x4B37` — chính là test vector CCITT cũ mà **D1 (Track D)** được giao sửa thành `0x4B37`. Đây là trạng thái chuyển tiếp của thứ tự `A1 → … → D1`, không phải hồi quy chức năng. Cả hai lần chạy đều kết thúc `SIGSEGV` **giống hệt baseline** → pre-existing.
  6. **Rà soát nợ kỹ thuật / ràng buộc:** grep toàn repo `calculateCrc16` → mọi call-site còn lại đều là **RF wire path** (`rf_frame_codec.cpp:66,149,210`, `pump_node_controller.cpp:242`, `pump_node_controller.h:170`, tests); **không còn call-site storage path** nào dùng chung codec (S2-HARD-06 thỏa). `grep 0x1021` trong `src/pump_node_controller.cpp` + `include/pump_node_controller.h` → **rỗng** (điều kiện B1 thỏa sẵn). `git diff --check` sạch. Không đụng `isValidAddress`/node range/`RF_PROTOCOL_VERSION` (S2-ADDR-00), không đụng `agu_legacy_codec`.
- **Lưu ý lệch phạm vi (Review Agent cần audit):**
  1. **C2 được kéo lên trước A1** dù thuộc Track C — bắt buộc theo `Mandatory Execution Order`; nếu tách commit thì **C2 + A1 phải nằm cùng một commit**.
  2. **E1 mới làm phần `build_src_filter`** (theo chỉ thị A1); `test_filter` giữ nguyên. E2 vẫn `Pending` vì ESP32 build đang bị chặn bởi lỗi pre-existing `uart_read_byte_from_fifo`.
  3. **Task kế tiếp theo thứ tự bắt buộc:** `C1` (bump `RF_PROTOCOL_VERSION` `0x01 → 0x02`) → `B1` → `B2` → `D1` (đóng nốt fail CCITT duy nhất) → `D2` → `D2` còn lại → `E1` → `E2`.
  4. **Rủi ro vận hành:** CRC trên wire đã là Modbus nhưng `RF_PROTOCOL_VERSION` **vẫn `0x01`** cho tới khi C1 chạy — plan chấp nhận tạm thời, **không flash deploy** gateway/node giữa A1 và C1.

### [2026-09-26 06:16:26 UTC] Sprint 1 QA Gate Verification — S1-HARD-01..05, chờ QA Review

- **Thời gian thực hiện:** 2026-09-26 06:16:26 UTC
- **Task ID:** **S1-HARD-01, S1-HARD-02, S1-HARD-03, S1-HARD-04, S1-HARD-05** (Quality Gates, Sprint 1)
- **Trạng thái hiện tại:** **Đang chờ QA Review** (`[ ] QA Review`)
- **Danh sách file đã tạo mới / sửa đổi:**
  - `[MODIFIED]` `.ai/planning/esp32-rf-crc16/PROGRESS.md` (cập nhật 5 Quality Gates từ `Pending` → `In Progress` → `QA Review`; bổ sung bằng chứng gate và đối chiếu regression baseline)
  - `[MODIFIED]` `.ai/planning/esp32-rf-crc16/WALKTHROUGH_LOG.md` (chèn bản ghi mới nhất ở đầu file theo thứ tự thời gian đảo ngược)
  - `[VERIFIED - NOT MODIFIED THIS RUN]` `aeroponics-firmware/include/core/Crc16Modbus.h`, `src/core/Crc16Modbus.cpp`, `test/test_crc16/test_crc16.cpp`, `test/test_crc16/test_crc16_runner.c`, `platformio.ini` — các file này do các task A1/A2/B1/B2/C1 tạo ra ở các bản ghi trước; lần chạy này chỉ kiểm chứng, không sửa đổi.
- **Giải trình ngắn gọn về giải pháp logic đã viết và kết quả tự kiểm tra mã nguồn:**
  1. **S1-HARD-01:** `pio test -e native -f test_crc16` đạt **9/9 PASS**. Các vector độc lập khớp chính xác: `"123456789" → 0x4B37`, `{04 06 09} → 0xA7F3`, `{04 07 09} → 0x37F2`; các frame đầy đủ đều cho remainder `0x0000`. Một chương trình tham chiếu table-based độc lập cũng khớp toàn bộ fuzz payload độ dài 0..255.
  2. **S1-HARD-02:** `nullptr` với độ dài khác 0 trả `0`; verify null/short trả `false`; append từ chối null, `capacity < data_len + 2` và `data_len > capacity`; canary xác nhận không ghi khi bị từ chối.
  3. **S1-HARD-03:** Quét `Crc16Modbus.h/.cpp` không có `new`, `malloc`, `calloc`, `realloc` hoặc `free`; `nm -u` trên object strict-build không có symbol heap. Build với `-Os -fno-exceptions -fno-rtti -Wall -Wextra -Werror` đạt 0 lỗi, 0 cảnh báo.
  4. **S1-HARD-04:** Core chỉ include `core/Crc16Modbus.h`, `<cstddef>` và `<cstdint>`; không có Arduino, ESP-IDF hoặc FreeRTOS include.
  5. **S1-HARD-05:** Thuật toán dùng vòng lặp bit-by-bit 8 bước mỗi byte, không có LUT 256 phần tử; phù hợp giới hạn SRAM ATmega8.
  6. **Regression:** `test_fsm` đạt 21/21 PASS. `test_production` giữ nguyên kết quả baseline trước Sprint 1 (97 failed/104 succeeded), xác nhận không phát sinh hồi quy từ CRC utility. Bare `pio test -e native` cũng giữ nguyên trạng thái pre-existing 0 test cases; acceptance command có filter `test_crc16` vẫn PASS.
- **Lưu ý:** Không sửa `rf_frame_codec.cpp`, `agu_legacy_codec.cpp` hoặc `docs/interface-wire-contract.md`. Các gate chỉ chuyển đến `QA Review`, chưa đánh dấu `[x] Done`; cần Review Agent kiểm toán độc lập.

### [2026-09-26 05:05:31 UTC] Task D1 — CRC16 CCITT Reference Review (Track D — Business Layer Boundary, Review Only), chờ QA Review

- **Thời gian thực hiện:** 2026-09-26 05:05:31 UTC
- **Task ID:** **D1** (Track D — Business Layer Boundary (Review Only), Sprint 1)
- **Trạng thái hiện tại:** **Đang chờ QA Review** (`[ ] QA Review`)
- **Danh sách file đã tạo mới / sửa đổi:**
   - `[MODIFIED]` `.ai/planning/esp32-rf-crc16/PROGRESS.md` (Cập nhật Task D1: `Pending` → `In Progress` → `QA Review`; bổ sung xác nhận giá trị đã kiểm chứng độc lập vào cột Technical Notes)
   - `[MODIFIED]` `.ai/planning/esp32-rf-crc16/WALKTHROUGH_LOG.md` (Thêm bản ghi thực thi này ở đầu file, thứ tự thời gian đảo ngược)
   - `[NOT MODIFIED]` `docs/interface-wire-contract.md` — **TUYỆT ĐỐI KHÔNG sửa trong Sprint 1** theo ràng buộc D1; chỉ review/đọc để xác nhận sai lệch.
- **Giải trình ngắn gọn về giải pháp logic đã viết và kết quả tự kiểm tra mã nguồn:**
   1. **D1 — Review CRC16 CCITT reference (read-only, KHÔNG sửa doc):**
      - Đọc và xác nhận `docs/interface-wire-contract.md:312` hiện ghi: `` - CRC16 ASCII `123456789` = `0x29B1`. `` — đây là giá trị của **CRC-16/CCITT-FALSE** (đa thức `0x1021`, MSB-first), KHÔNG phải CRC16-Modbus.
      - **Kiểm chứng độc lập bằng code C thuần** (không dùng lại source firmware): tính lại từ đầu cả hai thuật toán trên ASCII `"123456789"`:
        - CCITT-FALSE → `0x29B1` → **MATCH** với giá trị sai lệch trong doc.
        - CRC16-Modbus → `0x4B37` → **MATCH** với giá trị đúng của Sprint 1 (đã bao phủ bởi test_crc16 golden vector).
        - Bonus cross-check vector Sprint 1 (đều MATCH): `{0x04,0x06,0x09}` → `0xA7F3`; `{0x04,0x07,0x09}` → `0x37F2`.
      - **Kết luận:** sai lệch được xác nhận là thật, và giá trị `0x4B37` (Modbus) là giá trị đúng cần cập nhật.
      - **Quét phạm vi ảnh hưởng (để Sprint 5 chuẩn bị, KHÔNG sửa ở Sprint 1):** ngoài `docs/interface-wire-contract.md:312`, giá trị/ghi chú CCITT `0x29B1` còn xuất hiện ở:
        - `aeroponics-firmware/test/test_production/test_production.cpp` (test CCITT cũ `test_rf_crc16_ccitt_false_standard_test_vector` + dòng assert `0x29B1`).
        - `docs/RF_PROTOCOL.md` (§2.2 CRC-16/CCITT-FALSE Specification, test vector `0x29B1`).
        - `docs/RF_FLOW_POC_TEST_PLAN.md` (TP-PROTO-01, vector `0x29B1`).
        - Ghi chú: `src/rf_frame_codec.cpp` **vẫn dùng CCITT `0x1021` (poly)** — nhưng Sprint 1 **KHÔNG sửa** file này (ràng buộc sprint); Sprint 2 mới migrate.
      - **Action theo yêu cầu Task:** mapping table "Discrepancy Tracking (Track D - Deferred)" trong PROGRESS.md đã có sẵn và đã được xác nhận đúng — giữ nguyên cấu trúc, chỉ bổ sung xác nhận giá trị đã kiểm chứng trong Technical Notes của D1.
   2. **Kết quả tự kiểm tra:**
      - Chương trình kiểm chứng C thuần compile với `-Wall -Wextra -Werror` → 0 error, 0 warning.
      - Xác nhận 4/4 giá trị CRC (CCITT `0x29B1`, Modbus `0x4B37`, pump-on `0xA7F3`, pump-off `0x37F2`) đều MATCH với giá trị kỳ vọng.
      - **Ràng buộc tôn trọng:** KHÔNG sửa `docs/interface-wire-contract.md`; KHÔNG sửa `rf_frame_codec.cpp` / `agu_legacy_codec.cpp` (đúng constraint Sprint 1).
      - **Không phát sinh nợ kỹ thuật:** D1 là task review-only nên chỉ tạo/cập nhật 2 file planning (`PROGRESS.md`, `WALKTHROUGH_LOG.md`); không đụng vào bất kỳ file production nào.
- **Lưu ý:** D1 là Task cuối cùng của Track D và là task cuối của Sprint 1 Track A→D (S1-T6). Sau khi D1 vào QA Review, toàn bộ 6 task (A1, A2, B1, B2, C1, D1) của Sprint 1 đều đã qua bước "viết code / review" và chờ Review Agent kiểm toán độc lập (chưa đánh dấu `[x] Done`).

### [2026-09-26 04:57:30 UTC] Task C1 — platformio.ini test_filter Update (Sprint 1: Chuẩn hóa thuật toán CRC16-Modbus + Golden Vectors), chờ QA Review

- **Thời gian thực hiện:** 2026-09-26 04:57:30 UTC
- **Task ID:** **C1** (Track C — Build/CI Layer (Native Test Filter), Sprint 1)
- **Trạng thái hiện tại:** **Đang chờ QA Review** (`[ ] QA Review`)
- **Danh sách file đã tạo mới / sửa đổi:**
  - `[MODIFIED]` `aeroponics-firmware/platformio.ini` (Cập nhật `test_filter` của `[env:native]` thêm `test_crc16` → `test_production, test_fsm, test_crc16`)
  - `[MODIFIED]` `.ai/planning/esp32-rf-crc16/PROGRESS.md` (Cập nhật Task C1: `Pending` → `In Progress` → `QA Review`)
- **Giải trình ngắn gọn về giải pháp logic đã viết và kết quả tự kiểm tra mã nguồn:**
  1. **C1 — Test filter (`platformio.ini`):** Môi trường native `[env:native]` có `test_filter = test_production, test_fsm` (chỉ chạy regression Production Gateway + FSM). Thêm `test_crc16` vào danh sách để CI tự động bao gồm suite CRC16-Modbus mới — đảm bảo đủ 3 test target: `test_production, test_fsm, test_crc16`.
  2. **Kết quả tự kiểm tra mã nguồn:**
     - `pio test -e native -f test_crc16` → **9/9 PASSED** (0 failure), build 0 warning.
     - Không sửa `rf_frame_codec.cpp` hay `agu_legacy_codec.cpp` (sprint constraint giữ nguyên).
     - `git status`: chỉ có 1 file production thay đổi (`platformio.ini`) + file planning `PROGRESS.md`; không phát sinh nợ kỹ thuật.
- **Lưu ý:** Task D1 (Track D) vẫn `[ ] Pending` — là task "Review Only" deferred sang Sprint 5, không thuộc đợt xử lý này.

### [2026-09-26 04:49:28 UTC] Task B1 & B2 — CRC-16/Modbus Unity Native Test Suite (Sprint 1: Chuẩn hóa thuật toán CRC16-Modbus + Golden Vectors), chờ QA Review

- **Thời gian thực hiện:** 2026-09-26 04:49:28 UTC
- **Task ID:** **B1, B2** (Track B — Test Layer (Unity Native), Sprint 1)
- **Trạng thái hiện tại:** **Đang chờ QA Review** (`[ ] QA Review`)
- **Danh sách file đã tạo mới / sửa đổi:**
  - `[CREATED]` `aeroponics-firmware/test/test_crc16/test_crc16.cpp` (Suite unit test Unity: 9 test case gồm golden vectors, append layout, fail-closed null/boundary, anti-tamper single-bit flip toàn bộ vị trí, và roundtrip max frame 255 byte)
  - `[CREATED]` `aeroponics-firmware/test/test_crc16/test_crc16_runner.c` (Unity runner native: `UNITY_BEGIN()` → 9 × `RUN_TEST()` → `return UNITY_END();`, khai báo `extern "C"` linkage với các test body, không phụ thuộc phần cứng)
  - `[MODIFIED]` `.ai/planning/esp32-rf-crc16/PROGRESS.md` (Cập nhật Task B1, B2: `Pending` → `In Progress` → `QA Review`; cập nhật Last Updated / Current Phase)
- **Giải trình ngắn gọn về giải pháp logic đã viết và kết quả tự kiểm tra mã nguồn:**
  1. **B1 — Test cases (`test_crc16.cpp`):** Test theo chiến lược TDD với giá trị độc lập từ Big Plan, khẳng định chính xác từng byte — không "sửa vector cho khớp code" (S1-HARD-01):
     - Golden vectors: `"123456789"` → `0x4B37`; `{0x04,0x06,0x09}` → `0xA7F3` (append LE `{0xF3,0xA7}` + verify true); `{0x04,0x07,0x09}` → `0x37F2` (append LE `{0xF2,0x37}` + verify true).
     - Append layout: `appendCrc16Modbus` trả `data_len + 2`, ghi `frame[3]=lo`, `frame[4]=hi`.
     - Edge cases (S1-HARD-02): `calculateCrc16Modbus(nullptr,0) == 0xFFFF` (quy ước empty), `(nullptr,10) == 0` (fail-closed), `verifyCrc16Modbus(nullptr,8) == false`; `verify` từ chối `len < 2`; `append` từ chối `capacity < data_len + 2` (kể cả `data_len > capacity` không underflow) và không ghi chìm guard byte.
     - Anti-tamper: duyệt đủ `5 × 8 = 40` phép lật 1-bit trên frame chuẩn, mọi trường hợp đều `verify == false`.
     - Boundary `len=255`: payload 253 + CRC 2 roundtrip append/verify PASS; payload rỗng append `{0xFF,0xFF}` cũng verify PASS.
  2. **B2 — Runner (`test_crc16_runner.c`):** Khai báo 9 prototype `void test_...(void)` với C linkage (test body dùng `extern "C"` trong `.cpp`), `main(void)` gọi `UNITY_BEGIN()`, `RUN_TEST()` cho cả 9 case, trả về `UNITY_END()` — host native, không chạy trên phần cứng.
  3. **Kết quả tự kiểm tra mã nguồn:**
     - `pio test -e native -f test_crc16` → **9/9 PASSED** (0 failure), build 0 warning.
     - Không sửa `platformio.ini`, `rf_frame_codec.cpp`, hay `agu_legacy_codec.cpp` (Task C1 và Sprint constraint còn nguyên vẹn).
     - `git status`: chỉ có 2 file mới `test/test_crc16/` + 1 file planning đã sửa (`PROGRESS.md`); không có thay đổi file production nào.
- **Lưu ý:** Task C1 (`test_filter` của `[env:native]`) chưa chạy vì thuộc Track C; suite `test_crc16` chỉ được chạy bằng `-f` filter riêng trong lần kiểm chứng này.

### [2026-09-26 04:16:31 UTC] Task A1 & A2 — CRC-16/Modbus Pure Domain Utility (Sprint 1: Chuẩn hóa thuật toán CRC16-Modbus + Golden Vectors), chờ QA Review

- **Thời gian thực hiện:** 2026-09-26 04:16:31 UTC
- **Task ID:** **A1, A2** (Track A — Domain Layer (Pure Utility), Sprint 1)
- **Trạng thái hiện tại:** **Đang chờ QA Review** (`[ ] QA Review`)
- **Danh sách file đã tạo mới / sửa đổi:**
  - `[CREATED]` `aeroponics-firmware/include/core/Crc16Modbus.h` (Header khai báo 3 hàm pure utility: `calculateCrc16Modbus`, `appendCrc16Modbus`, `verifyCrc16Modbus`; constexpr constants `kCrc16ModbusInitialValue = 0xFFFF`, `kCrc16ModbusPolynomial = 0xA001`; `#pragma once`; chỉ include `<cstddef>` và `<cstdint>`)
  - `[CREATED]` `aeroponics-firmware/src/core/Crc16Modbus.cpp` (Cài đặt bitwise LSB-first reflected CRC-16/Modbus; fail-closed null/boundary checks; little-endian append; verify dùng Modbus remainder-zero property; không heap allocation; không platform-specific includes)
  - `[MODIFIED]` `.ai/planning/esp32-rf-crc16/PROGRESS.md` (Cập nhật Task A1, A2: `Pending` → `In Progress` → `QA Review`)
- **Giải trình ngắn gọn về giải pháp logic đã viết và kết quả tự kiểm tra mã nguồn:**
  1. **A1 — Header (`Crc16Modbus.h`):** Khai báo hằng `kCrc16ModbusInitialValue = 0xFFFF`, `kCrc16ModbusPolynomial = 0xA001`, `kCrc16ModbusSize = 2` và 3 hàm thuần (pure functions, zero state). Toàn bộ doxygen params/return/invariant rõ ràng. Không có Arduino/ESP-IDF/FreeRTOS include — thỏa S1-HARD-04.
  2. **A2 — Implementation (`Crc16Modbus.cpp`):**
     - `calculateCrc16Modbus`: Fail-closed `data == nullptr && len != 0 → return 0`; init `0xFFFF`, XOR byte → loop 8 bit: `(crc & 1) ? ((crc >> 1) ^ 0xA001) : (crc >> 1)`. Zero heap allocation (S1-HARD-03). Loop bit-by-bit không dùng 256-byte LUT (S1-HARD-05).
     - `appendCrc16Modbus`: `frame == nullptr || data_len > capacity || capacity - data_len < 2 → return 0`. Ghi CRC_Lo tại `frame[data_len]`, CRC_Hi tại `frame[data_len+1]` (little-endian). Return `data_len + 2` trên success.
     - `verifyCrc16Modbus`: `frame == nullptr || len < 2 → false`. Dùng Modbus remainder-zero property: `calculateCrc16Modbus(frame, len) == 0` — khớp Delphi `TSCI.CheckCRC16`.
  3. **Golden vectors verified (S1-HARD-01):**
     - `"123456789"` → `0x4B37` ✓
     - `{0x04, 0x06, 0x09}` → `0xA7F3` (append LE `{0xF3, 0xA7}`) ✓
     - `{0x04, 0x07, 0x09}` → `0x37F2` (append LE `{0xF2, 0x37}`) ✓
  4. **Kết quả tự kiểm tra mã nguồn:**
     - 11/11 unit tests独立 PASS: golden vectors x3, empty buffer, null+nonzero fail-closed, append LE layout, append insufficient capacity, verify good frame, verify single-bit flip, verify nullptr, verify len<3.
     - Max payload roundtrip 253+2=255 bytes PASS; tamper detection 13/13 sampled positions PASS.
     - Compile strict: `-std=c++17 -Os -fno-exceptions -fno-rtti -Wall -Wextra -Werror` → 0 errors, 0 warnings (both ATMEGA8-profile and native profiles).
     - Không đụng vào `rf_frame_codec.cpp` hoặc `agu_legacy_codec.cpp` (sprint constraint).
> **Documentation audit boundary (2026-09-26):** This execution log records repository/model tests and build observations only. It does not establish real-wire legacy AGU evidence, independent 4-node hardware acceptance, or a release-green status. Modern RF documentation is version `0x02`, `HMAC_TAG_SIZE=16`, CRC16-Modbus (init `0xFFFF`, poly `0xA001`) over header + payload + HMAC tag, with `[CRC_LO][CRC_HI]`; AGU legacy zero-sum remains separate. Historical CCITT references are storage/legacy history only.
