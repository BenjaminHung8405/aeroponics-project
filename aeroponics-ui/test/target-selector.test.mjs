import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

import {
  buildNodeToGroupLookup,
  computeCascadeDisabledNodes,
  reconcileSelectedNodes,
  sanitizeTargetPayload,
  DEFAULT_GROUP_MEMBERSHIP,
} from '../src/lib/target-selector.ts';

// ============================================================================
// Test Suite: Cascade Disable & Payload Deduplication (IIoT Target Selection)
// ============================================================================

test('TargetSelector: buildNodeToGroupLookup builds correct bidirectional map', () => {
  // Test with custom group configuration
  const customGroups = [
    { groupId: 1, nodeIds: [1, 2, 3, 4] },
    { groupId: 2, nodeIds: [5, 6] },
  ];

  const lookup = buildNodeToGroupLookup(customGroups, false);
  assert.equal(lookup.get(1), 1);
  assert.equal(lookup.get(2), 1);
  assert.equal(lookup.get(3), 1);
  assert.equal(lookup.get(4), 1);
  assert.equal(lookup.get(5), 2);
  assert.equal(lookup.get(6), 2);
  assert.equal(lookup.get(7), undefined);

  // Test fallback to default 15-node topology
  const defaultLookup = buildNodeToGroupLookup(null, true);
  assert.equal(defaultLookup.get(1), 1);
  assert.equal(defaultLookup.get(4), 1);
  assert.equal(defaultLookup.get(5), 2);
  assert.equal(defaultLookup.get(8), 2);
  assert.equal(defaultLookup.get(9), 3);
  assert.equal(defaultLookup.get(12), 3);
  assert.equal(defaultLookup.get(13), 4);
  assert.equal(defaultLookup.get(15), 4);
});

// ----------------------------------------------------------------------------
// Test case 1: Tick chọn Group 1 -> Xác nhận Node 1, 2, 3, 4 bị disabled
// ----------------------------------------------------------------------------
test('Test Case 1: Selecting Group 1 disables Node 1, 2, 3, 4 while independent nodes remain enabled', () => {
  const nodeToGroup = buildNodeToGroupLookup(null, true);
  const selectedGroups = [1];

  const disabledMap = computeCascadeDisabledNodes(selectedGroups, nodeToGroup);

  // Verify member nodes of Group 1 are disabled
  for (const nodeId of [1, 2, 3, 4]) {
    assert.equal(
      disabledMap.has(nodeId),
      true,
      `Node ${nodeId} must be in disabledMap when Group 1 is selected`
    );
    const info = disabledMap.get(nodeId);
    assert.equal(info.disabled, true);
    assert.equal(info.includedInGroupId, 1);
    assert.match(info.reason, /Nhóm 1/);
  }

  // Verify member nodes of other groups (5..15) are NOT disabled
  for (let nodeId = 5; nodeId <= 15; nodeId++) {
    assert.equal(
      disabledMap.has(nodeId),
      false,
      `Node ${nodeId} should remain enabled when only Group 1 is selected`
    );
  }
});

// ----------------------------------------------------------------------------
// Test case 2: Tick chọn Node 1 trước -> Sau đó tick Group 1 -> Node 1 bị xóa
// ----------------------------------------------------------------------------
test('Test Case 2: Selecting Node 1 first, then ticking Group 1 automatically removes Node 1 from selectedNodes and disables it', () => {
  const nodeToGroup = buildNodeToGroupLookup(null, true);

  // Step 1: User initially selected Node 1 along with independent nodes (e.g., Node 5, 9)
  const initialSelectedNodes = [1, 5, 9];

  // Step 2: User ticks Group 1
  const selectedGroups = [1];
  const disabledMap = computeCascadeDisabledNodes(selectedGroups, nodeToGroup);

  // Step 3: Reconcile selection state
  const reconciledNodes = reconcileSelectedNodes(initialSelectedNodes, disabledMap);

  // Assertions:
  // Node 1 must be removed from selectedNodes
  assert.equal(
    reconciledNodes.includes(1),
    false,
    'Node 1 must be removed from selectedNodes because its parent Group 1 was selected'
  );
  // Independent nodes 5 and 9 must remain preserved
  assert.deepEqual(reconciledNodes, [5, 9]);

  // Node 1 is also confirmed to be disabled
  assert.equal(disabledMap.get(1)?.disabled, true);
});

// ----------------------------------------------------------------------------
// Test case 3: Bỏ tick Group 1 -> Xác nhận Node 1, 2, 3, 4 được enable trở lại
// ----------------------------------------------------------------------------
test('Test Case 3: Deselecting Group 1 unlocks Nodes 1, 2, 3, 4 so they can be individually selected', () => {
  const nodeToGroup = buildNodeToGroupLookup(null, true);

  // User deselects Group 1
  const selectedGroups = [];
  const disabledMap = computeCascadeDisabledNodes(selectedGroups, nodeToGroup);

  // All nodes must be enabled
  assert.equal(disabledMap.size, 0, 'No nodes should be disabled when selectedGroups is empty');
  for (const nodeId of [1, 2, 3, 4]) {
    assert.equal(disabledMap.has(nodeId), false, `Node ${nodeId} must be unlocked`);
  }

  // Simulating user now ticking Node 2 individually
  const nextSelectedNodes = reconcileSelectedNodes([2], disabledMap);
  assert.deepEqual(nextSelectedNodes, [2]);
});

// ----------------------------------------------------------------------------
// Test case 4: Submit form -> Vệ sinh payload không trùng lặp giữa Group và Node con
// ----------------------------------------------------------------------------
test('Test Case 4: Payload Sanitization Guard strips any child nodes belonging to target_groups before API dispatch', () => {
  const nodeToGroup = buildNodeToGroupLookup(null, true);

  // Dirty payload where Group 1 is selected, but Node 1, 2 (members of Group 1)
  // are also mistakenly in target_nodes alongside independent Node 5, 6
  const dirtyPayload = {
    target_groups: [1, 1], // Contains duplicate group ID
    target_nodes: [1, 2, 5, 6, 5], // Contains duplicate and overlapped nodes
  };

  const cleanPayload = sanitizeTargetPayload(dirtyPayload, nodeToGroup);

  // 1. Group IDs must be unique and sorted
  assert.deepEqual(cleanPayload.target_groups, [1]);

  // 2. Nodes 1 and 2 MUST be completely eliminated because Group 1 is in target_groups
  assert.equal(cleanPayload.target_nodes.includes(1), false, 'Node 1 must be stripped');
  assert.equal(cleanPayload.target_nodes.includes(2), false, 'Node 2 must be stripped');

  // 3. Only truly independent nodes (Node 5, 6 belonging to Group 2) remain, unique & sorted
  assert.deepEqual(cleanPayload.target_nodes, [5, 6]);
});

test('Test Case 4 (Multi-Group overlap): Complex scenario with multiple groups and mixed nodes', () => {
  const nodeToGroup = buildNodeToGroupLookup(null, true);

  // User selected Group 1 and Group 3
  // Nodes belonging to Group 1: 1, 2, 3, 4
  // Nodes belonging to Group 3: 9, 10, 11, 12
  const dirtyPayload = {
    target_groups: [3, 1],
    target_nodes: [1, 4, 7, 8, 10, 14, 15],
  };

  const clean = sanitizeTargetPayload(dirtyPayload, nodeToGroup);

  // target_groups sorted
  assert.deepEqual(clean.target_groups, [1, 3]);

  // Stripped nodes: 1, 4 (belong to G1), 10 (belongs to G3)
  // Surviving independent nodes: 7, 8 (belong to G2), 14, 15 (belong to G4)
  assert.deepEqual(clean.target_nodes, [7, 8, 14, 15]);
});

// ----------------------------------------------------------------------------
// Test case 5: Design System, Ergonomics & Zero Emoji Compliance
// ----------------------------------------------------------------------------
test('Design System & Code Quality Audit for Target Selector components', () => {
  const selectorFile = 'src/components/control/NodeGroupSelector.tsx';
  const modalFile = 'src/components/control/BatchControlModal.tsx';
  const libFile = 'src/lib/target-selector.ts';

  const files = [selectorFile, modalFile, libFile];
  const emojiRegex = /[\u{1F300}-\u{1FAFF}]/u;

  for (const f of files) {
    const content = readFileSync(f, 'utf8');
    assert.equal(
      content.match(emojiRegex),
      null,
      `Forbidden emoji found in ${f}`
    );
  }

  // Component touch targets & accessibility
  const selectorContent = readFileSync(selectorFile, 'utf8');
  assert.ok(selectorContent.includes('min-h-['), 'NodeGroupSelector must specify min-h for touch targets');
  assert.ok(selectorContent.includes('active:scale-95'), 'NodeGroupSelector must include tactile active feedback');
  assert.ok(selectorContent.includes('role="checkbox"'), 'Interactive targets must declare accessibility roles');
  assert.ok(selectorContent.includes('aria-checked'), 'Checkboxes must declare aria-checked state');
  assert.ok(selectorContent.includes('aria-disabled'), 'Disabled targets must declare aria-disabled');

  const modalContent = readFileSync(modalFile, 'utf8');
  assert.ok(modalContent.includes('sanitizeTargetPayload'), 'BatchControlModal must invoke sanitizeTargetPayload before dispatch');
});
