'use client';

import React, { useState, useMemo } from 'react';
import { Modal } from '../common/Modal';
import { AlertBanner } from '../common/AlertBanner';
import { useToast } from '../common/Toast';
import { NodeGroupSelector } from './NodeGroupSelector';
import { useAllGroups } from '../../store/useGroupStore';
import {
  buildNodeToGroupLookup,
  sanitizeTargetPayload,
  TargetSelectionPayload,
} from '../../lib/target-selector';
import { useSendPumpOverride } from '../../hooks/queries/useNodes';
import { useSelectedDevice } from '../../lib/selected-device-context';
import { Play, Square, Loader2, Zap } from 'lucide-react';

interface BatchControlModalProps {
  isOpen: boolean;
  onClose: () => void;
  defaultAction?: 'ON' | 'OFF';
}

const DURATION_PRESETS = [15, 30, 60, 120];

export function BatchControlModal({
  isOpen,
  onClose,
  defaultAction = 'ON',
}: BatchControlModalProps) {
  const { selectedDeviceId } = useSelectedDevice();
  const groups = useAllGroups();
  const overrideMutation = useSendPumpOverride(selectedDeviceId);
  const { toast } = useToast();

  const [action, setAction] = useState<'ON' | 'OFF'>(defaultAction);
  const [leaseDurationSec, setLeaseDurationSec] = useState<number>(30);
  const [selectedGroups, setSelectedGroups] = useState<number[]>([]);
  const [selectedNodes, setSelectedNodes] = useState<number[]>([]);
  const [isSubmitting, setIsSubmitting] = useState(false);
  const [submitError, setSubmitError] = useState<Error | null>(null);

  const nodeToGroupMap = useMemo(() => {
    return buildNodeToGroupLookup(groups, true);
  }, [groups]);

  const handleTargetsChange = (targets: {
    selectedGroups: number[];
    selectedNodes: number[];
  }) => {
    setSelectedGroups(targets.selectedGroups);
    setSelectedNodes(targets.selectedNodes);
  };

  const handleSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    setSubmitError(null);

    // 1. Enforce Sanitization Guard before dispatching
    const sanitizedPayload: TargetSelectionPayload = sanitizeTargetPayload(
      {
        target_groups: selectedGroups,
        target_nodes: selectedNodes,
      },
      nodeToGroupMap,
    );

    const totalTargets =
      sanitizedPayload.target_groups.length + sanitizedPayload.target_nodes.length;

    if (totalTargets === 0) {
      toast.error('Chưa chọn mục tiêu', 'Vui lòng chọn ít nhất một Nhóm hoặc một Trạm.');
      return;
    }

    setIsSubmitting(true);

    try {
      // 2. Dispatch sanitized group broadcasts first
      for (const groupId of sanitizedPayload.target_groups) {
        await overrideMutation.mutateAsync({
          target_type: 'GROUP',
          group_id: groupId,
          action,
          run_lease_ms: leaseDurationSec * 1000,
        });
      }

      // 3. Dispatch sanitized independent unicast node commands
      for (const nodeId of sanitizedPayload.target_nodes) {
        await overrideMutation.mutateAsync({
          target_type: 'NODE',
          node_id: nodeId,
          action,
          run_lease_ms: leaseDurationSec * 1000,
        });
      }

      const summaryParts: string[] = [];
      if (sanitizedPayload.target_groups.length > 0) {
        summaryParts.push(`${sanitizedPayload.target_groups.length} nhóm`);
      }
      if (sanitizedPayload.target_nodes.length > 0) {
        summaryParts.push(`${sanitizedPayload.target_nodes.length} trạm lẻ`);
      }

      toast.success(
        `Đã gửi lệnh ${action === 'ON' ? 'Bật' : 'Tắt'} bơm`,
        `Mục tiêu: ${summaryParts.join(' & ')} (${leaseDurationSec}s lease)`,
      );

      // Reset and close
      setSelectedGroups([]);
      setSelectedNodes([]);
      onClose();
    } catch (err: any) {
      setSubmitError(err instanceof Error ? err : new Error(String(err)));
      toast.error('Lỗi gửi lệnh điều khiển', err.message || 'Không thể thực thi lệnh.');
    } finally {
      setIsSubmitting(false);
    }
  };

  const totalSelected = selectedGroups.length + selectedNodes.length;

  return (
    <Modal
      isOpen={isOpen}
      onClose={onClose}
      title="Điều Khiển Tập Trung / Kích Hoạt Thủ Công"
      titleId="batch-control-modal-title"
      maxWidth="lg"
    >
      <form onSubmit={handleSubmit} className="space-y-5" data-testid="batch-control-form">
        {/* ACTION SELECTOR: ON / OFF */}
        <div className="flex flex-col sm:flex-row items-stretch sm:items-center justify-between gap-3 p-3.5 rounded-xl bg-surface/50 border border-border/40">
          <div>
            <span className="block text-xs font-bold uppercase tracking-wider text-text">
              Hành động điều khiển
            </span>
            <p className="text-xs text-text-subtle">
              Áp dụng cho toàn bộ các mục tiêu đã chọn
            </p>
          </div>

          <div className="flex items-center gap-2">
            <button
              type="button"
              disabled={isSubmitting}
              onClick={() => setAction('ON')}
              data-testid="batch-action-on"
              className={`flex-1 sm:flex-none inline-flex items-center justify-center gap-1.5 px-4 py-2 rounded-xl text-xs font-bold min-h-[44px] transition-all cursor-pointer active:scale-95 ${
                action === 'ON'
                  ? 'bg-primary text-background shadow-md shadow-primary/30 border border-primary'
                  : 'bg-surface/80 text-text-muted border border-border/40 hover:text-text'
              }`}
            >
              <Play size={14} fill="currentColor" aria-hidden="true" />
              <span>BẬT BƠM</span>
            </button>

            <button
              type="button"
              disabled={isSubmitting}
              onClick={() => setAction('OFF')}
              data-testid="batch-action-off"
              className={`flex-1 sm:flex-none inline-flex items-center justify-center gap-1.5 px-4 py-2 rounded-xl text-xs font-bold min-h-[44px] transition-all cursor-pointer active:scale-95 ${
                action === 'OFF'
                  ? 'bg-accent-amber/20 text-accent-amber border border-accent-amber/50 shadow-md shadow-accent-amber/20'
                  : 'bg-surface/80 text-text-muted border border-border/40 hover:text-text'
              }`}
            >
              <Square size={14} fill="currentColor" aria-hidden="true" />
              <span>TẮT BƠM</span>
            </button>
          </div>
        </div>

        {/* DURATION PRESET (FOR ON ACTION) */}
        {action === 'ON' && (
          <div className="space-y-1.5">
            <label
              htmlFor="lease-duration-select"
              className="block text-xs font-bold uppercase tracking-wider text-text-muted"
            >
              Thời gian chạy an toàn (Deadman Lease)
            </label>
            <div className="flex flex-wrap gap-2">
              {DURATION_PRESETS.map((sec) => (
                <button
                  key={sec}
                  type="button"
                  onClick={() => setLeaseDurationSec(sec)}
                  className={`px-3 py-2 rounded-xl text-xs font-semibold min-h-[44px] transition-all cursor-pointer active:scale-95 ${
                    leaseDurationSec === sec
                      ? 'bg-primary/20 text-primary border border-primary font-bold'
                      : 'bg-surface/60 text-text-subtle border border-border/30 hover:bg-surface hover:text-text'
                  }`}
                >
                  {sec} Giây
                </button>
              ))}
            </div>
          </div>
        )}

        {/* TARGET SELECTOR WITH CASCADE DISABLE */}
        <NodeGroupSelector
          selectedGroups={selectedGroups}
          selectedNodes={selectedNodes}
          onChange={handleTargetsChange}
          disabled={isSubmitting}
        />

        {submitError && (
          <AlertBanner
            error={submitError}
            fallbackContext="Không thể gửi lệnh điều khiển tập trung"
          />
        )}

        {/* FOOTER ACTIONS */}
        <div className="flex flex-col sm:flex-row items-center justify-between gap-3 pt-3 border-t border-border/20">
          <span className="text-xs text-text-subtle font-medium">
            {totalSelected === 0
              ? 'Chưa chọn mục tiêu nào'
              : `Đã chọn: ${selectedGroups.length} nhóm, ${selectedNodes.length} trạm lẻ`}
          </span>

          <div className="flex items-center gap-2 w-full sm:w-auto justify-end">
            <button
              type="button"
              onClick={onClose}
              disabled={isSubmitting}
              className="btn-secondary px-4 py-2 rounded-xl bg-surface/60 hover:bg-surface text-text-muted text-sm font-semibold border border-border/40 min-h-[44px] cursor-pointer"
            >
              Hủy
            </button>

            <button
              type="submit"
              disabled={isSubmitting || totalSelected === 0}
              className="btn-primary inline-flex items-center justify-center gap-2 px-5 py-2.5 rounded-xl bg-primary hover:bg-primary/90 disabled:opacity-50 text-background font-bold text-sm shadow-lg shadow-primary/30 min-h-[44px] cursor-pointer active:scale-95 transition-all"
            >
              {isSubmitting ? (
                <>
                  <Loader2 size={16} className="animate-spin" aria-hidden="true" />
                  <span>Đang phát lệnh...</span>
                </>
              ) : (
                <>
                  <Zap size={16} aria-hidden="true" />
                  <span>Phát Lệnh Điều Khiển ({totalSelected})</span>
                </>
              )}
            </button>
          </div>
        </div>
      </form>
    </Modal>
  );
}
