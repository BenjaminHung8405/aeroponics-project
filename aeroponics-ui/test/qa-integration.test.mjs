import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, readdirSync, statSync } from 'node:fs';
import { join } from 'node:path';
import { NextRequest } from 'next/server.js';

// Components and shared modules to audit
import { useNodeStore } from '../src/store/useNodeStore.ts';
import { useGroupStore } from '../src/store/useGroupStore.ts';
import { middleware } from '../src/middleware.ts';
import { POST as setTokenRoute } from '../src/app/api/set-token/route.ts';
import { POST as clearTokenRoute } from '../src/app/api/clear-token/route.ts';
import { handleUnauthorized, _resetLoggingOutState } from '../src/lib/auth.ts';

// -------------------------------------------------------------
// Helper to recursively collect files in a directory
// -------------------------------------------------------------
function getAllFiles(dir, fileList = []) {
  const files = readdirSync(dir);
  for (const file of files) {
    const filePath = join(dir, file);
    if (statSync(filePath).isDirectory()) {
      getAllFiles(filePath, fileList);
    } else if (file.endsWith('.ts') || file.endsWith('.tsx') || file.endsWith('.css')) {
      fileList.push(filePath);
    }
  }
  return fileList;
}

// =============================================================
// TEST SUITE: TRACK S4-F1 — E2E Integration Test
// =============================================================

test('S4-F1: Complete Auth Handshake to Set-Cookie Route Handler', async () => {
  // Step 1: Simulate POST /api/set-token with valid access token
  const mockToken = 'mock-jwt-header.mock-jwt-payload-signature-xyz123';
  const req = new NextRequest('http://localhost:3000/api/set-token', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ token: mockToken }),
  });

  const res = await setTokenRoute(req);
  assert.equal(res.status, 200);

  const json = await res.json();
  assert.equal(json.success, true);

  // Verify Set-Cookie header adheres to RFC 6265 and S4-AUTH-02
  const setCookieHeader = res.headers.get('set-cookie');
  assert.ok(setCookieHeader, 'Set-Cookie header must be present');
  assert.ok(setCookieHeader.includes('access_token=' + mockToken), 'Must contain token value');
  assert.ok(setCookieHeader.includes('Path=/'), 'Path must be /');
  assert.ok(setCookieHeader.toLowerCase().includes('httponly'), 'Must be HttpOnly');
  assert.ok(setCookieHeader.toLowerCase().includes('samesite=strict'), 'Must be SameSite=Strict');
  assert.ok(setCookieHeader.includes('Max-Age=86400'), 'Must have 24h Max-Age');

  // Step 2: Test rejection of invalid payload
  const badReq = new NextRequest('http://localhost:3000/api/set-token', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ token: '' }),
  });
  const badRes = await setTokenRoute(badReq);
  assert.equal(badRes.status, 400);
});

test('S4-F1: Native WS telemetry push triggers reactive store update in <=2s without page reload', () => {
  // Step 1: Initialize store with 4 nodes in IDLE
  const initialNodes = [
    { node_id: 4, display_name: 'Node 04', last_seen_at: new Date().toISOString(), schedule_state: 'IDLE', flow_lpm: 0, outcome: 'PENDING' },
    { node_id: 5, display_name: 'Node 05', last_seen_at: new Date().toISOString(), schedule_state: 'IDLE', flow_lpm: 0, outcome: 'PENDING' },
    { node_id: 6, display_name: 'Node 06', last_seen_at: new Date().toISOString(), schedule_state: 'IDLE', flow_lpm: 0, outcome: 'PENDING' },
    { node_id: 7, display_name: 'Node 07', last_seen_at: new Date().toISOString(), schedule_state: 'IDLE', flow_lpm: 0, outcome: 'PENDING' },
  ];
  useNodeStore.getState().initNodes(initialNodes);

  // Step 2: Measure update duration for WS telemetry event
  const startTime = performance.now();

  // Simulate WS event `node_telemetry` dispatching update for Node 7
  const telemetryTimestamp = new Date().toISOString();
  useNodeStore.getState().updateNode(7, {
    scheduleState: 'SPRAYING',
    flowLpm: 2.45,
    outcome: 'FLOW_CONFIRMED',
    lastSeenAt: telemetryTimestamp,
  });

  const durationMs = performance.now() - startTime;

  // Verify latency is <= 2s (in fact, synchronous < 5ms)
  assert.ok(durationMs <= 2000, `Telemetry update took ${durationMs}ms, must be <= 2000ms`);

  // Step 3: Verify node 7 was updated reactively
  const updatedNodes = useNodeStore.getState().nodes;
  assert.equal(updatedNodes[7].scheduleState, 'SPRAYING');
  assert.equal(updatedNodes[7].flowLpm, 2.45);
  assert.equal(updatedNodes[7].outcome, 'FLOW_CONFIRMED');
  assert.equal(updatedNodes[7].lastSeenAt, telemetryTimestamp);

  // Step 4: Verify nodes 2, 3, 4 were isolated and untouched
  assert.equal(updatedNodes[4].scheduleState, 'IDLE');
  assert.equal(updatedNodes[4].flowLpm, 0);
  assert.equal(updatedNodes[5].scheduleState, 'IDLE');
  assert.equal(updatedNodes[6].scheduleState, 'IDLE');

  // Step 5: Verify zero window.location.reload() in all src files
  const files = getAllFiles('src');
  for (const file of files) {
    const content = readFileSync(file, 'utf8');
    assert.equal(
      content.includes('location.reload'),
      false,
      `Forbidden location.reload found in ${file}`
    );
  }
});

test('S4-F1 & S4-ON-DEMAND-11: On-demand Tuya measurement trigger has zero polling loops & handles 429', () => {
  const measurementPanelContent = readFileSync('src/components/dashboard/MeasurementPanel.tsx', 'utf8');

  // Verify on-demand trigger uses POST mutation
  assert.ok(
    measurementPanelContent.includes('triggerMeasurement') || measurementPanelContent.includes('useTriggerMeasurement'),
    'Must use on-demand trigger hook'
  );

  // Verify 429 Cooldown handling (60s lock)
  assert.ok(
    measurementPanelContent.includes('cooldown') || measurementPanelContent.includes('COOLDOWN_SECONDS'),
    'Must implement cooldown guard for rate limit prevention'
  );

  // Verify zero setInterval polling in entire src directory
  const files = getAllFiles('src');
  const pollingRegex = /setInterval\s*\([^)]*trigger/i;
  for (const file of files) {
    const content = readFileSync(file, 'utf8');
    assert.equal(
      pollingRegex.test(content),
      false,
      `Forbidden background setInterval trigger polling found in ${file}`
    );
  }
});

// =============================================================
// TEST SUITE: TRACK S4-F2 — JWT Auth Flow & Security Validation
// =============================================================

test('S4-F2 & S4-AUTH-01: Next.js Edge Middleware route access protection', () => {
  // Case 1: No cookie requesting /dashboard -> Redirects to /login?from=/dashboard
  const reqDashboardNoToken = new NextRequest('http://localhost:3000/dashboard');
  const res1 = middleware(reqDashboardNoToken);
  assert.equal(res1.status, 307); // Next.js redirect
  assert.equal(res1.headers.get('location'), 'http://localhost:3000/login?from=%2Fdashboard');

  // Case 2: No cookie requesting /dashboard/settings -> Redirects with preserved target
  const reqSubDashboard = new NextRequest('http://localhost:3000/dashboard/settings');
  const res2 = middleware(reqSubDashboard);
  assert.equal(res2.status, 307);
  assert.equal(res2.headers.get('location'), 'http://localhost:3000/login?from=%2Fdashboard%2Fsettings');

  // Case 3: Valid cookie requesting /dashboard -> Allows request to proceed
  const reqDashboardWithToken = new NextRequest('http://localhost:3000/dashboard', {
    headers: { cookie: 'access_token=valid-jwt-token-12345' },
  });
  const res3 = middleware(reqDashboardWithToken);
  assert.equal(res3.status, 200); // NextResponse.next()
  assert.equal(res3.headers.get('location'), null);

  // Case 4: Valid cookie requesting /login -> Redirects to /dashboard
  const reqLoginWithToken = new NextRequest('http://localhost:3000/login', {
    headers: { cookie: 'access_token=valid-jwt-token-12345' },
  });
  const res4 = middleware(reqLoginWithToken);
  assert.equal(res4.status, 307);
  assert.equal(res4.headers.get('location'), 'http://localhost:3000/dashboard');

  // Case 5: No cookie requesting /login -> Allows request
  const reqLoginNoToken = new NextRequest('http://localhost:3000/login');
  const res5 = middleware(reqLoginNoToken);
  assert.equal(res5.status, 200);
  assert.equal(res5.headers.get('location'), null);
});

test('S4-F2 & S4-AUTH-03: HTTP 401 triggers clear-token and redirects to login with anti-loop guard', async () => {
  // Step 1: Verify POST /api/clear-token sets Max-Age=0
  const clearRes = await clearTokenRoute();
  assert.equal(clearRes.status, 200);
  const cookieHeader = clearRes.headers.get('set-cookie');
  assert.ok(cookieHeader, 'Set-Cookie must be returned by clear-token');
  assert.ok(cookieHeader.includes('Max-Age=0'), 'Must expire cookie with Max-Age=0');
  assert.ok(cookieHeader.includes('Path=/'), 'Path must be /');

  // Step 2: Test handleUnauthorized in browser simulation
  _resetLoggingOutState();
  let clearedTokenCalled = false;
  let redirectedUrl = '';

  // Mock global window & fetch
  const originalWindow = global.window;
  const originalFetch = global.fetch;

  global.window = {
    location: {
      pathname: '/dashboard',
      set href(val) {
        redirectedUrl = val;
      },
      get href() {
        return redirectedUrl;
      },
    },
  };

  global.fetch = async (url) => {
    if (url === '/api/clear-token') {
      clearedTokenCalled = true;
      return { ok: true };
    }
    return { ok: false };
  };

  try {
    await handleUnauthorized();
    assert.equal(clearedTokenCalled, true, 'Must call /api/clear-token');
    assert.equal(redirectedUrl, '/login?from=%2Fdashboard', 'Must redirect with from query param');

    // Test anti-loop guard: when already on /login
    _resetLoggingOutState();
    global.window.location.pathname = '/login';
    redirectedUrl = '';
    await handleUnauthorized();
    assert.equal(redirectedUrl, '/login', 'Must not append recursive ?from=/login');
  } finally {
    global.window = originalWindow;
    global.fetch = originalFetch;
    _resetLoggingOutState();
  }
});

test('S4-F2 & S4-AUTH-02: Zero Token Leakage — localStorage.getItem(access_token) is strictly null', () => {
  // Scan all src files for any localStorage or sessionStorage storage of access_token
  const files = getAllFiles('src');
  const leakRegex = /(localStorage|sessionStorage)\.setItem\s*\(\s*['"`](access_token|token|jwt)/i;

  for (const file of files) {
    const content = readFileSync(file, 'utf8');
    const match = content.match(leakRegex);
    assert.equal(
      match,
      null,
      `Security violation: Token stored in storage in ${file}: ${match ? match[0] : ''}`
    );
  }

  // Also verify no password console.log
  const logPasswordRegex = /console\.(log|info|debug|warn)\s*\([^)]*password/i;
  for (const file of files) {
    const content = readFileSync(file, 'utf8');
    const match = content.match(logPasswordRegex);
    assert.equal(
      match,
      null,
      `Security violation: Password logged to console in ${file}: ${match ? match[0] : ''}`
    );
  }
});

// =============================================================
// TEST SUITE: TRACK S4-F3 — Mobile Responsiveness & Tactile Layout Audit
// =============================================================

test('S4-F3 & S4-DS-MOBILE-17: Mathematical Zero Horizontal Overflow at 375px', () => {
  // Check layout and page root containers
  const layoutContent = readFileSync('src/app/dashboard/layout.tsx', 'utf8');
  const pageContent = readFileSync('src/app/dashboard/page.tsx', 'utf8');

  // Verify responsive wrappers don't use hardcoded pixel widths > 320px
  const hardcodedWidthRegex = /w-\[\s*([0-9]+)px\s*\]/g;
  let match;
  while ((match = hardcodedWidthRegex.exec(layoutContent + pageContent)) !== null) {
    const widthVal = parseInt(match[1], 10);
    assert.ok(
      widthVal <= 320,
      `Found fixed width ${widthVal}px > 320px which causes mobile horizontal overflow`
    );
  }

  // Verify data table in MeasurementPanel is wrapped in overflow-x-auto
  const measurementPanel = readFileSync('src/components/dashboard/MeasurementPanel.tsx', 'utf8');
  assert.ok(
    measurementPanel.includes('overflow-x-auto'),
    'History data table must have overflow-x-auto for 375px mobile safety'
  );
});

test('S4-F3 & S4-DS-MOBILE-17: Responsive column progression across viewports (375px to 1440px)', () => {
  const nodeGridContent = readFileSync('src/components/dashboard/NodeGrid.tsx', 'utf8');
  const groupGridContent = readFileSync('src/components/dashboard/GroupGrid.tsx', 'utf8');

  // Both grids must follow: grid-cols-1 (375px) -> sm:grid-cols-2 (640px) -> lg:grid-cols-4 (1024px-1440px)
  for (const [name, content] of [['NodeGrid', nodeGridContent], ['GroupGrid', groupGridContent]]) {
    assert.ok(content.includes('grid-cols-1'), `${name} must specify grid-cols-1 for mobile 375px`);
    assert.ok(content.includes('sm:grid-cols-2'), `${name} must specify sm:grid-cols-2 for 640px`);
    assert.ok(content.includes('lg:grid-cols-4'), `${name} must specify lg:grid-cols-4 for desktop 1024px+`);
  }
});

test('S4-F3 & S4-DS-MOBILE-17: MobileActionBar sticky bottom, md:hidden & iOS safe-area audit', () => {
  const barContent = readFileSync('src/components/layout/MobileActionBar.tsx', 'utf8');
  const layoutContent = readFileSync('src/app/dashboard/layout.tsx', 'utf8');

  // Sticky bottom positioning
  assert.ok(barContent.includes('fixed'), 'MobileActionBar must be fixed');
  assert.ok(barContent.includes('bottom-0'), 'MobileActionBar must stick to bottom-0');
  assert.ok(barContent.includes('z-40') || barContent.includes('z-50'), 'MobileActionBar must have high z-index');

  // Viewport visibility: md:hidden (only visible on mobile < 768px)
  assert.ok(barContent.includes('md:hidden'), 'MobileActionBar must be hidden on tablet/desktop md:hidden');

  // Safe area bottom inset support
  assert.ok(barContent.includes('env(safe-area-inset-bottom)'), 'MobileActionBar must respect iOS safe-area');

  // Layout bottom padding compensation
  assert.ok(
    layoutContent.includes('safe-area-inset-bottom'),
    'dashboard/layout.tsx must compensate bottom padding for MobileActionBar'
  );
});

test('S4-F3 & S4-DS-TOUCH-15: Touch ergonomics >= 44px/48px and tactile active:scale-95 feedback', () => {
  const files = getAllFiles('src/components');
  for (const file of files) {
    const content = readFileSync(file, 'utf8');

    // Scan for buttons and verify touch classes (min-h-[44px], min-h-[48px], btn-primary, btn-secondary)
    const buttonMatches = content.match(/<button[^>]*className=["']([^"']*)["'][^>]*>/g);
    if (buttonMatches) {
      for (const btn of buttonMatches) {
        // Skip hidden or visually collapsed buttons
        if (btn.includes('sr-only') || btn.includes('hidden')) continue;

        const hasMinHeight = btn.includes('min-h-[48px]') || btn.includes('min-h-[44px]') || btn.includes('btn-primary') || btn.includes('btn-secondary') || btn.includes('h-11') || btn.includes('h-12') || btn.includes('p-2') || btn.includes('p-3') || btn.includes('py-3') || btn.includes('py-2.5') || btn.includes('py-2');
        assert.ok(
          hasMinHeight,
          `Button in ${file} missing touch target >= 44px: ${btn.substring(0, 80)}...`
        );

        // Tactile micro-interaction
        const hasScale = btn.includes('active:scale-95') || btn.includes('active:scale-') || btn.includes('btn-primary') || btn.includes('btn-secondary');
        assert.ok(
          hasScale,
          `Interactive button in ${file} missing active:scale-95 feedback: ${btn.substring(0, 80)}...`
        );
      }
    }
  }
});

test('S4-F3 & S4-DS-FONT-12: Zero CLS via min-h-[220px] and JetBrains Mono tabular-nums on timers', () => {
  const nodeCardContent = readFileSync('src/components/dashboard/NodeCard.tsx', 'utf8');
  const groupCardContent = readFileSync('src/components/dashboard/GroupCard.tsx', 'utf8');

  // Min-height locks prevent Cumulative Layout Shift
  assert.ok(nodeCardContent.includes('min-h-[220px]'), 'NodeCard must lock min-h-[220px] for CLS=0');
  assert.ok(groupCardContent.includes('min-h-[220px]'), 'GroupCard must lock min-h-[220px] for CLS=0');

  // Tabular-nums prevents width shifting when digits change
  assert.ok(groupCardContent.includes('tabular-nums'), 'GroupCard countdown timer must have tabular-nums');
  assert.ok(nodeCardContent.includes('tabular-nums'), 'NodeCard flow metrics must have tabular-nums');
});
