# Aeroponics Lean — Progress Tracker

## Quyết định kiến trúc cuối cùng (2026-07-30)

| Layer | Công nghệ | Lý do |
|---|---|---|
| **Database** | TimescaleDB (`timescale/timescaledb:latest-pg15`) | Tái dùng từ `mushroom-cp`. PostgreSQL chuẩn + hypertable cho time-series sensor data. |
| **Backend** | NestJS (Node.js + TypeScript) | Tái dùng ~70% boilerplate từ `mushroom-backend`: DatabaseModule, MqttModule, TypeORM, class-validator |
| **Tuya Bridge** | Tích hợp vào NestJS (`TuyaBridgeModule` + `tuyapi`) | Không cần container riêng |
| **MQTT Broker** | Mosquitto | Nhẹ, chuẩn |
| **Frontend** | Vanilla HTML + Chart.js CDN | Single file, no build step |
| **Cache** | ❌ Không có | Không cần |
| **InfluxDB** | ❌ Không có | TimescaleDB đủ cho 8.640 record/ngày |
| **Redis** | ❌ Không có | Không cần |

**Nguồn tham chiếu boilerplate:** `/Users/benjaminhung8405/Code/mushroom-cp/mushroom-backend/`

---

## Reference Plan

- **Thư mục:** `aeroponics-project/.ai/planning/aeroponics-lean/`
- [README.md](./README.md) — Master context, tech stack, kiến trúc
- [sprint_0.md](./sprint_0.md) — Infrastructure (3 containers + TimescaleDB schema)
- [sprint_1.md](./sprint_1.md) — Firmware core (không đổi)
- [sprint_2.md](./sprint_2.md) — Firmware MQTT (không đổi)
- [sprint_3.md](./sprint_3.md) — NestJS Backend
- [sprint_4.md](./sprint_4.md) — HTML Dashboard

---

## Sprint 0 — Infrastructure

| Task | Mô tả | Status |
|---|---|---|
| A1 | `docker-compose.yml` (3 services: timescaledb + mosquitto + aero-backend) | `[ ] Pending` |
| B1 | `database/schema.sql` (TimescaleDB: extension, 5 tables, 2 hypertables, indexes) | `[ ] Pending` |
| C1 | `mosquitto/config/mosquitto.conf` | `[ ] Pending` |
| C2 | `mosquitto/config/acl` | `[ ] Pending` |
| D1 | `aeroponics-backend/Dockerfile` (Node.js 20 multi-stage) | `[ ] Pending` |
| D2 | `aeroponics-backend/package.json` (NestJS, TypeORM, tuyapi — không có @influxdata) | `[ ] Pending` |
| D3 | NestJS placeholder: `/health` endpoint + DatabaseModule connect | `[ ] Pending` |
| E1 | `.env.example` (không có INFLUXDB_*, không có REDIS_*) | `[x] Done` |
| F1 | `scripts/setup.sh` | `[ ] Pending` |
| F2 | `scripts/health-check.sh` | `[ ] Pending` |

**Cổng nghiệm thu:**
- [ ] `docker compose up -d` → 3 services healthy trong 90s
- [ ] TimescaleDB: extension enabled, 5 tables, 2 hypertables
- [ ] MQTT: auth required, anonymous rejected
- [ ] `GET /health` → 200
- [ ] Volume persistent

---

## Sprint 1 — Firmware Core (không đổi)

Xem [sprint_1.md](./sprint_1.md)

- [ ] platformio.ini + partitions.csv
- [ ] config.h (pinout, defaults)
- [ ] nvs_storage.h/cpp
- [ ] rtc_manager.h/cpp
- [ ] relay_controller.h/cpp
- [ ] schedule_manager.h/cpp
- [ ] main.cpp (safe boot sequence)

---

## Sprint 2 — Firmware MQTT (không đổi)

Xem [sprint_2.md](./sprint_2.md)

- [ ] mqtt_client.h/cpp
- [ ] Tích hợp vào main.cpp

---

## Sprint 3 — NestJS Backend

| Task | Mô tả | Nguồn | Status |
|---|---|---|---|
| A1 | `database/` (copy & adapt từ mushroom-cp) | `mushroom-cp/src/database/` | `[ ] Pending` |
| A2 | `config/` (adapt: bỏ InfluxDB, thêm Tuya vars) | `mushroom-cp/src/config/` | `[ ] Pending` |
| A3 | `mqtt/` (adapt cho topics Aeroponics) | `mushroom-cp/src/mqtt/` | `[ ] Pending` |
| B1 | `relay/entities/relay-profile.entity.ts` | NEW | `[ ] Pending` |
| B2 | `relay/entities/relay-event.entity.ts` | NEW | `[ ] Pending` |
| B3 | `sensor/entities/sensor-reading.entity.ts` | NEW | `[ ] Pending` |
| B4 | `device/entities/device-status.entity.ts` | NEW | `[ ] Pending` |
| C1 | `relay/relay.service.ts` | NEW | `[ ] Pending` |
| C2 | `relay/relay.controller.ts` | NEW | `[ ] Pending` |
| D1 | `sensor/sensor.service.ts` | NEW | `[ ] Pending` |
| D2 | `sensor/sensor.controller.ts` | NEW | `[ ] Pending` |
| E1 | `device/device.service.ts` | Adapt từ mushroom-cp | `[ ] Pending` |
| F1 | `tuya-bridge/tuya-bridge.service.ts` (tuyapi) | NEW | `[ ] Pending` |
| G1 | `events/events.gateway.ts` (native WS) | NEW | `[ ] Pending` |
| H1 | `app.module.ts` (wire tất cả modules) | Adapt | `[ ] Pending` |
| H2 | `main.ts` (ServeStatic cho Dashboard) | Adapt | `[ ] Pending` |

---

## Sprint 4 — HTML Dashboard

| Task | Mô tả | Status |
|---|---|---|
| A | CSS Design System (dark theme, CSS vars) | `[ ] Pending` |
| B | Relay Cards: HTML render + SVG countdown ring | `[ ] Pending` |
| C | Sensor Gauges: SVG arc + color coding | `[ ] Pending` |
| D | WebSocketManager class (native WS, auto-reconnect) | `[ ] Pending` |
| E | Chart.js: pH + Temperature 24h | `[ ] Pending` |
| F | Schedule Form modal → PUT /api/relay/:id/profile | `[ ] Pending` |
| G | Override Control modal → POST /api/relay/:id/override | `[ ] Pending` |
| H | Toast notifications + connection banner | `[ ] Pending` |
| I | Init + load all data từ REST API | `[ ] Pending` |
