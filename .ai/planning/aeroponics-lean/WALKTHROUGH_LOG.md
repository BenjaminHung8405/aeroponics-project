# Aeroponics Lean — Walkthrough Log

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
