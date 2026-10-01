'use client';

import React, { useState, useMemo } from 'react';
import { useTreatments } from '../../hooks/queries/useTreatments';
import { useAssignGroup, useGroups } from '../../hooks/queries/useGroups';
import { useGroupStore, useAllGroups, useGroupByNodeId } from '../../store/useGroupStore';
import { SUCCESS_MESSAGES } from '../../lib/messages';
import { AlertBanner } from '../common/AlertBanner';
import { useToast } from '../common/Toast';
import {
  Leaf,
  AlertCircle,
  Loader2,
  BookOpen,
  Sun,
  Moon,
  Radio,
  Users,
  UserCheck,
  CheckCircle2,
} from 'lucide-react';
import type { NodeState } from '../../store/useNodeStore';
import { useSelectedDevice } from '../../lib/selected-device-context';

interface NodeRecipeTabProps {
  node: NodeState;
  onClose?: () => void;
}

/**
 * NodeRecipeTab Component
 * Displayed inside NodeDetailModal's "Lịch tưới" tab.
 * Implements IIoT Node-first Smart Group Allocation & Blast Radius Management.
 *
 * Follows:
 *  - S4-DS-ICON-14: Zero emoji, 100% Lucide SVG
 *  - S4-DS-TOUCH-15: min-h-[44px] / min-h-[48px], active:scale-95
 */
export function NodeRecipeTab({ node, onClose }: NodeRecipeTabProps) {
  const { data: treatmentResponse, isLoading: treatmentsLoading } = useTreatments();
  const assignMutation = useAssignGroup();
  const { selectedDeviceId } = useSelectedDevice();
  const { isSuccess: groupsLoaded } = useGroups();
  const { toast } = useToast();
  const allGroups = useAllGroups();

  // Bi-directional lookup: find group containing this node or matching cachedGroupId
  const currentGroup = useGroupByNodeId(node.id, node.cachedGroupId);

  // Detect sibling nodes in the same timer group
  const siblingNodeIds = useMemo(() => {
    if (!currentGroup?.nodeIds) return [];
    return currentGroup.nodeIds.filter((id) => id !== node.id);
  }, [currentGroup?.nodeIds, node.id]);

  const hasSiblings = siblingNodeIds.length > 0;

  // Find first vacant group (no nodes or unassigned status) for isolation
  const vacantGroup = useMemo(() => {
    return allGroups.find(
      (g) => g.groupId !== currentGroup?.groupId && (g.nodeIds.length === 0 || g.status === 'UNASSIGNED'),
    );
  }, [allGroups, currentGroup?.groupId]);

  // Blast radius allocation mode: 'GROUP' = apply to all siblings, 'ISOLATE' = split to new group
  const [allocationMode, setAllocationMode] = useState<'GROUP' | 'ISOLATE'>(
    hasSiblings && vacantGroup ? 'ISOLATE' : 'GROUP',
  );

  const [selectedVersionId, setSelectedVersionId] = useState<number | ''>('');

  // Target group calculation:
  // - If isolating and vacant group available -> vacantGroup.groupId
  // - Else if currentGroup exists -> currentGroup.groupId
  // - Else fallback to vacant group or group 1
  const targetGroupId = useMemo(() => {
    if (hasSiblings && allocationMode === 'ISOLATE' && vacantGroup) {
      return vacantGroup.groupId;
    }
    if (currentGroup) {
      return currentGroup.groupId;
    }
    return vacantGroup?.groupId ?? 1;
  }, [hasSiblings, allocationMode, vacantGroup, currentGroup]);

  // Target nodes calculation:
  // - If applying to group -> all members of current group
  // - If isolating or single node -> only [node.id]
  const targetNodeIds = useMemo(() => {
    if (hasSiblings && allocationMode === 'GROUP' && currentGroup) {
      return Array.from(new Set([...currentGroup.nodeIds, node.id])).sort((a, b) => a - b);
    }
    return [node.id];
  }, [hasSiblings, allocationMode, currentGroup, node.id]);

  const canSubmit = Boolean(
    groupsLoaded && targetGroupId && selectedVersionId && targetNodeIds.length > 0,
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
    if (!selectedVersionId || !canSubmit || !selectedDeviceId) return;

    const chosen = publishedVersions.find((v) => v.versionId === selectedVersionId);
    try {
      await assignMutation.mutateAsync({
        groupId: targetGroupId,
        deviceId: selectedDeviceId,
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
            <div className="flex items-center gap-1.5 shrink-0">
              <span className="text-[11px] px-2 py-0.5 rounded bg-primary/15 text-primary border border-primary/30 font-medium">
                Nhóm #{currentGroup!.groupId}
              </span>
              {hasSiblings ? (
                <span className="text-[11px] px-2 py-0.5 rounded bg-accent-amber/15 text-accent-amber border border-accent-amber/30 flex items-center gap-1">
                  <Users size={11} aria-hidden="true" />
                  {currentGroup!.nodeIds.length} trạm chung
                </span>
              ) : (
                <span className="text-[11px] px-2 py-0.5 rounded bg-surface border border-border/30 text-text-subtle flex items-center gap-1">
                  <UserCheck size={11} aria-hidden="true" />
                  Độc lập
                </span>
              )}
            </div>
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
        <div className="flex flex-col items-center justify-center p-5 bg-surface/30 rounded-xl border border-dashed border-border/40 text-center space-y-1.5">
          <BookOpen size={24} className="text-text-subtle" aria-hidden="true" />
          <span className="text-xs text-text-muted font-medium">
            Trạm chưa cài đặt lịch tưới nào.
          </span>
        </div>
      )}

      {/* Assign New Recipe Form */}
      <form onSubmit={handleSubmit} className="p-4 rounded-xl bg-surface/70 border border-border/40 space-y-3.5">
        <span className="block text-xs font-bold text-text uppercase tracking-wider">
          Thiết Lập Lịch Tưới Mới
        </span>

        {/* Blast Radius Section: Only when sharing group with other sibling nodes */}
        {hasSiblings && (
          <div className="p-3 rounded-xl bg-accent-amber/10 border border-accent-amber/30 space-y-2.5">
            <div className="flex items-start gap-2">
              <Users size={16} className="text-accent-amber shrink-0 mt-0.5" aria-hidden="true" />
              <div className="text-xs text-accent-amber leading-relaxed">
                <p className="font-semibold">
                  {node.displayName} đang chung Nhóm #{currentGroup!.groupId} với:{' '}
                  {siblingNodeIds.map((id) => `Node ${id < 10 ? '0' + id : id}`).join(', ')}
                </p>
                <p className="text-[11px] text-text-muted mt-0.5">
                  Chọn phạm vi ảnh hưởng khi áp dụng lịch tưới mới:
                </p>
              </div>
            </div>

            <div className="grid grid-cols-1 sm:grid-cols-2 gap-2 pt-1">
              <button
                type="button"
                onClick={() => setAllocationMode('GROUP')}
                className={`flex items-start gap-2 p-2.5 rounded-lg border text-left text-xs transition-all cursor-pointer min-h-[44px] ${
                  allocationMode === 'GROUP'
                    ? 'bg-accent-amber/20 border-accent-amber text-text font-semibold'
                    : 'bg-surface/50 border-border/40 text-text-muted hover:text-text'
                }`}
              >
                <div className={`w-3.5 h-3.5 rounded-full border mt-0.5 shrink-0 flex items-center justify-center ${
                  allocationMode === 'GROUP' ? 'border-accent-amber bg-accent-amber' : 'border-border/60'
                }`}>
                  {allocationMode === 'GROUP' && <div className="w-1.5 h-1.5 rounded-full bg-background" />}
                </div>
                <div>
                  <span className="block font-medium">Toàn bộ Nhóm #{currentGroup!.groupId}</span>
                  <span className="text-[10px] text-text-subtle">Cập nhật cho cả {currentGroup!.nodeIds.length} trạm</span>
                </div>
              </button>

              {vacantGroup ? (
                <button
                  type="button"
                  onClick={() => setAllocationMode('ISOLATE')}
                  className={`flex items-start gap-2 p-2.5 rounded-lg border text-left text-xs transition-all cursor-pointer min-h-[44px] ${
                    allocationMode === 'ISOLATE'
                      ? 'bg-primary/20 border-primary text-text font-semibold'
                      : 'bg-surface/50 border-border/40 text-text-muted hover:text-text'
                  }`}
                >
                  <div className={`w-3.5 h-3.5 rounded-full border mt-0.5 shrink-0 flex items-center justify-center ${
                    allocationMode === 'ISOLATE' ? 'border-primary bg-primary' : 'border-border/60'
                  }`}>
                    {allocationMode === 'ISOLATE' && <div className="w-1.5 h-1.5 rounded-full bg-background" />}
                  </div>
                  <div>
                    <span className="block font-medium">Tách riêng {node.displayName}</span>
                    <span className="text-[10px] text-text-subtle">Chuyển sang Nhóm #{vacantGroup.groupId} trống</span>
                  </div>
                </button>
              ) : (
                <div className="p-2 rounded-lg bg-surface/30 border border-border/20 text-[11px] text-text-subtle">
                  Cả 4 nhóm điều khiển đều đang có trạm hoạt động. Lịch mới sẽ áp dụng chung cho nhóm.
                </div>
              )}
            </div>
          </div>
        )}

        {/* Recipe Selection */}
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
            <div className="space-y-1">
              <label htmlFor="recipe-select" className="text-xs text-text-muted font-medium">
                Chọn công thức mong muốn:
              </label>
              <select
                id="recipe-select"
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
            </div>

            {/* Smart Allocation Summary */}
            <div className="p-2.5 rounded-lg bg-surface/60 border border-border/30 text-[11px] text-text-muted flex items-center justify-between">
              <span>Đích áp dụng:</span>
              <span className="font-semibold text-text">
                Nhóm #{targetGroupId} ({targetNodeIds.length} trạm)
              </span>
            </div>

            {/* Hardware Feedback Action Button */}
            <button
              type="submit"
              disabled={!canSubmit || assignMutation.isPending}
              className="btn-primary w-full inline-flex items-center justify-center gap-2 px-4 py-3 rounded-xl bg-primary hover:bg-primary/90 active:scale-95 text-background font-bold text-sm shadow-lg shadow-primary/25 min-h-[48px] disabled:opacity-50 disabled:cursor-not-allowed transition-all cursor-pointer"
              aria-label={`Xác nhận nạp lịch tưới cho ${node.displayName}`}
            >
              {assignMutation.isPending ? (
                <>
                  <Loader2 size={16} className="animate-spin" aria-hidden="true" />
                  <span>Đang lưu cấu hình và gửi sóng RF...</span>
                </>
              ) : (
                <>
                  <Leaf size={16} aria-hidden="true" />
                  <span>Lưu & Đồng Bộ Lịch Tưới</span>
                </>
              )}
            </button>
          </div>
        )}

        {assignMutation.isError && (
          <AlertBanner
            error={assignMutation.error}
            fallbackContext={`Không thể nạp lịch tưới cho ${node.displayName}`}
          />
        )}
      </form>
    </div>
  );
}
