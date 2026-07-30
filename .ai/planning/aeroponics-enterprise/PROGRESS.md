# Aeroponics Enterprise — Progress Tracker

## Started

- **Thời điểm khởi tạo:** 2026-07-30 11:45:41 UTC+07:00 (Asia/Ho_Chi_Minh)
- **Execution Agent:** Gemini
- **Sprint hiện tại:** Sprint 0 — Docker Infrastructure Setup (Local Dev Environment)

## Reference Plan

- **Thư mục kế hoạch:** `aeroponics-project/.ai/planning/aeroponics-enterprise/`
- **Master plan:** `aeroponics-project/.ai/planning/aeroponics-enterprise/README.md`
- **Sprint đang tham chiếu:** `aeroponics-project/.ai/planning/aeroponics-enterprise/sprint_0.md`

## Addition Plan

- **Chưa có yêu cầu phát sinh.** Mọi thay đổi về phạm vi, kiến trúc, bảo mật hoặc thứ tự thực hiện phải được bổ sung vào phần này và được phê duyệt trước khi triển khai.

## Quy ước trạng thái

| Status | Ý nghĩa |
|---|---|
| `[ ] Pending` | Task chưa chạm vào. |
| `[ ] In Progress` | Execution Agent đang viết code. |
| `[ ] QA Review` | Code đã viết xong, đang chờ rà soát chất lượng. |
| `[x] Done` | Đã qua vòng review nghiêm ngặt và được duyệt. |

## Track A — Docker Compose Files

| Task ID | Mô tả Task | Status | Note / Chỉ thị kỹ thuật bắt buộc |
|---|---|---|---|
| A1 | Tạo `aeroponics-project/docker-compose.yml` cho TimescaleDB, Redis, Mosquitto, NestJS Backend và Tuya Bridge; khai báo network, named volumes, dependency chain và health check. | `[ ] QA Review` | Áp dụng **Infrastructure as Code** và nguyên tắc **least exposure**: production compose chỉ expose Mosquitto `1883/9001` và Backend `3001`; DB/Redis chỉ ở `aeroponics_net`. Mọi service phải có healthcheck hoàn chỉnh và `depends_on.condition: service_healthy`; dùng named volumes có `name:` rõ ràng; image tag phải được pin theo major/version đã phê duyệt, không dùng `latest` nếu không có lý do/kiểm thử tái lập. Không đưa secret vào YAML hoặc image layer. |
| A2 | Tạo `aeroponics-project/docker-compose.override.yml` cho phát triển local: bind mount/hot reload, debugger và port DB/Redis khi cần. | `[ ] Pending` | Tách tuyệt đối cấu hình dev khỏi production theo **configuration overlay pattern**. Chỉ override phần cần thiết, không sao chép toàn bộ compose để tránh drift. Nếu dev Mosquitto cho anonymous access, phải là profile/override tường minh, không được là mặc định production; ghi rõ cảnh báo và bảo đảm lệnh kiểm thử production luôn dùng `mosquitto.conf` với `allow_anonymous false`. Không commit secret; ưu tiên file `.example` nếu override cần giá trị local. |

## Track B — Mosquitto Configuration

| Task ID | Mô tả Task | Status | Note / Chỉ thị kỹ thuật bắt buộc |
|---|---|---|---|
| B1 | Tạo `mosquitto/config/mosquitto.conf` cấu hình production: MQTT/WebSocket listeners, authentication, ACL, persistence, logging, connection limits và retained messages. | `[ ] Pending` | Tuân thủ **defense in depth**: `allow_anonymous false`, dùng `password_file` và `acl_file`, không xuất hiện password plaintext. Bật persistence bằng named volume, QoS/retain phù hợp LWT, giới hạn inflight/queued message và payload để giảm DoS. Cấu hình listener WebSocket phải khai báo `protocol websockets` đúng listener; kiểm thử config bằng container trước khi merge. TLS-ready phải có đường dẫn/cơ chế mở rộng rõ ràng, không hạ bảo mật để “chạy được”. |
| B2 | Tạo `mosquitto/config/mosquitto.dev.conf` phục vụ debug phát triển. | `[ ] Pending` | Dùng **environment separation**: chỉ khác production ở các option phục vụ debug được liệt kê rõ. Anonymous access (nếu thật sự cần) chỉ được phép trong local dev, tuyệt đối không mount nhầm vào production/CI security test; đặt comment cảnh báo. Duy trì các giới hạn, persistence và listener tương thích production để tránh sai lệch hành vi. |
| B3 | Tạo `mosquitto/config/acl` phân quyền topic cho ESP32, NestJS, Tuya Bridge và MQTT Admin. | `[ ] Pending` | Áp dụng **least privilege / deny by default**: ESP32 chỉ write status/telemetry và read command/config của chính thiết bị; bridge chỉ write sensor topic; backend chỉ có quyền thật sự cần. Không dùng wildcard `#` ngoài admin giám sát. Xác thực ACL bằng test publish/subscribe dương tính và âm tính cho từng principal; mọi topic mới phải cập nhật `docs/MQTT_TOPICS.md`. |

## Track C — Database Initialization

| Task ID | Mô tả Task | Status | Note / Chỉ thị kỹ thuật bắt buộc |
|---|---|---|---|
| C1 | Tạo `scripts/init-db.sql`: enable TimescaleDB, application role, schema `relay_events`, `sensor_readings`, `device_status`, hypertable, index, compression và retention. | `[ ] Pending` | Dùng **migration-first / idempotent bootstrap**: tất cả DDL phải chạy lặp lại an toàn; hypertable policy không được tạo trùng. Áp dụng **least-privilege DB role** và không hardcode password `PLACEHOLDER_REPLACED_BY_ENV` vào SQL chạy thực tế—dùng cơ chế init an toàn từ biến môi trường hoặc role provisioning đã được kiểm soát. Ràng buộc `relay_id`, kiểu dữ liệu, index theo access pattern time-series; kiểm thử cold start, restart và persistence. Không dùng `synchronize:true` để thay thế migration ở Sprint sau. |

## Track D — Setup & Health Check Scripts

| Task ID | Mô tả Task | Status | Note / Chỉ thị kỹ thuật bắt buộc |
|---|---|---|---|
| D1 | Tạo `scripts/setup.sh` tương thích macOS zsh và Ubuntu 22.04: kiểm tra prerequisite, tạo/validate `.env`, tạo thư mục, sinh MQTT password file, kiểm tra port và in tóm tắt. | `[ ] Pending` | Viết shell theo **fail-fast, idempotent, secure-by-default** (`set -euo pipefail`, quote toàn bộ biến, kiểm tra exit code). Không `source` `.env` theo cách có thể thực thi lệnh tùy ý; parse/validate allowlist biến hoặc dùng Docker Compose env parsing an toàn. Không echo secret, không ghi đè `.env`/`passwd` khi chưa có xác nhận rõ; kiểm tra Docker Compose plugin version và port host. Password file phải tạo với quyền truy cập tối thiểu. |
| D2 | Tạo `scripts/health-check.sh` kiểm tra health của các container, MQTT auth/anonymous denial, TimescaleDB extension/tables/hypertables, Redis và tổng hợp kết quả. | `[ ] Pending` | Áp dụng **automated verification gate**: mỗi probe có timeout, mã thoát chính xác và summary cuối; failed check phải làm script exit `1`. Test an ninh bắt buộc gồm auth hợp lệ **và** anonymous bị từ chối. Không in secrets qua command log; dùng container/network nội bộ cho probe. Kiểm tra đúng health status thay vì chỉ container running; script phải an toàn để chạy nhiều lần. |

## Track E — Environment Variables

| Task ID | Mô tả Task | Status | Note / Chỉ thị kỹ thuật bắt buộc |
|---|---|---|---|
| E1 | Tạo `aeroponics-project/.env.example` chứa đầy đủ template MQTT, TimescaleDB, Redis, Backend, Tuya Bridge và firmware. | `[ ] Pending` | Thực thi **twelve-factor configuration**: chỉ dùng placeholder không có giá trị secret thật; document format, required/default và phạm vi của từng biến. Đồng bộ biến với Compose/backend/bridge, sau đó validate bằng setup script. Bổ sung `.env`, `mosquitto/config/passwd`, data/log vào `.gitignore`; chạy secret scan và `git status` để bảo đảm không theo dõi credential. |

## Track F — Documentation

| Task ID | Mô tả Task | Status | Note / Chỉ thị kỹ thuật bắt buộc |
|---|---|---|---|
| F1 | Tạo `docs/ARCHITECTURE.md`: kiến trúc các layer, vai trò service, port mapping, Docker volumes và các ADR chính. | `[ ] Pending` | Dùng **Architecture Decision Record (ADR)** ngắn gọn cho mỗi lựa chọn có đánh đổi (Mosquitto, TimescaleDB, Tuya Local Key), bao gồm context, decision và consequences. Sơ đồ phải khớp cấu hình Compose thực tế; thể hiện security boundary, luồng dữ liệu và internal/external ports để tránh documentation drift. |
| F2 | Tạo `docs/HARDWARE_PINOUT.md`: GPIO ESP32-S3, relay, DS3231, sơ đồ kết nối và lưu ý an toàn nguồn. | `[ ] Pending` | Đây là tài liệu **safety-critical**: xác nhận xung đột GPIO (kế hoạch hiện có đồng thời gán GPIO 2 cho Relay 2 và LED Status) trước khi chốt; không để hai chức năng dùng chung một pin nếu chưa có phần cứng multiplexing được phê duyệt. Nêu rõ relay Active HIGH, điện trở pull-down 10kΩ và yêu cầu firmware set LOW trước `pinMode(OUTPUT)`. Không suy đoán điện áp/công suất relay—đánh dấu cần xác thực nếu thiếu sơ đồ thực tế. |
| F3 | Tạo `docs/MQTT_TOPICS.md`: taxonomy topic, publisher/subscriber, QoS, retain, JSON payload và quy tắc wildcard. | `[ ] Pending` | Dùng **contract-first messaging**: chuẩn hóa schema/versioning, ownership, QoS/retain và validation cho từng topic. LWT bắt buộc QoS 1 + retain; không ghi PII/secret vào payload. Topic wildcard phải có naming convention và ACL mapping tương ứng; tài liệu là nguồn hợp đồng cho firmware, bridge, backend và test. |
| F4 | Tạo `docs/TUYA_PH_W218_SPEC.md`: DP mapping, type/unit/range/scale, lấy Local Key, xác định IP LAN, raw DPS mẫu và troubleshooting. | `[ ] Pending` | Bảo vệ **secret lifecycle**: hướng dẫn lấy Local Key không được chứa key thật hoặc khuyến khích ghi vào source/log; mọi ví dụ phải redact. DP parser phải xử lý dữ liệu thiếu/out-of-range bằng validation, nullable fields và warning không lộ secret. Ghi rõ giả định protocol/version (3.3/3.4), scale factor và nguồn cần xác minh trên phần cứng để không biến mapping suy đoán thành hợp đồng cố định. |

## Cổng nghiệm thu Sprint 0

- [ ] `docker compose up -d` khởi động toàn bộ stack thành công và mọi service healthy trong 60 giây.
- [ ] MQTT authentication hợp lệ; anonymous access bị từ chối trong cấu hình production.
- [ ] TimescaleDB extension, database/schema/hypertable và Redis được health-check xác nhận.
- [ ] Production không expose PostgreSQL/Redis ra host; persistent named volumes giữ dữ liệu sau khi tái tạo container.
- [ ] `setup.sh` và `health-check.sh` chạy idempotent trên macOS zsh và Ubuntu 22.04.
- [ ] Tài liệu kiến trúc, pinout, MQTT topics và Tuya specification hoàn chỉnh, nhất quán với cấu hình thực tế.
- [ ] Không có secret, `.env` hoặc `mosquitto/config/passwd` được theo dõi bởi Git.
