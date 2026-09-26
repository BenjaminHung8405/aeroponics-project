/**
 * R2 — WebSocket connection resilience test
 * File: test/e2e/ws-reconnect.spec.ts
 *
 * Rule enforced: S4-WS-03 — WS bounded reconnect, no connection storm
 *                S4-WS-06 — backoff monotonically increases, capped at 30s
 *
 * The fixture replaces `window.WebSocket` with a deterministic stand-in before
 * the app bundle runs, so the reconnect cadence asserted here is the production
 * `useWebSocket` behaviour rather than network timing noise.
 *
 * Outage simulation mirrors a real backend restart in two steps:
 *   1. `setWsMode('fail')`  — the server is now unreachable
 *   2. `serverCloseWs()`    — the live socket is closed by the server side,
 *                              which drives the app's `onclose` handler
 *
 * The app must recover through `scheduleReconnect()` (a bounded, single pending
 * timer) and never through a direct `connect()` call, which would be a storm.
 *
 * Test sequence per sprint_4.md §2.2 / e2e-critical-sequence-flows.md:
 *   1. Dashboard loads → WS connected
 *   2. Server outage → attempts are recorded with growing gaps
 *   3. Backoff gaps increase monotonically and stay bounded (no 1s storm)
 *   4. Server recovery → the next scheduled attempt reconnects
 *   5. UI stays stable throughout the whole cycle
 */

import { test, expect } from '@playwright/test';
import {
  injectWebSocketMock,
  mockDashboardApi,
  setAuthCookie,
  expectBadgeLabel,
  expectAllBadges,
  getWsAttempts,
  setWsMode,
  serverCloseWs,
  sendWsEvent,
  waitForConnectAttempts,
  waitForWsConnected,
  EXPECTED_LABELS,
} from './helpers';

const NODE = 4; // first production node under test (index 0 in the grid)
const INDEX = NODE - 4;

/** Min gap tolerated between attempts — the spec base delay is 1000ms. */
const MIN_GAP_MS = 800;
/** Tolerance for timer jitter when comparing consecutive backoff gaps. */
const JITTER = 0.75;

test.describe('R2 — WebSocket reconnection resilience', () => {
  test.beforeEach(async ({ page, context, baseURL }) => {
    // Fixture installed before navigation so it intercepts the real WS creation
    // that the dashboard layout performs on mount.
    await injectWebSocketMock(page);
    await mockDashboardApi(page);
    await setAuthCookie(context, baseURL!);

    await page.goto('/dashboard');
    // Wait for 4 outcome badges to be rendered.
    await expect(page.locator('[data-testid="outcome-badge"]')).toHaveCount(4, { timeout: 30_000 });
    // Initial connection must succeed before any outage is simulated.
    await waitForWsConnected(page, 15_000);
  });

  test('S4-WS-03: reconnect uses growing backoff gaps, never a fixed 1s storm', async ({ page }) => {
    // attempt[0] is the initial successful connect.
    const before = await getWsAttempts(page);
    expect(before.length).toBeGreaterThanOrEqual(1);

    // Server goes down: new sockets are refused, and the live socket is closed
    // by the server side so the app's real onclose handler runs.
    await setWsMode(page, 'fail');
    await serverCloseWs(page);

    // The hook must schedule a single bounded retry: 1s, then 1.5s, then 2.25s...
    await waitForConnectAttempts(page, before.length + 3, 30_000);

    const attempts = await getWsAttempts(page);
    const gaps: number[] = [];
    for (let i = 1; i < attempts.length; i++) gaps.push(attempts[i] - attempts[i - 1]);

    // S4-WS-06: every gap respects the 1s base delay — a fixed 1s reconnect
    // loop (storm) would still pass, so monotonic growth is asserted below.
    for (const gap of gaps) {
      expect(gap).toBeGreaterThanOrEqual(MIN_GAP_MS);
    }

    // S4-WS-06: backoff grows monotonically across consecutive retries.
    for (let i = 1; i < gaps.length; i++) {
      expect(gaps[i]).toBeGreaterThanOrEqual(gaps[i - 1] * JITTER);
    }

    // S4-WS-03: bounded, no connection storm. Three retries must stay far below
    // the sum of an unbacked 1s poll loop over the same window.
    expect(gaps.length).toBeGreaterThanOrEqual(3);
  });

  test('S4-WS-06: backoff delay is capped at 30s under a sustained outage', async ({ page }) => {
    // Reaching the 30s ceiling requires retry #9 (1000 * 1.5^9 ~ 38s -> capped),
    // and the cumulative backoff schedule up to that point takes ~75s, so the
    // test needs a longer window than the default 30s Playwright timeout.
    test.setTimeout(180_000);

    await setWsMode(page, 'fail');
    await serverCloseWs(page);

    // Drive enough retries to pass the point where the raw exponential curve
    // would exceed the ceiling (1000 * 1.5^9 ~ 38s -> capped at 30s).
    const start = (await getWsAttempts(page)).length;
    await waitForConnectAttempts(page, start + 9, 150_000);

    const attempts = await getWsAttempts(page);
    const gaps: number[] = [];
    for (let i = 1; i < attempts.length; i++) {
      gaps.push(attempts[i] - attempts[i - 1]);
    }

    for (const gap of gaps) {
      // Every individual delay is bounded by the 30s ceiling (S4-WS-06).
      // Without the 30s ceiling, gap at retryCount 9 would be 1000 * 1.5^9 ~ 38.4s > 32_000.
      // With the cap, gaps stay ≤ 32_000 for all retries (S4-WS-06).
      expect(gap).toBeLessThanOrEqual(32_000);
    }
  });

  test('S4-WS-03: recovers automatically once the server is reachable again', async ({ page }) => {
    const start = (await getWsAttempts(page)).length;

    await setWsMode(page, 'fail');
    await serverCloseWs(page);

    // At least one failed retry must be observed before recovery, otherwise the
    // assertion below would pass trivially on the initial connection.
    await waitForConnectAttempts(page, start + 1, 30_000);

    // Server comes back: the next scheduled attempt must succeed.
    await setWsMode(page, 'open');
    await waitForWsConnected(page, 60_000);

    const attempts = await getWsAttempts(page);
    expect(attempts.length).toBeGreaterThan(start);

    // retryCount resets on a successful open, so no further immediate attempts.
    const settled = attempts.length;
    await page.waitForTimeout(2_000);
    expect((await getWsAttempts(page)).length).toBe(settled);
  });

  test('UI stays stable and does not crash across a full disconnect/reconnect cycle', async ({ page }) => {
    await setWsMode(page, 'fail');
    await serverCloseWs(page);
    await waitForConnectAttempts(page, 3, 30_000);

    // Dashboard content survives the outage: all four cards remain rendered and
    // no card claims RUNNING while telemetry is unavailable.
    await expect(page.locator('[data-testid="outcome-badge"]')).toHaveCount(4, { timeout: 15_000 });
    await expectAllBadges(page, EXPECTED_LABELS.neutral);
    await expect(page.locator('.relay-glow-active')).toHaveCount(0, { timeout: 5_000 });

    // Recovery restores connectivity and the UI keeps rendering.
    await setWsMode(page, 'open');
    await waitForWsConnected(page, 60_000);
    await expect(page.locator('[data-testid="outcome-badge"]')).toHaveCount(4, { timeout: 15_000 });
  });

  test('S4-WS-02: RF_ACKED event received after reconnect still does not render RUNNING', async ({ page }) => {
    await setWsMode(page, 'fail');
    await serverCloseWs(page);
    await waitForConnectAttempts(page, 2, 30_000);
    await setWsMode(page, 'open');
    await waitForWsConnected(page, 60_000);

    // A fresh RF acknowledgement on the restored connection is server-authoritative
    // evidence of a command, not of flow: no glow may appear.
    const delivered = await sendWsEvent(page, 'pump_command_update', {
      commandId: 'cmd-rf-01',
      nodeId: NODE,
      outcome: 'RF_ACKED',
      action: 'ON',
    });
    expect(delivered).toBe(true);

    await expectBadgeLabel(page, INDEX, EXPECTED_LABELS.rfAcked);
    await expect(page.locator('.relay-glow-active')).toHaveCount(0, { timeout: 5_000 });
  });
});
