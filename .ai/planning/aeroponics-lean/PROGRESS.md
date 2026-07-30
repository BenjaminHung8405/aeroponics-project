# Aeroponics Lean — Progress Tracker (Sprint 0)

## 📌 Started
- **Thời gian khởi tạo:** 2026-07-30T19:19:56+07:00
- **Agent thực thi (Execution Agent):** Gemini

---

## 🎯 Reference Plan
- **Thư mục kế hoạch:** `.ai/planning/aeroponics-lean/`
- **Sprint tham chiếu hiện tại:** [sprint_0.md](file:///Users/benjaminhung8405/Code/aeroponics-project/.ai/planning/aeroponics-lean/sprint_0.md) (Infrastructure Setup — 3 Containers Stack)

---

## 📝 Addition Plan
- **Các yêu cầu phát sinh:** Chưa có (Mặc định tuân thủ 100% phạm vi và quy chuẩn trong `sprint_0.md`).

---

## 🚀 Track Execution Tables (Sprint 0: Infrastructure Setup)

### TRACK A — Docker Compose Topology & Orchestration

| Task ID | Mô tả Task | Status | Note (Chỉ thị kỹ thuật cấp cao từ Senior Solution Architect) |
|---|---|---|---|
| **A1** | Khởi tạo file `docker-compose.yml` khai báo 3 services (`timescaledb`, `mosquitto`, `aero-backend`) trên network `aero_net` cùng 2 named volumes persistent (`aero_timescale_data`, `aero_mosquitto_data`). | [ ] QA Review | • **Design Pattern:** Declarative Infrastructure as Code (IaC), Least Privilege Exposure.<br>• **Security Rules:** CẤM expose port 5432 của `timescaledb` ra host machine (chỉ giao tiếp nội bộ qua network `aero_net`). Khai báo port mapping dynamic qua biến môi trường (`${MQTT_PORT:-1883}`, `${MQTT_WS_PORT:-9001}`, `${BACKEND_PORT:-3001}`).<br>• **Anti-Technical Debt:** Thiết lập strict `healthcheck` cho từng service (`pg_isready` cho DB, `mosquitto_pub` cho Broker, `curl` /health cho Backend). Cài đặt dependency condition `service_healthy` để backend chỉ start khi DB & MQTT đã sẵn sàng. |

---

### TRACK B — Database Schema & TimescaleDB Optimization

| Task ID | Mô tả Task | Status | Note (Chỉ thị kỹ thuật cấp cao từ Senior Solution Architect) |
|---|---|---|---|
| **B1** | Khởi tạo file `database/schema.sql` chứa DDL cho 5 bảng (`devices`, `relay_profiles`, `relay_events`, `sensor_readings`, `device_status`), kích hoạt extension `timescaledb` và khởi tạo hypertables + indexes. | [ ] QA Review | • **Design Pattern:** Time-Series Hypertables Pattern, Idempotent DDL (`CREATE IF NOT EXISTS`).<br>• **Domain Modeling:** 4 Relays (`relay_id` SMALLINT CHECK 1..4) với các cặp thời gian `spray_day_s`, `cooldown_day_s`, `spray_night_s`, `cooldown_night_s`. Hypertables: `relay_events` (chunk 1 day), `sensor_readings` (chunk 1 hour).<br>• **Security & Data Integrity:** Ràng buộc CHECK constraints chặt chẽ cho ngưỡng thời gian (spray 5-300s, cooldown 30-7200s, pH 0.00-14.00). Seed mặc định 4 relay profile bằng `INSERT ... ON CONFLICT DO NOTHING`.<br>• **Performance:** Tạo B-Tree composite index `(relay_id, time DESC)` và `(sensor_id, time DESC)` để tối ưu hóa truy vấn telemetry mới nhất cho Dashboard. |

---

### TRACK C — MQTT Broker & ACL Security Policy

| Task ID | Mô tả Task | Status | Note (Chỉ thị kỹ thuật cấp cao từ Senior Solution Architect) |
|---|---|---|---|
| **C1** | Khởi tạo file `mosquitto/config/mosquitto.conf` cấu hình Mosquitto v2.0 hỗ trợ song song TCP Listener (1883) và WebSocket Listener (9001). | [ ] Pending | • **Design Pattern:** Secure Broker Pattern, Resource Guarding.<br>• **Security Rules:** `allow_anonymous false` BẮT BUỘC. Bật `password_file /mosquitto/config/passwd` và `acl_file /mosquitto/config/acl`. Cấm tuyệt đối kết nối không xác thực.<br>• **Reliability & Limits:** Bật persistence tại `/mosquitto/data/`. Giới hạn `max_connections 20`, `max_inflight_messages 10`, `message_size_limit 16384` để chống DDoS/OOM trong môi trường lab. |
| **C2** | Khởi tạo file `mosquitto/config/acl` định nghĩa chính sách phân quyền truy cập Topic MQTT cho từng Role. | [ ] Pending | • **Design Pattern:** Role-Based Access Control (RBAC) / Principle of Least Privilege.<br>• **Topic Permissions:**<br>  - `esp32_device`: WRITE `aeroponics/device/+/status`, WRITE `aeroponics/device/+/telemetry/#`, READ `aeroponics/device/+/command/#`, READ `aeroponics/device/+/config/#`.<br>  - `aero_backend`: READ status/telemetry, WRITE command/config/sensor readings.<br>  - `mqtt_admin`: READWRITE `#` (chỉ dùng cho debug/testing).<br>• **Anti-Tampering:** ESP32 không có quyền ghi trực tiếp vào topic sensor hoặc command của thiết bị khác. |

---

### TRACK D — NestJS Backend Infrastructure Placeholder

| Task ID | Mô tả Task | Status | Note (Chỉ thị kỹ thuật cấp cao từ Senior Solution Architect) |
|---|---|---|---|
| **D1** | Khởi tạo file `aeroponics-backend/Dockerfile` xây dựng image Node.js 20 Alpine cho Backend. | [ ] Pending | • **Design Pattern:** Multi-Stage Docker Build (`builder` -> `runner`).<br>• **Security & Footprint Optimization:** Chỉ copy `dist`, `node_modules` sản phẩm cuối và `package.json` sang runner stage. Cài đặt `curl` trong runner stage phục vụ Docker Healthcheck.<br>• **Anti-Technical Debt:** Sử dụng `pnpm` với `--frozen-lockfile` để đảm bảo tính đồng nhất môi trường build (Deterministic Builds). |
| **D2** | Cấu hình `aeroponics-backend/package.json` khai báo đầy đủ các dependencies cần thiết (NestJS 11, TypeORM 11, pg, mqtt, tuyapi, class-validator...). | [ ] Pending | • **Clean Dependency Governance:** LOẠI BỎ hoàn toàn `@influxdata/influxdb-client` và Redis packages (không dùng trong stack Lean). THÊM `tuyapi` để kết nối cảm biến Tuya PH-W218 qua Local Key.<br>• **Quality Assurance:** Đảm bảo phiên bản đồng nhất với boilerplate `mushroom-backend` đã kiểm chứng. |
| **D3** | Khởi tạo codebase placeholder NestJS (`src/main.ts`, `src/app.module.ts`, `src/app.controller.ts`) kết nối TimescaleDB và cung cấp endpoint GET `/health`. | [ ] Pending | • **Design Pattern:** Modular Architecture (NestJS Standard), Health Indicator Pattern.<br>• **Implementation Rules:** `AppModule` import `AppConfigModule` (validate env vars với `class-validator`) và `DatabaseModule` (TypeORM Postgres driver). Controller trả về `{"status": "ok"}` trên đường dẫn `/health`.<br>• **Security:** Ẩn x-powered-by header, bật CORS có kiểm soát. |

---

### TRACK E — Environment Configuration Template

| Task ID | Mô tả Task | Status | Note (Chỉ thị kỹ thuật cấp cao từ Senior Solution Architect) |
|---|---|---|---|
| **E1** | Khởi tạo file `.env.example` làm mẫu cấu hình chuẩn cho toàn bộ hệ thống. | [ ] Pending | • **Design Pattern:** Configuration As Code, 12-Factor App Config.<br>• **Security Rules:** CẤM điền password/secret thật vào `.env.example`. Đặt giá trị giữ chỗ rõ ràng (e.g. `CHANGE_ME_DB_PASSWORD`). Bắt buộc kiểm tra file `.env` đã nằm trong `.gitignore`.<br>• **Variables Required:** `DB_*` (TimescaleDB), `MQTT_*` (Mosquitto Admin/Device/Backend), `BACKEND_PORT`, `JWT_SECRET`, `TUYA_*` (`TUYA_DEVICE_IP`, `TUYA_DEVICE_ID`, `TUYA_LOCAL_KEY`). Loại bỏ toàn bộ tiền tố `INFLUXDB_*` và `REDIS_*`. |

---

### TRACK F — DevOps Operational & Health Verification Scripts

| Task ID | Mô tả Task | Status | Note (Chỉ thị kỹ thuật cấp cao từ Senior Solution Architect) |
|---|---|---|---|
| **F1** | Khởi tạo kịch bản Shell Script `scripts/setup.sh` phục vụ khởi tạo hạ tầng 1-click. | [ ] Pending | • **Design Pattern:** Defensive Shell Scripting (`set -euo pipefail`), Idempotent Execution.<br>• **Automation Logic:** 1. Check Docker & Compose CLI; 2. Tạo `.env` từ `.env.example` nếu chưa có; 3. Validate bắt buộc các secret không được giữ nguyên `CHANGE_ME`; 4. Thư mục `mosquitto/config`, `mosquitto/data`, `database`; 5. Tự động sinh `mosquitto/config/passwd` qua lệnh `mosquitto_passwd` (hoặc docker container helper); 6. Kiểm tra xung đột cổng host.<br>• **Compatibility:** Đảm bảo tương thích macOS Zsh và Linux Bash. |
| **F2** | Khởi tạo kịch bản Shell Script `scripts/health-check.sh` rà soát toàn bộ sức khỏe hạ tầng sau khi triển khai. | [ ] Pending | • **Design Pattern:** Automated Infrastructure Verification, Acceptance Testing.<br>• **Verification Checklist:** 1. Kiểm tra 3 container status (`healthy`); 2. Test MQTT Auth (login hợp lệ pass, anonymous fail); 3. Connect TimescaleDB kiểm tra extension `timescaledb` và 5 bảng + 2 hypertables; 4. Test REST `http://localhost:3001/health` == 200 OK.<br>• **Reporting:** Xuất kết quả dạng bảng terminal trực quan với mã màu PASS/FAIL. |

---

## 🛡️ Tiêu chuẩn rà soát chất lượng & Duyệt Task (QA Gateways)
Mọi Task khi chuyển từ `[ ] In Progress` sang `[ ] QA Review` và `[x] Done` phải đi qua các cổng kiểm tra sau:
1. **Zero Secret Leakage:** Không có file `.env`, `passwd` hay private keys nào lọt vào Git tracking.
2. **Container Isolation:** TimescaleDB port 5432 không thể kết nối từ bên ngoài host.
3. **Idempotency:** Lệnh `scripts/setup.sh` và `docker compose up -d` có thể chạy lại nhiều lần mà không sinh lỗi hoặc ghi đè mất dữ liệu persistent volumes.
4. **Health Check Green:** Cả 3 containers đều ở trạng thái `healthy` trong vòng 90 giây.
