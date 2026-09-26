import { defineConfig, devices } from '@playwright/test';

/**
 * Playwright E2E Configuration for Aeroponics Smart Farm
 *
 * Tests validate the full control loop and WebSocket resilience through
 * the nginx reverse proxy at port 6003 (S4-E2E-06, S4-WS-03).
 *
 * Prerequisites:
 *   docker compose up -d (full stack running on port 6003)
 * Or for development:
 *   NEXT_PUBLIC_API_URL= http://localhost:3001 (backend directly)
 */
export default defineConfig({
  testDir: './test/e2e',
  fullyParallel: false, // E2E tests are sequential to avoid port conflicts
  forbidOnly: !!process.env.CI,
  retries: process.env.CI ? 2 : 0,
  workers: 1,
  reporter: [['list'], ['html', { open: 'never' }]],

  use: {
    baseURL: process.env.E2E_BASE_URL || 'http://localhost:6003',
    trace: 'on-first-retry',
    screenshot: 'only-on-failure',
    video: 'on-first-retry',
    // Generous timeout for WS reconnect tests (S4-WS-03: max 40s reconnect window)
    actionTimeout: 15_000,
    navigationTimeout: 30_000,
  },

  projects: [
    {
      name: 'chromium',
      use: { ...devices['Desktop Chrome'] },
    },
  ],

  /* No built-in webServer — stack must be running via docker compose */
});
