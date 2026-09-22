# Hướng dẫn Triển khai & Vận hành MQTT: Aeroponics Farm

Tài liệu này chuẩn hóa kiến trúc kết nối MQTT giữa **ESP32 Gateway** và **NestJS Backend** trong hệ thống Docker trên VM Host.

---

## 1. Bảng quy hoạch Port (So sánh giữa 2 dự án trên cùng VM Host)

| Dự án | Dịch vụ | Port Container | Port Host (Public) | Ghi chú |
| :--- | :--- | :--- | :--- | :--- |
| **mushroom-cp** | Next.js UI | 3000 | **`6001`** | Đang chạy |
| **mushroom-cp** | NestJS Backend | 3001 | **`6002`** | Đang chạy |
| **Hạ tầng chung** | Mosquitto MQTT (`mushroom_mqtt`) | 1883 | **`10883`** | **Dùng chung cho Nấm & Khí Canh (Dual-Auth: files,http)** |
| **aeroponics** | Nginx Reverse Proxy | 80 | **`6003`** | Gom UI (`/`), REST API (`/api/`), WebSocket (`/ws`) |
| **aeroponics** | Mosquitto Standalone | 1883 | `11883` | *(Tắt mặc định, dùng `--profile standalone` khi cần rollback)* |

> **Yêu cầu mở Firewall/Security Group VM**:
> - Mở cổng **`6003/TCP`** (Truy cập Web UI + API).
> - Mở cổng **`10883/TCP`** (ESP32 Nấm & Khí Canh kết nối MQTT TCP chung).

---

## 2. Kiến trúc luồng kết nối (Hợp nhất Broker)

```text
[ESP32 Gateway (Farm)]
     │
     │ MQTT TCP (PubSubClient)
     ▼
[VM Host Public IP : 10883]
     │
     │ Docker Port Forwarding (:10883 -> :1883)
     ▼
[Docker: mushroom_mqtt:1883]  <── (Go-Auth Plugin: files,http)
     ▲                                   │
     │ Internal Docker Network           ├── 1. Khí Canh: /etc/mosquitto/passwd & acl
     │ (mushroom-network)                └── 2. Nấm: http://mushroom-backend:3001/api/mqtt/...
     │ User: aero_backend
[Docker aero_net + mushroom-network : aero_backend:3001]
```

### Nguyên tắc kỹ thuật:
1. **ESP32**: Kết nối vào `Public_IP:10883` sử dụng user `esp32_device`.
2. **Backend**: Nằm trên cả 2 mạng Docker (`aero_net` và `mushroom-network`), kết nối trực tiếp `mqtt://mushroom_mqtt:1883`, **không** đi vòng qua IP public hay port 10883.
3. **Identity Binding & Topic Isolation**:
   - Khí Canh: Topic namespace `aeroponics/#`, ACL rule `pattern write aeroponics/device/%u/...`.
   - Nấm: Topic namespace `mushroom/#`.
   - User `MQTT_USER` **bắt buộc phải bằng** `MQTT_DEVICE_ID`.

---

## 3. Cấu hình mẫu

### 3.1. File `.env` (Aeroponics)
```bash
MQTT_PORT=11883
MQTT_WS_PORT=19001
MQTT_ADMIN_USER=mqtt_admin
MQTT_ADMIN_PASS=admin_password_dev

MQTT_DEVICE_USER=esp32_device
MQTT_DEVICE_ID=esp32_device
MQTT_DEVICE_PASS=qa_local_device_password

MQTT_BACKEND_USER=aero_backend
MQTT_BACKEND_PASS=backend_password_dev

PROXY_PORT=6003
```

### 3.2. Firmware `aeroponics-firmware/include/secrets.h`
```cpp
#pragma once

#define WIFI_SSID       "YOUR_WIFI_SSID"
#define WIFI_PASS       "YOUR_WIFI_PASSWORD"

#define MQTT_HOST       "YOUR_VM_PUBLIC_IP_OR_DOMAIN"
#define MQTT_PORT       11883

#define MQTT_USER       "esp32_device"
#define MQTT_DEVICE_ID  "esp32_device"
#define MQTT_PASS       "qa_local_device_password"
```

---

## 4. Kịch bản Kiểm thử nhanh (Smoke Test)

### Bước 1: Subscribe lắng nghe lệnh gửi xuống ESP32
```bash
mosquitto_sub -h <IP_VM_HOST> -p 11883 \
  -u "esp32_device" -P "qa_local_device_password" \
  -t "aeroponics/device/esp32_device/command/#" -v
```

### Bước 2: Giả lập ESP32 publish trạng thái Heartbeat
```bash
mosquitto_pub -h <IP_VM_HOST> -p 11883 \
  -u "esp32_device" -P "qa_local_device_password" \
  -t "aeroponics/device/esp32_device/status" \
  -m '{"status":"online","uptime_s":120,"free_heap_b":180000,"ntp_synced":true,"rtc_valid":true}'
```

### Bước 3: Kiểm tra log Backend NestJS
```bash
docker logs --tail 30 aero_backend
```
*Kết quả mong đợi:*
```text
[MqttRouterService] DeviceStatus updated for gateway "esp32_device": status=online, uptime=120s
```
