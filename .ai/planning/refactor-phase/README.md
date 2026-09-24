# Refactor Phase — Master Planning Context

> **Vai trò tài liệu này:** Nguồn sự thật duy nhất (Single Source of Truth) cho toàn bộ kế hoạch Refactor giai đoạn chuyển hóa Golden Baseline đặc tả thành mã nguồn thực tế. Mọi Agent thực thi **bắt buộc** đọc tài liệu này trước khi bắt đầu bất kỳ Sprint nào.
>
> **Golden Baseline (đã phê duyệt, không thay đổi trong phạm vi refactor):**
> - [`docs/e2e-critical-sequence-flows.md`](../../docs/e2e-critical-sequence-flows.md) — Sequence Flows v2.0
> - [`docs/STATE_MACHINE_MATRIX.md`](../../docs/STATE_MACHINE_MATRIX.md) — State Machine Matrix v1.0.0
> - [`docs/interface-wire-contract.md`](../../docs/interface-wire-contract.md) — Interface & Wire Contract Rev 3

---

## 1. MỤC TIÊU KỸ THUẬT TỔNG QUÁT

Chuyển hóa ba bộ đặc tả Golden Baseline thành mã nguồn production-ready **không làm gián đoạn hạ tầng đang chạy**, tuân theo nguyên tắc:

| Nguyên tắc | Mô tả |
|---|---|
| **Strangler Fig & Neo kiểm thử** | Không refactor đồng thời hai đầu mút; dùng bộ 273/273 native unit tests hiện có làm mỏ neo hồi quy |
| **Top-down Contract, Bottom-up Refactor** | Giữ hợp đồng giao tiếp đã chốt; thứ tự triển khai bắt buộc: RF Wire Codec → Virtual FSM Gateway → Backend Ingestion & Admission → Dashboard UI |
| **Fail-safe bất biến** | Mọi thay đổi firmware không được phá vỡ invariant fail-closed; Local Deadman tại trạm luôn cắt được rơ-le khi mất liên kết |
| **Không persist raw frame** | Byte RF/AGU chỉ là transport tạm thời, không được ghi vào MQTT/DB/audit business record |

### Bài toán cốt lõi theo từng tầng

- **Tầng Edge (ESP32):** Tái cấu trúc codec AGU legacy, cô lập UART HC-12 trên FreeRTOS Core 1, hiện thực hóa virtual FSM `(MacroState, EvidenceStage)` cho 4 trạm, cài đặt Deadman Lease Timer và bảng correlation `pending_command_table`.
- **Tầng Backend (NestJS & TimescaleDB):** Chuẩn hóa topic namespace `aeroponics/v1/...`, tắt Retain trên topic giao dịch, củng cố khóa an toàn DB (UC-BE-10), tối ưu TimescaleDB ingestion với Connection Pool chuyên biệt và Batch Insert.
- **Tầng Dashboard (Next.js):** Khử triệt Optimistic UI — chỉ hiển thị `RUNNING` khi nhận `FLOW_CONFIRMED` từ WebSocket; cấu hình Nginx reverse proxy port 6003.

---

## 2. TECH STACK CỐT LÕI

### 2.1 Edge / Firmware Layer

| Thành phần | Công nghệ | Version |
|---|---|---|
| **MCU** | ESP32-S3 (Dual-core Xtensa LX7) | — |
| **RTOS** | FreeRTOS (tích hợp trong ESP-IDF / Arduino core) | ESP-IDF v5.x |
| **Build System** | PlatformIO | ≥ 6.x |
| **Framework** | Arduino framework cho ESP32 | espressif32 ≥ 6.x |
| **Codec** | `AguLegacyCodec` (namespace `AguLegacy`) | rev hiện tại |
| **RF Transport** | `UartRfTransport` (HardwareSerial UART) | — |
| **RF Host** | `AguLegacyRfHost` (serialized transaction engine) | — |
| **NVS Storage** | ESP-IDF NVS API | built-in |
| **RTC Driver** | RTClib (Adafruit) cho DS3231 qua I2C | ≥ 2.x |
| **MQTT Client** | PubSubClient | ≥ 2.8 |
| **JSON** | ArduinoJson | ≥ 7.x |
| **Watchdog** | ESP32 Task Watchdog Timer (TWDT) | built-in |

### 2.2 Infrastructure Layer

| Thành phần | Công nghệ | Version |
|---|---|---|
| **MQTT Broker** | Eclipse Mosquitto (Docker) | ≥ 2.0 |
| **Container** | Docker + Docker Compose | ≥ 24.x |
| **Database** | TimescaleDB (extension trên PostgreSQL) | PG 15 + TS 2.x |
| **Reverse Proxy** | Nginx (port 6003 trên VPS) | ≥ 1.24 |

### 2.3 Backend Layer

| Thành phần | Công nghệ | Version |
|---|---|---|
| **Runtime** | Node.js | ≥ 20 LTS |
| **Framework** | NestJS | ≥ 10.x |
| **Language** | TypeScript | ≥ 5.x |
| **ORM** | TypeORM | ≥ 0.3.x |
| **Database** | TimescaleDB (via TypeORM) | PG 15 + TS 2.x |
| **MQTT Client** | `mqtt` npm package | ≥ 5.x |
| **WebSocket** | NestJS Gateway (`@nestjs/websockets` + native WS) | — |
| **Auth** | JWT + Passport.js | — |
| **Validation** | class-validator + class-transformer | — |
| **Event Bus** | `@nestjs/event-emitter` | — |

### 2.4 Frontend Layer

| Thành phần | Công nghệ | Version |
|---|---|---|
| **Framework** | Next.js (App Router) | 15 |
| **Language** | TypeScript | ≥ 5.x |
| **UI Components** | shadcn/ui + Radix UI | — |
| **Styling** | Tailwind CSS | ≥ 3.x |
| **Charts** | Recharts hoặc Chart.js | — |
| **Real-time** | Native WebSocket API (không dùng Socket.IO client) | — |
| **State Management** | Zustand | ≥ 4.x |
| **Server State** | TanStack Query | ≥ 5.x |
| **HTTP Client** | Axios | ≥ 1.x |

---

## 3. QUY TẮC VIẾT CODE TOÀN CỤC (Global Coding Conventions)

> **Quy tắc vàng:** Mọi Agent thực thi PHẢI tuân theo toàn bộ các quy tắc dưới đây. Không có ngoại lệ.

### 3.1 Kiến Trúc Tổng Thể

- **Clean Architecture** áp dụng cho tất cả các layer Backend và Frontend.
- Phân tầng rõ ràng: `Infrastructure → Domain/Core → Application → Presentation`.
- **Dependency Inversion:** Tầng trên chỉ phụ thuộc vào Interface, không phụ thuộc vào Implementation cụ thể.
- **Single Responsibility Principle (SRP):** Mỗi module/class/hàm chỉ làm đúng một việc.
- **Open/Closed Principle (OCP):** Mở để mở rộng, đóng để sửa đổi — tránh sửa code cũ khi thêm tính năng mới.
- **Strangler Fig pattern:** Khi thay thế module cũ, giữ module cũ chạy song song cho đến khi module mới pass toàn bộ test; sau đó mới xóa.

### 3.2 Quy Tắc Đặt Tên

**Firmware (C++):**
```text
- Hàm/Phương thức: camelCase              (vd: encodeReadRamBurst, serviceAguLivenessTick)
- Biến thành viên: snake_case với prefix_ (vd: _baud_rate, _initialized, _stats)
- Hằng số/Macro: SCREAMING_SNAKE_CASE    (vd: AGU_LEGACY_ACK_TIMEOUT_MS, RF_UART_DEFAULT_BAUD_RATE)
- Class/Struct/Enum: PascalCase          (vd: AguLegacyCodec, UartRfTransport, AguRfTransactionResult)
- Enum value: SCREAMING_SNAKE_CASE       (vd: PUMP_ON, PUMP_OFF, ACKED, TIMEOUT)
- Namespace: PascalCase                  (vd: AguLegacy)
- File Header: lowercase_underscore.h    (vd: agu_legacy_codec.h)
- File Source: lowercase_underscore.cpp  (vd: agu_legacy_codec.cpp)
```

**Backend TypeScript (NestJS):**
```text
- Class/Interface/Enum/Type: PascalCase  (vd: MqttService, FlowService, CalibrationStatus)
- Hàm/Phương thức: camelCase            (vd: handleMessage, publishTelemetry, getActiveCalibration)
- Biến/Tham số: camelCase               (vd: nodeId, commandId, topic)
- Biến instance private: this.xxx       (vd: this.client, this.connected)
- Hằng số module: UPPER_SNAKE_CASE      (vd: MQTT_EVENTS, DEFAULT_SUBSCRIBE_TOPICS)
- File: kebab-case.suffix.ts            (vd: mqtt.service.ts, flow-event.entity.ts)
- DTO: kebab-case.dto.ts                (vd: create-treatment.dto.ts)
- Spec: kebab-case.spec.ts              (vd: mqtt.service.spec.ts)
```

**Frontend TypeScript (Next.js):**
```text
- Component/Type/Interface/Enum: PascalCase (vd: NodeCard, OutcomeBadge, WsBanner)
- Hook: camelCase prefix "use"          (vd: useNodeStore, useWebSocket, useAuth)
- Constant: UPPER_SNAKE_CASE            (vd: OUTCOME_CONFIG, STALE_THRESHOLD_MS)
- Variabale local: camelCase            (vd: isRunning, nodeStatus)
- File component: PascalCase.tsx        (vd: NodeCard.tsx, LoginForm.tsx)
- File non-component: kebab-case.ts     (vd: use-node-store.ts, types.ts)
- CSS class: Tailwind utility           (vd: bg-white rounded-lg shadow-sm)
```

### 3.3 Quy Tắc Xử Lý Lỗi (Error Handling)

**Firmware (C++):**
- **KHÔNG bao giờ** để hàm quan trọng `void` và bỏ qua return error.
- Tất cả hàm driver (`nvs_*`, `rtc_*`, `mqtt_*`, `encode*`, `transact*`) phải trả về `bool`, `size_t` (0 = fail), hoặc struct kết quả có trường `result`.
- Khi encode fail → return 0; caller phải check `frame_size == 0` trước khi `send()`.
- Khi NVS write/read fail → log error qua `ESP_LOGE` + fallback về giá trị mặc định an toàn.
- Khi MQTT disconnect → trigger reconnect với **Exponential Backoff** (1s → 2s → 4s → max 60s).
- Khi RF transaction timeout → return `AguRfResult::TIMEOUT`; KHÔNG retry无限; respect `AGU_LEGACY_MAX_ATTEMPTS = 3`.
- Relay state mặc định luôn là `OFF` (LOW signal = không kích relay Active HIGH).

**Backend TypeScript:**
- Dùng NestJS `ExceptionFilter` global để bắt tất cả lỗi.
- Không bao giờ để `console.log` trong production code → dùng NestJS `Logger`.
- Tất cả DB operation phải bọc trong `try/catch` và throw `InternalServerErrorException` có message rõ ràng.
- DTO validation bắt buộc với `class-validator` cho mọi endpoint.
- MQTT `handleMessage` phải catch toàn bộ exception, không bao giờ crash event loop trên malformed payload.
- Khi publish MQTT fail → log error, không throw ra ngoài caller nếu caller không cần biết.

**Frontend TypeScript:**
- Tất cả API call dùng `try/catch` + toast notification cho user.
- WebSocket connection error phải hiện UI indicator rõ ràng (không âm thầm fail).
- Khi nhận WebSocket event không hợp lệ → ignore + log warning, không crash render.

### 3.4 Quy Tắc Bảo Mật (Security Rules)

- **KHÔNG** hardcode credential (WiFi SSID/Password, MQTT user/pass, JWT secret) trong source code.
- Firmware: Đọc credential từ NVS sau khi provisioning hoặc từ `secrets.h` (trong `.gitignore`).
- Backend: Đọc credential từ biến môi trường (`.env`), validate qua `@nestjs/config`.
- MQTT Broker: Bắt buộc Authentication. Anonymous access = `false`.
- Docker: Các service không cần expose ra ngoài thì chỉ dùng internal network.
- TimescaleDB chỉ expose port nội bộ trong Docker network (không expose ra host).
- Không commit `.env`, `mosquitto/config/passwd`.
- `synchronize: false` — KHÔNG BAO GIỜ bật TypeORM auto-sync trong production.
- Mọi schema change qua TypeORM migrations.
- Raw RF/AGU bytes: **không được persist** vào MQTT, database hay audit business record.
- Topic transactional (`command`, `ack`, `event`, `telemetry`): **non-retained** bắt buộc.
- Topic `status`/`LWT`: retained = true.

### 3.5 Quy Tắc Git & Commit

```text
- Conventional Commits: feat:, fix:, chore:, docs:, refactor:, test:
- Branch per sprint: refactor/sprint-1-codec, refactor/sprint-2-fsm, refactor/sprint-3-backend, refactor/sprint-4-ui
- Mỗi PR phải có description rõ "What" và "Why"
- Không merge PR khi còn TODO/FIXME chưa giải quyết
- Chạy toàn bộ 273/273 native unit tests trước khi push
```

### 3.6 Quy Tắc Tài Liệu

- Mọi hàm public trong Firmware và Backend phải có comment JSDoc / Doxygen ngắn gọn.
- Mọi MQTT topic mới phải cập nhật vào `docs/interface-wire-contract.md` ngay khi tạo.
- Mọi state transition mới phải cập nhật vào `docs/STATE_MACHINE_MATRIX.md`.
- Mọi opcode/frame layout mới phải cập nhật vào `docs/interface-wire-contract.md`.

---

## 4. CẤU TRÚC SPRINT ROADMAP

```text
Sprint 1  →  Sprint 2  →  Sprint 3  →  Sprint 4
RF Wire      Virtual      Backend       Dashboard
Codec        FSM &        Ingestion &   Sync & E2E
(ESP32)      Safety       Admission     Validation
             Timers       (NestJS)      (Next.js)
             (ESP32)                    + Nginx
```

| Sprint | File kế hoạch | Phạm vi | Phụ thuộc |
|---|---|---|---|
| Sprint 1: Gateway Transport & Codec Refactoring | [sprint_1.md](./sprint_1.md) | ESP32: `agu_legacy_codec`, UART HC-12 isolation | Không có (Bottom-up đầu tiên) |
| Sprint 2: Gateway Virtual FSM & Safety Timers | [sprint_2.md](./sprint_2.md) | ESP32: Virtual FSM, Deadman Lease, correlation table | Sprint 1 PASS |
| Sprint 3: Backend Ingestion & Admission Pipeline | [sprint_3.md](./sprint_3.md) | NestJS: MQTT topic, DB safety lock, TimescaleDB batch | Sprint 2 PASS |
| Sprint 4: Dashboard State Sync & E2E Validation | [sprint_4.md](./sprint_4.md) | Next.js: Optimistic UI removal, Nginx, E2E | Sprint 3 PASS |

---

## 5. CỔNG KIỂM SOÁT CHẤT LƯỢNG (Quality Gates)

| Gate | Phạm vi | Tiêu chí PASS |
|---|---|---|
| **Gate 1** | Firmware Compilation & Memory Budget | Build sạch trên PlatformIO; không cấp phát heap động trong vòng lặp RT; RAM correlation table < 20 KB |
| **Gate 2** | Hardware-in-the-Loop & Anti-Collision | 4 trạm ổn định trên 433 MHz half-duplex; không packet collision / rụng byte ở polling 1s |
| **Gate 3** | Fault Injection & Failsafe Assurance | Cắt nguồn / ngắt RF / ngắt Broker MQTT lúc bơm đang phun → rơ-le tự ngắt nhờ Local Deadman; không cháy màng bơm |

---

## 6. QUẢN TRỊ RỦI RO KỸ THUẬT

| Rủi ro | Biện pháp giảm thiểu |
|---|---|
| **Độ lệch Topology vật lý (`1..4` vs `4..7`)** | Mảng ánh xạ `physical_node_id_map` trong phân vùng NVS của Gateway; cấu hình linh hoạt ID trạm thực tế không cần sửa mã nguồn logic trung tâm |
| **Tràn bộ đếm xung (Pulse Counter Wrap-around)** | Số học modulo 16-bit `Δ_pulse = (curr - last) & 0xFFFF` tại Gateway khi đọc 8 byte RAM từ opcode `0x0E` |
| **Rollback khi refactor fail** | Đóng gói firmware hiện tại thành `v0.9-legacy-backup.bin`; snapshot DB trước khi nạp mã refactor lên thiết bị lab |

---

*Tài liệu này được khởi tạo bởi Baseline Agent vào ngày 2026-09-24.*
*Mọi thay đổi về kiến trúc phải được phê duyệt và cập nhật tại đây trước khi thực thi.*
