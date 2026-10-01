'use client';

import React, { useMemo, useCallback } from 'react';
import { useAllGroups } from '../../store/useGroupStore';
import { useNodeStore, MODERN_NODE_IDS } from '../../store/useNodeStore';
import {
  buildNodeToGroupLookup,
  computeCascadeDisabledNodes,
  reconcileSelectedNodes,
} from '../../lib/target-selector';
import { Layers, Cpu, CheckCircle2, ShieldCheck, Info } from 'lucide-react';

export interface NodeGroupSelectorProps {
  selectedGroups: number[];
  selectedNodes: number[];
  onChange: (targets: { selectedGroups: number[]; selectedNodes: number[] }) => void;
  disabled?: boolean;
}

/**
 * NodeGroupSelector Component
 *
 * Implements:
 *  - Hierarchical Cascade Disable: Disables all member nodes when their parent group is selected.
 *  - Reactive Auto-Deselection: Automatically deselects previously chosen nodes when their group is checked.
 *  - Clear Badge Annotations: Informs users why a node cannot be individually toggled.
 *  - Industrial Safety: Prevents RF 433MHz broadcast + unicast collisions.
 *  - Mobile Touch Ergonomics: >= 44px touch targets with active:scale-95.
 */
export function NodeGroupSelector({
  selectedGroups,
  selectedNodes,
  onChange,
  disabled = false,
}: NodeGroupSelectorProps) {
  const groups = useAllGroups();
  const nodeStates = useNodeStore((state) => state.nodes);

  // 1. Build lookup map: nodeId -> groupId
  const nodeToGroupMap = useMemo(() => {
    return buildNodeToGroupLookup(groups, true);
  }, [groups]);

  // 2. Compute cascade disabled nodes from currently selected groups
  const disabledNodeMap = useMemo(() => {
    return computeCascadeDisabledNodes(selectedGroups, nodeToGroupMap);
  }, [selectedGroups, nodeToGroupMap]);

  // 3. Handle Group Toggle with automatic cascade deselection
  const handleToggleGroup = useCallback(
    (groupId: number) => {
      if (disabled) return;

      const isCurrentlySelected = selectedGroups.includes(groupId);
      const nextGroups = isCurrentlySelected
        ? selectedGroups.filter((id) => id !== groupId)
        : [...selectedGroups, groupId].sort((a, b) => a - b);

      // Recompute disabled nodes for the next group selection
      const nextDisabledMap = computeCascadeDisabledNodes(nextGroups, nodeToGroupMap);

      // Remove any nodes that are now covered by newly selected groups
      const nextNodes = reconcileSelectedNodes(selectedNodes, nextDisabledMap);

      onChange({
        selectedGroups: nextGroups,
        selectedNodes: nextNodes,
      });
    },
    [disabled, selectedGroups, selectedNodes, nodeToGroupMap, onChange],
  );

  // 4. Handle Node Toggle
  const handleToggleNode = useCallback(
    (nodeId: number) => {
      if (disabled || disabledNodeMap.has(nodeId)) return;

      const isCurrentlySelected = selectedNodes.includes(nodeId);
      const nextNodes = isCurrentlySelected
        ? selectedNodes.filter((id) => id !== nodeId)
        : [...selectedNodes, nodeId].sort((a, b) => a - b);

      onChange({
        selectedGroups,
        selectedNodes: nextNodes,
      });
    },
    [disabled, disabledNodeMap, selectedGroups, selectedNodes, onChange],
  );

  return (
    <div className="space-y-5" data-testid="node-group-selector">
      {/* SECTION 1: Group Targets (Broadcast 0x10..0x40) */}
      <section aria-labelledby="target-groups-heading" className="space-y-2.5">
        <div className="flex items-center justify-between">
          <div className="flex items-center gap-2">
            <Layers size={16} className="text-primary shrink-0" aria-hidden="true" />
            <h4 id="target-groups-heading" className="text-xs font-bold uppercase tracking-wider text-text">
              Nhóm Mục Tiêu (Group Broadcast)
            </h4>
          </div>
          <span className="text-[11px] text-text-subtle">
            Đã chọn {selectedGroups.length}/4 nhóm
          </span>
        </div>

        <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-4 gap-2.5">
          {[1, 2, 3, 4].map((groupId) => {
            const groupData = groups.find((g) => g.groupId === groupId);
            const memberIds = groupData?.nodeIds && groupData.nodeIds.length > 0
              ? groupData.nodeIds
              : (nodeToGroupMap.size > 0
                  ? Array.from(nodeToGroupMap.entries())
                      .filter(([_, gid]) => gid === groupId)
                      .map(([nid]) => nid)
                  : []);
            const isChecked = selectedGroups.includes(groupId);

            return (
              <button
                key={groupId}
                type="button"
                role="checkbox"
                aria-checked={isChecked}
                disabled={disabled}
                onClick={() => handleToggleGroup(groupId)}
                data-testid={`group-select-${groupId}`}
                className={`p-3 rounded-xl border text-left flex flex-col justify-between min-h-[58px] transition-all duration-150 cursor-pointer active:scale-95 disabled:opacity-50 disabled:cursor-not-allowed ${
                  isChecked
                    ? 'bg-primary/20 border-primary shadow-sm shadow-primary/20 text-text'
                    : 'bg-surface/50 border-border/40 hover:bg-surface/80 text-text-muted hover:text-text'
                }`}
              >
                <div className="flex items-center justify-between w-full">
                  <span className="text-xs font-bold">
                    {groupData?.name || `Nhóm #${groupId}`}
                  </span>
                  {isChecked ? (
                    <CheckCircle2 size={16} className="text-primary shrink-0" aria-hidden="true" />
                  ) : (
                    <span className="w-4 h-4 rounded-full border border-border/60 shrink-0" />
                  )}
                </div>

                <div className="flex items-center gap-1.5 mt-2 text-[10px] text-text-subtle">
                  <span className="font-mono">
                    {memberIds.length > 0
                      ? `${memberIds.length} trạm: [${memberIds.join(', ')}]`
                      : '0 trạm phụ trách'}
                  </span>
                </div>
              </button>
            );
          })}
        </div>
      </section>

      {/* SECTION 2: Individual Node Targets (Unicast 1..15) */}
      <section aria-labelledby="target-nodes-heading" className="space-y-2.5">
        <div className="flex items-center justify-between">
          <div className="flex items-center gap-2">
            <Cpu size={16} className="text-primary shrink-0" aria-hidden="true" />
            <h4 id="target-nodes-heading" className="text-xs font-bold uppercase tracking-wider text-text">
              Trạm Khí Canh Lẻ (Node Unicast)
            </h4>
          </div>
          <span className="text-[11px] text-text-subtle">
            Đã chọn {selectedNodes.length} trạm độc lập
          </span>
        </div>

        <div className="grid grid-cols-2 sm:grid-cols-3 md:grid-cols-5 gap-2">
          {MODERN_NODE_IDS.map((nodeId) => {
            const isChecked = selectedNodes.includes(nodeId);
            const disabledInfo = disabledNodeMap.get(nodeId);
            const isCascadeDisabled = Boolean(disabledInfo);
            const isBtnDisabled = disabled || isCascadeDisabled;
            const parentGroupId = nodeToGroupMap.get(nodeId);
            const isOnline = Boolean(
              nodeStates[nodeId]?.lastSeenAt &&
              !nodeStates[nodeId]?.isStale &&
              ['ONLINE', 'DISCOVERED'].includes(nodeStates[nodeId]?.discoveryStatus ?? '')
            );

            return (
              <button
                key={nodeId}
                type="button"
                role="checkbox"
                aria-checked={isChecked}
                aria-disabled={isCascadeDisabled}
                disabled={isBtnDisabled}
                onClick={() => handleToggleNode(nodeId)}
                data-testid={`node-select-${nodeId}`}
                title={
                  disabledInfo
                    ? disabledInfo.reason
                    : `Trạm #${nodeId} (Nhóm ${parentGroupId ?? '?'})`
                }
                className={`p-2.5 rounded-xl border text-left flex flex-col justify-between min-h-[56px] transition-all duration-150 ${
                  isCascadeDisabled
                    ? 'opacity-40 cursor-not-allowed bg-surface/20 border-border/20 text-text-subtle'
                    : isChecked
                    ? 'bg-primary/20 border-primary text-text active:scale-95 cursor-pointer shadow-sm shadow-primary/20'
                    : 'bg-surface/40 border-border/30 hover:bg-surface/70 text-text-muted hover:text-text active:scale-95 cursor-pointer'
                }`}
              >
                <div className="flex items-center justify-between w-full">
                  <span className="text-xs font-semibold">
                    Trạm #{nodeId.toString().padStart(2, '0')}
                  </span>
                  {isChecked ? (
                    <CheckCircle2 size={14} className="text-primary shrink-0" aria-hidden="true" />
                  ) : isCascadeDisabled ? (
                    <ShieldCheck size={14} className="text-accent-amber shrink-0" aria-hidden="true" />
                  ) : (
                    <span className="w-3.5 h-3.5 rounded border border-border/50 shrink-0" />
                  )}
                </div>

                <div className="mt-1 flex items-center justify-between gap-1 w-full">
                  {isCascadeDisabled ? (
                    <span
                      data-testid={`node-badge-included-${nodeId}`}
                      className="inline-flex items-center gap-1 text-[9px] font-semibold text-accent-amber bg-accent-amber/15 px-1 py-0.5 rounded border border-accent-amber/30 truncate"
                    >
                      Nhóm #{disabledInfo?.includedInGroupId}
                    </span>
                  ) : (
                    <span className="text-[10px] text-text-subtle truncate">
                      {isOnline ? 'Online' : 'Offline'}
                    </span>
                  )}

                  {parentGroupId && !isCascadeDisabled && (
                    <span className="text-[9px] text-text-subtle font-mono">
                      G#{parentGroupId}
                    </span>
                  )}
                </div>
              </button>
            );
          })}
        </div>

        {/* Informative Hint */}
        {selectedGroups.length > 0 && (
          <div className="flex items-start gap-2 p-2.5 rounded-xl bg-accent-indigo/10 border border-accent-indigo/25 text-accent-indigo text-xs">
            <Info size={15} className="shrink-0 mt-0.5" aria-hidden="true" />
            <p className="leading-snug text-[11px]">
              <strong>Bảo vệ phân cấp RF:</strong> Các trạm con thuộc các nhóm đã chọn được tự động vô hiệu hóa để tránh phát lệnh kép Unicast + Broadcast, bảo vệ relay và chống nghẽn sóng 433MHz.
            </p>
          </div>
        )}
      </section>
    </div>
  );
}
