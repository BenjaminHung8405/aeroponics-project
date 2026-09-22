# Sprint 3 Handoff Package: Production Architecture, Schemas & BOM

> **Document ID:** `HANDOFF-SPRINT-3-PROD-001`  
> **Version:** `1.0.0` (Track S2-E Final Deliverable)  
> **Date:** 2026-09-12  
> **Author:** Senior IoT Systems Engineer  
> **Target Audience:** Sprint 3 Frontend / Backend Engineers, QA Engineers, Hardware Assembly Team  
> **Architecture Scope:** 01 ESP32-S3 Gateway + 04 ATmega8 Nodes (Baseline IDs 1..4)
> **Critical constraint:** ATmega8 nodes are preloaded legacy devices. Source is unavailable, firmware is immutable and version is UNKNOWN. This handoff covers ESP32-side control and observed RF integration only. See [`ATMEGA8_INTEGRATION_BOUNDARY.md`](./ATMEGA8_INTEGRATION_BOUNDARY.md).

---

## 1. Executive Summary & Readiness Gate Sign-Off

Tài liệu này là gói hồ sơ kỹ thuật tổng hợp (**Technical Handoff Package**) chuyển giao từ **Sprint 2: Production Gateway & RF Integration** sang **Sprint 3: Operational Hardening, Real-time Dashboard & TimescaleDB Analytics**.

Các cổng chất lượng gateway đã được rà soát theo evidence hiện có. Không được coi host tests, simulator hoặc gateway build là bằng chứng firmware ATmega8 đã nạp đạt PASS:

```
┌────────────────────────────────────────────────────────────────────────────────────────┐
│                          PRODUCTION READINESS GATE SIGN-OFF                            │
├───────────────┬─────────────────────────────────────────────────┬────────┬─────────────┤
│ Gate ID       │ Focus Area                                      │ Status │ Verification│
├───────────────┼─────────────────────────────────────────────────┼────────┼─────────────┤
│ S2-RF-01      │ 4-Node Latency Envelope (p50<=200ms, PDR>=98%)  │ PASS   │ Bench Report│
│ S2-SECURITY-02│ Gateway/model security framing               │ GATEWAY│ Test Suite  │
│ S2-SAFETY-03  │ Remote Safe-OFF after RF loss                │ HOLD   │ Black-box QA │
│ S2-FLOW-04    │ Volumetric Confirmation, Leak/Burst Protection  │ PASS   │ Flow Matrix │
│ S2-MQTT-05    │ Normalized JSON v1.0, LWT Contract, Bounded Buf │ PASS   │ Broker Test │
│ S2-STORAGE-06 │ Zero NVS Writes in Telemetry Path, Flash Health │ PASS   │ Endurance   │
│ S2-HARDWARE-07│ Gateway adapter; ATmega8 behavior unverified     │ HOLD   │ Black-box QA │
│ S2-QUALITY-08 │ Clean Architecture, Zero Legacy Direct Relays   │ PASS   │ Arch Script │
└───────────────┴─────────────────────────────────────────────────┴────────┴─────────────┘
```

---

## 2. MQTT Topic & JSON Payload Contract v1.0

Tất cả các chủ đề MQTT đều tuân thủ phân cấp chặt chẽ theo Device ID và Schema JSON v1.0:

### 2.1. Ingestion / Publish Topics (Gateway -> Broker)

| Topic | QoS | Retain | Mô Tả Chức Năng |
|---|---|---|---|
| `aeroponics/device/{device_id}/status` | 1 | True (LWT) | Trạng thái sống còn Gateway (Online / Offline LWT). |
| `aeroponics/device/{device_id}/telemetry` | 1 | False | Dữ liệu cảm biến & FSM đã chuẩn hóa của 4 node định kỳ. |
| `aeroponics/device/{device_id}/command/{command_id}/ack` | 1 | False | Phản hồi xác nhận tiếp nhận lệnh (ACCEPTED / REJECTED). |
| `aeroponics/device/{device_id}/safety/audit` | 1 | False | Nhật ký kiểm toán an toàn (Safe-OFF triggers, Faults). |

#### A. Telemetry Schema (`.../telemetry`)
```json
{
  "node_id": 1,
  "node_timestamp_ms": 5000,
  "gateway_timestamp_ms": 5015,
  "pump_state": 1,
  "driver_feedback": 1,
  "current_ma": 1950,
  "flow_lpm": 1.85,
  "delivered_volume_ml": 450,
  "pulse_count": 83,
  "fault_code": 0,
  "fault_phrase": "FAULT_NONE",
  "fsm_state": "FLOW_CONFIRMED",
  "last_command_id": 1042
}
```

#### B. Command ACK Schema (`.../command/{command_id}/ack`)
```json
{
  "command_id": "cmd_manual_001",
  "status": "ACCEPTED",
  "node_id": 2,
  "reason": "DISPATCHED_TO_RF",
  "timestamp_ms": 1050
}
```

#### C. Gateway Status & LWT Schema (`.../status`)
```json
{
  "status": "online",
  "device_id": "esp32_gateway_01",
  "uptime_s": 86400,
  "rssi_dbm": -62,
  "free_heap_b": 248560,
  "ntp_synced": true,
  "rtc_valid": true,
  "timestamp_utc": 1789123456
}
```
*(Khi đứt nguồn hoặc mất mạng, Broker tự động phân phối payload LWT với `"status": "offline"`)*

### 2.2. Egress / Subscribe Topics (Broker -> Gateway)

| Topic | QoS | Mô Tả Chức Năng |
|---|---|---|
| `aeroponics/device/{device_id}/command/override` | 1 | Lệnh ghi đè tưới thủ công tạm thời (`MANUAL_OVERRIDE`). |
| `aeroponics/device/{device_id}/command/config/assignment` | 1 | Gán nhóm tưới cho node kèm cơ chế chuyển giao Safe-OFF. |
| `aeroponics/device/{device_id}/command/config/flow_policy` | 1 | Cấu hình ngưỡng kiểm tra dòng chảy (`min_flow`, `timeouts`). |
| `aeroponics/device/{device_id}/command/reset_fault` | 1 | Xóa chốt lỗi và phục hồi hoạt động sau can thiệp kỹ thuật. |

#### A. Command Override Payload
```json
{
  "command_id": "cmd_manual_001",
  "node_id": 1,
  "desired_state": "ON",
  "run_lease_ms": 20000,
  "override_duration_ms": 60000,
  "source": "MANUAL_OVERRIDE"
}
```

---

## 3. Hardware Pinout & Wiring Specifications

### 3.1. ESP32-S3 Gateway Pinout Mapping

```
┌────────────────────────────────────────────────────────────────────────┐
│                        ESP32-S3 GATEWAY PINOUT                         │
├───────────┬──────────────┬─────────────┬───────────────────────────────┤
│ Pin / Net │ Peripheral   │ Direction   │ Signal & Connection           │
├───────────┼──────────────┼─────────────┼───────────────────────────────┤
│ GPIO 17   │ UART1 TX     │ Output      │ E32 LoRa RXD (3.3V Direct)    │
│ GPIO 18   │ UART1 RX     │ Input       │ E32 LoRa TXD (3.3V Direct)    │
│ GPIO 21   │ GPIO Output  │ Output      │ E32 M0 Mode Select Pin        │
│ GPIO 47   │ GPIO Output  │ Output      │ E32 M1 Mode Select Pin        │
│ GPIO 48   │ GPIO Input   │ Input       │ E32 AUX Ready / Clear-to-Send │
│ GPIO 8    │ I2C0 SDA     │ Bi-dir      │ DS3231 RTC SDA (Pullup 4.7k)  │
│ GPIO 9    │ I2C0 SCL     │ Output      │ DS3231 RTC SCL (Pullup 4.7k)  │
│ 5V EXT    │ Power Rail   │ Power       │ Step-down buck 12V -> 5V 2A   │
│ GND       │ Power Return │ Ground      │ Star Grounding with Shield    │
└───────────┴──────────────┴─────────────┴───────────────────────────────┘
```

### 3.2. ATmega8 Legacy Node Physical Pinout (Firmware Use Unverified)

```
┌────────────────────────────────────────────────────────────────────────┐
│                         ATMEGA8 REMOTE NODE PINOUT                     │
├───────────┬──────────────┬─────────────┬───────────────────────────────┤
│ Pin / Net │ Peripheral   │ Direction   │ Signal & Connection           │
├───────────┼──────────────┼─────────────┼───────────────────────────────┤
│ PD0 (RXD) │ USART RX     │ Input       │ E32 LoRa TXD (Via Resistor Div│
│ PD1 (TXD) │ USART TX     │ Output      │ E32 LoRa RXD                  │
│ PD2 (INT0)│ Ext Interrupt│ Input       │ OF06ZAT Flow Sensor Pulses    │
│ PD3 (INT1)│ Ext Interrupt│ Input       │ E32 AUX Interrupt (Wakeup)    │
│ PB1 (OC1A)│ GPIO Output  │ Output      │ Gate Driver MOSFET (Low=Safe) │
│ PB0       │ GPIO Input   │ Input       │ Optocoupler Driver Gate Sense │
│ PC0 (ADC0)│ ADC Channel 0│ Analog In   │ ACS712-05B Current Sense VIOUT│
│ PB2       │ GPIO Output  │ Output      │ E32 M0 Mode Control           │
│ PB3       │ GPIO Output  │ Output      │ E32 M1 Mode Control           │
│ AREF      │ Analog Ref   │ Input       │ 5.0V Precision Rail + 100nF   │
└───────────┴──────────────┴─────────────┴───────────────────────────────┘
```

---

## 4. Bill of Materials (BOM) & Component Sourcing

| TT | Linh Kiện | Mã Hiệu / Model | Số Lượng / Hệ Thống | Chức Năng |
|---|---|---|---|---|
| 1 | Gateway Controller | ESP32-S3-DevKitC-1-N8 (8MB Flash) | 1 | Điều phối mạng RF, MQTT client, TimescaleDB connector |
| 2 | Remote Node MCU | Microchip ATmega8A-PU (DIP-28 / TQFP) | 4 | FSM điều khiển tự chủ độc lập tại từng vỉ phun |
| 3 | RF LoRa Transceiver | Ebyte E32-433T20D (SX1278 433 MHz) | 5 (1 GW + 4 Nodes) | Kênh sóng vô tuyến bán công nghiệp tầm phủ 1000m |
| 4 | Cảm Biến Lưu Lượng | OF06ZAT (Bánh răng hình oval) | 4 | Đo lưu lượng thể tích chính xác cao ($1,200\text{ p/L}$) |
| 5 | Cảm Biến Dòng Điện | Allegro ACS712ELCTR-05B-T | 4 | Phát hiện kẹt động cơ, đứt dây tải ($185\text{ mV/A}$) |
| 6 | Transistor Chuyển Mạch| N-Channel MOSFET LR7843 (30V, 161A) | 4 | Đóng cắt bơm màng áp cao 12VDC |
| 7 | Cách Ly Quang Học | EL817 / PC817 Optocoupler | 4 | Đo kiểm phản hồi mức logic cổng Gate MOSFET |
| 8 | Đồng Hồ Thời Gian Thực| DS3231M Precision I2C RTC | 1 (Gateway) | Cung cấp mốc giờ chuẩn độ chính xác $\pm 2\text{ppm}$ |
| 9 | Nguồn Điện | AC/DC MeanWell LRS-150-12 (12V 12.5A) | 1 | Nguồn cấp động lực bơm và hạ áp hệ thống |
| 10| Hạ Áp DC-DC | LM2596S / MP1584 Buck Converter | 5 | Hạ áp 12V -> 5V cấp MCU và mô-đun RF |

---

## 5. Gateway Configuration Keys Catalog (NVS Storage)

Gateway lưu trữ các cấu hình tĩnh và bán tĩnh trong ESP32 Non-Volatile Storage (NVS) phân vùng `nvs`:

```
┌────────────────────────────────────────────────────────────────────────────────────────┐
│                              GATEWAY CONFIGURATION KEYS                                │
├─────────────────┬──────────┬───────────────┬───────────────────────────────────────────┤
│ Key Name        │ Type     │ Default Value │ Description & Validation Rules            │
├─────────────────┼──────────┼───────────────┼───────────────────────────────────────────┤
│ `rf_protocol`   │ String   │ "AGU_LEGACY_SCI" │ Southbound AGU-Aeroponics; không dùng HMAC. │
│ `node_mask`     │ uint16_t │ 0x000F        │ Mặt nạ node hoạt động (Node 1..4 = bit 0..3)│
│ `stale_ms`      │ uint32_t │ 15000         │ Ngưỡng thời gian mất tin hiệu coi là STALE│
│ `retry_int_ms`  │ uint32_t │ 1000          │ Khoảng thời gian giữa các lần thử lại RF  │
│ `max_retries`   │ uint8_t  │ 3             │ Số lần thử lại tối đa trước khi khóa lỗi  │
│ `mqtt_host`     │ String   │ "127.0.0.1"   │ Địa chỉ IP / hostname của Mosquitto broker│
│ `mqtt_port`     │ uint16_t │ 1883          │ Cổng kết nối MQTT (1883 no-TLS / 8883 TLS)│
│ `mqtt_dev_id`   │ String   │ "esp32_gw_01" │ Định danh Gateway trong chuỗi topic       │
│ `mqtt_user`     │ String   │ "gw_user"     │ Tài khoản xác thực MQTT                   │
│ `mqtt_pass`     │ String   │ "gw_secret"   │ Mật khẩu xác thực MQTT                    │
└─────────────────┴──────────┴───────────────┴───────────────────────────────────────────┘
```

> [!NOTE]
> Để bảo vệ tuổi thọ chip Flash (NVS Endurance Gate), **tuyệt đối không ghi telemetry hoặc trạng thái FSM tức thời vào NVS trong vòng lặp runtime**. Chỉ các sự kiện thay đổi nhóm tưới có xác thực hoặc thay đổi cấu hình qua MQTT mới được phép kích hoạt ghi NVS.

---

## 6. Hướng Dẫn Bàn Giao Triển Khai (Sprint 3 Roadmap)

1. **Backend Integration:**
   - Kết nối Mosquitto MQTT vào TimescaleDB Ingestion Service.
   - Sử dụng topic `aeroponics/device/+/telemetry` để điền bảng `flow_events` và `pump_feedback_events`.
2. **Dashboard UI:**
   - Lắng nghe topic `.../status` để hiển thị cờ Online/Offline thời gian thực.
   - Lắng nghe `.../safety/audit` để bật chuông cảnh báo khi phát hiện rò rỉ hoặc nghẹt béc.
3. **Firmware Freeze:**
   - Khóa phiên bản firmware `v1.0.0-sprint2` cho ESP32-S3 và ATmega8.
   - Sử dụng bộ giả lập `NodeSimulatorHarness` làm chuẩn kiểm định cho các kịch bản kiểm thử tự động trên CI.
