/** Modern control-plane node IDs supported by the production domain. */
export const MODERN_NODE_IDS = [
  1, 2, 3, 4, 5, 6, 7, 8,
  9, 10, 11, 12, 13, 14, 15,
] as const;
export type ModernNodeId = (typeof MODERN_NODE_IDS)[number];

export function isModernNodeId(nodeId: number): nodeId is ModernNodeId {
  return Number.isInteger(nodeId) && nodeId >= 1 && nodeId <= 15;
}

/** Physical RF IDs used by the AGU legacy SCI host. */
export const AGU_LEGACY_NODE_IDS = [4, 5, 6, 7] as const;
export type AguLegacyNodeId = (typeof AGU_LEGACY_NODE_IDS)[number];

export function isAguLegacyNodeId(nodeId: number): nodeId is AguLegacyNodeId {
  return AGU_LEGACY_NODE_IDS.includes(nodeId as AguLegacyNodeId);
}
