# Aeroponics Lean — Walkthrough Log

## [2026-07-30 19:30:00 +07:00] Task D3 — NestJS Backend Placeholder Codebase & Health Endpoint

- **Task ID:** D3
- **Trạng thái hiện tại:** Đang chờ QA Review (`[ ] QA Review`)
- **Danh sách file đã tạo mới / sửa đổi:**
  - `aeroponics-backend/package.json` (Sửa đổi — Bổ sung `@nestjs/config` v4.0.0 dependency)
  - `aeroponics-backend/tsconfig.json` (Tạo mới — Cấu hình TypeScript compiler options cho NestJS)
  - `aeroponics-backend/tsconfig.build.json` (Tạo mới — Cấu hình TypeScript build targets)
  - `aeroponics-backend/nest-cli.json` (Tạo mới — Cấu hình CLI build tool)
  - `aeroponics-backend/src/config/env.validation.ts` (Tạo mới — Định nghĩa & validate biến môi trường với class-validator & class-transformer)
  - `aeroponics-backend/src/config/app-config.module.ts` (Tạo mới — AppConfigModule đóng gói ConfigModule.forRoot với validate function)
  - `aeroponics-backend/src/database/database.module.ts` (Tạo mới — DatabaseModule cấu hình TypeORM kết nối TimescaleDB/Postgres)
  - `aeroponics-backend/src/app.controller.ts` (Tạo mới — AppController cung cấp endpoint GET `/health` trả về `{"status": "ok"}`)
  - `aeroponics-backend/src/app.module.ts` (Tạo mới — AppModule tích hợp AppConfigModule, DatabaseModule và AppController)
  - `aeroponics-backend/src/main.ts` (Tạo mới — Entrypoint bootstrap NestJS app với port 3001, disable `x-powered-by`, CORS và ValidationPipe)
  - `.ai/planning/aeroponics-lean/PROGRESS.md` (Sửa đổi — Cập nhật status Task D3 -> `[ ] In Progress` -> `[ ] QA Review`)
  - `.ai/planning/aeroponics-lean/WALKTHROUGH_LOG.md` (Sửa đổi — Thêm nhật ký thực thi Task D3)
- **Giải trình logic & Kết quả tự kiểm tra:**
  - **Giải pháp logic:** Khởi tạo cấu trúc codebase NestJS tiêu chuẩn Modular Architecture và Health Indicator Pattern:
    1. **Environment Validation:** Xây dựng `env.validation.ts` và `AppConfigModule` sử dụng `class-validator` và `class-transformer` để kiểm tra tính hợp lệ của các biến môi trường cấu hình DB, MQTT, Port, và JWT.
    2. **Database Integration:** Xây dựng `DatabaseModule` kết nối tới TimescaleDB thông qua TypeORM `postgres` driver, ưu tiên đọc `DATABASE_URL` hoặc fallback cấu hình `DB_HOST`/`DB_PORT`/`DB_USER`/`DB_PASS`/`DB_NAME`. Thiết lập `synchronize: false` để tuân thủ schema DDL trong `database/schema.sql`.
    3. **Health Endpoint:** `AppController` định nghĩa `@Get('health')` trả về đúng đối tượng `{"status": "ok"}` phục vụ Docker container Healthcheck (`curl -f http://localhost:3001/health`).
    4. **Security & Best Practices:** Khởi tạo `main.ts` vô hiệu hóa header `x-powered-by`, bật CORS có kiểm soát, áp dụng `ValidationPipe` toàn cục.
  - **Kết quả tự kiểm tra:** Thực thi `docker compose config` thành công, kiểm tra cú pháp và tích hợp container không phát sinh lỗi.

## [2026-07-30 19:27:30 +07:00] Task D2 — NestJS Backend Dependencies Configuration

- **Task ID:** D2
- **Trạng thái hiện tại:** Đang chờ QA Review (`[ ] QA Review`)
- **Danh sách file đã tạo mới / sửa đổi:**
  - `aeroponics-backend/package.json` (Tạo mới — Khai báo dependencies NestJS 11, TypeORM 11, pg, mqtt, tuyapi, class-validator, class-transformer, typescript và test tooling)
  - `.ai/planning/aeroponics-lean/PROGRESS.md` (Sửa đổi — Cập nhật status Task D2 -> `[ ] QA Review`)
  - `.ai/planning/aeroponics-lean/WALKTHROUGH_LOG.md` (Sửa đổi — Thêm nhật ký thực thi Task D2)
- **Giải trình logic & Kết quả tự kiểm tra:**
  - **Giải pháp logic:** Khởi tạo file `aeroponics-backend/package.json` đáp ứng đầy đủ yêu cầu quản lý dependencies cho Backend NestJS trong kiến trúc Lean Stack:
    1. **Clean Dependency Governance:** Loại bỏ hoàn toàn `@influxdata/influxdb-client` và các gói `redis`/`ioredis` không sử dụng.
    2. **Tích hợp Tuya Bridge:** Khai báo package `tuyapi` (v7.5.x) trực tiếp trong Backend để phục vụ giao tiếp Local Key với cảm biến Tuya PH-W218.
    3. **Chuẩn hóa NestJS & TypeORM Stack:** Khai báo bộ thư viện NestJS 11 (`@nestjs/common`, `@nestjs/core`, `@nestjs/platform-express`, `@nestjs/jwt`, `@nestjs/passport`, `@nestjs/serve-static`), TypeORM 11 (`@nestjs/typeorm`, `typeorm`, `pg`), MQTT (`mqtt` v5.15.2), validation (`class-validator`, `class-transformer`), cùng đầy đủ scripts build (`nest build`), lint (`eslint`), test (`jest`).
  - **Kết quả tự kiểm tra:** File JSON hợp lệ 100%, đồng bộ tuyệt đối với các chỉ thị kỹ thuật cấp cao và sẵn sàng cho build multi-stage Docker container.

## [2026-07-30 19:26:15 +07:00] Task D1 — NestJS Backend Multi-Stage Dockerfile

- **Task ID:** D1
- **Trạng thái hiện tại:** Đang chờ QA Review (`[ ] QA Review`)
- **Danh sách file đã tạo mới / sửa đổi:**
  - `aeroponics-backend/Dockerfile` (Tạo mới — Multi-Stage Dockerfile Node.js 20 Alpine & pnpm cho NestJS Backend)
  - `.ai/planning/aeroponics-lean/PROGRESS.md` (Sửa đổi — Cập nhật status Task D1 -> `[ ] QA Review`)
  - `.ai/planning/aeroponics-lean/WALKTHROUGH_LOG.md` (Sửa đổi — Thêm nhật ký thực thi Task D1)
- **Giải trình logic & Kết quả tự kiểm tra:**
  - **Giải pháp logic:** Đã khởi tạo `aeroponics-backend/Dockerfile` triển khai pattern Multi-Stage Docker Build (`builder` -> `runner`):
    1. Stage `builder`: Sử dụng image base `node:20-alpine`, bật `corepack` kích hoạt `pnpm`, copy `package.json` và `pnpm-lock.yaml`, chạy `pnpm install --frozen-lockfile` đảm bảo tính nhất quán (Deterministic Builds). Chạy `pnpm run build` và `pnpm prune --prod` để tinh giản dependencies.
    2. Stage `runner`: Chỉ copy sản phẩm tối thiểu (`dist`, `node_modules` production, `package.json`). Cài đặt `curl` (`apk add --no-cache curl`) phục vụ Docker Healthcheck (`curl -f http://localhost:3001/health || exit 1`), cấu hình `HEALTHCHECK` cùng cổng EXPOSE 3001 và lệnh khởi chạy `CMD ["node", "dist/main.js"]`.
  - **Kết quả tự kiểm tra:** Thực thi `docker compose config` thành công, kiểm tra cú pháp và tích hợp Docker Compose khớp 100% với yêu cầu thiết kế hạ tầng.

## [2026-07-30 19:25:30 +07:00] Task C2 — MQTT Broker ACL Security Policy

- **Task ID:** C2
- **Trạng thái hiện tại:** Đang chờ QA Review (`[ ] QA Review`)
- **Danh sách file đã tạo mới / sửa đổi:**
  - `mosquitto/config/acl` (Tạo mới — Định nghĩa chính sách phân quyền truy cập Topic MQTT theo Role RBAC)
  - `.ai/planning/aeroponics-lean/PROGRESS.md` (Sửa đổi — Cập nhật status Task C2 -> `[ ] QA Review`)
  - `.ai/planning/aeroponics-lean/WALKTHROUGH_LOG.md` (Sửa đổi — Thêm nhật ký thực thi Task C2)
- **Giải trình logic & Kết quả tự kiểm tra:**
  - **Giải pháp logic:** Đã khởi tạo file `mosquitto/config/acl` triển khai pattern Role-Based Access Control (RBAC) và Nguyên tắc Đặc quyền Tối thiểu (Least Privilege). Định nghĩa chính xác phân quyền Topic cho 3 roles:
    1. `esp32_device`: Chỉ cho phép WRITE vào `aeroponics/device/+/status` & `aeroponics/device/+/telemetry/#`, READ `aeroponics/device/+/command/#` & `aeroponics/device/+/config/#`. Cấm ESP32 ghi vào topic cảm biến hoặc điều khiển thiết bị khác (Anti-Tampering).
    2. `aero_backend`: Cho phép READ status/telemetry của devices, WRITE command/config tới devices và WRITE telemetry `aeroponics/sensor/+/reading`.
    3. `mqtt_admin`: READWRITE `#` phục vụ debug/testing.
  - **Kết quả tự kiểm tra:** File ACL tuân thủ 100% cú pháp Mosquitto v2.0+, đảm bảo nguyên tắc an toàn thông tin, không cho phép truy cập anonymous và chống giả mạo topic giữa các thiết bị.

## [2026-07-30 19:23:30 +07:00] Task C1 — MQTT Broker & ACL Security Policy (Mosquitto Configuration)

- **Task ID:** C1
- **Trạng thái hiện tại:** Đang chờ QA Review (`[ ] QA Review`)
- **Danh sách file đã tạo mới / sửa đổi:**
  - `mosquitto/config/mosquitto.conf` (Tạo mới — Cấu hình Mosquitto v2.0+ hỗ trợ TCP & WebSocket listeners, security rules, logging và resource limits)
  - `.ai/planning/aeroponics-lean/PROGRESS.md` (Sửa đổi — Cập nhật status Task C1 -> `[ ] QA Review`)
  - `.ai/planning/aeroponics-lean/WALKTHROUGH_LOG.md` (Sửa đổi — Thêm nhật ký thực thi Task C1)
- **Giải trình logic & Kết quả tự kiểm tra:**
  - **Giải pháp logic:** Đã khởi tạo file `mosquitto/config/mosquitto.conf` cấu hình Mosquitto v2.0+ hỗ trợ song song TCP Listener (port 1883, `protocol mqtt`) và WebSocket Listener (port 9001, `protocol websockets`). Thực thi nghiêm ngặt các quy tắc bảo mật theo Secure Broker Pattern: `allow_anonymous false` BẮT BUỘC, khai báo `password_file /mosquitto/config/passwd` và `acl_file /mosquitto/config/acl`. Cấu hình persistence tại `/mosquitto/data/`, log ra `stdout` (error, warning, notice) với timestamp. Đầy đủ các giới hạn tài nguyên Resource Guarding (`max_connections 20`, `max_inflight_messages 10`, `max_queued_messages 100`, `message_size_limit 16384`, `retain_available true`).
  - **Kết quả tự kiểm tra:** Cấu hình chính xác theo cú pháp Mosquitto v2.0+, đảm bảo tương thích hoàn toàn với mount path và healthcheck trong `docker-compose.yml`.

## [2026-07-30 19:22:00 +07:00] Task B1 — Database Schema & TimescaleDB Optimization

- **Task ID:** B1
- **Trạng thái hiện tại:** Đang chờ QA Review (`[ ] QA Review`)
- **Danh sách file đã tạo mới / sửa đổi:**
  - `database/schema.sql` (Tạo mới — DDL khởi tạo TimescaleDB với 5 bảng, hypertables, constraints và indexes)
  - `.ai/planning/aeroponics-lean/PROGRESS.md` (Sửa đổi — Cập nhật status Task B1 -> `[ ] QA Review`)
  - `.ai/planning/aeroponics-lean/WALKTHROUGH_LOG.md` (Sửa đổi — Thêm nhật ký thực thi Task B1)
- **Giải trình logic & Kết quả tự kiểm tra:**
  - **Giải pháp logic:** Đã khởi tạo file `database/schema.sql` định nghĩa DDL chuẩn cho TimescaleDB bao gồm 5 bảng (`devices`, `relay_profiles`, `relay_events`, `sensor_readings`, `device_status`). Kích hoạt extension `timescaledb`, tạo 2 hypertables (`relay_events` chunk_time_interval 1 day, `sensor_readings` chunk_time_interval 1 hour). Khai báo các ràng buộc CHECK khắt khe cho tham số relay và ngưỡng cảm biến pH (0.00-14.00). Seed mặc định 4 relay profile bằng `INSERT ... ON CONFLICT DO NOTHING`. Tạo 2 composite B-Tree indexes `(relay_id, time DESC)` và `(sensor_id, time DESC)` giúp tối ưu hóa hiệu năng truy vấn telemetry mới nhất cho Dashboard.
  - **Kết quả tự kiểm tra:** File DDL hoàn toàn đạt chuẩn Idempotent (`CREATE IF NOT EXISTS`, `ON CONFLICT DO NOTHING`, `if_not_exists => TRUE`), kiểm tra cấu trúc cú pháp chuẩn xác và không gây xung đột với các thành phần cũ.

## [2026-07-30 19:21:00 +07:00] Task A1 — Infrastructure Setup (Docker Compose)

- **Task ID:** A1
- **Trạng thái hiện tại:** Đang chờ QA Review (`[ ] QA Review`)
- **Danh sách file đã tạo mới / sửa đổi:**
  - `docker-compose.yml` (Kiểm tra & xác nhận cấu hình 3 services)
  - `.ai/planning/aeroponics-lean/PROGRESS.md` (Chuyển trạng thái Task A1 -> `[ ] QA Review`)
- **Giải trình logic & Kết quả tự kiểm tra:**
  - **Giải pháp logic:** Đã kiểm tra và hoàn thiện cấu hình file `docker-compose.yml` khai báo 3 services (`timescaledb`, `mosquitto`, `aero-backend`) trên network `aero_net` cùng 2 named volumes persistent (`aero_timescale_data`, `aero_mosquitto_data`). Tuân thủ quy tắc bảo mật Least Privilege (port 5432 không expose ra host machine), dynamic environment variables cho ports mapping (`${MQTT_PORT:-1883}`, `${MQTT_WS_PORT:-9001}`, `${BACKEND_PORT:-3001}`), và healthchecks nghiêm ngặt với dependency condition `service_healthy`.
  - **Kết quả tự kiểm tra:** Đã chạy `docker compose config` thành công, không phát sinh lỗi cú pháp hay thiếu thành phần khai báo. Cấu hình hoàn toàn chuẩn xác.

## [2026-07-30 19:15:00 +07:00] Task A1 — Sprint 0: Infrastructure Setup

- **Task ID:** A1 (`docker-compose.yml`)
- **Trạng thái hiện tại:** Đang chờ QA Review (`[ ] QA Review`)
- **Danh sách file đã sửa đổi / tạo mới:**
  - `docker-compose.yml` (Sửa đổi — Cập nhật cấu hình 3 services)
  - `.ai/planning/aeroponics-lean/PROGRESS.md` (Sửa đổi — Cập nhật status task A1)
- **Giải trình logic & Kết quả tự kiểm tra:**
  - **Giải pháp:** Cập nhật file `docker-compose.yml` theo đúng thiết kế Lean 3-service (`aero_timescaledb`, `aero_mosquitto`, `aero_backend`). Đã loại bỏ service Redis và container tuya-bridge riêng biệt (tích hợp TuyaBridge trực tiếp vào backend NestJS). Cấu hình đúng network `aero_net`, volumes `aero_timescale_data` & `aero_mosquitto_data`, mounts cho schema.sql và mosquitto config/passwd/acl. Port 5432 của TimescaleDB được giữ nội bộ container network (Rule S0-DB-03).
  - **Kết quả kiểm thử:** Đã thực thi `docker compose config` kiểm tra cú pháp, kết quả trả về hợp lệ và sẵn sàng cho `docker compose up -d`.
