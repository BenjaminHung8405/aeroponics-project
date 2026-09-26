/**
 * Shared E2E Test Helpers for Aeroponics Smart Farm
 *
 * Provides a controllable WebSocket fixture, API route mocks, auth helpers,
 * and selector/assertion utilities for validating the full pump control loop.
 *
 * Rules enforced by the specs using this module:
 *  - S4-E2E-06: Full loop + reconnect test coverage
 *  - S4-NOOPT-01: Zero optimistic UI (no intermediate RUNNING state)
 *  - S4-WS-02:  RUNNING only from server-authoritative WS events
 *  - S4-WS-03:  WS bounded reconnect, no connection storm
 *  - S4-WS-04:  Initial badge is the neutral "Chờ lệnh"
 *  - S4-STALE-05: Stale node never renders RUNNING glow
 *
 * The fixture replaces `window.WebSocket` with a deterministic mock before the
 * app bundle runs. The rest of the stack (Next.js app, Zustand store, NodeCard,
 * OutcomeBadge, PumpControl) is the real production code under test.
 */

import { type Page, type BrowserContext, expect } from '@playwright/test';

// ============================================================
// WebSocket fixture
// ============================================================

export type WsFixtureMode = 'open' | 'fail' | 'recover';

/**
 * Installs the WebSocket fixture. MUST be called before `page.goto()` so the
 * override is in place before the app bundle executes.
 *
 * Exposed API on `window.__wsTest`:
 *   mode                      current fixture mode ('open' | 'fail' | 'recover')
 *   setMode(mode)             switch behaviour for sockets opened afterwards
 *   connectAttempts:number[]  timestamps of every `new WebSocket()` call
 *   closeEvents:number[]      timestamps of every close
 *   isConnected:boolean       last known open state
 *   sendEvent(msg)            deliver a server broadcast to the app handler
 *   reset()                   clear all recorded telemetry
 */
export async function injectWebSocketMock(page: Page): Promise<void> {
  await page.addInitScript(() => {
    type Mode = 'open' | 'fail' | 'recover';

    const api = {
      mode: 'open' as Mode,
      instance: null as any,
      connectAttempts: [] as number[],
      closeEvents: [] as number[],
      isConnected: false,

      setMode(mode: Mode) {
        api.mode = mode;
      },
      getMode(): Mode {
        return api.mode;
      },
      getConnectionCount(): number {
        return api.connectAttempts.length;
      },
      getAttempts(): number[] {
        return api.connectAttempts.slice();
      },
      getCloseEvents(): number[] {
        return api.closeEvents.slice();
      },
      reset() {
        api.connectAttempts = [];
        api.closeEvents = [];
        api.isConnected = false;
      },
      /**
       * Deliver a server broadcast exactly as `events.gateway.ts` would.
       * Routing through the app's real `onmessage` handler keeps the production
       * `wsMessageHandler` dispatcher in the tested path.
       */
      sendEvent(msg: { event: string; data: unknown; timestamp?: string }) {
        const ws = api.instance;
        if (!ws || ws.readyState !== 1 /* OPEN */) return false;
        const payload = JSON.stringify({
          event: msg.event,
          data: msg.data,
          timestamp: msg.timestamp ?? new Date().toISOString(),
        });
        if (typeof ws.onmessage === 'function') {
          ws.onmessage(new MessageEvent('message', { data: payload }));
          return true;
        }
        return false;
      },

      /**
       * Simulate a server-initiated close of the live connection (e.g. the
       * backend restarting). This drives the app's real `onclose` handler,
       * which must call `scheduleReconnect()` rather than `connect()` directly.
       * Must be called after `setMode('fail')` so the resulting reconnect
       * attempts are refused until the server is marked as back.
       */
      serverClose() {
        const ws = api.instance;
        if (!ws) return false;
        ws.close(1006, 'server restart');
        return true;
      },
    };

    (window as any).__wsTest = api;

    /**
     * Deterministic offline-first WebSocket stand-in. Never touches the network,
     * so reconnect timing assertions are exact and repeatable.
     */
    class WebSocketFixture {
      static readonly CONNECTING = 0;
      static readonly OPEN = 1;
      static readonly CLOSING = 2;
      static readonly CLOSED = 3;

      readonly CONNECTING = 0;
      readonly OPEN = 1;
      readonly CLOSING = 2;
      readonly CLOSED = 3;

      url: string;
      readyState: number = 0;
      onopen: ((ev: Event) => void) | null = null;
      onmessage: ((ev: MessageEvent) => void) | null = null;
      onerror: ((ev: Event) => void) | null = null;
      onclose: ((ev: CloseEvent) => void) | null = null;

      constructor(url: string | URL) {
        this.url = String(url);
        api.instance = this;
        api.connectAttempts.push(Date.now());

        // Async handshake, mirroring a real socket. In 'fail' mode the handshake
        // is refused, which drives the app into onclose -> scheduleReconnect().
        setTimeout(() => {
          if (api.mode === 'fail') {
            this._fail();
          } else {
            this.readyState = 1;
            api.isConnected = true;
            this.onopen?.(new Event('open'));
          }
        }, 0);
      }

      /** Simulated connection refusal: onerror then onclose. */
      private _fail() {
        if (this.readyState === 3) return;
        this.readyState = 3;
        api.isConnected = false;
        api.closeEvents.push(Date.now());
        this.onerror?.(new Event('error'));
        this.onclose?.(new CloseEvent('close', { code: 1006, reason: 'connection refused', wasClean: false }));
      }

      close(code = 1000, reason = 'normal closure') {
        if (this.readyState === 3) return;
        this.readyState = 3;
        api.isConnected = false;
        api.closeEvents.push(Date.now());
        this.onclose?.(new CloseEvent('close', { code, reason, wasClean: code === 1000 }));
      }

      send() {
        /* outbound frames are not part of these specs */
      }
    }

    (window as any).WebSocket = WebSocketFixture as any;
  });
}

// ============================================================
// API route mocks
// ============================================================

/**
 * Stubs the backend REST tier so the dashboard renders deterministically
 * without a live NestJS + TimescaleDB stack.
 *
 * `/api/set-token` and `/api/clear-token` are intentionally left untouched:
 * they are Next.js Route Handlers that own the httpOnly cookie, and the specs
 * depend on them setting a real cookie.
 */
export async function mockDashboardApi(page: Page): Promise<void> {
  // Playwright evaluates routes in REVERSE registration order: the most
  // recently registered matching handler wins. The catch-all is therefore
  // registered FIRST so the specific handlers below take precedence.
  // `/api/set-token` and `/api/clear-token` are Next.js Route Handlers that own
  // the httpOnly cookie and must never be intercepted.
  await page.route('**/api/**', (route) => {
    const { pathname } = new URL(route.request().url());
    if (pathname.endsWith('/set-token') || pathname.endsWith('/clear-token')) {
      return route.continue();
    }
    return route.fulfill({
      status: 200,
      contentType: 'application/json',
      body: JSON.stringify({}),
    });
  });

  // Auth login — the specs prefer cookie injection, this covers form-based login.
  await page.route('**/api/auth/login', (route) =>
    route.request().method() === 'POST'
      ? route.fulfill({
          status: 200,
          contentType: 'application/json',
          body: JSON.stringify({ access_token: 'e2e-jwt-token', token_type: 'Bearer' }),
        })
      : route.continue(),
  );

  // Node list — 4 calibrated production nodes (IDs 4-7), all with default state.
  // NOTE: Must be registered BEFORE the catch-all routes.
  await page.route('**/api/node', (route) => {
    const url = new URL(route.request().url());
    const nodeIds = [4, 5, 6, 7];
    const nodes = nodeIds.map((id) => ({
      node_id: id,
      display_name: `Node #${id}`,
      cached_group_id: 1,
      sensor_serial: `RF-00${id}`,
      calibration_status: 'CALIBRATED',
      schedule_state: 'IDLE',
      override_state: 'NONE',
      last_boot_session_id: null,
      last_seen_at: new Date(0).toISOString(),
      health_status: 'OK',
      is_stale: false,
      stale_for_ms: 0,
      rf_protocol: 'SUBGHZ_433',
      last_scan_id: null,
      last_rf_rtt_ms: null,
      last_discovered_at: null,
      discovery_status: null,
      active_calibration: {
        id: 1,
        version_num: 1,
        pulses_per_litre: '100.000',
        reference_volume_ml: 100,
        calibrated_at: new Date(0).toISOString(),
        calibrated_by: 'e2e',
      },
    }));
    return route.fulfill({
      status: 200,
      contentType: 'application/json',
      body: JSON.stringify(nodes),
    });
  });

  await page.route(/\/api\/node\/[4-7]$/, (route) => {
    const url = new URL(route.request().url());
    const id = Number(url.pathname.match(/\/node\/(\d+)/)?.[1] ?? 4);
    return route.fulfill({
      status: 200,
      contentType: 'application/json',
      body: JSON.stringify({
        node_id: id,
        display_name: `Node #${id}`,
        cached_group_id: 1,
        sensor_serial: `RF-00${id}`,
        calibration_status: 'CALIBRATED',
        schedule_state: 'IDLE',
        override_state: 'NONE',
        last_seen_at: new Date(0).toISOString(),
        health_status: 'OK',
        is_stale: false,
        stale_for_ms: 0,
        last_rf_rtt_ms: null,
        last_discovered_at: null,
        discovery_status: null,
        active_calibration: {
          id: 1,
          version_num: 1,
          pulses_per_litre: '100.000',
          reference_volume_ml: 100,
          calibrated_at: new Date(0).toISOString(),
          calibrated_by: 'e2e',
        },
      }),
    });
  });

  await page.route('**/api/group', (route) =>
    route.fulfill({
      status: 200,
      contentType: 'application/json',
      body: JSON.stringify({ items: [], total: 0 }),
    }),
  );

  await page.route('**/api/group/*', (route) =>
    route.fulfill({
      status: 200,
      contentType: 'application/json',
      body: JSON.stringify({ items: [], total: 0 }),
    }),
  );

  await page.route('**/api/season/active', (route) =>
    route.fulfill({
      status: 200,
      contentType: 'application/json',
      body: JSON.stringify(null),
    }),
  );

  await page.route('**/api/treatment**', (route) =>
    route.fulfill({
      status: 200,
      contentType: 'application/json',
      body: JSON.stringify({ items: [], total: 0 }),
    }),
  );

  await page.route('**/api/measurement**', (route) =>
    route.fulfill({
      status: 200,
      contentType: 'application/json',
      body: JSON.stringify({ items: [], total: 0, limit: 50, offset: 0 }),
    }),
  );

  await page.route('**/api/device/status', (route) =>
    route.fulfill({
      status: 200,
      contentType: 'application/json',
      body: JSON.stringify({
        device_id: 'esp32_gw_01',
        status: 'online',
        uptime_s: 3_600,
        rssi_dbm: -58,
        free_heap_b: 210_000,
        ntp_synced: true,
        rtc_valid: true,
        last_seen_at: new Date(0).toISOString(),
      }),
    }),
  );

  // Ensure /set-token and /clear-token routes are never intercepted by Playwright.
  // Re-register as the LAST (highest priority) handlers so they always win.
  await page.route('**/api/set-token', (route) => route.continue());
  await page.route('**/api/clear-token', (route) => route.continue());
}

// ============================================================
// Authentication
// ============================================================

/**
 * Injects the `access_token` httpOnly cookie that `src/middleware.ts` checks
 * before rendering `/dashboard`. Credentials come from the environment so no
 * secret is hardcoded (Zero-hardcode-credential rule).
 */
export async function setAuthCookie(context: BrowserContext, baseURL: string): Promise<void> {
  const token = process.env.E2E_ACCESS_TOKEN?.trim();
  if (!token) {
    throw new Error(
      'E2E_ACCESS_TOKEN is not set. Export a valid dashboard JWT (e.g. from .env ADMIN_* login) before running E2E specs.',
    );
  }
  const { hostname } = new URL(baseURL);
  await context.addCookies([
    { name: 'access_token', value: token, domain: hostname, path: '/', httpOnly: true, sameSite: 'Lax' },
  ]);
}

/** Logs in through the real form; requires a reachable backend login route. */
export async function loginViaForm(
  page: Page,
  username = process.env.E2E_ADMIN_USERNAME ?? 'admin',
  password = process.env.E2E_ADMIN_PASSWORD ?? '',
): Promise<void> {
  await page.goto('/login');
  await page.locator('#username').fill(username);
  await page.locator('#password').fill(password);
  await page.getByRole('button', { name: 'Đăng nhập' }).click();
  await page.waitForURL('**/dashboard', { timeout: 20_000 });
}

// ============================================================
// Selectors — scoped to dashboard NodeGrid
// ============================================================

/**
 * Find the NodeCard for a given node index (0-indexed, maps to node IDs 4..7)
 * by matching the h3 heading text. This is far more robust than CSS class
 * matching since the heading always contains "Node #N" in production.
 */
export function nodeCard(page: Page, index: number) {
  // Locate the h3 heading "Node #N" then walk up to its enclosing card div
  // (the element carrying class "glass-card").
  return page.locator(
    `//h3[normalize-space()="Node #${index + 4}"]/ancestor::div[contains(@class, "glass-card")]`,
  );
}

/**
 * Get the outcome badge for a specific node (0-indexed).
 * Navigates from the node card's h3 heading to the badge within the same card.
 */
export const outcomeBadge = (page: Page, index: number) =>
  nodeCard(page, index).locator('[data-testid="outcome-badge"]');

/** Get the pump-on button for a specific node (0-indexed). */
export function pumpOnButton(page: Page, index: number) {
  return nodeCard(page, index).locator('[data-testid="pump-on"]');
}

/** Get the pump-off button for a specific node (0-indexed). */
export function pumpOffButton(page: Page, index: number) {
  return nodeCard(page, index).locator('[data-testid="pump-off"]');
}

/** Glowing pump relay indicator inside a NodeCard. */
export const pumpGlow = (page: Page, index: number) =>
  nodeCard(page, index).locator('.relay-glow-active');

// ============================================================
// Queries
// ============================================================

/** Returns the number of NodeCards currently rendered (counts outcome badges). */
export async function getNodeCount(page: Page): Promise<number> {
  return page.locator('[data-testid="outcome-badge"]').count();
}

/** Returns whether the WS fixture reports a connected state. */
export function getWsConnected(page: Page): Promise<boolean> {
  return page.evaluate(() => (window as any).__wsTest?.isConnected === true);
}

/**
 * Get connection attempt timestamps from the WS fixture for backoff validation.
 * Must execute inside the page because the fixture lives on `window`.
 */
export function getWsAttempts(page: Page): Promise<number[]> {
  return page.evaluate(() => ((window as any).__wsTest?.getAttempts?.() ?? []) as number[]);
}

// ============================================================
// Assertions
// ============================================================

export const EXPECTED_LABELS = {
  neutral: 'Chờ lệnh',
  pending: 'Đang gửi lệnh',
  rfAcked: 'Đã nhận lệnh (RF)',
  flowConfirmed: 'Xác nhận dòng chảy',
} as const;

export async function expectBadgeLabel(
  page: Page,
  index: number,
  label: string,
  timeout = 10_000,
): Promise<void> {
  await expect(outcomeBadge(page, index)).toHaveText(label, { timeout });
}

/** S4-NOOPT-01: badge must never claim flow without server evidence. */
export async function expectNoGlow(page: Page, index: number): Promise<void> {
  // The glow class is applied to the card container itself, not a child node.
  await expect(nodeCard(page, index)).not.toHaveClass(/relay-glow-active/);
}

export async function expectGlow(page: Page, index: number): Promise<void> {
  await expect(nodeCard(page, index)).toHaveClass(/relay-glow-active/, { timeout: 10_000 });
}

export async function expectAllBadges(
  page: Page,
  label: string,
  timeout = 10_000,
): Promise<void> {
  for (let i = 0; i < 4; i++) await expectBadgeLabel(page, i, label, timeout);
}

export async function expectNoGlowAnywhere(page: Page): Promise<void> {
  await expect(page.locator('.relay-glow-active')).toHaveCount(0, { timeout: 5_000 });
}

/** Set the fixture mode from the test (open / fail / recover). */
export function setWsMode(page: Page, mode: WsFixtureMode): Promise<void> {
  return page.evaluate((m) => (window as any).__wsTest.setMode(m), mode);
}

/**
 * Simulate a server-initiated close of the live WS connection (e.g. backend
 * restart). Drives the app's real `onclose` → `scheduleReconnect()` path.
 * Pair with `setWsMode(page, 'fail')` to keep the server "down".
 */
export function serverCloseWs(page: Page): Promise<boolean> {
  return page.evaluate(() => (window as any).__wsTest.serverClose());
}

/** Send a server event to the app's WS message handler. */
export async function sendWsEvent(
  page: Page,
  event: string,
  data: unknown,
): Promise<boolean> {
  return page.evaluate(
    ({ event: e, data: d }) => (window as any).__wsTest.sendEvent({ event: e, data: d }),
    { event, data },
  );
}

/**
 * Wait until the fixture has completed at least `count` connect attempts.
 */
export async function waitForConnectAttempts(page: Page, count: number, timeout = 45_000): Promise<void> {
  await page.waitForFunction(
    (expected) => (window as any).__wsTest.getConnectionCount() >= expected,
    count,
    { timeout },
  );
}

/** Wait until the fixture reports an established connection. */
export async function waitForWsConnected(page: Page, timeout = 20_000): Promise<void> {
  await page.waitForFunction(() => (window as any).__wsTest.isConnected === true, undefined, { timeout });
}
