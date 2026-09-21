/** Physical RF IDs used by the AGU legacy SCI host. */
export const AGU_LEGACY_NODE_IDS = [4, 5, 6, 7] as const;
export type AguLegacyNodeId = (typeof AGU_LEGACY_NODE_IDS)[number];

export function isAguLegacyNodeId(nodeId: number): nodeId is AguLegacyNodeId {
  return AGU_LEGACY_NODE_IDS.includes(nodeId as AguLegacyNodeId);
}
