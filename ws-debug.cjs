const { chromium } = require('@playwright/test');

(async () => {
  const browser = await chromium.launch();
  const context = await browser.newContext();
  const page = await context.newPage();

  // Capture requests to /api/node
  page.on('request', (req) => {
    if (req.url().includes('/api/node')) {
      console.log(`REQUEST: ${req.method()} ${req.url()}`);
    }
  });
  page.on('response', async (res) => {
    if (res.url().includes('/api/node')) {
      let body = '';
      try { body = (await res.text()).substring(0, 300); } catch {}
      console.log(`RESPONSE: ${res.status()} ${res.url()}\n  ${body}`);
    }
  });

  // Register mock BEFORE navigation
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
      active_calibration: null,
    }));
    console.log('MOCK HANDLER HIT for', route.request().url());
    return route.fulfill({
      status: 200,
      contentType: 'application/json',
      body: JSON.stringify(nodes),
    });
  });

  // Set auth cookie
  await context.addCookies([
    {
      name: 'access_token',
      value: 'eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJ1c2VybmFtZSI6ImFkbWluIiwic3ViIjoiYWRtaW4iLCJyb2xlIjoiYWRtaW4iLCJpYXQiOjE3OTAzNTY1NjIsImV4cCI6MTc5MDQ0Mjk2Mn0.LwTpGF2fld9pUBeRK_7uYE4ac7mqg-D2X1dXzJ7-8cI',
      domain: 'localhost',
      path: '/',
      httpOnly: true,
      sameSite: 'Lax',
    },
  ]);

  await page.goto('http://localhost:6003/dashboard', { waitUntil: 'networkidle' });
  await page.waitForTimeout(2000);

  // Check button state for Node #4
  const disabled = await page
    .locator('//h3[normalize-space()="Node #4"]/ancestor::div[contains(@class, "glass-card")]')
    .locator('[data-testid="pump-on"]')
    .isDisabled()
    .catch(() => 'not-found');
  console.log('PUMP-ON DISABLED:', disabled);

  // Check the badge text
  const badge = await page
    .locator('[data-testid="outcome-badge"]')
    .first()
    .textContent()
    .catch(() => 'not-found');
  console.log('FIRST BADGE TEXT:', badge);

  await browser.close();
})();