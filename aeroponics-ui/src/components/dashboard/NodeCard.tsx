'use client';

import React, { useState } from 'react';
import { useNode } from '../../store/useNodeStore';
import { useGroup } from '../../store/useGroupStore';
import { isNodeRunning } from '../../lib/types';
import { StalenessIndicator } from '../common/StalenessIndicator';
import { OutcomeBadge } from '../common/OutcomeBadge';
import { NodeDetailModal } from './NodeDetailModal';
import { PumpControl } from './PumpControl';
import { Droplets, Activity, ChevronRight, Leaf } from 'lucide-react';

interface NodeCardProps {
  nodeId: number;
  disabled?: boolean;
}

/**
 * NodeCard Component
 * Follows:
 *  - S4-D3: Actuator Node card with staleness dot, outcome badge, and flow metrics.
 *  - S4-DS-COLOR-13: Active pump glow `.relay-glow-active` when FLOW_CONFIRMED.
 *  - S4-DS-FONT-12: font-mono tabular-nums for flow rate and total litres.
 *  - Granular subscription via useNode(nodeId) to eliminate cross-node re-renders.
 */
export function NodeCard({ nodeId, disabled = false }: NodeCardProps) {
  const node = useNode(nodeId);
  const [isDetailOpen, setIsDetailOpen] = useState(false);

  // Active mist spraying / pump running glow
  // S4-NOOPT-01: Glow only when node is running (flowConfirmed + FLOW_CONFIRMED from WS),
  //           NOT from scheduleState or outcome alone.
  const isRunning = isNodeRunning(node);
  const group = useGroup(node.cachedGroupId ?? 0);

  return (
    <>
      <div
        className={`glass-card p-4 sm:p-5 flex flex-col justify-between h-full min-h-[220px] space-y-4 transition-all duration-200 ${
          isRunning ? 'relay-glow-active' : ''
        }`}
      >
        {/* Card Header: Name, Staleness Dot, Group Tag */}
        <div>
          <div className="flex items-center justify-between gap-2 mb-1.5">
            <div className="flex items-center gap-2">
              <StalenessIndicator
                lastSeenAt={node.lastSeenAt}
                isStale={node.isStale}
                staleForMs={node.staleForMs}
              />
              <h3 className="text-base sm:text-lg font-bold text-text">
                {node.displayName}
              </h3>
            </div>

            <span className="text-[11px] font-semibold px-2 py-0.5 rounded-full bg-surface border border-border/30 text-text-muted">
              {node.cachedGroupId ? `Nhóm #${node.cachedGroupId}` : 'Chưa gán'}
            </span>
          </div>

          <div className="flex items-center justify-between gap-2 mt-2">
            <OutcomeBadge outcome={node.outcome} nodeFlowConfirmed={node.flowConfirmed} />

            <div className="flex items-center gap-1.5">
              {node.overrideState && node.overrideState !== 'NONE' && (
                <span
                  className={`text-[10px] font-bold px-1.5 py-0.5 rounded border uppercase tracking-wider ${
                    node.overrideState === 'OVERRIDE_ON'
                      ? 'bg-primary/25 text-primary border-primary/50 animate-pulse'
                      : 'bg-accent-amber/20 text-accent-amber border-accent-amber/50'
                  }`}
                >
                  {node.overrideState === 'OVERRIDE_ON' ? 'OVR BẬT' : 'OVR TẮT'}
                </span>
              )}

              <span
                className={`text-[11px] font-semibold px-2 py-0.5 rounded border uppercase tracking-wider ${
                  node.scheduleState === 'SPRAYING'
                    ? 'bg-primary/20 text-primary border-primary/40'
                    : node.scheduleState === 'COOLDOWN'
                      ? 'bg-accent-amber/15 text-accent-amber border-accent-amber/40'
                      : 'bg-surface/50 text-text-subtle border-border/20'
                }`}
              >
                {node.scheduleState || 'IDLE'}
              </span>
            </div>
          </div>
        </div>

        {/* Center: Live Flow Metrics */}
        <div className="p-3 rounded-xl bg-background/50 border border-border/20 space-y-2">
          <div className="flex items-baseline justify-between">
            <span className="text-xs text-text-muted flex items-center gap-1">
              <Droplets size={13} className="text-primary" aria-hidden="true" />
              <span>Lưu lượng tức thời:</span>
            </span>
            <div className="font-mono tabular-nums text-xl sm:text-2xl font-bold text-primary">
              {node.flowLpm.toFixed(2)}{' '}
              <span className="text-xs font-normal text-text-muted">L/phút</span>
            </div>
          </div>

          <div className="flex items-baseline justify-between border-t border-border/20 pt-1.5">
            <span className="text-xs text-text-muted">Tổng thể tích đã phun:</span>
            <div className="font-mono tabular-nums text-sm font-semibold text-text">
              {node.litresTotal.toFixed(1)}{' '}
              <span className="text-xs font-normal text-text-muted">Lít</span>
            </div>
          </div>
          {group?.treatment && (
            <div className="flex items-center gap-1.5 pt-1.5 border-t border-border/20 text-[11px]">
              <Leaf size={11} className="text-primary shrink-0" aria-hidden="true" />
              <span className="text-text-muted truncate font-medium">
                {group.treatment.treatment_name} v{group.treatment.version_num}
              </span>
              <span className="text-text-subtle font-mono ml-auto shrink-0">
                {group.treatment.spray_day_s}s/{group.treatment.cooldown_day_s}s
              </span>
            </div>
          )}
        </div>

        {/* Pump Control (S4-NOOPT-01: PENDING only, never direct RUNNING) */}
        <div className="flex items-center justify-between gap-2">
          <span className="text-xs font-semibold text-text-muted">Điều khiển bơm:</span>
          <PumpControl nodeId={nodeId} disabled={disabled} />
        </div>

        {/* Footer: Details / Inspection Button */}
        <button
          type="button"
          onClick={() => setIsDetailOpen(true)}
          className="btn-secondary w-full inline-flex items-center justify-between px-3.5 py-2 rounded-xl bg-surface/80 hover:bg-surface border border-border/40 text-xs font-semibold text-text active:scale-95 cursor-pointer transition-all duration-150 min-h-[44px]"
          aria-label={`Xem chi tiết viễn thám ${node.displayName}`}
        >
          <span className="inline-flex items-center gap-1.5">
            <Activity size={14} className="text-primary" aria-hidden="true" />
            <span>Chi tiết trạm &amp; Nhật ký</span>
          </span>
          <ChevronRight size={14} className="text-text-subtle" aria-hidden="true" />
        </button>
      </div>

      <NodeDetailModal
        node={node}
        isOpen={isDetailOpen}
        onClose={() => setIsDetailOpen(false)}
        disabled={disabled}
      />
    </>
  );
}
