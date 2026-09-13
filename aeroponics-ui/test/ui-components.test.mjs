import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, readdirSync, statSync } from 'node:fs';
import { join } from 'node:path';

import { getOutcomeConfig, OUTCOME_CONFIG, STALE_THRESHOLD_MS, STALE_AMBER_MS, getStalenessLevel } from '../src/lib/constants.ts';
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
// Test Suite: S4-D UI Components Verification
// =============================================================

test('S4-D6 & S4-STALENESS-09: StalenessIndicator boundary transitions', () => {
  // Test boundary < 60s (fresh)
  const fresh0 = getStalenessLevel(null, false, 0);
  assert.equal(fresh0.level, 'fresh');

  const fresh59 = getStalenessLevel(null, false, 59_000);
  assert.equal(fresh59.level, 'fresh');
  assert.equal(fresh59.elapsedSeconds, 59);

  // Test boundary 60s .. 119s (warning)
  const warn60 = getStalenessLevel(null, false, STALE_AMBER_MS); // 60,000ms
  assert.equal(warn60.level, 'warning');
  assert.equal(warn60.elapsedSeconds, 60);

  const warn119 = getStalenessLevel(null, false, 119_000);
  assert.equal(warn119.level, 'warning');
  assert.equal(warn119.elapsedSeconds, 119);

  // Test boundary >= 120s (stale)
  const stale120 = getStalenessLevel(null, false, STALE_THRESHOLD_MS); // 120,000ms
  assert.equal(stale120.level, 'stale');
  assert.equal(stale120.elapsedSeconds, 120);

  const stale300 = getStalenessLevel(null, false, 300_000);
  assert.equal(stale300.level, 'stale');

  // Test explicit isStale flag
  const explicitStale = getStalenessLevel(null, true, 10_000);
  assert.equal(explicitStale.level, 'stale');
});

test('S4-D6 & S4-OUTCOME-08: OutcomeBadge getOutcomeConfig mappings', () => {
  const flowConfirmed = getOutcomeConfig('FLOW_CONFIRMED');
  assert.equal(flowConfirmed.color, 'primary');
  assert.equal(flowConfirmed.isFault, false);
  assert.ok(flowConfirmed.bgClass.includes('bg-primary'));

  const rfAcked = getOutcomeConfig('RF_ACKED');
  assert.equal(rfAcked.color, 'accent-indigo');
  assert.equal(rfAcked.isFault, false);

  const timeout = getOutcomeConfig('TIMEOUT');
  assert.equal(timeout.color, 'accent-amber');
  assert.equal(timeout.isFault, false);

  const pending = getOutcomeConfig('PENDING');
  assert.equal(pending.isFault, false);

  // FAULT_* variations
  const faultNoAck = getOutcomeConfig('FAULT_NO_ACK');
  assert.equal(faultNoAck.isFault, true);
  assert.equal(faultNoAck.color, 'danger');
  assert.ok(faultNoAck.label.includes('ACK'));

  const faultOverRange = getOutcomeConfig('FAULT_OVER_RANGE');
  assert.equal(faultOverRange.isFault, true);
  assert.ok(faultOverRange.label.includes('lưu lượng'));

  const faultCustom = getOutcomeConfig('FAULT_SENSOR_DRIFT');
  assert.equal(faultCustom.isFault, true);
  assert.ok(faultCustom.bgClass.includes('bg-danger'));
});

test('S4-DS-ICON-14: Zero emoji in entire src/ directory', () => {
  const files = getAllFiles('src');
  const emojiRegex = /[\u{1F300}-\u{1FAFF}]/u;

  for (const file of files) {
    const content = readFileSync(file, 'utf8');
    const match = content.match(emojiRegex);
    assert.equal(
      match,
      null,
      `Forbidden emoji found in ${file}: ${match ? match[0] : ''}`
    );
  }
});

test('S4-API-05: Zero hardcoded host:port in src/ directory', () => {
  const files = getAllFiles('src');

  for (const file of files) {
    const content = readFileSync(file, 'utf8');
    assert.ok(
      !content.includes('localhost:3001'),
      `Hardcoded localhost:3001 found in ${file}`
    );
    assert.ok(
      !content.includes('127.0.0.1:3001'),
      `Hardcoded 127.0.0.1:3001 found in ${file}`
    );
  }
});

test('S4-WS-04: Zero socket.io and zero location.reload in src/', () => {
  const files = getAllFiles('src');

  for (const file of files) {
    const content = readFileSync(file, 'utf8');
    assert.ok(
      !content.includes('socket.io'),
      `Socket.io import or reference found in ${file}`
    );
    assert.ok(
      !content.includes('location.reload'),
      `location.reload call found in ${file}`
    );
  }
});

test('S4-ON-DEMAND-11: Zero setInterval polling for measurement triggers in src/', () => {
  const files = getAllFiles('src');
  const forbiddenTriggerPolling = /setInterval.*trigger|trigger.*setInterval/i;

  for (const file of files) {
    const lines = readFileSync(file, 'utf8').split('\n');
    for (let i = 0; i < lines.length; i++) {
      const line = lines[i];
      assert.ok(
        !forbiddenTriggerPolling.test(line),
        `Forbidden setInterval trigger polling found on line ${i + 1} of ${file}: ${line}`
      );
    }
  }
});

test('S4-DS-TOUCH-15: Touch targets >=44px and active:scale-95 on interactive panels', () => {
  const panelFiles = [
    'src/components/dashboard/SeasonPanel.tsx',
    'src/components/dashboard/GroupCard.tsx',
    'src/components/dashboard/NodeCard.tsx',
    'src/components/dashboard/TreatmentPanel.tsx',
    'src/components/dashboard/MeasurementPanel.tsx',
    'src/components/common/WsBanner.tsx',
  ];

  for (const file of panelFiles) {
    const content = readFileSync(file, 'utf8');
    assert.ok(
      content.includes('active:scale-95'),
      `${file} missing active:scale-95 tactile micro-interaction`
    );
    assert.ok(
      content.includes('min-h-[44px]') || content.includes('min-h-[48px]') || content.includes('btn-primary') || content.includes('btn-secondary'),
      `${file} missing minimum touch target classes`
    );
  }
});
