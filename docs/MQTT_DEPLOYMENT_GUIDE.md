# Hướng dẫn Triển khai & Vận hành MQTT: Aeroponics Farm

Tài liệu này chuẩn hóa kiến trúc kết nối MQTT giữa **ESP32 Gateway** và **NestJS Backend** trong hệ thống Docker trên VM Host.

---

## 1. Bảng quy hoạch Port (So sánh giữa 2 dự án trên cùng VM Host)

| Dự án | Dịch vụ | Port Container | Port Host (Public) | Ghi chú |
| :--- | :--- | :--- | :--- | :--- |
| **mushroom-cp** | Next.js UI | 3000 | **`6001`** | Đang chạy |
| **mushroom-cp** | NestJS Backend | 3001 | **`6002`** | Đang chạy |
| **mushroom-cp** | Mosquitto MQTT | 1883 | **`10883`** | MQTT TCP |
| **aeroponics** | Nginx Reverse Proxy | 80 | **`6003`** | Gom UI (`/`), REST API (`/api/`), WebSocket (`/ws`) |
| **aeroponics** | Mosquitto MQTT | 1883 | **`11883`** | **Không đụng với 10883 của mushroom-cp** |
| **aeroponics** | Mosquitto WebSocket | 9001 | **`19001`** | Cho Web Dashboard |

> **Yêu cầu mở Firewall/Security Group VM**:
> - Mở cổng **`6003/TCP`** (Truy cập Web UI + API).
> - Mở cổng **`11883/TCP`** (ESP32 kết nối MQTT TCP).

---

## 2. Kiến trúc luồng kết nối

```text
[ESP32 Gateway (Farm)]
     │
     │ MQTT TCP (PubSubClient)
     ▼
[VM Host Public IP : 11883]
     │
     │ Docker Port Forwarding (:11883 -> :1883)
     ▼
[Docker aero_net : aero_mosquitto:1883]
     ▲
     │ Internal Docker DNS (mqtt://mosquitto:1883)
     │ User: aero_backend
[Docker aero_net : aero_backend:3001]
```

### Nguyên tắc kỹ thuật:
1. **ESP32**: Kết nối vào `Public_IP:11883` sử dụng user `esp32_device`.
2. **Backend**: Nằm cùng mạng Docker `aero_net`, kết nối trực tiếp `mqtt://mosquitto:1883`, **không** đi vòng qua IP public hay port 11883.
3. **Identity Binding**: Mosquitto ACL áp dụng rule `pattern write aeroponics/device/%u/...`. Do đó `MQTT_USER` **bắt buộc phải bằng** `MQTT_DEVICE_ID`.

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
