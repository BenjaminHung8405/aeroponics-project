# Sprint 4: Next.js Dashboard UI — JWT Auth + Mobile-First + Remote Access

> **Phụ thuộc:** Sprint 3 hoàn thành — NestJS Backend + WebSocket `/ws` đang chạy, TimescaleDB có dữ liệu thực.
> **Output bàn giao:** Next.js 15 App Router Dashboard, JWT-protected, mobile-first, truy cập qua domain công ty qua Nginx reverse proxy.
> **Hosting:** Máy chủ công ty — `docker compose up` (4 services). Không cần lo cấu hình hạ tầng.

---

## Section 1: Scope & Objectives

### 1.1 Deployment & Nginx Routing

```
[Browser / Mobile]  →  https://domain.com/
          │
          ▼
[Nginx Reverse Proxy — Company Config]
  location /         →  http://aero-ui:3000      (Next.js App)
  location /api/     →  http://aero-backend:3001  (NestJS REST)
  location /ws       →  http://aero-backend:3001  (WebSocket upgrade)
```

- `NEXT_PUBLIC_API_URL` env var: **blank = `/api`** (same-origin via Nginx). Override đến `http://localhost:3001` cho local dev.
- `NEXT_PUBLIC_WS_URL` env var: **blank = `/ws`** (same-origin via Nginx). Override đến `ws://localhost:3001` cho local dev.
- Next.js `output: 'standalone'` cho Docker multi-stage build.

### 1.2 Auth Requirements

- **Login screen:** `/login` page — username + password form, public route.
- **JWT storage:** `httpOnly` cookie — XSS-proof, phù hợp public internet.
- **Middleware guard:** `middleware.ts` intercept `/dashboard/*`, redirect về `/login` nếu cookie absent.
- **Auto-logout:** 401 response từ NestJS API → `useAuth` hook trigger logout → redirect `/login`.

### 1.3 Dashboard Features

- **Season Panel:** Tên mùa vụ, ngày bắt đầu, số ngày đã chạy. Nút "Kết thúc vụ mùa" (confirmation modal). CTA tạo mùa vụ mới nếu không có active season.
- **4 Group Cards:** Treatment name, version, phase DAY/NIGHT (Lucide icon), phase countdown timer, node count, ACTIVE/UNASSIGNED badge. Modal gán treatment.
- **4 Node Cards:** Staleness dot (green/amber/red + pulse), outcome badge per enum, flow rate LPM, relay glow khi FLOW_CONFIRMED, Command Log modal.
- **Treatment Panel:** Danh sách treatment + version params. Clone, Publish, Assign to Group.
- **On-demand Measurement:** Nút "Đo ngay" (Lucide Thermometer) → `POST /api/measurement/trigger` → hiển thị 7 values (pH, EC, TDS, Temp, Salinity, ORP, Turbidity) + bảng lịch sử.
- **WS Resiliency:** Auto-reconnect với exponential backoff. Disconnect banner.

---

## Section 2: Architecture & Data Flow

### 2.1 Next.js App Router Structure

```
src/app/
├── layout.tsx                       (Root layout: Google Fonts, metadata, providers)
├── page.tsx                         (Redirect → /login hoặc /dashboard)
├── providers.tsx                    (QueryClientProvider)
│
├── (auth)/
│   └── login/page.tsx               (Public: JWT login form)
│
├── (dashboard)/
│   ├── layout.tsx                   (Protected layout: header, WsBanner)
│   └── page.tsx                     (Dashboard: tất cả panels)
│
└── api/
    ├── set-token/route.ts            (POST: set httpOnly cookie)
    └── clear-token/route.ts          (POST: clear httpOnly cookie — logout)
```

### 2.2 JWT Auth Flow

```
[User truy cập domain.com/]
          │
          ▼
middleware.ts: check cookie "access_token"
          │
      valid? ──YES──► render /dashboard/page.tsx
          │
         NO
          │
          ▼
redirect → /login
          │
    [LoginForm.tsx]
    POST /api/auth/login  { username, password }
          │
          ▼
    NestJS AuthController returns { access_token }
          │
          ▼
    POST /api/set-token  { token }
    Next.js Route Handler:
    Set-Cookie: access_token=...; HttpOnly; Secure; SameSite=Strict; Max-Age=86400
          │
          ▼
    redirect → /dashboard
```

### 2.3 WebSocket Data Flow

```
[NestJS EventsGateway :3001/ws]
    native WS push { event, data }
          │
          ▼
[useWebSocket.ts hook]
  connect: WS_URL = NEXT_PUBLIC_WS_URL || derive from window.location
  onmessage → parse JSON → dispatch
          │
  ┌───────┴────────────────────────┐
  │                                │
  ▼                                ▼
useNodeStore (Zustand)        useGroupStore (Zustand)
  node_telemetry               group_status
  node_flow
  pump_command_update
  staleness_alert
          │                        │
          ▼                        ▼
    NodeCard.tsx             GroupCard.tsx
    (React re-render)        (React re-render)
```

### 2.4 Environment Variables

```bash
# aeroponics-ui/.env.example
NEXT_PUBLIC_API_URL=      # blank = same-origin /api via Nginx
NEXT_PUBLIC_WS_URL=       # blank = same-origin /ws via Nginx

# Local development (.env.local — NOT committed):
# NEXT_PUBLIC_API_URL=http://localhost:3001
# NEXT_PUBLIC_WS_URL=ws://localhost:3001

# aeroponics-backend/.env.example (additions)
CORS_ORIGIN=https://your-domain.com
```

---

## Section 3: Detailed Task Breakdown

### TRACK A — Project Setup & Infrastructure

#### Task A-1: Khởi tạo Next.js 15 Project

**Files tạo mới:** `aeroponics-ui/package.json`, `aeroponics-ui/next.config.ts`, `aeroponics-ui/tsconfig.json`, `aeroponics-ui/.env.example`

**Dependencies:**
```json
{
  "dependencies": {
    "next": "^15.x",
    "react": "^19.x",
    "react-dom": "^19.x",
    "zustand": "^5.x",
    "@tanstack/react-query": "^5.x",
    "lucide-react": "^0.x"
  },
  "devDependencies": {
    "typescript": "^5.x",
    "@types/node": "^22.x",
    "@types/react": "^19.x",
    "tailwindcss": "^3.x",
    "postcss": "^8.x",
    "autoprefixer": "^10.x"
  }
}
```

**`next.config.ts`:**
```typescript
import type { NextConfig } from 'next';
const nextConfig: NextConfig = { output: 'standalone' };
export default nextConfig;
```

**Hard Rules:**
- `NEXT_PUBLIC_API_URL` và `NEXT_PUBLIC_WS_URL` phải có trong `.env.example` với giá trị rỗng (fallback same-origin).
- **ZERO hardcode** `localhost:3001` trong bất kỳ component/hook nào.

---

#### Task A-2: Tailwind Config với Design System Tokens (MASTER.md)

**File:** `aeroponics-ui/tailwind.config.ts`

Port 11 core tokens MASTER.md sang Tailwind `colors` và `fontFamily`. Thêm `keyframes: 'pulse-emerald'` cho node active glow animation.

---

#### Task A-3: Dockerfile (Multi-stage Standalone)

**File:** `aeroponics-ui/Dockerfile` — 3 stages: `deps` (npm ci) → `builder` (npm run build) → `runner` (standalone, non-root user, port 3000).

---

#### Task A-4: docker-compose.yml + Nginx Template

**File sửa:** `docker-compose.yml` — thêm service `aeroponics-ui` (build từ `./aeroponics-ui`, port 3000, depends_on aeroponics-backend). Tổng cộng **4 services**: timescaledb + mosquitto + aero-backend + aero-ui.

**File tạo mới:** `nginx/aeroponics.conf.example` — template Nginx path routing:
- `location /ws` (trước) → WebSocket upgrade headers → `http://aero-backend:3001`
- `location /api/` → `http://aero-backend:3001`
- `location /` (catch-all) → `http://aero-ui:3000`

---

### TRACK B — Auth Layer

#### Task B-1: Login Page & LoginForm Component

**Files tạo mới:** `src/app/(auth)/login/page.tsx`, `src/components/auth/LoginForm.tsx`

- Fields: username + password. Submit: POST `/api/auth/login` → POST `/api/set-token` → `router.push('/dashboard')`.
- Error state: message từ server. Loading: disable button + `<Loader2 className="animate-spin" />`.
- Touch target: submit button `min-h-[48px] w-full`. Mobile-first, Dark OLED background.
- **Zero password leak:** không log, không store, không localStorage.

---

#### Task B-2: Next.js Middleware (Auth Guard)

**File:** `src/middleware.ts`

```typescript
import { NextRequest, NextResponse } from 'next/server';

export function middleware(request: NextRequest) {
  const token = request.cookies.get('access_token')?.value;
  const { pathname } = request.nextUrl;
  if (pathname.startsWith('/dashboard') && !token) {
    const url = new URL('/login', request.url);
    url.searchParams.set('from', pathname);
    return NextResponse.redirect(url);
  }
  if (pathname.startsWith('/login') && token) {
    return NextResponse.redirect(new URL('/dashboard', request.url));
  }
  return NextResponse.next();
}

export const config = { matcher: ['/dashboard/:path*', '/login'] };
```

**Hard Rule B-AUTH-01:** Middleware chỉ kiểm tra cookie existence — không decode/verify JWT payload. Verification xảy ra ở NestJS `JwtAuthGuard` (401 → client-side logout).

---

#### Task B-3: Next.js API Routes (Cookie Management)

**File tạo mới:** `src/app/api/set-token/route.ts` — set `httpOnly; Secure; SameSite=Strict; Max-Age=86400`.
**File tạo mới:** `src/app/api/clear-token/route.ts` — `response.cookies.delete('access_token')`.

---

#### Task B-4: useAuth Hook

**File:** `src/hooks/useAuth.ts` — `login()`: POST auth + set cookie. `logout()`: clear cookie + `window.location.href = '/login'`.

---

#### Task B-5: NestJS CORS Update

**File sửa:** `aeroponics-backend/src/main.ts` — `app.enableCors({ origin: process.env.CORS_ORIGIN, credentials: true })`.
**File sửa:** `aeroponics-backend/.env.example` — thêm `CORS_ORIGIN=`.

---

### TRACK C — Shared Infrastructure

#### Task C-1: API Client (`src/lib/api.ts`)

`apiFetch<T>(path, options)`: fetch `${NEXT_PUBLIC_API_URL || '/api'}${path}`, `credentials: 'include'`. On 401: clear cookie + redirect `/login`.

#### Task C-2: WebSocket Hook (`src/hooks/useWebSocket.ts`)

Native WS, derive URL từ `NEXT_PUBLIC_WS_URL` hoặc `window.location.protocol`. Exponential backoff max 30s. Dispatch WS events tới Zustand stores. Expose `{ isConnected, reconnectNow }`.

#### Task C-3: Zustand Stores

`src/store/useNodeStore.ts` — 4 nodes (`id`, `name`, `lastSeenAt`, `pumpState`, `flowLpm`, `outcome`, `deliveredVolumeL`). Actions: `initNodes`, `updateNode`.

`src/store/useGroupStore.ts` — 4 groups (`groupId`, `status`, `phase`, `treatmentName`, `nodeIds`, `nextTransitionAt`). Actions: `initGroups`, `updateGroup`.

#### Task C-4: TanStack Query Hooks

`useSeason`, `useGroups`, `useNodes`, `useTreatments`, `useMeasurement` — mỗi hook dùng `useQuery` với `staleTime: 30_000`. Query key chuẩn.

#### Task C-5: Types & Constants (`src/lib/types.ts`, `src/lib/constants.ts`)

`OUTCOME_CONFIG = Object.freeze({...})` — màu per outcome (FLOW_CONFIRMED → primary, RF_ACKED → indigo, FAULT_* → danger, TIMEOUT → amber, PENDING → subtle).
`STALE_THRESHOLD_MS = 120_000`, `STALE_AMBER_MS = 60_000`, `WS_EVENTS = Object.freeze({...})`.

---

### TRACK D — UI Components

#### Task D-1: SeasonPanel (`src/components/season/SeasonPanel.tsx`)

Active state: tên, ngày bắt đầu, số ngày. Nút "Kết thúc" (48px, confirmation). Empty state: form tạo mùa vụ mới (CTA).

#### Task D-2: GroupCard + GroupGrid

Phase: `<Sun />` (text-accent-amber) / `<Moon />` (text-accent-indigo). **Zero emoji.** Countdown: `font-mono tabular-nums text-2xl font-bold`. Assign modal (filter PUBLISHED versions).

#### Task D-3: NodeCard + NodeGrid

Staleness dot: `<60s`→primary, `60-120s`→amber, `≥120s`→danger+animate-pulse. Outcome badge: `<OutcomeBadge />`. Flow: `font-mono tabular-nums`. Active glow: `animate-pulse-emerald` khi FLOW_CONFIRMED.

#### Task D-4: TreatmentPanel

List treatments + version params. Assign to Group modal.

#### Task D-5: MeasurementPanel

"Đo ngay" (Thermometer icon, 48px) → POST trigger. 429: disable + countdown. 7 values `font-mono`. History table `overflow-x-auto`.

#### Task D-6: Common Components

`WsBanner.tsx` (bg-danger, auto-hide), `OutcomeBadge.tsx`, `StalenessIndicator.tsx`.

---

### TRACK E — Design System Integration

#### Task E-1: `src/app/globals.css`

`@import` Google Fonts Outfit + JetBrains Mono. `:root {}` với 11 core tokens MASTER.md + semantic derivatives. `.glass-card` class (backdrop-blur-16, border, border-radius 16px, shadow).

#### Task E-2: Mobile-First Grid

`grid-cols-1 sm:grid-cols-2 lg:grid-cols-4`. Primary: `min-h-[48px]`. Secondary: `min-h-[44px]`. iOS: `pb-[env(safe-area-inset-bottom)]`.

#### Task E-3: Iconography (Lucide React)

`lucide-react` package. Icons: Droplets, Timer, Activity, Sun, Moon, Zap, Thermometer, ShieldAlert, RefreshCw, Sliders, CheckCircle2, AlertTriangle, Loader2, LogOut. **Zero emoji absolutely.**

#### Task E-4: WCAG AAA Contrast

`#F0FDF4` trên `#07130E` → 16.8:1 (vượt AAA). Verify DevTools accessibility audit.

---

### TRACK F — QA & Integration

#### Task F-1: E2E Integration Test với backend thật

Login flow → dashboard load → WS update → Tuya trigger.

#### Task F-2: JWT Auth Flow Test

No cookie → redirect `/login`. Post login → `/dashboard`. 401 → auto logout. `localStorage.getItem('access_token')` = `null`.

#### Task F-3: Mobile Responsiveness

375px, 390px, 640px, 768px, 1024px, 1440px. No horizontal overflow. Touch targets ≥44px.

#### Task F-4: NestJS E2E Test Update

Xóa test case `GET /` → 200 HTML. Xóa `ServeStaticModule` từ `app.module.ts`. Xóa `getIndex()` từ `app.controller.ts`.

---

## Section 4: Hard QA Rules (Sprint 4 — Next.js Edition)

| Rule ID | PASS khi | Severity |
|---|---|---|
| **S4-AUTH-01** | `/dashboard/*` redirect về `/login` khi không có `access_token` cookie | 🔴 BLOCKER |
| **S4-AUTH-02** | JWT trong `httpOnly` cookie — `localStorage.getItem('access_token')` PHẢI trả `null` | 🔴 BLOCKER |
| **S4-AUTH-03** | 401 từ NestJS API → auto-logout + redirect `/login` không crash | 🔴 BLOCKER |
| **S4-WS-04** | Native WebSocket (không Socket.IO); live updates không trigger `window.location.reload()` | 🔴 BLOCKER |
| **S4-API-05** | `NEXT_PUBLIC_API_URL` fallback `/api`; `rg 'localhost:3001' src/` = 0 match | 🔴 BLOCKER |
| **S4-NULL-06** | Null-safe rendering — không crash khi API trả `null`; missing values hiển thị `'—'` | 🔴 BLOCKER |
| **S4-NO-RELAY-07** | `rg '/api/relay\|relay_update' src/` = 0 match | 🔴 BLOCKER |
| **S4-OUTCOME-08** | Node cards hiển thị `<OutcomeBadge>` với màu per `OUTCOME_CONFIG` | 🔴 BLOCKER |
| **S4-STALENESS-09** | Staleness dot: `<60s`=primary, `60–120s`=amber, `≥120s`=danger+animate-pulse | 🔴 BLOCKER |
| **S4-SEASON-10** | Dashboard show active season; không có → CTA "Tạo mùa vụ mới" (không blank page) | 🔴 BLOCKER |
| **S4-ON-DEMAND-11** | "Đo ngay" gọi `POST /api/measurement/trigger`; không có `setInterval` polling | 🔴 BLOCKER |
| **S4-DS-FONT-12** | Outfit (UI) + JetBrains Mono + `tabular-nums` (metrics/timers) | 🔴 BLOCKER |
| **S4-DS-COLOR-13** | 11 CSS variables MASTER.md trong `:root {}`; `--color-background: #07130E` | 🔴 BLOCKER |
| **S4-DS-ICON-14** | Zero emoji; tất cả icons là `lucide-react` components | 🔴 BLOCKER |
| **S4-DS-TOUCH-15** | Primary buttons `min-h-[48px]`; secondary `min-h-[44px]`; `active:scale-95` | 🔴 BLOCKER |
| **S4-DS-CONTRAST-16** | WCAG AAA ≥7:1 cho `--color-text` trên `--color-background` | 🔴 BLOCKER |
| **S4-DS-MOBILE-17** | Mobile-first: 375px–1440px, no horizontal overflow, no layout shift | 🔴 BLOCKER |
| **S4-BUILD-18** | `npm run build` + `npx tsc --noEmit` PASS 0 errors | 🔴 BLOCKER |
| **S4-BANNER-19** | WS disconnect banner `bg-danger`; auto-hide khi reconnect; backoff max 30s | 🟠 CRITICAL |
| **S4-DOCKER-20** | `docker compose build aeroponics-ui` PASS; 4 services healthy | 🟠 CRITICAL |

---

*Sprint 4 Planning (Next.js Dashboard UI — JWT Auth, Mobile-First, Remote Access via Company Domain) — Updated 2026-09-13*
