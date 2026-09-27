'use client';

import React, { useEffect, useState } from 'react';
import { useControlSlots, useUpdateControlSlot } from '../../hooks/queries/useControlSlots';
import { useNode, useNodeStore } from '../../store/useNodeStore';
import { useGroup, useAllGroups } from '../../store/useGroupStore';
import { useDeviceStore } from '../../store/useDeviceStore';
import { useSendPumpOverride } from '../../hooks/queries/useNodes';
import { useToast } from '../common/Toast';
import { NodeCard } from './NodeCard';
import { GroupCard } from './GroupCard';
import type { ControlSlotTargetType } from '../../lib/types';
import { Loader2, Sliders } from 'lucide-react';

interface ControlSlotCardProps {
  slotIndex: number;
  slots: NonNullable<ReturnType<typeof useControlSlots>['data']>;
}

export function ControlSlotCard({ slotIndex, slots }: ControlSlotCardProps) {
  const slot = slots.find((item) => item.slot_index === slotIndex);
  const updateMutation = useUpdateControlSlot();
  const commandMutation = useSendPumpOverride();
  const { toast } = useToast();
  const deviceStatus = useDeviceStore((state) => state.status);
  const nodeStates = useNodeStore((state) => state.nodes);
  const groups = useAllGroups();
  const [targetType, setTargetType] = useState<ControlSlotTargetType | ''>(slot?.target_type ?? '');
  const [targetId, setTargetId] = useState<string>(slot?.target_id?.toString() ?? '');
  const node = useNode(Number(targetId));
  const group = useGroup(Number(targetId));

  useEffect(() => {
    setTargetType(slot?.target_type ?? '');
    setTargetId(slot?.target_id?.toString() ?? '');
  }, [slot?.target_type, slot?.target_id]);

  const usedTargets = new Set(
    slots
      .filter((item) => item.slot_index !== slotIndex && item.target_type && item.target_id)
      .map((item) => `${item.target_type}:${item.target_id}`),
  );
  const isOffline = deviceStatus !== 'online';
  const isNodeUnavailable = targetType === 'NODE' && (
    !node.lastSeenAt ||
    node.isStale ||
    !['ONLINE', 'DISCOVERED'].includes(node.discoveryStatus ?? '')
  );
  const isGroupUnavailable = targetType === 'GROUP' && (group.status !== 'ACTIVE' || group.nodeIds.length === 0);

  const save = (nextType: ControlSlotTargetType | '', nextId: string) => {
    setTargetType(nextType);
    setTargetId(nextId);
    if (nextType && nextId) {
      updateMutation.mutate({
        slotIndex,
        dto: { target_type: nextType, target_id: Number(nextId) },
      });
    } else if (!nextType && !nextId) {
      updateMutation.mutate({ slotIndex, dto: { target_type: null, target_id: null } });
    }
  };

  return (
    <article className="space-y-3">
      <div className="flex items-center justify-between gap-2">
        <div className="flex items-center gap-2">
          <div className="p-1.5 rounded-lg bg-surface/80 border border-border/30 text-text-muted">
            <Sliders size={16} aria-hidden="true" />
          </div>
          <h3 className="text-sm font-bold text-text">Khe điều khiển {slotIndex}</h3>
        </div>
        {updateMutation.isPending && <Loader2 size={14} className="animate-spin text-primary" aria-label="Đang lưu" />}
      </div>

      <div className="glass-card p-4 space-y-3">
        <label className="block text-xs font-semibold text-text-muted">
          Thiết bị đích
          <select
            aria-label={`Đích khe ${slotIndex}`}
            value={targetType && targetId ? `${targetType}:${targetId}` : ''}
            onChange={(event) => {
              const [nextType, nextId] = event.target.value.split(':');
              save((nextType as ControlSlotTargetType) || '', nextId || '');
            }}
            className="mt-1 w-full min-h-[44px] rounded-lg bg-background border border-border/40 px-3 text-sm text-text"
          >
            <option value="">Chưa gán</option>
            <optgroup label="Node">
              {Array.from({ length: 15 }, (_, index) => index + 1).map((id) => (
                <option key={id} value={`NODE:${id}`} disabled={usedTargets.has(`NODE:${id}`)}>
                  Node {id.toString().padStart(2, '0')} ({nodeStates[id]?.lastSeenAt && !nodeStates[id]?.isStale ? 'online' : 'chưa commissioning/offline'})
                </option>
              ))}
            </optgroup>
            <optgroup label="Nhóm">
              {[1, 2, 3, 4].map((id) => (
                <option key={id} value={`GROUP:${id}`} disabled={usedTargets.has(`GROUP:${id}`)}>
                  Nhóm {id} ({groups[id - 1]?.nodeIds.length ?? 0} thành viên)
                </option>
              ))}
            </optgroup>
          </select>
        </label>

        {!targetType || !targetId ? (
          <p className="text-xs text-text-subtle italic">Chưa gán đích — điều khiển bị khóa.</p>
        ) : targetType === 'NODE' ? (
          <>
            <NodeCard nodeId={Number(targetId)} disabled={isOffline || isNodeUnavailable} />
            {(isOffline || isNodeUnavailable) && <p className="text-xs text-accent-amber">Node offline hoặc chưa commissioning — điều khiển bị khóa.</p>}
          </>
        ) : (
          <>
            <GroupCard groupId={Number(targetId)} />
            <div className="flex items-center justify-between gap-2">
              <span className="text-xs font-semibold text-text-muted">Điều khiển nhóm:</span>
              <div className="flex items-center gap-2">
                <button
                  type="button"
                  disabled={isOffline || isGroupUnavailable || commandMutation.isPending}
                  onClick={async () => {
                    if (!window.confirm(`Bật bơm cho toàn bộ Nhóm #${targetId}?`)) return;
                    try {
                      await commandMutation.mutateAsync({ target_type: 'GROUP', group_id: Number(targetId), action: 'ON', run_lease_ms: 30000 });
                      toast.success(`Đã gửi lệnh bật Nhóm #${targetId}`);
                    } catch {
                      toast.error('Không thể gửi lệnh nhóm', 'Kiểm tra trạng thái Gateway và cấu hình nhóm.');
                    }
                  }}
                  className="btn-primary min-h-[44px] rounded-xl px-3 text-xs font-bold disabled:opacity-50 disabled:cursor-not-allowed"
                >Bật</button>
                <button
                  type="button"
                  disabled={isOffline || isGroupUnavailable || commandMutation.isPending}
                  onClick={async () => {
                    try {
                      await commandMutation.mutateAsync({ target_type: 'GROUP', group_id: Number(targetId), action: 'OFF', run_lease_ms: 30000 });
                      toast.success(`Đã gửi lệnh tắt Nhóm #${targetId}`);
                    } catch {
                      toast.error('Không thể gửi lệnh nhóm', 'Kiểm tra trạng thái Gateway và cấu hình nhóm.');
                    }
                  }}
                  className="btn-secondary min-h-[44px] rounded-xl px-3 text-xs font-bold disabled:opacity-50 disabled:cursor-not-allowed"
                >Tắt</button>
              </div>
            </div>
            {isOffline && <p className="text-xs text-accent-amber">Gateway offline — điều khiển bị khóa.</p>}
            {isGroupUnavailable && !isOffline && <p className="text-xs text-accent-amber">Nhóm chưa ACTIVE hoặc chưa có thành viên — điều khiển bị khóa.</p>}
          </>
        )}
      </div>
    </article>
  );
}
