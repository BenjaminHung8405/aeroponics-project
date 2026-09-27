'use client';

import React, { useState } from 'react';
import { Modal } from '../common/Modal';
import { OutcomeBadge } from '../common/OutcomeBadge';
import { StalenessIndicator } from '../common/StalenessIndicator';
import { AlertBanner } from '../common/AlertBanner';
import { useToast } from '../common/Toast';
import { SUCCESS_MESSAGES } from '../../lib/messages';
import { useResetNodeFault, useSendPumpOverride } from '../../hooks/queries/useNodes';
import { useActiveSeason } from '../../hooks/queries/useSeason';
import {
  Activity,
  AlertTriangle,
  RotateCcw,
  Loader2,
  Calendar,
  Layers,
  Cpu,
  Sliders,
  Play,
  Square,
  Clock,
  ShieldCheck,
  Leaf,
} from 'lucide-react';

import type { NodeState } from '../../store/useNodeStore';
import { NodeRecipeTab } from './NodeRecipeTab';

interface NodeDetailModalProps {
  node: NodeState;
  isOpen: boolean;
  onClose: () => void;
  disabled?: boolean;
  initialTab?: 'telemetry' | 'recipe' | 'control';
}

/**
 * NodeDetailModal Component
 * Displays comprehensive telemetry, sensor calibration details, and fault reset action.
 * Follows:
 *  - S4-D3: Command log / node status inspection
 *  - S4-DS-TOUCH-15: Reset button min-h-[48px], active:scale-95
 *  - S4-DS-ICON-14: Zero emoji, 100% Lucide SVG
 */
export function NodeDetailModal({
  node,
  isOpen,
  onClose,
  disabled = false,
  initialTab = 'telemetry',
}: NodeDetailModalProps) {
  const { toast } = useToast();
  const resetFaultMutation = useResetNodeFault();
  const [activeTab, setActiveTab] = useState<'telemetry' | 'recipe' | 'control'>(initialTab);

  React.useEffect(() => {
    if (isOpen) {
      setActiveTab(initialTab);
    }
  }, [isOpen, initialTab]);

  const handleResetFault = async () => {
    try {
      await resetFaultMutation.mutateAsync(node.id);
      toast.success(SUCCESS_MESSAGES.RESET_FAULT(node.displayName));
    } catch {
      // Error handled by AlertBanner
    }
  };

  const overrideMutation = useSendPumpOverride();
  const { data: activeSeason, isLoading: isActiveSeasonLoading } = useActiveSeason();
  const [selectedLeaseSec, setSelectedLeaseSec] = React.useState<number>(30);

  const handleOverrideOn = async () => {
    try {
      await overrideMutation.mutateAsync({
        node_id: node.id,
        target_type: 'NODE',
        action: 'ON',
        run_lease_ms: selectedLeaseSec * 1000,
      });
      toast.success(SUCCESS_MESSAGES.PUMP_OVERRIDE_ON(node.displayName, selectedLeaseSec));
    } catch {
      // Error handled by AlertBanner
    }
  };

  const handleOverrideOff = async () => {
    try {
      await overrideMutation.mutateAsync({
        node_id: node.id,
        target_type: 'NODE',
        action: 'OFF',
        override_duration_ms: 600000, // 10 minutes temporary pause
      });
      toast.success(SUCCESS_MESSAGES.PUMP_OVERRIDE_OFF(node.displayName));
    } catch {
      // Error handled by AlertBanner
    }
  };

  const isFault = node.healthStatus === 'FAULT' || node.outcome?.startsWith('FAULT_');
  const canOverride = Boolean(activeSeason) && !isActiveSeasonLoading && !disabled;

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
        {/* Tabs */}
        <div role="tablist" className="flex gap-0 border-b border-border/30 -mx-1 mb-4">
          <button
            role="tab"
            aria-selected={activeTab === 'telemetry'}
            aria-controls="panel-telemetry"
            onClick={() => setActiveTab('telemetry')}
            className={`px-4 py-2.5 text-sm font-semibold transition-all cursor-pointer min-h-[44px] active:scale-95 flex items-center gap-2 ${
              activeTab === 'telemetry'
                ? 'border-b-2 border-primary text-primary font-bold'
                : 'text-text-muted hover:text-text border-b-2 border-transparent'
            }`}
          >
            <Activity size={16} /> Telemetry
          </button>
          <button
            role="tab"
            aria-selected={activeTab === 'recipe'}
            aria-controls="panel-recipe"
            onClick={() => setActiveTab('recipe')}
            className={`px-4 py-2.5 text-sm font-semibold transition-all cursor-pointer min-h-[44px] active:scale-95 flex items-center gap-2 ${
              activeTab === 'recipe'
                ? 'border-b-2 border-primary text-primary font-bold'
                : 'text-text-muted hover:text-text border-b-2 border-transparent'
            }`}
          >
            <Leaf size={16} /> Recipe
          </button>
          <button
            role="tab"
            aria-selected={activeTab === 'control'}
            aria-controls="panel-control"
            onClick={() => setActiveTab('control')}
            className={`px-4 py-2.5 text-sm font-semibold transition-all cursor-pointer min-h-[44px] active:scale-95 flex items-center gap-2 ${
              activeTab === 'control'
                ? 'border-b-2 border-primary text-primary font-bold'
                : 'text-text-muted hover:text-text border-b-2 border-transparent'
            }`}
          >
            <Sliders size={16} /> Control
          </button>
        </div>

        {activeTab === 'telemetry' && (
          <div id="panel-telemetry" className="space-y-4">
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
              <OutcomeBadge outcome={node.outcome} nodeFlowConfirmed={node.flowConfirmed} />
            </div>

            {/* Evidence Pipeline — Server-authoritative display only */}
            <div className="p-3.5 rounded-xl bg-surface/40 border border-border/30">
              <span className="block text-[11px] uppercase tracking-wider text-text-muted mb-2 font-medium">
                Pipeline xác nhận dòng chảy
              </span>
              <div className="flex items-center gap-1">
                {[
                  { label: 'Lệnh đã gửi', done: node.outcome !== 'PENDING' && node.outcome !== null },
                  { label: 'RF đã nhận (ACK)', done: ['RF_ACKED', 'FLOW_CONFIRMED'].includes(node.outcome ?? '') },
                  { label: 'Cảm biến dòng chảy', done: node.flowConfirmed },
                  { label: 'Xác nhận dòng chảy', done: node.flowConfirmed && node.outcome === 'FLOW_CONFIRMED' },
                ].map((stage, idx) => (
                  <React.Fragment key={stage.label}>
                    {idx > 0 && (
                      <div className={`h-px flex-1 ${stage.done ? 'bg-primary' : 'bg-border/40'}`} />
                    )}
                    <div className="flex flex-col items-center gap-1 min-w-0 flex-1">
                      <div
                        className={`w-3 h-3 rounded-full border-2 transition-all duration-200 ${
                          stage.done
                            ? 'bg-primary border-primary shadow-sm shadow-primary/30'
                            : 'bg-background border-border/50'
                        }`}
                      />
                      <span className={`text-[10px] text-center leading-tight ${stage.done ? 'text-primary font-medium' : 'text-text-muted'}`}>
                        {stage.label}
                      </span>
                    </div>
                  </React.Fragment>
                ))}
              </div>
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

            {/* Fault Alert in Telemetry Tab */}
            {isFault && (
              <div className="p-3 rounded-xl bg-danger/15 border border-danger/40 space-y-2">
                <div className="flex items-center gap-2 text-danger text-sm font-semibold">
                  <AlertTriangle size={18} aria-hidden="true" />
                  <span>Phát hiện lỗi phần cứng hoặc trạm mất tín hiệu</span>
                </div>
                <p className="text-xs text-text-muted leading-relaxed">
                  Trạm khí canh đang bị khóa an toàn (Hardware Latch). Có thể khôi phục ở tab Control.
                </p>
              </div>
            )}
          </div>
        )}

        {activeTab === 'recipe' && (
          <div id="panel-recipe">
            <NodeRecipeTab node={node} onClose={onClose} />
          </div>
        )}

        {activeTab === 'control' && (
          <div id="panel-control" className="space-y-4">
            {/* Manual Pump Override Panel */}
            <div className="p-4 rounded-xl bg-surface/70 border border-border/40 space-y-3">
              <div className="flex items-center justify-between gap-2">
                <div className="flex items-center gap-2">
                  <Sliders size={16} className="text-primary" aria-hidden="true" />
                  <span className="text-sm font-bold text-text">Điều Khiển Bơm Cưỡng Bức</span>
                </div>

                {/* Current Override Status Badge */}
                <span
                  className={`text-[11px] font-bold px-2 py-0.5 rounded border uppercase tracking-wider ${
                    node.overrideState === 'OVERRIDE_ON'
                      ? 'bg-primary/20 text-primary border-primary/40 relay-glow-active'
                      : node.overrideState === 'OVERRIDE_OFF'
                        ? 'bg-accent-amber/20 text-accent-amber border-accent-amber/40'
                        : 'bg-surface text-text-subtle border-border/30'
                  }`}
                >
                  {node.overrideState === 'OVERRIDE_ON'
                    ? 'ĐANG BẬT CƯỠNG BỨC'
                    : node.overrideState === 'OVERRIDE_OFF'
                      ? 'ĐANG TẮT CƯỠNG BỨC'
                      : 'TỰ ĐỘNG THEO LỊCH'}
                </span>
              </div>

              <p className="text-xs text-text-muted leading-relaxed">
                Kích hoạt hoặc ngắt bơm thủ công phục vụ kiểm tra vỉ phun và làm ẩm khẩn cấp. Lệnh bật được bảo vệ bởi
                cơ chế <span className="text-primary font-semibold">Deadman Lease</span> chống cháy bơm khi mất kết nối RF.
              </p>

              {!isActiveSeasonLoading && !activeSeason && (
                <div className="p-3 rounded-lg bg-accent-amber/10 border border-accent-amber/30 text-accent-amber text-xs leading-relaxed">
                  Chưa có vụ mùa đang hoạt động. Hãy khởi tạo vụ mùa ở mục <strong>Vụ Mùa Hiện Tại</strong> trước khi điều khiển bơm.
                </div>
              )}

              {/* Lease Duration Selection */}
              <div>
                <span className="text-[11px] uppercase tracking-wider text-text-muted mb-1.5 flex items-center gap-1 font-medium">
                  <Clock size={12} className="text-primary" aria-hidden="true" />
                  <span>Thời hạn bật an toàn (Lease Duration):</span>
                </span>
                <div className="grid grid-cols-3 gap-2">
                  {[15, 30, 60].map((sec) => (
                    <button
                      key={sec}
                      type="button"
                      onClick={() => setSelectedLeaseSec(sec)}
                      className={`py-2 px-3 rounded-lg text-xs font-mono font-semibold transition-all cursor-pointer min-h-[44px] active:scale-95 border ${
                        selectedLeaseSec === sec
                          ? 'bg-primary/20 text-primary border-primary shadow-sm shadow-primary/20'
                          : 'bg-background/60 text-text-muted border-border/30 hover:bg-surface'
                      }`}
                    >
                      {sec} giây
                    </button>
                  ))}
                </div>
              </div>

              {/* Action Buttons: ON (Emerald) & OFF (Amber/Red) */}
              <div className="grid grid-cols-1 sm:grid-cols-2 gap-2 pt-1">
                <button
                  type="button"
                  onClick={handleOverrideOn}
                  disabled={isFault || overrideMutation.isPending || !canOverride}
                  className="btn-primary w-full inline-flex items-center justify-center gap-2 px-4 py-3 rounded-xl bg-primary hover:bg-primary/90 active:scale-95 text-background font-bold text-sm shadow-lg shadow-primary/25 cursor-pointer disabled:opacity-50 disabled:cursor-not-allowed min-h-[48px] transition-all"
                  aria-label={`Bật bơm tức thời ${selectedLeaseSec} giây cho ${node.displayName}`}
                >
                  {overrideMutation.isPending && overrideMutation.variables?.action === 'ON' ? (
                    <>
                      <Loader2 size={16} className="animate-spin" aria-hidden="true" />
                      <span>Đang kích hoạt...</span>
                    </>
                  ) : (
                    <>
                      <Play size={16} aria-hidden="true" fill="currentColor" />
                      <span>Bật Bơm ({selectedLeaseSec}s)</span>
                    </>
                  )}
                </button>

                <button
                  type="button"
                  onClick={handleOverrideOff}
                  disabled={isFault || overrideMutation.isPending || !canOverride}
                  className="btn-secondary w-full inline-flex items-center justify-center gap-2 px-4 py-3 rounded-xl bg-accent-amber/15 hover:bg-accent-amber/25 active:scale-95 text-accent-amber border border-accent-amber/40 font-bold text-sm cursor-pointer disabled:opacity-50 disabled:cursor-not-allowed min-h-[48px] transition-all"
                  aria-label={`Tắt bơm cưỡng bức cho ${node.displayName}`}
                >
                  {overrideMutation.isPending && overrideMutation.variables?.action === 'OFF' ? (
                    <>
                      <Loader2 size={16} className="animate-spin" aria-hidden="true" />
                      <span>Đang ngắt bơm...</span>
                    </>
                  ) : (
                    <>
                      <Square size={16} aria-hidden="true" fill="currentColor" />
                      <span>Tắt Bơm Khẩn Cấp</span>
                    </>
                  )}
                </button>
              </div>

              {/* Feedback banners */}
              {overrideMutation.isError && (
                <AlertBanner
                  error={overrideMutation.error}
                  fallbackContext={`Không thể gửi lệnh điều khiển ${node.displayName}`}
                />
              )}

              {overrideMutation.isSuccess && (
                <div className="p-2.5 rounded-lg bg-primary/15 border border-primary/30 text-primary text-xs flex items-center gap-2 font-medium">
                  <ShieldCheck size={16} className="shrink-0" aria-hidden="true" />
                  <span>Lệnh điều khiển bơm đã được gửi thành công!</span>
                </div>
              )}
            </div>

            {/* Fault Reset button in Control Tab */}
            {isFault && (
              <div className="p-3 rounded-xl bg-danger/15 border border-danger/40 space-y-2">
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
        )}
      </div>
    </Modal>
  );
}

