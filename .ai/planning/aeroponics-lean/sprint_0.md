# Sprint 0: Infrastructure Setup

> **⚠️ LỊCH SỬ / SUPERSEDED — KHÔNG DÙNG LÀM PRODUCTION DESIGN**
> File này ghi lại kế hoạch và schema cho **prototype 4-relay ban đầu** (Sprint 0, hoàn thành 2026-07). Toàn bộ schema `relay_events`, `relay_profiles`, Tuya poll 10 giây và retention 90 ngày trong file này **đã bị thay thế** bởi kiến trúc production RF 12 node.
> - **Để migration production:** Xem [`PROJECT_ALIGNMENT_2026-08-10.md`](./PROJECT_ALIGNMENT_2026-08-10.md) và [`sprint_2.md`](./sprint_2.md).
> - **Giá trị của file này:** Lịch sử audit và reference cho việc migrate schema từ 4-relay sang 12-node RF.
> - **QA gate hiện hành:** Xem [`sprint_1_5.md`](./sprint_1_5.md) và [`PROGRESS.md`](./PROGRESS.md).

> **Phụ thuộc:** Không có (Sprint nền tảng, chạy đầu tiên).  
> **Output bàn giao:** Stack hạ tầng (`timescaledb` + `mosquitto` + `aero-backend` NestJS placeholder) khởi động bằng **1 lệnh** `docker compose up -d`. Health-check xanh. Volume persistent. Developer có thể Flash firmware ESP32 ngay sau đó.

> **Khác biệt so với `mushroom-cp`:**
> - ❌ Bỏ `influxdb` container
> - ❌ Bỏ Redis (mushroom-cp cũng không có Redis)
> - ✅ Chỉ 3 containers: `timescaledb` + `mosquitto` + `aero-backend`
> - ✅ `database/schema.sql` viết riêng cho domain Aeroponics (4 relay + sensor readings)
> - ✅ Backend port `3001` (NestJS standard), không phải `8000`

---

## 1. PHẠM VI & MỤC TIÊU

### 1.1 Files bị tác động

| File | Loại | Mô tả |
|---|---|---|
| `docker-compose.yml` | Infra | 3 services: timescaledb + mosquitto + aero-backend |
| `.env.example` | Config | Template biến môi trường |
| `.env` | Config | Secret thật — gitignore |
| `.gitignore` | Config | `.env`, `mosquitto/data/`, `mosquitto/config/passwd` |
| `database/schema.sql` | DB | TimescaleDB init: extension, tables, hypertables, indexes |
| `mosquitto/config/mosquitto.conf` | Broker | Cấu hình Mosquitto production |
| `mosquitto/config/passwd` | Broker | Generated, gitignore |
| `mosquitto/config/acl` | Broker | ACL phân quyền |
| `aeroponics-backend/Dockerfile` | Backend | Multi-stage Node.js build |
| `aeroponics-backend/package.json` | Backend | NestJS dependencies (pnpm) |
| `scripts/setup.sh` | DevOps | 1-click setup |
| `scripts/health-check.sh` | DevOps | Kiểm tra services |
| `docs/ARCHITECTURE.md` | Docs | Sơ đồ kiến trúc |
| `docs/HARDWARE_PINOUT.md` | Docs | GPIO ESP32-S3 |
| `docs/MQTT_TOPICS.md` | Docs | MQTT topic schema |
| `docs/TUYA_PH_W218_SPEC.md` | Docs | DP Code mapping |

### 1.2 Mục tiêu Sprint 0

- [ ] `docker compose up -d` khởi động 3 services không lỗi.
- [ ] TimescaleDB healthy: extension enable, 3 tables tồn tại, relay_events + sensor_readings là hypertable.
- [ ] Mosquitto từ chối anonymous, chấp nhận đúng user/pass.
- [ ] NestJS Backend placeholder `/health` trả về 200 OK.
- [ ] Port exposure đúng: chỉ 1883, 9001, 3001 ra host. TimescaleDB port 5432 nội bộ only.
- [ ] Volume persistent sau stop + rm + re-create containers.
- [ ] `scripts/setup.sh` chạy idempotent trên macOS zsh và Ubuntu 22.04.
- [ ] Không có `.env`, `passwd` trong git tracking.

---

## 2. KIẾN TRÚC & LUỒNG DỮ LIỆU

### 2.1 Docker Topology

```
HOST MACHINE
│
├── Port 1883 ──────────────────▶ [mosquitto]     (MQTT TCP)
├── Port 9001 ──────────────────▶ [mosquitto]     (MQTT WebSocket)
├── Port 3001 ──────────────────▶ [aero-backend]  (NestJS REST + WebSocket)
│
│   ┌──────── Docker Network: aero_net (bridge) ────────┐
│   │                                                    │
│   │  [mosquitto]  ◀── pub/sub ──▶ [aero-backend]     │
│   │                                     │              │
│   │                              TypeORM│              │
│   │                                     ▼              │
│   │                           [timescaledb] :5432      │
│   │                           (internal only)          │
│   └────────────────────────────────────────────────────┘
│
└── [ESP32-S3] ──── WiFi ────▶ Port 1883 [mosquitto]
```

### 2.2 Service Startup Dependency Chain

```
[docker compose up -d]
        │
        ├──▶ [timescaledb]   (no deps)
        │       └── healthcheck: pg_isready
        │
        ├──▶ [mosquitto]     (no deps)
        │       └── healthcheck: mosquitto_pub ping
        │
        └──▶ [aero-backend]  depends_on: timescaledb(healthy) + mosquitto(healthy)
                └── healthcheck: curl http://localhost:3001/health
```

---

## 3. PHÂN RÃ CHI TIẾT TÁC VỤ

### TRACK A — Docker Compose

---

#### Task A-1: `docker-compose.yml`

```yaml
name: aeroponics

services:
  timescaledb:
    image: timescale/timescaledb:latest-pg15
    container_name: aero_timescaledb
    restart: unless-stopped
    environment:
      POSTGRES_USER: ${DB_USER}
      POSTGRES_PASSWORD: ${DB_PASS}
      POSTGRES_DB: ${DB_NAME:-aeroponics}
    volumes:
      - timescale_data:/var/lib/postgresql/data
      - ./database/schema.sql:/docker-entrypoint-initdb.d/init.sql:ro
    networks:
      - aero_net
    healthcheck:
      test: ["CMD-SHELL", "pg_isready -U ${DB_USER} -d ${DB_NAME:-aeroponics}"]
      interval: 10s
      timeout: 5s
      retries: 5
      start_period: 20s
    # NOTE: Port 5432 KHÔNG expose ra host — internal only

  mosquitto:
    image: eclipse-mosquitto:2.0
    container_name: aero_mosquitto
    restart: unless-stopped
    ports:
      - "${MQTT_PORT:-1883}:1883"
      - "${MQTT_WS_PORT:-9001}:9001"
    volumes:
      - ./mosquitto/config/mosquitto.conf:/mosquitto/config/mosquitto.conf:ro
      - ./mosquitto/config/passwd:/mosquitto/config/passwd:ro
      - ./mosquitto/config/acl:/mosquitto/config/acl:ro
      - mosquitto_data:/mosquitto/data
    networks:
      - aero_net
    healthcheck:
      test: ["CMD-SHELL", "mosquitto_pub -h localhost -p 1883 -u ${MQTT_ADMIN_USER} -P ${MQTT_ADMIN_PASS} -t '$$SYS/health' -m 'ping' -q 0 2>/dev/null && echo OK"]
      interval: 15s
      timeout: 5s
      retries: 5
      start_period: 10s

  aero-backend:
    image: aeroponics/backend:latest
    container_name: aero_backend
    build:
      context: ./aeroponics-backend
      dockerfile: Dockerfile
    restart: unless-stopped
    ports:
      - "${BACKEND_PORT:-3001}:3001"
    environment:
      NODE_ENV: production
      DATABASE_URL: postgresql://${DB_USER}:${DB_PASS}@timescaledb:5432/${DB_NAME:-aeroponics}
      MQTT_HOST: mosquitto
      MQTT_PORT: 1883
      MQTT_USERNAME: ${MQTT_BACKEND_USER}
      MQTT_PASSWORD: ${MQTT_BACKEND_PASS}
      JWT_SECRET: ${JWT_SECRET}
      TUYA_DEVICE_IP: ${TUYA_DEVICE_IP}
      TUYA_DEVICE_ID: ${TUYA_DEVICE_ID}
      TUYA_LOCAL_KEY: ${TUYA_LOCAL_KEY}
      TUYA_SENSOR_ID: ${TUYA_SENSOR_ID:-ph-w218-01}
      TUYA_POLL_INTERVAL_MS: ${TUYA_POLL_INTERVAL_MS:-10000}
      PORT: 3001
    depends_on:
      timescaledb:
        condition: service_healthy
      mosquitto:
        condition: service_healthy
    networks:
      - aero_net
    healthcheck:
      test: ["CMD-SHELL", "curl -f http://localhost:3001/health || exit 1"]
      interval: 20s
      timeout: 5s
      retries: 5
      start_period: 60s

networks:
  aero_net:
    driver: bridge
    name: aero_net

volumes:
  timescale_data:
    name: aero_timescale_data
  mosquitto_data:
    name: aero_mosquitto_data
```

---

### TRACK B — TimescaleDB Schema

---

#### Task B-1: `database/schema.sql`

> Tham khảo pattern từ `mushroom-cp/database/schema.sql`. Giữ nguyên cấu trúc comment và conventions.

```sql
-- Kích hoạt extension TimescaleDB
CREATE EXTENSION IF NOT EXISTS timescaledb;

-- ============================================================================
-- PHẦN 1: BẢNG QUAN HỆ (REGULAR POSTGRESQL TABLES)
-- ============================================================================

-- 1. Device registry: ESP32 identity
CREATE TABLE IF NOT EXISTS devices (
    device_id     VARCHAR(64) PRIMARY KEY,       -- MQTT clientId, phải unique
    display_name  VARCHAR(100),
    mqtt_username VARCHAR(64) NOT NULL UNIQUE,
    enabled       BOOLEAN NOT NULL DEFAULT TRUE,
    created_at    TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    last_seen_at  TIMESTAMPTZ,
    updated_at    TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- 2. Relay configuration (4 relays, mỗi relay có profile Ngày/Đêm)
CREATE TABLE IF NOT EXISTS relay_profiles (
    relay_id          SMALLINT PRIMARY KEY CHECK (relay_id BETWEEN 1 AND 4),
    display_name      VARCHAR(50) NOT NULL DEFAULT '',  -- Vd: "Khu A", "Khu B"
    spray_day_s       INT NOT NULL DEFAULT 30   CHECK (spray_day_s BETWEEN 5 AND 300),
    cooldown_day_s    INT NOT NULL DEFAULT 300  CHECK (cooldown_day_s BETWEEN 30 AND 7200),
    spray_night_s     INT NOT NULL DEFAULT 30   CHECK (spray_night_s BETWEEN 5 AND 300),
    cooldown_night_s  INT NOT NULL DEFAULT 600  CHECK (cooldown_night_s BETWEEN 30 AND 7200),
    updated_at        TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- Seed dữ liệu mặc định 4 relay
INSERT INTO relay_profiles (relay_id, display_name)
VALUES (1, 'Khu A'), (2, 'Khu B'), (3, 'Khu C'), (4, 'Khu D')
ON CONFLICT DO NOTHING;

-- ============================================================================
-- PHẦN 2: BẢNG DỮ LIỆU THỜI GIAN THỰC (TIMESCALEDB HYPERTABLE)
-- ============================================================================

-- 3. Relay events (state changes từ ESP32 telemetry)
CREATE TABLE IF NOT EXISTS relay_events (
    time              TIMESTAMPTZ NOT NULL DEFAULT NOW(),  -- TimescaleDB time column
    relay_id          SMALLINT NOT NULL CHECK (relay_id BETWEEN 1 AND 4),
    device_id         VARCHAR(64) NOT NULL,
    state             VARCHAR(32) NOT NULL,   -- SPRAYING | COOLING_DOWN | MANUAL_ON | MANUAL_OFF | FLUSH
    phase_remaining_s INT,
    mode              VARCHAR(8) CHECK (mode IN ('day', 'night')),
    override_active   BOOLEAN NOT NULL DEFAULT FALSE
);

SELECT create_hypertable('relay_events', 'time',
    chunk_time_interval => INTERVAL '1 day',
    if_not_exists => TRUE
);

-- 4. Sensor readings từ Tuya PH-W218
CREATE TABLE IF NOT EXISTS sensor_readings (
    time        TIMESTAMPTZ NOT NULL DEFAULT NOW(),  -- TimescaleDB time column
    sensor_id   VARCHAR(64) NOT NULL,
    ph_value    NUMERIC(4,2),      -- 0.00 – 14.00
    ec_value    INT,               -- µS/cm
    tds_value   INT,               -- ppm
    temperature NUMERIC(5,2),      -- °C
    salinity    NUMERIC(6,3),      -- ppt
    orp_value   INT,               -- mV
    turbidity   NUMERIC(8,2)       -- NTU
);

SELECT create_hypertable('sensor_readings', 'time',
    chunk_time_interval => INTERVAL '1 hour',
    if_not_exists => TRUE
);

-- 5. Device status (upsert từ MQTT heartbeat)
CREATE TABLE IF NOT EXISTS device_status (
    device_id    VARCHAR(64) PRIMARY KEY,
    status       VARCHAR(16) NOT NULL DEFAULT 'offline',
    uptime_s     BIGINT DEFAULT 0,
    rssi_dbm     SMALLINT,
    free_heap_b  INT,
    ntp_synced   BOOLEAN DEFAULT FALSE,
    rtc_valid    BOOLEAN DEFAULT FALSE,
    last_seen_at TIMESTAMPTZ
);

-- ============================================================================
-- PHẦN 3: INDEXING
-- ============================================================================

-- Index relay_events: lấy trạng thái mới nhất của từng relay
CREATE INDEX IF NOT EXISTS idx_relay_events_relay_time
    ON relay_events (relay_id, time DESC);

-- Index sensor_readings: lấy reading mới nhất + query chart
CREATE INDEX IF NOT EXISTS idx_sensor_readings_sensor_time
    ON sensor_readings (sensor_id, time DESC);

-- ============================================================================
-- PHẦN 4: RETENTION & COMPRESSION (Tùy chọn cho lab dài hạn)
-- ============================================================================

-- Nén dữ liệu sau 7 ngày (tiết kiệm dung lượng disk)
-- SELECT add_compression_policy('relay_events', INTERVAL '7 days', if_not_exists => TRUE);
-- SELECT add_compression_policy('sensor_readings', INTERVAL '7 days', if_not_exists => TRUE);

-- Xóa dữ liệu cũ hơn 90 ngày (phù hợp lab thí nghiệm)
-- SELECT add_retention_policy('relay_events', INTERVAL '90 days', if_not_exists => TRUE);
-- SELECT add_retention_policy('sensor_readings', INTERVAL '90 days', if_not_exists => TRUE);
```

---

### TRACK C — Mosquitto Configuration

---

#### Task C-1: `mosquitto/config/mosquitto.conf`

```
# === Listeners ===
listener 1883 0.0.0.0
protocol mqtt

listener 9001 0.0.0.0
protocol websockets

# === Security ===
allow_anonymous false
password_file /mosquitto/config/passwd
acl_file /mosquitto/config/acl

# === Persistence ===
persistence true
persistence_location /mosquitto/data/

# === Logging ===
log_dest stdout
log_type error
log_type warning
log_type notice
log_timestamp true

# === Connection Limits (phù hợp lab nhỏ) ===
max_connections 20
max_inflight_messages 10
max_queued_messages 100
message_size_limit 16384

retain_available true
```

#### Task C-2: `mosquitto/config/acl`

```
# ESP32 firmware device
user esp32_device
topic write aeroponics/device/+/status
topic write aeroponics/device/+/telemetry/#
topic read  aeroponics/device/+/command/#
topic read  aeroponics/device/+/config/#

# NestJS backend (cũng handle Tuya bridge)
user aero_backend
topic read  aeroponics/device/+/status
topic read  aeroponics/device/+/telemetry/#
topic write aeroponics/device/+/command/#
topic write aeroponics/device/+/config/#
topic write aeroponics/sensor/+/reading

# Admin (debug)
user mqtt_admin
topic readwrite #
```

---

### TRACK D — NestJS Backend Placeholder

Sprint 0 chỉ cần Backend **khởi động được** và trả `/health`. Logic thực tế triển khai ở Sprint 3.

---

#### Task D-1: `aeroponics-backend/Dockerfile`

```dockerfile
FROM node:20-alpine AS builder
WORKDIR /app
RUN npm install -g pnpm
COPY package.json pnpm-lock.yaml ./
RUN pnpm install --frozen-lockfile
COPY . .
RUN pnpm build

FROM node:20-alpine AS runner
WORKDIR /app
RUN npm install -g pnpm
COPY --from=builder /app/dist ./dist
COPY --from=builder /app/node_modules ./node_modules
COPY --from=builder /app/package.json ./
RUN apk add --no-cache curl
EXPOSE 3001
CMD ["node", "dist/main"]
```

#### Task D-2: `aeroponics-backend/package.json`

Tái sử dụng từ `mushroom-backend/package.json`, **bỏ** các dependency InfluxDB:

**Bỏ:** `@influxdata/influxdb-client`

**Giữ lại:**
```json
{
  "dependencies": {
    "@nestjs/common": "^11.0.1",
    "@nestjs/core": "^11.0.1",
    "@nestjs/platform-express": "^11.0.1",
    "@nestjs/typeorm": "11.0.3",
    "@nestjs/jwt": "^10.x",
    "@nestjs/passport": "^10.x",
    "@nestjs/serve-static": "^4.x",
    "class-transformer": "0.5.1",
    "class-validator": "0.15.1",
    "mqtt": "^5.15.2",
    "pg": "^8.22.0",
    "tuyapi": "^7.5.x",
    "reflect-metadata": "^0.2.2",
    "rxjs": "^7.8.1",
    "typeorm": "1.0.0"
  }
}
```

**Thêm mới:** `tuyapi` (Tuya Local Key bridge)

#### Task D-3: NestJS Placeholder `src/main.ts` và `src/app.module.ts`

**`main.ts`** — Chỉ khởi động NestJS với port 3001.

**`app.module.ts`** placeholder — Import DatabaseModule (với TypeORM kết nối DB):

```typescript
@Module({
  imports: [
    AppConfigModule,    // ConfigModule với validation env vars
    DatabaseModule,     // TypeORM → TimescaleDB connection
    // Sprint 3: MqttModule, RelayModule, SensorModule, TuyaBridgeModule
  ],
  controllers: [AppController],  // GET /health
})
export class AppModule {}
```

**`app.controller.ts`** — GET `/health` trả về `{ status: 'ok' }`.

---

### TRACK E — Environment Variables

---

#### Task E-1: `.env.example`

```bash
# =====================================================
# AEROPONICS LAB — Environment Variables Template
# cp .env.example .env  → điền giá trị thật
# KHÔNG commit .env vào git!
# =====================================================

# --- TimescaleDB / PostgreSQL ---
DB_USER=aeroponics_user
DB_PASS=CHANGE_ME_DB_PASSWORD
DB_NAME=aeroponics
# DATABASE_URL tự động được Backend construct từ DB_USER/DB_PASS/DB_NAME

# --- MQTT Broker (Mosquitto) ---
MQTT_PORT=1883
MQTT_WS_PORT=9001
MQTT_ADMIN_USER=mqtt_admin
MQTT_ADMIN_PASS=CHANGE_ME_ADMIN_PASSWORD
MQTT_DEVICE_USER=esp32_device
MQTT_DEVICE_PASS=CHANGE_ME_DEVICE_PASSWORD
MQTT_BACKEND_USER=aero_backend
MQTT_BACKEND_PASS=CHANGE_ME_BACKEND_PASSWORD

# --- NestJS Backend ---
BACKEND_PORT=3001
JWT_SECRET=CHANGE_ME_MIN_32_CHARS_RANDOM_STRING

# --- Tuya Bridge (tích hợp trong Backend) ---
TUYA_DEVICE_IP=192.168.1.XXX        # IP của cảm biến PH-W218 trong LAN
TUYA_DEVICE_ID=CHANGE_ME            # Device ID từ Tuya App
TUYA_LOCAL_KEY=CHANGE_ME_16_CHARS   # Local Key 16 ký tự
TUYA_SENSOR_ID=ph-w218-01
TUYA_POLL_INTERVAL_MS=10000

# --- ESP32 Firmware (tham khảo, không dùng bởi Docker) ---
WIFI_SSID=your_wifi_ssid
WIFI_PASSWORD=your_wifi_password
DEVICE_ID=esp32s3-unique-id
```

---

### TRACK F — Scripts

---

#### Task F-1: `scripts/setup.sh`

```bash
#!/usr/bin/env bash
set -euo pipefail

# Logic:
# 1. Kiểm tra docker + docker compose version
# 2. Kiểm tra .env tồn tại → cp .env.example .env nếu chưa có
# 3. Validate REQUIRED_VARS: DB_PASS, MQTT_ADMIN_PASS, MQTT_DEVICE_PASS,
#    MQTT_BACKEND_PASS, JWT_SECRET
# 4. mkdir -p mosquitto/config mosquitto/data database
# 5. Sinh mosquitto/config/passwd (3 users: admin, device, backend)
# 6. Kiểm tra port 1883, 9001, 3001 không bị chiếm
# 7. Print summary
```

#### Task F-2: `scripts/health-check.sh`

```bash
#!/usr/bin/env bash
set -euo pipefail

# Logic:
# 1. check_service_healthy aero_timescaledb
# 2. check_service_healthy aero_mosquitto
# 3. check_service_healthy aero_backend
# 4. check_mqtt_auth: mosquitto_pub với đúng creds → OK
# 5. check_mqtt_anon_denied: mosquitto_pub không creds → phải fail
# 6. check_timescaledb:
#    - Extension timescaledb enabled
#    - Tables: relay_profiles, relay_events, sensor_readings, device_status tồn tại
#    - relay_events + sensor_readings là hypertable
# 7. check_backend: curl GET /health → 200
# 8. print_summary_table
#
# KHÔNG CÓ: check_redis, check_influxdb
```

---

## 4. TIÊU CHUẨN RÀ SOÁT CỨNG (Sprint 0)

### Rule S0-INFRA-01: Đúng 3 Containers
```
PASS: docker-compose.yml chỉ có 3 services: timescaledb, mosquitto, aero-backend
FAIL: Thêm redis, influxdb, hoặc bất kỳ service nào khác
```

### Rule S0-DB-02: TimescaleDB, Không Phải Postgres Thuần
```
PASS: Image là timescale/timescaledb:latest-pg15
PASS: schema.sql gọi CREATE EXTENSION IF NOT EXISTS timescaledb
PASS: relay_events và sensor_readings được tạo bằng create_hypertable()
FAIL: Dùng postgres:15 image (không có timescaledb extension)
```

### Rule S0-DB-03: Port 5432 KHÔNG Expose Ra Host
```
PASS: timescaledb service KHÔNG có ports mapping
FAIL: "5432:5432" trong docker-compose.yml (production)
NOTE: docker-compose.override.yml (dev) CÓ THỂ expose để dùng DBeaver
```

### Rule S0-SECURITY-04: Mosquitto Authentication Bắt Buộc
```
PASS: allow_anonymous false
PASS: password_file và acl_file được mount
FAIL: allow_anonymous true trong bất kỳ môi trường production nào
```

### Rule S0-SECRET-05: Không Commit Secrets
```
PASS: .env, mosquitto/config/passwd trong .gitignore
FAIL: Bất kỳ password/key nào được hardcode hoặc commit
```

---

## 5. CỔNG NGHIỆM THU SPRINT 0

- [ ] `docker compose up -d` khởi động 3 services thành công.
- [ ] Cả 3 container báo `healthy` trong vòng 90s.
- [ ] MQTT authentication hợp lệ; anonymous bị từ chối.
- [ ] TimescaleDB: extension enabled, 5 tables tồn tại, 2 hypertables.
- [ ] `GET http://localhost:3001/health` → `{"status":"ok"}`.
- [ ] Volume persistent: stop + rm containers → up lại → dữ liệu còn nguyên.
- [ ] `scripts/setup.sh` chạy idempotent.
- [ ] Không có `.env`, `passwd` trong git tracking.

---

*Sprint 0 Planning — Updated 2026-07-30: NestJS + TimescaleDB (không có InfluxDB/Redis)*
