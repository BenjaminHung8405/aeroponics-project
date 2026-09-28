'use client';

import React from 'react';
import { useNode } from '../../store/useNodeStore';
import { useNodeStore } from '../../store/useNodeStore';
import { useSendPumpOverride } from '../../hooks/queries/useNodes';
import { useToast } from '../common/Toast';
import { SUCCESS_MESSAGES, formatUserErrorMessage } from '../../lib/messages';
import { isNodeRunning } from '../../lib/types';
import { Play, Square, Loader2 } from 'lucide-react';

/** Deadman lease: pump auto-stops if the gateway link drops mid-run. */
const RUN_LEASE_MS = 60_000;
/** Temporary pause applied by the OFF command. */
const OVERRIDE_OFF_MS = 600_000;

interface PumpControlProps {
  nodeId: number;
  disabled?: boolean;
}

/**
 * PumpControl Component
 * Command-pattern pump control. The button click is NOT evidence of flow:
 * it only records a PENDING outcome and waits for the server/WebSocket.
 *
 * Follows:
 *  - S4-NOOPT-01: click sets outcome=PENDING, never RUNNING.
 *  - S4-WS-02: RUNNING is derived from `isNodeRunning()` (WS-authoritative).
 *  - S4-WS-04: PENDING badge renders "Đang gửi lệnh".
 */
export function PumpControl({ nodeId, disabled = false }: PumpControlProps) {
  const node = useNode(nodeId);
  const overrideMutation = useSendPumpOverride();
  const { toast } = useToast();

  const isRunning = isNodeRunning(node);
  // Uncalibrated flow sensor cannot prove flow, so manual control stays locked.
  const isDisabled = disabled || overrideMutation.isPending || node.calibrationStatus !== 'CALIBRATED';

  // Human-readable hint explaining why the button is disabled
  const disabledReason: string | null =
    node.calibrationStatus !== 'CALIBRATED'
      ? 'Cảm biến chưa hiệu chuẩn'
      : disabled
        ? 'Chưa có vụ mùa hoạt động'
        : null;

  const handleOnClick = async () => {
    try {
        await overrideMutation.mutateAsync({
          node_id: nodeId,
          target_type: 'NODE',
          action: 'ON',
        run_lease_ms: RUN_LEASE_MS,
      });
      // PENDING only: the badge must not claim flow before the sensor confirms.
      useNodeStore.getState().updateOutcome(nodeId, 'PENDING');
      toast.success(
        SUCCESS_MESSAGES.PUMP_OVERRIDE_ON(node.displayName, RUN_LEASE_MS / 1000),
        'Đang chờ cảm biến xác nhận dòng chảy.',
      );
    } catch (error) {
      const { title, message } = formatUserErrorMessage(
        error,
        `Không thể gửi lệnh bật bơm cho ${node.displayName}`,
      );
      toast.error(title, message);
    }
  };

  const handleOffClick = async () => {
    try {
        await overrideMutation.mutateAsync({
          node_id: nodeId,
          target_type: 'NODE',
          action: 'OFF',
        override_duration_ms: OVERRIDE_OFF_MS,
      });
      useNodeStore.getState().updateOutcome(nodeId, 'PENDING');
      toast.success(SUCCESS_MESSAGES.PUMP_OVERRIDE_OFF(node.displayName));
    } catch (error) {
      const { title, message } = formatUserErrorMessage(
        error,
        `Không thể gửi lệnh tắt bơm cho ${node.displayName}`,
      );
      toast.error(title, message);
    }
  };

  return (
    <div className="flex flex-col gap-1" data-testid="pump-control">
      <div className="flex items-center gap-2">
        {isRunning ? (
          <button
            type="button"
            onClick={handleOffClick}
            disabled={isDisabled}
            data-testid="pump-off"
            className="btn-secondary inline-flex items-center justify-center gap-1.5 px-3.5 py-2 rounded-xl bg-accent-amber/15 hover:bg-accent-amber/25 active:scale-95 text-accent-amber border border-accent-amber/40 text-xs font-bold cursor-pointer disabled:opacity-50 disabled:cursor-not-allowed min-h-[44px] transition-all"
            aria-label={`Tắt bơm cho ${node.displayName}`}
          >
            {overrideMutation.isPending ? (
              <Loader2 size={14} className="animate-spin" aria-hidden="true" />
            ) : (
              <Square size={14} aria-hidden="true" fill="currentColor" />
            )}
            <span>Tắt Bơm</span>
          </button>
        ) : (
          <button
            type="button"
            onClick={handleOnClick}
            disabled={isDisabled}
            data-testid="pump-on"
            className="btn-primary inline-flex items-center justify-center gap-1.5 px-3.5 py-2 rounded-xl bg-primary hover:bg-primary/90 active:scale-95 text-background text-xs font-bold shadow-lg shadow-primary/25 cursor-pointer disabled:opacity-50 disabled:cursor-not-allowed min-h-[44px] transition-all"
            aria-label={`Bật bơm ${RUN_LEASE_MS / 1000} giây cho ${node.displayName}`}
          >
            {overrideMutation.isPending ? (
              <Loader2 size={14} className="animate-spin" aria-hidden="true" />
            ) : (
              <Play size={14} aria-hidden="true" fill="currentColor" />
            )}
            <span>Bật Bơm ({RUN_LEASE_MS / 1000}s)</span>
          </button>
        )}

        {overrideMutation.isError && (
          <span className="text-[11px] font-medium text-danger">Không gửi được lệnh</span>
        )}
      </div>

      {/* Task 2: Explain why button is disabled — helps user take corrective action */}
      {disabledReason && !overrideMutation.isPending && (
        <span className="text-[10px] text-text-muted italic leading-tight pl-0.5">
          {disabledReason}
        </span>
      )}
    </div>
  );
}
