# PROGRESS — Refactor Phase Tracking

> Tài liệu theo dõi tiến độ thực thi (Progress Register) cho kế hoạch Refactor được khởi tạo bởi **Gemini**. Mọi Agent thực thi phải cập nhật Status tại đây sau mỗi tác vụ theo đúng quy ước markdown checkbox (Pending / In Progress / QA Review / Done). Không được sửa cột Note trừ khi có thay đổi hướng dẫn kỹ thuật được phê duyệt ở `.ai/planning/refactor-phase/README.md`.

---

## 1. Started

| Trường | Giá trị |
|---|---|
| **Thời điểm khởi tạo** | `2026-09-25T15:25:45Z` (UTC) |
| **Execution Agent** | **Gemini** |
| **Vai trò** | Kỹ sư thực thi (Execution Agent) theo kế hoạch Sprint |
| **Baseline Agent** | Đã khởi tạo master plan `README.md` ngày `2026-09-24` (không thay đổi trong phạm vi refactor) |

---

## 2. Reference Plan

| Trường | Giá trị |
|---|---|
| **Thư mục kế hoạch** | `.ai/planning/refactor-phase/` |
| **Master Planning Context** | `.ai/planning/refactor-phase/README.md` (Single Source of Truth — bắt buộc đọc trước khi bắt đầu bất kỳ Sprint nào) |
| **Sprint hiện tại (đang tham chiếu)** | `.ai/planning/refactor-phase/sprint_4.md — **Sprint 4: Dashboard State Synchronization & E2E Validation (Next.js & Nginx)**` |
| **Thứ tự Sprint roadmap** | `Sprint 1 (Codec ESP32)` → `Sprint 2 (Virtual FSM & Safety Timers)` → `Sprint 3 (Backend NestJS & TimescaleDB)` → `Sprint 4 (Dashboard Next.js & Nginx)` |
| **Golden Baseline tham chiếu Sprint 4** | `docs/interface-wire-contract.md` §8–§9, `docs/e2e-critical-sequence-flows.md` |
| **Phụ thuộc Sprint 3** | Sprint 3 PASS (Backend Ingestion & Admission Pipeline) |
| **Output bàn giao Sprint 4** | Dashboard không còn Optimistic UI, nút điều khiển hiển thị `RUNNING` chỉ sau `FLOW_CONFIRMED` từ WebSocket, Nginx Reverse Proxy port 6003 hoạt động, E2E test toàn bộ chuỗi control loop |

---

## 3. Addition Plan (Yêu cầu phát sinh)

**Chưa có yêu cầu phát sinh nào được bổ sung.** Mọi yêu cầu mới vượt phạm vi Golden Baseline hoặc Sprint hiện tại phải được ghi vào bảng dưới đây trước khi triển khai, và phải được phê duyệt tại `.ai/planning/refactor-phase/README.md`.

| ID | Yêu cầu phát sinh | Trạng thái | Ghi chú |
|---|---|---|---|
| (trống) | Chưa có | — | — |

---

## 4. Track Status — Sprint 4

> Quy ước Status (bắt buộc, không thay đổi ký hiệu):
> - `[ ] Pending` — Task chưa chạm vào.
> - `[ ] In Progress` — Execution Agent đang viết code.
> - `[ ] QA Review` — Code đã viết xong, đang chờ rà soát chất lượng.
> - `[x] Done` — Đã qua vòng review nghiêm ngặt và được duyệt.

> **Chỉ thị kỹ thuật bắt buộc (đóng vai Note):** Mỗi Task Note phải tuân theo các quy tắc trong `.ai/planning/refactor-phase/README.md` (Clean Architecture, Dependency Inversion, Strangler Fig, Fail-safe, Zero-hardcode credential, Non-retained transactional topics, `synchronize: false`, v.v.) và các rule chuẩn sprint tương ứng (S4-NOOPT-01, S4-WS-02, S4-WS-03, S4-NGINX-04, S4-STALE-05, S4-E2E-06, v.v.).

### 4.1 TRACK N — WebSocket Client Reconstruction (Khử Stub)

| Task ID | Mô tả Task | Status | Note (chỉ thị kỹ thuật bắt buộc) |
|---|---|---|---|
| N1 | `aeroponics-ui/src/hooks/useWebSocket.ts` — Hiện thực hóa native WebSocket với exponential backoff | [ ] QA Review | **Pattern:** Event-Driven Architecture + Exponential Backoff. **Bắt buộc:** (1) `connect()` tạo real `new WebSocket(resolveWsUrl())`, KHÔNG stub. (2) `ws.onmessage` dispatch `wsMessageHandler(data)` tới store. (3) `ws.onclose` gọi `scheduleReconnect()`, KHÔNG gọi `connect()` trực tiếp. (4) `calculateBackoffDelay`: base=1s, factor=1.5, max=30s. (5) Max 1 pending retry timer tại mọi thời điểm. (6) ResolveWsUrl: `NEXT_PUBLIC_WS_URL` env hoặc fallback `ws(s)://${hostname}:${port}/ws`. **Rule S4-WS-03:** backoff increases monotonically. **Rule S4-NOOPT-01:** dispatch to store only on real server events. |
| N2 | `aeroponics-ui/src/hooks/useWebSocket.ts` — Message dispatcher (WS → Store) | [ ] QA Review | **Pattern:** Dispatcher / Observer. **Bắt buộc:** `wsMessageHandler(msg)` switch-case dispatch: (1) `node_telemetry` → `useNodeStore.getState().updateNode()`. (2) `node_flow` → `useNodeStore.getState().applyFlowConfirmed()`. (3) `pump_command_update` → `useNodeStore.getState().updateOutcome()`. (4) `staleness_alert` → `useNodeStore.getState().updateNode({isStale})`. (5) `device_status` → `useDeviceStore.getState().updateDevice()`. **KHÔNG optimistic writes:** chỉ server-authoritative state được apply. **Rule S4-WS-02:** `flowConfirmed` chỉ set true qua `applyFlowConfirmed()`. |

### 4.2 TRACK O — Store & State Management (Flow Confirmed Authority)

| Task ID | Mô tả Task | Status | Note (chỉ thị kỹ thuật bắt buộc) |
|---|---|---|---|
| O1 | `aeroponics-ui/src/store/useNodeStore.ts` — Thêm `applyFlowConfirmed` và `updateOutcome` action | [ ] Pending | **Pattern:** Single Source of Truth (Zustand). **Bắt buộc:** (1) `applyFlowConfirmed(id, flowConfirmed, flowRateLpm, confirmedAt)`: chỉ action set `flowConfirmed=true`, validate node ∈ AGU_NODE_IDS whitelist. (2) `updateOutcome(id, outcome)`: update outcome-only, KHÔNG infer RUNNING. (3) `createDefaultNode` outcome phải là `null` hoặc `''` (PENDING default — xem Task V2). (4) Immutable state update pattern (spread operator, không mutate). **Rule S4-WS-02:** `flowConfirmed` chỉ true từ WS event qua `applyFlowConfirmed()`. **Rule S4-WS-04:** Initial badge neutral "Chờ lệnh". |
| O2 | `aeroponics-ui/src/lib/types.ts` — Thêm `FlowConfirmedWsEvent`, `PumpCommandUpdateWsEvent`, `isNodeRunning` | [ ] Pending | **Pattern:** Typed Event Payload + Derived State. **Bắt buộc:** (1) `FlowConfirmedWsEvent` interface: nodeId, flowConfirmed, flowRateLpm, commandId?, confirmedAt?, timestamp. (2) `PumpCommandUpdateWsEvent` interface: nodeId, commandId, outcome union type. (3) `isNodeRunning(node) = node.flowConfirmed && node.outcome === 'FLOW_CONFIRMED'`. **Rule S4-STALE-05:** Thêm `&& !node.isStale` vào `isNodeRunning` (xem Task V5). **Rule S4-NOOPT-01:** Không dùng `scheduleState` hay `outcome` đơn lẻ để suy RUNNING. |

### 4.3 TRACK P — Component Refactor (Khử Optimistic UI)

| Task ID | Mô tả Task | Status | Note (chỉ thị kỹ thuật bắt buộc) |
|---|---|---|---|
| P1 | `aeroponics-ui/src/components/dashboard/NodeCard.tsx` — Bỏ optimistic glow, dựa hoàn toàn vào `isRunning` | [ ] Pending | **Pattern:** Declarative Rendering (server-authoritative). **Bắt buộc:** (1) Bỏ `node.scheduleState === 'SPRAYING'` khỏi glow condition — schedule state KHÔNG phải flow evidence. (2) Bỏ `node.outcome === 'FLOW_CONFIRMED'` nếu không đi kèm `node.flowConfirmed` (chống out-of-sync). (3) Glow condition = `isNodeRunning(node)` duy nhất. (4) Không có `useState` nào control RUNNING display. **Rule S4-NOOPT-01:** NodeCard glow chỉ bật khi `node.flowConfirmed === true` từ WS. |
| P2 | `aeroponics-ui/src/components/common/OutcomeBadge.tsx` — Chỉ render RUNNING khi `isRunning` | [ ] Pending | **Pattern:** Presentational Component. **Bắt buộc:** (1) `showRunning = outcome === 'FLOW_CONFIRMED' && nodeFlowConfirmed === true`. (2) Vietnamese labels: `FLOW_CONFIRMED` → "Xác nhận dòng chảy" (primary + glow), `RF_ACKED` → "Đã nhận lệnh (RF)" (indigo, NO glow), `PENDING` → "Đang gửi lệnh" (subtle, NO glow), `REJECTED` → "Đã từ chối", `null/empty` → "Chờ lệnh" (neutral). (3) Config `OUTCOME_CONFIG` map đầy đủ outcome → {label, style}. (4) `data-testid=outcome-badge` cho E2E test selector. **Rule S4-NOOPT-01:** Không render RUNNING glow khi chỉ có outcome RF_ACKED/PENDING. |
| P3 | `aeroponics-ui/src/components/dashboard/PumpControl.tsx` — Tạo mới nút điều khiển pump | [ ] Pending | **Pattern:** Command Pattern + Optimistic Feedback (PENDING only). **Bắt buộc:** (1) `handleOnClick`: POST `/api/node/${nodeId}/override` với DTO `{action: 'ON', node_id: nodeId, run_lease_ms: 60000}` — endpoint theo Sprint 3 Task T-1. (2) Sau ACCEPTED: chỉ set `outcome: 'PENDING'` (KHÔNG set RUNNING). (3) RUNNING chỉ hiện sau WS `FLOW_CONFIRMED`. (4) `handleOffClick`: POST với `action: 'OFF'`, outcome → PENDING. (5) Disabled state: `sending || !node.calibrationStatus`. (6) Error handling: try/catch + toast notification. **Rule S4-NOOPT-01:** Không set RUNNING trực tiếp từ button click. **Rule S4-WS-04:** PENDING badge = "Đang gửi lệnh". |
| P4 | `aeroponics-ui/src/components/dashboard/NodeDetailModal.tsx` — Evidence pipeline display | [ ] Pending | **Pattern:** Evidence Pipeline Visualizer. **Bắt buộc:** (1) Hiển thị 4 evidence stages từ server (không suy diễn): {label: 'Lệnh đã gửi', done: outcome !== 'PENDING'}, {label: 'RF đã nhận (ACK)', done: ['RF_ACKED','FLOW_CONFIRMED'].includes(outcome)}, {label: 'Cảm biến dòng chảy', done: node.flowConfirmed}, {label: 'Xác nhận dòng chảy', done: node.flowConfirmed && outcome === 'FLOW_CONFIRMED'}. (2) Mỗi stage render dot green/active chỉ khi server báo tương ứng. (3) KHÔNG hiển thị RUNNING khi evidence chưa đủ. |

### 4.4 TRACK Q — Nginx Reverse Proxy (Port 6003)

| Task ID | Mô tả Task | Status | Note (chỉ thị kỹ thuật bắt buộc) |
|---|---|---|---|
| Q1 | `nginx/aeroponics.conf` — Port 6003 + WebSocket upgrade configuration | [ ] Pending | **Pattern:** Reverse Proxy Routing. **Bắt buộc:** (1) `listen 6003`, KHÔNG listen 80. (2) `location /ws`: proxy_pass http://aero_backend, `proxy_http_version 1.1`, `Upgrade $http_upgrade`, `Connection "upgrade"`, `proxy_read_timeout 86400s`. (3) `location /socket.io/`: proxy_pass http://aero_backend, `proxy_buffering off` (CRITICAL chống long-poll delay),同样 `Connection "upgrade"` headers. (4) `location /api/`: proxy_pass http://aero_backend. (5) `location /`: proxy_pass http://aero_ui (Next.js catch-all). (6) upstream keepalive 32. (7) Headers: Host, X-Real-IP, X-Forwarded-For, X-Forwarded-Proto. **Rule S4-NGINX-04:** WebSocket upgrade correctness. **Firewall:** `ufw allow 6003/tcp`. |
| Q2 | `nginx/aeroponics.conf.example` — Template sync port 6003 | [ ] Pending | **Bắt buộc:** Copy nội dung chuẩn hóa từ Task Q-1, thêm comment hướng dẫn domain placeholder (`server_name YOUR_DOMAIN`). Đồng bộ port 6003 giữa config chính và example template. |
| Q3 | `docker-compose.yml` — Expose port 6003 | [ ] Pending | **Bắt buộc:** (1) `nginx` service ports: `"6003:6003"`. (2) `aero-backend`: `expose: ["3001"]` (internal only, KHÔNG expose ra host). (3) `aero-ui`: nội bộ port 3000, Nginx proxy forward. (4) Đảmभाव tất cả services nằm trong Docker network chung. **Security:** TimescaleDB chỉ expose internal; không expose DB port ra host. |

### 4.5 TRACK R — E2E Validation Test Suite

| Task ID | Mô tả Task | Status | Note (chỉ thị kỹ thuật bắt buộc) |
|---|---|---|---|
| R1 | `test/e2e/pump-control.spec.ts` — Full control loop test (UI → Backend → MQTT → Gateway → Node → WS → UI) | [ ] Pending | **Pattern:** E2E Test (Playwright). **Bắt buộc:** (1) Test flow: login → dashboard → wait WS connected → click BẬT BƠM → Expect badge "Đang gửi lệnh" (PENDING) → publish FLOW_CONFIRMED via test MQTT broker → Expect badge "Xác nhận dòng chảy" (FLOW_CONFIRMED) → click TẮT BƠM → Expect badge "Chờ lệnh". (2) **Assertion critical:** `NO intermediate RUNNING state` giữa step click ON và step WS FLOW_CONFIRMED. (3) `data-testid=outcome-badge` selector. (4) `data-testid=pump-glow` class assertion. (5) Mock gateway MQTT telemetry publish. **Rule S4-NOOPT-01:** Validate zero optimistic UI. **Rule S4-E2E-06:** Full loop test coverage. |
| R2 | `test/e2e/ws-reconnect.spec.ts` — WS connection resilience test | [ ] Pending | **Pattern:** Resilience / Chaos Test. **Bắt buộc:** (1) Test: page.goto dashboard → Expect WS connected banner → restart backend fixture → Expect "Reconnecting" → Expect "Connected" (timeout 40s). (2) Validate backoff: không reconnect mỗi 1s (connection storm). (3) UI không crash khi WS disconnect. **Rule S4-WS-03:** WS bounded reconnect — no connection storm. |

### 4.6 TRACK V — Critical Frontend Sync Fixes

| Task ID | Mô tả Task | Status | Note (chỉ thị kỹ thuật bắt buộc) |
|---|---|---|---|
| V1 | `aeroponics-ui/src/hooks/useWebSocket.ts` — Exponential backoff genuine implementation | [ ] Pending | **Finding #1:** Stub returns `isConnected: true`, `calculateBackoffDelay: 0`. **Fix:** (1) `connect()` tạo real `new WebSocket(resolveWsUrl())`. (2) `ws.onmessage` dispatch `wsMessageHandler(data)`. (3) `ws.onclose` → `scheduleReconnect()`. (4) Backoff: `base(1s) × factor(1.5)^retryCount, capped at 30s`. (5) Max 1 pending retry timer. **Rule S4-WS-03:** PASS; reconnect time ≤ 40s total. **Rule S4-WS-06:** Backoff monotonically increases. |
| V2 | `aeroponics-ui/src/store/useNodeStore.ts` — Outcome default to neutral (not PENDING) | [ ] Pending | **Finding #2:** `createDefaultNode` sets `outcome: 'PENDING'` → fresh dashboard hiển thị "Đang gửi lệnh" cho tất cả 4 nodes. **Fix:** (1) `createDefaultNode` outcome = `null` hoặc `''`. (2) `OUTCOME_CONFIG` thêm entry cho `null`/empty → badge renders "Chờ lệnh" (Vietnamese neutral). (3) Đảm bảo initial load = neutral trước WS event. **Rule S4-WS-04:** PASS; fresh dashboard badge = "Chờ lệnh". |
| V3 | `aeroponics-backend/src/mqtt/mqtt-router.service.ts` — Separate Admission ACK from RF_ACKED | [ ] Pending | **Finding #7:** `handleCommandAckEvent` includes `ACCEPTED` trong `acked` check → UI show "Đã nhận lệnh (RF)" TRƯỚC KHI RF transmission. **Fix:** (1) `acked = true` chỉ khi `payload.acked === true` hoặc `status === 'RF_ACKED'`. (2) `ACCEPTED` → emit `MQTT_EVENTS.COMMAND_ACCEPTED` (lifecycle admission, KHÔNG RF acknowledgment). (3) Thêm type `CommandAcceptedEvent` riêng. (4) `handleNodeAck`: đọc `payload.acked` chính; fall back `payload.status` cho backward-compat. **Rule S4-WS-05:** ACK/RF separation integrity. |
| V4 | `aeroponics-ui/src/hooks/useWebSocket.ts` — Fix retry backoff closure capture | [ ] Pending | **Finding #8:** `connect` closure captures first-render `scheduleReconnect` với `retryCount=0` mãi mãi → delay luôn 1s → connection storm. **Fix:** (1) Pass `retryCount` làm dependency duy nhất cho `scheduleReconnect`. (2) KHÔNG capture `scheduleReconnect` trong `connect` closure. (3) Dùng `useRef` cho `retryCount` hoặc truyền callback. (4) Đảm bảo `scheduleReconnect` đọc retryCount từ current context. **Rule S4-WS-03:** PASS; backoff: 1s → 1.5s → 2.25s → ... → 30s. **Rule S4-WS-06:** delay(n) ≥ delay(n-1) ∀ n ≥ 0. |
| V5 | `aeroponics-ui/src/lib/types.ts` + `aeroponics-ui/src/components/dashboard/NodeCard.tsx` — Add `isStale` check to `isNodeRunning` | [ ] Pending | **Finding #9:** `isNodeRunning` không check `isStale` → stale node vẫn glow RUNNING. **Fix:** (1) `isNodeRunning = node.flowConfirmed && node.outcome === 'FLOW_CONFIRMED' && !node.isStale`. (2) NodeCard glow condition = `isRunning` (đã include stale check). (3) `isStale` set đúng từ `STALENESS_ALERT` WS event. **Rule S4-STALE-05:** PASS; stale node KHÔNG glow RUNNING. **Override:** Stale node (≥ STALE_THRESHOLD_MS) → danger display. |
| V6 | `aeroponics-ui/src/store/useNodeStore.ts` — Clear outcome after pump OFF | [ ] Pending | **Finding #10:** `applyFlowConfirmed(false)` giữ outcome cũ = 'FLOW_CONFIRMED' → badge vẫn "Xác nhận dòng chảy" sau pump OFF. **Fix:** (1) `applyFlowConfirmed`: khi `flowConfirmed = false` → set `outcome: 'PENDING'` (clear previous RUNNING). (2) Backend broadcast `pump.command_update` outcome `PENDING` trên pump OFF; UI listen và set outcome `'PENDING'`. (3) E2E step 9: sau TẮT BƠM → badge = "Chờ lệnh". **Rule S4-E2E-07:** PASS; outcome clearing after pump OFF. |

### 4.7 TRACK W — Moderate Frontend Sync Fixes

| Task ID | Mô tả Task | Status | Note (chỉ thị kỹ thuật bắt buộc) |
|---|---|---|---|
| W1 | `aeroponics-ui/src/components/dashboard/NodeCard.tsx` — Remove `scheduleState === 'SPRAYING'` from glow condition | [ ] Pending | **Finding #12:** Code NodeCard line 29 vẫn include `node.scheduleState === 'SPRAYING'` → glow trigger từ schedule state alone. **Fix:** Đã có trong plan P-1 — implement khi code Sprint 4. Bỏ `scheduleState === 'SPRAYING'` khỏi condition glow. Glow = `isRunning` duy nhất. |
| W2 | `aeroponics-ui/src/components/common/OutcomeBadge.tsx` — OutcomeBadge `REJECTED` label + full Vietnamese config | [ ] Pending | **Finding #15:** `getOutcomeConfig` falls back raw `outcome` string → badge show English "REJECTED". **Fix:** Cấu hình `OUTCOME_CONFIG` map đầy đủ: `'PENDING'` → "Đang gửi lệnh", `'FLOW_CONFIRMED'` → "Xác nhận dòng chảy", `'RF_ACKED'` → "Đã nhận lệnh (RF)", `'REJECTED'` → "Đã từ chối", `null/empty` → "Chờ lệnh". Badge hiển thị tiếng Việt cho mọi outcome value. |
| W3 | `nginx/aeroponics.conf` — Socket.IO config path documented as retro compatibility only | [ ] Pending | **Finding #18:** Plan §2.3 mention `/socket.io/` nhưng backend EventsGateway dùng native WS `/ws` only. **Fix:** Giữ `/socket.io/` path trong Nginx config nhưng comment `# RETRO COMPATIBILITY ONLY — backend EventsGateway uses native WS at /ws`. Hoặc thêm Socket.IO layer nếu cần nhưng KHÔNG bắt buộc cho Sprint 4 WS flow. |

---

## 5. TIÊU CHUẨN RÀ SOÁT CỨNG (Sprint 4) — Quick Reference

| Rule | Phạm vi | PASS criteria |
|---|---|---|
| **S4-NOOPT-01** | Zero Optimistic UI | NodeCard glow chỉ bật khi `node.flowConfirmed === true` (từ WS). Không dùng `scheduleState`/local setState cho RUNNING. Nút bấm → PENDING, KHÔNG trực tiếp RUNNING. |
| **S4-WS-02** | WS Event Single Source of Truth | `flowConfirmed` chỉ set true qua `applyFlowConfirmed()` từ `wsMessageHandler()`. REST response KHÔNG được dùng để hiển thị RUNNING. |
| **S4-WS-03** | WS Bounded Reconnect | Reconnect dùng exponential backoff: base 1s × 1.5^n, max 30s. `onclose`/`onerror` KHÔNG gọi `connect()` trực tiếp. Max 1 pending retry timer. |
| **S4-NGINX-04** | WebSocket Upgrade Correctness | `location /ws` và `/socket.io/` đều có `Upgrade` + `Connection "upgrade"` headers. `proxy_buffering off` trên `/socket.io/`. Listen 6003. |
| **S4-STALE-05** | Staleness Override | Stale node (≥ STALE_THRESHOLD_MS) → override RUNNING display, hiển thị danger. Offline node KHÔNG glow RUNNING. |
| **S4-E2E-06** | Full Loop Test Coverage | ≥ 1 E2E test pump control loop. ≥ 1 E2E test WS reconnect. |
| **S4-WS-04** | Outcome Neutral Initial State | Initial badge = "Chờ lệnh" (neutral), không phải "Đang gửi lệnh". |
| **S4-WS-05** | ACK/RF Separation Integrity | `ACCEPTED` KHÔNG set outcome = `RF_ACKED`. `RF_ACKED` chỉ khi `payload.acked === true` hoặc `status === 'RF_ACKED'`. |
| **S4-WS-06** | Backoff Monotonic Increase | delay(retry=n) ≥ delay(retry=n-1) ∀ n ≥ 0. Max 30s enforced. |
| **S4-E2E-07** | Outcome Clearing After Pump OFF | Sau TẮT BƠM → outcome = PENDING → badge = "Chờ lệnh". Không intermediate RUNNING state. |

---

*File PROGRESS.md đã được khởi tạo tại `.ai/planning/refactor-phase/PROGRESS.md` kèm theo cấu trúc định dạng Markdown chuẩn, Track N–W từ Sprint 4 đã được chuyển hóa thành các bảng 4 cột (Task ID / Mô tả Task / Status / Note chỉ thị kỹ thuật bắt buộc) và tất cả Status khởi tạo là `[ ] Pending`.*
