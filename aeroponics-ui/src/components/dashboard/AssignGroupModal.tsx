'use client';

import React, { useState, useEffect, useMemo } from 'react';
import { Modal } from '../common/Modal';
import { AlertBanner } from '../common/AlertBanner';
import { useToast } from '../common/Toast';
import { SUCCESS_MESSAGES } from '../../lib/messages';
import { useTreatments } from '../../hooks/queries/useTreatments';
import { useAssignGroup, useUnassignGroup } from '../../hooks/queries/useGroups';
import { Loader2, Sliders, CheckCircle2, AlertCircle } from 'lucide-react';
import type { GroupState } from '../../store/useGroupStore';


interface AssignGroupModalProps {
  group: GroupState;
  isOpen: boolean;
  onClose: () => void;
}

export function AssignGroupModal({ group, isOpen, onClose }: AssignGroupModalProps) {
  const { data: treatmentList, isLoading: isTreatmentsLoading } = useTreatments();
  const assignMutation = useAssignGroup();
  const unassignMutation = useUnassignGroup();

  const { toast } = useToast();
  const [selectedVersionId, setSelectedVersionId] = useState<number | null>(null);
  const [selectedNodes, setSelectedNodes] = useState<number[]>([]);

  const publishedVersions = useMemo(() => {
    if (!treatmentList?.items) return [];
    const list: Array<{
      versionId: number;
      treatmentName: string;
      versionNum: number;
      sprayDay: number;
      cooldownDay: number;
      sprayNight: number;
      cooldownNight: number;
    }> = [];

    for (const treatment of treatmentList.items) {
      if (treatment.versions) {
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
    }
    return list;
  }, [treatmentList]);

  const { reset: resetAssign } = assignMutation;
  const { reset: resetUnassign } = unassignMutation;

  useEffect(() => {
    if (isOpen) {
      setSelectedVersionId(group.treatmentVersionId ?? (publishedVersions[0]?.versionId ?? null));
      setSelectedNodes(group.nodeIds && group.nodeIds.length > 0 ? [...group.nodeIds] : [group.groupId]);
      resetAssign();
      resetUnassign();
    }
  }, [isOpen, group, publishedVersions, resetAssign, resetUnassign]);


  const handleToggleNode = (nodeId: number) => {
    if (assignMutation.isError) assignMutation.reset();
    if (unassignMutation.isError) unassignMutation.reset();
    setSelectedNodes((prev) =>
      prev.includes(nodeId) ? prev.filter((id) => id !== nodeId) : [...prev, nodeId].sort(),
    );
  };

  const handleAssignSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    if (!selectedVersionId) return;

    try {
      await assignMutation.mutateAsync({
        groupId: group.groupId,
        dto: {
          treatment_version_id: selectedVersionId,
          node_ids: selectedNodes,
        },
      });
      toast.success(SUCCESS_MESSAGES.ASSIGN_GROUP(group.groupId));
      onClose();
    } catch {
      // Handled by AlertBanner
    }
  };

  const handleUnassign = async () => {
    try {
      await unassignMutation.mutateAsync(group.groupId);
      toast.success(SUCCESS_MESSAGES.UNASSIGN_GROUP(group.groupId));
      onClose();
    } catch {
      // Handled by AlertBanner
    }
  };

  const isPending = assignMutation.isPending || unassignMutation.isPending;

  return (
    <Modal
      isOpen={isOpen}
      onClose={onClose}
      title={`Cấu Hình Nhóm Điều Khiển #${group.groupId}`}
      titleId="assign-group-modal-title"
      maxWidth="md"
    >
      <form onSubmit={handleAssignSubmit} className="space-y-4">
        <div>
          <label
            htmlFor="select-treatment-version"
            className="block text-xs font-semibold uppercase tracking-wider text-text-muted mb-1.5"
          >
            Phiên bản công thức khí canh (Chỉ chọn bản đã PUBLISHED) <span className="text-danger">*</span>
          </label>

          {isTreatmentsLoading ? (
            <div className="flex items-center gap-2 p-3 rounded-xl bg-surface/50 text-text-muted text-xs">
              <Loader2 size={14} className="animate-spin" aria-hidden="true" />
              <span>Đang tải danh mục công thức...</span>
            </div>
          ) : publishedVersions.length === 0 ? (
            <div className="flex items-center gap-2 p-3 rounded-xl bg-accent-amber/15 border border-accent-amber/30 text-accent-amber text-xs">
              <AlertCircle size={16} aria-hidden="true" />
              <span>
                Chưa có công thức nào ở trạng thái <strong>PUBLISHED</strong>. Hãy phát hành phiên bản công thức ở bảng bên dưới trước khi gán.
              </span>
            </div>
          ) : (
            <select
              id="select-treatment-version"
              value={selectedVersionId ?? ''}
              onChange={(e) => {
                if (assignMutation.isError) assignMutation.reset();
                setSelectedVersionId(Number(e.target.value));
              }}
              className="w-full px-3.5 py-2.5 rounded-xl bg-background/80 border border-border/50 text-text text-sm focus:outline-none focus:border-primary transition-colors min-h-[44px]"
              required
            >
              {publishedVersions.map((pv) => (
                <option key={pv.versionId} value={pv.versionId}>
                  {pv.treatmentName} (v{pv.versionNum}) — Ngày: {pv.sprayDay}s/{pv.cooldownDay}s | Đêm: {pv.sprayNight}s/{pv.cooldownNight}s
                </option>
              ))}
            </select>
          )}
        </div>

        <div>
          <span className="block text-xs font-semibold uppercase tracking-wider text-text-muted mb-2">
            Chọn trạm khí canh phụ trách (RF Nodes 4–7)
          </span>
          <div className="grid grid-cols-2 sm:grid-cols-4 gap-2">
            {[4, 5, 6, 7].map((nodeId) => {
              const isChecked = selectedNodes.includes(nodeId);
              const btnClass = isChecked
                ? 'flex items-center justify-between p-3 rounded-xl border text-sm font-semibold transition-all duration-150 min-h-[44px] cursor-pointer active:scale-95 bg-primary/20 border-primary/50 text-text'
                : 'flex items-center justify-between p-3 rounded-xl border text-sm font-semibold transition-all duration-150 min-h-[44px] cursor-pointer active:scale-95 bg-surface/40 border-border/30 text-text-subtle hover:text-text hover:bg-surface/70';
              return (
                <button
                  key={nodeId}
                  type="button"
                  onClick={() => handleToggleNode(nodeId)}
                  className={btnClass}
                  aria-pressed={isChecked}
                >
                  <span>Trạm #{nodeId}</span>
                  {isChecked ? (
                    <CheckCircle2 size={16} className="text-primary shrink-0" aria-hidden="true" />
                  ) : null}
                </button>
              );
            })}
          </div>
        </div>

        {assignMutation.isError && (
          <AlertBanner
            error={assignMutation.error}
            fallbackContext="Không thể lưu cấu hình nhóm"
          />
        )}

        {unassignMutation.isError && (
          <AlertBanner
            error={unassignMutation.error}
            fallbackContext="Không thể hủy gán nhóm"
          />
        )}


        <div className="flex flex-col sm:flex-row items-center justify-between gap-3 pt-3 border-t border-border/20">
          {group.status === 'ACTIVE' ? (
            <button
              type="button"
              onClick={handleUnassign}
              disabled={isPending}
              className="btn-secondary w-full sm:w-auto px-4 py-2 rounded-xl bg-danger/15 hover:bg-danger/25 text-danger border border-danger/40 text-xs font-semibold"
            >
              Hủy gán nhóm này
            </button>
          ) : (
            <div />
          )}

          <div className="flex items-center gap-2 w-full sm:w-auto justify-end">
            <button
              type="button"
              onClick={onClose}
              disabled={isPending}
              className="btn-secondary px-4 py-2 rounded-xl bg-surface/60 hover:bg-surface text-text-muted text-sm font-semibold border border-border/40"
            >
              Đóng
            </button>
            <button
              type="submit"
              disabled={isPending || !selectedVersionId}
              className="btn-primary inline-flex items-center justify-center gap-2 px-5 py-2.5 rounded-xl bg-primary hover:bg-primary/90 disabled:opacity-50 text-background font-bold text-sm shadow-lg shadow-primary/30"
            >
              {assignMutation.isPending ? (
                <>
                  <Loader2 size={16} className="animate-spin" aria-hidden="true" />
                  <span>Đang lưu...</span>
                </>
              ) : (
                <>
                  <Sliders size={16} aria-hidden="true" />
                  <span>Lưu cấu hình</span>
                </>
              )}
            </button>
          </div>
        </div>
      </form>
    </Modal>
  );
}
