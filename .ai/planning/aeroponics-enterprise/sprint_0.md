# Sprint 0: Docker Infrastructure Setup (Local Dev Environment)

> **Phụ thuộc:** Không có (Sprint nền tảng, chạy đầu tiên trước tất cả).
> **Output bàn giao:** Toàn bộ stack hạ tầng (`mosquitto`, `timescaledb`, `redis`, `nestjs-backend`, `tuya-bridge`) khởi động bằng **1 lệnh** `docker compose up -d`, health-check xanh, volume persistent, network isolation đúng, secrets quản lý qua `.env` — Developer có thể `pio run` firmware ngay sau đó mà không cần cấu hình thủ công thêm bất kỳ thứ gì.

---

## 1. PHẠM VI & MỤC TIÊU

### 1.1 Modules / Components bị tác động trực tiếp

| File | Loại | Mô tả |
|---|---|---|
| `docker-compose.yml` | Infra | Orchestration toàn bộ stack, health-check, depends_on |
| `docker-compose.override.yml` | Infra | Dev overrides: bind mount source, hot-reload, expose thêm port debug |
| `.env.example` | Config | Template đầy đủ biến môi trường, không chứa secret thật |
| `.env` | Config | Secret thật — nằm trong `.gitignore` |
| `.gitignore` | Config | Bổ sung `.env`, `mosquitto/config/passwd`, `mosquitto/data/`, `mosquitto/log/` |
| `mosquitto/config/mosquitto.conf` | Broker | Cấu hình Mosquitto production-ready |
| `mosquitto/config/mosquitto.dev.conf` | Broker | Cấu hình Mosquitto dev (allow_anonymous=true để test nhanh) |
| `mosquitto/config/passwd` | Broker | File password MQTT (sinh bằng script, không commit) |
| `mosquitto/config/acl` | Broker | Access Control List phân quyền topic theo user |
| `scripts/init-db.sql` | DB | SQL khởi tạo TimescaleDB: extension, roles, databases |
| `scripts/setup.sh` | DevOps | Script 1-click: sinh passwd, tạo .env từ template, kiểm tra prerequisites |
| `scripts/health-check.sh` | DevOps | Script kiểm tra tất cả services healthy sau `docker compose up` |
| `docs/ARCHITECTURE.md` | Docs | Sơ đồ kiến trúc toàn hệ thống, network diagram |
| `docs/HARDWARE_PINOUT.md` | Docs | Sơ đồ chân ESP32-S3, relay, RTC DS3231 |
| `docs/MQTT_TOPICS.md` | Docs | Toàn bộ MQTT topic schema |
| `docs/TUYA_PH_W218_SPEC.md` | Docs | DP Code mapping Tuya PH-W218 |

### 1.2 Mục tiêu cụ thể của Sprint 0

- [ ] `docker compose up -d` khởi động tất cả services không lỗi.
- [ ] Tất cả services có `healthcheck` và báo `healthy` trong vòng 60s.
- [ ] Mosquitto từ chối kết nối anonymous, chấp nhận đúng user/pass từ `passwd` file.
- [ ] TimescaleDB extension `timescaledb` được enable, database `aeroponics` tồn tại.
- [ ] Redis ping thành công từ trong Docker network.
- [ ] Port exposure đúng: chỉ Mosquitto (1883, 9001) và NestJS (3001) expose ra host. DB và Redis không expose.
- [ ] Volume data persistent: xóa container rồi re-create, dữ liệu không mất.
- [ ] `scripts/setup.sh` chạy được trên macOS (zsh) và Ubuntu 22.04 mà không cần can thiệp thủ công.
- [ ] Tất cả tài liệu kỹ thuật cốt lõi (`ARCHITECTURE.md`, `HARDWARE_PINOUT.md`, `MQTT_TOPICS.md`, `TUYA_PH_W218_SPEC.md`) được viết xong.

---

## 2. KIẾN TRÚC & LUỒNG DỮ LIỆU

### 2.1 Docker Network Topology

```
HOST MACHINE
│
├── Port 1883  ──────────────────▶  [mosquitto]   (MQTT TCP)
├── Port 9001  ──────────────────▶  [mosquitto]   (MQTT WebSocket)
├── Port 3001  ──────────────────▶  [nestjs-backend]
│
│   ┌─────────────────── Docker Network: aeroponics_net (bridge) ───────────────────┐
│   │                                                                               │
│   │  [mosquitto]  ◀── subscribe/publish ──▶  [nestjs-backend]                   │
│   │       ▲                                         │                            │
│   │       │ publish                          TypeORM│                            │
│   │  [tuya-bridge]                                  ▼                            │
│   │                                        [timescaledb] :5432 (internal only)   │
│   │                                                  │                            │
│   │                                         Redis    │                            │
│   │                                        [redis]   │ :6379 (internal only)     │
│   └───────────────────────────────────────────────────────────────────────────────┘
│
└── [ESP32-S3] ──── WiFi ────▶ Port 1883 [mosquitto]
```

### 2.2 Cấu Trúc Thư Mục Toàn Dự Án (Sau Sprint 0)

```
aeroponics-project/
├── docker-compose.yml              ← Production/Staging stack
├── docker-compose.override.yml     ← Dev overrides (auto-loaded)
├── .env.example                    ← Template (commit)
├── .env                            ← Secrets (gitignore)
├── .gitignore
│
├── mosquitto/
│   ├── config/
│   │   ├── mosquitto.conf          ← Production config
│   │   ├── mosquitto.dev.conf      ← Dev config (allow_anonymous)
│   │   ├── passwd                  ← Generated (gitignore)
│   │   └── acl                     ← ACL rules (commit, no secrets)
│   ├── data/                       ← Persistent (gitignore)
│   └── log/                        ← Log files (gitignore)
│
├── scripts/
│   ├── setup.sh                    ← 1-click dev setup
│   ├── health-check.sh             ← Verify all services OK
│   └── init-db.sql                 ← TimescaleDB initialization
│
├── docs/
│   ├── ARCHITECTURE.md
│   ├── HARDWARE_PINOUT.md
│   ├── MQTT_TOPICS.md
│   └── TUYA_PH_W218_SPEC.md
│
├── aeroponics-firmware/            ← Sprint 1+
├── aeroponics-backend/             ← Sprint 3+
├── aeroponics-tuya-bridge/         ← Sprint 3+
└── aeroponics-ui/                  ← Sprint 4+
```

### 2.3 Service Startup Dependency Chain

```
[docker compose up -d]
        │
        ├──▶ [timescaledb]  (no deps)
        │       └── healthcheck: pg_isready
        │
        ├──▶ [redis]        (no deps)
        │       └── healthcheck: redis-cli ping
        │
        ├──▶ [mosquitto]    (no deps)
        │       └── healthcheck: mosquitto_pub -t $SYS/test (hoặc nc -z localhost 1883)
        │
        └──▶ [nestjs-backend]  depends_on: timescaledb(healthy), redis(healthy), mosquitto(healthy)
                └── healthcheck: curl http://localhost:3001/api/health
        │
        └──▶ [tuya-bridge]     depends_on: mosquitto(healthy)
                └── healthcheck: curl http://localhost:3002/health (nếu có HTTP health endpoint)
```

### 2.4 Luồng Quản Lý Secrets

```
[Developer mới clone repo]
        │
        ▼
Bước 1: cp .env.example .env
        │
        ▼
Bước 2: Điền secrets vào .env:
  MQTT_ADMIN_PASS=...
  MQTT_DEVICE_PASS=...
  MQTT_BACKEND_PASS=...
  DB_PASS=...
  JWT_SECRET=...
  TUYA_LOCAL_KEY=...
        │
        ▼
Bước 3: bash scripts/setup.sh
  → Script đọc .env
  → Sinh mosquitto/config/passwd từ MQTT_*_PASS
  → Verify Docker + docker compose version
  → Verify port 1883, 5432, 6379 không bị chiếm
        │
        ▼
Bước 4: docker compose up -d
        │
        ▼
Bước 5: bash scripts/health-check.sh
  → Xác nhận tất cả services healthy
  → In ra summary bảng trạng thái
```

### 2.5 Luồng Sinh MQTT Password File

```
[scripts/setup.sh]
        │
        ▼
Đọc từ .env:
  MQTT_DEVICE_USER, MQTT_DEVICE_PASS
  MQTT_BACKEND_USER, MQTT_BACKEND_PASS
  MQTT_ADMIN_USER, MQTT_ADMIN_PASS
        │
        ▼
Chạy trong container tạm:
  docker run --rm -v $(pwd)/mosquitto/config:/config \
    eclipse-mosquitto:2.0 \
    mosquitto_passwd -c /config/passwd $MQTT_ADMIN_USER $MQTT_ADMIN_PASS

  docker run --rm -v $(pwd)/mosquitto/config:/config \
    eclipse-mosquitto:2.0 \
    mosquitto_passwd -b /config/passwd $MQTT_DEVICE_USER $MQTT_DEVICE_PASS

  docker run --rm -v $(pwd)/mosquitto/config:/config \
    eclipse-mosquitto:2.0 \
    mosquitto_passwd -b /config/passwd $MQTT_BACKEND_USER $MQTT_BACKEND_PASS
        │
        ▼
File mosquitto/config/passwd được tạo ra với 3 users
(Nằm trong .gitignore, không commit)
```

---

## 3. PHÂN RÃ CHI TIẾT TÁC VỤ

### TRACK A — Docker Compose Files

---

#### Task A-1: Viết `docker-compose.yml` (Production/Base)
**File tạo mới:** `aeroponics-project/docker-compose.yml`

**Service `timescaledb`:**
```yaml
image: timescale/timescaledb:latest-pg15
container_name: aeroponics_timescaledb
environment:
  POSTGRES_USER: ${DB_USER}
  POSTGRES_PASSWORD: ${DB_PASS}
  POSTGRES_DB: ${DB_NAME:-aeroponics}
volumes:
  - timescale_data:/var/lib/postgresql/data
  - ./scripts/init-db.sql:/docker-entrypoint-initdb.d/init.sql:ro
networks:
  - aeroponics_net
restart: unless-stopped
healthcheck:
  test: ["CMD-SHELL", "pg_isready -U ${DB_USER} -d ${DB_NAME:-aeroponics}"]
  interval: 10s
  timeout: 5s
  retries: 5
  start_period: 30s
```

**Service `redis`:**
```yaml
image: redis:7-alpine
container_name: aeroponics_redis
command: redis-server --requirepass ${REDIS_PASS} --appendonly yes
volumes:
  - redis_data:/data
networks:
  - aeroponics_net
restart: unless-stopped
healthcheck:
  test: ["CMD", "redis-cli", "-a", "${REDIS_PASS}", "ping"]
  interval: 10s
  timeout: 3s
  retries: 5
```

**Service `mosquitto`:**
```yaml
image: eclipse-mosquitto:2.0
container_name: aeroponics_mosquitto
ports:
  - "${MQTT_PORT:-1883}:1883"
  - "${MQTT_WS_PORT:-9001}:9001"
volumes:
  - ./mosquitto/config/mosquitto.conf:/mosquitto/config/mosquitto.conf:ro
  - ./mosquitto/config/passwd:/mosquitto/config/passwd:ro
  - ./mosquitto/config/acl:/mosquitto/config/acl:ro
  - mosquitto_data:/mosquitto/data
  - mosquitto_log:/mosquitto/log
networks:
  - aeroponics_net
restart: unless-stopped
healthcheck:
  test: ["CMD-SHELL", "mosquitto_pub -h localhost -p 1883 -u ${MQTT_ADMIN_USER} -P ${MQTT_ADMIN_PASS} -t '$$SYS/health' -m 'ping' -q 0 2>/dev/null && echo OK"]
  interval: 15s
  timeout: 5s
  retries: 5
  start_period: 10s
```

**Service `nestjs-backend`:**
```yaml
image: aeroponics/backend:latest
container_name: aeroponics_backend
build:
  context: ./aeroponics-backend
  dockerfile: Dockerfile
ports:
  - "${BACKEND_PORT:-3001}:3001"
environment:
  NODE_ENV: production
  DB_HOST: timescaledb
  DB_PORT: 5432
  DB_USER: ${DB_USER}
  DB_PASS: ${DB_PASS}
  DB_NAME: ${DB_NAME:-aeroponics}
  REDIS_HOST: redis
  REDIS_PORT: 6379
  REDIS_PASS: ${REDIS_PASS}
  MQTT_BROKER_URL: mqtt://mosquitto:1883
  MQTT_USERNAME: ${MQTT_BACKEND_USER}
  MQTT_PASSWORD: ${MQTT_BACKEND_PASS}
  JWT_SECRET: ${JWT_SECRET}
  PORT: 3001
depends_on:
  timescaledb:
    condition: service_healthy
  redis:
    condition: service_healthy
  mosquitto:
    condition: service_healthy
networks:
  - aeroponics_net
restart: unless-stopped
healthcheck:
  test: ["CMD-SHELL", "curl -f http://localhost:3001/api/health || exit 1"]
  interval: 20s
  timeout: 5s
  retries: 5
  start_period: 60s
```

**Service `tuya-bridge`:**
```yaml
image: aeroponics/tuya-bridge:latest
container_name: aeroponics_tuya_bridge
build:
  context: ./aeroponics-tuya-bridge
  dockerfile: Dockerfile
environment:
  TUYA_DEVICE_IP: ${TUYA_DEVICE_IP}
  TUYA_DEVICE_ID: ${TUYA_DEVICE_ID}
  TUYA_LOCAL_KEY: ${TUYA_LOCAL_KEY}
  TUYA_SENSOR_ID: ${TUYA_SENSOR_ID:-ph-w218-01}
  MQTT_BROKER_URL: mqtt://mosquitto:1883
  MQTT_USERNAME: ${MQTT_BACKEND_USER}
  MQTT_PASSWORD: ${MQTT_BACKEND_PASS}
  POLL_INTERVAL_MS: ${TUYA_POLL_INTERVAL_MS:-10000}
depends_on:
  mosquitto:
    condition: service_healthy
networks:
  - aeroponics_net
restart: unless-stopped
```

**Networks & Volumes:**
```yaml
networks:
  aeroponics_net:
    driver: bridge
    name: aeroponics_net

volumes:
  timescale_data:
    name: aeroponics_timescale_data
  redis_data:
    name: aeroponics_redis_data
  mosquitto_data:
    name: aeroponics_mosquitto_data
  mosquitto_log:
    name: aeroponics_mosquitto_log
```

---

#### Task A-2: Viết `docker-compose.override.yml` (Dev Overrides)
**File tạo mới:** `aeroponics-project/docker-compose.override.yml`

**Mục đích:** Auto-loaded bởi Docker Compose khi chạy `docker compose up` (không cần `-f`). Ghi đè config cho môi trường dev local.

**Overrides cần định nghĩa:**

| Service | Override nội dung |
|---|---|
| `timescaledb` | Thêm port `"5432:5432"` expose ra host (để dùng với DBeaver/psql local) |
| `redis` | Thêm port `"6379:6379"` expose ra host (để dùng với redis-cli local) |
| `mosquitto` | Mount `mosquitto.dev.conf` thay vì `mosquitto.conf` (allow_anonymous=true) |
| `nestjs-backend` | `NODE_ENV: development`, bind mount `./aeroponics-backend/src:/app/src` để hot-reload, thêm port `"9229:9229"` cho Node.js debugger |
| `tuya-bridge` | Bind mount source `./aeroponics-tuya-bridge/src:/app/src` |

**Chú ý quan trọng:** `docker-compose.override.yml` phải nằm trong `.gitignore` nếu có thông tin nhạy cảm, hoặc tạo file mẫu `docker-compose.override.yml.example` để commit.

---

### TRACK B — Mosquitto Configuration

---

#### Task B-1: Cấu hình Mosquitto Production
**File tạo mới:** `mosquitto/config/mosquitto.conf`

**Các directive bắt buộc theo thứ tự:**

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
autosave_interval 1800

# === Logging ===
log_dest file /mosquitto/log/mosquitto.log
log_dest stdout
log_type error
log_type warning
log_type notice
log_type information
log_timestamp true
log_timestamp_format %Y-%m-%dT%H:%M:%S

# === Connection Limits ===
max_connections 100
max_inflight_messages 20
max_queued_messages 1000

# === Keep-alive ===
keepalive_interval 60

# === Message Size ===
message_size_limit 65536

# === Retain ===
retain_available true
```

---

#### Task B-2: Cấu hình Mosquitto Dev
**File tạo mới:** `mosquitto/config/mosquitto.dev.conf`

**Khác biệt so với production:**
```
allow_anonymous true      ← Cho phép test nhanh không cần auth
log_type all              ← Log tất cả để debug
```
Toàn bộ phần còn lại giống `mosquitto.conf`.

---

#### Task B-3: Cấu hình ACL (Access Control List)
**File tạo mới:** `mosquitto/config/acl`

**Phân quyền theo nguyên tắc Least Privilege:**

```
# User: ESP32 firmware device
user esp32_device
topic write aeroponics/device/+/status
topic write aeroponics/device/+/telemetry/#
topic read  aeroponics/device/+/command/#
topic read  aeroponics/device/+/config/#

# User: NestJS backend
user nestjs_backend
topic read  aeroponics/device/+/status
topic read  aeroponics/device/+/telemetry/#
topic write aeroponics/device/+/command/#
topic write aeroponics/device/+/config/#
topic read  aeroponics/sensor/+/reading
topic write aeroponics/sensor/+/reading

# User: Tuya bridge (dùng chung account với backend để đơn giản)
# Hoặc tạo user riêng:
user tuya_bridge
topic write aeroponics/sensor/+/reading

# User: Admin (monitoring, debug)
user mqtt_admin
topic readwrite #
```

---

### TRACK C — Database Initialization

---

#### Task C-1: SQL Script Khởi Tạo TimescaleDB
**File tạo mới:** `scripts/init-db.sql`

**Nội dung cần có (theo thứ tự):**

```sql
-- 1. Enable TimescaleDB extension (chạy trên DB aeroponics)
CREATE EXTENSION IF NOT EXISTS timescaledb CASCADE;

-- 2. Tạo role cho application (least privilege)
DO $$
BEGIN
  IF NOT EXISTS (SELECT FROM pg_roles WHERE rolname = 'aeroponics_app') THEN
    CREATE ROLE aeroponics_app LOGIN PASSWORD 'PLACEHOLDER_REPLACED_BY_ENV';
  END IF;
END $$;

-- 3. Grant privileges
GRANT CONNECT ON DATABASE aeroponics TO aeroponics_app;
GRANT USAGE ON SCHEMA public TO aeroponics_app;
GRANT CREATE ON SCHEMA public TO aeroponics_app;
ALTER DEFAULT PRIVILEGES IN SCHEMA public
  GRANT SELECT, INSERT, UPDATE, DELETE ON TABLES TO aeroponics_app;

-- 4. Tạo bảng relay_events (sẽ làm hypertable)
CREATE TABLE IF NOT EXISTS relay_events (
  id          UUID DEFAULT gen_random_uuid(),
  relay_id    SMALLINT NOT NULL CHECK (relay_id BETWEEN 1 AND 4),
  device_id   VARCHAR(64) NOT NULL,
  state       VARCHAR(32) NOT NULL,
  phase_remaining_s INT,
  mode        VARCHAR(8),
  override_active BOOLEAN DEFAULT FALSE,
  occurred_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- 5. Convert to hypertable
SELECT create_hypertable('relay_events', 'occurred_at',
  chunk_time_interval => INTERVAL '1 day',
  if_not_exists => TRUE
);

-- 6. Tạo bảng sensor_readings (hypertable)
CREATE TABLE IF NOT EXISTS sensor_readings (
  id          UUID DEFAULT gen_random_uuid(),
  sensor_id   VARCHAR(64) NOT NULL,
  ph_value    NUMERIC(4,2),
  ec_value    INT,
  tds_value   INT,
  temperature NUMERIC(5,2),
  salinity    NUMERIC(6,3),
  orp_value   INT,
  turbidity   NUMERIC(8,2),
  recorded_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

SELECT create_hypertable('sensor_readings', 'recorded_at',
  chunk_time_interval => INTERVAL '1 hour',
  if_not_exists => TRUE
);

-- 7. Tạo bảng device_status (regular table)
CREATE TABLE IF NOT EXISTS device_status (
  device_id   VARCHAR(64) PRIMARY KEY,
  status      VARCHAR(16) NOT NULL DEFAULT 'offline',
  uptime_s    BIGINT DEFAULT 0,
  rssi_dbm    SMALLINT,
  free_heap_b INT,
  ntp_synced  BOOLEAN DEFAULT FALSE,
  rtc_valid   BOOLEAN DEFAULT FALSE,
  last_seen_at TIMESTAMPTZ
);

-- 8. Indexes cho query performance
CREATE INDEX IF NOT EXISTS idx_relay_events_relay_id ON relay_events (relay_id, occurred_at DESC);
CREATE INDEX IF NOT EXISTS idx_relay_events_device_id ON relay_events (device_id, occurred_at DESC);
CREATE INDEX IF NOT EXISTS idx_sensor_readings_sensor_id ON sensor_readings (sensor_id, recorded_at DESC);

-- 9. TimescaleDB compression policy (compress chunks older than 7 days)
SELECT add_compression_policy('relay_events', INTERVAL '7 days', if_not_exists => TRUE);
SELECT add_compression_policy('sensor_readings', INTERVAL '7 days', if_not_exists => TRUE);

-- 10. Retention policy (drop data older than 90 days)
SELECT add_retention_policy('relay_events', INTERVAL '90 days', if_not_exists => TRUE);
SELECT add_retention_policy('sensor_readings', INTERVAL '90 days', if_not_exists => TRUE);
```

---

### TRACK D — Setup & Health Check Scripts

---

#### Task D-1: Setup Script 1-Click
**File tạo mới:** `scripts/setup.sh`

**Logic bash script (zsh-compatible):**

```bash
Bước 1: Kiểm tra prerequisites
  - docker --version (min 24.x)
  - docker compose version (min 2.x) [plugin syntax, không phải docker-compose]
  - openssl (để generate JWT_SECRET nếu chưa có)
  - Nếu thiếu: in hướng dẫn cài và exit 1

Bước 2: Kiểm tra .env tồn tại
  - Nếu chưa có: tự cp .env.example .env và thông báo user điền secrets
  - Nếu đã có: tiếp tục

Bước 3: Validate .env không thiếu required fields
  REQUIRED_VARS=(MQTT_ADMIN_PASS MQTT_DEVICE_PASS MQTT_BACKEND_PASS DB_PASS JWT_SECRET)
  Với mỗi var: kiểm tra có set và không empty
  Nếu thiếu: in danh sách thiếu và exit 1

Bước 4: Tạo thư mục cần thiết
  mkdir -p mosquitto/config mosquitto/data mosquitto/log scripts

Bước 5: Sinh mosquitto/config/passwd
  - Nếu file đã tồn tại: hỏi confirm overwrite
  - Chạy mosquitto_passwd trong container tạm (không cần cài mosquitto local)

Bước 6: Kiểm tra port conflict
  for port in 1883 9001 3001:
    lsof -i :$port → nếu occupied: warn user, hỏi tiếp tục không

Bước 7: Print summary
  ✅ Setup complete! Run: docker compose up -d
  ✅ Then verify: bash scripts/health-check.sh
```

**Các hàm cần định nghĩa trong script:**
- `check_command(cmd)` — Kiểm tra binary tồn tại.
- `check_docker_version(min_major)` — So sánh version.
- `load_env()` — Source .env file một cách an toàn.
- `validate_required_vars(vars[])` — Loop và check.
- `generate_mqtt_passwd()` — Chạy mosquitto_passwd trong container.
- `check_port_conflict(port)` — lsof check.
- `print_success_summary()` — Bảng đẹp cuối script.

---

#### Task D-2: Health Check Script
**File tạo mới:** `scripts/health-check.sh`

**Logic bash — Kiểm tra theo thứ tự:**

```bash
Hàm check_service_healthy(service_name):
  status = docker inspect --format='{{.State.Health.Status}}' aeroponics_$service_name
  if status == "healthy": print "✅ $service_name: healthy"
  elif status == "starting": print "⏳ $service_name: starting (wait...)"
  else: print "❌ $service_name: $status"

Hàm check_mqtt_auth():
  Chạy mosquitto_sub trong container tạm:
    docker run --rm --network aeroponics_net eclipse-mosquitto:2.0
      mosquitto_sub -h mosquitto -t '$SYS/broker/version' -C 1 -W 3
      -u $MQTT_ADMIN_USER -P $MQTT_ADMIN_PASS
  Nếu nhận được response: ✅ MQTT auth OK
  Else: ❌ MQTT auth FAIL

Hàm check_mqtt_anonymous_denied():
  Thử kết nối không có auth, expect thất bại (return code != 0)
  Nếu bị từ chối: ✅ Anonymous correctly denied
  Else: ❌ SECURITY ISSUE: anonymous allowed

Hàm check_timescaledb():
  docker exec aeroponics_timescaledb psql -U $DB_USER -d $DB_NAME
    -c "SELECT extname FROM pg_extension WHERE extname='timescaledb';"
  Expect: timescaledb trong output
  Check: 3 tables tồn tại (relay_events, sensor_readings, device_status)
  Check: relay_events, sensor_readings là hypertable

Hàm check_redis():
  docker exec aeroponics_redis redis-cli -a $REDIS_PASS ping
  Expect: PONG

Hàm print_summary_table():
  In bảng:
  ┌──────────────────┬────────────┬──────────────────────────────────────┐
  │ Service          │ Status     │ Detail                               │
  ├──────────────────┼────────────┼──────────────────────────────────────┤
  │ TimescaleDB      │ ✅ healthy  │ Extension OK, 3 tables OK            │
  │ Redis            │ ✅ healthy  │ PONG received                        │
  │ Mosquitto        │ ✅ healthy  │ Auth OK, Anonymous denied            │
  │ NestJS Backend   │ ✅ healthy  │ /api/health → 200 OK                │
  │ Tuya Bridge      │ ✅ healthy  │ Running                              │
  └──────────────────┴────────────┴──────────────────────────────────────┘

Main:
  load_env()
  check_service_healthy timescaledb
  check_service_healthy redis
  check_service_healthy mosquitto
  check_mqtt_auth
  check_mqtt_anonymous_denied
  check_timescaledb
  check_redis
  check_service_healthy backend
  check_service_healthy tuya_bridge
  print_summary_table
  exit 0 nếu tất cả OK, exit 1 nếu có failure
```

---

### TRACK E — Environment Variables

---

#### Task E-1: Template `.env.example`
**File tạo mới:** `aeroponics-project/.env.example`

**Tất cả biến phải có:**

```bash
# =====================================================
# AEROPONICS PROJECT — Environment Variables Template
# Copy to .env and fill in your actual values
# NEVER commit .env to git!
# =====================================================

# --- MQTT Broker (Mosquitto) ---
MQTT_PORT=1883
MQTT_WS_PORT=9001
MQTT_ADMIN_USER=mqtt_admin
MQTT_ADMIN_PASS=CHANGE_ME_STRONG_PASSWORD
MQTT_DEVICE_USER=esp32_device
MQTT_DEVICE_PASS=CHANGE_ME_DEVICE_PASSWORD
MQTT_BACKEND_USER=nestjs_backend
MQTT_BACKEND_PASS=CHANGE_ME_BACKEND_PASSWORD

# --- TimescaleDB / PostgreSQL ---
DB_USER=aeroponics_user
DB_PASS=CHANGE_ME_DB_PASSWORD
DB_NAME=aeroponics
DB_HOST=timescaledb    # Docker service name (do not change for Docker)
DB_PORT=5432

# --- Redis ---
REDIS_PASS=CHANGE_ME_REDIS_PASSWORD
REDIS_HOST=redis
REDIS_PORT=6379

# --- NestJS Backend ---
BACKEND_PORT=3001
JWT_SECRET=CHANGE_ME_MIN_32_CHARS_RANDOM_STRING
JWT_EXPIRES_IN=7d
NODE_ENV=development

# --- Tuya Bridge ---
TUYA_DEVICE_IP=192.168.1.XXX       # IP của cảm biến PH-W218 trong LAN
TUYA_DEVICE_ID=CHANGE_ME           # Device ID từ Tuya App
TUYA_LOCAL_KEY=CHANGE_ME_16_CHARS  # Local Key 16 ký tự
TUYA_SENSOR_ID=ph-w218-01          # Custom sensor identifier
TUYA_POLL_INTERVAL_MS=10000        # Poll mỗi 10 giây

# --- ESP32 Firmware (tham khảo, không dùng trực tiếp bởi Docker) ---
WIFI_SSID=your_wifi_ssid
WIFI_PASSWORD=your_wifi_password
DEVICE_ID=esp32s3-unique-id        # Phải unique, dùng làm MQTT client ID
```

---

### TRACK F — Documentation

---

#### Task F-1: `docs/ARCHITECTURE.md`
**File tạo mới:** `aeroponics-project/docs/ARCHITECTURE.md`

**Nội dung cần có:**
- Sơ đồ kiến trúc text-based (như Section 2.1 sprint này nhưng đầy đủ hơn với cả 4 Sprint layers).
- Mô tả vai trò của từng service trong 1-2 câu.
- Bảng port mapping đầy đủ.
- Bảng Docker volume và ý nghĩa.
- Quyết định kiến trúc quan trọng (ADR — Architecture Decision Records) ngắn gọn:
  - Tại sao chọn Mosquitto thay vì EMQX?
  - Tại sao chọn TimescaleDB thay vì InfluxDB?
  - Tại sao Local Key Tuya thay vì Tuya Cloud API?

---

#### Task F-2: `docs/HARDWARE_PINOUT.md`
**File tạo mới:** `aeroponics-project/docs/HARDWARE_PINOUT.md`

**Nội dung cần có:**

| GPIO | Chức năng | Pull-up/down | Mức Active | Ghi chú |
|---|---|---|---|---|
| GPIO 1 | Relay 1 Signal | Pull-down 10kΩ | HIGH = ON | Vùng phun A |
| GPIO 2 | Relay 2 Signal | Pull-down 10kΩ | HIGH = ON | Vùng phun B |
| GPIO 3 | Relay 3 Signal | Pull-down 10kΩ | HIGH = ON | Vùng phun C |
| GPIO 4 | Relay 4 Signal | Pull-down 10kΩ | HIGH = ON | Dự phòng |
| GPIO 21 | DS3231 SDA | Internal Pull-up | — | I2C Data |
| GPIO 22 | DS3231 SCL | Internal Pull-up | — | I2C Clock |
| GPIO 2 | LED Status | — | HIGH = ON | Onboard LED |

- Sơ đồ kết nối DS3231 text-based: VCC→3.3V, GND→GND, SDA→GPIO21, SCL→GPIO22, SQW→NC.
- Ghi chú an toàn: **BẮT BUỘC lắp trở pull-down 10kΩ** tại chân Signal của relay trước khi cấp nguồn lần đầu.
- Sơ đồ cấp nguồn relay: 5V VCC của module relay, COM relay, NO/NC.

---

#### Task F-3: `docs/MQTT_TOPICS.md`
**File tạo mới:** `aeroponics-project/docs/MQTT_TOPICS.md`

**Nội dung cần có:**
- Bảng đầy đủ tất cả topic (từ Sprint 2 doc), phân loại theo Publisher / Subscriber.
- Ghi rõ QoS, Retain flag cho từng topic.
- JSON example payload cho từng topic.
- Quy tắc wildcard: `{device_id}` có format gì, ví dụ thực tế.

---

#### Task F-4: `docs/TUYA_PH_W218_SPEC.md`
**File tạo mới:** `aeroponics-project/docs/TUYA_PH_W218_SPEC.md`

**Nội dung cần có:**
- Bảng DP Code → Field → TypeScript Type → Unit → Range hợp lệ → Scale factor.
- Cách lấy `Local Key` từ Tuya Developer Console (step-by-step).
- Cách xác định `Device IP` trong mạng LAN.
- Ví dụ raw DPS response từ tuyapi.
- Troubleshooting: Device không kết nối được (check key version 3.3 vs 3.4).

---

## 4. TIÊU CHUẨN RÀ SOÁT CỨNG (Sprint 0 Hardened Rules)

### Rule S0-SEC-01: `.env` Không Được Commit Vào Git
```
PASS: .env nằm trong .gitignore
PASS: mosquitto/config/passwd nằm trong .gitignore
PASS: git status không show .env hoặc passwd trong untracked/staged files
FAIL: .env hoặc passwd bị commit vào bất kỳ branch nào
FAIL: Bất kỳ secret nào (password, key) xuất hiện trong lịch sử git
Kiểm tra: git log --all --full-history -- .env
```

### Rule S0-COMPOSE-02: Tất Cả Services Phải Có `healthcheck`
```
PASS: Mọi service trong docker-compose.yml có block healthcheck đầy đủ
       (test, interval, timeout, retries, start_period)
PASS: depends_on dùng condition: service_healthy (không phải service_started)
FAIL: Service không có healthcheck
FAIL: depends_on: [service_name] dạng list (không check health)
```

### Rule S0-NETWORK-03: Database và Redis Không Expose Ra Host (Production)
```
PASS: timescaledb KHÔNG có ports mapping trong docker-compose.yml (production)
PASS: redis KHÔNG có ports mapping trong docker-compose.yml (production)
PASS: Ports 5432 và 6379 chỉ expose trong docker-compose.override.yml (dev only)
FAIL: timescaledb hoặc redis có ports trong docker-compose.yml chính
```

### Rule S0-MQTT-04: Anonymous Access Phải Bị Từ Chối
```
PASS: mosquitto.conf có allow_anonymous false
PASS: scripts/health-check.sh có test case verify anonymous bị reject
FAIL: allow_anonymous true trong mosquitto.conf (production config)
FAIL: Không có test verify anonymous rejection
Kiểm tra: mosquitto_sub -h localhost -t 'test' -C 1 -W 3 → phải exit code != 0
```

### Rule S0-VOLUME-05: Named Volumes Bắt Buộc (Không Dùng Anonymous Volume)
```
PASS: Tất cả persistent data dùng named volume (timescale_data, redis_data, ...)
PASS: Named volumes có name: field để tránh compose project prefix gây confusion
FAIL: volumes: - /var/lib/postgresql/data (anonymous volume)
FAIL: Volume không có explicit name → khó identify khi docker volume ls
```

### Rule S0-SCRIPT-06: setup.sh Phải Idempotent
```
PASS: Chạy setup.sh lần 2 không gây lỗi, không xóa data, không ghi đè không hỏi
PASS: Script check file đã tồn tại trước khi tạo mới
FAIL: Script crash nếu passwd file đã tồn tại
FAIL: Script không check docker version và chạy trên Docker < 24 gây lỗi khó debug
```

### Rule S0-DB-07: init-db.sql Phải Idempotent
```
PASS: Tất cả CREATE TABLE dùng IF NOT EXISTS
PASS: create_hypertable dùng if_not_exists => TRUE
PASS: CREATE ROLE dùng DO $$ IF NOT EXISTS $$ block
FAIL: SQL crash khi chạy lần 2 (database đã có tables)
Kiểm tra: Chạy docker compose down -v; docker compose up -d → DB re-create OK
          Chạy docker compose restart timescaledb → init script không crash
```

---

*Sprint 0 Planning — Khởi tạo bởi Baseline Agent ngày 2026-07-30*
*Thực thi: Sprint 0 Infrastructure Agent (không phụ thuộc Sprint nào — chạy đầu tiên)*
