import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

test('dashboard uses four dynamic control slots', () => {
  const page = readFileSync('src/app/dashboard/page.tsx', 'utf8');
  const grid = readFileSync('src/components/dashboard/ControlSlotGrid.tsx', 'utf8');
  const card = readFileSync('src/components/dashboard/ControlSlotCard.tsx', 'utf8');
  assert.match(page, /ControlSlotGrid/);
  assert.match(grid, /\[1, 2, 3, 4\]/);
  assert.match(card, /target_type/);
  assert.match(card, /usedTargets/);
  assert.match(card, /window\.confirm/);
});

test('node overrides never include a group target', () => {
  const hook = readFileSync('src/hooks/queries/useNodes.ts', 'utf8');
  const pump = readFileSync('src/components/dashboard/PumpControl.tsx', 'utf8');
  assert.match(hook, /target_type === 'GROUP'/);
  assert.doesNotMatch(pump, /group_id:/);
  assert.match(pump, /target_type: 'NODE'/);
  assert.match(hook, /const \{ group_id: _groupId, \.\.\.nodeCommand \} = dto/);
});

test('node slot disables controls for offline or uncommissioned nodes and cascade-disables when parent group is active in another slot', () => {
  const card = readFileSync('src/components/dashboard/ControlSlotCard.tsx', 'utf8');
  assert.match(card, /!node\.lastSeenAt/);
  assert.match(card, /node\.isStale/);
  assert.match(card, /ONLINE', 'DISCOVERED'/);
  assert.match(card, /<NodeCard\s+nodeId=\{Number\(targetId\)\}\s+disabled=\{isOffline \|\| isNodeUnavailable \|\| isNodeCoveredByOtherGroup\}/);
  assert.match(card, /isCoveredByGroup/);
  assert.match(card, /Đã bao gồm trong Nhóm/);
});

test('group ON confirms while OFF does not', () => {
  const card = readFileSync('src/components/dashboard/ControlSlotCard.tsx', 'utf8');
  const onStart = card.indexOf("action: 'ON'");
  const offStart = card.indexOf("action: 'OFF'");
  assert.ok(onStart > 0 && offStart > onStart);
  assert.ok(card.lastIndexOf('window.confirm', onStart) > card.lastIndexOf('action: \'GROUP\'', onStart - 1));
  assert.equal(card.slice(offStart - 350, offStart).includes('window.confirm'), false);
});
