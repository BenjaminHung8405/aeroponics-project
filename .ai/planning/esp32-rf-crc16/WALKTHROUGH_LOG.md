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
