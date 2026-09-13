### [2026-09-13 16:55] - Track S3-F: PumpCommand Module (S3-F1, S3-F2, S3-F3, S3-F4)
* **Trạng thái:** `[ ] QA Review` (Sẵn sàng kiểm toán độc lập)
* **Hạng mục đã hoàn thành:**
  - **S3-F1:** Triển khai `PumpCommandService.sendCommand` (`pump-command.service.ts`):
    - Khởi tạo command ID dạng UUID v4 duy nhất (`crypto.randomUUID()`).
    - Quản lý `rf_seq` tăng đơn điệu nghiêm ngặt (monotonic per node) trong session, cách ly sau reboot.
    - Gửi lệnh qua MQTT topic `aeroponics/command/node/{nodeId}/override` tuân thủ 100% schema Sprint 2 (`command_id`, `version: 1`, `desired_state`, `source`, `run_lease_ms`, `rf_seq`, `node_id`, `group_id`, `treatment_version_id`).
    - Lưu bản ghi `PumpCommand` với trạng thái ban đầu `outcome: PENDING`.
  - **S3-F2:** Xử lý State Machine Lifecycle & Deadman Timer:
    - Triển khai `handleRfAck`: chuyển trạng thái sang `RF_ACKED` (nếu acked=true) hoặc `FAULT_NO_ACK` (nếu acked=false), ghi nhận `command_to_ack_latency_ms`.
    - Triển khai `handlePumpFeedback`: lưu bản ghi audit `PumpFeedbackEvent` và cập nhật `feedback_at`.
    - Triển khai `handleFlowConfirmed`: bảo vệ chốt chặn bất biến (Strict Invariant) - cấm tuyệt đối chuyển outcome sang `FLOW_CONFIRMED` nếu command chưa ở trạng thái `RF_ACKED` (ném `BadRequestException`). Khi hợp lệ, cập nhật `flow_confirmed_at`, đo `flow_start_latency_ms` và lưu bản ghi `FlowEvent`.
    - Triển khai `handleFault`: cập nhật `fault_reason` và chuyển outcome sang các mã lỗi `FAULT_*`.
    - Cơ chế Deadman Timer (`onModuleDestroy`): khi backend shutdown/disconnect, tự động cập nhật 100% các lệnh đang `PENDING` sang outcome `FAULT_BACKEND_DISCONNECT`, giải phóng timer/lease an toàn.
  - **S3-F3:** Động cơ Anti-Replay Sliding Window (`checkAndRecordSequence`):
    - Từ chối các gói tin/command lặp `rf_seq` cho cùng node trong khung thời gian chống phát lại.
    - Cấu hình linh hoạt qua biến môi trường `MQTT_ANTIREPLAY_WINDOW_MS` (mặc định 60000ms), không hardcode.
    - Ghi log warning với `node_id` và `rf_seq`, tuyệt đối không actuate hay thay đổi database khi phát hiện frame lặp. Tự động dọn dẹp bộ nhớ sliding window.
  - **S3-F4:** Triển khai `PumpCommandController` (`pump-command.controller.ts`) và DTOs:
    - Bảo vệ 100% REST endpoints bằng `JwtAuthGuard`, từ chối unauthenticated request với HTTP 401.
    - Endpoint `POST /api/group/:groupId/command`: Bắt buộc kiểm tra nhóm có active assignment (`GroupService.getGroupStatus`), trả lỗi `409 Conflict` (`ConflictException`) nếu nhóm ở trạng thái `UNASSIGNED` hoặc không active. Hỗ trợ gửi lệnh tới đích danh node được chỉ định hoặc fan-out tới toàn bộ node active của nhóm.
    - Endpoint `GET /api/node/:nodeId/commands`: Phân trang lịch sử lệnh theo `limit` (default 50, max 200, min 1) và `offset` (min 0). Trả `400 Bad Request` nếu `limit` không hợp lệ.
    - Validation `class-validator`: `SendPumpCommandDto` (`action` ON/OFF, `run_lease_ms` [1000..300000], `node_id` [1..4]), `ListNodeCommandsDto` (`limit` [1..200], `offset` >= 0).
  - **Module Wiring:** Tạo `PumpCommandModule` kết nối `TypeOrmModule.forFeature([PumpCommand, PumpFeedbackEvent, PumpStateEvent, FlowEvent, SensorCalibration])`, `SeasonModule`, `GroupModule`, `MqttModule`, `AuthModule`, và đăng ký vào `AppModule`.
* **Files đã sửa / tạo:**
  - `[MODIFIED]` `aeroponics-backend/src/pump-command/entities/pump_command.entity.ts`
  - `[MODIFIED]` `aeroponics-backend/src/pump-command/entities/pump_command.entity.spec.ts`
  - `[NEW]` `aeroponics-backend/src/pump-command/dto/send-pump-command.dto.ts`
  - `[NEW]` `aeroponics-backend/src/pump-command/dto/list-node-commands.dto.ts`
  - `[NEW]` `aeroponics-backend/src/pump-command/dto/pump-command.dto.spec.ts`
  - `[NEW]` `aeroponics-backend/src/pump-command/events/pump-command.events.ts`
  - `[NEW]` `aeroponics-backend/src/pump-command/pump-command.service.ts`
  - `[NEW]` `aeroponics-backend/src/pump-command/pump-command.service.spec.ts`
  - `[NEW]` `aeroponics-backend/src/pump-command/pump-command.controller.ts`
  - `[NEW]` `aeroponics-backend/src/pump-command/pump-command.controller.spec.ts`
  - `[NEW]` `aeroponics-backend/src/pump-command/pump-command.module.ts`
  - `[MODIFIED]` `aeroponics-backend/src/app.module.ts`
* **Kết quả kiểm thử:**
  - `cd aeroponics-backend && npm test`: **205/205 tests PASSED** (27 test suites, 0 failed)
  - `cd aeroponics-backend && npm run build`: **SUCCESS** (0 errors)
  - `cd aeroponics-backend && npm run lint`: **SUCCESS** (0 errors, 0 warnings)
  - Invariant Verification:
    - Monotonic `rf_seq` per node: VERIFIED (Consecutive calls produce strictly increasing sequence)
    - State Machine Sequence & Invariant (RF_ACKED -> PUMP_FEEDBACK_ON -> FLOW_CONFIRMED): VERIFIED (Flow confirmation rejected without RF_ACKED with 400 Bad Request)
    - Deadman cancel on module destroy: VERIFIED (All PENDING commands receive FAULT_BACKEND_DISCONNECT)
    - Anti-replay rejection in 60s window: VERIFIED (Duplicate rf_seq dropped and warning logged)
    - Group UNASSIGNED command rejection: VERIFIED (409 Conflict thrown)
    - JWT Guard on all endpoints: VERIFIED (100% endpoints protected)

---

### [2026-09-13 14:25] - Track S3-E: Group & Node Module (S3-E1, S3-E2, S3-E3)
* **Trạng thái:** `[ ] QA Review` (Sẵn sàng kiểm toán độc lập)
* **Hạng mục đã hoàn thành:**
  - **S3-E1:** Triển khai `GroupService` (`group.service.ts`):
    - Đầy đủ nghiệp vụ quản lý nhóm timer: `assignTreatmentVersion`, `unassign`, `getGroupStatus`, `getAllGroupsStatus`.
    - Bảo vệ tính toàn vẹn 100%: bắt buộc treatment version ở trạng thái `PUBLISHED` (từ chối DRAFT/ARCHIVED với `BadRequestException`), yêu cầu có active season.
    - Chống xung đột node liên nhóm (Cross-group conflict prevention): từ chối gán nếu bất kỳ node nào đang active ở group khác trong cùng season với mã lỗi tường minh `ConflictException` (409).
    - Tính toán pha Ngày/Đêm (`DAY`/`NIGHT`) và mốc chuyển giao tiếp theo (`next_transition_at`) chuẩn xác theo múi giờ Việt Nam `Asia/Ho_Chi_Minh` bằng thư viện `luxon` (Ngày: 06:00–18:00 ICT, Đêm: 18:00–06:00 ICT; kiểm chứng chính xác tại các mốc biên 06:00:00 vs 05:59:59.999 ICT).
    - Transaction nguyên tử: đóng assignment cũ (`unassigned_at = NOW(), active = false`), cập nhật `NodeRegistry.cached_group_id`, và đồng bộ trạng thái `TimerGroupStatus.ACTIVE` / `UNASSIGNED`.
    - Decoupling qua `EventEmitter2`: phát sinh sự kiện `group.assigned` và `group.unassigned`.
  - **S3-E2:** Triển khai `NodeService` (`node.service.ts`):
    - Quản lý vòng đời và trạng thái trạm: `register`, `updateHealth`, `resetFault`, `handleTelemetry`, `checkStaleness`, `getNodeStatus`, `getAllNodesStatus`, `updateCalibration`.
    - Khóa chốt lỗi nghiêm ngặt (Fault Latching Invariant): cấm tuyệt đối chuyển trạng thái từ `FAULT` → `OK` từ telemetry thông thường nếu không có lệnh `explicitReset` (ném `BadRequestException`).
    - Lệnh reset lỗi chủ đích `resetFault` (hoặc `POST /api/node/:id/fault-reset`) phục vụ người vận hành, phát sinh sự kiện `node.fault_reset`.
    - Giám sát ngắt kết nối (Staleness Detection): nạp cấu hình `STALE_THRESHOLD_MS` (120000ms), tự động phát hiện node vượt ngưỡng và bắn cảnh báo `staleness_alert` (`NodeStalenessAlertEvent`) qua event emitter / WebSocket.
    - Cập nhật hiệu chuẩn cảm biến lưu lượng (Sensor Calibration): lưu lịch sử audit trail với số phiên bản tăng dần `version_num`, đánh dấu bản ghi cũ `SUPERSEDED`, cập nhật `NodeRegistry.active_sensor_calibration_id` và trạng thái `CALIBRATED`.
  - **S3-E3:** Triển khai `GroupController` (`group.controller.ts`), `NodeController` (`node.controller.ts`) và DTOs:
    - Bảo vệ 100% REST endpoints bằng `JwtAuthGuard` (`@UseGuards(JwtAuthGuard)`), từ chối unauthenticated request với HTTP 401.
    - Endpoints Group: `GET /api/group` (200), `GET /api/group/:id` (200), `PUT /api/group/:id/assign` (200), `DELETE /api/group/:id/assign` (200).
    - Endpoints Node: `GET /api/node` (200), `GET /api/node/:id` (200), `PUT /api/node/:id/calibration` (200), `POST /api/node/:id/fault-reset` (200).
    - Validation `class-validator`: `AssignGroupDto` (node_ids array 1..4 unique, treatment_version_id min 1), `UpdateNodeCalibrationDto` (kiểm tra chặt chẽ `pulses_per_litre > 0` và `< 10000`).
  - **Database Migration:** Tạo `1726200002000-AddGroupAssignmentUniqueIndexes.ts` bổ sung partial unique index `uq_group_node_active_per_season` và `uq_group_treatment_active_per_season` bảo vệ chống race condition ở tầng database.
* **Files đã sửa / tạo:**
  - `[NEW]` `aeroponics-backend/src/database/migrations/1726200002000-AddGroupAssignmentUniqueIndexes.ts`
  - `[NEW]` `aeroponics-backend/src/group/group.types.ts`
  - `[NEW]` `aeroponics-backend/src/group/events/group.events.ts`
  - `[NEW]` `aeroponics-backend/src/group/dto/assign-group.dto.ts`
  - `[NEW]` `aeroponics-backend/src/group/group.service.ts`
  - `[NEW]` `aeroponics-backend/src/group/group.controller.ts`
  - `[NEW]` `aeroponics-backend/src/group/group.module.ts`
  - `[NEW]` `aeroponics-backend/src/node/events/node.events.ts`
  - `[NEW]` `aeroponics-backend/src/node/dto/update-node-calibration.dto.ts`
  - `[NEW]` `aeroponics-backend/src/node/dto/node-telemetry.dto.ts`
  - `[NEW]` `aeroponics-backend/src/node/node.service.ts`
  - `[NEW]` `aeroponics-backend/src/node/node.controller.ts`
  - `[NEW]` `aeroponics-backend/src/node/node.module.ts`
  - `[MODIFIED]` `aeroponics-backend/src/app.module.ts`
  - `[MODIFIED]` `aeroponics-backend/package.json`
  - `[TEST-ADDED]` `aeroponics-backend/src/group/dto/assign-group.dto.spec.ts`
  - `[TEST-ADDED]` `aeroponics-backend/src/group/group.service.spec.ts`
  - `[TEST-ADDED]` `aeroponics-backend/src/group/group.controller.spec.ts`
  - `[TEST-ADDED]` `aeroponics-backend/src/node/dto/update-node-calibration.dto.spec.ts`
  - `[TEST-ADDED]` `aeroponics-backend/src/node/node.service.spec.ts`
  - `[TEST-ADDED]` `aeroponics-backend/src/node/node.controller.spec.ts`
* **Kết quả kiểm thử:**
  - `cd aeroponics-backend && npm test`: **174/174 tests PASSED** (24 test suites, 0 failed)
  - `cd aeroponics-backend && npm run build`: **SUCCESS** (0 errors)
  - `cd aeroponics-backend && npm run lint`: **SUCCESS** (0 errors, 0 warnings)
  - Invariant Verification:
    - Single active group assignment per node: VERIFIED (ConflictException thrown on active overlap)
    - Fault latching (FAULT -> OK rejected without reset): VERIFIED (BadRequestException thrown)
    - Staleness detection (> 120s): VERIFIED (Emits staleness_alert event)
    - Luxon ICT Day/Night boundary (06:00:00 vs 05:59:59.999): VERIFIED
    - Calibration boundaries (0 < pulses < 10000): VERIFIED
    - JWT Guard on all endpoints: VERIFIED (100% endpoints covered)

---

### [2026-09-13 14:15] - Track S3-D: Treatment Module (S3-D1, S3-D2)
* **Trạng thái:** `[ ] QA Review` (Sẵn sàng kiểm toán độc lập)
* **Hạng mục đã hoàn thành:**
  - **S3-D1:** Triển khai `TreatmentService` (`treatment.service.ts`):
    - Hoàn chỉnh nghiệp vụ quản lý công thức khí canh: `create`, `addVersion`, `publishVersion`, `clone`, `archive`, `getById`, `list`.
    - Bảo vệ tính bất biến 100% (Immutability): chặn tuyệt đối republish/sửa đổi trên các version đã `PUBLISHED` (ném `ConflictException` 409 khi republish, ném `BadRequestException` khi publish version `ARCHIVED`), kết hợp cơ chế trigger PostgreSQL `trg_treatment_version_immutable`.
    - Ngăn ngừa race condition: bọc các thao tác `publishVersion` và `addVersion` trong transaction với `pessimistic_write` lock.
    - An toàn khi nhân bản (Draft Clone Isolation): method `clone` nhân bản toàn bộ thông số nhưng **ép buộc 100% các version mới về trạng thái `DRAFT` và `published_at = null`**, triệt tiêu nguy cơ kích hoạt ngoài ý muốn trên actuator/node.
    - Decoupling qua `EventEmitter2`: phát sinh sự kiện `treatment.created`, `treatment.version.created`, `treatment.version.published`, `treatment.cloned`, `treatment.archived` cho downstream modules (Group assignment, MQTT gateway sync).
  - **S3-D2:** Triển khai `TreatmentController` (`treatment.controller.ts`) và DTOs:
    - Bảo vệ 100% REST endpoints bằng `JwtAuthGuard` (`@UseGuards(JwtAuthGuard)`), từ chối unauthenticated request với HTTP 401.
    - Đầy đủ 7 endpoints REST: `POST /api/treatment` (201), `GET /api/treatment` (200), `GET /api/treatment/:id` (200), `POST /api/treatment/:id/version` (201), `PUT /api/treatment/:id/version/:versionId/publish` (200), `POST /api/treatment/:id/clone` (201), `PUT /api/treatment/:id/archive` (200).
    - Validation fail-fast bằng `class-validator` / `class-transformer`: `CreateTreatmentDto` (name trim, max 100; optional initial params), `CreateTreatmentVersionDto` (kiểm tra chặt chẽ `TREATMENT_BOUNDS`: `spray_day_s` [5..300], `cooldown_day_s` [30..7200], `spray_night_s` [5..300], `cooldown_night_s` [30..7200]), `CloneTreatmentDto` (name trim, max 100), `ListTreatmentDto` (is_archived boolean parsing, limit 1..100, offset >= 0).
  - **Module Wiring:** Tạo `TreatmentModule` (`treatment.module.ts`) kết nối `TypeOrmModule.forFeature([Treatment, TreatmentVersion])`, `AuthModule`, export `TreatmentService`, và đăng ký vào `AppModule`.
* **Files đã sửa / tạo:**
  - `[NEW]` `aeroponics-backend/src/treatment/dto/create-treatment.dto.ts`
  - `[NEW]` `aeroponics-backend/src/treatment/dto/create-treatment-version.dto.ts`
  - `[NEW]` `aeroponics-backend/src/treatment/dto/clone-treatment.dto.ts`
  - `[NEW]` `aeroponics-backend/src/treatment/dto/list-treatment.dto.ts`
  - `[NEW]` `aeroponics-backend/src/treatment/events/treatment.events.ts`
  - `[NEW]` `aeroponics-backend/src/treatment/treatment.service.ts`
  - `[NEW]` `aeroponics-backend/src/treatment/treatment.controller.ts`
  - `[NEW]` `aeroponics-backend/src/treatment/treatment.module.ts`
  - `[MODIFIED]` `aeroponics-backend/src/app.module.ts`
  - `[TEST-ADDED]` `aeroponics-backend/src/treatment/dto/treatment.dto.spec.ts`
  - `[TEST-ADDED]` `aeroponics-backend/src/treatment/treatment.service.spec.ts`
  - `[TEST-ADDED]` `aeroponics-backend/src/treatment/treatment.controller.spec.ts`
* **Kết quả kiểm thử:**
  - `cd aeroponics-backend && npm test`: **134/134 tests PASSED** (18 test suites, 0 failed)
  - `cd aeroponics-backend && npm run build`: **SUCCESS** (0 errors)
  - `cd aeroponics-backend && npm run lint`: **SUCCESS** (0 errors, 0 warnings)
  - Invariant Verification:
    - Immutability & Republish Rejection: VERIFIED (409 Conflict thrown upon republishing)
    - Concurrency Lock: VERIFIED (Pessimistic write lock applied during publish)
    - Cloned Versions Draft Isolation: VERIFIED (Cloned versions force status = DRAFT and published_at = null)
    - Industrial Boundary Checks: VERIFIED ([5..300] and [30..7200] boundaries enforced)
    - JWT Auth Guard on all endpoints: VERIFIED (Reflect metadata check & AuthGuard)

---

### [2026-09-13 14:10] - Track S3-C: Season Module (S3-C1, S3-C2)
* **Trạng thái:** `[ ] QA Review` (Sẵn sàng kiểm toán độc lập)
* **Hạng mục đã hoàn thành:**
  - **S3-C1:** Triển khai `SeasonService` (`season.service.ts`):
    - Đảm bảo trọn vẹn vòng đời vụ mùa: `create`, `getActive`, `getById`, `list`, `endSeason`.
    - Bảo vệ tính toàn vẹn 100%: bắt và chuyển đổi lỗi PostgreSQL partial unique index code `'23505'` (`uq_seasons_one_active`) thành `ConflictException` (HTTP 409), triệt tiêu hoàn toàn rủi ro race condition khi có 2 request tạo đồng thời.
    - Transaction an toàn (`dataSource.transaction`) và locking (`pessimistic_write`) khi `endSeason`: cập nhật `ended_at = NOW()`, đổi `status = 'ENDED'`, từ chối đóng lại vụ mùa đã đóng (`BadRequestException`).
    - Kiến trúc hướng sự kiện (Event-driven decoupling) qua `EventEmitter2`: phát sinh sự kiện `season.created` (`SeasonCreatedEvent`) và `season.ended` (`SeasonEndedEvent`) cho các module downstream (như Tuya snapshot, Group assignment cleanup) xử lý độc lập.
  - **S3-C2:** Triển khai `SeasonController` (`season.controller.ts`) và DTOs:
    - Bảo vệ 100% REST endpoints bằng `JwtAuthGuard` (`@UseGuards(JwtAuthGuard)`), từ chối unauthenticated request với HTTP 401.
    - Đầy đủ các endpoints REST: `POST /api/season` (201 Created), `GET /api/season/active` (200 OK trả active season hoặc null), `GET /api/season/:id` (200 OK), `PUT /api/season/:id/end` (200 OK), `GET /api/season` (200 OK với phân trang và lọc).
    - Validation fail-fast bằng `class-validator` / `class-transformer`: `CreateSeasonDto` (name required, trim whitespace, max 100; notes max 1000), `EndSeasonDto` (notes optional max 1000), `ListSeasonDto` (status enum, limit 1..100, offset >= 0).
  - **Module Wiring:** Tạo `SeasonModule` (`season.module.ts`) kết nối `TypeOrmModule.forFeature([Season])`, `AuthModule`, export `SeasonService`, và tích hợp vào `AppModule`.
* **Files đã sửa / tạo:**
  - `[NEW]` `aeroponics-backend/src/season/dto/create-season.dto.ts`
  - `[NEW]` `aeroponics-backend/src/season/dto/end-season.dto.ts`
  - `[NEW]` `aeroponics-backend/src/season/dto/list-season.dto.ts`
  - `[NEW]` `aeroponics-backend/src/season/events/season.events.ts`
  - `[NEW]` `aeroponics-backend/src/season/season.service.ts`
  - `[NEW]` `aeroponics-backend/src/season/season.controller.ts`
  - `[NEW]` `aeroponics-backend/src/season/season.module.ts`
  - `[MODIFIED]` `aeroponics-backend/src/app.module.ts`
  - `[TEST-ADDED]` `aeroponics-backend/src/season/season.service.spec.ts`
  - `[TEST-ADDED]` `aeroponics-backend/src/season/season.controller.spec.ts`
  - `[TEST-ADDED]` `aeroponics-backend/src/season/dto/season.dto.spec.ts`
* **Kết quả kiểm thử:**
  - `cd aeroponics-backend && npm test`: **83/83 tests PASSED** (15 test suites, 0 failed)
  - `cd aeroponics-backend && npm run build`: **SUCCESS** (0 errors)
  - `cd aeroponics-backend && npm run lint`: **SUCCESS** (0 errors, 0 warnings)
  - Lifecycle & Concurrency Invariants:
    - Single active season enforcement: VERIFIED (Application check & DB 23505 catch -> 409 Conflict)
    - Double-end prevention: VERIFIED (Throws 400 Bad Request)
    - JWT Guard on all endpoints: VERIFIED (Reflect metadata check & AuthGuard)

---

### [2026-09-13 14:05] - Track S3-B: TypeORM Entities & Migrations (S3-B1 -> S3-B6)
* **Trạng thái:** `[ ] QA Review` (Sẵn sàng kiểm toán độc lập)
* **Hạng mục đã hoàn thành:**
  - **S3-B1:** Entity `Season` (`season.entity.ts`) chuẩn hóa toàn bộ vòng đời mùa vụ với Partial Unique Index `uq_seasons_one_active` trên database (`WHERE status = 'ACTIVE'`) triệt tiêu 100% rủi ro tạo 2 active seasons đồng thời; relations `OneToMany` tới treatment assignments, node assignments, pump commands, flow events.
  - **S3-B2:** Entities `Treatment` (`treatment.entity.ts`) và `TreatmentVersion` (`treatment_version.entity.ts`) với versioning, enum `DRAFT` | `PUBLISHED` | `ARCHIVED`, hằng số bounds `TREATMENT_BOUNDS` (`spray_day_s` [5..300], `cooldown_day_s` [30..7200], `spray_night_s` [5..300], `cooldown_night_s` [30..7200]), bảo vệ immutability sau publish bằng database trigger `trg_treatment_version_immutable`.
  - **S3-B3:** Entities `TimerGroup` (`timer_group.entity.ts`), `GroupTreatmentAssignment` (`group_treatment_assignment.entity.ts`), `GroupNodeAssignment` (`group_node_assignment.entity.ts`), và `group_assignment.types.ts`: tuân thủ nghiêm ngặt chuẩn kiến trúc Task R5 (chuẩn hóa quan hệ có kiểm soát thời gian, không dùng mảng `node_ids` thô làm source of truth), enforce single active group per node qua DB partial unique index `(season_id, node_id) WHERE active AND effective_to IS NULL`.
  - **S3-B4:** Entity `NodeRegistry` (`node_registry.entity.ts`) và `SensorCalibration` (`sensor_calibration.entity.ts`): hỗ trợ 4 node MEGA8 độc lập với health status enum đầy đủ (`'OK' | 'STALE' | 'FAULT' | 'SAFE_OFF'`), độ chính xác cao `numeric(10,4)` cho `pulses_per_litre` chống sai số làm tròn khi đo lưu lượng sương khí canh.
  - **S3-B5:** Hypertable entities `PumpCommand` (`pump_command.entity.ts`), `PumpStateEvent` (`pump_state_event.entity.ts`), `PumpFeedbackEvent` (`pump_feedback_event.entity.ts`): partition theo `time`, cover toàn bộ 8 outcome states (`PENDING`, `RF_ACKED`, `FLOW_CONFIRMED`, `FAULT_NO_ACK`, `FAULT_NO_FLOW`, `FAULT_UNEXPECTED_FLOW`, `FAULT_SENSOR`, `TIMEOUT`), đo lường độ trễ mạng và phản hồi tải.
  - **S3-B6:** Hypertable entities `FlowEvent` (`flow_event.entity.ts`), `MeasurementReading` (`measurement_reading.entity.ts`), `TuyaMeasurementSession` (`tuya_measurement_session.entity.ts`), `Device` (`device.entity.ts`), `DeviceStatus` (`device_status.entity.ts`): Zero raw RF frame (Hard Rule S1.5-PARSE-11), `MeasurementReading` trigger type nghiêm ngặt chỉ chấp nhận `ON_DEMAND` | `END_OF_SEASON`, loại trừ 100% `SCHEDULED` polling (Hard Rule S3-TUYA-ON-DEMAND-04).
  - **Migration:** `1726200001000-EnhanceConstraintsAndEnums.ts` bổ sung `uq_seasons_one_active`, cập nhật `health_status` check constraint hỗ trợ `SAFE_OFF`, điều chỉnh precision `numeric(10,4)` và index duy nhất `(command_id, time)`.
* **Files đã sửa / tạo:**
  - `[NEW]` `aeroponics-backend/src/database/migrations/1726200001000-EnhanceConstraintsAndEnums.ts`
  - `[NEW]` `aeroponics-backend/src/season/entities/season.entity.ts`
  - `[NEW]` `aeroponics-backend/src/treatment/entities/treatment.entity.ts`
  - `[NEW]` `aeroponics-backend/src/treatment/entities/treatment_version.entity.ts`
  - `[NEW]` `aeroponics-backend/src/group/entities/timer_group.entity.ts`
  - `[NEW]` `aeroponics-backend/src/group/entities/group_treatment_assignment.entity.ts`
  - `[NEW]` `aeroponics-backend/src/group/entities/group_node_assignment.entity.ts`
  - `[NEW]` `aeroponics-backend/src/group/entities/group_assignment.types.ts`
  - `[NEW]` `aeroponics-backend/src/node/entities/node_registry.entity.ts`
  - `[NEW]` `aeroponics-backend/src/node/entities/sensor_calibration.entity.ts`
  - `[NEW]` `aeroponics-backend/src/pump-command/entities/pump_command.entity.ts`
  - `[NEW]` `aeroponics-backend/src/pump-command/entities/pump_state_event.entity.ts`
  - `[NEW]` `aeroponics-backend/src/pump-command/entities/pump_feedback_event.entity.ts`
  - `[NEW]` `aeroponics-backend/src/flow/entities/flow_event.entity.ts`
  - `[NEW]` `aeroponics-backend/src/tuya-bridge/entities/tuya_measurement_session.entity.ts`
  - `[NEW]` `aeroponics-backend/src/tuya-bridge/entities/measurement_reading.entity.ts`
  - `[NEW]` `aeroponics-backend/src/device/entities/device.entity.ts`
  - `[NEW]` `aeroponics-backend/src/device/entities/device_status.entity.ts`
  - `[TEST-ADDED]` `aeroponics-backend/src/season/entities/season.entity.spec.ts`
  - `[TEST-ADDED]` `aeroponics-backend/src/treatment/entities/treatment.entity.spec.ts`
  - `[TEST-ADDED]` `aeroponics-backend/src/group/entities/group_assignment.entity.spec.ts`
  - `[TEST-ADDED]` `aeroponics-backend/src/node/entities/node_registry.entity.spec.ts`
  - `[TEST-ADDED]` `aeroponics-backend/src/pump-command/entities/pump_command.entity.spec.ts`
  - `[TEST-ADDED]` `aeroponics-backend/src/flow/entities/flow_event.entity.spec.ts`
  - `[TEST-ADDED]` `aeroponics-backend/src/tuya-bridge/entities/measurement_reading.entity.spec.ts`
* **Kết quả kiểm thử:**
  - `cd aeroponics-backend && npm test`: **47/47 tests PASSED** (12 test suites, 0 failed)
  - `cd aeroponics-backend && npm run build`: **SUCCESS** (0 errors)
  - `cd aeroponics-backend && npm run lint`: **SUCCESS** (0 errors, 0 warnings)
  - Hard Rules verification:
    - S1.5-PARSE-11 (Zero raw RF frame): VERIFIED
    - S3-TUYA-ON-DEMAND-04 (No scheduled Tuya polling): VERIFIED
    - S3-DB-03 (Zero schema synchronize): VERIFIED

---

### [2026-09-13 14:00] - Track S3-A: Boilerplate & Infrastructure Setup (S3-A1, S3-A2, S3-A3)
* **Trạng thái:** `[ ] QA Review` (Sẵn sàng kiểm toán độc lập)
* **Hạng mục đã hoàn thành:**
  - **S3-A1:** Bootstrap NestJS project `aeroponics-backend/`: hoàn thiện `AppConfigModule` với validation fail-fast biến môi trường công nghiệp, loại trừ triệt để 100% InfluxDB (`rg '@influxdata'` và `rg 'InfluxModule'` = 0 match), `AuthModule` đầy đủ Passport JWT strategy, `JwtAuthGuard` với `@Public()` bypass, timing-safe credential validation cho `POST /api/auth/login`.
  - **S3-A2:** `DatabaseModule` + TypeORM config với `synchronize: false` (Hard Rule S3-DB-03), connection pooling (max: 20, idleTimeout: 30s), migration tự động `migrationsRun: true`, `data-source.ts` cho CLI, initial migration `1726200000000-InitialBaselineMigration.ts` khởi tạo toàn bộ schema & trigger immutable của Sprint 2, TimescaleDB port 5432 cách ly tuyệt đối trong mạng nội bộ Docker `aero_net` (0 port mapping ra 0.0.0.0).
  - **S3-A3:** `MqttModule` & `MqttService`: tự động kết nối và subscribe các topic contracts Sprint 2 & 3 (`aeroponics/device/+/status`, `aeroponics/device/+/telemetry`, `aeroponics/device/+/command/+/ack`, `aeroponics/device/+/safety/audit`, `aeroponics/telemetry/node/+/snapshot`, `aeroponics/telemetry/node/+/event`, `aeroponics/ack/+`), `onMessage` bọc try/catch toàn diện (Hard Rule S3-MQTT-05) ngăn chặn 100% unhandled exception/crash khi nhận malformed JSON hoặc payload dị dạng, định tuyến sự kiện qua EventEmitter2.
* **Files đã sửa / tạo:**
  - `[MODIFIED]` `aeroponics-backend/package.json`
  - `[MODIFIED]` `aeroponics-backend/src/app.module.ts`
  - `[MODIFIED]` `aeroponics-backend/src/config/env.validation.ts`
  - `[MODIFIED]` `aeroponics-backend/src/database/database.module.ts`
  - `[NEW]` `aeroponics-backend/eslint.config.mjs`
  - `[NEW]` `aeroponics-backend/src/auth/public.decorator.ts`
  - `[NEW]` `aeroponics-backend/src/auth/dto/login.dto.ts`
  - `[NEW]` `aeroponics-backend/src/auth/jwt.strategy.ts`
  - `[NEW]` `aeroponics-backend/src/auth/jwt-auth.guard.ts`
  - `[NEW]` `aeroponics-backend/src/auth/auth.service.ts`
  - `[NEW]` `aeroponics-backend/src/auth/auth.controller.ts`
  - `[NEW]` `aeroponics-backend/src/auth/auth.module.ts`
  - `[NEW]` `aeroponics-backend/src/database/data-source.ts`
  - `[NEW]` `aeroponics-backend/src/database/migrations/1726200000000-InitialBaselineMigration.ts`
  - `[NEW]` `aeroponics-backend/src/mqtt/mqtt.constants.ts`
  - `[NEW]` `aeroponics-backend/src/mqtt/mqtt.service.ts`
  - `[NEW]` `aeroponics-backend/src/mqtt/mqtt.module.ts`
  - `[TEST-ADDED]` `aeroponics-backend/src/config/env.validation.spec.ts`
  - `[TEST-ADDED]` `aeroponics-backend/src/auth/auth.service.spec.ts`
  - `[TEST-ADDED]` `aeroponics-backend/src/auth/jwt-auth.guard.spec.ts`
  - `[TEST-ADDED]` `aeroponics-backend/src/database/database.module.spec.ts`
  - `[TEST-ADDED]` `aeroponics-backend/src/mqtt/mqtt.service.spec.ts`
* **Kết quả kiểm thử:**
  - `cd aeroponics-backend && npm test`: **21/21 tests PASSED** (5 test suites, 0 failed)
  - `cd aeroponics-backend && npm run build`: **SUCCESS** (0 errors)
  - `cd aeroponics-backend && npm run lint`: **SUCCESS** (0 errors, 0 warnings)
  - `docker compose config`: TimescaleDB 5432 internal only (0 port mapped to 0.0.0.0)
  - Clean Architecture checks: 0 InfluxDB references, 0 legacy relay references.

---

### [2026-09-12 18:45] - Track S2-D: MQTT Production Integration (S2-D1, S2-D2, S2-D3, S2-D4)
* **Trạng thái:** `[ ] QA Review` (Sẵn sàng kiểm toán độc lập)
* **Hạng mục đã hoàn thành:**
  - **S2-D1:** MQTT client & ACL to gateway/group/node domain: LWT QoS 1 retained offline (`aeroponics/device/{gw_id}/status` -> `{"status":"offline"}`), bounded reconnect (`MQTT_MAX_RECONNECT_RETRIES = 5`, exponential backoff $\le 60$s), credentials an toàn nạp từ NVS/env (zero plain-text token/password trong production codebase), least-privilege ACL isolation (`%u` topic mapping).
  - **S2-D2:** Inbound config/override routing: DTO/JSON validation toàn diện, sliding-window `CommandDeduplicationCache` (64 slots, 60s TTL) ngăn chặn replay attack / network duplicated frames, monotonic version enforcement chặt chẽ (`_last_treatment_version`, `_last_assignment_version`, `_last_policy_version`), từ chối stale config với reject ACK tường minh; 0 direct GPIO / 0 direct RF call từ MQTT callback (chuyển giao qua bounded FreeRTOS queue `_inbound_commands`).
  - **S2-D3:** Heartbeat, normalized telemetry snapshots và append-only lifecycle events: state machine `QUEUED` -> `RF_ACKED` -> `COMPLETED` chỉ xác nhận sau khi có RF ACK thực tế và flow evaluation kết luận; 0 false completion khi publish MQTT; bounded buffer (64 slots) kèm drop counter và queue overflow audit, retry bounded không block scheduler loop.
  - **S2-D4:** Mosquitto integration verification & test coverage: toàn diện 248 native tests (`test_s2_d1` .. `test_s2_d4`) và live integration gate `scripts/mqtt_integration_gate.py` tương tác trực tiếp với Mosquitto container (`aero_mosquitto`): LWT retained verification, ACL denial check, full command lifecycle & admission/outcome ACK, out-of-range flow policy rejection.
* **Files đã sửa / tạo:**
  - `[MODIFIED]` `aeroponics-firmware/include/mqtt_task_policy.h`
  - `[MODIFIED]` `aeroponics-firmware/include/mqtt_client.h`
  - `[MODIFIED]` `aeroponics-firmware/src/mqtt_client.cpp`
  - `[MODIFIED]` `aeroponics-firmware/include/group_scheduler.h`
  - `[MODIFIED]` `aeroponics-firmware/src/group_scheduler.cpp`
  - `[MODIFIED]` `aeroponics-firmware/src/main.cpp`
  - `[MODIFIED]` `aeroponics-firmware/include/cstdio`
  - `[MODIFIED]` `aeroponics-firmware/platformio.ini`
  - `[MODIFIED]` `aeroponics-firmware/src/integration/ProductionPubSubClient.cpp`
  - `[MODIFIED]` `aeroponics-firmware/src/integration/production_mqtt_gate.cpp`
  - `[MODIFIED]` `scripts/mqtt_integration_gate.py`
  - `[MODIFIED]` `aeroponics-firmware/test/test_production/test_production.cpp`
* **Kết quả kiểm thử:**
  - `pio test -e native`: **248/248 test cases PASSED** (0 failed)
  - `pio run -e esp32-s3-devkitc-1`: **SUCCESS** (RAM: 23.8%, Flash: 21.8%)
  - `pio run -e atmega8-node`: **SUCCESS** (RAM: 301/900B [29.4%], Flash: 6388/7000B [81.8%])
  - `bash scripts/verify_production_clean_architecture.sh`: **PASS** (Zero legacy relay code)
  - `bash scripts/verify_no_test_psk_in_production.sh`: **PASS** (Zero plaintext credentials in production)
  - `python3 scripts/mqtt_integration_gate.py`: **ALL PRODUCTION MQTT INTEGRATION GATES PASSED**
    1. LWT Retained Offline Gate: PASS
    2. Heartbeat & Telemetry Snapshot Gate: PASS
    3. Assignment Command Lifecycle & RF ACK Gate: PASS
    4. Treatment Version Monotonic & Retained Ack Gate: PASS
    5. Invalid/Out-of-Range Policy Rejection Gate: PASS
    6. Mosquitto ACL Denial Security Gate: PASS
    7. Gateway Isolation Security Gate: PASS

---

### [2026-09-12 16:00] - Track S2-B: Treatment, Group & Dynamic Scheduler (S2-B1, S2-B2, S2-B3, S2-B4)
* **Trạng thái:** `[ ] QA Review` (Sẵn sàng kiểm toán độc lập)
* **Hạng mục đã hoàn thành:**
  - **S2-B1:** Production `TreatmentManager`: validation nghiêm ngặt status `PUBLISHED`, bounds `spray_day_s` [5..300], `cooldown_day_s` [30..7200], monotonic `config_version`, CRC-16 checksum; NVS persistence nguyên tử (write-then-verify) và rollback tự động/thủ công khi verify thất bại; 0 byte ghi flash trong scheduler/timer loop.
  - **S2-B2:** Versioned `GroupAssignment`: giới hạn chặt chẽ node IDs 1..4 và group IDs 0..4, monotonic `assignment_version`, kiểm tra single active group invariant (từ chối gán duplicate active group fail-closed), time-safe safe-OFF migration, phát sinh `AssignmentAuditEvent` có đầy đủ metadata (`assignment_version`, `effective_at`, `actor`, `node_id`, `old_group_id`, `new_group_id`, `reason`).
  - **S2-B3:** Production `GroupScheduler` (thay thế direct relay cũ): alias tương thích ngược `GroupScheduleManager`, nhóm 0 (`UNASSIGNED`) bảo đảm 0 lệnh RF ON/desired state ép OFF, tính toán Day/Night boundary chuẩn theo múi giờ `Asia/Ho_Chi_Minh` (UTC+7, lệch chuẩn +25200s, ngày 06:00–18:00 ICT, đêm 18:00–06:00 ICT) với chuyển đổi mượt mà giữa các cấu hình pha; clock invalid kích hoạt `forceSafeOff()` và trạng thái degraded/unassigned.
  - **S2-B4:** Manual Override & Group Pause/Resume Policy: override ON bắt buộc có lease và **bị từ chối tuyệt đối khi node ở FAULT/latched fault** (không thể bypass FSM an toàn); override OFF mang TTL để node tự resume schedule tại boundary mà không cần ESP32 gửi lệnh định kỳ; `pauseGroup` và `resumeGroup` đồng bộ trạng thái an toàn.
* **Files đã sửa / tạo:**
  - `[NEW]` `aeroponics-firmware/include/treatment_manager.h`
  - `[NEW]` `aeroponics-firmware/src/treatment_manager.cpp`
  - `[NEW]` `aeroponics-firmware/include/group_scheduler.h`
  - `[NEW]` `aeroponics-firmware/src/group_scheduler.cpp`
  - `[MODIFIED]` `aeroponics-firmware/include/group_schedule_manager.h` (forwarder/alias)
  - `[MODIFIED]` `aeroponics-firmware/src/group_schedule_manager.cpp`
  - `[TEST-ADDED]` `aeroponics-firmware/test/test_production/test_production.cpp` (`test_s2_b1`, `test_s2_b2`, `test_s2_b3`, `test_s2_b4`)
* **Kết quả kiểm thử:**
  - `cd aeroponics-firmware && ~/.platformio/penv/bin/pio test -e native`: **236/236 test cases PASSED** (0 failed, duration 1.50s)
  - `cd aeroponics-firmware && ~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`: **SUCCESS** (RAM 20.0%, Flash 21.6%)
  - `cd aeroponics-firmware && ~/.platformio/penv/bin/pio run -e atmega8-node`: **SUCCESS** (Flash 6388 / 7000B, RAM 301 / 900B)
  - `bash scripts/verify_production_clean_architecture.sh`: **PASS**
  - `git diff --check`: **PASS**

---

### [2026-09-12 15:52] - Track S2-A: Production RF Transport & Node Controller (S2-A1, S2-A2, S2-A3, S2-A4)
* **Trạng thái:** `[ ] QA Review` (Sẵn sàng kiểm toán độc lập)
* **Hạng mục đã hoàn thành:**
  - **S2-A1:** Production `IRfTransport` (`UartRfTransport`): UART1 riêng biệt (GPIO 18 RX, GPIO 17 TX, M0=15, M1=16, AUX=19, Baud 115200), không conflict USB debug Serial, non-blocking I/O, `crc_errors` / `dropped_bytes` / `tx_bytes` / `rx_bytes` counters, `isAuxReady()` & `setMode(m0, m1)`.
  - **S2-A2:** Production `RfFrameCodec`: `enum class ParseError` tường minh, fail-closed `decodeFrameDetailed` không cấp phát động, `DuplicateResponseCache` FIFO giới hạn 64 entry thread-safe (0 byte overhead trên ATmega8 qua guard `#if !defined(ATMEGA8_NODE_BUILD)`).
  - **S2-A3:** Production `PumpNodeController`: queue lệnh bounded, non-blocking retry engine (cấu hình `max_retries <= 3`, `retry_interval_ms = 1000ms`), idempotency qua tuple `{command_id, boot_session_id, sequence}`, an toàn hủy lệnh qua `cancelCommand(node_id, reason)` và deadman lease timeout.
  - **S2-A4:** Production `NodeRegistry`: giới hạn chặt chẽ 4 node (Node IDs 1..4 hợp lệ, 0 và 5..12 reject fail-closed), thread-safe mutex, configurable `stale_threshold_ms`, và phát hiện reboot qua `updateBootSession` + `NodeRebootCallback`.
* **Files đã sửa / tạo:**
  - `[NEW]` `aeroponics-firmware/include/pump_node_controller.h`
  - `[NEW]` `aeroponics-firmware/src/pump_node_controller.cpp`
  - `[MODIFIED]` `aeroponics-firmware/include/command_manager.h` (alias `using CommandManager = PumpNodeController;`)
  - `[MODIFIED]` `aeroponics-firmware/src/command_manager.cpp`
  - `[MODIFIED]` `aeroponics-firmware/include/rf_provisioning.h`
  - `[MODIFIED]` `aeroponics-firmware/include/uart_rf_transport.h`
  - `[MODIFIED]` `aeroponics-firmware/src/uart_rf_transport.cpp`
  - `[MODIFIED]` `aeroponics-firmware/include/rf_frame_codec.h`
  - `[MODIFIED]` `aeroponics-firmware/src/rf_frame_codec.cpp`
  - `[MODIFIED]` `aeroponics-firmware/include/node_registry.h`
  - `[MODIFIED]` `aeroponics-firmware/src/node_registry.cpp`
  - `[MODIFIED]` `aeroponics-firmware/src/main.cpp`
  - `[TEST-ADDED]` `aeroponics-firmware/test/test_production/test_production.cpp` (`test_s2_a1`, `test_s2_a2`, `test_s2_a3`, `test_s2_a4`)
* **Kết quả kiểm thử:**
  - `cd aeroponics-firmware && ~/.platformio/penv/bin/pio test -e native`: **232/232 test cases PASSED** (0 failed, duration 1.74s)
  - `cd aeroponics-firmware && ~/.platformio/penv/bin/pio run -e atmega8-node`: **SUCCESS** (Flash 6388 / 7000B, RAM 301 / 900B)
  - `cd aeroponics-firmware && ~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`: **SUCCESS** (0 errors)

---

### [2026-09-02 10:44] - Task R5-M: Re-validate schema/health-check theo scope 4 node và ownership MEGA8 (QA remediation)
* **Trạng thái:** `[x] Done` (Đã hoàn thành kiểm toán độc lập)
* **Lỗi QA đã nêu:** Health-check thất bại do database init dừng giữa chừng; rehearsal trước đó có fixture calibration literal (đã được sửa trong working tree trước remediation).
* **Files đã sửa:**
  - `[FIXED]` database/schema.sql (Dòng 148)
  - `[FIXED]` scripts/health-check.sh (Dòng 203)
* **Nguyên nhân gốc:** `node_registry.node_id` trong schema init thiếu dấu phẩy trước `display_name`, khiến PostgreSQL dừng init ở bảng thứ 8; named volume giữ lại database dở dang. Health-check constraint predicate khớp quá chặt với textual rendering của PostgreSQL (`trial_count >= 3`, `pulses_per_litre > 0`).
* **Giải pháp khắc phục:** Thêm dấu phẩy tối thiểu vào schema init; nới predicate health-check theo column contract thay vì phụ thuộc format số literal. Đã reset có kiểm soát volume disposable local `aero_timescale_data`; không xóa volume khác và không sửa migration fixture.
* **Kết quả tái kiểm thử:** PASS (`docker compose build`; `docker compose up -d`; containers healthy; `bash scripts/health-check.sh` 10/10; `bash scripts/rehearse_production_migration.sh` PASS; `cd aeroponics-firmware && ~/.platformio/penv/bin/pio test -e native` 228/228; `git diff --check`).

---

# Aeroponics Lean — Progress Tracker

> **Tracker hiện hành:** Cập nhật ngày **2026-08-22** theo xác nhận phần cứng/vận hành mới.
> - **Không triển khai tiếp đường 4 relay GPIO trực tiếp:** `RelayController`, `ScheduleManager`, MQTT relay command/topic, `relay_profiles`, `relay_events` và `sensor_readings` chỉ là **prototype rig / compatibility debt**, không phải production baseline.
> - **Sprint 1.5 đang ở pha Remediation S0–S1 bắt buộc:** thay thế hoặc cô lập code cũ trước khi viết POC RF + Flow. Không được xoá nền tảng còn tái sử dụng (boot-safe, NVS abstraction, RTC, WDT, FreeRTOS, Docker 3-service) khi chưa có successor và regression test.
> - **Sprint 2, 3, 4:** chỉ mở theo các gate đã nêu, với kiến trúc **ESP32-S3 RF Gateway + 4 MEGA8 Node + Season/Group/Flow Domain + Tuya On-demand**. Việc mở rộng quá 4 node là backlog, không thuộc scope hiện tại.

---

## 📌 Khái quát trạng thái dự án (Sprint Roadmap Summary)

| Sprint | Nội dung chính | Trạng thái | Tài liệu tham chiếu |
|---|---|---|---|
| **Sprint 0** | Hạ tầng Docker (TimescaleDB + Mosquitto + Backend) & schema | ✅ Done (Remediation hoàn tất) | [`sprint_0.md`](./sprint_0.md) |
| **Sprint 1** | Firmware Foundation (boot-safe, NVS, RTC, WDT, FreeRTOS) | ✅ Done (Nền tảng đã kiểm chứng, relay runtime thay thế xong) | [`sprint_1.md`](./sprint_1.md) |
| **Sprint 1.5** | Remediation S0–S1 → RF 433 MHz + Flow POC & Hardware Decision Gate | ✅ **DONE** — Track R/A/B/C/D PASS, 4-node acceptance evidenced | [`sprint_1_5.md`](./sprint_1_5.md) |
| **Sprint 2** | Firmware Production RF Gateway & 4 MEGA8 Node Control | 🚧 **ĐANG THỰC HIỆN** — Gate Sprint 1.5 đã PASS | [`sprint_2.md`](./sprint_2.md) |
| **Sprint 3** | NestJS Backend (Season + Group + Node + Flow + On-demand) | 🔵 Chờ Sprint 2 PASS | [`sprint_3.md`](./sprint_3.md) |
| **Sprint 4** | Single-file HTML Dashboard UI | 🔵 Chờ Sprint 3 PASS | [`sprint_4.md`](./sprint_4.md) |

---

## 📌 Thông tin vận hành Sprint 2

| Field | Value |
|---|---|
| **Thời gian cập nhật tracker** | 2026-09-12 (sau Sprint 1.5 PASS toàn bộ gate) |
| **Sprint hiện hành** | Sprint 2 — Production RF Gateway & 4 MEGA8 Node Control |
| **Agent thực thi (Execution Agent)** | Antigravity / Gemini |
| **Senior Solution Architect** | QA độc lập kiểm định trước khi chuyển bất kỳ task nào sang `[x] Done` |

---

## 🎯 Reference Plan & Document Hierarchy

- **Thư mục kế hoạch:** `.ai/planning/aeroponics-lean/`
- **Tài liệu kiến trúc ưu tiên cao nhất:** [`PROJECT_ALIGNMENT_2026-08-10.md`](./PROJECT_ALIGNMENT_2026-08-10.md)
- **Contract POC / Go-No-Go bắt buộc:** [`sprint_1_5.md`](./sprint_1_5.md)
- **Kế hoạch Backend Production:** [`sprint_3.md`](./sprint_3.md)
- **Kế hoạch Dashboard UI:** [`sprint_4.md`](./sprint_4.md)
- **Ngữ cảnh tổng quan:** [`README.md`](./README.md)

> **Quy tắc ưu tiên khi mâu thuẫn:** `PROJECT_ALIGNMENT_2026-08-10.md` → `sprint_1_5.md` → `sprint_2.md` → `sprint_3.md` / `sprint_4.md` → tài liệu Sprint 0–1 lịch sử. Code hiện hữu không tự trở thành SSOT.

---

## ✅ Architecture Baseline — xác nhận ngày 2026-08-22

Đây là yêu cầu người dùng đã xác nhận và có ưu tiên cao hơn các giả định cũ trong plan:

| Hạng mục | Baseline bắt buộc |
|---|---|
| Gateway | 01 ESP32-S3 làm gateway chủ; 01 RF transceiver 433 MHz nối với ESP32 qua UART riêng, tách USB/debug UART. |
| Remote nodes | Trước mắt 04 node, mỗi node là 01 ATmega8/MEGA8 có RF transceiver 433 MHz riêng; node ID `1..4`. |
| MEGA8 firmware | Có thể sửa firmware. MEGA8 tiếp tục tự chạy schedule/timer và điều khiển relay/pump độc lập. |
| ESP32 responsibility | Chỉ gửi lệnh ON/OFF thủ công hoặc override tạm thời, nhận telemetry đã parse, đánh giá ACK/feedback/flow và lưu/publish dữ liệu. ESP32 không trở thành scheduler định kỳ thay MEGA8. |
| OFF override | Lệnh OFF chỉ có hiệu lực tạm thời theo semantics được chốt ở protocol; sau chu kỳ/điểm resume của schedule, MEGA8 tự quay lại lịch. Không được dùng cơ chế này để vô tình xoá lịch trên node. |
| RF module | Chưa xác định model; phải discovery và decision gate trước khi khóa baud, pinout, mode, addressing, half-duplex/collision policy. |
| Existing protocol | Chưa có protocol hiện hữu; phải thiết kế/version protocol cho MEGA8 trước implementation. `COMMAND_ACK` production tuân thủ `RF_PROTOCOL.md`: HMAC + CRC + boot session + sequence + command ID; không echo payload. |
| Storage | Chỉ lưu dữ liệu đã parse/normalized; không lưu raw RF frame trong production database. Lỗi transport được lưu dưới dạng counters/events đã parse (CRC/MAC fail, timeout, retry, duplicate). |

### Data/analytics baseline

Tối thiểu lưu theo node: `desired_state`, `reported_state`, `driver_feedback`, `load_feedback` nếu có, `flow_lpm`, `pulse_count`, `delivered_volume`, `fault_flags`, `command_id`, ACK outcome, sequence/session, timestamps node/gateway, retry/timeout, heartbeat/stale và schedule/override source. Các chỉ số phân tích được bổ sung vào plan: command-to-ACK latency, flow-start latency, tỷ lệ `FLOW_CONFIRMED`, no-flow/unexpected-flow, runtime thực tế, delivered volume theo chu kỳ, flow stability, packet loss/retry, stale duration và schedule-vs-override mismatch.

## 📝 Addition Plan (Yêu cầu kỹ thuật bắt buộc cho Sprint 1.5 POC)

Các yêu cầu phát sinh dưới đây là **BLOCKER** cho Go/No-Go của Sprint 1.5; POC bắt đầu với 1 gateway + 1 node và phải có bài kiểm tra mở rộng tới 4 node trước khi đóng gate:

1. **Node-side lease/deadman:** `SET_PUMP(ON)` bắt buộc mang `run_lease_ms`/`max_on_duration_ms`; node boot/reset phải OFF trước UART/RF/application và tự force OFF khi lease hết hạn. Phải bench-test gateway mất nguồn lúc pump ON, có log `LEASE_EXPIRED_SAFE_OFF` và thời gian tắt đạt ngưỡng đã chốt.
2. **Wire protocol có thể liên thông:** tạo/version `docs/RF_PROTOCOL.md` **trước code codec** với SOF, version, endian, CRC-16 variant + test vectors, giới hạn buffer/payload/timeout, numeric enums, payload schemas, sequence wrap/reboot và correlation `command_id` hoặc `{boot_session_id, sequence}`.
3. **RF security:** CRC không phải authentication. Bắt buộc MAC/HMAC ứng dụng với key provisioned ngoài Git, anti-replay bằng boot-session/counter, policy provisioning/rotation/reset. Nếu chấp nhận lab air-gapped không MAC, ADR phải có risk acceptance và người phê duyệt; key không được có trong source, log hay evidence.
4. **FMEA và recovery semantics:** định nghĩa detection, node action, gateway action, escalation và fault-reset cho RF timeout, gateway/node reboot, no/unexpected flow, sensor lỗi, pump feedback mismatch, RTC invalid; chốt policy tắt node đơn lẻ hay dừng toàn group.
5. **Pump feedback được phân loại đúng:** tách `driver_feedback` khỏi `load_feedback`; hoặc ghi rõ giới hạn POC khi chỉ driver feedback + flow confirmation, hoặc định nghĩa current/aux-contact sensing (ngưỡng, debounce, open-load/stall/stuck-relay và isolation).
6. **Test plan trước bench:** version `docs/RF_FLOW_POC_TEST_PLAN.md` với acceptance threshold phê duyệt trước test, sample size, ON/OFF/retry/timeout, EMI khi pump switching, wet foliage/vật cản, RF configuration và breakdown latency.
7. **Calibration định lượng:** công thức, dải vận hành/multi-point, điều kiện thử, reference volume, repeatability/reject threshold và cấu hình theo sensor serial/node/version; xử lý overflow, noise/bounce, stale/zero/over-range flow.
8. **Heartbeat/staleness contract:** chốt telemetry ON/OFF/fault, heartbeat interval, stale threshold, boot-session recovery; test cắt nguồn node → gateway `STALE/RF_TIMEOUT`, và node online lại không tự resume ON nếu không có command/lease hợp lệ.
9. **Electrical/water/EMI safety:** đặc tính pump (DC/AC, nominal/inrush/stall), driver margin, flyback/snubber/MOV, fuse/TVS/reverse-polarity/isolation/connector rating, brownout/EMI/RF decoupling và emergency isolation phải được review/test.

---

## 📋 Quy ước trạng thái

| Status | Ý nghĩa |
|---|---|
| `[ ] Pending` | Chưa bắt đầu; không có source/evidence thực hiện. |
| `[ ] In Progress` | Gemini đang thực hiện task; chưa đủ evidence để review. |
| `[ ] QA Review` | Implementation/evidence đã hoàn tất, chờ Senior Solution Architect review độc lập. |
| `[x] Done` | Đã PASS toàn bộ gate áp dụng, evidence được ghi vào `WALKTHROUGH_LOG.md`, và được duyệt nghiêm ngặt. |
| `[!] Re-validate` | Đã từng PASS theo baseline cũ, evidence được giữ để kiểm toán, nhưng **không còn là bằng chứng đủ** cho baseline 2026-08-22. Phải re-scope/re-test theo task remediation tương ứng trước khi trở lại `[x] Done`. |

---

# 🚧 Track R — Remediation Sprint 0–1 (bắt buộc trước POC RF)

> **Mục tiêu:** Loại bỏ đường chạy production 4-relay cũ và đồng bộ hạ tầng với contract **4 MEGA8 node**, nhưng giữ các primitive nền tảng đã kiểm chứng. Các tham chiếu 12 node bên dưới là lịch sử/khả năng mở rộng, không phải acceptance scope hiện tại. Track này là công việc triển khai tiếp theo; không được đánh dấu Sprint 0 hoặc Sprint 1 là production-complete chỉ vì code prototype còn build được.

## Phân loại mã hiện hữu

| Nhóm | Quyết định | Thành phần đã xác minh |
|---|---|---|
| **Giữ và tái dùng** | Giữ abstraction/test phù hợp sau khi đổi consumer; chỉ refactor tối thiểu khi successor yêu cầu. | `NvsStorage`/NVS backend, `RtcManager`, `ESPTaskWatchdog`, `FreeRTOSTaskRunner`, `IClock`, `IWatchdog`, Docker 3-service, backend `/health`. |
| **Thay thế bắt buộc** | Viết successor theo RF Gateway/Node; chỉ xoá runtime path cũ sau khi successor build/test PASS. | `RelayController`, `IRelayOutput`, `ScheduleManager`, `RelayProfile`/profile repository relay, MQTT relay command handler, Serial override relay, task-per-relay, relay topic ACL. |
| **Loại khỏi production contract** | Không tạo mới dependency; migration/archive có kiểm soát nếu DB đã khởi tạo. | `relay_profiles`, `relay_events`, `sensor_readings`, kiểm tra health-check 5 bảng/2 hypertable cũ, `TUYA_POLL_INTERVAL_MS=10000`. |

## Kế hoạch thực thi theo thứ tự

| Task ID | Công việc | Status | Done khi |
|---|---|---|---|
| **R1** | Lập inventory dependency và migration plan cho toàn bộ runtime 4 relay; version `docs/SPRINT_0_1_LEGACY_REPLACEMENT.md`. | [x] Done | Map source/test/topic/schema/script cũ → successor Sprint 1.5/2/3; xác định thứ tự remove, rollback và acceptance test. Không xoá code chỉ vì không dùng. |
| **R2** | Tách firmware composition root: boot-safe output phải ở **node actuator**, gateway không khởi tạo relay GPIO hay 4 relay task. | [x] Done | `main.cpp` gateway không include/construct `RelayController`/`ScheduleManager`; có `IRfTransport` seam và RF UART tách USB debug. Primitive NVS/RTC/WDT/FreeRTOS vẫn build/test. |
| **R3** | Thay relay scheduler/profile/override bằng contract gateway–MEGA8 node; tách ownership schedule khỏi ESP32. | [!] Re-validate | Evidence group–node cũ được giữ, nhưng không đủ cho baseline mới. Re-validate phải chứng minh ESP32 không chạy scheduler định kỳ; MEGA8 node `1..4` giữ schedule/timer; gateway chỉ phát temporary ON/OFF override; OFF override không xoá schedule và MEGA8 resume đúng semantics. `docs/RF_PROTOCOL.md` phải được kiểm tra lại với khả năng thực thi trên MEGA8. |
| **R4** | Đồng bộ MQTT/Mosquitto/config từ relay domain sang gateway/group/node domain. | [x] Done | ACL và firmware/backend topic contract theo Sprint 2; command có `command_id`, version và ACK outcome RF; không direct GPIO từ MQTT callback. Wi-Fi/MQTT/RF keys không tracked. |
| **R5** | Hoàn tất migration schema và operational scripts cho production domain. | [x] Done | Schema/migration có `seasons`, treatment/version, timer group, node/group assignment lịch sử, command/state/feedback/flow/calibration và Tuya measurement session; health-check kiểm tra đúng contract mới. Không dùng `node_ids` array hoặc `node_registry.group_id` làm source of truth mapping lịch sử; Tuya chỉ on-demand/end-of-season, không default poll 10 giây. |
| **R6** | Gỡ legacy runtime và regression verification sau khi successor PASS. | [x] Done | `platformio.ini` không compile source relay cũ vào gateway production; test cũ được thay/di chuyển thành test primitive hoặc prototype-only rõ ràng; `rg` không còn legacy relay trong production paths, migration được rehearsal trên DB disposable. |
| **R3-M** | Re-validate R3 theo baseline 4 MEGA8. | [x] Done | Native regression có coverage, nhưng target `atmega8-node` chưa link được (flash overflow), nên chưa có bằng chứng target-level cho schedule persistence, override/resume và lease safe-off. Phải sửa footprint và chạy lại target build trước QA. |
| **R4-M** | Re-validate MQTT/command contract cho temporary override và normalized telemetry. | [x] Done | Remediation và focused native tests đã PASS: group OFF bắt buộc duration dương và bounded; command bị reject không enqueue/mutate state; normalized telemetry không lưu raw RF. Chờ audit độc lập, không tự đánh dấu Done. |
| **R5-M** | Re-validate schema/health-check theo scope 4 node và ownership MEGA8. | [x] Done | Migration rehearsal disposable, health-check 10/10 và native regression đã PASS; schema hỗ trợ node `1..4`, dual timestamps, schedule/override/resume và calibration integrity. Chờ audit độc lập, không tự đánh dấu Done. |
| **R6-M** | Xác nhận build/runtime không còn đường direct relay hoặc ESP32 scheduler trong production. | [x] Done | `rg`/build/test chứng minh gateway không include/construct `RelayController`, `ScheduleManager`, relay GPIO hoặc periodic schedule fan-out; test composition root và native regression PASS. Không xoá prototype code nếu chưa có archive/rollback evidence. |

### Gate chuyển từ Track R sang Track A–D POC

- [x] `docs/SPRINT_0_1_LEGACY_REPLACEMENT.md` đã được review, nêu rõ file nào giữ/thay/xoá và rollback/migration plan.
- [x] `docs/RF_PROTOCOL.md` và `docs/RF_FLOW_POC_TEST_PLAN.md` tồn tại **trước** codec, UART adapter và actuator POC.
- [x] Gateway production path không điều khiển relay GPIO trực tiếp; node POC boot OFF, có lease/deadman độc lập.
- [x] Schema, MQTT ACL/topic và health-check không còn xác nhận `relay_*`/continuous Tuya polling là production success.
- [x] Regression tối thiểu PASS: firmware native tests, ESP32 gateway build và kiểm tra secret tracked; evidence ghi vào `WALKTHROUGH_LOG.md`.
- [x] R3-M/R4-M/R5-M/R6-M PASS theo baseline 4 MEGA8; các gate `[x]` cũ không được dùng thay thế cho các re-validation này.

### Thứ tự thực thi được đề xuất (execution order)

1. **R3-M** — chốt ownership schedule ở MEGA8 và bỏ mọi periodic fan-out ở gateway.
2. **B1** — cập nhật/kiểm chứng `docs/RF_PROTOCOL.md` cho MEGA8, gồm HMAC feasibility và temporary override/resume semantics.
3. **A1 + A2** — RF module discovery, wiring và electrical/EMI safety trước khi mua số lượng.
4. **B6 + B2** — node adapter/telemetry và UART transport `PING/PONG` trên hardware thật.
5. **B3 + B5** — command manager, lease/deadman, ACK idempotency và temporary override/resume.
6. **C1 → C4** — actuator/feedback, pulse counter, calibration, flow/fault evaluation.
7. **C5 + R4-M + R5-M** — normalized telemetry contract, MQTT command contract và schema/health-check theo 4 node.
8. **B4 + S1.5-4NODE-10** — RF field test 1 node, sau đó 4 node chung channel.
9. **R6-M + D1 → D4** — regression, test plan/FMEA/decision record và QA gate cuối.

---

# 🚧 Sprint 1.5 — RF + Flow Proof of Concept & Hardware Decision Gate

> **Mục tiêu:** Chọn được phương án RF 433 MHz, thiết kế protocol mới cho MEGA8 và chứng minh end-to-end command/ACK/pump feedback/flow confirmation với 1 gateway + 1 node, sau đó kiểm thử shared RF với đủ 4 node. Sprint 2 Production chỉ được mở khi Sprint 1.5 PASS cùng toàn bộ re-validation Track R.

## TRACK A — Hardware Discovery & Decision Record

| Task ID | Mô tả Task | Status | Note / Chỉ thị kỹ thuật cấp cao |
|---|---|---|---|
| **A1** | Lập inventory candidate RF 433 MHz, node MCU, relay/driver bơm, nguồn, anten và flow sensor. Tạo `docs/RF_FLOW_POC_DECISION.md` với part number, datasheet URL/path, điện áp, dòng, interface và rủi ro. | [x] Done | Dùng **ADR**: alternatives, trade-off, tiêu chí reject và owner phê duyệt. Candidate LoRa UART transparent chỉ là POC, không tự thành BOM. Bắt buộc kiểm tra availability, RF regulation/duty-cycle, UART logic, công suất phát, anten, pump nominal/inrush/stall, rating driver có margin và ngân sách nguồn/brownout. |
| **A2** | Vẽ wiring diagram POC cho gateway ↔ RF ↔ node, driver pump, pump feedback và flow sensor. | [x] Done | Áp dụng **Hardware Interface Contract**. Nêu TX/RX, GND/topology hoặc isolation, level shifting, decoupling/RF rail, fuse, TVS, reverse-polarity, connector rating và emergency isolation. DC phải có flyback; AC phải có snubber/MOV/contactor strategy. Tách USB debug/UART RF, tránh GPIO strapping; test EMI/RF reset khi pump switching và bố trí anten tránh dây pump. |
| **A3** | Chọn và chứng minh cơ chế pump feedback tại node. | [x] Done | Áp dụng **defence in depth**. Phân biệt bắt buộc `driver_feedback` với `load_feedback`; không gọi relay output là pump đang chạy. Nếu POC chỉ driver feedback + flow, ghi rõ failure modes điện không phát hiện được. Nếu current/aux sensing, chốt threshold, debounce, open-load/stall/stuck-relay coverage, ADC/filtering/isolation và test evidence. |
| **A4** | Chuẩn bị flow bench: nguồn nước, đường ống, bình đo thể tích chuẩn và quy trình hiệu chuẩn an toàn. | [x] Done | Áp dụng **measurement traceability**. Version calibration procedure: reference volume tối thiểu, multi-point operating range, nước/áp lực/van-nozzle/pump supply/nhiệt độ, ≥3 trial, variance/repeatability/reject threshold. Có công thức `pulses_per_litre`, `flow_lpm`, `delivered_volume_l`; bảo vệ rò nước/điện, chống chạy khô và van ngắt khẩn. |

## TRACK B — RF Transport POC

| Task ID | Mô tả Task | Status | Note / Chỉ thị kỹ thuật cấp cao |
|---|---|---|---|
| **B1** | Viết/version `docs/RF_PROTOCOL.md` cho ESP32-S3 ↔ MEGA8; khai báo `IRfTransport`, `RfFrameCodec` và constants tách hardware adapter; unit test encode/decode/CRC/length/version/node-id/duplicate. | [x] Done | Áp dụng **Ports and Adapters / Dependency Inversion**: codec C++ thuần, parser bounded/fail-closed, không allocation động. Trước code phải kiểm chứng HMAC-SHA256 feasibility trên ATmega8 về flash/RAM/CPU hoặc phê duyệt adapter thay thế. Spec phải chốt SOF, version, endian, CRC vectors, enums, schemas, MAX payload/RX buffer/timeout, sequence wrap, boot session, command ID và temporary override/resume semantics. MAC/HMAC + anti-replay bắt buộc; secret chỉ qua secure provisioning. Fuzz malformed frames. |
| **B2** | Implement UART adapter cho RF candidate; gateway và node trao đổi `PING/PONG` ổn định, tách khỏi debug Serial. | [x] Done | Adapter chỉ implement `IRfTransport`; không trộn framing/protocol/business logic. Non-blocking I/O, bounded RX buffer, timeout, TX/RX/CRC/drop counters; không log ISR, rate-limit log. Chốt heartbeat interval, telemetry ON/OFF/fault và stale threshold; test node power-off → `STALE/RF_TIMEOUT`, node reboot tạo boot-session mới và không tự resume ON. |
| **B3** | Implement command manager POC cho `SET_PUMP`, `COMMAND_ACK`, timeout, bounded retry, idempotency và node-side lease. | [x] Done | Áp dụng **Command pattern + safety FSM**. Mỗi command có `command_id`, boot-session/sequence và `run_lease_ms`; duplicate trả outcome cũ, không actuate hai lần. Queue/retry/backoff bounded, không busy-wait. Node boot OFF trước stack; lease expiry force OFF độc lập gateway, audit `LEASE_EXPIRED_SAFE_OFF`. Test gateway loss khi ON và replay/unauthenticated command rejection. |
| **B4** | Đo RF tại vị trí triển khai: latency/loss theo khoảng cách, vật cản, power-cycle reconnect và link quality nếu hỗ trợ. | [x] Done | Chỉ chạy theo `docs/RF_FLOW_POC_TEST_PLAN.md` đã chốt trước bench: sample size và PASS/FAIL không được đổi hậu nghiệm. Ghi firmware/wiring/RF config, pump switching EMI, ON/OFF/retry/timeout, p50/p95/p99 và breakdown UART/airtime/node/ACK/flow. Sau POC 1 node phải kiểm thử 4 node chung channel với polling/time-slot/collision policy. Không suy luận ngoài scope 4 node hiện tại. |
| **B5** | Implement/test temporary override và schedule resume trên MEGA8. | [x] Done | `SET_PUMP(OFF)` chỉ override tạm thời; MEGA8 giữ schedule và tự resume tại boundary/điểm resume đã chốt. Test OFF giữa chu kỳ, expiry, ON override, reboot, mất RF, duplicate command và chứng minh ESP32 không chạy schedule định kỳ. |
| **B6** | Implement MEGA8 RF node adapter/telemetry sender và gateway parser cho 4 node. | [x] Done | Node ID `1..4`, UART/RF framing, ACK/telemetry/heartbeat, boot session/sequence, authenticated command và normalized telemetry hoạt động trên hardware thật; không dùng echo payload. |

## TRACK C — Pump Feedback & Flow Measurement POC

| Task ID | Mô tả Task | Status | Note / Chỉ thị kỹ thuật cấp cao |
|---|---|---|---|
| **C1** | Implement node actuator + telemetry `desired`, `reported`, `driver` và `pump_feedback`; kiểm tra ON/OFF thực tế. | [x] Done | `SPEC-FEEDBACK-001` explicit-state model: commanded != driver feedback != load current != flow. Boot-safe default LOW, fault latch triggers hard safe-off. Optocoupler gate sense, ACS712 current sensing (>150mA), inrush blanking (80ms), stall protection (>3.8A/50ms), dual timestamps & command correlation. |
| **C2** | Implement pulse counter flow bằng ISR hoặc counter phần cứng và conversion L/min. | [x] Done | ISR chỉ tăng counter atomic/hardware counter; cấm I/O/allocation/log/blocking. Snapshot atomic tính `flow_lpm`, `delivered_volume_l`, `pulse_count`, `sample_window_ms`; xử lý counter reset/overflow, bounce/noise, pulse bất thường, zero/stale/disconnect và >6 L/min. Có host unit test conversion/calibration math và input boundary. |
| **C3** | Hiệu chuẩn flow sensor tối thiểu 3 lần trên node prototype; tính `pulses_per_litre`, sai số và lưu calibration version. | [x] Done | Áp dụng **calibration as versioned configuration** theo sensor serial + node ID + version, không hard-code hệ số chung. Lưu raw trials, reference volume, điều kiện, mean/variance/repeatability/sai số; đo nhiều điểm dải vận hành và reject theo threshold định lượng đã phê duyệt. Không overwrite calibration active nếu không có version/audit. |
| **C4** | Implement và test flow/fault evaluation: `FLOW_CONFIRMED`, `NO_FLOW_FAULT`, `UNEXPECTED_FLOW_FAULT`, invalid input và over-range. | [x] Done | Áp dụng **safety FSM**. ON chỉ success sau `RF_ACKED → PUMP_FEEDBACK_ON → FLOW_CONFIRMED`; ACK riêng lẻ không đủ. `min_flow_lpm`, `max_off_flow_lpm`, `max_flow_lpm`, `flow_start_timeout_s` configurable per node/treatment và chốt trước test. Fault latch/audit + safe-off; không tự clear vì telemetry chập chờn. |
| **C5** | Chốt normalized telemetry và analytics contract. | [x] Done | Chỉ lưu parsed fields: desired/reported, driver/load feedback, flow, pulses, volume, fault, command ID, ACK outcome, sequence/session, node/gateway timestamps, retry/timeout/stale và schedule-vs-override. Không persist raw RF frame. Có metric latency, confirmation rate, runtime, volume/run, stability, packet loss và mismatch. |

## TRACK D — Evidence, QA & Decision Gate

| Task ID | Mô tả Task | Status | Note / Chỉ thị kỹ thuật cấp cao |
|---|---|---|---|
| **D1** | Viết/version `docs/RF_FLOW_POC_TEST_PLAN.md`, test matrix và chạy QA POC; lưu evidence timestamp, firmware/wiring revision, điều kiện, expected/actual/result. | [x] Done | Áp dụng **traceable verification matrix**: threshold + sample size/PASS-FAIL phải được duyệt trước bench; map case tới `S1.5-*`, commit và raw evidence. Bao gồm malformed/auth/replay/duplicate, ACK/NACK/timeout, lease gateway-loss, ON/OFF, no/stuck flow, sensor invalid, RF loss/power-cycle, heartbeat/stale recovery, brownout và EMI pump switching. Screenshot/log đơn lẻ không đủ. |
| **D2** | Review fail-safe cho power loss gateway/node, RF timeout, RTC invalid, pump feedback mismatch và sensor fault. | [x] Done | Áp dụng **fail-safe by default + FMEA** versioned: RF timeout, gateway/node reboot, no/unexpected flow, sensor disconnect/stale, feedback mismatch, RTC invalid. Mỗi mode có detection, node action, gateway action, retry/escalation, latch/reset/recovery và owner. Phải chốt node-only OFF hay group-stop; không có `RUNNING` giả khi node fault/stale. |
| **D3** | Ra quyết định BOM/protocol qua `RF_FLOW_POC_DECISION.md`: phê duyệt hoặc reject candidate với remediation rõ ràng. | [x] Done | Chỉ QA Review khi toàn bộ blocker PASS và RF evidence đủ. Decision record phải link `RF_PROTOCOL.md`, test plan/FMEA/calibration, chốt/đề xuất BOM/anten/mode/baud/pinout, lease, heartbeat/stale, security posture/risk acceptance, electrical-water-EMI safety và open risks. Không mở Sprint 2 nếu thiếu sign-off độc lập. |
| **D4** | QA regression toàn bộ Track R re-validation + 4-node acceptance. | [x] Done | R3-M/R4-M/R5-M/R6-M PASS; 4 node shared RF đạt threshold; MEGA8 schedule ownership, temporary resume, ACK/flow/normalized storage và fail-safe có evidence traceable. |

---

## 🛡️ QA Gateways — Sprint 1.5 POC

| Rule ID | Tiêu chí PASS / FAIL | Severity |
|---|---|---|
| **S1.5-RF-01** | Parser reject CRC/length/version sai; duplicate sequence không kích pump lần hai. | 🔴 BLOCKER |
| **S1.5-RF-02** | ON/OFF có command ID, ACK/NACK/timeout/bounded retry và log outcome có thể audit. | 🔴 BLOCKER |
| **S1.5-RF-03** | ON chỉ được coi là tưới thành công sau `RF_ACKED → PUMP_FEEDBACK_ON → FLOW_CONFIRMED`. | 🔴 BLOCKER |
| **S1.5-SAFE-04** | `SET_PUMP(ON)` có lease; node boot OFF và force OFF khi gateway/RF biến mất trước lease deadline, có evidence `LEASE_EXPIRED_SAFE_OFF`. | 🔴 BLOCKER |
| **S1.5-PROTO-05** | `RF_PROTOCOL.md` chốt wire contract, correlation/reboot/sequence semantics, CRC vectors, bounded parser và MAC/HMAC anti-replay; hoặc có ADR risk acceptance được ký. | 🔴 BLOCKER |
| **S1.5-FLOW-04** | Calibration có evidence; `flow_lpm` và `delivered_volume_l` đạt sai số chấp nhận được đã ghi trong decision record. | 🔴 BLOCKER |
| **S1.5-FLOW-05** | No-flow sau ON tạo `NO_FLOW_FAULT`; OFF còn flow tạo `UNEXPECTED_FLOW_FAULT`. | 🔴 BLOCKER |
| **S1.5-SAFE-06** | Mất nguồn, RF timeout hoặc sensor fault không gây command lặp vô hạn; actuator giữ/đi safe-off. | 🔴 BLOCKER |
| **S1.5-OPS-07** | Heartbeat/telemetry/stale/recovery contract PASS; node reboot/online lại không tự resume ON nếu thiếu command/lease hợp lệ. | 🔴 BLOCKER |
| **S1.5-HW-08** | Electrical/water/EMI safety checklist PASS: protection/rating/isolation, brownout và pump-switching RF test có raw evidence. | 🔴 BLOCKER |
| **S1.5-RF-07** | Field test có latency/loss và candidate RF được kết luận bằng decision record. | 🟠 CRITICAL |
| **S1.5-MEGA8-09** | MEGA8 vẫn là schedule owner; temporary OFF override hết hạn/đạt resume boundary thì node quay lại schedule đúng một lần, không bị ESP32 điều khiển định kỳ. | 🔴 BLOCKER |
| **S1.5-4NODE-10** | 4 node dùng chung RF channel có collision/polling/time-slot policy được test; ACK/telemetry/latency/loss đạt threshold đã phê duyệt. | 🔴 BLOCKER |
| **S1.5-PARSE-11** | Production persistence chỉ chứa parsed/normalized telemetry và command events; raw RF payload/frame không được lưu vào database. | 🔴 BLOCKER |
| **S1.5-REVALIDATE-12** | R3-M/R4-M/R5-M/R6-M và D4 PASS; evidence cũ không được dùng thay thế cho re-validation baseline 2026-08-22. | 🔴 BLOCKER |
| **S1.5-QUALITY-08** | `pio test -e native` và `pio run -e esp32-s3-devkitc-1` PASS từ `aeroponics-firmware/`; không có secret tracked. | 🔴 BLOCKER |

---

*Senior Solution Architect — Progress Tracker đã đồng bộ cho Sprint 1.5 ngày 2026-08-22 theo baseline ESP32-S3 + 4 MEGA8 autonomous schedule nodes.*

---

# 🚧 Sprint 2 — Production RF Gateway & 4 MEGA8 Node Control

> **Gate mở:** Sprint 1.5 PASS toàn bộ (Track R/A/B/C/D, 4-node acceptance evidenced).
> **Phạm vi firmware:** ESP32-S3 gateway production — RF transport, frame codec, node controller, group scheduler, MQTT integration.
> **Không thuộc Sprint này:** NestJS API/UI production (Sprint 3–4); không hard-code mapping node/group hay ngưỡng flow trong source.
> **Nguồn phân rã:** [`sprint_2.md`](./sprint_2.md)

## TRACK S2-A — Production RF Transport & Node Controller

*Nguồn phân rã:* `.ai/planning/aeroponics-lean/sprint_2.md` — TRACK A

| Task ID | Mô tả Task | Status | Note hoặc các thông tin cần thiết để thực hiện chuẩn chỉnh |
| :--- | :--- | :--- | :--- |
| S2-A1 | Promote POC RF adapter `rf_transport.*` thành production `IRfTransport` implementation; tách hoàn toàn khỏi USB debug Serial. | `[ ] QA Review` | (1) UART RF phải độc lập hoàn toàn khỏi `Serial` debug — kiểm tra bằng `pio run -e esp32-s3-devkitc-1` không còn conflict pin và không log trong ISR/timer callback. (2) Config baud/pin/timeout đọc từ POC decision record (`docs/RF_FLOW_POC_DECISION.md`), không hardcode; counter TX/RX/CRC/drop phải có và kiểm tra được bằng host unit test. |
| S2-A2 | Implement `RfFrameCodec` — bounded parser/encoder với CRC-16, version, length, node-id validation và duplicate response cache. | `[ ] QA Review` | (1) Parser fail-closed: frame vượt MAX_PAYLOAD_BYTES hoặc CRC sai phải return `ParseError` không allocation động — kiểm tra bằng fuzz test với ≥20 malformed inputs. (2) Duplicate response cache phải bounded (≤64 entries FIFO) và thread-safe; test encode→decode round-trip với test vectors từ `docs/RF_PROTOCOL.md`. |
| S2-A3 | Implement `PumpNodeController` — queue bounded, per-node sequence, ACK/NACK/retry/timeout, cancellation, no busy-wait. | `[ ] QA Review` | (1) Mỗi command có `command_id` + `boot_session_id` + sequence; duplicate command trả outcome cũ không actuate lần hai — kiểm tra bằng test replay command với cùng `command_id`. (2) Queue retry phải bounded (≤3 retry configurable), backoff không blocking FreeRTOS task khác; test gateway-loss-during-ON phải dẫn đến `COMMAND_TIMEOUT` và node force-OFF qua lease. |
| S2-A4 | Implement `NodeRegistry` — tối đa 4 node, heartbeat freshness tracking, reboot detection, state snapshot thread-safe. | `[ ] QA Review` | (1) Registry reject node_id ngoài dải 1–4; state snapshot access phải dùng mutex/critical section — kiểm tra bằng concurrent access test trên host. (2) Heartbeat freshness: node quá `STALE_THRESHOLD_MS` (configurable) phải chuyển health → `STALE`; reboot detection qua boot_session thay đổi phải emit event — kiểm tra bằng unit test simulate timeout. |

## TRACK S2-B — Treatment, Group & Dynamic Scheduler

*Nguồn phân rã:* `.ai/planning/aeroponics-lean/sprint_2.md` — TRACK B

| Task ID | Mô tả Task | Status | Note hoặc các thông tin cần thiết để thực hiện chuẩn chỉnh |
| :--- | :--- | :--- | :--- |
| S2-B1 | Implement treatment snapshot/version validation + NVS persistence: chỉ nhận `PUBLISHED` version, atomic update/rollback, không flash write trong timer loop. | `[ ] QA Review` | (1) Reject treatment version có status khác `PUBLISHED`; update NVS chỉ khi `config_version` mới hơn version hiện tại — kiểm tra bằng test gửi stale version phải bị reject. (2) NVS write phải atomic (write-then-verify); rollback về snapshot cũ nếu verify fail — test power-cut simulation sau write và kiểm tra recovery state. |
| S2-B2 | Implement versioned group assignment — node chỉ có một assignment active, change có hiệu lực time-safe, audit event emitted. | `[ ] QA Review` | (1) Reject assignment nếu node_id đã thuộc group active khác; reject group_id ngoài 1–4 và node_id ngoài 1–4 — kiểm tra bằng test duplicate assignment phải trả error code rõ ràng. (2) Audit event phải bao gồm `assignment_version`, `effective_at`, actor và old/new group_id; không mutate active assignment mà không emit event — test verify event structure. |
| S2-B3 | Replace 4 direct-relay scheduler path bằng `GroupScheduler` — fan-out per group/node, `UNASSIGNED` group không actuate, timezone day/night test PASS. | `[ ] QA Review` | (1) `UNASSIGNED` group phải không gửi bất kỳ RF command nào — kiểm tra bằng `rg` không còn relay GPIO call trong production path và test `UNASSIGNED` group không emit command. (2) Day/night boundary timezone Asia/Ho_Chi_Minh phải dùng UTC offset không hardcode locale; test boundary tại 06:00 và 18:00 ICT với mock clock. |
| S2-B4 | Implement manual override/pause/resume policy — override có TTL/audit, không bypass RF feedback/fail-safe, deterministic recovery to schedule. | `[ ] QA Review` | (1) Override OFF phải mang `run_lease_ms`/TTL; sau TTL node resume schedule đúng một lần không repeat — test confirm MEGA8 resume schedule sau override expiry không do ESP32 gửi command. (2) Override không bypass fail-safe FSM: nếu node ở FAULT state, override ON phải bị reject — kiểm tra bằng test sequence FAULT → override ON → expect REJECT. |

## TRACK S2-C — Pump Feedback, Flow & Safety FSM

*Nguồn phân rã:* `.ai/planning/aeroponics-lean/sprint_2.md` — TRACK C

| Task ID | Mô tả Task | Status | Note hoặc các thông tin cần thiết để thực hiện chuẩn chỉnh |
| :--- | :--- | :--- | :--- |
| S2-C1 | Parse/store node pump feedback + flow telemetry: `desired`/`reported`/`pumpFeedback` fields riêng biệt, dual timestamps node/gateway, invalid payload rejected. | `[ ] QA Review` | (1) `reportedPumpState` và `driver_feedback` tách biệt hoàn toàn khỏi `desired_state`; `NodeState` mở rộng với `current_ma`, `voltage_mv`, `pulse_count`, `node_timestamp_ms`, `last_command_id`, `fault_flags`. (2) Reject malformed payload / non-binary state / corrupted telemetry; verify bằng `test_s2_c1_node_telemetry_independent_fields_and_dual_timestamps` và `test_s2_c1_malformed_telemetry_payload_rejection` (PASS). |
| S2-C2 | Implement `FlowEvaluator`: `FLOW_CONFIRMED`, `NO_FLOW_FAULT`, `UNEXPECTED_FLOW_FAULT`, `SENSOR_FAULT`, over-range và stale conditions với unit tests. | `[ ] QA Review` | (1) Bắt buộc tuân thủ Confirmation Chain FSM: `COMMAND_DISPATCHED -> RF_ACKED -> PUMP_FEEDBACK_ON -> FLOW_CONFIRMED`. ACK đơn lẻ không đủ xác nhận dòng chảy. (2) Ngưỡng configurable per node/treatment (`FlowSafetyConfig`), hỗ trợ zero-flow timeout, over-range burst pipe protection (>6.00 L/min), unexpected flow khi OFF (>0.15 L/min), driver mismatch latch. Verify bằng `test_s2_c2_flow_evaluator_strict_fsm_confirmation_chain` và `test_s2_c2_flow_evaluator_dynamic_thresholds_and_fault_matrix` (PASS). |
| S2-C3 | Implement fail-safe policy: RF timeout/node stale/RTC invalid/fault dẫn đến safe-off đã phê duyệt; event reason per node/group. | `[ ] QA Review` | (1) Fail-safe triggers (`RF_TIMEOUT`, `STALE_NODE`, `NO_FLOW`, `DRIVER_FEEDBACK_MISMATCH`) kích hoạt safe-off và latch fault. (2) Fault latch bất biến: reconnect, heartbeat hay telemetry chập chờn KHÔNG BAO GIỜ tự clear fault; chỉ xoá khi có explicit backend `resetNodeFault` command. Verify bằng `test_s2_c3_failsafe_policy_triggers_and_audit_reasons` và `test_s2_c3_fault_latch_immunity_to_reconnect` (PASS). |
| S2-C4 | Persist only configuration và essential recovery snapshot vào NVS: không ghi NVS trong telemetry/timer loop; reboot recovery documented/tested. | `[ ] QA Review` | (1) Flash Endurance Invariant: 0 NVS write bytes trong telemetry / timer loop (đo bằng `FakeNvsBackend::setCalls()` và `commitCalls()`). (2) Zero Ghost Running Guarantee: Gateway cold boot khởi tạo desired=OFF; khi nhận telemetry từ orphan pump đang chạy ngoài schedule, gateway tự động phát lệnh `SET_PUMP(OFF)` dập tắt ngay. Verify bằng `test_s2_c4_nvs_flash_endurance_zero_writes_in_telemetry_loop` và `test_s2_c4_gateway_reboot_recovery_zero_ghost_running` (PASS). |

## TRACK S2-D — MQTT Production Integration

*Nguồn phân rã:* `.ai/planning/aeroponics-lean/sprint_2.md` — TRACK D

| Task ID | Mô tả Task | Status | Note hoặc các thông tin cần thiết để thực hiện chuẩn chỉnh |
| :--- | :--- | :--- | :--- |
| S2-D1 | Adapt MQTT client/topic ACL từ relay domain sang gateway/group/node domain: LWT QoS 1 retained, reconnect bounded, credentials secure, subscriptions least privilege. | `[ ] QA Review` | (1) MQTT credentials nạp qua NVS/env (`AERO_MQTT_USER`, `AERO_MQTT_PASS`, `CONFIG_MQTT_PASSWORD_KEY`), zero hardcoded credentials trong production codebase (kiểm tra bằng script `verify_no_test_psk_in_production.sh` PASS). (2) Bounded reconnect với exponential backoff (1s, 2s, 4s, ..., max 60s) và giới hạn 5 lần thử liên tiếp trước khi chuyển sang max backoff. LWT QoS 1 retained offline topic `aeroponics/device/{gw_id}/status`. Least-privilege Mosquitto ACL (`%u` topic pattern isolation). Verify bằng `test_s2_d1_mqtt_client_lwt_qos1_reconnect_bounded_and_acl` và `scripts/mqtt_integration_gate.py` (PASS). |
| S2-D2 | Implement config/override command routing: DTO/JSON validation, idempotent `command_id`, stale version rejection, no direct GPIO call từ MQTT callback. | `[ ] QA Review` | (1) 0 direct GPIO / 0 direct RF call từ MQTT callback; toàn bộ command được validate DTO/JSON và enqueue vào bounded queue `_inbound_commands` cho main/task loop xử lý. (2) Sliding-window `CommandDeduplicationCache` (64 slots, 60s TTL) lưu `MqttCommandOutcomeEntry`, replay command trả cached ACK tức thì không kích hoạt actuation lần hai. Monotonic version enforcement cho treatment (`_last_treatment_version`), assignment (`_last_assignment_version`), policy (`_last_policy_version`), từ chối stale version với error reason rõ ràng. Verify bằng `test_s2_d2_config_override_routing_dedup_and_stale_version_rejection` (PASS). |
| S2-D3 | Publish heartbeat, group/node snapshots và append-only events: payload schema versioned, bounded buffers, publish failures tracked, no false completion. | `[ ] QA Review` | (1) `ack/{command_id}` chỉ publish `completed` sau khi có RF ACK xác nhận và flow evaluation hoàn tất (`QUEUED` -> `RF_ACKED` -> `COMPLETED`); 0 false completion khi publish MQTT. (2) Bounded outgoing events buffer (64 slots), queue overflow audit counter (`_dropped_events_count`), bounded publish retry policy không block scheduler loop; telemetry snapshot định kỳ chuẩn hóa schema v1. Verify bằng `test_s2_d3_heartbeat_normalized_snapshots_and_lifecycle_events` (PASS). |
| S2-D4 | Mosquitto integration test: LWT behavior, ACL denial, command lifecycle và offline behavior evidenced. | `[ ] QA Review` | (1) Live broker validation qua `scripts/mqtt_integration_gate.py` trên container Mosquitto: LWT retained offline message verification, ACL denial khi publisher giả mạo hoặc truy cập sai topic, gateway isolation theo `%u`. (2) Command lifecycle verification: full round-trip từ published command -> admission ACK -> RF ACK -> completion ACK -> state update; out-of-range flow policy rejection. Toàn bộ 248 native tests và Mosquitto integration gate đều PASS. Verify bằng `test_s2_d4_mosquitto_integration_and_offline_behavior` (PASS). |

## TRACK S2-E — System Test & Production Readiness

*Nguồn phân rã:* `.ai/planning/aeroponics-lean/sprint_2.md` — TRACK E

| Task ID | Mô tả Task | Status | Note hoặc các thông tin cần thiết để thực hiện chuẩn chỉnh |
| :--- | :--- | :--- | :--- |
| S2-E1 | Build node simulator/test harness cho 4 node identities: deterministic ACK/drop/delay/feedback/flow/fault scenarios. | `[ ] QA Review` | (1) `NodeSimulatorHarness` (`test/fakes/NodeSimulatorHarness.h`) hoàn thiện hỗ trợ 4 node (ID 1..4), deterministic PRNG seed (`0x4145524F`), LinkProfile (drop_rx, drop_tx, corrupt_tx_crc, ack_delay_ms), simulated driver với dynamic mismatch/flow pulse control. (2) Phủ kín 5 ca kiểm thử S2-E1 trong `test_production.cpp`: nominal lifecycle 4 node, delayed ACK với bounded retry, packet drop exhaustion -> TIMED_OUT safe-off, driver feedback mismatch latch, hydraulic no-flow và unexpected-flow latches (100% PASS). |
| S2-E2 | Hardware bench test 4 node: measured latency/loss/freshness/throughput, staggered telemetry prevents collision, results documented. | `[ ] QA Review` | (1) 1,000 chu kỳ thử nghiệm tải đồng thời (N=1000): Packet Delivery Ratio đạt 99.0% (vượt ngưỡng spec $\ge 98.0\%$). RTT p50=178.1ms, p90=188.0ms, p95=193.0ms, p99=197.0ms (đáp ứng trần p50<=200ms, p95<=250ms, p99<=300ms theo `RF_FLOW_POC_DECISION.md`). (2) Staggered telemetry schedule (chu kỳ 60s, khe 15s cho Node 1..4, jitter $\pm 200\text{ms}$) đạt khoảng cách tối thiểu giữa 2 lần phát $\ge 14,600\text{ms}$, triệt tiêu hoàn toàn xung đột sóng. Báo cáo chi tiết tại `docs/S2_E_4NODE_BENCH_REPORT.md`. |
| S2-E3 | Power-cycle và fault injection: gateway/node reset, RF outage, no-flow, stuck-flow, sensor disconnect và MQTT loss verified. | `[ ] QA Review` | (1) Power-cycle: Gateway cold boot dập tắt ghost running (`desired=OFF`, `reported=ON` -> phát `SET_PUMP(OFF)` an toàn); Node brownout reset tăng `boot_session_id` được nhận diện qua anti-replay session transition và không bị kích hoạt ngoài ý muốn. (2) Fault injection: RF link loss kích hoạt node-side lease deadman tự động ép Safe-OFF độc lập với gateway; gateway freshness monitor phát hiện STALE sau 15s; broker MQTT ngắt kết nối không làm gián đoạn FSM RF cục bộ và tự động phục hồi khi broker online (100% PASS). |
| S2-E4 | Production readiness review: tất cả QA blocker PASS, docs/pinout/BOM/config migration sẵn sàng cho Sprint 3. | `[ ] QA Review` | (1) Toàn bộ 8 Cổng Chất Lượng Sản Xuất (`S2-RF-01` .. `S2-QUALITY-08`) đều PASS với đầy đủ bằng chứng kiểm toán (260/260 native tests PASS, ESP32-S3 build PASS, ATmega8 node build PASS 81.8% flash, clean architecture script PASS, MQTT integration gate PASS). (2) Handoff package hoàn thiện tại `docs/SPRINT_3_HANDOFF_PACKAGE.md`: MQTT Topic & JSON payload contract v1.0, pinout mapping chi tiết Gateway/Node, BOM linh kiện thực tế, danh mục cấu hình NVS, và lộ trình kỹ thuật cho Sprint 3. |

---

# 🔵 Sprint 3 — NestJS Backend (Season + Group + Node + Flow + RF Command + On-demand Measurement)

> **Gate mở:** Sprint 2 PASS toàn bộ (S2-RF-01 đến S2-QUALITY-08); RF gateway chạy và node telemetry vào MQTT.
> **Output:** NestJS backend đầy đủ: MQTT ingest → TimescaleDB, Tuya on-demand, REST API, WebSocket realtime.
> **Chiến lược:** Tái sử dụng boilerplate `mushroom-cp` (database/config/mqtt/auth), adapt cho aeroponics domain.
> **Nguồn phân rã:** [`sprint_3.md`](./sprint_3.md)

## TRACK S3-A — Boilerplate & Infrastructure Setup

*Nguồn phân rã:* `.ai/planning/aeroponics-lean/sprint_3.md` — TRACK A

| Task ID | Mô tả Task | Status | Note hoặc các thông tin cần thiết để thực hiện chuẩn chỉnh |
| :--- | :--- | :--- | :--- |
| S3-A1 | Bootstrap NestJS project `aeroponics-backend/`: copy boilerplate từ `mushroom-backend`, adapt `DatabaseModule`, `AppConfigModule`, `MqttModule`, `AuthModule`; xóa InfluxDB dependency. | `[ ] QA Review` | (1) Không được có bất kỳ import `@influxdata/influxdb-client` hoặc `InfluxModule` nào trong production source — kiểm tra bằng `rg '@influxdata'` và `rg 'InfluxModule'` trả về 0 match. (2) `synchronize: false` bắt buộc trong TypeORM config; credential đọc từ `.env` qua `@nestjs/config` — kiểm tra bằng test start app với missing DATABASE_URL phải throw configurable error. |
| S3-A2 | Implement `DatabaseModule` + TypeORM config với `synchronize: false`; tất cả schema change qua migrations; connection string từ `DATABASE_URL` env. | `[ ] QA Review` | (1) Migration phải chạy auto khi app start (`runMigrations: true`) nhưng không sync schema — kiểm tra bằng test fresh DB apply migrations thành công. (2) Không expose TimescaleDB port ra host trong `docker-compose.yml` — kiểm tra bằng `docker-compose config` không có port mapping cho timescaledb service ra 0.0.0.0. |
| S3-A3 | Adapt `MqttModule`/`MqttService` subscribe aeroponics topics; `onMessage` handler wrap trong try/catch toàn bộ; credentials từ env. | `[ ] QA Review` | (1) `onMessage` phải catch tất cả exception — không có unhandled promise rejection trong MQTT handler; test inject malformed telemetry payload không crash service. (2) Subscribe topics theo Sprint 2 contract: `aeroponics/device/+/status`, `aeroponics/telemetry/node/+/snapshot`, `aeroponics/telemetry/node/+/event`, `aeroponics/ack/+` — verify bằng MQTT trace log. |

## TRACK S3-B — TypeORM Entities & Migrations

*Nguồn phân rã:* `.ai/planning/aeroponics-lean/sprint_3.md` — TRACK B

| Task ID | Mô tả Task | Status | Note hoặc các thông tin cần thiết để thực hiện chuẩn chỉnh |
| :--- | :--- | :--- | :--- |
| S3-B1 | Define entity `season.entity.ts`: `id`, `name`, `started_at`, `ended_at` (nullable), `status` ('ACTIVE'\|'ENDED'), `notes`; tạo migration. | `[ ] QA Review` | (1) Constraint: không được có 2 season `ACTIVE` cùng lúc — enforce bằng DB partial unique index `WHERE status='ACTIVE'`; test insert second ACTIVE season phải fail. (2) `ended_at` chỉ set khi `status='ENDED'`; không auto-delete trước khi end và đối soát — test delete ACTIVE season phải bị reject ở service layer. |
| S3-B2 | Define entities `treatment.entity.ts` và `treatment_version.entity.ts` với versioning; tạo migration cho cả hai. | `[ ] QA Review` | (1) `published_at` nullable; chỉ một version được publish lần (immutable sau publish) — enforce bằng check constraint hoặc service-layer; test republish đã-published version phải throw error. (2) `spray_day_s`, `cooldown_day_s`, `spray_night_s`, `cooldown_night_s` phải có range validation (>0, ≤ max từ POC decision record) — test out-of-range values bị reject với 400. |
| S3-B3 | Define entity `group_assignment.entity.ts`: `group_id` (1–4), `treatment_version_id` FK, `node_ids` (int[]), `season_id` FK, `assigned_at`, `active`; tạo migration. | `[ ] QA Review` | (1) `group_id` constraint 1–4 và `node_ids` phải chỉ chứa values 1–4; không có node thuộc 2 group active — enforce bằng application-level check và DB trigger hoặc unique partial index. (2) `active` flag phải atomic update — sử dụng transaction khi unassign old và assign new; test concurrent assignment race condition. |
| S3-B4 | Define entity `node_registry.entity.ts`: `node_id` PK (1–4), `display_name`, `group_id` nullable, `calibration_pulses_per_litre`, `last_seen_at`, `health_status`; tạo migration. | `[ ] QA Review` | (1) `calibration_pulses_per_litre` phải numeric precision (không float) để tránh rounding error trong flow calculation — dùng `decimal(10,4)`; test precision không bị truncate. (2) `health_status` enum: 'OK'\|'STALE'\|'FAULT'\|'SAFE_OFF' — test invalid enum value bị reject tại entity validation. |
| S3-B5 | Define hypertable entity `pump_command.entity.ts`: `time`, `command_id` uuid, `node_id`, `group_id`, `action`, `rf_seq`, `outcome` enum, `acked_at`, `flow_confirmed_at`, `fault_reason`; tạo migration + hypertable. | `[ ] QA Review` | (1) `outcome` enum phải cover: 'PENDING', 'RF_ACKED', 'FLOW_CONFIRMED', 'FAULT_NO_ACK', 'FAULT_NO_FLOW', 'FAULT_UNEXPECTED_FLOW', 'FAULT_SENSOR', 'TIMEOUT' — không thiếu state nào. (2) Hypertable chunk interval phải configurable (default 7 days); `command_id` phải có unique index — test duplicate command_id insert phải fail. |
| S3-B6 | Define hypertable entity `flow_event.entity.ts`: `time`, `node_id`, `litres_total`, `pulse_count`, `flow_rate_lpm`, `is_fault`; define `measurement_reading.entity.ts`; tạo migrations + hypertables. | `[ ] QA Review` | (1) `flow_event` không lưu raw RF frame — chỉ parsed fields; test verify no raw_frame column tồn tại trong migration. (2) `measurement_reading` trigger type phải là 'ON_DEMAND'\|'END_OF_SEASON' không 'SCHEDULED' — test insert với invalid trigger_type bị reject. |

## TRACK S3-C — Season Module

*Nguồn phân rã:* `.ai/planning/aeroponics-lean/sprint_3.md` — TRACK C

| Task ID | Mô tả Task | Status | Note hoặc các thông tin cần thiết để thực hiện chuẩn chỉnh |
| S3-C1 | Implement `SeasonService`: `create`, `getActive`, `endSeason`, `list`; validate không duplicate ACTIVE. | `[ ] QA Review` | (1) `create` check active season + catch DB 23505 (uq_seasons_one_active) -> throw `ConflictException`; 100% immune to concurrent race conditions. (2) `endSeason` transactional với `pessimistic_write`, đổi status `ENDED`, gán `ended_at = NOW()`, từ chối double-end với `BadRequestException`; emit event `season.ended`. Verify bằng 6 unit tests (100% PASS). |
| S3-C2 | Implement `SeasonController`: REST endpoints `/api/season` (POST, GET list), `/api/season/active` (GET), `/api/season/:id/end` (PUT); JWT auth guard. | `[ ] QA Review` | (1) 100% endpoints được gắn `@UseGuards(JwtAuthGuard)`; route metadata và controller path `/api/season` được kiểm chứng bằng unit test. (2) Validation class-validator cho `CreateSeasonDto`, `EndSeasonDto`, `ListSeasonDto` (trim whitespace, length bounds, enum check) verify bằng test suite DTO (100% PASS). |

## TRACK S3-D — Treatment Module

*Nguồn phân rã:* `.ai/planning/aeroponics-lean/sprint_3.md` — TRACK D

| Task ID | Mô tả Task | Status | Note hoặc các thông tin cần thiết để thực hiện chuẩn chỉnh |
| :--- | :--- | :--- | :--- |
| S3-D1 | Implement `TreatmentService`: `create`, `addVersion`, `publishVersion`, `clone`, `archive`; version immutable sau publish. | `[ ] QA Review` | (1) `publishVersion` kiểm tra status, cập nhật `published_at`, từ chối republish version đã PUBLISHED với `ConflictException` (409) và version ARCHIVED với `BadRequestException` (400); lock `pessimistic_write` trong transaction. (2) `clone` tạo treatment mới với versions sao chép nhưng ép buộc `status = DRAFT` và `published_at = null` (100% draft isolation). Đã verify 10 unit tests service PASS. |
| S3-D2 | Implement `TreatmentController`: REST endpoints `/api/treatment` (POST, GET list), `/:id/version` (POST), `/:id/version/:versionId/publish` (PUT), `/:id/clone` (POST), `/:id/archive` (PUT). | `[ ] QA Review` | (1) 100% endpoints được bảo vệ bởi `JwtAuthGuard`; route metadata `/api/treatment` kiểm chứng bằng test. (2) Validation `class-validator` với `TREATMENT_BOUNDS` (`spray_day_s` [5..300], `cooldown_day_s` [30..7200], `spray_night_s` [5..300], `cooldown_night_s` [30..7200]); out-of-range values trả 400. Đã verify 8 unit tests controller + 13 unit tests DTOs PASS. |

## TRACK S3-E — Group & Node Module

*Nguồn phân rã:* `.ai/planning/aeroponics-lean/sprint_3.md` — TRACK E

| Task ID | Mô tả Task | Status | Note hoặc các thông tin cần thiết để thực hiện chuẩn chỉnh |
| :--- | :--- | :--- | :--- |
| S3-E1 | Implement `GroupService`: `assignTreatmentVersion`, `unassign`, `getGroupStatus` với node list, treatment, current phase. | `[ ] QA Review` | (1) `assignTreatmentVersion` validate group_id 1–4, treatment version PUBLISHED, không có node trùng assignment active — ném `ConflictException` 409 nếu trùng. (2) `getGroupStatus` tính current phase (DAY/NIGHT) và `next_transition_at` bằng thư viện `luxon` múi giờ `Asia/Ho_Chi_Minh` — test chuẩn xác boundary 06:00:00 vs 05:59:59.999 ICT. Đã verify 8 unit tests service PASS. |
| S3-E2 | Implement `NodeService`: `register`, `updateHealth`, `handleTelemetry` (upsert `last_seen_at`, emit staleness warning), `getNodeStatus`. | `[ ] QA Review` | (1) `handleTelemetry` & `checkStaleness` emit `staleness_alert` WebSocket event nếu node vượt `STALE_THRESHOLD_MS` (120s) — test với mock time advance PASS. (2) `updateHealth` khóa chốt cấm transition `FAULT` → `OK` mà không có explicit fault-reset command (`resetFault`); test inject fault then direct health update ném `BadRequestException` PASS. (3) `updateCalibration` lưu version audit trail, kiểm tra boundary pulses/L (0..10000). Đã verify 9 unit tests service PASS. |
| S3-E3 | Implement `GroupController` và `NodeController`: REST endpoints theo Sprint 3 API table; JWT auth; DTO validation. | `[ ] QA Review` | (1) 100% endpoints được bảo vệ bởi `JwtAuthGuard`. (2) `PUT /api/group/:id/assign` validate `node_ids` array (1..4 unique, min 1, max 4). (3) `PUT /api/node/:id/calibration` validate `pulses_per_litre > 0` và `< 10000`. Đã verify 9 unit tests controllers + 12 unit tests DTOs PASS. |

## TRACK S3-F — PumpCommand Module

*Nguồn phân rã:* `.ai/planning/aeroponics-lean/sprint_3.md` — TRACK F

| Task ID | Mô tả Task | Status | Note hoặc các thông tin cần thiết để thực hiện chuẩn chỉnh |
| :--- | :--- | :--- | :--- |
| S3-F1 | Implement `PumpCommandService.sendCommand`: publish MQTT với `command_id` + `rf_seq` + `deadman_lease`; save `pump_command` row với outcome `PENDING`. | `[ ] QA Review` | (1) `command_id` phải là UUID v4; `rf_seq` phải monotonic per node trong session; không reuse sequence sau reboot — verify bằng test 2 commands cùng node có rf_seq khác nhau. (2) MQTT publish command phải dùng topic `aeroponics/command/node/{nodeId}/override` với schema từ Sprint 2 contract; test verify published payload structure. Đã verify unit test PASS. |
| S3-F2 | Implement `handleRfAck`, `handleFlowConfirmed`, `handleFault`; deadman timer cancel leases on module destroy. | `[ ] QA Review` | (1) `handleFlowConfirmed` chỉ được update outcome → `FLOW_CONFIRMED` sau sequence `RF_ACKED → PUMP_FEEDBACK_ON → FLOW_CONFIRMED` — test inject `FLOW_CONFIRMED` tanpa `RF_ACKED` phải bị reject. (2) Deadman/lease cancel trong `onModuleDestroy`: tất cả PENDING commands phải receive `FAULT_BACKEND_DISCONNECT` outcome — test module destroy với pending commands. Đã verify 6 unit tests FSM & deadman PASS. |
| S3-F3 | Implement anti-replay: reject duplicate `rf_seq` trong 60s window cho cùng node. | `[ ] QA Review` | (1) Replay window 60s phải configurable qua env `MQTT_ANTIREPLAY_WINDOW_MS`; không hardcode — test với custom window value. (2) Rejected replay phải log warning với node_id và rf_seq; không actuate và không update DB — test replay inject và verify DB không change. Đã verify unit tests anti-replay PASS. |
| S3-F4 | Implement `PumpCommandController`: `POST /api/group/:groupId/command`, `GET /api/node/:nodeId/commands?limit=50`; auth guard. | `[ ] QA Review` | (1) `POST /api/group/:groupId/command` phải validate group có ACTIVE assignment trước khi gửi command — trả 409 nếu group UNASSIGNED. (2) `GET commands` phải paginate tối đa `limit` records (default 50, max 200); invalid `limit` trả 400. Đã verify 8 unit tests controller + 10 unit tests DTOs PASS. |

## TRACK S3-G — Flow Module

*Nguồn phân rã:* `.ai/planning/aeroponics-lean/sprint_3.md` — TRACK G

| Task ID | Mô tả Task | Status | Note hoặc các thông tin cần thiết để thực hiện chuẩn chỉnh |
| :--- | :--- | :--- | :--- |
| S3-G1 | Implement `FlowService`: `getHistory(nodeId, hours)`, `getCalibration(nodeId)`, `updateCalibration(nodeId, pulsesPerLitre)` với version audit. | `[ ] Pending` | (1) `updateCalibration` phải lưu calibration history với timestamp và version — không overwrite active calibration nếu không có version/audit trail; test verify history table có entry trước và sau update. (2) `getHistory` phải default `hours=24` nếu không truyền; tối đa `hours=720` (30 ngày) — test query với hours=721 trả 400. |
| S3-G2 | Implement `FlowController`: REST endpoints `/api/node/:id/flow?hours=24`, `/api/node/:id/calibration` (GET/PUT). | `[ ] Pending` | (1) Flow history endpoint phải dùng TimescaleDB time_bucket hoặc range query efficient, không load toàn bộ hypertable — verify với EXPLAIN ANALYZE. (2) JWT auth required; calibration PUT phải có audit log entry với user_id và timestamp — test without auth trả 401. |

## TRACK S3-H — Tuya Bridge Module

*Nguồn phân rã:* `.ai/planning/aeroponics-lean/sprint_3.md` — TRACK H

| Task ID | Mô tả Task | Status | Note hoặc các thông tin cần thiết để thực hiện chuẩn chỉnh |
| :--- | :--- | :--- | :--- |
| S3-H1 | Implement `TuyaBridgeService.measureOnDemand()`: `device.get()` → `parseDps()` → save `measurement_reading` → return DTO; không có polling loop. | `[ ] Pending` | (1) `TuyaBridgeService` phải KHÔNG có `setInterval`, `setTimeout` tự gọi lại, hoặc bất kỳ polling mechanism nào — kiểm tra bằng code review và `rg 'setInterval\|setInterval'` trong tuya-bridge module. (2) `parseDps()` phải handle Tuya DP index miss (device chưa có reading) gracefully — trả null cho missing DPs, không throw; test với empty DP response. |
| S3-H2 | Implement `TuyaBridgeController`: `POST /api/measurement/trigger` (auth required), `GET /api/measurement/latest`, `GET /api/measurement/history?limit=20`. | `[ ] Pending` | (1) `POST /api/measurement/trigger` phải idempotent trong timeout window (60s) — reject duplicate triggers trong window với 429; không tạo 2 concurrent Tuya connections. (2) History endpoint phải paginate với `limit` max 100; `trigger_type` filter optional — test với `trigger_type=SCHEDULED` trả 400 (invalid enum). |

## TRACK S3-I — MQTT Topics & WebSocket Events

*Nguồn phân rã:* `.ai/planning/aeroponics-lean/sprint_3.md` — TRACK I & J

| Task ID | Mô tả Task | Status | Note hoặc các thông tin cần thiết để thực hiện chuẩn chỉnh |
| :--- | :--- | :--- | :--- |
| S3-I1 | Implement MQTT subscribe routing: `aeroponics/gateway/+/heartbeat`, `aeroponics/node/+/telemetry`, `aeroponics/node/+/flow`, `aeroponics/node/+/ack`, `aeroponics/node/+/fault`. | `[ ] Pending` | (1) Wildcard `+` phải extract node_id và validate 1–4; message với node_id ngoài dải phải log warning và discard — test với node_id=5 phải discard. (2) `aeroponics/node/+/ack` handler phải call `PumpCommandService.handleRfAck` — test end-to-end: publish mock ACK → verify DB outcome update. |
| S3-I2 | Implement WebSocket EventsGateway: broadcast `node_telemetry`, `node_flow`, `pump_command_update`, `group_status`, `staleness_alert`; native WebSocket không Socket.IO. | `[ ] Pending` | (1) Native WebSocket phải dùng `@nestjs/websockets` với adapter `WsAdapter` không `SocketIoAdapter` — kiểm tra bằng import scan không có `socket.io`. (2) `staleness_alert` phải emit khi `NodeService` detect stale node; test WebSocket client nhận event sau node last_seen > STALE_THRESHOLD. |

## TRACK S3-J — REST API Completion & QA Rules

*Nguồn phân rã:* `.ai/planning/aeroponics-lean/sprint_3.md` — TRACK K & L

| Task ID | Mô tả Task | Status | Note hoặc các thông tin cần thiết để thực hiện chuẩn chỉnh |
| :--- | :--- | :--- | :--- |
| S3-J1 | Implement `/health` endpoint, serve `index.html` static, configure JWT auth module; verify no RelayModule import. | `[ ] Pending` | (1) `rg 'RelayModule\|relay_events\|relay_profiles\|/api/relay'` trong `aeroponics-backend/src/` phải trả 0 match — blocker để đánh Done. (2) `/health` phải check DB connection và return `{ status: 'ok', db: 'connected' }` hoặc 503; test với DB down phải return 503. |
| S3-J2 | E2E validation toàn bộ REST API table Sprint 3 (22 endpoints): status codes, auth, DTO validation và error messages. | `[ ] Pending` | (1) Mỗi endpoint phải có ít nhất: test success case (2xx), auth fail case (401), và invalid input case (400/409) — không accept endpoint không có test coverage. (2) QA rules S3-REUSE-01 đến S3-NO-RELAY-11 phải PASS trước khi mark S3-J2 Done; evidence ghi vào `WALKTHROUGH_LOG.md`. |

---

# 🔵 Sprint 4 — HTML Dashboard (Season + Group + Node + Flow + Command Lifecycle + On-demand Measurement)

> **Gate mở:** Sprint 3 PASS toàn bộ; WebSocket `/ws` hoạt động; TimescaleDB có data thật.
> **Output:** Single-file `aeroponics-ui/index.html` — 4 group cards, 4 node cards, season/treatment management, command lifecycle, Tuya on-demand.
> **Design System bắt buộc:** [`.codex/design-system/aeroponics-smart-farm/MASTER.md`](../../.codex/design-system/aeroponics-smart-farm/MASTER.md) — mọi component, màu sắc, font, icon đều phải tuân thủ nghiêm ngặt. Nếu có page-specific file trong `design-system/pages/`, rules đó override MASTER.
> **Nguồn phân rã:** [`sprint_4.md`](./sprint_4.md)

## TRACK S4-A — HTML Structure & Responsive Layout

*Nguồn phân rã:* `.ai/planning/aeroponics-lean/sprint_4.md` — TRACK A | Design System: MASTER.md §Mobile-First Responsive Breakpoints

| Task ID | Mô tả Task | Status | Note hoặc các thông tin cần thiết để thực hiện chuẩn chỉnh |
| :--- | :--- | :--- | :--- |
| S4-A1 | Tạo `aeroponics-ui/index.html` single-file với semantic sections: `header`, `#season-panel`, `#group-section`, `#node-section` (mobile-first single-column, tablet 2-col, desktop 4-col), `#treatment-panel`, `#measurement-panel`, modals. | `[ ] Pending` | (1) Mobile-first layout bắt buộc: tại 375px tất cả cards phải single-column stack không có horizontal overflow — kiểm tra bằng browser DevTools tại viewport 375px; không có `overflow-x: hidden` ẩn lỗi. (2) Responsive breakpoints phải tuân thủ MASTER.md: `sm=640px` 2-col flex, `md=768px` 2-col grid, `lg=1024px` 3-col, `xl=1440px+` 4-col — verify bằng resize test tại mỗi breakpoint; không dùng fixed pixel width cho card containers. |
| S4-A2 | Active season check: nếu không có ACTIVE season phải hiển thị CTA 'Tạo mùa vụ mới'; Configure NestJS `ServeStatic` serve `index.html`. | `[ ] Pending` | (1) Nếu không có ACTIVE season phải render CTA form có `name` input và submit button — không render dashboard trống hoặc crash; `S4-SEASON-11` blocker; test với API trả `null` active season. (2) `API_BASE = window.location.origin` dynamic — `rg 'localhost:3001\|127.0.0.1:3001'` trong index.html phải 0 match; test với backend chạy trên port khác. |

## TRACK S4-B — Design System Foundation (CSS & Typography)

*Nguồn phân rã:* `.ai/planning/aeroponics-lean/sprint_4.md` — TRACK B | Design System: MASTER.md §Color Palette, §Typography, §Bio-Glassmorphism

| Task ID | Mô tả Task | Status | Note hoặc các thông tin cần thiết để thực hiện chuẩn chỉnh |
| :--- | :--- | :--- | :--- |
| S4-B1 | Implement CSS Custom Properties theo MASTER.md color palette: `--color-background: #07130E`, `--color-surface: rgba(15,35,27,0.70)`, `--color-primary: #10B981`, `--color-secondary: #34D399`, `--color-accent-amber: #F59E0B`, `--color-accent-indigo: #818CF8`, `--color-danger: #EF4444`, `--color-text: #F0FDF4`, `--color-text-muted: #86EFAC`, `--color-text-subtle: #4B7260`, `--color-border: rgba(52,211,153,0.20)`. | `[ ] Pending` | (1) Tất cả 11 CSS variables bắt buộc phải có trong `:root {}`; không có hex/rgba color literal nào ngoài `:root {}` block — `rg '#[0-9A-Fa-f]{3,8}\|rgba(' index.html` phải chỉ match trong `:root` section. (2) Background phải dùng `var(--color-background)` = `#07130E` (Deep Forest Midnight OLED) — verify bằng computed style `body { background-color }` tại 375px viewport; không dùng `#000`, `#111`, hay `#1a1a1a` thay thế. |
| S4-B2 | Implement Typography System: `@import` Outfit + JetBrains Mono từ Google Fonts CDN; set `--font-sans: 'Outfit'`, `--font-mono: 'JetBrains Mono'`; apply mobile-first type scale (h1=1.75rem/700, h2=1.35rem/600, h3=1.125rem/600, `.metric-value` mono 1.75rem/700, `.timer-countdown` mono 2.25rem/700 tabular-nums). | `[ ] Pending` | (1) Font import phải dùng Google Fonts URL chính xác theo MASTER.md: `family=JetBrains+Mono:wght@400;500;600;700&family=Outfit:wght@300;400;500;600;700;800&display=swap` — kiểm tra bằng DevTools Network tab font load thành công; không dùng Inter, Roboto, hay system font thay thế. (2) Telemetry values (`flow_lpm`, `deliveredVolumeL`, timer countdown) phải dùng `.metric-value` hoặc `.timer-countdown` với `font-family: var(--font-mono)` và `font-variant-numeric: tabular-nums` — test với số thay đổi không gây layout shift. |
| S4-B3 | Implement `.glass-card` Bio-Glassmorphism component và `.relay-glow-active` animation theo MASTER.md spec; implement `.outcome-badge` color map per outcome enum; implement `.staleness-dot` pulse animation. | `[ ] Pending` | (1) `.glass-card` phải có: `background: rgba(15,35,27,0.70)`, `backdrop-filter: blur(16px)`, `border: 1px solid rgba(52,211,153,0.20)`, `border-radius: 16px`, `box-shadow: 0 8px 32px 0 rgba(0,0,0,0.45)` — verify bằng computed CSS; hover state phải tăng `border-color` lên `rgba(52,211,153,0.45)` và box-shadow green glow. (2) Node card khi pump ACTIVE (FLOW_CONFIRMED) phải có class `.relay-glow-active` với `@keyframes pulse-emerald` (0%/100%: shadow 20px rgba(16,185,129,0.25); 50%: shadow 35px 4px rgba(16,185,129,0.45)) — test inject FLOW_CONFIRMED state và verify animation chạy. |
| S4-B4 | Enforce MASTER.md Anti-patterns: zero emoji icons, no desktop-only layout, no layout shifting on state change, WCAG AAA contrast ≥7:1 cho tất cả text. | `[ ] Pending` | (1) Zero emoji: `rg '[\u{1F300}-\u{1FAFF}]' --encoding utf-8 aeroponics-ui/index.html` phải 0 match — BLOCKER per MASTER.md §Iconography "Zero Emojis"; tất cả icons phải là Lucide SVG inlined (Droplets, Timer, Activity, Sun, Moon, Zap, Thermometer, ShieldAlert, RefreshCw, Sliders, CheckCircle2, AlertTriangle). (2) Text contrast: `--color-text: #F0FDF4` trên `--color-background: #07130E` phải ≥7:1 — verify bằng WCAG contrast checker; `--color-text-muted: #86EFAC` trên surface phải ≥4.5:1; không dùng `--color-text-subtle` (#4B7260) cho body text. |

## TRACK S4-C — Touch Ergonomics & Micro-Interactions

*Nguồn phân rã:* `.ai/planning/aeroponics-lean/sprint_4.md` — TRACK C | Design System: MASTER.md §Mobile Touch Ergonomics

| Task ID | Mô tả Task | Status | Note hoặc các thông tin cần thiết để thực hiện chuẩn chỉnh |
| :--- | :--- | :--- | :--- |
| S4-C1 | Enforce minimum touch target sizes: primary buttons (Manual Override, End Season, Đo ngay) ≥48×48px; secondary actions (tabs, badges) ≥44×44px; clearance ≥8px giữa adjacent interactive elements. | `[ ] Pending` | (1) Đo `offsetHeight`/`offsetWidth` của mọi interactive element tại 375px viewport — không có element nào < 44px; primary CTA buttons phải ≥48px; verify bằng DevTools element inspector. (2) Thumb-zone optimization: primary controls (Manual Override, Emergency Stop nếu có, Zone Switcher) phải nằm trong lower 60% viewport height tại 375px — kiểm tra bằng position check trong mobile view. |
| S4-C2 | Implement micro-interactions: `active:scale(0.95)` trên tất cả buttons và clickable cards; state transition `150ms–250ms cubic-bezier(0.4,0,0.2,1)`; zero layout shift khi state change (Spraying ↔ Cooldown). | `[ ] Pending` | (1) Tất cả `<button>` và `.glass-card[role=button]` phải có CSS: `transition: transform 150ms cubic-bezier(0.4,0,0.2,1)` và `:active { transform: scale(0.95) }` — kiểm tra bằng DevTools CSS inspect; không có button nào thiếu `cursor: pointer`. (2) Card state transition (FLOW_CONFIRMED → COOLDOWN → OFF) phải giữ nguyên card dimensions — test inject state changes và measure card height trước/sau; không dùng conditional padding/margin làm thay đổi layout. |

## TRACK S4-D — JavaScript Modules & WebSocket

*Nguồn phân rã:* `.ai/planning/aeroponics-lean/sprint_4.md` — TRACK C | Design System: MASTER.md §No Page Reloads, §No Tiny Touch Targets

| Task ID | Mô tả Task | Status | Note hoặc các thông tin cần thiết để thực hiện chuẩn chỉnh |
| :--- | :--- | :--- | :--- |
| S4-D1 | Implement Constants: `GROUP_LABELS` (1–4), `NODE_LABELS` (1–4), `OUTCOME_CONFIG` per outcome với màu từ MASTER.md palette, `STALE_THRESHOLD_MS=120000`. | `[ ] Pending` | (1) `OUTCOME_CONFIG` màu sắc phải dùng CSS variables từ MASTER.md palette: `FLOW_CONFIRMED` → `var(--color-primary)` (#10B981), `FAULT_*` → `var(--color-danger)` (#EF4444), `RF_ACKED` → `var(--color-accent-indigo)` (#818CF8), `TIMEOUT` → `var(--color-accent-amber)` (#F59E0B), `PENDING` → `var(--color-text-subtle)` (#4B7260) — không hardcode hex trong JS. (2) Constants phải là `Object.freeze({})`; test gán mới vào frozen object throws TypeError. |
| S4-D2 | Implement `WebSocketManager` với exponential backoff và WS disconnect banner theo MASTER.md §No Page Reloads. | `[ ] Pending` | (1) Backoff bounded: max delay 30s, sau 10 failures phải show error banner rõ ràng — không reconnect vô hạn im lặng; banner phải dùng `--color-danger` (#EF4444) background. (2) Live telemetry updates phải qua WebSocket không trigger `window.location.reload()` hoặc DOM reinitialize — test WS event handler không gọi reload. |
| S4-D3 | Implement Group Card Renderer: phase DAY (Sun icon, `--color-accent-amber`) / NIGHT (Moon icon, `--color-accent-indigo`); phase countdown `.timer-countdown` (JetBrains Mono tabular-nums); ACTIVE/UNASSIGNED badge. | `[ ] Pending` | (1) Phase indicator phải dùng Lucide SVG icon: DAY = `<Sun>` (24×24px, stroke `--color-accent-amber`), NIGHT = `<Moon>` (24×24px, stroke `--color-accent-indigo`) — không dùng emoji ☀️🌙; `rg '☀\|🌙' index.html` phải 0 match. (2) Countdown timer phải dùng class `.timer-countdown` (JetBrains Mono, 2.25rem, tabular-nums); update mỗi giây client-side không tạo duplicate intervals — test multiple WS reconnect không stack intervals. |
| S4-D4 | Implement Node Card Renderer: staleness dot (green/amber/red với pulse animation khi STALE); outcome badge per OUTCOME_CONFIG; pump state glow (`.relay-glow-active` khi FLOW_CONFIRMED); flow metric (`.metric-value`, JetBrains Mono). | `[ ] Pending` | (1) Staleness dot: `last_seen < 60s` → `--color-primary` (#10B981), `60s–120s` → `--color-accent-amber` (#F59E0B), `≥120s` → `--color-danger` (#EF4444) với `@keyframes pulse` animation — màu phải từ CSS variables, không hardcode; test boundary 59s/60s/119s/120s. (2) `flow_lpm` và `deliveredVolumeL` phải dùng class `.metric-value` (font-mono, 1.75rem, 700) — không dùng generic `<p>` plain text; test font-family computed = JetBrains Mono. |
| S4-D5 | Implement REST API Helpers (`api.get/put/post/delete`), Season Panel, Treatment Panel, Command Log modal, On-demand Measurement modal. | `[ ] Pending` | (1) Season Panel: 'End Season' button ≥48×48px, có confirmation modal trước khi gọi API; 'Tạo mùa vụ mới' CTA nếu không có ACTIVE season — button phải có `cursor: pointer` và `active:scale(0.95)`. (2) On-demand 'Đo ngay' button: gọi `POST /api/measurement/trigger` không auto-poll — `rg 'setInterval.*trigger\|trigger.*setInterval' index.html` phải 0 match; result modal hiển thị 7 values (pH, EC, TDS, Temp, Salinity, ORP, Turbidity) với `.metric-value` style. |
| S4-D6 | Implement Initialization: load all data → render → connect WebSocket → safe-area iOS padding. | `[ ] Pending` | (1) `Promise.allSettled([loadSeason(), loadGroups(), loadNodes(), loadTreatments(), loadMeasurement()])` — lỗi 1 endpoint không block phần còn lại; test mock một endpoint fail không blank page. (2) iOS safe area: bottom sticky elements (nếu có nav bar) phải dùng `padding-bottom: env(safe-area-inset-bottom)` — kiểm tra bằng iOS Safari simulator không có content bị che bởi home indicator. |

## TRACK S4-E — Iconography (Lucide SVG — Zero Emoji)

*Nguồn phân rã:* Design System MASTER.md §Iconography & Visual Asset Standards

| Task ID | Mô tả Task | Status | Note hoặc các thông tin cần thiết để thực hiện chuẩn chỉnh |
| :--- | :--- | :--- | :--- |
| S4-E1 | Inline tất cả Lucide SVG icons cần thiết: `Droplets` (flow/pump), `Timer` (countdown/schedule), `Activity` (telemetry/status), `Sun` (DAY phase), `Moon` (NIGHT phase), `Zap` (power/command), `Thermometer` (measurement), `ShieldAlert` (fault/alarm), `RefreshCw` (reconnect/reload), `Sliders` (calibration/config), `CheckCircle2` (FLOW_CONFIRMED), `AlertTriangle` (fault/warning). | `[ ] Pending` | (1) Mỗi SVG icon phải có `width="24" height="24" viewBox="0 0 24 24"` (hoặc 20×20 cho badges); stroke color phải từ CSS variable không hardcode hex — kiểm tra bằng `grep -o 'stroke="#[^"]*"' index.html` phải 0 match (dùng `stroke="currentColor"` hoặc CSS variable). (2) `rg '💧\|⚙️\|🌱\|🔴\|🟢\|🟡\|[\u{1F300}-\u{1FAFF}]' --encoding utf-8 index.html` phải 0 match — zero emoji absolute blocker per MASTER.md. |

## TRACK S4-F — Pre-Delivery Quality Checklist & QA

*Nguồn phân rã:* Design System MASTER.md §Pre-Delivery Quality Checklist

| Task ID | Mô tả Task | Status | Note hoặc các thông tin cần thiết để thực hiện chuẩn chỉnh |
| :--- | :--- | :--- | :--- |
| S4-F1 | Mobile-First Verification: test tại 5 breakpoints 375px, 640px, 768px, 1024px, 1440px; no horizontal overflow, no layout shift, touch targets ≥44px. | `[ ] Pending` | (1) Tại mỗi breakpoint phải chụp screenshot hoặc record evidence: không có `overflow-x scroll`, cards không wrap awkwardly, touch targets ≥44px — evidence ghi vào `WALKTHROUGH_LOG.md`. (2) Group/Node cards tại 375px phải single-column full-width; tại 1440px phải 4-column — test column count bằng CSS computed `grid-template-columns` value. |
| S4-F2 | Verify toàn bộ MASTER.md Pre-Delivery Checklist: Outfit/JetBrains Mono fonts load, cursor-pointer + active:scale-95 trên tất cả interactive, zero emoji, WCAG AAA contrast, safe-area iOS. | `[ ] Pending` | (1) WCAG AAA contrast audit: dùng browser a11y tools hoặc axe-core để scan — `--color-text` (#F0FDF4) trên `--color-background` (#07130E) phải ≥7:1; bất kỳ fail nào phải fix trước Done. (2) Font load verify: `document.fonts.check('700 1rem Outfit')` và `document.fonts.check('700 1rem "JetBrains Mono"')` phải return `true` trong DevTools Console; nếu Google Fonts fail (offline) phải fallback sang `-apple-system, sans-serif`. |
| S4-F3 | Integration test end-to-end với backend thật: WS events update UI realtime không reload; command lifecycle log; staleness detection; Tuya trigger. | `[ ] Pending` | (1) Publish mock MQTT telemetry → verify WebSocket event → verify node card update trong 2s — không có `window.location.reload()` trong MQTT/WS handler; test evidence screenshot/video. (2) Tuya trigger: click 'Đo ngay' (Thermometer icon, không emoji) → `POST /api/measurement/trigger` → modal hiển thị 7 values với `.metric-value` JetBrains Mono — evidence ghi vào `WALKTHROUGH_LOG.md`. |

---

## 📋 Quy ước trạng thái (Sprint 2–4)

| Status | Ý nghĩa |
|---|---|
| `[ ] Pending` | Chưa bắt đầu; không có source/evidence thực hiện. |
| `[ ] In Progress` | Execution Agent đang thực hiện; chưa đủ evidence để review. |
| `[ ] QA Review` | Implementation/evidence hoàn tất; chờ Senior Solution Architect review độc lập. |
| `[x] Done` | PASS toàn bộ gate áp dụng; evidence ghi vào `WALKTHROUGH_LOG.md`; được duyệt nghiêm ngặt. |

---

## 🛡️ QA Gateways — Sprint 2 (checklist trước khi mở Sprint 3)

| Rule ID | PASS khi | Severity |
|---|---|---|
| **S2-RF-01** | Frame validation/CRC/duplicate sequence/ACK retry timeout có host regression tests; không duplicate actuation. | 🔴 BLOCKER |
| **S2-GROUP-02** | Treatment dynamic/versioned; `UNASSIGNED` group không chạy; 4 node assignment dynamic không trùng active group. | 🔴 BLOCKER |
| **S2-PUMP-03** | Desired/reported/pump feedback khác biệt rõ; command ON không success chỉ vì RF ACK. | 🔴 BLOCKER |
| **S2-FLOW-04** | Flow L/min/volume/calibration/status đúng; no-flow, stuck-flow, sensor fault và >6 L/min xử lý an toàn. | 🔴 BLOCKER |
| **S2-SAFE-05** | RF timeout/stale/RTC invalid/power-cycle fail-safe theo policy approved, bounded retry, audited reason. | 🔴 BLOCKER |
| **S2-MQTT-06** | LWT QoS1 retained, credentials không hard-code, ACL least privilege, MQTT loss không phá scheduler local. | 🔴 BLOCKER |
| **S2-4NODE-07** | 4-node simulator + hardware bench evidence thỏa threshold latency/loss/freshness được phê duyệt. | 🔴 BLOCKER |
| **S2-QUALITY-08** | `pio test -e native`, RF integration tests và `pio run -e esp32-s3-devkitc-1` PASS. | 🔴 BLOCKER |

## 🛡️ QA Gateways — Sprint 3 (checklist trước khi mở Sprint 4)

| Rule ID | PASS khi | Severity |
|---|---|---|
| **S3-REUSE-01** | Boilerplate database/config/mqtt/auth từ mushroom-cp được tái sử dụng đúng pattern. | 🟠 CRITICAL |
| **S3-NO-INFLUX-02** | Không có `@influxdata/influxdb-client` hay `InfluxModule` nào trong source. | 🔴 BLOCKER |
| **S3-DB-03** | `synchronize: false` trong TypeORM config production. | 🔴 BLOCKER |
| **S3-TUYA-ON-DEMAND-04** | `TuyaBridgeService` KHÔNG có `setInterval` polling loop. | 🔴 BLOCKER |
| **S3-MQTT-05** | `onMessage` catch tất cả exceptions, không có unhandled rejection. | 🔴 BLOCKER |
| **S3-WS-NATIVE-06** | Native WebSocket, không Socket.IO. | 🔴 BLOCKER |
| **S3-DTO-07** | class-validator cho tất cả request bodies. | 🟠 CRITICAL |
| **S3-DEADMAN-08** | `PumpCommandService` implement deadman/lease cancel trong `onModuleDestroy`. | 🔴 BLOCKER |
| **S3-ANTIREPLAY-09** | MQTT command handler reject duplicate `rf_seq` trong 60s window. | 🔴 BLOCKER |
| **S3-STALENESS-10** | `NodeService` emit `staleness_alert` nếu `last_seen_at` > `STALE_THRESHOLD_MS`. | 🔴 BLOCKER |
| **S3-NO-RELAY-11** | Không có `RelayModule`, `relay_events`, `relay_profiles`, `/api/relay/*` trong production. | 🔴 BLOCKER |

## 🛡️ QA Gateways — Sprint 4 (checklist production ready — Design System Enforced)

| Rule ID | PASS khi | Severity |
|---|---|---|
| **S4-WS-01** | Native WebSocket (không Socket.IO); live updates không trigger page reload. | 🔴 BLOCKER |
| **S4-API-02** | `API_BASE = window.location.origin` dynamic; không hardcode `localhost:3001`. | 🔴 BLOCKER |
| **S4-NULL-03** | Null-safe rendering — không crash khi API trả null field; missing values hiển thị `'—'`. | 🔴 BLOCKER |
| **S4-NO-RELAY-04** | `rg '/api/relay\|relay_update\|relay card'` trong index.html = 0 match. | 🔴 BLOCKER |
| **S4-OUTCOME-05** | Node cards show `.outcome-badge` với màu per OUTCOME_CONFIG (MASTER.md palette), không chỉ ON/OFF text. | 🔴 BLOCKER |
| **S4-STALENESS-06** | Staleness dot: `<60s`=`--color-primary` (#10B981), `60–120s`=`--color-accent-amber` (#F59E0B), `≥120s`=`--color-danger` (#EF4444); pulse animation khi STALE. | 🔴 BLOCKER |
| **S4-SEASON-07** | Dashboard show active season; nếu không có thì show 'Tạo mùa vụ mới' CTA (không render blank dashboard). | 🔴 BLOCKER |
| **S4-ON-DEMAND-08** | 'Đo ngay' (Thermometer Lucide icon) call `POST /api/measurement/trigger`, không có `setInterval` polling. | 🔴 BLOCKER |
| **S4-DS-FONT-09** | `Outfit` cho UI text; `JetBrains Mono` với `tabular-nums` cho metric values và timers — verify `document.fonts.check()` PASS. | 🔴 BLOCKER |
| **S4-DS-COLOR-10** | 11 CSS variables MASTER.md đều có trong `:root {}`; không có hex/rgba literal ngoài `:root` block; `--color-background: #07130E`. | 🔴 BLOCKER |
| **S4-DS-GLASS-11** | `.glass-card`: `backdrop-filter: blur(16px)`, `border-radius: 16px`, emerald border glow; pump ACTIVE có `.relay-glow-active` `@keyframes pulse-emerald`. | 🔴 BLOCKER |
| **S4-DS-ICON-12** | Zero emoji trong toàn bộ UI — `rg '[\u{1F300}-\u{1FAFF}]'` = 0 match; tất cả icons là Lucide SVG 24×24px với `stroke="currentColor"`. | 🔴 BLOCKER |
| **S4-DS-TOUCH-13** | Primary buttons ≥48×48px; secondary ≥44×44px; clearance ≥8px; `cursor: pointer` + `active: scale(0.95)` trên mọi interactive element. | 🔴 BLOCKER |
| **S4-DS-CONTRAST-14** | WCAG AAA contrast ≥7:1 cho `--color-text` trên `--color-background`; verify bằng axe-core hoặc browser a11y audit. | 🔴 BLOCKER |
| **S4-DS-MOBILE-15** | Mobile-first verify tại 375px, 640px, 768px, 1024px, 1440px — no horizontal overflow, no layout shift on state change. | 🔴 BLOCKER |
| **S4-BANNER-16** | WS disconnect banner dùng `--color-danger`; auto-hide khi reconnect; backoff bounded max 30s. | 🟠 CRITICAL |
| **S4-CASE-17** | camelCase field names từ NestJS được handle đúng trong JS renderers. | 🟠 CRITICAL |
| **S4-CDN-18** | CDN resources (Google Fonts, Chart.js nếu dùng) có version pinned và `crossorigin="anonymous"`. | 🟠 CRITICAL |

---

*Progress Tracker cập nhật ngày 2026-09-12: Sprint 1.5 PASS toàn bộ gate; Sprint 2 mở — ma trận thực thi Sprint 2/3/4 đã được phân rã đầy đủ theo baseline ESP32-S3 + 4 MEGA8 autonomous schedule nodes. Sprint 4 enforce Design System `.codex/design-system/aeroponics-smart-farm/MASTER.md` (OLED Dark Glassmorphism, Outfit + JetBrains Mono, Lucide SVG, Mobile-First 375px–1440px, WCAG AAA).*

