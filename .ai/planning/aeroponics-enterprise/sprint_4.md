# Sprint 4: Next.js Dashboard UI & E2E Stress Testing (UI & QA)

> **Phụ thuộc:** Sprint 3 hoàn thành — NestJS Backend + WebSocket Gateway đang chạy, TimescaleDB có dữ liệu thực.
> **Output bàn giao:** Next.js 14/15 Dashboard với 4 Relay Cards (đếm ngược realtime), Gauge cảm biến nước Tuya 8-in-1, Form chỉnh Schedule; kết hợp bộ E2E stress tests kiểm tra fail-safe scenarios.

---

## 1. PHẠM VI & MỤC TIÊU

### 1.1 Modules / Components bị tác động trực tiếp

**Next.js App (`aeroponics-ui/`):**

| File / Component | Loại | Mô tả |
|---|---|---|
| `app/layout.tsx` | Layout | Root layout: font, metadata, providers |
| `app/page.tsx` | Page | Dashboard home: render RelayDashboard + SensorPanel |
| `app/providers.tsx` | Provider | SocketProvider, QueryClientProvider (TanStack Query) |
| `components/relay/RelayDashboard.tsx` | Component | Grid 4 relay cards |
| `components/relay/RelayCard.tsx` | Component | 1 relay: state, countdown, mode badge, override button |
| `components/relay/ScheduleForm.tsx` | Component | Form chỉnh spray/cooldown Ngày/Đêm |
| `components/relay/OverrideModal.tsx` | Component | Modal: manual ON/OFF/Flush với duration |
| `components/sensor/SensorPanel.tsx` | Component | Grid sensor gauges |
| `components/sensor/SensorGauge.tsx` | Component | 1 gauge: pH, EC, TDS, Temp, v.v. |
| `components/sensor/SensorHistoryChart.tsx` | Component | Line chart lịch sử 24h |
| `components/common/StatusBadge.tsx` | Component | Badge trạng thái: online/offline/spraying/cooling |
| `components/common/CountdownRing.tsx` | Component | SVG ring đếm ngược thời gian |
| `hooks/useRelaySocket.ts` | Hook | Subscribe Socket.IO relay_update events |
| `hooks/useSensorSocket.ts` | Hook | Subscribe Socket.IO sensor_update events |
| `hooks/useRelayHistory.ts` | Hook | TanStack Query fetch relay history |
| `hooks/useSensorHistory.ts` | Hook | TanStack Query fetch sensor history |
| `lib/api.ts` | Lib | Axios instance với base URL, interceptors |
| `lib/socket.ts` | Lib | Socket.IO client singleton |
| `lib/types.ts` | Lib | TypeScript interfaces: RelayState, SensorReading, RelayProfile |
| `lib/constants.ts` | Lib | SOCKET_EVENTS, RELAY_STATES, API_ENDPOINTS |
| `store/useRelayStore.ts` | Store | Zustand store cho relay states |
| `store/useSensorStore.ts` | Store | Zustand store cho sensor readings |

**E2E Testing (`aeroponics-project/test/e2e/`):**

| File | Mô tả |
|---|---|
| `stress/wifi-disconnect.test.ts` | Giả lập mất Wi-Fi 24h: kiểm tra RTC fallback, relay hoạt động offline |
| `stress/power-cutoff.test.ts` | Giả lập mất nguồn đột ngột: kiểm tra LWT, MQTT offline status |
| `stress/schedule-change.test.ts` | Gửi 100 schedule changes nhanh: kiểm tra NVS không corrupt, relay không stuck |
| `e2e/relay-command.test.ts` | E2E: UI gửi override → MQTT → ESP32 → telemetry về → UI cập nhật |
| `e2e/sensor-realtime.test.ts` | E2E: Tuya bridge publish → NestJS → WebSocket → UI gauge update |
| `helpers/mqtt-test-client.ts` | Helper: MQTT client cho test |
| `helpers/mock-esp32.ts` | Helper: Mock ESP32 publish/subscribe |
| `helpers/mock-tuya.ts` | Helper: Mock Tuya sensor data generator |

### 1.2 Mục tiêu cụ thể của Sprint 4

**UI:**
- [ ] 4 RelayCard hiển thị trạng thái realtime (SPRAYING/COOLING_DOWN) với đếm ngược `phase_remaining_s`.
- [ ] Badge phân biệt Day/Night mode cho từng relay.
- [ ] ScheduleForm submit → POST API → MQTT command → relay cập nhật.
- [ ] OverrideModal với 3 action: Manual ON, Manual OFF, Flush.
- [ ] 6 SensorGauge (pH, EC, TDS, Temp, Salinity, ORP) cập nhật realtime.
- [ ] SensorHistoryChart: Line chart 24h cho pH và Temperature.
- [ ] Device status indicator: online (green pulse) / offline (red, timestamp cuối).

**E2E Tests:**
- [ ] LWT test: Ngắt ESP32 → Verify MQTT `status=offline` trong vòng 30s.
- [ ] RTC fallback test: Disconnect Wi-Fi → Relay vẫn phân biệt Ngày/Đêm đúng sau 1h.
- [ ] Schedule stress test: 100 writes liên tiếp không làm NVS corrupt.

---

## 2. KIẾN TRÚC & LUỒNG DỮ LIỆU

### 2.1 Kiến Trúc UI — Realtime Data Flow

```
[NestJS WebSocket Gateway]
  Socket.IO server
        │
        │ ws://backend:3001
        ▼
[lib/socket.ts — Socket.IO Client Singleton]
  socket.on('relay_update', handler)
  socket.on('sensor_update', handler)
        │
        ├──▶ useRelaySocket() hook
        │         │
        │         ▼
        │    useRelayStore (Zustand)
        │    relayStates[relay_id] = { state, phase_remaining_s, mode }
        │         │
        │         ▼
        │    RelayCard component
        │    (re-render khi state thay đổi)
        │
        └──▶ useSensorSocket() hook
                  │
                  ▼
             useSensorStore (Zustand)
             latestReading = { ph, ec, temp, ... }
                  │
                  ▼
             SensorGauge component
```

### 2.2 Luồng User Action — Gửi Command

```
[User] → Click "Manual ON 120s" trong OverrideModal
        │
        ▼
OverrideModal.handleSubmit()
        │
        ▼
lib/api.ts:
  POST /api/relay/{id}/override
  Body: { action: "on", duration_s: 120, relay_id: 1 }
        │
        ▼
NestJS relay.controller.override()
        │
        ▼
relay.service.sendRelayCommand()
  → mqtt.publish("aeroponics/device/{id}/command/relay/1/override", payload)
        │
        ▼
[ESP32 nhận MQTT, xử lý override, bật relay]
        │
        ▼
[ESP32 publish telemetry mới]
        │
        ▼
[NestJS MQTT service nhận telemetry]
        │
        ▼
events.gateway.emitRelayUpdate(data)
        │
        ▼
[Socket.IO push tới tất cả connected UI clients]
        │
        ▼
useRelaySocket hook cập nhật Zustand store
        │
        ▼
RelayCard re-render với state mới (MANUAL_ON, countdown 120s)
```

### 2.3 Luồng CountdownRing Component

```
[RelayCard receives state: { phase_remaining_s: 25, state: "SPRAYING", total_duration: 30 }]
        │
        ▼
CountdownRing:
  - progress = phase_remaining_s / total_duration  (0.0 → 1.0)
  - SVG circle: stroke-dashoffset = circumference * (1 - progress)
  - CSS transition: stroke-dashoffset 1s linear (smooth animation)
  - Color: SPRAYING = #22c55e (green), COOLING_DOWN = #3b82f6 (blue)
  - Center text: "${phase_remaining_s}s"

Update trigger:
  - Socket.IO relay_update event → Zustand store update → React re-render
  - Fallback local countdown: setInterval(1s) giảm UI-side khi không có socket event
    (reset khi nhận event mới)
```

### 2.4 Luồng E2E Test — LWT Verification

```
[Test: power-cutoff.test.ts]
        │
        ▼
1. Kết nối MQTT test client, subscribe "aeroponics/device/+/status"
2. Lấy retained message hiện tại (status = online)
3. Giả lập ngắt nguồn: gửi MQTT DISCONNECT packet đột ngột
   (hoặc kill ESP32 mock process)
4. Chờ tối đa 30s (keep-alive * 1.5)
5. Assert: nhận message { "status": "offline" } trên /status topic
6. Assert: message có Retain = true
7. Restore: ESP32 reconnect → nhận { "status": "online" }
```

### 2.5 Luồng E2E Test — Schedule Stress Test

```
[Test: schedule-change.test.ts]
        │
        ▼
1. Ghi nhớ trạng thái NVS ban đầu qua GET /api/relay/1/state
2. FOR i = 1 to 100:
     Publish random valid schedule command qua MQTT
     Wait 100ms
3. Chờ 5s cho NVS settle
4. Assert: GET /api/relay/1/state trả về đúng schedule từ command cuối cùng
5. Assert: ESP32 vẫn đang SPRAYING hoặc COOLING_DOWN (không bị stuck/crash)
6. Assert: Không có "NVS write error" trong Serial log
```

---

## 3. PHÂN RÃ CHI TIẾT TÁC VỤ

### TRACK A — Foundation & Shared Infrastructure

---

#### Task A-1: Khởi tạo Next.js Project
**File tạo mới:** `aeroponics-ui/package.json`

**Dependencies cần cài:**
```json
{
  "dependencies": {
    "next": "^15.x",
    "react": "^19.x",
    "socket.io-client": "^4.x",
    "zustand": "^4.x",
    "axios": "^1.x",
    "@tanstack/react-query": "^5.x",
    "recharts": "^2.x",
    "clsx": "^2.x",
    "tailwind-merge": "^2.x"
  }
}
```

**`next.config.ts` — cần cấu hình:**
- `output: 'standalone'` cho Docker.
- `env.NEXT_PUBLIC_API_URL` và `NEXT_PUBLIC_WS_URL` từ process.env.

---

#### Task A-2: TypeScript Types & Constants
**File tạo mới:** `aeroponics-ui/src/lib/types.ts`

**Interfaces cần định nghĩa:**
```typescript
interface RelayState {
  relay_id: number;           // 1-4
  state: 'SPRAYING' | 'COOLING_DOWN' | 'MANUAL_ON' | 'MANUAL_OFF' | 'FLUSH' | 'STOPPED';
  phase_remaining_s: number;
  mode: 'day' | 'night';
  override_active: boolean;
  timestamp: string;
}

interface RelayProfile {
  relay_id: number;
  spray_day_s: number;
  cooldown_day_s: number;
  spray_night_s: number;
  cooldown_night_s: number;
}

interface SensorReading {
  sensor_id: string;
  ph_value: number | null;
  ec_value: number | null;
  tds_value: number | null;
  temperature: number | null;
  salinity: number | null;
  orp_value: number | null;
  recorded_at: string;
}

interface DeviceStatus {
  device_id: string;
  status: 'online' | 'offline';
  last_seen_at: string;
  rssi_dbm: number;
}
```

---

#### Task A-3: Axios API Client
**File tạo mới:** `aeroponics-ui/src/lib/api.ts`

**Hàm cần triển khai:**
- `createApiClient(): AxiosInstance` — instance với `baseURL = NEXT_PUBLIC_API_URL`.
- Interceptor: Thêm `Authorization: Bearer {token}` header (nếu có auth).
- Interceptor response: Catch 4xx/5xx, format lỗi thành `ApiError { message, statusCode }`.
- Export các typed functions:
  - `getRelayState(relayId: number): Promise<RelayState>`
  - `getRelayHistory(relayId: number, from: string, to: string): Promise<RelayState[]>`
  - `updateRelaySchedule(relayId: number, profile: RelayProfile): Promise<void>`
  - `sendRelayOverride(relayId: number, action: string, durationS: number): Promise<void>`
  - `getLatestSensorReading(sensorId: string): Promise<SensorReading>`
  - `getSensorHistory(sensorId: string, from: string, to: string): Promise<SensorReading[]>`

---

#### Task A-4: Socket.IO Client Singleton
**File tạo mới:** `aeroponics-ui/src/lib/socket.ts`

**Hàm cần triển khai:**
- `getSocket(): Socket` — Singleton pattern, chỉ tạo 1 lần.
- `connectSocket(): void` — Connect với `NEXT_PUBLIC_WS_URL`, auto reconnect = true.
- `disconnectSocket(): void`

**Constants cần định nghĩa:**
**File tạo mới:** `aeroponics-ui/src/lib/constants.ts`
```typescript
export const SOCKET_EVENTS = {
  RELAY_UPDATE: 'relay_update',
  SENSOR_UPDATE: 'sensor_update',
  DEVICE_STATUS: 'device_status',
} as const;
```

---

### TRACK B — State Management (Zustand)

---

#### Task B-1: Relay Store
**File tạo mới:** `aeroponics-ui/src/store/useRelayStore.ts`

**Store interface:**
```typescript
interface RelayStore {
  relayStates: Record<number, RelayState>;    // key = relay_id
  relayProfiles: Record<number, RelayProfile>;
  setRelayState: (state: RelayState) => void;
  setRelayProfile: (profile: RelayProfile) => void;
  getRelayState: (relayId: number) => RelayState | undefined;
}
```

**`setRelayState` logic:** Replace state cho `relay_id` tương ứng. Không mutate array.

---

#### Task B-2: Sensor Store
**File tạo mới:** `aeroponics-ui/src/store/useSensorStore.ts`

**Store interface:**
```typescript
interface SensorStore {
  latestReading: SensorReading | null;
  readingHistory: SensorReading[];   // Max 288 readings (24h × 12/h)
  deviceStatus: DeviceStatus | null;
  setLatestReading: (reading: SensorReading) => void;
  appendHistory: (reading: SensorReading) => void;   // Max 288, FIFO drop oldest
  setDeviceStatus: (status: DeviceStatus) => void;
}
```

---

### TRACK C — Realtime Hooks

---

#### Task C-1: useRelaySocket Hook
**File tạo mới:** `aeroponics-ui/src/hooks/useRelaySocket.ts`

**Logic:**
```typescript
export function useRelaySocket() {
  const setRelayState = useRelayStore(s => s.setRelayState);

  useEffect(() => {
    const socket = getSocket();
    socket.on(SOCKET_EVENTS.RELAY_UPDATE, (data: RelayState) => {
      setRelayState(data);
    });
    return () => {
      socket.off(SOCKET_EVENTS.RELAY_UPDATE);
    };
  }, [setRelayState]);
}
```

---

#### Task C-2: useSensorSocket Hook
**File tạo mới:** `aeroponics-ui/src/hooks/useSensorSocket.ts`

**Logic:** Tương tự `useRelaySocket` nhưng subscribe `sensor_update`, gọi `setLatestReading()` và `appendHistory()`.

---

#### Task C-3: useRelayHistory Hook (TanStack Query)
**File tạo mới:** `aeroponics-ui/src/hooks/useRelayHistory.ts`

**Hàm cần triển khai:**
- `useRelayHistory(relayId: number, from: string, to: string)` → `useQuery`
  - `queryKey: ['relay_history', relayId, from, to]`
  - `queryFn: () => getRelayHistory(relayId, from, to)`
  - `staleTime: 60000` (1 phút)

---

### TRACK D — UI Components

---

#### Task D-1: CountdownRing Component
**File tạo mới:** `aeroponics-ui/src/components/common/CountdownRing.tsx`

**Props interface:**
```typescript
interface CountdownRingProps {
  remainingSeconds: number;
  totalSeconds: number;
  state: RelayState['state'];
  size?: number;           // Default: 120px
}
```

**SVG Ring logic:**
- `radius = (size / 2) - 10`
- `circumference = 2 * Math.PI * radius`
- `progress = remainingSeconds / totalSeconds` (0.0 → 1.0)
- `strokeDashoffset = circumference * (1 - progress)`
- Transition: `stroke-dashoffset 1s linear`
- Color map: `SPRAYING → #22c55e`, `COOLING_DOWN → #3b82f6`, `MANUAL_ON → #f59e0b`, `FLUSH → #8b5cf6`

---

#### Task D-2: RelayCard Component
**File tạo mới:** `aeroponics-ui/src/components/relay/RelayCard.tsx`

**Props interface:**
```typescript
interface RelayCardProps {
  relayId: number;
  onOverrideClick: (relayId: number) => void;
  onScheduleClick: (relayId: number) => void;
}
```

**Hiển thị cần có:**
- Tiêu đề: "Relay 1 — Vòi Phun Khu A"
- `CountdownRing` với `remainingSeconds` và `totalSeconds` (tính từ profile).
- `StatusBadge` hiện state (SPRAYING / COOLING_DOWN / MANUAL_ON / ...).
- Badge "🌙 Night Mode" hoặc "☀️ Day Mode".
- Nút "Manual Control" → mở OverrideModal.
- Nút "Schedule" → mở ScheduleForm.
- Hiển thị thông tin profile hiện tại: "Spray: 30s | Cooldown: 5m" (Day) / "Spray: 30s | Cooldown: 10m" (Night).

---

#### Task D-3: RelayDashboard Component
**File tạo mới:** `aeroponics-ui/src/components/relay/RelayDashboard.tsx`

**Logic:**
- Render grid 2×2 gồm 4 `RelayCard` (relayId = 1, 2, 3, 4).
- Quản lý state local: `selectedRelay` cho modal.
- Render `OverrideModal` và `ScheduleForm` dưới dạng sheet/dialog.
- Gọi `useRelaySocket()` hook ở đây (một lần duy nhất).

---

#### Task D-4: ScheduleForm Component
**File tạo mới:** `aeroponics-ui/src/components/relay/ScheduleForm.tsx`

**Props:**
```typescript
interface ScheduleFormProps {
  relayId: number;
  currentProfile: RelayProfile;
  onSubmit: (profile: RelayProfile) => Promise<void>;
  onClose: () => void;
}
```

**Fields cần có:**
- Day Mode: `spray_day_s` (input number, min=5, max=300), `cooldown_day_s` (min=30, max=7200).
- Night Mode: `spray_night_s`, `cooldown_night_s` (cùng range).
- Validation client-side trước khi submit.
- Loading state khi đang submit.
- Toast success/error sau submit.

---

#### Task D-5: SensorGauge Component
**File tạo mới:** `aeroponics-ui/src/components/sensor/SensorGauge.tsx`

**Props:**
```typescript
interface SensorGaugeProps {
  label: string;               // "pH", "EC", "Temperature", v.v.
  value: number | null;
  unit: string;               // "pH", "µS/cm", "°C", "ppm", "mV"
  min: number;
  max: number;
  warningMin?: number;        // Hiện màu vàng nếu dưới ngưỡng này
  warningMax?: number;        // Hiện màu vàng nếu trên ngưỡng này
  criticalMin?: number;       // Hiện màu đỏ nếu dưới ngưỡng này
  criticalMax?: number;
}
```

**Thresholds mặc định:**
- pH: warning [5.5, 7.5], critical [4.5, 8.5]
- Temp: warning [18°C, 28°C], critical [15°C, 32°C]
- EC: warning [500, 2500], critical [200, 3500]

---

#### Task D-6: SensorHistoryChart Component
**File tạo mới:** `aeroponics-ui/src/components/sensor/SensorHistoryChart.tsx`

**Props:**
```typescript
interface SensorHistoryChartProps {
  sensorId: string;
  metric: 'ph_value' | 'temperature' | 'ec_value';
  timeRangeHours: number;    // Default: 24
}
```

**Dùng Recharts `LineChart`:**
- X-axis: timestamp (format: "HH:mm").
- Y-axis: giá trị metric.
- Tooltip: hiện đầy đủ timestamp + value + unit.
- Reference lines: warningMin, warningMax (dashed yellow).
- Responsive container: chiều rộng 100%.

---

### TRACK E — E2E & Stress Testing

---

#### Task E-1: MQTT Test Client Helper
**File tạo mới:** `aeroponics-project/test/helpers/mqtt-test-client.ts`

**Class `MqttTestClient` — Hàm cần triển khai:**
- `connect(brokerUrl: string, options: MqttConnectOptions): Promise<void>`
- `subscribe(topic: string): Promise<void>`
- `publish(topic: string, payload: string, options?: PublishOptions): Promise<void>`
- `waitForMessage(topic: string, timeoutMs: number): Promise<string>` — Resolve với payload, reject nếu timeout.
- `getRetainedMessage(topic: string): Promise<string | null>` — Subscribe với `clean: true`, lấy retained.
- `disconnect(): Promise<void>`

---

#### Task E-2: Mock ESP32 Helper
**File tạo mới:** `aeroponics-project/test/helpers/mock-esp32.ts`

**Class `MockEsp32` — Hàm cần triển khai:**
- `start(deviceId: string): Promise<void>` — Connect MQTT, register LWT, start heartbeat 10s.
- `publishHeartbeat(): Promise<void>`
- `publishRelayTelemetry(relayId: number, state: string, remainingS: number): Promise<void>`
- `simulatePowerCutoff(): void` — Gọi `socket.destroy()` (không graceful disconnect) để trigger LWT.
- `simulateGracefulDisconnect(): Promise<void>` — Normal MQTT disconnect.

---

#### Task E-3: LWT / Power Cutoff Test
**File tạo mới:** `aeroponics-project/test/stress/power-cutoff.test.ts`

**Test cases:**
```
describe('LWT Power Cutoff Test'):

  test('LWT message should appear within 30s after power cutoff'):
    1. mockEsp32.start(device_id)
    2. Verify testClient.getRetainedMessage(status_topic) = { status: 'online' }
    3. mockEsp32.simulatePowerCutoff()
    4. lwtMessage = testClient.waitForMessage(status_topic, 30000)
    5. expect(JSON.parse(lwtMessage).status).toBe('offline')

  test('Device should recover and publish online after reconnect'):
    1. (After previous test) mockEsp32.start(device_id)
    2. onlineMessage = testClient.waitForMessage(status_topic, 15000)
    3. expect(JSON.parse(onlineMessage).status).toBe('online')
```

---

#### Task E-4: Schedule Stress Test
**File tạo mới:** `aeroponics-project/test/stress/schedule-change.test.ts`

**Test cases:**
```
describe('NVS Schedule Stress Test'):

  test('100 rapid schedule changes should not corrupt NVS'):
    1. FOR i = 1 to 100:
         payload = random valid schedule (spray: 5-60s, cooldown: 30-300s)
         testClient.publish("aeroponics/device/{id}/command/relay/1/schedule", payload)
         await sleep(50ms)  // không quá nhanh để không overwhelm
    2. await sleep(5000ms)  // chờ NVS settle
    3. finalSchedule = GET /api/relay/1/state
    4. lastPayload = schedules[99]
    5. expect(finalSchedule.spray_day_s).toBe(lastPayload.spray_day_s)
    6. Kiểm tra ESP32 serial log: không có "NVS error" substring

  test('Relay should remain operational during schedule stress'):
    1. Publish 50 commands như trên
    2. Lấy relay telemetry: expect state in ['SPRAYING', 'COOLING_DOWN']
    3. expect(relay_state.phase_remaining_s).toBeGreaterThan(0)
```

---

## 4. TIÊU CHUẨN RÀ SOÁT CỨNG (Sprint 4 Hardened Rules)

### Rule S4-UI-01: Không Hard-code Giá Trị Env trong Source
```
PASS: API URL, WebSocket URL chỉ đọc từ process.env.NEXT_PUBLIC_*
PASS: Tất cả env vars được document trong .env.example
FAIL: URL hard-code như "http://localhost:3001" trong component hoặc hook
```

### Rule S4-UI-02: Socket.IO Singleton Pattern Bắt Buộc
```
PASS: getSocket() trả về cùng một instance qua mọi lần gọi
PASS: Socket chỉ được connect 1 lần, không reconnect spam khi component re-render
FAIL: new io() được gọi bên trong component body hoặc useEffect mà không check singleton
```

### Rule S4-UI-03: Zustand State Update Idempotent
```
PASS: setRelayState() với cùng relay_id và timestamp không tạo duplicate state
PASS: Store update không trigger unnecessary re-render của unrelated components
FAIL: Store sử dụng mutable pattern (object mutation thay vì spread)
```

### Rule S4-UI-04: Sensor Gauge Threshold Không Hard-code trong Component
```
PASS: Threshold values được định nghĩa trong constants.ts hoặc types.ts
PASS: SensorGauge nhận warningMin/Max qua props, không tự define threshold
FAIL: Magic numbers như 5.5, 7.5 nằm trực tiếp trong component JSX/logic
```

### Rule S4-TEST-05: E2E Test Isolation
```
PASS: Mỗi test case dùng unique device_id để tránh conflict với test khác
PASS: Setup/teardown dọn dẹp retained MQTT messages sau mỗi test
FAIL: Tests share MQTT state → kết quả bị ảnh hưởng bởi thứ tự chạy
```

### Rule S4-TEST-06: Realistic Timing trong Stress Test
```
PASS: Schedule stress test có delay >= 50ms giữa mỗi publish
PASS: LWT test chờ ít nhất keep_alive * 1.5 seconds (minimum 45s)
FAIL: Publish 100 messages trong < 1s tổng (unrealistic và có thể gây false fail)
```

### Rule S4-PERF-07: RelayCard Không Re-render Khi Không Liên Quan
```
PASS: RelayCard bọc trong React.memo hoặc tương đương
PASS: Zustand selector chỉ subscribe slice nhỏ: useRelayStore(s => s.relayStates[relayId])
FAIL: RelayCard 1 re-render khi Relay 2 thay đổi state
```

### Rule S4-A11Y-08: Dashboard Accessibility
```
PASS: RelayCard có aria-label rõ ràng: "Relay 1 - Currently Spraying, 25 seconds remaining"
PASS: Countdown cập nhật có aria-live="polite" để screen reader thông báo
PASS: Status Badge có role="status"
FAIL: SVG CountdownRing không có aria-label hoặc title element
```

---

*Sprint 4 Planning — Khởi tạo bởi Baseline Agent ngày 2026-07-30*
*Thực thi: Sprint 4 Implementation Agent (phụ thuộc Sprint 3 complete)*
