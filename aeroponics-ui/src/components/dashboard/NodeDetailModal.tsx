'use client';

import React from 'react';
import { Modal } from '../common/Modal';
import { OutcomeBadge } from '../common/OutcomeBadge';
import { StalenessIndicator } from '../common/StalenessIndicator';
import { AlertBanner } from '../common/AlertBanner';
import { useToast } from '../common/Toast';
import { SUCCESS_MESSAGES } from '../../lib/messages';
import { useResetNodeFault } from '../../hooks/queries/useNodes';
import {
  Activity,
  AlertTriangle,
  RotateCcw,
  Loader2,
  Calendar,
  Layers,
  Cpu,
} from 'lucide-react';

import type { NodeState } from '../../store/useNodeStore';

interface NodeDetailModalProps {
  node: NodeState;
  isOpen: boolean;
  onClose: () => void;
}

/**
 * NodeDetailModal Component
 * Displays comprehensive telemetry, sensor calibration details, and fault reset action.
 * Follows:
 *  - S4-D3: Command log / node status inspection
 *  - S4-DS-TOUCH-15: Reset button min-h-[48px], active:scale-95
 *  - S4-DS-ICON-14: Zero emoji, 100% Lucide SVG
 */
export function NodeDetailModal({ node, isOpen, onClose }: NodeDetailModalProps) {
  const { toast } = useToast();
  const resetFaultMutation = useResetNodeFault();

  const handleResetFault = async () => {
    try {
      await resetFaultMutation.mutateAsync(node.id);
      toast.success(SUCCESS_MESSAGES.RESET_FAULT(node.displayName));
    } catch {
      // Error handled by AlertBanner
    }
  };


  const isFault = node.healthStatus === 'FAULT' || node.outcome?.startsWith('FAULT_');

  const formattedLastSeen = node.lastSeenAt
    ? new Date(node.lastSeenAt).toLocaleString('vi-VN', {
        timeZone: 'Asia/Ho_Chi_Minh',
        dateStyle: 'short',
        timeStyle: 'medium',
      })
    : 'Chưa có tín hiệu';

  return (
    <Modal
      isOpen={isOpen}
      onClose={onClose}
      title={`Thông Số Viễn Thám — ${node.displayName}`}
      titleId="node-detail-modal-title"
      maxWidth="md"
    >
      <div className="space-y-4">
        {/* Status Highlights */}
        <div className="flex items-center justify-between p-3 rounded-xl bg-surface/60 border border-border/30">
          <div className="flex items-center gap-2">
            <StalenessIndicator
              lastSeenAt={node.lastSeenAt}
              isStale={node.isStale}
              staleForMs={node.staleForMs}
              showLabel
            />
          </div>
          <OutcomeBadge outcome={node.outcome} />
        </div>

        {/* Telemetry Metrics Grid */}
        <div className="grid grid-cols-2 gap-3">
          <div className="p-3 rounded-xl bg-background/50 border border-border/20">
            <span className="block text-[11px] uppercase tracking-wider text-text-muted mb-1">
              Lưu lượng dòng chảy
            </span>
            <div className="font-mono tabular-nums text-xl font-bold text-primary">
              {node.flowLpm.toFixed(2)}{' '}
              <span className="text-xs font-normal text-text-muted">L/phút</span>
            </div>
          </div>

          <div className="p-3 rounded-xl bg-background/50 border border-border/20">
            <span className="block text-[11px] uppercase tracking-wider text-text-muted mb-1">
              Tổng thể tích đã cấp
            </span>
            <div className="font-mono tabular-nums text-xl font-bold text-text">
              {node.litresTotal.toFixed(1)}{' '}
              <span className="text-xs font-normal text-text-muted">Lít</span>
            </div>
          </div>
        </div>

        {/* Detailed Hardware & Calibration Attributes */}
        <div className="p-3.5 rounded-xl bg-surface/40 border border-border/30 space-y-2 text-xs">
          <div className="flex items-center justify-between py-1 border-b border-border/20">
            <span className="text-text-muted flex items-center gap-1.5">
              <Cpu size={14} className="text-primary" aria-hidden="true" />
              <span>Cảm biến dòng chảy:</span>
            </span>
            <span className="font-mono tabular-nums text-text font-medium">
              {node.sensorSerial || 'YF-S201 (Mặc định)'}
            </span>
          </div>

          <div className="flex items-center justify-between py-1 border-b border-border/20">
            <span className="text-text-muted flex items-center gap-1.5">
              <Layers size={14} className="text-primary" aria-hidden="true" />
              <span>Trạng thái hiệu chuẩn:</span>
            </span>
            <span
              className={`font-semibold px-2 py-0.5 rounded text-[11px] ${
                node.calibrationStatus === 'CALIBRATED'
                  ? 'bg-primary/15 text-primary border border-primary/30'
                  : 'bg-accent-amber/15 text-accent-amber border border-accent-amber/30'
              }`}
            >
              {node.calibrationStatus === 'CALIBRATED' ? 'ĐÃ HIỆU CHUẨN' : 'CHƯA HIỆU CHUẨN'}
            </span>
          </div>

          <div className="flex items-center justify-between py-1 border-b border-border/20">
            <span className="text-text-muted flex items-center gap-1.5">
              <Activity size={14} className="text-primary" aria-hidden="true" />
              <span>Trạng thái lịch trình:</span>
            </span>
            <span className="font-mono text-text font-semibold">
              {node.scheduleState || 'IDLE'}
            </span>
          </div>

          <div className="flex items-center justify-between py-1 border-b border-border/20">
            <span className="text-text-muted flex items-center gap-1.5">
              <Calendar size={14} className="text-primary" aria-hidden="true" />
              <span>Tín hiệu gần nhất:</span>
            </span>
            <span className="font-mono text-text font-medium tabular-nums">
              {formattedLastSeen}
            </span>
          </div>

          <div className="flex items-center justify-between py-1">
            <span className="text-text-muted">Nhóm điều khiển trực thuộc:</span>
            <span className="text-text font-semibold">
              {node.cachedGroupId ? `Nhóm #${node.cachedGroupId}` : 'Chưa gán nhóm'}
            </span>
          </div>
        </div>

        {/* Fault Alert & Emergency Recovery */}
        {isFault && (
          <div className="p-3 rounded-xl bg-danger/15 border border-danger/40 space-y-2">
            <div className="flex items-center gap-2 text-danger text-sm font-semibold">
              <AlertTriangle size={18} aria-hidden="true" />
              <span>Phát hiện lỗi phần cứng hoặc trạm mất tín hiệu</span>
            </div>
            <p className="text-xs text-text-muted leading-relaxed">
              Trạm khí canh đang bị khóa an toàn (Hardware Latch). Sau khi kiểm tra dây dẫn và đường ống áp lực,
              hãy nhấn nút khôi phục bên dưới để thiết lập lại trạng thái bình thường.
            </p>

            <button
              type="button"
              onClick={handleResetFault}
              disabled={resetFaultMutation.isPending}
              className="btn-primary w-full inline-flex items-center justify-center gap-2 px-4 py-2.5 rounded-xl bg-danger hover:bg-danger/90 active:scale-95 text-text font-bold text-sm shadow-lg shadow-danger/30 cursor-pointer min-h-[48px]"
              aria-label={`Khôi phục lỗi cho ${node.displayName}`}
            >
              {resetFaultMutation.isPending ? (
                <>
                  <Loader2 size={16} className="animate-spin" aria-hidden="true" />
                  <span>Đang khôi phục...</span>
                </>
              ) : (
                <>
                  <RotateCcw size={16} aria-hidden="true" />
                  <span>Khôi phục lỗi trạm (Fault Reset)</span>
                </>
              )}
            </button>
          </div>
        )}

        {resetFaultMutation.isError && (
          <AlertBanner
            error={resetFaultMutation.error}
            fallbackContext={`Không thể khôi phục trạng thái ${node.displayName}`}
          />
        )}

        {resetFaultMutation.isSuccess && (
          <div className="p-2.5 rounded-lg bg-primary/15 border border-primary/30 text-primary text-xs text-center font-medium">
            Đã gửi yêu cầu khôi phục trạng thái trạm thành công!
          </div>
        )}


        <div className="flex justify-end pt-2">
          <button
            type="button"
            onClick={onClose}
            className="btn-secondary px-5 py-2 rounded-xl bg-surface/60 hover:bg-surface text-text-muted text-sm font-semibold border border-border/40 min-h-[44px]"
          >
            Đóng
          </button>
        </div>
      </div>
    </Modal>
  );
}
