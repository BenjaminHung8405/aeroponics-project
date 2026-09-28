import {
  AGU_LEGACY_NODE_IDS,
  MODERN_NODE_IDS,
  isAguLegacyNodeId,
  isModernNodeId,
} from './node-topology';

describe('Node topology', () => {
  it('defines aligned modern and AGU legacy node ranges (1..15)', () => {
    expect(MODERN_NODE_IDS).toEqual([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15]);
    expect(AGU_LEGACY_NODE_IDS).toEqual([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15]);
    expect(AGU_LEGACY_NODE_IDS.every(isAguLegacyNodeId)).toBe(true);
  });

  it.each([1, 4, 7, 8, 15])('accepts modern node %s', (nodeId) => {
    expect(isModernNodeId(nodeId)).toBe(true);
    expect(isAguLegacyNodeId(nodeId)).toBe(true);
  });

  it.each([0, 16, 1.5, Number.NaN])('rejects invalid modern node %s', (nodeId) => {
    expect(isModernNodeId(nodeId)).toBe(false);
    expect(isAguLegacyNodeId(nodeId)).toBe(false);
  });
});
