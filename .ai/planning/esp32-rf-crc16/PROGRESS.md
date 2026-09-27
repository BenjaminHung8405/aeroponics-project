# PROGRESS.md - ESP32 RF CRC16 Project

## Started

- **Timestamp:** 2026-09-26 14:24:26 (Asia/Ho_Chi_Minh) / 2026-09-26T07:24:26Z
- **Execution Agent:** Gemini
- **Reviewer Agent:** GPT-5.3 (Implementation Architect / Senior Solution Architect)
- **Current Sprint:** Sprint 2 — Migrate `RfFrameCodec` sang CRC16-Modbus (gateway TX/RX + node build)

---

## Reference Plan

- **Plan Directory:** `.ai/planning/esp32-rf-crc16/`
- **Baseline Plan:** `.ai/planning/esp32-rf-crc16/README.md`
- **Current Sprint File:** `.ai/planning/esp32-rf-crc16/sprint_2.md`
- **Master Plan:** `.ai/planning/esp32-rf-crc16/EXECUTION_MASTER_PLAN.md` (dependency graph, risk register, quality gates, rollback)
- **Sprint Chain:** `sprint_0.md` (baseline, song song) → `sprint_1.md` → **`sprint_2.md` (current)** → `sprint_3a.md` (evidence, blocking) → `sprint_3.md` → `sprint_4.md` → `sprint_5.md` → `sprint_6.md`
- **Objective:** Thay thuật toán CRC-16/CCITT-FALSE (poly `0x1021`, MSB-first) trong modern `RfFrameCodec` bằng CRC16-Modbus (init `0xFFFF`, poly `0xA001`, LSB-first) — giữ nguyên wire-layout `[header][payload][hmac_tag(16)][crc_lo][crc_hi]` và dùng `RF_PROTOCOL_VERSION=0x02`.

> **Correction (2026-09-26, master-plan audit):** `HMAC_TAG_SIZE` thực tế là **16 byte** (`include/core/hmac_sha256.h:8`). Wire layout đúng: `[header(17)][payload(0..64)][hmac_tag(16)][crc_lo][crc_hi]` → `RF_MAX_FRAME_SIZE = 99`. Mọi fixture/test phải dùng 16 byte. This is a modern gateway/model contract, not evidence of ATmega8 legacy wire behavior.

### Sprint 1 Carry-over (Dependency satisfied)

| Item | State on entry to Sprint 2 |
|---|---|
| `include/core/Crc16Modbus.h` / `src/core/Crc16Modbus.cpp` | Created, 3 pure functions, awaiting independent QA sign-off |
| `test/test_crc16/` | 9/9 PASS via `pio test -e native -f test_crc16` |
| Golden vectors | `0x4B37`, `0xA7F3`, `0x37F2` independently verified |
| `rf_frame_codec.cpp` | **Untouched** (Sprint 1 constraint) — still CCITT `0x1021` |

> ✅ **DEF-01 RESOLVED (2026-09-27):** commit `434c80c` rewrote `platformio.ini` `test_filter` as newline-separated suite names, so bare `pio test -e native` now collects the full suite. With `test_rf_address` added it reports **315/315 PASS** (`test_crc16` 9, `test_fsm` 21, `test_production` 278, `test_rf_address` 7). The old "0 test cases" symptom is gone from the bare command.
>
> ⚠️ **Residual trap:** the CLI form `pio test -e native -f test_crc16,test_fsm,test_production` is parsed as **one glob**, matches no suite, and prints `0 test cases: 0 succeeded` with **exit 0** (false green). Never use the comma form as a gate; use bare `pio test -e native` or a single `-f <suite>` per run. The historical `test_production` baseline (97 pre-existing failures at `41feec6`) is now fully green at 278/278, so it no longer blocks the gate.

---

## Addition Plan

- **Status:** Đã bổ sung yêu cầu NodeID/GroupID RF addressing (planning-only; chưa code hóa)
- **Source requirement:** nodeID hiện tại là Hex `0x1..0xF`; RF groupID là `[0x10, 0x14, 0x18, 0x1C]`. Công thức xác nhận: `groupID = 0x10 | (nodeID & 0x0C)` — legacy Delphi `TestSCI.dpr:340` chứng minh `gid == $14` cho nodes `4..7`. **Không** phải `nodeID × $F8`.
- **Scope impact:** mở rộng address validation của RF codec từ topology production cũ `4..7` lên toàn bộ node address `1..F`, cho phép group address chỉ ở target, và giữ logical `group_id` backend ở `1..4`.
- **Compatibility boundary:** AGU legacy vẫn giữ node `4..7` cho tới khi có firmware legacy tương thích; không tự động mở rộng legacy command path.
- **Decision status:** RESOLVED — legacy Delphi source xác nhận `gid == $14 [4,5,6,7]`, khớp đúng công thức `0x10 | (nodeID & 0x0C)`. Ghi chú `$F8` là không chính xác; mask block là `0x0C`.
- **Notes:** Mọi yêu cầu phát sinh tiếp theo phải ghi ở đây **trước khi** code hóa, kèm lý do và ảnh hưởng phạm vi. Storage checksum `treatment_manager` vẫn là task riêng, không merge vào Sprint 2 (xem C2).

---

## Status Legend

| Marker | Meaning |
|---|---|
| `[ ]` | **Pending** — Task chưa chạm vào |
| `[ ]` | **In Progress** — Execution Agent đang viết code |
| `[ ]` | **QA Review** — Code đã viết xong, đang chờ rà soát chất lượng |
| `[x]` | **Done** — Đã qua vòng review nghiêm ngặt và được duyệt |

---

## Track A — Tầng Data (Codec)

> Nguồn: `sprint_2.md` §3 TRACK A (S2-T1, S2-T2). **Đây là track trọng yếu nhất của Sprint 2** — sai ở đây là sai toàn hệ RF.

| Task ID | Description | Status | Note / Chỉ thị kỹ thuật bắt buộc |
|---------|-------------|--------|--------------------------------|
| **A1** | `aeroponics-firmware/src/rf_frame_codec.cpp` — Thay thân hàm `RfFrameCodec::calculateCrc16` (hiện dòng 62–73) để delegate sang `calculateCrc16Modbus` | [ ] QA Review | **Design pattern:** *Adapter/Delegation* — giữ `RfFrameCodec::calculateCrc16` làm Facade ổn định, chỉ đổi implementation bên trong; **KHÔNG** đổi signature. **Anti-technical-debt:** xoá hẳn vòng lặp CCITT cũ, không giữ dead code `0x1021` trong file; nếu cần rollback thì dựa vào git tag, không để nhánh code chết.<br>**Anti-technical-debt #2 (bắt buộc):** không chép lại thuật toán Modbus vào đây — **chỉ gọi** `core/Crc16Modbus.h`. Một nguồn sự thật duy nhất; cấm tạo bản sao "tạm thời".<br>**Security:** giữ nguyên null-guard `if (data == nullptr && len != 0) return 0;` — hành vi này **khớp chính xác** với `calculateCrc16Modbus` (đã kiểm chứng: `(nullptr,0) → 0xFFFF`, `(nullptr,10) → 0`), nên delegate là **behavior-preserving**; không được thêm/bớt guard.<br>**Include:** thêm `#include "core/Crc16Modbus.h"`; bảo đảm header này nằm trong `build_src_filter` của **mọi** env (`native`, `esp32-s3-devkitc-1`, `atmega8-node-*`) — nếu chưa có thì bổ sung (xem E1).<br>**Verification:** `calculateCrc16("123456789") == 0x4B37` (không phải `0x29B1`). |
| **A2** | `aeroponics-firmware/include/rf_frame_codec.h` — Document `@brief` CRC-16/MODBUS + (tuỳ chọn) alias `calculateCrc16Modbus` | [ ] QA Review | **Open/Closed Principle:** chỉ **bổ sung** tài liệu, **không** xoá declaration cũ và **không** đổi tên hàm — 3 caller hiện tại (`encodeFrame:155`, `decodeFrameDetailed:216`, `PumpNodeController:170`) phải tiếp tục build sạch.<br>**Doc bắt buộc:** ghi rõ `@brief CRC-16/MODBUS (init 0xFFFF, poly 0xA001, reflected LSB-first) — since RF_PROTOCOL_VERSION 0x02`, kèm 3 contract line: (1) CRC **không** phải authentication, HMAC mới là; (2) CRC tính trên `[header + payload + hmac_tag]`, **không** gồm 2 byte CRC đuôi; (3) wire order `[crc_lo][crc_hi]` little-endian.<br>**Anti-technical-debt:** alias `calculateCrc16Modbus` (nếu thêm) chỉ là **forwarding `static inline`/delegate** — **cấm** cài đặt logic CRC thứ hai trong header.<br>**Cấm:** magic number `0xA001` rải rác trong file codec — dùng `kCrc16ModbusPolynomial` từ `core/Crc16Modbus.h`. |

---

## Track B — Tầng Nghiệp vụ (Delegates)

> Nguồn: `sprint_2.md` §3 TRACK B (S2-T3, S2-T4).

| Task ID | Description | Status | Note / Chỉ thị kỹ thuật bắt buộc |
|---------|-------------|--------|--------------------------------|
| **B1** | `aeroponics-firmware/include/pump_node_controller.h:169` — `PumpNodeController::calculateCrc16` delegate | [ ] QA Review | **Hành động chính: KHÔNG ĐỔI CODE** — `return RfFrameCodec::calculateCrc16(data, len);` đã tự động theo Modbus sau A1. Đây là task **verification-only**; chỉ sửa nếu A1 bắt buộc phải đổi interface.<br>**Bắt buộc:** `grep -n "0x1021" src/pump_node_controller.cpp include/pump_node_controller.h` → **phải trả về rỗng**. Nếu có kết quả → **DỪNG**, tạo task con mới, **không** sửa âm thầm.<br>**Anti-technical-debt:** cấm tạo thêm wrapper/delegate trung gian không cần thiết — 1 tầng delegate duy nhất là codec. |
| **B2** | `aeroponics-firmware/src/pump_node_controller.cpp:242` — `verifyCrcAndMac` / `buildFrame`: xác nhận `crc_check_len = frame_len - 2` | [ ] QA Review | **Invariant bắt buộc:** `expected_crc = calculateCrc16(frame_data, frame_len - 2)` — đối chiếu bằng `readU16Le(frame_data + frame_len - 2)`; bất kỳ sai lệch nào = fail ngay.<br>**Security — thứ tự kiểm tra:** giữ nguyên **CRC trước, HMAC sau** (fail-fast). **Cấm đảo thứ tự** trong Sprint 2 (nếu đảo phải mở task riêng + review bảo mật, xem S4-HARD-06).<br>**Bound-check:** phải có guard độ dài tối thiểu trước khi đọc 2 byte cuối; tuyệt đối không đọc `frame_data[frame_len-1]` khi `frame_len < 2`.<br>**Anti-technical-debt:** sửa gộp toàn bộ vào 1 commit nhỏ, không trộn refactor vô tình với CRC change. |

---

## Track C — Version / Persistence Boundary

> Nguồn: `sprint_2.md` §3 TRACK C (S2-T5, S2-T6). **C2 là track có nguy cơ phá NVS cao nhất — đọc kỹ trước khi code.**

| Task ID | Description | Status | Note / Chỉ thị kỹ thuật bắt buộc |
|---------|-------------|--------|--------------------------------|
| **C1** | `aeroponics-firmware/include/config.h:150` — bump `RF_PROTOCOL_VERSION` từ `0x01` → `0x02` | [ ] QA Review | **Giá trị bắt buộc:** `constexpr uint8_t RF_PROTOCOL_VERSION = 0x02;`<br>**Security/Protocol rule (S2-HARD-01):** bump version là **bắt buộc**, không phải tuỳ chọn — đây là hàng rào chặn node cũ (CCITT) lọt qua filter của gateway mới (Modbus) và ngược lại. Thiếu bump = trạng thái "cả hai đầu đều decode được nhưng CRC luôn mismatch", mất lệnh âm thầm.<br>**Anti-technical-debt (magic number):** `decodeFrameDetailed` / `validateFrameEnvelope` phải đọc từ `RF_PROTOCOL_VERSION`; **cấm** hardcode `0x02` bất kỳ đâu trong logic check version.<br>**Verification:** assert/pin rằng node build (`ATMEGA8_NODE_BUILD`) và gateway build đều compile với cùng hằng này từ một nguồn duy nhất. |
| **C2** | `aeroponics-firmware/include/treatment_manager.h:42-60` — tách `computeChecksum()` khỏi `RfFrameCodec::calculateCrc16` | [ ] QA Review | **Quy tắc tuyệt đối (S2-HARD-06):** checksum NVS của treatment snapshot **KHÔNG thuộc RF wire** → **KHÔNG ĐƯỢC** bị đổi thuật toán chỉ vì Sprint 2 đổi codec. Nếu để nguyên gọi `RfFrameCodec::calculateCrc16` sau A1 → toàn bộ snapshot NVS cũ sẽ **fail verify** và bị coi là hỏng dữ liệu. → **Bắt buộc tách.**<br>**Design pattern:** *Extract Function* — tạo `static uint16_t calculateStorageCrc16(const uint8_t*, size_t)` **riêng trong module**, giữ thuật toán **CCITT-FALSE (poly `0x1021`)** để bảo toàn dữ liệu NVS cũ. Cấm sử dụng lại `RfFrameCodec::calculateCrc16` cho mục đích lưu trữ.<br>**Security:** thêm comment `@brief Internal NVS integrity checksum — deliberately NOT the RF wire CRC (deliberately frozen to CCITT-FALSE to preserve stored snapshots).` để người sau không "tối ưu" nhầm.<br>**Anti-technical-debt:** **KHÔNG** thay đổi cấu trúc lưu trữ NVS, **KHÔNG** viết migration script trong Sprint 2. Nếu team muốn migrate storage luôn → tạo Task riêng ở Sprint 4/5 kèm migration + backup NVS; **ưu tiên giữ nguyên trong sprint này**.<br>**Bắt buộc:** grep toàn repo `calculateCrc16` sau tất cả task → mọi call-site còn lại **phải là RF wire path**; bất kỳ call-site nào là storage path là lỗi. |

---

## Track D — Tầng Test / QA

> Nguồn: `sprint_2.md` §3 TRACK D (S2-T7, S2-T8). **Track này là hàng rào phát hiện lỗi port MSB/LSB — không được bỏ sót fixture nào.**

| Task ID | Description | Status | Note / Chỉ thị kỹ thuật bắt buộc |
|---------|-------------|--------|--------------------------------|
| **D1** | `aeroponics-firmware/test/test_production/test_production.cpp` — Thay golden vector CRC tại dòng 2252, 2256, 6755–6758 | [ ] QA Review | **Đổi tên hàm:** `test_rf_crc16_ccitt_false_standard_test_vector` → `test_rf_crc16_modbus_standard_vector`; cập nhật cả call-site `RUN_TEST` tại dòng 10431 (đừng bỏ sót — đây là lỗi build/ghost test phổ biến nhất).<br>**Giá trị bắt buộc:** `"123456789"` → `0x4B37`; thay toàn bộ `0x29B1` và `0x1021` còn sót.<br>**Edge case (`calculateCrc16(nullptr,*)`):** giữ đúng quy ước Sprint 1 đã chốt — `(nullptr, 0) == 0xFFFF`, `(nullptr, 10) == 0`.<br>**Tuyệt đối cấm (S1-HARD-01 carry-over):** **KHÔNG sửa vector để cho khớp code.** Nếu code cho ra kết quả khác `0x4B37` → code sai, phải sửa code, không sửa test. Vector là chuẩn độc lập từ Big Plan.<br>**Fixture hằng hex (dòng ~8124 `hmac_corrupt` và mọi fixture encode baseline):** **REGENERATE** bằng `appendCrc16Modbus` từ `core/Crc16Modbus.h`, **không** gõ tay hex CRC. |
| **D2** | `test_production.cpp` — rà & sửa toàn bộ integration fixture frame hex tĩnh + thêm test 1-bit flip | [ ] QA Review | **Quét bắt buộc:** `rg -n '0x29B1\|0x1021\|ccitt\|CCITT' test/test_production/test_production.cpp` → mọi kết quả đều phải được xử lý (đổi giá trị **hoặc** chuyển thành comment lịch sử được ghi rõ). Không được để sót.<br>**Test mới bắt buộc:** flip 1 bit trong **payload** → `decodeFrameDetailed` trả **chính xác** `ParseError::CRC_MISMATCH` (không phải `HMAC_AUTH_FAIL`, không phải crash, không phải `OK`).<br>**Test mới bắt buộc #2:** với buffer chứa frame hợp lệ → `calculateCrc16(frame, len-2) == readU16Le(frame+len-2)`.<br>**Bắt buộc:** có ít nhất **1** test case hợp lệ PASS để chứng minh harness không "luôn luôn fail" (anti-tautology).<br>**Anti-technical-debt:** fixture CRC **phải sinh từ code** qua `appendCrc16Modbus`; tuyệt đối **không hardcode** chuỗi hex CRC mới — tránh nợ kỹ thuật "vector lệch với thuật toán" trong mọi lần thay đổi CRC sau này. |

---

## Track E — Tầng Build (Node + Filter)

> Nguồn: `sprint_2.md` §3 TRACK E (S2-T9, S2-T10).

| Task ID | Description | Status | Note / Chỉ thị kỹ thuật bắt buộc |
|---------|-------------|--------|--------------------------------|
| **E1** | `aeroponics-firmware/platformio.ini` — verify `build_src_filter` / `test_filter` đã bao gồm `core/Crc16Modbus.*` | [ ] QA Review | **Không thêm env mới.** Rà `build_src_filter` của `atmega8-node-*`: nếu đang filter theo glob, phải bảo đảm `core/Crc16Modbus.cpp` **vào** build (nếu không → link error ở Track E2, đây là nguyên nhân lỗi phổ biến nhất).<br>**ATmega8 constraint (S2-HARD-04):** file phải compile dưới profile `-Os -fno-exceptions -fno-rtti`; cấm heap allocation trong đường CRC (Sprint 1 đã bảo đảm — không được phá vỡ).<br>**Ghi chú (DEF-01 resolved):** `test_filter` hiện là newline-separated (`test_production` / `test_fsm` / `test_crc16` / `test_rf_address`) trong commit `434c80c` — giữ 4 suite này, chỉ dùng separator hợp lệ (newline), **không** quay lại dạng comma. |
| **E2** | Build & smoke test: `pio run -e esp32-s3-devkitc-1`, `pio run -e atmega8-node-4`, `pio test -e native` | [ ] QA Review | **Lệnh bắt buộc (chạy đủ 3, exit code = 0):**<br>• `pio run -e esp32-s3-devkitc-1` (gateway)<br>• `pio run -e atmega8-node-4` (node ATmega8)<br>• `pio test -e native` (bare — DEF-01 đã sửa; chạy **315/315** gồm 4 suite). **Không** dùng dạng CLI comma `-f a,b,c` (parse thành 1 glob → 0 test, exit 0).<br>**Bắt buộc:** chạy `post:scripts/check_atmega8_size.py` sau khi build node — **flash/SRAM vượt ngưỡng là fail release** (đây là hệ quả trực tiếp của việc thêm utility vào node build).<br>**Fail-closed (S2-HARD-02):** xác nhận CRC sai → `ParseError::CRC_MISMATCH` → **drop frame**; tuyệt đối không đưa frame CRC sai vào `handleIncomingFrame`.<br>**Wire layout (S2-HARD-05):** xác nhận roundtrip `appendCrc16Modbus` + `readU16Le` — thứ tự `[crc_lo][crc_hi]` bắt buộc giữ nguyên.<br>**Ghi lại:** bằng chứng build (log, flash size) vào `WALKTHROUGH_LOG.md`; **không** tự ý đánh dấu `Done` — phải qua QA Review. |

---

## Track F — NodeID / GroupID RF Addressing (DEFERRED — không thuộc Sprint 2/3)

> Chi tiết thiết kế: `node_group_scheme.md`.
>
> **Trạng thái:** `[ ] DEFERRED` — tách khỏi Sprint 2 theo scope boundary `S2-ADDR-00`. Sprint 2 chỉ đổi CRC + version. Phần group address **trên legacy SCI** đã được đưa vào Sprint 3 (`S3-T2A`, `S3-ADDR-01`) vì bằng chứng `TestSCI.dpr:340` cho thấy group address là khái niệm thật của AGU legacy, không phải của modern `RfFrameCodec`.
>
> **Khi nào Track F chạy:** sau Sprint 5, dưới dạng ticket riêng, kèm feature flag. Lý do trì hoãn: mở rộng address space modern RF là **tính năng routing**, không phải điều kiện cần để CRC migration đúng. Gateway hiện đã fan-out group command bằng unicast (`queueExternalGroupCommand` lặp từng node), nên thiếu group multicast **không** làm sai chức năng — chỉ kém hiệu quả.

| Task ID | Description | Status | Note / Chỉ thị kỹ thuật bắt buộc |
|---------|-------------|--------|--------------------------------|
| **F1** | Freeze address constants and mapping | [ ] Deferred | Chốt node `0x1..0xF`, gateway `0x0`, RF group `[0x10,0x14,0x18,0x1C]`; công thức `groupID = 0x10 \| (nodeID & 0x0C)` (dùng `\|` hoặc `OR` bitwise để tránh phá vỡ Markdown table); named constants trong `config.h`, không rải magic number. |
| **F2** | Split node/group/source/target validation | [ ] Pending | `isValidNodeId` nhận `1..F`; `isValidRfGroupId` chỉ nhận 4 giá trị; group address chỉ hợp lệ ở target. `source_node_id` tuyệt đối không được là group address. |
| **F3** | Expand RF codec topology without widening AGU legacy | [ ] Pending | Cập nhật `RfFrameCodec::isValidAddress` và routing group target; giữ AGU legacy `4..7`; phân loại rõ production RF path và legacy path, không sửa âm thầm các guard legacy. |
| **F4** | Preserve logical group namespace | [ ] Pending | Backend/MQTT/database vẫn dùng `group_id 1..4`; tạo một mapping helper ở RF boundary; test round-trip logical group ↔ RF group address. |
| **F5** | Address-aware CRC/golden frames | [ ] Pending | Regenerate frame fixtures bằng CRC16-Modbus cho node `0x01`, `0x0F` và group `0x10/0x14/0x18/0x1C`; flip address bit phải trả `CRC_MISMATCH`; không hardcode CRC thủ công. |
| **F6** | End-to-end and compatibility verification | [ ] Pending | Chạy native/ESP32/ATmega8; xác nhận 15 node address pass, 4 group address pass, invalid group/source bị reject fail-closed; AGU `4..7` không regression; cập nhật `WALKTHROUGH_LOG.md`. |

### Track F Quality Gates

| Gate ID | Description | Status | Verification |
|---|---|---|---|
| **S2-ADDR-00** | **Scope freeze (đang áp dụng):** Sprint 2 không đổi `isValidAddress`, không đổi node range, không nhận group target | [ ] Active | Diff review: `git diff` Sprint 2 không được chạm `RF_MAX_NODE_ID` / `isProductionNodeId` / `isValidAddress` |
| **F-ADR-01** | Node space đúng `0x1..0xF`, gateway `0x0`; bỏ giới hạn RF tổng quát ở `1..12` | [ ] Deferred | Unit test + grep constants/guards |
| **F-ADR-02** | Chỉ 4 RF group address hợp lệ trên modern codec | [ ] Deferred | Positive/negative table-driven tests |
| **F-ADR-03** | Group chỉ xuất hiện ở target; source group bị reject | [ ] Deferred | Codec validation tests |
| **F-ADR-04** | Logical group `1..4` không bị đổi thành Hex RF ID trong backend/API | [ ] Deferred | Mapping and MQTT contract tests |
| **F-ADR-05** | CRC bao phủ address bytes và fail-closed khi address bị mutate | [ ] Deferred | Golden frames + one-bit-flip tests |
| **S3-ADDR-01** | **Đang Sprint 3:** legacy group target `$14` an toàn — không broadcast read/config, không giả ACK | [ ] Pending | `S3-T2A` characterization + `transact()` review |

---

## Quality Gates (Sprint 2)

| Gate ID | Description | Status | Verification Command / Evidence |
|---------|-------------|--------|---------------------------------|
| **S2-HARD-01** | Bump `RF_PROTOCOL_VERSION` đồng bộ; không deploy gateway mới cho node cũ chưa update trong cùng window | [ ] Pending | `grep -n "RF_PROTOCOL_VERSION" include/config.h` → `0x02`; không có hardcode `0x02` trong logic check version |
| **S2-HARD-02** | Fail-closed decode: mọi đường nhận có thể trả `CRC_MISMATCH` **và phải drop frame**; không bao giờ vào `handleIncomingFrame` | [ ] Pending | Test D2 (flip 1 bit) + review call-site `recordCrcError()` |
| **S2-HARD-03** | Bound & null: `calculateCrc16` giữ null-guard → `0`, không deref; `decodeFrameDetailed` kiểm tra `frame_len` tối thiểu trước khi đọc CRC cuối | [ ] Pending | Unit test `(nullptr,0)==0xFFFF` / `(nullptr,10)==0`; review guard độ dài tại B2 |
| **S2-HARD-04** | ATmega8: kiểm tra Flash/SRAM qua `post:scripts/check_atmega8_size.py`; `core/Crc16Modbus.*` phải nằm trong `build_src_filter` của env `atmega8-node` | [ ] Pending | Output script size check của `pio run -e atmega8-node-4` |
| **S2-HARD-05** | Không đổi wire-layout: thứ tự `[crc_lo][crc_hi]` little-endian ở cuối frame bắt buộc giữ nguyên | [ ] Pending | Test roundtrip `appendCrc16Modbus` ↔ `readU16Le` |
| **S2-HARD-06** | Storage checksum cô lập: `treatment_manager` **phải** dùng helper riêng, không gọi chung codec RF | [ ] Pending | `grep -n "calculateCrc16" include/treatment_manager.h` → **không được** có; phải thấy `calculateStorageCrc16` |

---

## Regression Watch (must not worsen — carried from Sprint 1)

| Suite | Baseline (`41feec6`, pre-Sprint-1) | Sprint 1 | Sprint 2 Requirement |
|-------|-----------------------------------|----------|----------------------|
| `test_crc16` | n/a (new) | 9/9 PASS | Giữ 9/9 PASS sau A1 (delegate không được phá vỡ) |
| `test_fsm` | 21/21 PASS | 21/21 PASS | Giữ 21/21 — không liên quan CRC, bất kỳ fail nào = regression |
| `test_production` | 97 failed / 104 succeeded | 97 failed / 104 succeeded | **Không được tăng** số fail. Các fail CCITT cũ do D1 sửa được là **cải thiện**, không phải regression |
| `pio test -e native` (bare) | 0 test cases collected | 0 test cases collected | ✅ **Fixed in `434c80c`** (newline-separated `test_filter`); now **315/315 PASS**. Gate on the bare command; never the CLI comma form |

---

## Sprint 2 Acceptance Criteria

- [x] `RfFrameCodec::calculateCrc16("123456789") == 0x4B37`
- [x] `encodeFrame(SET_PUMP)` giữ CRC ở 2 byte cuối; HMAC tag 16 byte trước đó không đổi
- [x] `decodeFrameDetailed` trả `CRC_MISMATCH` khi flip 1 bit dữ liệu / CRC đuôi sai
- [x] `RF_PROTOCOL_VERSION == 0x02`
- [x] `treatment_manager` dùng helper storage riêng, NVS checksum cũ vẫn verify được
- [x] `pio test -e native` (bare) → **PASS 315/315**
- [x] `pio run -e esp32-s3-devkitc-1` → PASS
- [x] `pio run -e atmega8-node-4` → PASS + `check_atmega8_size.py` trong ngưỡng (6358/7000 flash, 301/900 RAM)
- [ ] Tất cả 6 Quality Gates `S2-HARD-01..06` có bằng chứng kiểm chứng
- [ ] Zero regression trên `test_fsm` và `test_production` (xem Regression Watch)
- [ ] Task A1–A2, B1–B2, C1–C2, D1–D2, E1–E2 đều đạt `[x] Done` sau vòng QA review độc lập

---

**Last Updated:** 2026-09-27 10:15:00 (Asia/Ho_Chi_Minh) / 2026-09-27T03:15:00Z
**Current Phase:** Software implementation and automated release gates complete in the working tree; DEF-01 (test-harness false green) is now resolved. AGU legacy SCI is now migrated to CRC16-Modbus per the authoritative Delphi `TSCI.SendComCRC16`/`CalCRC16` and AVR assembly evidence (init `0xFFFF`, reflected polynomial `0xA001`, `[length=payloadLen+2][payload][CRC_LO][CRC_HI]`); golden vectors `04 06 09 F3 A7` and `04 07 09 F2 37` are asserted, 11-byte burst responses are validated fail-closed, and old one-byte zero-sum frames are rejected. Sequential firmware gates pass: bare `pio test -e native` runs 4 suites / **315 test cases, 315 PASS** (`test_crc16` 9, `test_fsm` 21, `test_production` 278, `test_rf_address` 7), ESP32-S3 build pass, and ATmega8 flash/RAM gate pass at 6358/7000 flash and 301/900 RAM. Backend unit tests pass 42 suites/368 tests, backend REST e2e passes 82/82, backend build passes; UI type-check, test suite 48/48, and production build pass. Raw logic-analyzer/UART captures from the deployed node, live DB migration execution, independent QA sign-off, deployment/rollback bench evidence, and field observability remain open; do not call the release green until those gates are completed.

### Mandatory Execution Order (không được đảo)

```
C2 ──► A1 ──► C1 ──► A2 ──► B1 ──► B2 ──► D1 ──► D2 ──► E1 ──► E2
│       │       │
│       │       └── bump version SAU khi algorithm đổi, nhưng TRƯỚC khi bất kỳ frame wire nào được regenerate
│       └────────── algorithm flip — GÂY BREAK nếu C2 chưa tách storage
└────────────────── CÁCH LY storage trước: nếu chạy A1 khi C2 chưa xong,
                    treatment_manager sẽ verify NVS cũ bằng Modbus → FAIL → hỏng snapshot
```

**Ràng buộc commit:** `C2` **bắt buộc** hoàn thành trước `A1`, hoặc `C2 + A1` phải nằm trong **cùng một commit**. Giao commit giữa chừng = snapshot NVS hỏng giữa phiên. `C1` (bump `0x02`) đi ngay sau `A1` để không tồn tại trạng thái "CRC mới nhưng version cũ" trên wire.
