# Sprint 4: Dashboard State Synchronization & E2E Validation (Next.js & Nginx)

> **Phụ thuộc:** Sprint 3 PASS (Backend Ingestion & Admission Pipeline).
> **Output bàn giao:** Dashboard không còn Optimistic UI, nút điều khiển hiển thị `RUNNING` chỉ sau `FLOW_CONFIRMED` từ WebSocket, Nginx Reverse Proxy port 6003 hoạt động, E2E test toàn bộ chuỗi control loop.
> **Golden Baseline tham chiếu:** [`docs/interface-wire-contract.md`](../../docs/interface-wire-contract.md) §8–§9, [`docs/e2e-critical-sequence-flows.md`](../../docs/e2e-critical-sequence-flows.md).

---

## 1. PHẠM VI & MỤC TIÊU

### 1.1 Modules / Components bị tác động trực tiếp

| Module / File | Hành động | Mô tả |
|---|---|---|
| `nginx/aeroponics.conf` | **Sửa** | Đổi `listen 80` → `listen 6003`, cấu hình WebSocket `/socket.io/` upgrade chính xác |
| `nginx/aeroponics.conf.example` | **Sửa** | Template đồng bộ port 6003 |
| `aeroponics-ui/src/hooks/useWebSocket.ts` | **Sửa + Khởi lại** | Bỏ stub, hiện thực hóa native WebSocket reconnect/backoff đầy đủ |
| `aeroponics-ui/src/store/useNodeStore.ts` | **Sửa** | Thêm action `applyFlowConfirmed(nodeId, event)`, strict `isRunning` derived state |
| `aeroponics-ui/src/lib/constants.ts` | **Sửa** | Bổ sung `RUNNING` state policy, WS path `/socket.io/`, tăng `STALE_THRESHOLD` |
| `aeroponics-ui/src/lib/types.ts` | **Sửa** | Thêm `RUNNING` vào enum trạng thái, thêm `FlowConfirmedEvent` payload type |
| `aeroponics-ui/src/components/dashboard/NodeCard.tsx` | **Sửa** | Khử `isSprayingActive` từ local state; dựa hoàn toàn vào `node.flowConfirmed` |
| `aeroponics-ui/src/components/dashboard/NodeDetailModal.tsx` | **Sửa** | Hiển thị evidence pipeline stages, không hiển thị `RUNNING` sớm |
| `aeroponics-ui/src/components/dashboard/PumpControl.tsx` | **Tạo mới** | Nút BẬT/TẮT bơm chỉ phản hồi theo lifecycle events từ WS |
| `aeroponics-ui/src/components/common/OutcomeBadge.tsx` | **Sửa** | Render `RUNNING` chỉ khi outcome = `FLOW_CONFIRMED` + flowConfirmed=true |
| `test/e2e/` | **Tạo mới** | E2E test cho full control loop (UI → Backend → MQTT → Gateway → Node) |
| `docker-compose.yml` | **Sửa** | Expose UI/nginx port 6003 |

### 1.2 Mục tiêu cụ thể Sprint 4

- [ ] **Khử triệt Optimistic UI:** Không có UI nào hiển thị `RUNNING`/pump active trước khi nhận WebSocket event `node_flow` với `flowConfirmed=true`.
- [ ] **Nút điều khiển:** Bấm BẬT bơm → gọi API → mới đổi trạng thái UI là `PENDING` → sau khi nhận `FLOW_CONFIRMED` mới hiển thị `RUNNING`.
- [ ] **WebSocket reconnneci:** `useWebSocket` reconnect với exponential backoff (1s → 1.5s → 2.25s → ... → max 30s), không spam server khi mất kết nối.
- [ ] **Nginx port 6003:** Reverse proxy hiển thị đúng WebSocket upgrade. `/socket.io/` → backend, `/api/` → backend, `/` → Next.js UI.
- [ ] **E2E test:** Playwright hoặc Cypress test full loop: login → dashboard → override ON → wait FLOW_CONFIRMED → assert RUNNING badge.
- [ ] Toàn bộ test unit + integration pass.

---

## 2. KIẾN TRÚC & LUỒNG DỮ LIỆU

### 2.1 Luồng Dashboard Sync (Không Optimistic)

```text
[User bấm nút "BẬT BƠM" trên NodeCard]
        │
        ▼
[POST /api/pump-command/override  → body: {nodeId, desired_state:"ON", run_lease_ms:60000}]
        │
        ▼
[Backend: pump-command.service.ts]
        │
        ├── UC-BE-10 check: calibration ACTIVE? → nếu không → 400 + toast lỗi
        │
        ├── Publish MQTT command → aeroponics/v1/node/{nodeId}/command (retain=false)
        │
        └── Publish REST response {status: "ACCEPTED", command_id}
        │
        ▼
[UI nhận response ACCEPTED]
        │
        └── NodeStore.updateNode(nodeId, { outcome: "PENDING" })
            → NodeCard hiển thị badge "Đang gửi lệnh" (KHÔNG phải RUNNING)
        │
        ▼
────────────────────────── WS EVENT FLOW ──────────────────────────
        │
[Gateway FSM: RF_ACKED → GATE_FEEDBACK_ON → CURRENT_DETECTED → FLOW_CONFIRMED]
        │
        ▼
[Backend WS Gateway broadcast: node_flow {event:"node_flow", flowConfirmed:true, nodeId}]
        │
        ▼
[UI useWebSocket listener → NodeStore.applyFlowConfirmed(nodeId, event)]
        │
        └── NodeStore.updateNode(nodeId, { flowConfirmed: true, outcome: "FLOW_CONFIRMED" })
        │
        ▼
[NodeCard re-render → OutcomeBadge hiển thị "Xác nhận dòng chảy" + RUNNING glow]
```

**Invariant:** `RUNNING` = `node.flowConfirmed === true` (chỉ từ WS event, không từ local setState khi bấm nút).

### 2.2 Kiến trúc WebSocket Reconnect & Event Flow

```text
┌───────────────────────────────────────────────────────────────────┐
│                WebSocket Client (aeroponics-ui)                  │
│                                                                   │
│  useWebSocket() hook                                              │
│    ├── useState: WSConnectionState ('connecting'/'connected'/...) │
│    ├── useEffect: connect()                                       │
│    ├── connect(): new WebSocket(`${wsUrl}/socket.io/?EIO=4`)     │
│    ├── onmessage: parse JSON → dispatch to store                  │
│    │     │                                                        │
│    │     ├── node_telemetry → useNodeStore.updateNode()           │
│    │     ├── node_flow      → useNodeStore.applyFlowConfirmed()   │
│    │     ├── pump_command_update → useNodeStore.updateOutcome()   │
│    │     └── device_status   → useDeviceStore.updateDevice()      │
│    ├── onclose: schedule reconnect with exponential backoff       │
│    │     backoff = min(WS_RECONNECT_BASE_DELAY_MS * FACTOR^retry) │
│    │     FACTOR = 1.5, MAX_DELAY = 30s                            │
│    ├── onerror: update state, do NOT spam reconnect               │
│    └── reconnectNow(): manual trigger (from WS banner)            │
└───────────────────────────────────────────────────────────────────┘
```

### 2.3 Nginx Reverse Proxy (Port 6003) Routing

```text
[Client: https://domain.com:6003 (hoặc :80 → :6003)]
        │
        ▼
[Nginx listen 6003]
        │
        ├── location /ws {                       ← WebSocket (native WS)
        │     │  proxy_pass http://aero_backend;
        │     │  proxy_http_version 1.1;
        │     │  proxy_set_header Upgrade $http_upgrade;
        │     │  proxy_set_header Connection "upgrade";
        │     │  proxy_read_timeout 86400s;
        │     │  proxy_send_timeout 86400s;
        │     │  → Chuyển tới NestJS EventsGateway (native WS path /ws)
        │
        ├── location /socket.io/ {               ← Socket.IO duplex (cho retro UI)
        │     │  proxy_pass http://aero_backend;
        │     │  proxy_http_version 1.1;
        │     │  proxy_set_header Upgrade $http_upgrade;   ← CRITICAL
        │     │  proxy_set_header Connection "upgrade";
        │     │  proxy_read_timeout 86400s;
        │     │  → Socket.IO engine.io handshake + polling/xhr + WS upgrade
        │
        ├── location /api/ {                     ← REST API
        │     │  proxy_pass http://aero_backend;
        │     │  proxy_set_header Host $host;
        │     │  proxy_set_header X-Real-IP $remote_addr;
        │     │  proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        │     │  proxy_set_header X-Forwarded-Proto $scheme;
        │
        └── location / {                          ← Next.js App
              │  proxy_pass http://aero_ui;
              │  proxy_set_header Host $host;
              │  proxy_set_header X-Real-IP $remote_addr;
              │  proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
              │  proxy_set_header X-Forwarded-Proto $scheme;
        }
```

### 2.4 Luồng E2E Validation

```text
[Playwright test: e2e/pump-control.spec.ts]
        │
        ├── 1. Goto http://localhost:3000/login
        ├── 2. Fill username/password → submit
        │     → Nhận JWT cookie
        ├── 3. Navigate to /dashboard
        ├── 4. Chờ NodeCards render (ngắt WebSocket)
        ├── 5. Mock gateway: publish MQTT telemetry (via test broker)
        ├── 6. Click button "BẬT BƠM" trên Node #4
        │     → Expect OutcomeBadge = "Đang gửi lệnh" (PENDING)
        ├── 7. Mock gateway: publish FLOW_CONFIRMED event qua MQTT
        │     → Backend broadcast `node_flow` {flowConfirmed:true}
        ├── 8. Expect OutcomeBadge = "Xác nhận dòng chảy" (FLOW_CONFIRMED)
        │     → NodeCard glow active (relay-glow-active) hiển thị RUNNING
        ├── 9. Click "TẮT BƠM"
        │     → Expect OutcomeBadge trả về "Chờ lệnh"
        │
        └── Assertions: NO intermediate RUNNING state between step 6 and step 8
```

---

## 3. PHÂN RÁ CHI TIẾU TÁC VỤ

### TRACK N — WebSocket Client Reconstruction (Khử Stub)

#### Task N-1: `aeroponics-ui/src/hooks/useWebSocket.ts` — Hiện thực hóa native WS

- **File:** `aeroponics-ui/src/hooks/useWebSocket.ts`
- **Hàm bị ảnh hưởng:** `useWebSocket()`, `resolveWsUrl()`, `calculateBackoffDelay()`

```typescript
'use client';

export type WsConnectionState =
  | 'connecting' | 'connected' | 'disconnected' | 'reconnecting';

export interface UseWebSocketReturn {
  isConnected: boolean;
  connectionState: WsConnectionState;
  retryCount: number;
  reconnectNow: () => void;
}

export function calculateBackoffDelay(retryCount: number): number {
  const base = WS_RECONNECT_BASE_DELAY_MS;        // 1000ms
  const factor = WS_RECONNECT_FACTOR;              // 1.5
  const max = WS_RECONNECT_MAX_DELAY_MS;           // 30s
  const delay = base * Math.pow(factor, retryCount);
  return Math.min(delay, max);
}

export function resolveWsUrl(): string {
  if (typeof window === 'undefined') return '';
  const base = process.env.NEXT_PUBLIC_WS_URL;
  if (base) return base;
  const proto = window.location.protocol === 'https:' ? 'wss' : 'ws';
  return `${proto}://${window.location.hostname}:${window.location.port}/ws`;
}

/**
 * Real-time WebSocket with exponential backoff reconnect.
 * NO optimistic UI: dispatch to Zustand store only on real server events.
 */
export function useWebSocket(): UseWebSocketReturn {
  const [connectionState, setConnectionState] = useState<WsConnectionState>('connecting');
  const [retryCount, setRetryCount] = useState(0);
  const wsRef = useRef<WebSocket | null>(null);
  const retryTimerRef = useRef<NodeJS.Timeout | null>(null);

  const connect = useCallback(() => {
    setConnectionState((prev) =>
      prev === 'connected' ? prev : 'connecting',
    );
    try {
      const url = resolveWsUrl();
      const ws = new WebSocket(url);
      wsRef.current = ws;

      ws.onopen = () => {
        setConnectionState('connected');
        setRetryCount(0);
      };

      ws.onmessage = (event) => {
        try {
          const data = JSON.parse(event.data as string);
          wsMessageHandler(data);
        } catch {
          // ignore malformed frames — never crash WS loop
        }
      };

      ws.onclose = () => scheduleReconnect();
      ws.onerror = () => {
        // onerror fires before onclose; do NOT spam reconnect here
      };
    } catch {
      scheduleReconnect();
    }
  }, []);

  const scheduleReconnect = useCallback(() => {
    if (retryTimerRef.current) return;
    setConnectionState('reconnecting');
    const delay = calculateBackoffDelay(retryCount);
    retryTimerRef.current = setTimeout(() => {
      retryTimerRef.current = null;
      setRetryCount((c) => c + 1);
      connect();
    }, delay);
  }, [retryCount]);  // FIX: [retryCount] thay vì [connect, retryCount] — backoff tính toán đúng

  // Mount / unmount lifecycle
  useEffect(() => {
    connect();
    return () => {
      if (retryTimerRef.current) clearTimeout(retryTimerRef.current);
      wsRef.current?.close();
    };
  }, [connect]);

  return {
    isConnected: connectionState === 'connected',
    connectionState,
    retryCount,
    reconnectNow: () => {
      if (retryTimerRef.current) clearTimeout(retryTimerRef.current);
      retryTimerRef.current = null;
      wsRef.current?.close();
      setRetryCount(0);
      connect();
    },
  };
}
```

#### Task N-2: `aeroponics-ui/src/hooks/useWebSocket.ts` — Message dispatcher (WS → Store)

- **File:** `aeroponics-ui/src/hooks/useWebSocket.ts`
- **Hàm mới:** `wsMessageHandler(msg: WsMessage)`

```typescript
/**
 * Dispatch incoming WebSocket events to the appropriate Zustand store.
 * NO optimistic writes: only server-authoritative state is applied.
 */
function wsMessageHandler(msg: any): void {
  if (!msg?.event) return;

  switch (msg.event) {
    case WS_EVENTS.NODE_TELEMETRY:
      useNodeStore.getState().updateNode(msg.data.nodeId, {
        healthStatus: msg.data.health,
        scheduleState: msg.data.scheduleState,
        overrideState: msg.data.overrideState,
        sensorSerial: msg.data.sensorSerial,
        lastSeenAt: msg.timestamp,
      });
      break;

    case WS_EVENTS.NODE_FLOW:
      // ⚠️ SET FLOW_CONFIRMED STATUS ONLY HERE — authoritative source
      useNodeStore.getState().applyFlowConfirmed(
        msg.data.nodeId,
        msg.data.flowConfirmed,
        msg.data.flowRateLpm,
        msg.data.confirmedAt ?? msg.timestamp,
      );
      break;

    case WS_EVENTS.PUMP_COMMAND_UPDATE:
      useNodeStore.getState().updateOutcome(
        msg.data.nodeId,
        msg.data.outcome,
      );
      break;

    case WS_EVENTS.STALENESS_ALERT:
      useNodeStore.getState().updateNode(msg.data.nodeId, {
        isStale: true,
        staleForMs: msg.data.staleForMs,
      });
      break;

    case WS_EVENTS.DEVICE_STATUS:
      useDeviceStore.getState().updateDevice(msg.data);
      break;

    default:
      break;
  }
}
```

---

### TRACK O — Store & State Management (Flow Confirmed Authority)

#### Task O-1: `aeroponics-ui/src/store/useNodeStore.ts` — `applyFlowConfirmed` action

- **File:** `aeroponics-ui/src/store/useNodeStore.ts`
- **Hàm mới:** `applyFlowConfirmed()`, `updateOutcome()`

```typescript
export interface NodeStoreState {
  nodes: Record<number, NodeState>;
  initNodes: (nodeResponses: NodeStatusResponse[]) => void;
  updateNode: (id: number, partial: Partial<NodeState>) => void;
  // NEW: authoritative FLOW_CONFIRMED handler (only from WS event)
  applyFlowConfirmed: (
    id: number,
    flowConfirmed: boolean,
    flowRateLpm?: number,
    confirmedAt?: string | null,
  ) => void;
  // NEW: outcome-only update (ACK/REJECTED/etc, no RUNNING implication)
  updateOutcome: (id: number, outcome: string) => void;
  resetAll: () => void;
}
```

```typescript
applyFlowConfirmed: (id, flowConfirmed, flowRateLpm, confirmedAt) => {
  if (!AGU_NODE_IDS.includes(id as (typeof AGU_NODE_IDS)[number])) return;
  set((state) => {
    const current = state.nodes[id] || createDefaultNode(id);
    return {
      nodes: {
        ...state.nodes,
        [id]: {
          ...current,
          flowConfirmed,                    // ← ONLY set true via this action
          flowLpm: flowRateLpm ?? current.flowLpm,
          flowConfirmedAt: confirmedAt ?? current.flowConfirmedAt,
          outcome: flowConfirmed ? 'FLOW_CONFIRMED' : current.outcome,
        },
      },
    };
  });
},

updateOutcome: (id, outcome) => {
  if (!AGU_NODE_IDS.includes(id as (typeof AGU_NODE_IDS)[number])) return;
  set((state) => {
    const current = state.nodes[id] || createDefaultNode(id);
    return {
      nodes: {
        ...state.nodes,
        [id]: { ...current, outcome },
      },
    };
  });
},
```

#### Task O-2: `aeroponics-ui/src/lib/types.ts` — Thêm enum & event types

- **File:** `aeroponics-ui/src/lib/types.ts`
- **Hàm mới (types):** `FlowConfirmedWsEvent`, `PumpCommandUpdateWsEvent`

```typescript
// New authoritative WS event types
export interface FlowConfirmedWsEvent {
  nodeId: number;
  flowConfirmed: boolean;
  flowRateLpm: number;
  commandId?: string;
  confirmedAt?: string;
  timestamp: string;
}

export interface PumpCommandUpdateWsEvent {
  nodeId: number;
  commandId: string;
  outcome:
    | 'PENDING' | 'RF_ACKED' | 'FLOW_CONFIRMED'
    | 'REJECTED' | 'TIMEOUT' | 'FAULT';
  ackedAt?: string;
  flowConfirmedAt?: string | null;
}

// Derive "isRunning" — synchronised with FLOW_CONFIRMED only
export const isNodeRunning = (node: {
  flowConfirmed: boolean;
  outcome: string;
}): boolean => node.flowConfirmed && node.outcome === 'FLOW_CONFIRMED';
```

---

### TRACK P — Component Refactor (Khử Optimistic UI)

#### Task P-1: `aeroponics-ui/src/components/dashboard/NodeCard.tsx` — Bỏ optimistic glow

- **File:** `aeroponics-ui/src/components/dashboard/NodeCard.tsx`
- **Hàm bị ảnh hưởng:** `NodeCard()`

```typescript
// TRƯỚC — optimistic: glow bật từ local state / scheduleState
const isSprayingActive =
  node.outcome === 'FLOW_CONFIRMED' ||
  node.flowConfirmed ||
  node.scheduleState === 'SPRAYING';

// SAU — chỉ bật khi có FLOW_CONFIRMED từ WS, không từ scheduleState/outcome local
const isRunning = isNodeRunning(node);  // flowConfirmed && outcome === 'FLOW_CONFIRMED'
const isSprayingActive = isRunning;      // glow = RUNNING only
```

- **Bắt buộc:**
  - Bỏ `node.scheduleState === 'SPRAYING'` khỏi glow condition (schedule state không phải bằng chứng flow).
  - Bỏ `node.outcome === 'FLOW_CONFIRMED'` nếu không đi kèm `node.flowConfirmed` (chống out-of-sync).

#### Task P-2: `aeroponics-ui/src/components/common/OutcomeBadge.tsx` — Chỉ render RUNNING khi isRunning

- **File:** `aeroponics-ui/src/components/common/OutcomeBadge.tsx`

```typescript
// Endorse RUNNING only from authoritative flow status
const showRunning =
  outcome === 'FLOW_CONFIRMED' && nodeFlowConfirmed === true;

// Render:
//   showRunning  → Badge "Xác nhận dòng chảy" (primary + glow)
//   outcome === 'RF_ACKED' → Badge "Đã nhận lệnh (RF)" (indigo, NO glow)
//   outcome === 'PENDING'  → Badge "Đang gửi lệnh" (subtle, NO glow)
```

#### Task P-3: `aeroponics-ui/src/components/dashboard/PumpControl.tsx` — Tạo mới nút điều khiển

- **File:** `aeroponics-ui/src/components/dashboard/PumpControl.tsx`
- **Hàm mới:** `PumpControl()`, `handleOnClick()`, `handleOffClick()`

```typescript
'use client';

import { useState } from 'react';
import { useNode } from '../../store/useNodeStore';
import { api } from '../../lib/api';

export function PumpControl({ nodeId }: { nodeId: number }) {
  const node = useNode(nodeId);
  const [sending, setSending] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const isRunning = isNodeRunning(node); // from store, NOT optimistic

  const handleOnClick = async () => {
    setSending(true);
    setError(null);
    try {
      // Backend check: UC-BE-10 → rejection if calibration not ACTIVE
      const res = await api.post(`/api/node/${nodeId}/override`, {
        action: 'ON',
        node_id: nodeId,
        run_lease_ms: 60000,
      });
      // After ACCEPTED response:
      //   UI only shows PENDING (never RUNNING immediately)
      useNodeStore.getState().updateOutcome(nodeId, 'PENDING');
      // RUNNING will only appear after WS FLOW_CONFIRMED arrives
    } catch (err: any) {
      setError(err?.message ?? 'Lỗi gửi lệnh bơm');
      useNodeStore.getState().updateOutcome(nodeId, 'REJECTED');
    } finally {
      setSending(false);
    }
  };

  const handleOffClick = async () => {
    // similar: dispatch PUMP_OFF, set PENDING, await WS event
  };

  return (
    <div className="flex gap-2">
      <button
        onClick={handleOnClick}
        disabled={sending || !node.calibrationStatus}
        className="..."
        aria-label={`Bật bơm ${nodeId}`}
      >
        {isRunning ? 'RUNNING' : 'BẬT BƠM'}
      </button>
      <button onClick={handleOffClick} disabled={sending} className="...">
        TẮT BƠM
      </button>
      {error && <p className="text-xs text-danger">{error}</p>}
    </div>
  );
}
```

#### Task P-4: `aeroponics-ui/src/components/dashboard/NodeDetailModal.tsx` — Evidence pipeline display

- **File:** `aeroponics-ui/src/components/dashboard/NodeDetailModal.tsx`
- **Hàm bị ảnh hưởng:** `NodeDetailModal()`

```typescript
// Hiển thị evidence pipeline stages từ server (không suy diễn):
const evidenceStages = [
  { label: 'Lệnh đã gửi', done: node.outcome !== 'PENDING' },
  { label: 'RF đã nhận (ACK)', done: ['RF_ACKED', 'FLOW_CONFIRMED'].includes(node.outcome) },
  { label: 'Cảm biến dòng chảy', done: node.flowConfirmed },
  { label: 'Xác nhận dòng chảy', done: node.flowConfirmed && node.outcome === 'FLOW_CONFIRMED' },
];
// Mỗi stage hiển thị dot green/active chỉ khi server báo tương ứng.
```

---

### TRACK Q — Nginx Reverse Proxy (Port 6003)

#### Task Q-1: `nginx/aeroponics.conf` — Port 6003 + socket.io/ upgrade

- **File:** `nginx/aeroponics.conf`

```nginx
# ============================================================
# Nginx Reverse Proxy — Aeroponics (Port 6003)
# ============================================================

upstream aero_ui {
    server aero-ui:3000;
    keepalive 32;
}

upstream aero_backend {
    server aero-backend:3001;
    keepalive 32;
}

server {
    listen 6003;
    server_name domain.com www.domain.com;

    # === WebSocket native — PHẢI khai báo TRƯỚC /api/ và / ===
    location /ws {
        proxy_pass http://aero_backend;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
        proxy_read_timeout 86400s;
        proxy_send_timeout 86400s;
    }

    # === Socket.IO duplex (retro path) ===
    location /socket.io/ {
        proxy_pass http://aero_backend;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";
        proxy_set_header Host $host;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_read_timeout 86400s;
        proxy_send_timeout 86400s;
        proxy_buffering off;   # CRITICAL: prevent latency on long-poll
    }

    # === NestJS REST API ===
    location /api/ {
        proxy_pass http://aero_backend;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
        proxy_pass_header Access-Control-Allow-Origin;
    }

    # === Next.js App (catch-all) ===
    location / {
        proxy_pass http://aero_ui;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
    }
}
```

- **Critical notes:**
  - `proxy_buffering off` bắt buộc cho `/socket.io/` để tránh buffer delay.
  - `Connection "upgrade"` header bắt buộc cho cả `/ws` và `/socket.io/`.
  - Port 6003 trên VPS phải mở trong firewall (ufw allow 6003/tcp).

#### Task Q-2: `nginx/aeroponics.conf.example` — Template sync

- **File:** `nginx/aeroponics.conf.example`
- **Thay đổi:** Copy nội dung chuẩn hóa từ Task Q-1 với comment hướng dẫn domain placeholder.

#### Task Q-3: `docker-compose.yml` — Expose port 6003

- **File:** `docker-compose.yml`
- **Service bi ảnh hưởng:** `aero-ui`, `aero-backend`, `nginx` (nếu có)

```yaml
# Thay đổi expose UI qua port 6003 (hoặc thêm service nginx nếu chưa có):
aero-ui:
  ports:
    - "6003:3000"   # hoặc để nginx xử lý, chỉ expose nginx

aero-backend:
  expose:
    - "3001"        # internal only

nginx:
  ports:
    - "6003:6003"   # reverse proxy chạy trên VPS port 6003
```

---

### TRACK R — E2E Validation Test Suite

#### Task R-1: `test/e2e/pump-control.spec.ts` — Full control loop test

- **File:** `test/e2e/pump-control.spec.ts`
- **Hàm mới:** `setupGatewayMock()`, `test('pump ON shows PENDING then FLOW_CONFIRMED')`

```typescript
// playwright.config.ts — Playwright E2E
import { test, expect } from '@playwright/test';

/**
 * E2E: Full control loop
 * UI → Backend REST → MQTT → Gateway → Node feedback → WS → UI
 * Mocks gateway at MQTT+WS boundary using a test MQTT client + broker.
 */
test('pump ON: no optimistic RUNNING; RUNNING only after FLOW_CONFIRMED', async ({ page }) => {
  await page.goto('/login');
  await page.fill('[name=username]', 'operator');
  await page.fill('[name=password]', 'secret');
  await page.click('button[type=submit]');
  await page.waitForSelector('text=Dashboard');

  // Wait for NodeCards + WS connected
  await page.waitForSelector('[data-testid=ws-banner-connected]');

  const card = page.locator('[data-node-id="4"]');
  await expect(card.locator('[data-testid=outcome-badge]')).toHaveText('Chờ lệnh');

  // Click BẬT BƠM (test backend replies ACCEPTED)
  await card.locator('button[data-action=on]').click();

  // Expected: PENDING (NOT RUNNING)
  await expect(card.locator('[data-testid=outcome-badge]')).toHaveText('Đang gửi lệnh');
  // Assert NO RUNNING glow prematurely
  await expect(card.locator('[data-testid=pump-glow]')).toHaveClass(/hidden/);

  // Simulate gateway: publish telemetry + FLOW_CONFIRMED via MQTT test client
  await publishGatewayTelemetry(4, { flowConfirmed: true, flowRateLpm: 1.23 });
  await page.waitForTimeout(1500);  // allow WS propagation

  // Expected: RUNNING badge after WS event
  await expect(card.locator('[data-testid=outcome-badge]')).toHaveText('Xác nhận dòng chảy');
  await expect(card.locator('[data-testid=pump-glow]')).toHaveClass(/relay-glow-active/);
});
```

#### Task R-2: `test/e2e/ws-reconnect.spec.ts` — WS connection resilience test

- **File:** `test/e2e/ws-reconnect.spec.ts`

```typescript
test('WebSocket reconnects with exponential backoff after server restart', async ({ page }) => {
  await page.goto('/dashboard');
  await expect(page.locator('[data-testid=ws-banner]')).toHaveText(/Connected/);

  // Restart backend (or close WS server) from test fixture
  await restartBackendFixture();

  // UI must show reconnecting, not crash
  await expect(page.locator('[data-testid=ws-banner]')).toHaveText(/Reconnecting/);
  await expect(page.locator('[data-testid=ws-banner]')).toHaveText(/Connected/, { timeout: 40000 });
});
```

---

## 4. TIÊU CHUẨN RÀ SOÁT CỨNG (Sprint 4)

### Rule S4-NOOPT-01: Zero Optimistic UI
```
PASS: NodeCard glow chỉ bật khi node.flowConfirmed === true (từ WS event)
PASS: Không dùng scheduleState hoặc local setState để hiển thị RUNNING
PASS: Nút bấm đổi outcome → PENDING, không bao giờ trực tiếp RUNNING
FAIL: Bắt gặp setState trực tiếp RUNNING trong component mà không qua applyFlowConfirmed
```

### Rule S4-WS-02: WS Event is Single Source of Truth
```
PASS: flowConfirmed chỉ được set true qua action applyFlowConfirmed() từ wsMessageHandler()
PASS: Không có code nào khác set flowConfirmed=true (REST response không được dùng)
FAIL: REST API response mang desired_state mà component dùng để hiển thị RUNNING
```

### Rule S4-WS-03: WS Bounded Reconnect — No Connection Storm
```
PASS: Reconnect dùng exponential backoff: base 1s × 1.5^n, max 30s
PASS: onclose/onerror không gọi connect() trực tiếp (phải qua scheduleReconnect)
PASS: Tối đa 1 pending retry timer tại mọi thời điểm
FAIL: retryCount tăng vô hạn / reconnect spam mỗi giây
FAIL: Client tự reconnect mà không dừng sau khi server down quá lâu
```

### Rule S4-NGINX-04: WebSocket Upgrade Correctness
```
PASS: location /ws và /socket.io/ đều có Upgrade + Connection "upgrade" headers
PASS: proxy_buffering off trên /socket.io/ (chống long-poll buffer delay)
PASS: listen 6003 và firewalled đúng (ufw allow 6003/tcp)
FAIL: Thiếu Connection "upgrade" → WS fallback về streaming/long-poll hỏng
```

### Rule S4-STALE-05: Staleness Threshold — Offline Node Not RUNNING
```
PASS: Node stale (>= STALE_THRESHOLD_MS) → override RUNNING display, hiển thị danger
PASS: Node offline không bao giờ được hiển thị RUNNING dù có outcome cũ FLOW_CONFIRMED
FAIL: Node mất kết nối nhưng NodeCard vẫn glow xanh RUNNING
```

### Rule S4-E2E-06: Full Loop Test Coverage
```
PASS: Ít nhất 1 test E2E validate pump control loop (PENDING → FLOW_CONFIRMED → RUNNING)
PASS: Ít nhất 1 test E2E validate WS reconnect behavior
FAIL: Share test logic chỉ kiểm tra UI visual, không kiểm tra trạng thái data thật
```

---

## 5. SYNCHRONIZATION REVIEW FIXES (Frontend Codebase ↔ Sprint 4 Plan)

> **Source:** Senior Dev flow synchronization review — 2026-09-25.
> **Purpose:** Bổ sung các task fixing mismatch giữa planning và codebase hiện tại. PHẢI hoàn thành trước khi Sprint 4 đánh PASS.

---

### 5.1 TRACK V — Critical Frontend Sync Fixes

#### Task V-1: useWebSocket Stub Removal — Exponential Backoff Implementation

- **Finding:** Finding #1 — `useWebSocket.ts` (line 26-33) returns `isConnected: true`, `resolveWsUrl: ''`, `calculateBackoffDelay: 0`. Comment: *"WebSocket has been replaced by MQTT + REST Polling"* — nhưng Sprint 4 design depends on native WS flow (`node_flow` → `applyFlowConfirmed` → `OutcomeBadge` RUNNING glow only after `flowConfirmed=true`).
- **Impact:** `applyFlowConfirmed` never runs, `node_flow` never reaches store, `flowConfirmed` stays `false`, `OutcomeBadge` never shows RUNNING glow, NodeCard metrics permanently `0.00 L/phút`. E2E test step 8 (`await publishGatewayTelemetry`) cannot succeed — there's no WS listener.
- **Fix:** Implement real native WS với exponential backoff (Task N-1 đã bổ sung trong Sprint 4). Ensure:
  1. `connect()` creates real `new WebSocket(resolveWsUrl())` connection
  2. `ws.onmessage` dispatches `wsMessageHandler(data)` to store
  3. `ws.onclose` → `scheduleReconnect()` (not direct `connect()`)
  4. `scheduleRecompute` uses `calculateBackoffDelay(retryCount)` with actual increasing backoff
  5. Backoff formula: `base(1s) × factor(1.5)^retryCount, capped at 30s`
  6. Max 1 pending retry timer tại mọi thời điểm
- **Affected file:** `aeroponics-ui/src/hooks/useWebSocket.ts`
- **Acceptance:** Task N-1 PASS; Rule S4-WS-03 PASS; WS reconnect time ≤ 40s total

#### Task V-2: Outcome Default to Neutral (Not PENDING) on Initial Load

- **Finding:** Finding #2 — `createDefaultNode` sets `outcome: 'PENDING'` (line 51 in `useNodeStore.ts`). `useNodeOutcome` fallback `?? 'PENDING'`. No path resets outcome to `null`/empty on initial load.
- **Impact:** Fresh dashboard loads with all 4 nodes showing *"Đang gửi lệnh"* badge — contradicts E2E expectation and creates confusing UX (users think a command is in-flight when none was sent).
- **Fix:**
  1. Change `createDefaultNode` to set `outcome: null` (or `''`) so initial badge is neutral
  2. Add `DEFAULT_NEUTRAL_STYLE` in `OUTCOME_CONFIG` for `null`/empty outcome → badge renders "Chờ lệnh" (Vietnamese neutral)
  3. Ensure initial load sets outcome to neutral before any WS event arrives
- **Affected file:** `aeroponics-ui/src/store/useNodeStore.ts`
- **Acceptance:** Fresh dashboard: badge = "Chờ lệnh" (neutral), not "Đang gửi lệnh"; E2E step 6 assertion PASS: badge text = 'Chờ lệnh'

#### Task V-3: Separate Admission ACK from RF_ACKED in Backend Logic

- **Finding:** Finding #7 — `handleCommandAckEvent` (mqtt-router.service.ts line 87-93): `acked = ['ACCEPTED','COMPLETED','OK','RF_ACKED'].includes(status)` → includes `ACCEPTED`. Then `handleNodeAck` sets command outcome `RF_ACKED`.
- **Impact:** UI badge immediately shows *"Đã nhận lệnh (RF)"* after command acceptance, before any RF transmission or gateway ACK. Misleads user about pump state and breaks the evidence pipeline that UI depends on (Sprint 4 `node_flow` → `flowConfirmed` never fires because command already marked `RF_ACKED`).
- **Fix:**
  1. `handleCommandAckEvent`: chỉ set `acked = true` khi `payload.acked === true` hoặc `status === 'RF_ACKED'` (không bao gồm ACCEPTED)
  2. `ACCEPTED` → emit separate `MQTT_EVENTS.COMMAND_ACCEPTED` (lifecycle admission step)
  3. `handleNodeAck`: đọc `payload.acked` chính; chỉ fall back to `payload.status` cho tương thích ngược với comment rõ ràng
  4. Cập nhật TypeScript types: tách `CommandAcceptedEvent` và `CommandAckEvent` rõ ràng
- **Affected file:** `aeroponics-backend/src/mqtt/mqtt-router.service.ts`
- **Acceptance:** Rule S3-MQTT-09 PASS; UI badge không show "RF" sau khi chỉ nhận ACCEPTED

#### Task V-4: Fix Retry Backoff Closure — `scheduleReconnect` Deps

- **Finding:** Finding #8 — `connect` closure (useCallback `[]` deps) captures `scheduleReconnect` từ first render. `scheduleReconnect` có deps `[connect, retryCount]`. Vì `connect` stable, closure holds **first-render** `scheduleReconnect` với `retryCount=0` mãi mãi → delay luôn `1000 * 1.5^0 = 1s`. `onclose` → `scheduleReconnect()` → reconnect mỗi 1s mãi mãi.
- **Impact:** Violates S4-WS-03, causes connection storm, drains battery trên mobile, breaches Sprint 4 reliability rule.
- **Fix:** Restructure `connect`/`scheduleReconnect` so backoff genuinely tăng:
  1. Pass `retryCount` làm dependency duy nhất cho `scheduleReconnect`
  2. **KHÔNG** capture `scheduleReconnect` trong `connect` closure — thay vào đó truyền `scheduleReconnect` làm callback hoặc dùng `useRef` cho `retryCount`
  3. Đảm bảo `scheduleReconnect` đọc `retryCount` từ state/current context thay vì closure capture
- **Affected file:** `aeroponics-ui/src/hooks/useWebSocket.ts`
- **Acceptance:** Rule S4-WS-03 PASS; backoff delays: 1s → 1.5s → 2.25s → ... → 30s; `retryCount` tăng theo đúng

#### Task V-5: Add `isStale` Check to `isNodeRunning`

- **Finding:** Finding #9 — `isNodeRunning` (types.ts line 433-436) = `node.flowConfirmed && node.outcome === 'FLOW_CONFIRMED'` — **does not check `isStale`**. Old NodeCard glow (line 27-30): `node.outcome === 'FLOW_CONFIRMED' || node.flowConfirmed || node.scheduleState === 'SPRAYING'` — also no stale check.
- **Impact:** Nếu node mất kết nối nhưng `flowConfirmed` đã từng true, UI tiếp tục show RUNNING glow vô tận — violates S4-STALE-05 và tạo state false-positive.
- **Fix:**
  1. Update `isNodeRunning` to: `node.flowConfirmed && node.outcome === 'FLOW_CONFIRMED' && !node.isStale`
  2. Update NodeCard glow condition tương tự: chỉ `isRunning` thay vì r condition cũ
  3. Đảm bảo `isStale` set đúng từ `STALENESS_ALERT` WS event (Task N-2)
- **Affected file:** `aeroponics-ui/src/lib/types.ts` và `aeroponics-ui/src/components/dashboard/NodeCard.tsx`
- **Acceptance:** Rule S4-STALE-05 PASS; stale node KHÔNG glow RUNNING

#### Task V-6: Clear Outcome After Pump OFF

- **Finding:** Finding #10 — `applyFlowConfirmed` (useNodeStore.ts line 373-390): sets `flowConfirmed` and `outcome: flowConfirmed ? 'FLOW_CONFIRMED' : current.outcome`. Nếu `flowConfirmed=false`, outcome stays previous value (`'FLOW_CONFIRMED'` nếu đã true trước). Không path sets outcome to neutral sau OFF.
- **Impact:** Sau khi pump OFF completes, `OutcomeBadge` vẫn show *"Xác nhận dòng chảy"* (FLOW_CONFIRMED) và `NodeCard` glow có thể stay (old code). E2E step 9 assertion fails.
- **Fix:**
  1. `applyFlowConfirmed`: khi `flowConfirmed = false` → set `outcome: 'PENDING'` (clear previous RUNNING state)
  2. Sau khi pump OFF hoàn thành → backend broadcast `pump.command_update` outcome `PENDING` trên `pump.command.sent`; UI listen và set outcome to `'PENDING'` để clear RUNNING state
  3. Hoặc: `updateOutcome` action chấp nhận `outcome: 'PENDING'` để reset neutral state
- **Affected file:** `aeroponics-ui/src/store/useNodeStore.ts` — method `applyFlowConfirmed`
- **Acceptance:** E2E step 9 PASS: sau khi TẮT BƠM → badge = "Chờ lệnh"; không còn "Xác nhận dòng chảy"

---

### 5.2 TRACK W — Moderate Frontend Sync Fixes

#### Task W-1: Remove `scheduleState === 'SPRAYING'` from Glow Condition (P-1 Already)

- **Finding:** Finding #12 — Plan P-1 explicitly removes `node.scheduleState === 'SPRAYING'` từ glow condition. Code NodeCard line 29 vẫn include `node.scheduleState === 'SPRAYING'` → glow có thể trigger từ schedule statealone, không chỉ flow evidence. Minor UX inconsistency nhưng không blocking.
- **Status:** Đã có trong plan — chỉ cần developer implement khi code Sprint 4.

#### Task W-2: OutcomeBadge `REJECTED` Label Not Configured

- **Finding:** Finding #15 — `getOutcomeConfig` falls back to raw `outcome` string cho unrecognized values → badge show English `"REJECTED"`.
- **Impact:** Sprint expects Vietnamese toast trên error. Minor polish issue.
- **Fix:** Cấu hình `OUTCOME_CONFIG` để map outcome values sang nhãn tiếng Việt:
  - `'PENDING'` → "Đang gửi lệnh"
  - `'FLOW_CONFIRMED'` → "Xác nhận dòng chảy"
  - `'RF_ACKED'` → "Đã nhận lệnh (RF)"
  - `'REJECTED'` → "Đã từ chối"
- **Affected file:** `aeroponics-ui/src/components/common/OutcomeBadge.tsx`
- **Acceptance:** Badge hiển thị tiếng Việt cho mọi outcome value

#### Task W-3: Nginx socket.io Config but No Socket.IO Server

- **Finding:** Finding #18 — Plan §2.3 & §1.1 constants mention `/socket.io/` path. Backend `EventsGateway` là native WS tại `/ws` only (line 39: `@WebSocketGateway({ path: '/ws' })`). No Socket.IO handling. Nếu UI connect đến `/socket.io/` expecting engine.io → handshake fails → no events.
- **Note:** Đây là planning aspiration — codebase dùng native WS duy nhất. Nên:
  - Giữ `/socket.io/` path trong Nginx config nhưng đánh dấu **DOCUMENTED AS RETRO COMPATIBILITY ONLY**
  - Hoặc: Thêm native Socket.IO layer nếu cần, nhưng KHÔNG bắt buộc cho Sprint 4 WS flow

---

### 5.3 Additional Hardening Rules (Sprint 4)

#### Rule S4-WS-04: Outcome Neutral Initial State
```
PASS: Initial badge = "Chờ lệnh" (neutral), không phải "Đang gửi lệnh"
PASS: createDefaultNode sets outcome = null (không PENDING)
FAIL: Fresh dashboard: tất cả NodeCard hiển thị "Đang gửi lệnh"
```

#### Rule S4-WS-05: ACK/RF Separation Integrity
```
PASS: ACCEPTED event KHÔNG set outcome = RF_ACKED
PASS: RF_ACKED chỉ set khi payload.acked === true hoặc status === 'RF_ACKED'
PASS: UI badge không show "Đã nhận lệnh (RF)" trước khi RF ACK thực sự
FAIL: outcome set to RF_ACKED sau ACCEPTED đơn lẻ
```

#### Rule S4-WS-06: Backoff Monotonic Increase
```
PASS: delay(retry=n) ≥ delay(retry=n-1) ∀ n ≥ 0
PASS: delay(retry=0) = 1s, delay(retry=1) = 1.5s, delay(retry=2) = 2.25s, ...
PASS: max 30s cap enforced
FAIL: delay tetap 1s bất kể retryCount bao nhiêu
```

#### Rule S4-STALE-05: Staleness Override — Offline Not RUNNING
```
PASS: isNodeRunning = flowConfirmed && outcome === 'FLOW_CONFIRMED' && !isStale
PASS: NodeCard glow chỉ isRunning, KHÔJ scheduleState/outcome cũ
PASS: Stale node (>= STALE_THRESHOLD_MS) → danger display override
FAIL: Stale node vẫn glow RUNNING xanh mãi
```

#### Rule S4-E2E-07: Outcome Clearing After Pump OFF
```
PASS: Sau khi TẮT BƠM → outcome = PENDING → badge = "Chờ lệnh"
PASS: Không có intermediate RUNNING state giữa step ON click và OFF click
FAIL: Badge vẫn "Xác nhận dòng chảy" sau khi pump OFF
```

---

*Sprint 4 Planning — Dashboard State Synchronization & E2E Validation. Golden Baseline: e2e-critical-sequence-flows v2.0.*