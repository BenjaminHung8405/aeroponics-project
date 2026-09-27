import test from 'node:test';
import assert from 'node:assert/strict';
import { execSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';
import { readdirSync, readFileSync } from 'node:fs';

// Import modules to test
import {
  OUTCOME_CONFIG,
  getOutcomeConfig,
  STALE_THRESHOLD_MS,
  STALE_AMBER_MS,
  WS_RECONNECT_MAX_DELAY_MS,
  WS_EVENTS,
  QUERY_KEYS,
} from '../src/lib/constants.ts';

import { buildApiUrl, ApiError } from '../src/lib/api.ts';
import { calculateBackoffDelay } from '../src/hooks/useWebSocket.ts';
import { useNodeStore } from '../src/store/useNodeStore.ts';
import { useGroupStore } from '../src/store/useGroupStore.ts';

test('S4-C5: OUTCOME_CONFIG is strictly frozen (Object.freeze)', () => {
  assert.equal(Object.isFrozen(OUTCOME_CONFIG), true);
  assert.throws(
    () => {
      // @ts-expect-error - testing runtime mutation rejection
      OUTCOME_CONFIG.NEW_KEY = { label: 'hack' };
    },
    { name: 'TypeError' },
  );
});

test('S4-C5: getOutcomeConfig correctly maps outcomes & FAULT_* prefixes', () => {
  const confirmed = getOutcomeConfig('FLOW_CONFIRMED');
  assert.equal(confirmed.color, 'primary');
  assert.equal(confirmed.isFault, false);

  const acked = getOutcomeConfig('RF_ACKED');
  assert.equal(acked.color, 'accent-indigo');

  const pending = getOutcomeConfig('PENDING');
  assert.equal(pending.color, 'text-subtle');

  // W2 (Finding #15): REJECTED must map to Vietnamese label, not raw English fallback
  const rejected = getOutcomeConfig('REJECTED');
  assert.equal(rejected.label, 'Đã từ chối');
  assert.equal(rejected.isFault, false);
  assert.equal(rejected.color, 'danger');

  const timeout = getOutcomeConfig('TIMEOUT');
  assert.equal(timeout.color, 'accent-amber');

  // Prefix checks for all FAULT_*
  const faultNoAck = getOutcomeConfig('FAULT_NO_ACK');
  assert.equal(faultNoAck.color, 'danger');
  assert.equal(faultNoAck.isFault, true);
  assert.equal(faultNoAck.label, 'Lỗi không nhận ACK');

  const faultOverRange = getOutcomeConfig('FAULT_OVER_RANGE');
  assert.equal(faultOverRange.color, 'danger');
  assert.equal(faultOverRange.isFault, true);

  const faultCustom = getOutcomeConfig('FAULT_MOTOR_STALL');
  assert.equal(faultCustom.color, 'danger');
  assert.equal(faultCustom.isFault, true);

  // Fallbacks
  const nullConfig = getOutcomeConfig(null);
  assert.equal(nullConfig.color, 'text-subtle');
  assert.equal(nullConfig.label, 'Chờ lệnh');

  const undefinedConfig = getOutcomeConfig(undefined);
  assert.equal(undefinedConfig.color, 'text-subtle');
  assert.equal(undefinedConfig.label, 'Chờ lệnh');
});

test('S4-C5: Staleness constants are 120s and 60s', () => {
  assert.equal(STALE_THRESHOLD_MS, 120_000);
  assert.equal(STALE_AMBER_MS, 60_000);
  assert.equal(WS_RECONNECT_MAX_DELAY_MS, 30_000);
  assert.equal(Object.isFrozen(WS_EVENTS), true);
  assert.equal(Object.isFrozen(QUERY_KEYS), true);
});

test('S4-C1: buildApiUrl handles path normalization & fallback to /api', () => {
  const originalEnv = process.env.NEXT_PUBLIC_API_URL;

  // Case 1: NEXT_PUBLIC_API_URL is unset / blank -> fallback to same-origin /api
  delete process.env.NEXT_PUBLIC_API_URL;
  assert.equal(buildApiUrl('/season/active'), '/api/season/active');
  assert.equal(buildApiUrl('season/active'), '/api/season/active');
  assert.equal(buildApiUrl('/api/season/active'), '/api/season/active');
  assert.equal(buildApiUrl('/node'), '/api/node');

  // Case 2: NEXT_PUBLIC_API_URL is dev host without /api
  process.env.NEXT_PUBLIC_API_URL = 'http://test-backend:3001';
  assert.equal(buildApiUrl('/season/active'), 'http://test-backend:3001/api/season/active');
  assert.equal(buildApiUrl('/api/season/active'), 'http://test-backend:3001/api/season/active');
  assert.equal(buildApiUrl('node'), 'http://test-backend:3001/api/node');

  // Case 3: NEXT_PUBLIC_API_URL is dev host with /api
  process.env.NEXT_PUBLIC_API_URL = 'http://test-backend:3001/api';
  assert.equal(buildApiUrl('/season/active'), 'http://test-backend:3001/api/season/active');
  assert.equal(buildApiUrl('/api/season/active'), 'http://test-backend:3001/api/season/active');

  // Restore env
  if (originalEnv !== undefined) {
    process.env.NEXT_PUBLIC_API_URL = originalEnv;
  } else {
    delete process.env.NEXT_PUBLIC_API_URL;
  }
});

test('S4-C1: ApiError formats message accurately', () => {
  const err1 = new ApiError(404, 'Not Found', { message: 'Season not found' });
  assert.equal(err1.status, 404);
  assert.equal(err1.message, 'Season not found');

  const err2 = new ApiError(400, 'Bad Request', { message: ['Invalid ID', 'Name required'] });
  assert.equal(err2.message, 'Invalid ID, Name required');

  const err3 = new ApiError(500, 'Internal Server Error');
  assert.equal(err3.message, 'API Error 500: Internal Server Error');
});

test('S4-C2: calculateBackoffDelay enforces exponential backoff capped at 30s', () => {
  assert.equal(calculateBackoffDelay(0), 1000);
  assert.equal(calculateBackoffDelay(1), 1500);
  assert.equal(calculateBackoffDelay(2), 2250);
  assert.equal(calculateBackoffDelay(3), 3375);
  // Capped at 30,000ms
  assert.equal(calculateBackoffDelay(10), 30000);
  assert.equal(calculateBackoffDelay(20), 30000);
});

test('S4-C3: useNodeStore manages 15 modern nodes with immutable updates', () => {
  const store = useNodeStore.getState();
  store.resetAll();

  // Baseline modern node range 1..15
  const nodes = useNodeStore.getState().nodes;
  assert.equal(Object.keys(nodes).length, 15);
  assert.equal(nodes[4].id, 4);
  assert.equal(nodes[7].id, 7);

  // Update physical node 4
  store.updateNode(4, { flowLpm: 2.8, outcome: 'FLOW_CONFIRMED', flowConfirmed: true });
  const updated = useNodeStore.getState().nodes;
  assert.equal(updated[4].flowLpm, 2.8);
  assert.equal(updated[4].outcome, 'FLOW_CONFIRMED');
  assert.equal(updated[4].flowConfirmed, true);

  // Neighboring node must remain unchanged
  assert.equal(updated[5].flowLpm, 0);
  assert.equal(updated[5].outcome, null);

  // Out of bounds node update ignored
  store.updateNode(16, { flowLpm: 99 });
  assert.equal(useNodeStore.getState().nodes[16], undefined);
});

test('S4-C3: useGroupStore manages 4 groups with immutable updates', () => {
  const store = useGroupStore.getState();
  store.resetAll();

  // Baseline 4 groups
  const groups = useGroupStore.getState().groups;
  assert.equal(Object.keys(groups).length, 4);
  assert.equal(groups[1].groupId, 1);
  assert.equal(groups[4].groupId, 4);

  // Update group 2
  store.updateGroup(2, { status: 'ACTIVE', phase: 'DAY', nextTransitionAt: '2026-09-14T06:00:00Z' });
  const updated = useGroupStore.getState().groups;
  assert.equal(updated[2].status, 'ACTIVE');
  assert.equal(updated[2].phase, 'DAY');
  assert.equal(updated[2].nextTransitionAt, '2026-09-14T06:00:00Z');

  // Group 1 must remain unchanged
  assert.equal(updated[1].status, 'UNASSIGNED');
  assert.equal(updated[1].phase, null);

  // Out of bounds group update ignored
  store.updateGroup(9, { status: 'ACTIVE' });
  assert.equal(useGroupStore.getState().groups[9], undefined);
});

test('S4 Hard Rules: Zero hardcoded host, zero socket.io, zero emoji, zero reload in src/', () => {
  const __filename = fileURLToPath(import.meta.url);
  const __dirname = dirname(__filename);
  const srcDir = join(__dirname, '..', 'src');

  // Hard Rule S4-API-05: Zero localhost:3001 or 127.0.0.1:3001
  const localhostCheck = execSync(`grep -rnE 'localhost:3001|127\\.0\\.0\\.1:3001' "${srcDir}" || true`).toString();
  assert.equal(localhostCheck.trim(), '', 'Found hardcoded localhost:3001 in src/');

  // Hard Rule S4-WS-04: Zero socket.io
  const socketIoCheck = execSync(`grep -rn 'socket.io' "${srcDir}" || true`).toString();
  assert.equal(socketIoCheck.trim(), '', 'Found socket.io import in src/');

  // Hard Rule S4-WS-04: Zero window.location.reload()
  const reloadCheck = execSync(`grep -rn 'location\\.reload' "${srcDir}" || true`).toString();
  assert.equal(reloadCheck.trim(), '', 'Found window.location.reload() in src/');

  // Hard Rule S4-NO-RELAY-07: Zero legacy relay endpoints or events
  const relayCheck = execSync(`grep -rnE '/api/relay|relay_update' "${srcDir}" || true`).toString();
  assert.equal(relayCheck.trim(), '', 'Found legacy relay endpoints or events in src/');

  // Hard Rule S4-DS-ICON-14: Zero emoji
  const emojiRegex = /\p{Extended_Pictographic}/u;
  function scanForEmoji(dir) {
    const entries = readdirSync(dir, { withFileTypes: true });
    for (const entry of entries) {
      const fullPath = join(dir, entry.name);
      if (entry.isDirectory()) {
        scanForEmoji(fullPath);
      } else if (/\.(ts|tsx|js|jsx|css|html)$/.test(entry.name)) {
        const content = readFileSync(fullPath, 'utf8');
        const match = content.match(emojiRegex);
        assert.equal(match, null, `Found emoji in ${fullPath}: ${match?.[0]}`);
      }
    }
  }
  scanForEmoji(srcDir);
});
