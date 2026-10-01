import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

import { wsMessageHandler } from '../src/hooks/useWebSocket.ts';
import { useDeviceStore } from '../src/store/useDeviceStore.ts';

const bannerSource = readFileSync('src/components/common/SyncStatusBanner.tsx', 'utf8');
const retrySource = readFileSync('src/hooks/queries/useDeviceStatus.ts', 'utf8');

test('schedule sync WebSocket updates the selected device snapshot', () => {
  useDeviceStore.getState().reset();
  wsMessageHandler({
    event: 'device_schedule_sync',
    data: {
      deviceId: 'gateway-a',
      syncState: 'IN_SYNC',
      reportedScheduleState: { slots_reconciled: true },
      scheduleSyncUpdatedAt: '2026-10-01T10:00:00Z',
      scheduleSyncDetails: null,
    },
  });

  const device = useDeviceStore.getState().getDevice('gateway-a');
  assert.equal(device.syncState, 'IN_SYNC');
  assert.deepEqual(device.reportedScheduleState, { slots_reconciled: true });
  assert.equal(device.scheduleSyncUpdatedAt, '2026-10-01T10:00:00Z');
});

test('SyncStatusBanner defines all schedule sync states and their icons', () => {
  for (const state of [
    'IN_SYNC', 'IN_SYNC_PENDING_BOUNDARY', 'SYNCING',
    'DRIFTED', 'DRIFTED_LATCHED', 'UNCONFIRMED',
  ]) assert.match(bannerSource, new RegExp(state));
  for (const icon of ['CheckCircle', 'Clock', 'RefreshCw', 'AlertTriangle', 'HelpCircle']) {
    assert.match(bannerSource, new RegExp(icon));
  }
  assert.match(bannerSource, /IN_SYNC_PENDING_BOUNDARY:[\s\S]*?border-blue/);
  assert.match(bannerSource, /IN_SYNC_PENDING_BOUNDARY:[\s\S]*?Icon: Clock/);
});

test('retry action is limited to drift states', () => {
  assert.match(bannerSource, /syncState === 'DRIFTED' \|\| syncState === 'DRIFTED_LATCHED'/);
  assert.doesNotMatch(bannerSource, /syncState === 'IN_SYNC'/);
  assert.doesNotMatch(retrySource, /syncState.*IN_SYNC/);
});
