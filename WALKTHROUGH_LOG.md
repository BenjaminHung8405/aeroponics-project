# Walkthrough Log

## [2026-08-12T14:06:00+07:00] Track R (R1–R6) - Remediate All QA Review Rejection Findings (Lần 3)

- **Task ID:** Track R (Tasks R1–R6)
- **Trạng thái hiện tại:** Đang chờ QA Review (Lần 3)
- **Danh sách file đã sửa:**
  - `docs/RF_PROTOCOL.md`
  - `aeroponics-firmware/include/core/hmac_sha256.h` [NEW]
  - `aeroponics-firmware/src/core/hmac_sha256.cpp` [NEW]
  - `aeroponics-firmware/include/command_manager.h`
  - `aeroponics-firmware/src/command_manager.cpp`
  - `aeroponics-firmware/include/mqtt_client.h`
  - `aeroponics-firmware/src/mqtt_client.cpp`
  - `aeroponics-firmware/include/node_registry.h`
  - `aeroponics-firmware/src/main.cpp`
  - `aeroponics-firmware/test/test_production/test_production.cpp`
  - `aeroponics-firmware/platformio.ini`
  - `database/schema.sql`
  - `database/001_production_domain_migration.sql`
  - `scripts/health-check.sh`
  - `.env.example`
  - `docker-compose.yml`
  - `docs/SPRINT_0_1_LEGACY_REPLACEMENT.md`
  - `mosquitto/config/acl`
  - `.ai/planning/aeroponics-lean/PROGRESS.md`
  - `WALKTHROUGH_LOG.md`

- **Giải trình ngắn gọn:**
  1. **Gateway Composition Root (`main.cpp`):** Tạo và wiring hoàn chỉnh `NodeRegistry`, `GroupScheduleManager`, `CommandManager`, `MqttClient`, `UartRfTransport`. Thêm các vòng lặp periodic services có bounded timing cho RF RX UART frame slicing, schedule tick (1000ms), command fan-out / retry tick (100ms), và stale node evaluation (5000ms).
  2. **MQTT Validation & Refactoring (`mqtt_client.cpp`):** Đổi callback `_onMessage` sang full-match topic parsing với kiểm tra prefix/suffix nghiêm ngặt. Validate `node_id` (1..12) và `group_id` (1..4) bằng `strtoul` + boundary checks. Bắt buộc payload phải có `command_id` non-empty (max 64 bytes). Chỉ publish ACK `ACCEPTED`/`COMPLETED` khi mutation thành công, ngược lại publish NACK `REJECTED` cùng lý do explicit. Phân rã `_onMessage` thành 3 handlers gọn gàng (<50 dòng mỗi method).
  3. **RF Protocol Security & HMAC Tag (`RF_PROTOCOL.md`, `hmac_sha256`, `command_manager`):** Cập nhật wire contract thêm 4-byte truncated HMAC-SHA256 authentication tag (`mac[4]`) và anti-replay window table theo `boot_session_id` & `sequence` từng node. Benchmark verify HMAC theo constant-time `constantTimeCompare`. Loại bỏ pointer cast không căn lề (`reinterpret_cast`), thay bằng byte-wise copy vào packed local structs.
  4. **RF Pending Command Table & Retry Backoff (`command_manager.cpp`):** Thiết kế bảng pending command per node. Chỉ retry tối đa 3 lần với backoff 1000ms; khi chạm terminal timeout, huỷ pending command và mark node state thành `FAULT` (với lý do `RF_COMMAND_TIMEOUT`). Duplicate ACK chỉ correlate và clear pending command mà không gây lặp lại side-effects.
  5. **Database Schema & Migration Cleanup (`schema.sql`, `001_production_domain_migration.sql`):** Bổ sung `CREATE EXTENSION IF NOT EXISTS pgcrypto;` ở line 1. Gỡ bỏ hoàn toàn các legacy objects (`relay_profiles`, `relay_events`, `sensor_readings`, `group_assignments.node_ids`) khỏi fresh bootstrap `schema.sql`. Đảm bảo script migration `001_production_domain_migration.sql` tạo/update chuẩn xác 10 bảng production + 5 hypertables.
  6. **Security & Credentials Fail-Closed (`health-check.sh`, `.env.example`, `docker-compose.yml`):** Cập nhật `scripts/health-check.sh` để kiểm tra sự tồn tại và loại bỏ mật khẩu yếu/placeholder (`aeroponics_secret`, `admin_secret`, `CHANGE_ME`). Nếu thiếu hoặc là placeholder, script ngay lập tức fail-closed trước khi gọi Docker commands. Thay thế `TUYA_POLL_INTERVAL_MS=10000` thành `TUYA_ON_DEMAND_TIMEOUT_MS=5000`.
  7. **Tài liệu & Formatting:** Đã cập nhật `docs/SPRINT_0_1_LEGACY_REPLACEMENT.md` về trạng thái Draft/Review Pending. Đã dọn dẹp sạch trailing whitespace và blank lines EOF (`git diff --check` PASS 100%).
  8. **Kiểm thử nghiệm thu:**
     - `pio test -e native`: **22/22 PASSED**
     - `pio test -e native-prototype`: **23/23 PASSED**
     - `pio run -e esp32-s3-devkitc-1`: **SUCCESS** (RAM 8.2%, Flash 22.2%)
     - `git diff --check`: **CLEAN (0 errors)**


## [2026-08-12T13:52:00+07:00] Tasks R1 & R6 - Fix Remediation theo QA Review Feedback (Lần 2)

- **Task ID:** R1 & R6 (Track R — Remediation S0-S1)
- **Trạng thái hiện tại:** Đang chờ QA Review (Lần 2)
- **Danh sách file đã sửa:**
  - `docs/SPRINT_0_1_LEGACY_REPLACEMENT.md` [NEW]
  - `aeroponics-firmware/include/nvs_storage.h`
  - `aeroponics-firmware/src/nvs_storage.cpp`
  - `aeroponics-firmware/include/prototype/legacy_relay/legacy_relay_profile_repository.h` [NEW]
  - `aeroponics-firmware/src/prototype/legacy_relay/legacy_relay_profile_repository.cpp` [NEW]
  - `aeroponics-firmware/include/config.h`
  - `aeroponics-firmware/include/command_manager.h`
  - `aeroponics-firmware/src/command_manager.cpp`
  - `aeroponics-firmware/src/mqtt_client.cpp`
  - `aeroponics-firmware/src/prototype/legacy_relay/integration/legacy_mqtt_gate.cpp` [NEW]
  - `aeroponics-firmware/src/integration/production_mqtt_gate.cpp`
  - `aeroponics-firmware/platformio.ini`
  - `aeroponics-firmware/test/test_prototype/test_legacy_relay.cpp`
  - `scripts/mqtt_integration_gate.py`
  - `.ai/planning/aeroponics-lean/PROGRESS.md`
  - `.ai/planning/aeroponics-lean/WALKTHROUGH_LOG.md`

- **Giải trình ngắn gọn:**
  1. **Khắc phục Lỗi 1 (HIGH — Kiến trúc / R6 blocker):** Tách file integration gate legacy thành `src/prototype/legacy_relay/integration/legacy_mqtt_gate.cpp` (chỉ compile trong environment prototype). Viết lại `production_mqtt_gate.cpp` thuần túy sử dụng Gateway Domain entities (`NodeRegistry`, `CommandManager`, `MqttClient`, `NvsStorage`), tuyệt đối không include bất kỳ header hay fake legacy relay nào.
  2. **Khắc phục Lỗi 2 (HIGH — Traceability / R6 blocker):** Hoàn thành Task R1 bằng cách tạo tài liệu versioned `docs/SPRINT_0_1_LEGACY_REPLACEMENT.md` mapping đầy đủ toàn bộ source/test/topic/schema/script legacy sang successor Sprint 1.5/2/3, quy trình cô lập/tháo gỡ, kịch bản rollback và tiêu chí nghiệm thu (VAC).
  3. **Nhận xét 1 (NvsStorage Primitive Separation):** Tách `LegacyRelayProfileRepository` thành prototype adapter riêng nằm trong `prototype/legacy_relay/`. `NvsStorage` production hiện tại là primitive NVS sạch, không kế thừa `IProfileRepository`, không chứa macro `#if defined(LEGACY_RELAY_SUPPORT)` hay biết bất kỳ cấu trúc `RelayProfile` nào.
  4. **Nhận xét 2 (CommandManager Policy & Safety Check):** Đưa cấu hình `run_lease_ms` và `max_on_duration_ms` vào `NodeLeasePolicy` có thể cấu hình theo node/treatment policy (`setNodeLeasePolicy`). Bổ sung kiểm tra kết quả `transport_->send()` trả về đủ số byte frame đã dựng, fail command nếu frame gửi không đủ.
  5. **Nhận xét 3 (MQTT Ack Semantics Correlation):** Cập nhật callback MQTT `_onMessage` để trả về status `ACCEPTED` (hoặc `QUEUED`) ngay khi nhận command override/group control. Trạng thái `RF_ACKED` chỉ được publish sau khi nhận và đối chiếu ACK từ node qua RF.
  6. **Kiểm thử nghiệm thu:**
     - `pio test -e native`: **20/20 PASSED**
     - `pio test -e native-prototype`: **23/23 PASSED**
     - `pio run -e esp32-s3-devkitc-1`: **SUCCESS** (RAM 7.8%, Flash 21.9%)
     - `pio run -e native-integration`: **SUCCESS**
     - `python3 scripts/mqtt_integration_gate.py`: **ALL PRODUCTION MQTT INTEGRATION GATES PASSED** (LWT, Heartbeat schema, Gateway Node Assignment update, ACL Denial enforcement).

## [2026-08-12T13:44:00+07:00] Task R6 - Fix Remediation (Lần 2)

- **Task ID:** R6 (Gỡ legacy runtime và regression verification sau khi successor PASS)
- **Trạng thái hiện tại:** Đang chờ QA Review (Lần 2)
- **Danh sách file đã sửa:**
  - `aeroponics-firmware/platformio.ini`
  - `aeroponics-firmware/include/config.h`
  - `aeroponics-firmware/include/mqtt_client.h`
  - `aeroponics-firmware/src/mqtt_client.cpp`
  - `aeroponics-firmware/include/nvs_storage.h`
  - `aeroponics-firmware/src/nvs_storage.cpp`
  - `aeroponics-firmware/src/group_schedule_manager.cpp`
  - `aeroponics-firmware/include/group_schedule_manager.h`
  - `aeroponics-firmware/src/main.cpp`
  - `aeroponics-firmware/src/integration/production_mqtt_gate.cpp`
  - `aeroponics-firmware/test/fakes/FakeClock.h`
  - `aeroponics-firmware/test/fakes/FakeWatchdog.h`
  - `aeroponics-firmware/test/fakes/FakeNvsBackend.h`
  - `aeroponics-firmware/test/test_production/test_production.cpp`
  - `aeroponics-firmware/test/test_prototype/test_legacy_relay.cpp`
  - `aeroponics-firmware/include/prototype/legacy_relay/legacy_relay_config.h` [NEW]
  - `aeroponics-firmware/include/prototype/legacy_relay/*` [MOVED]
  - `aeroponics-firmware/src/prototype/legacy_relay/*` [MOVED]
  - `aeroponics-firmware/test/test_prototype/fakes/*` [MOVED/NEW]
  - `.ai/planning/aeroponics-lean/PROGRESS.md`

- **Giải trình ngắn gọn:**
  1. **Khắc phục Lỗi 1 (HIGH):** Đã loại bỏ `FreeRTOSTaskRunner.cpp`, `schedule_manager.cpp`, và `relay_controller.cpp` khỏi production build gateway (`esp32-s3-devkitc-1` và default `native`). Di chuyển toàn bộ runtime và header relay cũ vào thư mục `prototype/legacy_relay/`, chỉ được biên dịch trong environment prototype riêng (`native-prototype`, `native-integration`).
  2. **Khắc phục Lỗi 2 (HIGH):** Đã dọn dẹp triệt để `include/config.h` và `include/mqtt_client.h`/`src/mqtt_client.cpp`. Di chuyển các relay constants (`RELAY_PIN_1..4`, `TOTAL_RELAYS`), legacy MQTT topics (`/command/relay/`, `/telemetry/relay/`), và legacy MQTT parsers/methods sang `prototype/legacy_relay/legacy_relay_config.h`. Production `MqttClient` hiện chỉ phục vụ Gateway Production Domain (`group`, `node`, `treatment`, `assignment`, `ack`, `snapshot`).
  3. **Khắc phục Lỗi 3 (MEDIUM):** Đã cập nhật default regression test environment `[env:native]` (`pio test -e native`) để chỉ test các production primitives, GroupScheduleManager, NodeRegistry, CommandManager, và bổ sung test kiểm tra regression không lọt legacy relay symbols vào production config. Tách bộ test relay cũ sang `test/test_prototype/test_legacy_relay.cpp` chạy riêng qua environment `[env:native-prototype]`.
  4. **Kiểm thử nghiệm thu:**
     - `pio run -e esp32-s3-devkitc-1`: **SUCCESS**
     - `pio test -e native`: **20/20 PASSED**
     - `pio test -e native-prototype`: **23/23 PASSED**
