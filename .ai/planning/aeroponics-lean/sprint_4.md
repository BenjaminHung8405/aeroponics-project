# Sprint 4: HTML Dashboard UI

> **Phụ thuộc:** Sprint 3 hoàn thành — NestJS Backend chạy, WebSocket `/ws` hoạt động, TimescaleDB có dữ liệu thực.  
> **Output bàn giao:** Single-file `index.html` Dashboard, được serve trực tiếp từ NestJS `ServeStatic`. Hiển thị 4 Relay Cards realtime, 6 Sensor Gauges, Schedule Form, History Charts.

---

## 1. PHẠM VI & MỤC TIÊU

### 1.1 Files bị tác động

| File | Mô tả |
|---|---|
| `aeroponics-ui/index.html` | Single-file Dashboard (HTML + CSS + JS trong 1 file) |
| `aeroponics-backend/src/main.ts` | Thêm `ServeStaticModule` để serve `aeroponics-ui/` |

**Không cần:** `package.json` riêng cho UI, `node_modules/`, `npm run build`, Next.js.

### 1.2 Mục tiêu Sprint 4

- [ ] 4 Relay Cards: trạng thái realtime (SPRAYING/COOLING_DOWN), đếm ngược `phase_remaining_s`, badge Day/Night.
- [ ] 6 Sensor Gauges (pH, EC, TDS, Nhiệt độ, Salinity, ORP) cập nhật realtime.
- [ ] Line Chart pH và Temperature 24h.
- [ ] Device status indicator (online pulse / offline).
- [ ] Form chỉnh schedule → `PUT /api/relay/:id/profile`.
- [ ] Manual Override: bật/tắt/flush → `POST /api/relay/:id/override`.
- [ ] WebSocket auto-reconnect với exponential backoff.
- [ ] Connection banner khi mất kết nối.

---

## 2. KIẾN TRÚC & LUỒNG DỮ LIỆU

### 2.1 Cách Serve Dashboard

```
NestJS main.ts:
  ServeStaticModule.forRoot({
    rootPath: join(__dirname, '..', '..', 'aeroponics-ui'),
    serveRoot: '/',
    exclude: ['/api/(.*)'],
  })
```

**Kết quả:**
- `GET /` → serve `index.html`
- `GET /api/*` → NestJS REST API (không bị overridden)
- `ws://localhost:3001/ws` → NestJS WebSocket Gateway

### 2.2 Luồng Realtime (Native WebSocket)

```
[NestJS EventsGateway ws://localhost:3001/ws]
        │
        │ new WebSocket('ws://localhost:3001/ws')
        ▼
[index.html — ws.onmessage]
  msg = JSON.parse(event.data)
  switch(msg.type):
    'relay_update':   → updateRelayCard(msg.data)
    'sensor_update':  → updateSensorGauge(msg.data)
    'device_status':  → updateDeviceStatus(msg.data)
```

### 2.3 WebSocket Message Schema (từ NestJS EventsGateway)

```json
// relay_update
{ "type": "relay_update", "data": { "relayId": 1, "state": "SPRAYING", "phaseRemainingS": 25, "mode": "day", "overrideActive": false } }

// sensor_update
{ "type": "sensor_update", "data": { "sensorId": "ph-w218-01", "phValue": 6.8, "ecValue": 1200, "temperature": 23.5, "time": "..." } }

// device_status
{ "type": "device_status", "data": { "deviceId": "esp32s3-abc", "status": "online", "rssiDbm": -65, "uptimeS": 3600 } }
```

> **Lưu ý camelCase:** NestJS serialize JSON với camelCase (TypeORM entity column names). Dashboard JavaScript dùng `msg.data.phValue`, không phải `msg.data.ph_value`.

---

## 3. PHÂN RÃ CHI TIẾT TÁC VỤ

### TRACK A — Cấu trúc file `index.html`

```
<!DOCTYPE html>
<html lang="vi">
<head>
  <!-- Meta, Title, SEO tags -->
  <!-- Google Fonts: Inter (400, 500, 600, 700) -->
  <!-- Chart.js CDN: https://cdn.jsdelivr.net/npm/chart.js@4 -->
  <style>
    /* === CSS Variables (Dark Theme) === */
    /* === Reset & Base === */
    /* === Layout: Grid, Sections === */
    /* === Components: .relay-card, .sensor-gauge, .status-badge, .countdown-ring === */
    /* === Toast, Modal, Form styles === */
    /* === Connection Banner === */
    /* === Media Queries (responsive) === */
  </style>
</head>
<body>
  <header>
    <!-- Logo + Connection Status Badge + Device Status -->
  </header>

  <div id="connection-banner" class="hidden">
    ⚠️ Mất kết nối WebSocket — đang thử lại...
  </div>

  <main>
    <section id="relay-section">
      <!-- 2x2 grid: 4 RelayCard components -->
    </section>

    <section id="sensor-section">
      <!-- 3x2 grid: 6 SensorGauge components -->
    </section>

    <section id="charts-section">
      <!-- 2 Chart.js canvases: pH 24h + Temperature 24h -->
    </section>
  </main>

  <!-- Modal: Schedule Form -->
  <!-- Modal: Override Control -->
  <!-- Toast container -->

  <script>
    // === CONSTANTS ===
    // === STATE ===
    // === WebSocketManager class ===
    // === REST API helpers ===
    // === DOM update functions ===
    // === Chart.js setup ===
    // === Modal / Form handlers ===
    // === Toast utility ===
    // === Init ===
  </script>
</body>
</html>
```

---

### TRACK B — CSS Design System

**Bắt buộc Premium UI — không được trông basic:**

```css
:root {
  /* Dark Theme */
  --bg-base:        #0d1117;
  --bg-card:        #161b22;
  --bg-card-hover:  #1c2128;
  --border:         #30363d;
  --border-glow:    rgba(88, 166, 255, 0.15);

  /* Text */
  --text-primary:   #e6edf3;
  --text-secondary: #8b949e;
  --text-muted:     #484f58;

  /* State Colors */
  --c-spraying:     #22c55e;   /* xanh lá */
  --c-cooling:      #3b82f6;   /* xanh dương */
  --c-manual-on:    #f59e0b;   /* vàng */
  --c-flush:        #8b5cf6;   /* tím */
  --c-offline:      #ef4444;   /* đỏ */
  --c-online:       #22c55e;

  /* Gradients */
  --gradient-card:  linear-gradient(135deg, #161b22 0%, #1c2128 100%);
  --gradient-glow:  radial-gradient(ellipse at top, rgba(88,166,255,0.08) 0%, transparent 60%);

  font-family: 'Inter', -apple-system, sans-serif;
}
```

**Components phải có:**

| Component | CSS class | Mô tả |
|---|---|---|
| Card | `.relay-card` | Glassmorphism, border gradient, hover lift |
| Countdown Ring | `.countdown-svg` | SVG `<circle>` với `stroke-dashoffset` transition |
| State Badge | `.state-badge` | Pill màu theo state, pulse animation khi SPRAYING |
| Sensor Gauge | `.gauge-arc` | SVG semi-circle arc fill theo giá trị |
| Connection Banner | `.connection-banner` | Slide-down từ top khi WS mất kết nối |
| Toast | `.toast` | Slide-in từ phải, auto dismiss 3s |
| Overlay | `.modal-overlay` | Blur backdrop, center card |

**Animations bắt buộc:**
- `.pulse` — keyframe animation cho trạng thái SPRAYING và online status
- `stroke-dashoffset` CSS transition 1s linear cho countdown ring
- Gauge arc transition 0.5s ease khi giá trị thay đổi

---

### TRACK C — JavaScript Modules

---

#### C-1: Constants

```javascript
const API_BASE = window.location.origin  // http://localhost:3001
const WS_URL   = `ws://${window.location.host}/ws`

const RELAY_LABELS = { 1: 'Khu A', 2: 'Khu B', 3: 'Khu C', 4: 'Khu D' }

const STATE_CONFIG = {
  SPRAYING:      { label: '🔵 Phun',    cssClass: 'state-spraying',   color: '#22c55e' },
  COOLING_DOWN:  { label: '❄️ Nghỉ',    cssClass: 'state-cooling',    color: '#3b82f6' },
  MANUAL_ON:     { label: '🟡 Manual',  cssClass: 'state-manual-on',  color: '#f59e0b' },
  MANUAL_OFF:    { label: '⚫ Tắt',      cssClass: 'state-manual-off', color: '#8b949e' },
  FLUSH:         { label: '💧 Flush',   cssClass: 'state-flush',      color: '#8b5cf6' },
  STOPPED:       { label: '⏹ Dừng',    cssClass: 'state-stopped',    color: '#484f58' },
}

const SENSOR_CONFIG = {
  phValue:     { label: 'pH',          unit: 'pH',    min: 0,    max: 14,   warnMin: 5.5, warnMax: 7.5,  critMin: 4.5, critMax: 8.5 },
  ecValue:     { label: 'EC',          unit: 'µS/cm', min: 0,    max: 5000, warnMin: 500, warnMax: 2500, critMin: 200, critMax: 3500 },
  tdsValue:    { label: 'TDS',         unit: 'ppm',   min: 0,    max: 3000 },
  temperature: { label: 'Nhiệt độ',   unit: '°C',    min: 0,    max: 50,   warnMin: 18,  warnMax: 28,   critMin: 15,  critMax: 32 },
  salinity:    { label: 'Độ mặn',     unit: 'ppt',   min: 0,    max: 50 },
  orpValue:    { label: 'ORP',         unit: 'mV',    min: -500, max: 500 },
}
```

---

#### C-2: WebSocket Manager

```javascript
class WebSocketManager {
  constructor(url, onMessage) {
    this.url = url
    this.onMessage = onMessage
    this.ws = null
    this.reconnectDelay = 1000
    this.maxDelay = 30000
  }

  connect() {
    try {
      this.ws = new WebSocket(this.url)
      this.ws.onopen    = () => this._onOpen()
      this.ws.onmessage = (e) => this.onMessage(JSON.parse(e.data))
      this.ws.onclose   = () => this._onClose()
      this.ws.onerror   = () => {}  // onclose sẽ handle
    } catch { this._scheduleReconnect() }
  }

  _onOpen() {
    showConnectionStatus(true)
    this.reconnectDelay = 1000
  }

  _onClose() {
    showConnectionStatus(false)
    this._scheduleReconnect()
  }

  _scheduleReconnect() {
    setTimeout(() => this.connect(), this.reconnectDelay)
    this.reconnectDelay = Math.min(this.reconnectDelay * 2, this.maxDelay)
  }
}
```

---

#### C-3: Relay Card Renderer

```javascript
// HTML template cho mỗi relay card (được generate trong init)
function renderRelayCard(relayId) {
  return `
    <div class="relay-card" id="relay-${relayId}-card" data-relay="${relayId}">
      <div class="card-header">
        <h3>Relay ${relayId} — ${RELAY_LABELS[relayId]}</h3>
        <span class="state-badge" id="relay-${relayId}-badge">--</span>
      </div>
      <div class="card-center">
        <div class="countdown-ring-wrapper">
          <svg class="countdown-svg" viewBox="0 0 120 120" width="120" height="120">
            <circle class="ring-bg"   cx="60" cy="60" r="50" />
            <circle class="ring-fill" cx="60" cy="60" r="50"
              id="relay-${relayId}-ring"
              stroke-dasharray="${2 * Math.PI * 50}"
              stroke-dashoffset="${2 * Math.PI * 50}"
            />
          </svg>
          <div class="countdown-text" id="relay-${relayId}-countdown">--</div>
        </div>
      </div>
      <div class="card-meta">
        <span class="mode-badge" id="relay-${relayId}-mode">--</span>
        <span class="profile-info" id="relay-${relayId}-profile">--</span>
      </div>
      <div class="card-actions">
        <button onclick="openOverrideModal(${relayId})">⚡ Override</button>
        <button onclick="openScheduleModal(${relayId})">📅 Schedule</button>
      </div>
    </div>
  `
}

function updateRelayCard(data) {
  const { relayId, state, phaseRemainingS, mode, overrideActive } = data
  appState.relays[relayId] = data

  // State badge
  const cfg = STATE_CONFIG[state] || STATE_CONFIG['STOPPED']
  const badge = document.getElementById(`relay-${relayId}-badge`)
  badge.textContent = cfg.label
  badge.className = `state-badge ${cfg.cssClass}`

  // Mode badge
  document.getElementById(`relay-${relayId}-mode`).textContent = mode === 'night' ? '🌙 Đêm' : '☀️ Ngày'

  // Countdown ring
  const profile = appState.relayProfiles[relayId]
  const totalS = getTotalDuration(state, mode, profile)
  updateCountdownRing(relayId, phaseRemainingS, totalS, cfg.color)
}

function updateCountdownRing(relayId, remainingS, totalS, color) {
  const circle = document.getElementById(`relay-${relayId}-ring`)
  const text = document.getElementById(`relay-${relayId}-countdown`)
  const circumference = 2 * Math.PI * 50
  const progress = totalS > 0 ? (remainingS / totalS) : 0
  circle.style.strokeDashoffset = circumference * (1 - progress)
  circle.style.stroke = color
  text.textContent = remainingS > 0 ? `${remainingS}s` : '--'
}
```

---

#### C-4: Sensor Gauge Renderer

```javascript
function renderSensorGauge(key, cfg) {
  return `
    <div class="sensor-gauge" id="gauge-${key}">
      <div class="gauge-label">${cfg.label}</div>
      <svg class="gauge-svg" viewBox="0 0 120 70" width="120" height="70">
        <path class="gauge-bg-arc"   d="..." />
        <path class="gauge-fill-arc" d="..." id="gauge-${key}-arc" />
      </svg>
      <div class="gauge-value" id="gauge-${key}-value">--</div>
      <div class="gauge-unit">${cfg.unit}</div>
    </div>
  `
}

function updateSensorGauge(data) {
  appState.sensor = data
  for (const [key, cfg] of Object.entries(SENSOR_CONFIG)) {
    const value = data[key]
    const el = document.getElementById(`gauge-${key}-value`)
    if (!el) continue
    el.textContent = value != null ? value.toFixed(key === 'ecValue' || key === 'tdsValue' ? 0 : 2) : '--'

    // Color coding
    let color = 'var(--c-online)'
    if (cfg.critMin != null && value < cfg.critMin) color = 'var(--c-offline)'
    else if (cfg.critMax != null && value > cfg.critMax) color = 'var(--c-offline)'
    else if (cfg.warnMin != null && value < cfg.warnMin) color = '#f59e0b'
    else if (cfg.warnMax != null && value > cfg.warnMax) color = '#f59e0b'
    el.style.color = color

    // Update SVG arc
    if (cfg.min != null && cfg.max != null && value != null) {
      const pct = Math.max(0, Math.min(1, (value - cfg.min) / (cfg.max - cfg.min)))
      updateGaugeArc(`gauge-${key}-arc`, pct, color)
    }
  }
}
```

---

#### C-5: REST API Helpers

```javascript
const api = {
  async get(path) {
    const res = await fetch(`${API_BASE}${path}`)
    if (!res.ok) throw new Error(`${res.status} ${res.statusText}`)
    return res.json()
  },
  async put(path, body) {
    const res = await fetch(`${API_BASE}${path}`, {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body),
    })
    if (!res.ok) throw new Error(`${res.status} ${res.statusText}`)
    return res.json()
  },
  async post(path, body) {
    const res = await fetch(`${API_BASE}${path}`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body),
    })
    if (!res.ok) throw new Error(`${res.status} ${res.statusText}`)
  },
}
```

---

#### C-6: Chart.js History Charts

```javascript
let phChart, tempChart

function initCharts() {
  const chartDefaults = {
    type: 'line',
    options: {
      responsive: true,
      animation: false,
      plugins: {
        legend: { display: false },
        tooltip: { mode: 'index', intersect: false },
      },
      scales: {
        x: { grid: { color: '#30363d' }, ticks: { color: '#8b949e', maxTicksLimit: 8 } },
        y: { grid: { color: '#30363d' }, ticks: { color: '#8b949e' } },
      },
    },
  }

  phChart = new Chart(document.getElementById('chart-ph'), {
    ...chartDefaults,
    data: { labels: [], datasets: [{
      label: 'pH', data: [],
      borderColor: '#22c55e', backgroundColor: 'rgba(34,197,94,0.1)',
      tension: 0.4, fill: true,
    }] },
    options: { ...chartDefaults.options, scales: { ...chartDefaults.options.scales, y: { ...chartDefaults.options.scales.y, min: 4, max: 9 } } },
  })

  // Tương tự tempChart
}

async function refreshCharts() {
  const readings = await api.get('/api/sensor/ph-w218-01/history?hours=24')
  const labels = readings.map(r => {
    const d = new Date(r.time)
    return d.toLocaleTimeString('vi-VN', { hour: '2-digit', minute: '2-digit' })
  })
  phChart.data.labels = labels
  phChart.data.datasets[0].data = readings.map(r => r.phValue)
  phChart.update()
  // Tương tự tempChart với r.temperature
}
```

---

#### C-7: Initialization

```javascript
const appState = {
  relays:        { 1: null, 2: null, 3: null, 4: null },
  relayProfiles: { 1: null, 2: null, 3: null, 4: null },
  sensor:        null,
  device:        null,
}

async function init() {
  // 1. Render relay cards HTML
  const container = document.getElementById('relay-section')
  container.innerHTML = [1,2,3,4].map(renderRelayCard).join('')

  // 2. Render sensor gauges HTML
  const sensorSection = document.getElementById('sensor-section')
  sensorSection.innerHTML = Object.entries(SENSOR_CONFIG).map(([k,v]) => renderSensorGauge(k,v)).join('')

  // 3. Load relay profiles từ REST API
  const profiles = await api.get('/api/relay').catch(() => [])
  for (const p of profiles) appState.relayProfiles[p.relayId] = p

  // 4. Load initial relay state
  for (let id = 1; id <= 4; id++) {
    const state = await api.get(`/api/relay/${id}/state`).catch(() => null)
    if (state) updateRelayCard(state)
  }

  // 5. Load latest sensor
  const latest = await api.get('/api/sensor/ph-w218-01/latest').catch(() => null)
  if (latest) updateSensorGauge(latest)

  // 6. Init charts + load history
  initCharts()
  await refreshCharts().catch(() => {})

  // 7. Connect WebSocket
  const wsManager = new WebSocketManager(WS_URL, (msg) => {
    switch (msg.type) {
      case 'relay_update':  updateRelayCard(msg.data);   break
      case 'sensor_update': updateSensorGauge(msg.data); break
      case 'device_status': updateDeviceStatus(msg.data);break
    }
  })
  wsManager.connect()

  // 8. Refresh charts mỗi 5 phút
  setInterval(refreshCharts, 5 * 60 * 1000)
}

window.addEventListener('DOMContentLoaded', init)
```

---

### TRACK D — Tích hợp vào NestJS

**`src/main.ts` — Thêm ServeStatic:**

```typescript
import { ServeStaticModule } from '@nestjs/serve-static';
import { join } from 'path';

@Module({
  imports: [
    // ...other modules
    ServeStaticModule.forRoot({
      rootPath: join(__dirname, '..', '..', 'aeroponics-ui'),
      exclude: ['/api/(.*)'],
    }),
  ],
})
```

> `aeroponics-ui/` ở cùng level với `aeroponics-backend/`, được copy vào Docker image.

---

## 4. TIÊU CHUẨN RÀ SOÁT CỨNG (Sprint 4)

### Rule S4-WS-01: Native WebSocket, Không Socket.IO Client
```
PASS: new WebSocket(WS_URL) — không cần CDN socket.io-client
FAIL: <script src="socket.io.min.js"> trong index.html
```

### Rule S4-CASE-02: camelCase từ NestJS, Không snake_case
```
PASS: msg.data.relayId, msg.data.phValue, msg.data.phaseRemainingS
FAIL: msg.data.relay_id, msg.data.ph_value (snake_case là Python convention, không phải NestJS)
```

### Rule S4-UI-03: CDN Chart.js, Không NPM
```
PASS: <script src="https://cdn.jsdelivr.net/npm/chart.js@4/dist/chart.umd.min.js">
FAIL: npm install chart.js + import/require
```

### Rule S4-UI-04: API_BASE Dynamic, Không Hardcode
```
PASS: const API_BASE = window.location.origin
PASS: const WS_URL = `ws://${window.location.host}/ws`
FAIL: 'http://localhost:3001' hardcode
```

### Rule S4-UX-05: Null-safe Rendering
```
PASS: value != null ? value.toFixed(2) : '--'
FAIL: Hiển thị 'null', 'undefined', 'NaN' trên UI
```

### Rule S4-UX-06: WebSocket Reconnect Banner
```
PASS: Banner "Mất kết nối..." hiện khi WS offline, ẩn tự động khi reconnect
FAIL: User không biết WebSocket đã mất kết nối
```

### Rule S4-DESIGN-07: Premium UI (Non-negotiable)
```
PASS: Dark theme (#0d1117 base), Inter font, SVG countdown ring, gradient borders
PASS: State badge có màu theo state (green/blue/yellow/purple)
PASS: Sensor gauge có SVG arc fill + color coding theo threshold
FAIL: Plain HTML table, white background, no animations
FAIL: Text-only relay status với không có visual indicators
```

---

*Sprint 4 Planning (Vanilla HTML Dashboard — NestJS Backend Integration) — Updated 2026-07-30*
