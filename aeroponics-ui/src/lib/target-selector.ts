/**
 * Target Selector Pure Logic & Payload Sanitization
 *
 * Implements:
 *  - Hierarchical Cascade Disable: When a group is selected, all member nodes are disabled.
 *  - Automatic Deselection: Selected nodes belonging to a newly-selected group are removed.
 *  - Sanitization Guard: Before sending payloads to API or MQTT, filters out any node ID
 *    whose parent group is already present in target_groups.
 *
 * This prevents dual RF Broadcast (0x10..0x40) + Unicast collision on the 433MHz bus
 * and protects the safety FSM on embedded relays.
 */

export interface TargetSelectionPayload {
  target_groups: number[];
  target_nodes: number[];
}

export interface NodeGroupMembership {
  groupId: number;
  nodeIds: number[];
}

/**
 * Standard fallback topology for 15 modern nodes mapped to 4 logical timer groups.
 * Used when backend group membership is not yet populated.
 */
export const DEFAULT_GROUP_MEMBERSHIP: Record<number, number[]> = Object.freeze({
  1: [1, 2, 3, 4],
  2: [5, 6, 7, 8],
  3: [9, 10, 11, 12],
  4: [13, 14, 15],
});

/**
 * Builds a fast lookup Map of nodeId -> groupId.
 *
 * @param groups Object or array containing group definitions with nodeIds.
 * @param fallbackToDefault If true, fills missing groups from DEFAULT_GROUP_MEMBERSHIP.
 */
export function buildNodeToGroupLookup(
  groups?: Record<number, { nodeIds?: number[] }> | NodeGroupMembership[] | null,
  fallbackToDefault: boolean = true,
): Map<number, number> {
  const lookup = new Map<number, number>();

  if (groups) {
    const list = Array.isArray(groups) ? groups : Object.entries(groups).map(([k, v]) => ({
      groupId: Number(k),
      nodeIds: v?.nodeIds ?? [],
    }));

    for (const item of list) {
      if (item && Array.isArray(item.nodeIds) && item.nodeIds.length > 0) {
        for (const nodeId of item.nodeIds) {
          lookup.set(nodeId, item.groupId);
        }
      }
    }
  }

  // Fallback to default layout if lookup is empty and fallback is requested
  if (lookup.size === 0 && fallbackToDefault) {
    for (const [groupIdStr, nodeIds] of Object.entries(DEFAULT_GROUP_MEMBERSHIP)) {
      const gid = Number(groupIdStr);
      for (const nid of nodeIds) {
        lookup.set(nid, gid);
      }
    }
  }

  return lookup;
}

export interface DisabledNodeInfo {
  disabled: boolean;
  includedInGroupId: number | null;
  reason?: string;
}

/**
 * Computes disabled state for all nodes based on currently selected groups.
 *
 * @param selectedGroups Array of currently selected group IDs.
 * @param nodeToGroupMap Map of nodeId -> parentGroupId.
 * @returns Map of nodeId -> DisabledNodeInfo.
 */
export function computeCascadeDisabledNodes(
  selectedGroups: number[],
  nodeToGroupMap: Map<number, number>,
): Map<number, DisabledNodeInfo> {
  const disabledMap = new Map<number, DisabledNodeInfo>();
  const activeGroupSet = new Set(selectedGroups);

  for (const [nodeId, parentGroupId] of nodeToGroupMap.entries()) {
    if (activeGroupSet.has(parentGroupId)) {
      disabledMap.set(nodeId, {
        disabled: true,
        includedInGroupId: parentGroupId,
        reason: `Đã bao gồm trong Nhóm ${parentGroupId}`,
      });
    }
  }

  return disabledMap;
}

/**
 * Reconciles selected nodes by removing any node that has become disabled
 * because its parent group was selected.
 *
 * @param selectedNodes Current list of selected node IDs.
 * @param disabledNodeMap Map of disabled nodes computed from computeCascadeDisabledNodes.
 * @returns Filtered, sorted array of node IDs.
 */
export function reconcileSelectedNodes(
  selectedNodes: number[],
  disabledNodeMap: Map<number, DisabledNodeInfo>,
): number[] {
  return selectedNodes
    .filter((nodeId) => !disabledNodeMap.has(nodeId))
    .sort((a, b) => a - b);
}

/**
 * Sanitization Guard: Enforces payload deduplication before dispatching commands
 * to the API or MQTT broker.
 *
 * Guarantees:
 *  1. target_groups contains unique, positive, sorted integer group IDs.
 *  2. target_nodes contains unique, positive, sorted integer node IDs.
 *  3. Any node ID whose parent group is in target_groups is strictly REMOVED.
 */
export function sanitizeTargetPayload(
  payload: {
    target_groups?: (number | string)[] | null;
    target_nodes?: (number | string)[] | null;
  },
  nodeToGroupMap: Map<number, number>,
): TargetSelectionPayload {
  const rawGroups = payload?.target_groups ?? [];
  const rawNodes = payload?.target_nodes ?? [];

  const safeGroups = Array.from(
    new Set(
      rawGroups
        .map((g) => Number(g))
        .filter((g) => Number.isInteger(g) && g > 0),
    ),
  ).sort((a, b) => a - b);

  const groupSet = new Set(safeGroups);

  const safeNodes = Array.from(
    new Set(
      rawNodes
        .map((n) => Number(n))
        .filter((n) => Number.isInteger(n) && n > 0),
    ),
  )
    .filter((nodeId) => {
      const parentGroup = nodeToGroupMap.get(nodeId);
      // Strip node if its parent group is present in target_groups
      if (parentGroup !== undefined && groupSet.has(parentGroup)) {
        return false;
      }
      return true;
    })
    .sort((a, b) => a - b);

  return {
    target_groups: safeGroups,
    target_nodes: safeNodes,
  };
}
