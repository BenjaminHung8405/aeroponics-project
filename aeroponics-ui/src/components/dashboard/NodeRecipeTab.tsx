'use client';

import React, { useState, useMemo } from 'react';
import { useTreatments } from '../../hooks/queries/useTreatments';
import { useAssignGroup, useGroups } from '../../hooks/queries/useGroups';
import { useGroupStore, useAllGroups } from '../../store/useGroupStore';
import { SUCCESS_MESSAGES } from '../../lib/messages';
import { AlertBanner } from '../common/AlertBanner';
import { useToast } from '../common/Toast';
import {
  Leaf, AlertCircle, Loader2, BookOpen, Sun, Moon,
} from 'lucide-react';
import type { NodeState } from '../../store/useNodeStore';

interface NodeRecipeTabProps {
  node: NodeState;
  onClose?: () => void;
}

/**
 * NodeRecipeTab Component
 * Displayed inside NodeDetailModal's "Công Thức" tab.
 * Shows the current recipe assigned via the node's group and allows
 * re-assigning a new published treatment version to the same group.
 *
 * Follows:
 *  - S4-DS-ICON-14: Zero emoji, 100% Lucide SVG
 *  - S4-DS-TOUCH-15: min-h-[44px] / min-h-[48px], active:scale-95
 */
export function NodeRecipeTab({ node, onClose }: NodeRecipeTabProps) {
  const { data: treatmentResponse, isLoading: treatmentsLoading } = useTreatments();
  const assignMutation = useAssignGroup();
  // Ensures group membership is loaded (query is shared/deduped with the
  // dashboard grids). Without it we could submit a stale/empty membership and
  // evict sibling nodes, because node_ids is a full replacement set.
  const { isSuccess: groupsLoaded } = useGroups();
  // groups is Record<number, GroupState> — look up by cachedGroupId
  const currentGroup = useGroupStore((state) =>
    node.cachedGroupId ? state.groups[node.cachedGroupId] ?? null : null,
  );
  const { toast } = useToast();
  const allGroups = useAllGroups();

  const [selectedVersionId, setSelectedVersionId] = useState<number | ''>('');
  const [selectedGroupId, setSelectedGroupId] = useState<number | null>(
    node.cachedGroupId,
  );

  // Target group = node's current group when it has one, otherwise the
  // explicit user selection (never silently default to group 1).
  const targetGroupId = node.cachedGroupId !== null ? node.cachedGroupId : selectedGroupId;
  const targetGroup = useGroupStore((state) =>
    targetGroupId ? state.groups[targetGroupId] ?? null : null,
  );

  // Backend treats node_ids as the full replacement set. Sending the union of
  // current membership + this node preserves siblings instead of evicting them.
  const targetNodeIds = useMemo(() => {
    const members = targetGroup?.nodeIds ?? [];
    return Array.from(new Set([...members, node.id])).sort((a, b) => a - b);
  }, [targetGroup?.nodeIds, node.id]);

  // When the node already belongs to a group, its membership must be known
  // and must include this node. If the store is stale or not hydrated, the
  // replacement set would silently drop the other members of the group.
  const groupMembershipIsTrusted =
    groupsLoaded &&
    (node.cachedGroupId === null || (targetGroup?.nodeIds.includes(node.id) ?? false));

  const canSubmit = Boolean(
    groupsLoaded && groupMembershipIsTrusted && targetGroupId && selectedVersionId && targetNodeIds.length > 0,
  );

  /** Flatten all PUBLISHED versions across all treatments */
  const publishedVersions = useMemo(() => {
    if (!treatmentResponse?.items) return [];
    const list: Array<{
      versionId: number;
      treatmentName: string;
      versionNum: number;
      sprayDay: number;
      cooldownDay: number;
      sprayNight: number;
      cooldownNight: number;
    }> = [];
    for (const treatment of treatmentResponse.items) {
      if (!treatment.versions) continue;
      for (const v of treatment.versions) {
        if (v.status === 'PUBLISHED') {
          list.push({
            versionId: v.id,
            treatmentName: treatment.name,
            versionNum: v.version_num,
            sprayDay: v.spray_day_s,
            cooldownDay: v.cooldown_day_s,
            sprayNight: v.spray_night_s,
            cooldownNight: v.cooldown_night_s,
          });
        }
      }
    }
    return list;
  }, [treatmentResponse]);

  const handleSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    if (!selectedVersionId || !canSubmit) return;

    const chosen = publishedVersions.find((v) => v.versionId === selectedVersionId);
    try {
      await assignMutation.mutateAsync({
        groupId: targetGroupId as number,
        dto: {
          treatment_version_id: Number(selectedVersionId),
          node_ids: targetNodeIds,
        },
      });
      toast.success(
        SUCCESS_MESSAGES.ASSIGN_RECIPE_TO_NODE(
          chosen?.treatmentName ?? 'Công thức',
          chosen?.versionNum ?? 0,
          node.displayName,
        ),
      );
      setSelectedVersionId('');
      if (onClose) onClose();
    } catch {
      // Handled by AlertBanner
    }
  };

  const hasRecipe = Boolean(currentGroup?.treatment);

  return (
    <div className="space-y-4">
      {/* Current Recipe Card */}
      {hasRecipe ? (
        <div className="p-3.5 rounded-xl bg-surface/50 border border-primary/30 space-y-2">
          <div className="flex items-center justify-between gap-2">
            <span className="text-sm font-bold text-text">
              {currentGroup!.treatment!.treatment_name} v{currentGroup!.treatment!.version_num}
            </span>
            <span className="text-[11px] px-2 py-0.5 rounded bg-primary/15 text-primary border border-primary/30 shrink-0">
              Nhóm #{currentGroup!.groupId}
            </span>
          </div>

          {/* Day / Night timing with active phase highlight */}
          <div className="space-y-1.5 mt-1">
            <div
              className={`flex items-center justify-between px-2.5 py-1.5 rounded-lg border text-[11px] font-mono tabular-nums transition-all duration-300 ${
                currentGroup!.phase === 'DAY'
                  ? 'bg-accent-amber/20 border-accent-amber/50 ring-1 ring-accent-amber/30'
                  : 'bg-background/50 border-border/20 opacity-60'
              }`}
            >
              <span className="font-sans flex items-center gap-1 font-semibold text-accent-amber">
                <Sun size={11} aria-hidden="true" />
                {currentGroup!.phase === 'DAY' && (
                  <span className="text-[10px] uppercase tracking-wider">LIVE</span>
                )}
                <span>Ngày</span>
              </span>
              <span className="text-text">
                {currentGroup!.treatment!.spray_day_s}s phun / {currentGroup!.treatment!.cooldown_day_s}s nghỉ
              </span>
            </div>

            <div
              className={`flex items-center justify-between px-2.5 py-1.5 rounded-lg border text-[11px] font-mono tabular-nums transition-all duration-300 ${
                currentGroup!.phase === 'NIGHT'
                  ? 'bg-accent-indigo/20 border-accent-indigo/50 ring-1 ring-accent-indigo/30'
                  : 'bg-background/50 border-border/20 opacity-60'
              }`}
            >
              <span className="font-sans flex items-center gap-1 font-semibold text-accent-indigo">
                <Moon size={11} aria-hidden="true" />
                {currentGroup!.phase === 'NIGHT' && (
                  <span className="text-[10px] uppercase tracking-wider">LIVE</span>
                )}
                <span>Đêm</span>
              </span>
              <span className="text-text">
                {currentGroup!.treatment!.spray_night_s}s phun / {currentGroup!.treatment!.cooldown_night_s}s nghỉ
              </span>
            </div>
          </div>
        </div>
      ) : (
        <div className="flex flex-col items-center justify-center p-6 bg-surface/30 rounded-xl border border-dashed border-border/40 text-center space-y-2">
          <BookOpen size={28} className="text-text-subtle" aria-hidden="true" />
          <span className="text-sm text-text-muted font-medium">
            {node.cachedGroupId ? 'Nhóm chưa có công thức nào được gán.' : 'Node chưa thuộc nhóm nào.'}
          </span>
        </div>
      )}

      {/* Assign New Recipe Form */}
      <form onSubmit={handleSubmit} className="p-4 rounded-xl bg-surface/70 border border-border/40 space-y-3">
        <span className="block text-xs font-bold text-text uppercase tracking-wider">
          Gán Công Thức Mới
        </span>

        {node.cachedGroupId !== null ? (
          <p className="text-xs text-text-muted leading-relaxed">
            Node đang thuộc{' '}
            <span className="font-semibold text-text">Nhóm #{node.cachedGroupId}</span> (
            {targetGroup?.nodeIds.length ?? 0} trạm). Công thức áp dụng cho{' '}
            <span className="font-semibold text-text">toàn bộ nhóm</span> — các trạm
            khác giữ nguyên vị trí, chỉ công thức được thay đổi.
          </p>
        ) : (
          <div className="space-y-2">
            <p className="text-xs text-text-muted leading-relaxed">
              Node chưa thuộc nhóm nào. Chọn nhóm bên dưới — node sẽ được thêm vào
              nhóm đó và dùng chung công thức áp dụng cho toàn nhóm.
            </p>
            <select
              value={selectedGroupId ?? ''}
              onChange={(e) => {
                if (assignMutation.isError) assignMutation.reset();
                setSelectedGroupId(e.target.value ? Number(e.target.value) : null);
              }}
              className="w-full px-3.5 py-2.5 rounded-xl bg-background/80 border border-border/50 text-text text-sm focus:outline-none focus:border-primary transition-colors min-h-[44px]"
              required
            >
              <option value="">-- Chọn nhóm mục tiêu --</option>
              {allGroups.map((g) => (
                <option key={g.groupId} value={g.groupId}>
                  Nhóm #{g.groupId} — {g.nodeIds.length} trạm
                  {g.treatment ? ` · ${g.treatment.treatment_name} v${g.treatment.version_num}` : ''}
                </option>
              ))}
            </select>
          </div>
        )}

        {targetGroup && !targetGroup.nodeIds.includes(node.id) && (
          <div className="p-2.5 rounded-lg bg-primary/10 border border-primary/30 text-primary text-xs leading-relaxed">
            Node sẽ được thêm vào Nhóm #{targetGroup.groupId} (hiện có{' '}
            {targetGroup.nodeIds.length} trạm).
          </div>
        )}

        {groupsLoaded && !groupMembershipIsTrusted && (
          <div className="p-2.5 rounded-lg bg-accent-amber/10 border border-accent-amber/30 text-accent-amber text-xs leading-relaxed">
            Dữ liệu nhóm chưa đồng bộ với node này. Tải lại trang để tránh ghi đè
            danh sách trạm của nhóm.
          </div>
        )}

        {treatmentsLoading ? (
          <div className="flex items-center gap-2 p-3 rounded-xl bg-surface/50 text-text-muted text-xs">
            <Loader2 size={14} className="animate-spin text-primary" aria-hidden="true" />
            <span>Đang tải danh mục công thức...</span>
          </div>
        ) : publishedVersions.length === 0 ? (
          <div className="p-3 rounded-lg bg-accent-amber/10 border border-accent-amber/30 text-accent-amber text-xs flex items-start gap-2">
            <AlertCircle size={15} className="shrink-0 mt-0.5" aria-hidden="true" />
            <span>
              Chưa có công thức nào ở trạng thái <strong>PUBLISHED</strong>.
              Hãy tạo và phát hành công thức ở bảng bên dưới trước.
            </span>
          </div>
        ) : (
          <div className="space-y-3">
            <select
              value={selectedVersionId}
              onChange={(e) => {
                if (assignMutation.isError) assignMutation.reset();
                setSelectedVersionId(e.target.value ? Number(e.target.value) : '');
              }}
              className="w-full px-3.5 py-2.5 rounded-xl bg-background/80 border border-border/50 text-text text-sm focus:outline-none focus:border-primary transition-colors min-h-[44px]"
              required
            >
              <option value="">-- Chọn phiên bản công thức --</option>
              {publishedVersions.map((v) => (
                <option key={v.versionId} value={v.versionId}>
                  {v.treatmentName} (v{v.versionNum}) — Ngày: {v.sprayDay}s/{v.cooldownDay}s | Đêm: {v.sprayNight}s/{v.cooldownNight}s
                </option>
              ))}
            </select>

            <button
              type="submit"
              disabled={!canSubmit || assignMutation.isPending}
              className="btn-primary w-full inline-flex items-center justify-center gap-2 px-4 py-3 rounded-xl bg-primary hover:bg-primary/90 active:scale-95 text-background font-bold text-sm shadow-lg shadow-primary/25 min-h-[48px] disabled:opacity-50 disabled:cursor-not-allowed transition-all cursor-pointer"
              aria-label={`Xác nhận gán công thức cho ${node.displayName}`}
            >
              {assignMutation.isPending ? (
                <>
                  <Loader2 size={16} className="animate-spin" aria-hidden="true" />
                  <span>Đang xử lý...</span>
                </>
              ) : (
                <>
                  <Leaf size={16} aria-hidden="true" />
                  <span>Xác nhận Gán Công Thức</span>
                </>
              )}
            </button>
          </div>
        )}

        {assignMutation.isError && (
          <AlertBanner
            error={assignMutation.error}
            fallbackContext={`Không thể gán công thức cho ${node.displayName}`}
          />
        )}
      </form>
    </div>
  );
}
