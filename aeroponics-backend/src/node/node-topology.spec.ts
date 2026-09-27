import {
  AGU_LEGACY_NODE_IDS,
  MODERN_NODE_IDS,
  isAguLegacyNodeId,
  isModernNodeId,
} from './node-topology';

describe('Node topology', () => {
  it('defines distinct modern and AGU legacy node ranges', () => {
    expect(MODERN_NODE_IDS).toEqual([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15]);
    expect(AGU_LEGACY_NODE_IDS).toEqual([4, 5, 6, 7]);
    expect(MODERN_NODE_IDS).not.toEqual(AGU_LEGACY_NODE_IDS);
  });

  it.each([1, 4, 7, 15])('accepts modern node %s', (nodeId) => {
    expect(isModernNodeId(nodeId)).toBe(true);
  });

  it.each([0, 16, 1.5, Number.NaN])('rejects invalid modern node %s', (nodeId) => {
    expect(isModernNodeId(nodeId)).toBe(false);
  });

  it('keeps the legacy AGU predicate restricted to 4..7', () => {
    expect(AGU_LEGACY_NODE_IDS.every(isAguLegacyNodeId)).toBe(true);
    expect(isAguLegacyNodeId(1)).toBe(false);
    expect(isAguLegacyNodeId(15)).toBe(false);
  });
});
