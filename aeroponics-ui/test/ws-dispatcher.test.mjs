import test from 'node:test';
import assert from 'node:assert/strict';

import { wsMessageHandler, calculateBackoffDelay } from '../src/hooks/useWebSocket.ts';
import { useNodeStore } from '../src/store/useNodeStore.ts';
import { useDeviceStore } from '../src/store/useDeviceStore.ts';

test('N1: calculateBackoffDelay increases monotonically and matches spec', () => {
  const base = 1000;
  const factor = 1.5;
  const max = 30000;

  // Spec table: base * factor^n, capped at 30s
  for (let n = 1; n <= 12; n++) {
    const expected = Math.min(base * Math.pow(factor, n), max);
    assert.equal(calculateBackoffDelay(n), expected);
    // Monotonic increase (S4-WS-06): delay(n) >= delay(n-1)
    assert.ok(
      calculateBackoffDelay(n) >= calculateBackoffDelay(n - 1),
      `delay(${n}) must be >= delay(${n - 1})`,
    );
  }
  assert.equal(calculateBackoffDelay(100), max);
});

test('N1: calculateBackoffDelay exact values', () => {
  assert.equal(calculateBackoffDelay(0), 1000);
  assert.equal(calculateBackoffDelay(1), 1500);
  assert.equal(calculateBackoffDelay(2), 2250);
  assert.equal(calculateBackoffDelay(3), 3375);
  assert.equal(calculateBackoffDelay(10), 30000);
  assert.equal(calculateBackoffDelay(20), 30000);
});

test('N2: wsMessageHandler dispatches node_telemetry to node store', () => {
  useNodeStore.getState().resetAll();

  wsMessageHandler({
    event: 'node_telemetry',
    data: {
      nodeId: 4,
      health: 'OK',
      lastSeenAt: '2026-09-25T10:00:00Z',
      scheduleState: 'SPRAYING',
      overrideState: 'NONE',
      sensorSerial: 'RF-004',
    },
    timestamp: '2026-09-25T10:00:00Z',
  });

  const node = useNodeStore.getState().nodes[4];
  assert.equal(node.healthStatus, 'OK');
  assert.equal(node.scheduleState, 'SPRAYING');
  assert.equal(node.sensorSerial, 'RF-004');
  assert.equal(node.lastSeenAt, '2026-09-25T10:00:00Z');
});

test('N2: wsMessageHandler dispatches node_flow with flowConfirmed=true via applyFlowConfirmed only', () => {
  useNodeStore.getState().resetAll();

  wsMessageHandler({
    event: 'node_flow',
    data: {
      nodeId: 5,
      litresTotal: 12.5,
      flowRateLpm: 3.2,
      flowConfirmed: true,
      time: '2026-09-25T10:00:01Z',
    },
    timestamp: '2026-09-25T10:00:01Z',
  });

  const node = useNodeStore.getState().nodes[5];
  assert.equal(node.flowConfirmed, true);
  assert.equal(node.flowLpm, 3.2);
  assert.equal(node.litresTotal, 12.5);
  assert.equal(node.flowConfirmedAt, '2026-09-25T10:00:01Z');
});

test('N2: wsMessageHandler dispatches node_flow with flowConfirmed=false and clears flag', () => {
  useNodeStore.getState().resetAll();
  useNodeStore.getState().applyFlowConfirmed(6, true, 3.2, '2026-09-25T10:00:00Z');
  assert.equal(useNodeStore.getState().nodes[6].flowConfirmed, true);

  wsMessageHandler({
    event: 'node_flow',
    data: {
      nodeId: 6,
      litresTotal: 1.5,
      flowRateLpm: 0,
      flowConfirmed: false,
      time: '2026-09-25T10:00:02Z',
    },
    timestamp: '2026-09-25T10:00:02Z',
  });

  assert.equal(useNodeStore.getState().nodes[6].flowConfirmed, false);
});

test('N2: wsMessageHandler dispatches pump_command_update to outcome only', () => {
  useNodeStore.getState().resetAll();

  wsMessageHandler({
    event: 'pump_command_update',
    data: {
      commandId: 'cmd-123',
      nodeId: 7,
      outcome: 'RF_ACKED',
    },
    timestamp: '2026-09-25T10:00:03Z',
  });

  const node = useNodeStore.getState().nodes[7];
  assert.equal(node.outcome, 'RF_ACKED');
  // Must not infer RUNNING / flowConfirmed
  assert.equal(node.flowConfirmed, false);
});

test('N2: wsMessageHandler dispatches staleness_alert to node store', () => {
  useNodeStore.getState().resetAll();

  wsMessageHandler({
    event: 'staleness_alert',
    data: {
      nodeId: 4,
      lastSeenAt: '2026-09-25T09:58:00Z',
      staleForMs: 125_000,
    },
    timestamp: '2026-09-25T10:00:05Z',
  });

  const node = useNodeStore.getState().nodes[4];
  assert.equal(node.isStale, true);
  assert.equal(node.staleForMs, 125_000);
});

test('N2: wsMessageHandler dispatches device_status to device store', () => {
  useDeviceStore.getState().reset();

  wsMessageHandler({
    event: 'device_status',
    data: {
      deviceId: 'esp32_gw_01',
      status: 'online',
      uptime_s: 3600,
      rssi_dbm: -60,
      free_heap_b: 128000,
      ntpSynced: true,
      rtcValid: true,
      lastSeenAt: '2026-09-25T10:00:06Z',
    },
    timestamp: '2026-09-25T10:00:06Z',
  });

  const device = useDeviceStore.getState();
  assert.equal(device.status, 'online');
  assert.equal(device.deviceId, 'esp32_gw_01');
  assert.equal(device.uptime_s, 3600);
  assert.equal(device.ntpSynced, true);
});

test('N2: wsMessageHandler ignores malformed messages without crashing', () => {
  useNodeStore.getState().resetAll();

  // Missing nodeId
  wsMessageHandler({
    event: 'node_telemetry',
    data: { health: 'OK' },
    timestamp: '2026-09-25T10:00:07Z',
  });

  // Unknown event type
  wsMessageHandler({
    event: 'unknown_event',
    data: {},
    timestamp: '2026-09-25T10:00:08Z',
  });

  // Store still intact
  assert.equal(useNodeStore.getState().nodes[4].healthStatus, 'OK');
});

test('O1: applyFlowConfirmed validates whitelist and immutability', () => {
  useNodeStore.getState().resetAll();

  // Node outside AGU_NODE_IDS is ignored
  useNodeStore.getState().applyFlowConfirmed(9, true, 2.0, '2026-09-25T10:00:09Z');
  assert.equal(useNodeStore.getState().nodes[9], undefined);

  // Valid node updates
  useNodeStore.getState().applyFlowConfirmed(4, true, 2.5, '2026-09-25T10:00:10Z');
  const node = useNodeStore.getState().nodes[4];
  assert.equal(node.flowConfirmed, true);
  assert.equal(node.flowLpm, 2.5);
  assert.equal(node.flowConfirmedAt, '2026-09-25T10:00:10Z');

  // Other nodes unaffected (immutability)
  assert.equal(useNodeStore.getState().nodes[5].flowConfirmed, false);
});

test('O1: updateOutcome does not infer RUNNING state', () => {
  useNodeStore.getState().resetAll();

  useNodeStore.getState().updateOutcome(5, 'FLOW_CONFIRMED');
  const node = useNodeStore.getState().nodes[5];
  assert.equal(node.outcome, 'FLOW_CONFIRMED');
  // flowConfirmed must stay false — outcome alone never drives RUNNING
  assert.equal(node.flowConfirmed, false);
});