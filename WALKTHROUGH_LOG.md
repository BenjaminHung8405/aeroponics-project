# Walkthrough Log

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
