# ESP32 RF CRC16 — Implementation Execution Plan

## 1. Mục tiêu và quyết định đã chốt

Hoàn tất migration chuỗi RF modern ESP32 ↔ ATmega8 sang CRC16-Modbus, xác minh legacy AGU bằng thiết bị thật trước khi thay đổi codec, mở rộng modern node address lên `0x01..0x0F`, bổ sung mô hình **Dynamic Control Slot** trên Dashboard, rồi đóng gói release/rollout có rollback.

Các quyết định không được thay đổi âm thầm trong lúc triển khai:

- **Modern RF CRC:** CRC16-Modbus, init `0xFFFF`, polynomial `0xA001`, reflected LSB-first, trailer little-endian `[crc_lo][crc_hi]`.
- **Frame layout:** giữ nguyên; CRC bao phủ `[header + payload + HMAC]`, không bao phủ 2 byte CRC. `HMAC_TAG_SIZE` thực tế là **16 byte**, không phải 12.
- **Protocol version:** modern RF dùng `RF_PROTOCOL_VERSION = 0x02`; firmware lệch version bị reject fail-closed.
- **Storage boundary:** treatment/NVS checksum tiếp tục CCITT-FALSE riêng trong `treatment_manager`; không migrate storage trong đợt này.
- **Modern address:** mở rộng node vật lý từ `0x04..0x07` lên `0x01..0x0F`; gateway là `0x00`.
- **Modern group command:** không serialize group address vào modern `target_node_id`; group command tiếp tục fan-out unicast từng node để giữ ACK/observability theo node.
- **Logical group mapping:** backend/MQTT/database vẫn dùng `group_id 1..4`; mapping ở boundary là `groupID = 0x10 | (nodeID & 0x0C)`. Ví dụ node `0x04..0x07` thuộc RF group `0x14`. Không dùng công thức `nodeID × 0xF8` hoặc `(nodeID - 1) & 0x0C`.
- **Legacy AGU:** giữ giới hạn node `0x04..0x07`; không suy diễn group semantics legacy sang modern codec. Chỉ đổi zero-sum sang CRC16 nếu capture wire thật chứng minh an toàn.
- **Dynamic Control Slot:** thay 4 `GroupCard` + 4 `NodeCard` hardcoded bằng đúng 4 placeholder điều khiển; mỗi slot chọn rõ `NODE` (`1..15`) hoặc `GROUP` (`1..4`).
- **Hai luồng điều khiển:** `NODE` dùng direct unicast 1-1; `GROUP` dùng backend fan-out thành N lệnh node riêng, mỗi lệnh vẫn là RF unicast. Không suy luận loại target chỉ từ việc `group_id` có/không có.
- **Slot persistence:** cấu hình slot lưu backend/DB theo `device_id`, không chỉ localStorage; cấm trùng cùng một target giữa các slot.
- **Slot telemetry:** slot `NODE` hiển thị telemetry node hiện có; slot `GROUP` hiển thị group status hiện có (phase/countdown/members/treatment), không tạo pipeline aggregate mới.
- **Commissioning gate:** dropdown vẫn liệt kê Node `1..15`, nhưng node chưa commissioning/offline phải bị disable điều khiển và hiển thị lý do.

## 2. Baseline đã xác minh và điểm cần sửa trong kế hoạch cũ

- Sprint 1 đã có `include/core/Crc16Modbus.h`, `src/core/Crc16Modbus.cpp` và 9 vector native PASS; cần QA độc lập trước khi đóng gate.
- Sprint 2 đã sửa codec, version, storage boundary, fixture và build filter; các task A1–E2 đang ở `QA Review`, chưa được coi là hoàn tất.
- Gateway build trước đó bị blocker ngoài CRC tại `src/uart_rf_transport.cpp`; log hiện ghi đã sửa tối thiểu bằng ESP-IDF UART low-level API. QA phải xác nhận diff này không làm thay đổi semantics ISR.
- `test_production.cpp` còn fixture version cứng, fixture dùng node `0x01/0x02`, và test unsupported-version hiện dùng `0x02` dù `0x02` là version hợp lệ. Đây là test debt phải triage, không được gán nhầm là lỗi CRC.
- `platformio.ini` đang dùng multiline `test_filter`; claim cũ về “comma-as-glob” không khớp nội dung hiện tại. Sprint baseline phải đo thực tế bằng PlatformIO trước khi sửa cấu hình; không dùng lại claim cũ nếu output không chứng minh.
- `AGU-Aeroponics/TestSCI.dpr:340` là dòng comment minh họa group `$14`, còn lệnh thực thi tại 342–362 là unicast. Vì vậy group ACK/broadcast vẫn là câu hỏi thực nghiệm, chưa phải acceptance fact.
- Không chạy các lệnh PlatformIO song song trên cùng environment/build directory; log trước đã ghi nhận race `ar: unity.o` khi chạy đồng thời.

## 3. Dependency graph và execution order

```text
Freeze baseline/tag + Sprint 2 QA
             │
             ├── Baseline harness triage (song song, không sửa production CRC)
             │
             ├── Modern node address expansion 0x01..0x0F
             │       │
             │       ├── Dynamic Control Slot (backend/DB + Dashboard)
             │       └── Modern integration + fuzz + benchmark
             │
             └── Sprint 3A real-wire characterization
                         │
              ┌──────────┼──────────┐
              │          │          │
          Zero-sum    CRC16       Unknown
          freeze      migrate     BLOCK
              │          │          │
              └──────┬───┴──────────┘
                     v
              Docs + release gate
                     v
              Bench + field rollout
```

**Hard ordering:**

1. Không flash/deploy giữa trạng thái CRC mới nhưng version cũ và trạng thái mixed-fleet.
2. Sprint 2 chỉ được sign-off sau khi QA kiểm tra storage boundary, CRC/HMAC ordering, trailer layout, gateway build và ATmega8 size gate.
3. Address expansion phải có predicate/test trước khi đổi các vòng lặp, mảng trạng thái hoặc control-plane validation.
4. Sprint 3B bị khóa cho tới khi có raw capture và kết luận S3A rõ ràng.
5. Release gate chạy tuần tự từng environment, lưu log đầy đủ.
6. Dynamic Control Slot chỉ bật điều khiển sau khi control plane đã thống nhất node range `1..15`; không để UI mới gọi nhầm legacy range `4..7`.

## 4. Phase A — Freeze baseline và QA độc lập Sprint 2

### A1. Freeze và evidence

- Ghi commit/tag rollback anchor trước mọi chỉnh sửa mới; nếu tag `pre-crc16-modbus` chưa tồn tại thì tạo ở commit đúng trước migration, không tag nhầm HEAD đã có CRC mới.
- Lưu baseline gồm: toolchain, số test collected, pass/fail, SIGSEGV location, gateway build, ATmega8 flash/RAM.
- Đối chiếu `git diff` để tách rõ thay đổi CRC, storage isolation và UART compile fix; UART fix nên là commit độc lập nếu lịch sử hiện tại cho phép.

### A2. QA checklist Sprint 2

Kiểm tra các file:

- `aeroponics-firmware/src/rf_frame_codec.cpp`
- `aeroponics-firmware/include/rf_frame_codec.h`
- `aeroponics-firmware/src/pump_node_controller.cpp`
- `aeroponics-firmware/include/pump_node_controller.h`
- `aeroponics-firmware/include/config.h`
- `aeroponics-firmware/include/treatment_manager.h`
- `aeroponics-firmware/test/test_production/test_production.cpp`
- `aeroponics-firmware/platformio.ini`

Acceptance:

- `RfFrameCodec::calculateCrc16("123456789") == 0x4B37` và chỉ delegate tới `core/Crc16Modbus.h`.
- CRC được kiểm tra trước HMAC; lỗi CRC trả `CRC_MISMATCH`/drop, không vào business handler.
- `frame_len >= 2` và `crc_check_len + 2 == frame_len` trước `readU16Le`.
- `RF_PROTOCOL_VERSION` chỉ có một nguồn tại `config.h`, không hardcode trong parser.
- `TreatmentSnapshot::computeChecksum()` không gọi RF codec và vẫn dùng CCITT-FALSE.
- HMAC fixture/test dùng 16 byte; mọi doc/test ghi 12 byte phải đưa vào correction backlog.
- `test_crc16` 9/9, `test_fsm` 21/21, ATmega8 size gate trong ngưỡng; `test_production` phải có classification, không được tuyên bố “all green” khi còn SIGSEGV baseline.

## 5. Phase B — Baseline harness và test triage

### B1. Xác minh `test_filter` bằng đo thực tế

Chạy tuần tự:

```text
pio test -e native
pio test -e native -f test_crc16
pio test -e native -f test_fsm
pio test -e native -f test_production
```

Ghi số test collected và exit code. Nếu bare command không thu thập đúng ba suite, sửa `platformio.ini` bằng cú pháp PlatformIO đã xác minh; không dùng lại claim “comma bug” nếu output không chứng minh.

### B2. Phân loại `test_production`

Mỗi failure phải vào đúng một nhóm:

1. CRC vector/fixture cũ (`0x29B1`, `0x1021`, CCITT).
2. Protocol version hardcode (`0x01`/`0x02`), đặc biệt vector tại vùng test S2.
3. Topology/address mismatch do fixture node `0x01/0x02` trong khi production guard cũ là `0x04..0x07`.
4. SIGSEGV/pre-existing defect.
5. Failure khác, phải có owner và reproduction command.

Chỉ sửa nhóm 1–3 trong phạm vi migration/address decision. Không sửa hoặc che crash nhóm 4 bằng cách skip test; phải ghi stack/region làm release limitation nếu chưa xử lý.

## 6. Phase C — Modern address expansion lên `0x01..0x0F`

Đây là quyết định mới của plan, thực hiện sau khi Sprint 2 CRC QA đã ổn định để không trộn lỗi algorithm với lỗi topology.

### C1. Domain constants và predicates

Trong `aeroponics-firmware/include/config.h`:

- Đặt `RF_MAX_NODE_ID = 0x0F`, `RF_PRODUCTION_MIN_NODE_ID = 0x01`, `RF_PRODUCTION_MAX_NODE_ID = 0x0F`.
- Giữ `RF_GATEWAY_NODE_ID = 0x00` và `AGU_LEGACY_MIN/MAX_NODE_ID = 4..7`.
- Tách rõ predicates: `isValidNodeId`, `isValidSourceAddress`, `isValidTargetAddress`, `isAguLegacyNodeId`.
- Modern source chỉ là gateway hoặc node `0x01..0x0F`; group address không được làm source.
- Modern target trong đợt này chỉ là gateway/node `0x00..0x0F`; reject `0x10/0x14/0x18/0x1C` ở wire modern.

### C2. Propagate qua các boundary

Rà soát và cập nhật có chủ đích tất cả call-site của `isProductionNodeId` và các loop theo `RF_PRODUCTION_MIN/MAX`, tối thiểu:

- `rf_frame_codec.cpp` — metadata validation và decode validation.
- `pump_node_controller.cpp/.h` — anti-replay, pending commands, incoming ACK/telemetry, fan-out.
- `node_registry.cpp/.h` — node index, stale bitmask, registry capacity.
- `main.cpp`, `node_fsm.cpp`, `flow_fault_evaluator.cpp`, `telemetry_analytics.cpp`, `flow_calibration.cpp`.
- `mqtt_client.cpp/.h` — command validation/range reporting; vẫn giữ logical `group_id 1..4`.
- `test/fakes/NodeSimulatorHarness.h` và các fixture production.

Không nới các guard AGU legacy trong `agu_legacy_rf_host.h/.cpp` hoặc các đường `isAguLegacyNodeId`.
- Backend/DB range propagation cho REST, MQTT, telemetry và persistence được thực hiện trong Phase I; không sửa riêng một DTO rồi để schema/router vẫn khóa `4..7`.

### C3. Group fan-out boundary

- Giữ `queueExternalGroupCommand` fan-out unicast và ACK riêng từng node.
- Map logical group `1..4` sang thành viên node ở registry/control plane; không serialize RF group target vào modern frame.
- Nếu cần utility mapping cho legacy/documentation, dùng công thức duy nhất `0x10 | (nodeID & 0x0C)` và test các block `1..3`, `4..7`, `8..B`, `C..F`.
- Không thay đổi schema backend/MQTT hoặc đổi `group_id` decimal thành hex.

### C4. Address acceptance tests

Thêm table-driven tests:

- Positive: mọi node `0x01..0x0F`, gateway `0x00` ở vị trí được phép.
- Negative: source group, target group modern, reserved/out-of-range `0x10`, `0xFF`, source == target.
- Round-trip encode/decode cho node `0x01`, `0x04`, `0x07`, `0x0F`.
- Mutate source/target address mà không regenerate CRC → `CRC_MISMATCH` hoặc reject validation theo đúng thứ tự parser.
- Regression AGU legacy: chỉ node `0x04..0x07` được host chấp nhận.

### C5. Backend topology consistency prerequisite

Phase C chỉ được coi là hoàn tất khi control plane cũng hiểu modern node `1..15`:

- Tách `MODERN_NODE_IDS (1..15)` khỏi `AGU_LEGACY_NODE_IDS (4..7)` trong `aeroponics-backend/src/node/node-topology.ts`.
- Rà soát DTO/service/router đang dùng `AGU_LEGACY_NODE_IDS`: pump command, group assignment, node status/history, flow telemetry, MQTT topic validation và RF discovery.
- Reconcile mâu thuẫn hiện tại: `database/schema.sql` khóa `4..7`, trong khi `database/001_production_domain_migration.sql` khóa `1..4`. Không sửa lịch sử migration đã chạy; tạo forward migration + rollback note cho range `1..15`.
- Sửa `RecordFlowEventDto`/service để cùng dùng range modern `1..15`; giữ `group_id` `1..4`.
- Giữ legacy AGU host/codec `4..7`; không dùng việc mở modern range để mở rộng lệnh legacy.

### C6. Commissioning path cho node `1..15`

- `POST /node/claim` hiện bị chặn cứng bằng `NODE_ID_FIXED` (`aeroponics-backend/src/node/node.service.ts:737-742`). Giữ nguyên việc cấm `SET_ID` trên thiết bị legacy.
- Bổ sung đường đăng ký cho modern node: RF discovery (`POST /node/scan`) trả physical ID, backend bind physical ID `1..15` vào một dòng `node_registry` của device.
- Nếu physical ID đã bị bind, từ chối bind lại và yêu cầu safe-off trước khi reassign; không tự động đổi identity của node đang chạy.
- Node chưa bind phải hiển thị rõ trạng thái “chưa commissioning” ở UI và bị chặn điều khiển.

## 7. Phase I — Dynamic Control Slot (Dashboard + control plane)

### I1. Mô hình slot và 2 luồng xử lý

Mỗi slot có đúng một target, khai báo tường minh bằng discriminator `target_type`:

```text
[Slot] ── target_type = NODE  ──►  POST /node/:nodeId/override   (1-1, RF unicast)
      └─ target_type = GROUP ──►  POST /group/:groupId/command   (1-N, fan-out thành N lệnh node)
```

Ràng buộc bắt buộc:

- Command DTO phải cấm đặt đồng thời `node_id` và `group_id`; loại target suy ra từ `target_type`, không suy ra từ việc field nào "có mặt".
- Bỏ `group_id: node.cachedGroupId` khỏi payload của lệnh node trong `PumpControl.tsx` và `NodeDetailModal.tsx`; hiện tại việc này khiến lệnh 1-1 bị định tuyến qua controller group và phụ thuộc group phải `ACTIVE`/đã gán.
- Lệnh group chỉ mang `group_id` + `action` + lease; không mang `node_id`.
- Nút điều khiển group phải có bước xác nhận trước `ON`; `OFF` không cần xác nhận.

### I2. Slot persistence (backend + DB)

- Bảng mới `control_slots`: `device_id`, `slot_index` (`1..4`), `target_type` (`NODE`/`GROUP`), `target_id`, `updated_at`, `updated_by`; khóa chính `(device_id, slot_index)`.
- Unique constraint trên `(device_id, target_type, target_id)` để chặn trùng target ở tầng DB; API trả `409` khi trùng, UI disable option đã dùng.
- Cho phép `target_type`/`target_id` null khi slot chưa gán; slot chưa gán không được gửi lệnh.
- Endpoint theo convention hiện có (`JwtAuthGuard`, device-scoped): `GET /control-slot` trả đủ 4 slot, `PUT /control-slot/:slotIndex` nhận `{ target_type, target_id }`; validate range (`NODE 1..15`, `GROUP 1..4`), reject `target_id = 0`.
- Migration: forward migration mới cho bảng `control_slots` + rollback note theo convention `database/`; không sửa `001_production_domain_migration.sql` đã chạy.
- Audit: ghi lại ai đổi slot nào, lúc nào, vì các lệnh này tác động bơm thật.
- Không đụng schema `timer_groups` / `group_node_assignments`; group membership vẫn do operator gán như hiện tại, không ràng buộc cứng theo block `0x10 | (nodeID & 0x0C)` (công thức đó chỉ là mapping legacy RF group address, không phải ràng buộc control plane).

### I3. Dashboard: thay 2 grid bằng 4 slot

- Thay `GroupGrid` + `NodeGrid` trong `aeroponics-ui/src/app/dashboard/page.tsx` bằng một `ControlSlotGrid` render đúng 4 `ControlSlotCard`.
- Mỗi slot có một `<select>` native (dự án không dùng UI component library) với `<optgroup>`:
  - `Node`: `01..15`, hiển thị trạng thái online/offline/chưa commissioning; option đã dùng ở slot khác bị disable.
  - `Nhóm`: `1..4`, hiển thị số thành viên active.
- Slot chưa gán: hiển thị empty state, không render nút điều khiển.
- Slot `NODE`: dùng lại presentational của `NodeCard` (staleness, outcome, override/schedule state, flow, litres) qua `useNodeStore(nodeId)`.
- Slot `GROUP`: dùng lại presentational của `GroupCard` (phase, countdown, members, treatment) qua `useGroupStore(groupId)`.
- Tách phần presentational khỏi `NodeGrid`/`GroupGrid` để không nhân bản markup telemetry; xác nhận trước các chỗ dùng khác ngoài dashboard.
- Giữ nguyên lease selector 15/30/60 giây cho slot `NODE`; slot `GROUP` áp `run_lease_ms` cho từng thành viên.
- Cập nhật responsive test hiện có (`aeroponics-ui/test/qa-integration.test.mjs:301-312`) theo cấu trúc mới; thêm test mới cho slot.

### I4. Tách 2 luồng telemetry

- Luồng node: `node_telemetry` / `pump_command_update` (keyed by `node_id`) chỉ cập nhật slot `NODE` tương ứng.
- Luồng group: `group_status` (keyed by `group_id`) chỉ cập nhật slot `GROUP` tương ứng.
- Không render telemetry aggregate của group vào slot node và ngược lại; không đổi attribution `node_id` trong DB.
- Kết quả lệnh group vẫn là per-node; slot group hiển thị tiến độ phản hồi theo từng thành viên nếu dữ liệu outcome sẵn có, nếu không thì không tạo aggregation mới.
- Node vừa là slot `NODE` vừa là thành viên group ở slot `GROUP` là tình huống hợp lệ; UI hiển thị nhãn "đang thuộc nhóm #k", và xác nhận bằng test rằng firmware vẫn serialize pending command theo từng node (`pending_commands_[node_id]`) nên không có hai lệnh trùng nhau trên cùng node.

### I5. Validation cho Dynamic Control Slot

- Backend unit: DTO range (`1..15` pass, `0`/`16`/`0x10` fail), duplicate target trả `409`, node override không yêu cầu group, group fan-out đúng số thành viên.
- DB migration test: constraint cho phép `1..15`, chặn target trùng, cho phép clear rồi gán lại cùng target.
- UI test: render đúng 4 slot; chọn `NODE` gọi endpoint node và payload không chứa `group_id`; chọn `GROUP` gọi endpoint group; option trùng bị disable; node offline disable nút điều khiển kèm lý do; `ON` group yêu cầu xác nhận.
- E2E: gán slot → gửi lệnh → nhận outcome qua WebSocket; đổi slot không làm mất cấu hình sau reload.
- Regression: `aeroponics-backend` `npm test`, `npm run test:e2e`, `aeroponics-ui` `npm test` và `npm run type-check` phải pass.

## 8. Phase D — Sprint 3A: Characterize legacy wire bằng thiết bị thật

### D1. Capture protocol

Dùng node ATmega8 legacy thật và gateway/RF transport thật. Chấp nhận hai nguồn capture, nhưng raw bytes phải lưu được và reproducible:

- Logic analyzer/sniffer trên UART/RF module; hoặc
- Hex dump ở gateway với timestamp, direction và frame boundary.

Capture tối thiểu 5 lần cho mỗi nhóm:

- `PUMP_ON`, `PUMP_OFF` node `0x04..0x07`.
- `PING` một node.
- `READ_RAM_BURST` 8 byte.
- Nếu an toàn, một thử nghiệm target `$14`; mặc định pump OFF trước/sau thử nghiệm.

Mỗi capture ghi baud, module, node, khoảng cách, sequence/attempt, nguồn capture và trạng thái actuator. Không flash firmware trong S3A.

### D2. Phân tích checksum

Với từng capture, kiểm tra độc lập:

- Zero-sum 1 byte trên envelope hiện tại.
- CRC16-Modbus trên `[length + payload]` với trailer `[lo][hi]`.
- Các candidate khác chỉ khi cả hai fail; không ép dữ liệu vào giả thuyết.

Kết luận phải là một trong ba trạng thái:

- **A — Zero-sum confirmed:** giữ `agu_legacy_codec` zero-sum; không migrate legacy chỉ vì modern RF đã đổi CRC.
- **B — CRC16-Modbus confirmed:** triển khai Sprint 3B với evidence đính kèm.
- **C — Unknown/third checksum:** block Sprint 3B, mở rộng capture/analysis.

### D3. ACK và response-size evidence

- Xác minh `$14` có thật sự làm node `4..7` hành động và có bao nhiêu ACK; dòng comment trong Delphi không đủ làm proof.
- Chọn policy mặc định **unicast fan-out** nếu không có ACK per-node đáng tin cậy.
- Đối chiếu `expectedResponseSize` thực tế cho PUMP, PING, READ burst, EEPROM/ID nếu các lệnh này nằm trong scope capture.
- Tạo `docs/legacy_wire_captures/` và `docs/LEGACY_WIRE_EVIDENCE.md` chỉ khi có capture thật.

### D4. Dual-mode safety harness

Nếu code dual-mode được thêm để test, mặc định production phải là zero-sum; cả hai nhánh phải có test encode/decode và ATmega8 modern build tuyệt đối không link `agu_legacy_codec.cpp`.

## 9. Phase E — Sprint 3B conditional legacy implementation

Chỉ thực hiện nhánh này khi S3A kết luận B.

- `agu_legacy_codec.h/.cpp`: dùng utility CRC chung, envelope `[len=payloadLen+2][payload][crc_lo][crc_hi]`, fail-closed bounds.
- `decodeBurstRam`: đổi contract sang 11 byte chỉ khi capture chứng minh response thật có layout đó.
- `agu_legacy_rf_host.h/.cpp`: cập nhật expected response và buffering theo capture, không theo giả định trong plan.
- Thêm golden vectors sinh từ utility, không gõ tay CRC mới.
- Cập nhật `test_prototype` hoặc tạo suite legacy codec riêng; file hiện tại chủ yếu là ScheduleManager test nên không coi nó là coverage CRC nếu chưa thêm test codec.
- Nếu S3A kết luận A, chỉ đóng băng zero-sum, cập nhật wire evidence và bỏ qua migration code; đây là kết quả hợp lệ, không phải task thất bại.

## 10. Phase F — Modern integration, fuzz và benchmark

### F1. End-to-end modern RF

Kiểm tra:

- `encodeFrame` gateway → decode node với node IDs đại diện `0x01`, `0x07`, `0x0F`.
- COMMAND_ACK/TELEMETRY node → gateway.
- CRC sai → `CRC_MISMATCH`, drop trước HMAC/business dispatch.
- CRC đúng nhưng HMAC sai → `HMAC_AUTH_FAIL`, không side effect.
- Group logical command → unicast fan-out, ACK/error theo từng member.

### F2. Deterministic fuzz

- Ít nhất 2.500 frame với seed cố định và seed được log.
- Mutate header, address, payload, HMAC và trailer riêng biệt.
- Không frame hỏng nào được trả `OK` hoặc kích hoạt pump; có positive control frame hợp lệ.

### F3. Benchmark/resource

- Native và ESP32-S3: throughput/latency CRC trên frame thật, clean path `CRC_ERRORS == 0`.
- ATmega8: build + flash/SRAM gate; không dùng LUT/heap trong CRC hot path.
- Ghi kết quả và toolchain vào walkthrough log, không đặt số benchmark mẫu như số thật.

## 11. Phase G — Docs và release gate

Đồng bộ tối thiểu:

- `docs/RF_PROTOCOL.md`
- `docs/interface-wire-contract.md`
- `docs/QA_ACCEPTANCE_REPORT_4_NODES.md`
- `.ai/planning/esp32-rf-crc16/README.md`, `PROGRESS.md`, `WALKTHROUGH_LOG.md`

Mọi tài liệu modern phải ghi HMAC **16 byte**, CRC Modbus và version `0x02`. CCITT chỉ còn được mô tả ở storage checksum/legacy history nơi cần thiết.

Release gate chạy tuần tự:

```text
pio test -e native
pio test -e native-prototype
pio run -e native-integration
pio run -e esp32-s3-devkitc-1
pio run -e atmega8-node-4
```

Bare `pio test -e native` chạy đủ 4 suite (`test_crc16` 9, `test_fsm` 21, `test_production` 278, `test_rf_address` 7) = **315/315**; **không** dùng dạng CLI comma `-f a,b,c` (parse thành 1 glob → 0 test, exit 0). `native-integration` là **build** target (real-Mosquitto harness), không có test suite — phải chạy `pio run -e native-integration`, không phải `pio test`.

Sau khi address expansion được tích hợp, bổ sung build/test đại diện node ID `0x01` và `0x0F` nếu platform profile hỗ trợ; nếu không, chạy native codec table tests và ghi rõ giới hạn hardware evidence.

Control-plane/UI gate bổ sung (chạy tuần tự sau firmware gate):

```text
cd aeroponics-backend && npm test
cd aeroponics-backend && npm run test:e2e
cd aeroponics-ui && npm test
cd aeroponics-ui && npm run type-check
```

Không gọi release “green” nếu:

- test harness collect 0;
- `test_production` SIGSEGV chưa được phân loại;
- có regression mới ngoài baseline;
- gateway/node build lệch version hoặc ATmega8 size gate fail;
- S3A chưa có evidence nhưng S3B đã đổi legacy wire.
- slot command còn gửi `node_id` và `group_id` cùng lúc, hoặc backend/DB còn khóa node range khác `1..15` cho modern path.

## 12. Phase H — Rollout và rollback

### H1. Pre-deploy

- Build và lưu binary v0x01/CCITT từ rollback anchor offline.
- Build binary v0x02/Modbus gateway và node; kiểm tra version trong boot log.
- Bench test đủ node thật: PUMP OFF → bounded PUMP ON lease → OFF, PING/heartbeat, corrupted-frame injection, reboot giữa command.
- Backup NVS; xác nhận storage checksum cũ vẫn đọc được.

### H2. Maintenance window

1. Flash gateway và toàn bộ modern nodes trong cùng window.
2. Verify ACK từng node `0x01..0x0F` được triển khai; không chấp nhận mixed version.
3. Verify logical group fan-out không tạo group broadcast giả và có outcome per-node.
4. Nếu bất kỳ node nào không ACK hoặc pump state bất thường: emergency safe-off và rollback gateway về v0x01.

### H3. Observability

Theo dõi trong 24–48 giờ:

- CRC error rate theo 5 phút; mục tiêu `<0.1%`.
- stale event/last-seen gap theo node.
- telemetry volume so với baseline.
- timeout/retry và pump outcome.

Rollback không sửa NVS checksum vì storage CRC đã được cô lập; chỉ rollback binary/protocol pair. Chỉ tạo completion tag sau khi deployment report có evidence và sign-off của Firmware/QA/DevOps.

## 13. Quality gates cuối cùng

- **Q1:** Sprint 2 QA approved; utility 9/9, FSM 21/21, build/size gate pass.
- **Q2:** baseline harness có test collected thực tế; mọi `test_production` failure được phân loại.
- **Q3:** modern node `0x01..0x0F` validation và fan-out tests pass; AGU legacy vẫn `0x04..0x07`.
- **Q4:** raw legacy captures tồn tại; S3A checksum/ACK/response-size conclusion được review.
- **Q5:** Sprint 3B chỉ chạy theo branch A/B/C của evidence; không giả định broadcast.
- **Q6:** integration + deterministic fuzz ≥2.500 + benchmark clean path pass.
- **Q7:** docs khớp wire thực tế, HMAC 16 byte, version 0x02, rollback artifact có thể boot.
- **Q8:** bench pass, rollout đồng bộ, CRC error rate dưới ngưỡng và deployment report hoàn chỉnh.
- **Q9:** control plane thống nhất node range `1..15`; DTO, service, MQTT router và DB constraint cùng nhận giá trị này.
- **Q10:** 4 slot persist theo `device_id`; duplicate target bị chặn ở cả API và DB.
- **Q11:** slot `NODE` chỉ gọi unicast endpoint; slot `GROUP` chỉ gọi group endpoint; không còn payload trộn hai loại target.
- **Q12:** hai luồng telemetry tách biệt; slot node không hiển thị dữ liệu group và ngược lại; UI/backend tests pass.

## 14. Deliverables

- QA evidence Sprint 2 và baseline classification report.
- Modern address predicates, registry/control-plane propagation, node `0x01..0x0F` tests.
- `docs/legacy_wire_captures/` và `docs/LEGACY_WIRE_EVIDENCE.md` từ thiết bị thật.
- Legacy codec migration hoặc quyết định giữ zero-sum có evidence.
- Fuzz/benchmark logs, updated wire-contract docs, release checklist.
- Offline rollback binaries, bench/field deployment report và completion tag.
- Control plane node range `1..15` (DTO/service/router/DB migration + commissioning path).
- `control_slots` migration, slot API, và Dashboard `ControlSlotGrid` thay 2 grid cũ với 2 luồng điều khiển/telemetry tách biệt.
