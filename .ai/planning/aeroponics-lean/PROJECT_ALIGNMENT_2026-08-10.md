# Điều chỉnh phạm vi — 12 cụm bơm RF, 12 van lưu lượng và mùa vụ 120 ngày

> **Ngày:** 2026-08-10  
> **Trạng thái:** Bắt buộc áp dụng cho mọi công việc chưa thực hiện kể từ Sprint 2; đã cập nhật quyết định phần cứng/nghiệp vụ ngày 2026-08-10.  
> **Thứ tự ưu tiên:** Tài liệu này thay thế nội dung mâu thuẫn trong kế hoạch cũ. Lịch sử Sprint 0/Sprint 1 trong `PROGRESS.md` được giữ để kiểm toán, không phải bằng chứng thiết kế phần cứng cuối cùng.

## 1. Ground truth đã xác nhận

| Hạng mục | Yêu cầu đúng |
|---|---|
| Bộ điều khiển trung tâm | Một ESP32 làm gateway Wi-Fi/MQTT và gateway **UART over RF 433 MHz**. |
| Cụm chấp hành | 12 module RF độc lập, tương ứng 12 cụm bơm. ESP32 **không điều khiển 4 relay GPIO trực tiếp** trong production. |
| Timer nghiệm thức | Tối đa 4 group timer hoạt động đồng thời. Treatment/profile là dữ liệu cấu hình, tái sử dụng và cá nhân hoá; **không hard-code** M1–M4 trong firmware. |
| Gán cụm vào group | Người dùng gán từng node trong 12 module vào group bất kỳ; mapping có version/lịch sử theo mùa vụ, không cố định 3 node/group. |
| Quan sát | Hiển thị ON/OFF thực tế của 12 pump, feedback điều khiển và lưu lượng định lượng của 12 valve. |
| Lưu trữ | Lưu sự kiện điều khiển, xác nhận RF, dòng chảy và dữ liệu cần thiết xuyên suốt mùa vụ tối thiểu 120 ngày. |
| Tuya PH-W218 | Chỉ đo/lưu theo yêu cầu ở cuối vụ hoặc do người vận hành kích hoạt; **không poll 10 giây liên tục**. |
| Khung giờ | Ngày: 06:00–17:59; Đêm: 18:00–05:59, timezone `Asia/Ho_Chi_Minh` (UTC+7, không DST). |

### Bảng nghiệm thức được cung cấp

| Nghiệm thức | ON ngày | OFF ngày | ON đêm | OFF đêm |
|---|---:|---:|---:|---:|
| M1 | 30 giây | 10 phút (600 s) | 30 giây | 30 phút (1800 s) |
| M2 | 30 giây | 15 phút (900 s) | 30 giây | 30 phút (1800 s) |
| M3 | 30 giây | 20 phút (1200 s) | 30 giây | 30 phút (1800 s) |

**Quyết định đã chốt:** M1–M3 chỉ là preset mẫu ban đầu. Người dùng tạo, sửa, nhân bản, phiên bản hoá và tái sử dụng treatment tuỳ ý. Một group chỉ chạy khi được gán một version treatment đã phát hành (`PUBLISHED`); group không gán treatment là `UNASSIGNED` và không tự chạy. Không tồn tại M4 hard-code.

## 2. Sai lệch của kế hoạch cũ

1. Mô hình `4 relay = 4 giàn/khu` không đủ: cần **4 timer group → 12 pump node → 12 valve/flow channel**.
2. `RELAY_PIN_1..4`/`RelayController` chỉ phù hợp rig prototype/direct relay; production cần `RfPumpGateway`/`PumpNodeController` và feedback thật từ node.
3. Telemetry chỉ khi đổi pha không chứng minh trạng thái 12 pump và dòng chảy liên tục: cần event, telemetry định kỳ, ACK/NACK và stale detection.
4. `relay_events` thiếu pump command/ack, reported state, valve flow và mapping group–node.
5. Retention 90 ngày mâu thuẫn mùa vụ 120 ngày.
6. Tuya poll 10 giây liên tục không phù hợp yêu cầu đo cuối vụ.
7. Dashboard 4 relay cards không đáp ứng yêu cầu 12 pump ON/OFF + 12 valve flow.

## 3. Kiến trúc mục tiêu

```text
Dashboard / NestJS / TimescaleDB
              │ MQTT over Wi-Fi
              ▼
ESP32 gateway
  ├─ ScheduleManager: 4 treatment timer groups
  ├─ Node registry: group ↔ pump node 1..12 ↔ valve/flow channel 1..12
  ├─ RF transport: UART framing ↔ transceiver 433 MHz
  └─ Command manager: sequence, ACK/NACK, retry, timeout, stale/fault detection
              │ RF 433 MHz
              ▼
12 remote pump nodes: pump actuator + valve state/flow measurement
```

### Ràng buộc RF 433 MHz bắt buộc

- Mỗi node có `node_id` duy nhất 1–12; không broadcast lệnh pump thông thường.
- UART-over-RF frame có `protocol_version`, `message_type`, `node_id`, `sequence`, `payload_length`, `payload`, `CRC-16`.
- Lệnh pump phải yêu cầu ACK cùng `sequence`; retry có timeout/backoff/giới hạn. Kết quả: `confirmed`, `timeout`, `nack`, hoặc `transport_error`.
- Node gửi heartbeat/telemetry định kỳ; gateway gắn `STALE` khi quá ngưỡng. UI không hiển thị stale như trạng thái bình thường.
- Thiết kế half-duplex/collision dùng polling tuần tự hoặc time slot do gateway điều phối; bench test packet-loss và latency cho 12 node.
- Khi gateway boot, mất RF, RTC không hợp lệ hoặc node không ACK, mặc định fail-safe là **OFF**, trừ khi có phê duyệt kỹ thuật khác.
- Kiểm tra quy định RF 433 MHz/công suất áp dụng tại nơi triển khai trước khi lắp đặt.

### Contract group–node

- `timer_group_id` 1–4 có `treatment_version_id`, timezone, trạng thái `UNASSIGNED | ACTIVE | PAUSED | ENDED`. Treatment không bị ràng buộc tên M1–M4.
- `treatment` có tên, mô tả, owner, schedule ngày/đêm, lifecycle `DRAFT | PUBLISHED | ARCHIVED`, version immutable sau publish và khả năng clone để cá nhân hoá. Việc sửa treatment đang dùng trong mùa vụ phải tạo version mới.
- Group có nhiều node; scheduler phát lệnh theo group nhưng log kết quả **từng node**. Một node chỉ thuộc tối đa một group active trong cùng thời điểm, nhưng có thể chuyển group qua assignment có `effective_from`/`effective_to`.
- Node lưu `desired_pump_state`, `reported_pump_state`, `pump_feedback_state`, `last_ack_at`, `last_telemetry_at`, `health_status`.
- Mỗi node dùng flow sensor định lượng tối đa 6 L/min, lưu `flow_lpm`, tổng tích luỹ `delivered_volume_l`, số xung raw, cửa sổ đo và calibration version. Không hạ cấp thành `flow_state` trừ khi cảm biến thực tế không thể đo.

### Xác nhận bơm/tưới hai lớp

ACK RF chỉ xác nhận node đã nhận/xử lý lệnh, **không chứng minh bơm đã chạy hoặc nước đã đến cây**. Node phải gửi cả hai lớp feedback:

1. **Pump feedback:** trạng thái driver/relay thực tế (`pump_feedback_state`) và, nếu phần cứng cho phép, feedback điện áp hoặc dòng tải bơm.
2. **Water-delivery feedback:** flow sensor, đo `flow_lpm` và tích luỹ `delivered_volume_l` trong lúc lệnh ON.

Gateway đánh giá kết quả lệnh theo chuỗi: `RF_ACKED` → `PUMP_FEEDBACK_ON` → `FLOW_CONFIRMED`. Nếu lệnh ON đã ACK nhưng trong `FLOW_START_TIMEOUT_S` chưa đạt ngưỡng lưu lượng đã calibrate, node phải phát `NO_FLOW_FAULT`; UI cảnh báo rõ “bơm có lệnh ON nhưng không xác nhận được tưới”. Nếu bơm OFF nhưng flow vẫn vượt ngưỡng, phát `UNEXPECTED_FLOW_FAULT`. Không tự retry vô hạn khi có fault; ghi event, dừng/giữ fail-safe OFF theo policy được phê duyệt.

### Đo lưu lượng tối đa 6 L/min

- Đọc xung bằng GPIO interrupt/counter phần cứng tại node, không polling trong task timer; chuyển xung sang L/min bằng `pulses_per_litre` theo calibration thực nghiệm cho **từng node/sensor**.
- Báo cáo telemetry mỗi 5–10 giây khi OFF và cửa sổ 1–2 giây khi ON/fault; gateway lưu event khi đổi trạng thái và bản ghi định kỳ. Chu kỳ cuối cùng cần sizing sau bench test RF và dung lượng DB.
- Lưu `sample_window_ms`, `pulse_count`, `pulses_per_litre`, `flow_lpm`, `delivered_volume_l`, `sensor_status`, `quality_flag`; hỗ trợ phát hiện đứt dây, zero-flow và giá trị vượt thang 6 L/min.
- Ngưỡng `min_flow_lpm`, `max_flow_lpm`, `flow_start_timeout_s` là cấu hình riêng cho node/treatment, không hard-code toàn hệ thống.

### Lựa chọn RF/UART — kết quả rà soát và hướng quyết định

- Không tìm thấy module RF 433 MHz, UART RF framing, ACK RF hoặc driver flow sensor hiện hữu trong `aeroponics-project`.
- `mushroom-cp` cũng không có triển khai RF/flow. Có thể tái sử dụng **pattern**, không tái sử dụng driver: MQTT command/ACK, NVS validation, watchdog, telemetry queue và đồng bộ truy cập Serial.
- Do chưa có module cụ thể, không khoá baud rate hay pinout trong source. Tạo `RfTransport` interface và hardware adapter riêng; debug USB Serial phải tách khỏi UART nối RF.
- Khuyến nghị proof-of-concept: chọn một cặp transceiver **433 MHz LoRa UART có firmware transparent**, chẳng hạn dòng Ebyte E32/E220 bản 433 MHz, để giảm công sức PHY. Dù vậy, phải kiểm tra datasheet, độ phủ thực địa, duty-cycle/công suất và quy định tần số trước khi mua số lượng 13 bộ; không coi đây là quyết định BOM cuối cùng.
- Protocol ứng dụng ACK/sequence/CRC vẫn bắt buộc ngay cả khi transceiver có UART transparent, vì transparent mode không cung cấp end-to-end confirmation cho pump/flow.

## 4. Data model, retention và audit

| Thực thể | Mục đích |
|---|---|
| `seasons` | Mã/tên mùa vụ, cây trồng, thời gian bắt đầu/kết thúc, timezone, trạng thái. |
| `treatments`, `treatment_versions`, `timer_groups` | Treatment người dùng tạo/tái sử dụng/cá nhân hoá, version immutable và tối đa 4 group active. |
| `pump_nodes`, `group_assignments` | 12 node RF và lịch sử group mapping hiệu lực theo thời gian. |
| `pump_command_events` | Command/ACK/NACK/retry/timeout, RF sequence. |
| `pump_state_events` | Desired/reported state, nguồn schedule/manual/fail-safe, lý do. |
| `pump_feedback_events` | Feedback driver/relay/dòng tải từ node, tách khỏi lệnh và ACK RF. |
| `flow_readings` | `node_id`, xung thô, cửa sổ mẫu, hệ số calibration, `flow_lpm`, `delivered_volume_l`, chất lượng dữ liệu, timestamp node/gateway. |
| `tuya_measurement_sessions`, `sensor_readings` | Phiên đo cuối vụ/on-demand, người kích hoạt và readings. |

- Event/reading phải có `season_id`; timestamp lưu `TIMESTAMPTZ` UTC, UI hiển thị UTC+7.
- Không bật retention 90 ngày. Giữ toàn bộ mùa vụ 120 ngày + ít nhất 30 ngày đối soát; chỉ xóa sau nghiệm thu và backup được xác minh restore.
- Backup PostgreSQL hằng ngày, theo dõi dung lượng đĩa và test restore trước mùa vụ.
- Mọi thay đổi schedule/treatment/group assignment/calibration phải audit user, thời điểm, trước/sau và lý do; không sửa event thô.

## 5. Điều chỉnh bắt buộc theo sprint

### Sprint 1 — Core Edge

- Giữ boot-safe, DS3231, NVS, WDT; thay production actuator abstraction từ GPIO direct relay sang RF node controller.
- M1 `(30,600,30,1800)`, M2 `(30,900,30,1800)`, M3 `(30,1200,30,1800)` là seed presets ở backend/NVS provisioning; không hard-code thành giới hạn firmware. Group chưa gán published treatment không chạy auto-timer.
- Scheduler chạy mỗi group và fan-out lệnh tới node được user gán, thay vì 1 task/1 relay GPIO; node phải báo pump feedback và flow telemetry.

### Sprint 2 — MQTT **và RF gateway**

- Thêm `rf_transport`, `rf_frame_codec`, `pump_node_controller`, `node_registry`, `flow_meter`; test frame/CRC/retry/ACK/stale state, no-flow, unexpected-flow và calibration conversion.
- MQTT có group summary và snapshot/event cho từng node 1–12: desired/reported pump state, pump feedback, `flow_lpm`, delivered volume, RF sequence, ACK/result, link quality nếu có.
- Backend chỉ báo “đã thực thi” sau RF result, không chỉ sau MQTT publish.

### Sprint 3 — Backend / TimescaleDB

- Migration/schema bổ sung các thực thể ở Mục 4; domain 4 relay cũ chỉ là prototype hoặc migration source.
- API quản lý season, treatment/version/clone, group assignment động, 12 node state/history, pump feedback, flow/calibration, command/fault history; query mặc định lọc `season_id`.
- Tuya thành on-demand measurement service có audit session; không khởi tạo polling 10 giây.

### Sprint 4 — Dashboard

- Hiển thị 4 group/treatment cards **và** ma trận 12 pump/valve cards.
- Node card: desired/reported ON/OFF, pump feedback, ACK/timeout/fault, `last_seen`, flow L/min, delivered volume, timestamp, stale indicator.
- Lọc mùa vụ; biểu đồ pump/flow từ 24 h tới toàn vụ; export CSV theo `season_id`, group, node.
- Tuya là phiên đo cuối vụ với timestamp, không phải gauge realtime luôn có dữ liệu.

## 6. Cổng nghiệm thu mới trước triển khai RF

- [ ] Sơ đồ phần cứng: module RF, UART pin/baud, nguồn, driver pump, loại cảm biến flow, logic active level.
- [ ] Bảng inventory 12 `node_id` → pump → valve/flow sensor; UI/API cho phép user gán node động vào một trong tối đa 4 group active.
- [ ] Có seed M1–M3 và kiểm thử tạo/clone/publish/version treatment tuỳ ý; không có M4 hard-code.
- [ ] Xác minh feedback bơm và flow sensor max 6 L/min: calibration từng node, no-flow/abnormal-flow fault, delivered-volume correctness.
- [ ] Chọn module RF sau POC, khoá BOM/UART baud/pinout theo datasheet và kết quả field test.
- [ ] Protocol RF versioned, test parser/CRC/duplicate sequence/ACK retry/timeout/boot fail-safe.
- [ ] Bench test 12 node: packet loss, command latency, telemetry freshness, power-cycle recovery, emergency-off.
- [ ] Migration season + retention 120 ngày được review, backup/restore PASS.
- [ ] UI prototype hiển thị đúng 12 pump và 12 valve/flow, gồm stale/fault.

## 7. Thông tin cần xác nhận

1. Chốt sau POC: module/chip RF 433 MHz, anten, baud rate, UART pinout, nguồn và topology triển khai.
2. Chốt loại/cổng xung của flow sensor max 6 L/min và quy trình calibration (`pulses_per_litre`) cho từng node.
3. Chốt policy khi phát hiện `NO_FLOW_FAULT` / `UNEXPECTED_FLOW_FAULT`: tắt node riêng lẻ hay dừng toàn group, escalation và điều kiện reset fault.
