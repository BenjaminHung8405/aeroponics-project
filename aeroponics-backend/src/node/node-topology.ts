/**
 * Physical RF IDs used by the AGU legacy SCI host.
 *
 * @blocker TASK U-2 (Sprint 3) — PRODUCTION BLOCKER
 * Wire contract §6 item 163 specifies production nodes as 1..4,
 * NOT [4,5,6,7]. This discrepancy is a known integration blocker.
 * DO NOT change these IDs until a signed topology/adapter decision
 * is received. Ref: docs/interface-wire-contract.md §6 item 163.
 */
export const AGU_LEGACY_NODE_IDS = [4, 5, 6, 7] as const;
export type AguLegacyNodeId = (typeof AGU_LEGACY_NODE_IDS)[number];

export function isAguLegacyNodeId(nodeId: number): nodeId is AguLegacyNodeId {
  return AGU_LEGACY_NODE_IDS.includes(nodeId as AguLegacyNodeId);
}
