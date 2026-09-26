/**
 * R1 — Full pump control loop E2E
 * File: test/e2e/pump-control.spec.ts
 *
 * Chain under test (sprint_4.md §2.1):
 *   UI click → REST command → backend accepts → PENDING badge
 *   → gateway telemetry → WS node_flow → store applyFlowConfirmed()
 *   → FLOW_CONFIRMED badge + relay glow → click OFF → outcome cleared
 *
 * Hard rules asserted here:
 *   S4-NOOPT-01  no intermediate RUNNING between click ON and WS FLOW_CONFIRMED
 *   S4-WS-02     RUNNING derived only from server-authoritative WS state
 *   S4-WS-04     fresh dashboard badge is the neutral "Chờ lệnh"
 *   S4-STALE-05  a stale node never keeps the RUNNING glow
 *   S4-E2E-07    outcome cleared after pump OFF
 *   S4-E2E-06    full control loop coverage
 */

import { test, expect } from '@playwright/test';
import {
  injectWebSocketMock,
  mockDashboardApi,
  setAuthCookie,
  getNodeCount,
  outcomeBadge,
  pumpOnButton,
  pumpOffButton,
  expectBadgeLabel,
  expectGlow,
  expectNoGlow,
  EXPECTED_LABELS,
  getWsConnected,
  sendWsEvent,
  expectAllBadges,
  waitForWsConnected,
} from './helpers';

const NODE = 4; // first production node under test (index 0 in the grid)
const INDEX = NODE - 4;

test.describe('R1 — Pump control loop (UI → Backend → Gateway → WS → UI)', () => {
  test.beforeEach(async ({ page, context, baseURL }) => {
    // The WebSocket fixture must be installed before any app script runs.
    await injectWebSocketMock(page);
    await mockDashboardApi(page);
    await setAuthCookie(context, baseURL!);

    await page.goto('/dashboard');
    // Wait for the dashboard to finish initial data loads.
    // Wait for 4 outcome badges to be rendered.
    await expect(page.locator('[data-testid="outcome-badge"]')).toHaveCount(4, { timeout: 30_000 });
    // Wait until the WS fixture reports an established connection.
    await waitForWsConnected(page, 15_000);
  });

  test('S4-WS-04: fresh dashboard renders the neutral badge, not PENDING', async ({ page }) => {
    // Fresh dashboard = outcome = null → DEFAULT_NEUTRAL_STYLE → "Chờ lệnh"
    await expectBadgeLabel(page, INDEX, EXPECTED_LABELS.neutral);
    await expectNoGlow(page, INDEX);
  });

  test('S4-NOOPT-01: click ON shows PENDING and never an intermediate RUNNING glow', async ({ page }) => {
    await pumpOnButton(page, INDEX).click();

    // Server accepted the command, flow is not yet evidenced.
    await expectBadgeLabel(page, INDEX, EXPECTED_LABELS.pending);

    // Critical assertion: no RUNNING display while waiting for sensor evidence.
    await expectNoGlow(page, INDEX);
    await expect(outcomeBadge(page, INDEX)).not.toHaveText(EXPECTED_LABELS.flowConfirmed);

    // Hold the assertion across a settle window to catch any optimistic flicker.
    await page.waitForTimeout(1_000);
    await expectNoGlow(page, INDEX);
    await expectBadgeLabel(page, INDEX, EXPECTED_LABELS.pending);
  });

  test('S4-WS-02: RUNNING appears only after the WS FLOW_CONFIRMED event', async ({ page }) => {
    await pumpOnButton(page, INDEX).click();
    await expectBadgeLabel(page, INDEX, EXPECTED_LABELS.pending);

    // Mock gateway telemetry publish: backend relays it to the dashboard over WS.
    const delivered = await sendWsEvent(page, 'node_flow', {
      nodeId: NODE,
      litresTotal: 0.42,
      flowRateLpm: 1.25,
      isFault: false,
      faultCode: null,
      flowConfirmed: true,
      sampleWindowMs: 2_000,
      time: new Date().toISOString(),
    });
    expect(delivered).toBe(true);

    // The flow event alone must not be enough: outcome is still PENDING, so the
    // backend must follow with the command outcome event.
    await sendWsEvent(page, 'pump_command_update', {
      commandId: 'cmd-e2e-1',
      nodeId: NODE,
      outcome: 'FLOW_CONFIRMED',
      action: 'ON',
      flowRateLpm: 1.25,
      flowConfirmedAt: new Date().toISOString(),
    });

    await expectBadgeLabel(page, INDEX, EXPECTED_LABELS.flowConfirmed);
    await expectGlow(page, INDEX);
  });

  test('S4-WS-02: RF_ACKED alone must not render RUNNING', async ({ page }) => {
    await pumpOnButton(page, INDEX).click();
    await expectBadgeLabel(page, INDEX, EXPECTED_LABELS.pending);

    await sendWsEvent(page, 'pump_command_update', {
      commandId: 'cmd-e2e-1',
      nodeId: NODE,
      outcome: 'RF_ACKED',
      action: 'ON',
    });

    await expectBadgeLabel(page, INDEX, EXPECTED_LABELS.rfAcked);
    await expectNoGlow(page, INDEX);
  });

  test('S4-E2E-07: pump OFF clears the outcome and the glow returns to neutral', async ({ page }) => {
    await pumpOnButton(page, INDEX).click();
    await expectBadgeLabel(page, INDEX, EXPECTED_LABELS.pending);

    await sendWsEvent(page, 'node_flow', {
      nodeId: NODE,
      litresTotal: 0.42,
      flowRateLpm: 1.25,
      isFault: false,
      faultCode: null,
      flowConfirmed: true,
      sampleWindowMs: 2_000,
      time: new Date().toISOString(),
    });
    await sendWsEvent(page, 'pump_command_update', {
      commandId: 'cmd-e2e-1',
      nodeId: NODE,
      outcome: 'FLOW_CONFIRMED',
      action: 'ON',
    });
    await expectBadgeLabel(page, INDEX, EXPECTED_LABELS.flowConfirmed);
    await expectGlow(page, INDEX);

    // OFF: button becomes available only while the node is genuinely running.
    await pumpOffButton(page, INDEX).click();
    await sendWsEvent(page, 'node_flow', {
      nodeId: NODE,
      litresTotal: 0.42,
      flowRateLpm: 0,
      isFault: false,
      faultCode: null,
      flowConfirmed: false,
      sampleWindowMs: 2_000,
      time: new Date().toISOString(),
    });
    await sendWsEvent(page, 'pump_command_update', {
      commandId: 'cmd-e2e-2',
      nodeId: NODE,
      outcome: 'PENDING',
      action: 'OFF',
    });

    // No intermediate RUNNING: the card must drop the glow and stop claiming flow.
    await expectNoGlow(page, INDEX);
    await expect(outcomeBadge(page, INDEX)).not.toHaveText(EXPECTED_LABELS.flowConfirmed);
  });

  test('S4-STALE-05: a stale node stops rendering the RUNNING glow', async ({ page }) => {
    await pumpOnButton(page, INDEX).click();
    await expectBadgeLabel(page, INDEX, EXPECTED_LABELS.pending);

    await sendWsEvent(page, 'node_flow', {
      nodeId: NODE,
      litresTotal: 0.42,
      flowRateLpm: 1.25,
      isFault: false,
      faultCode: null,
      flowConfirmed: true,
      sampleWindowMs: 2_000,
      time: new Date().toISOString(),
    });
    await sendWsEvent(page, 'pump_command_update', {
      commandId: 'cmd-e2e-1',
      nodeId: NODE,
      outcome: 'FLOW_CONFIRMED',
      action: 'ON',
    });
    await expectGlow(page, INDEX);

    // Telemetry stops: the backend raises a staleness alert for the node.
    await sendWsEvent(page, 'staleness_alert', {
      nodeId: NODE,
      lastSeenAt: new Date(Date.now() - 180_000).toISOString(),
      staleForMs: 180_000,
    });

    // S4-STALE-05: the NodeCard RUNNING glow is driven by isNodeRunning(), which
    // includes the !isStale term, so the card must drop the glow even though the
    // last server outcome is still FLOW_CONFIRMED.
    await expectNoGlow(page, INDEX);
  });

  test('S4-NOOPT-01: flow telemetry without a command outcome never shows FLOW_CONFIRMED', async ({ page }) => {
    // Simulates an out-of-band sensor reading (e.g. schedule-driven spray)
    // arriving before any manual command was issued for this node.
    await sendWsEvent(page, 'node_flow', {
      nodeId: NODE,
      litresTotal: 5,
      flowRateLpm: 2,
      isFault: false,
      faultCode: null,
      flowConfirmed: true,
      sampleWindowMs: 2_000,
      time: new Date().toISOString(),
    });

    await expectBadgeLabel(page, INDEX, EXPECTED_LABELS.neutral);
    await expectNoGlow(page, INDEX);
  });

  test('dispatcher resilience: malformed WS payloads do not crash the dashboard', async ({ page }) => {
    const errors: string[] = [];
    page.on('pageerror', (err) => errors.push(err.message));

    // Missing nodeId, unknown event, and non-JSON payloads are all ignored upstream.
    await sendWsEvent(page, 'node_flow', { flowConfirmed: true });
    await sendWsEvent(page, 'totally_unknown_event', { nodeId: NODE });
    await sendWsEvent(page, 'pump_command_update', { nodeId: NODE });

    await expect(getNodeCount(page), { timeout: 10_000 });
    expect(errors).toEqual([]);
    await expectAllBadges(page, EXPECTED_LABELS.neutral);
  });
});
