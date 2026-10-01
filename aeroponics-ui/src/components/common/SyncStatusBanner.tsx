'use client';

import { AlertTriangle, CheckCircle, Clock, HelpCircle, RefreshCw } from 'lucide-react';
import { useEffect, useState } from 'react';
import { useSelectedDevice } from '../../lib/selected-device-context';
import { useDeviceStore } from '../../store/useDeviceStore';
import { useRetryScheduleSync } from '../../hooks/queries/useDeviceStatus';
import type { ScheduleSyncState } from '../../lib/types';

const CONTENT: Record<ScheduleSyncState, { label: string; className: string; Icon: typeof CheckCircle }> = {
  IN_SYNC: { label: 'Lịch tưới đã đồng bộ chuẩn xác với trạm phần cứng.', className: 'border-green-200 bg-green-50 text-green-800', Icon: CheckCircle },
  IN_SYNC_PENDING_BOUNDARY: { label: 'Đã nạp công thức mới; sẽ áp dụng sau khi kết thúc chu kỳ hiện tại.', className: 'border-blue-200 bg-blue-50 text-blue-800', Icon: Clock },
  SYNCING: { label: 'Đang gửi lại cấu hình xuống trạm...', className: 'border-amber-200 bg-amber-50 text-amber-800', Icon: RefreshCw },
  DRIFTED: { label: 'Lệch pha cấu hình kéo dài.', className: 'border-red-200 bg-red-50 text-red-800', Icon: AlertTriangle },
  DRIFTED_LATCHED: { label: 'Tự phục hồi thất bại 3 lần.', className: 'border-red-300 bg-red-100 text-red-900', Icon: AlertTriangle },
  UNCONFIRMED: { label: 'Đang chờ báo cáo đầu tiên từ Gateway...', className: 'border-gray-200 bg-gray-50 text-gray-600', Icon: HelpCircle },
};

export function SyncStatusBanner() {
  const { selectedDeviceId } = useSelectedDevice();
  const selected = useDeviceStore((state) => state.getDevice(selectedDeviceId || state.selectedDeviceId));
  const retry = useRetryScheduleSync();
  const syncState = selected?.syncState ?? 'UNCONFIRMED';
  const content = CONTENT[syncState];
  const Icon = content.Icon;
  const canRetry = syncState === 'DRIFTED' || syncState === 'DRIFTED_LATCHED';
  const syncDetails = selected?.scheduleSyncDetails;
  const [reportTimedOut, setReportTimedOut] = useState(false);
  useEffect(() => {
    if (syncState !== 'SYNCING') {
      setReportTimedOut(false);
      return undefined;
    }
    const startedAt = selected?.scheduleSyncUpdatedAt ? Date.parse(selected.scheduleSyncUpdatedAt) : Date.now();
    const timer = window.setTimeout(() => setReportTimedOut(true), Math.max(0, 30_000 - (Date.now() - startedAt)));
    return () => window.clearTimeout(timer);
  }, [selected?.scheduleSyncUpdatedAt, syncState]);
  const failureReason = typeof syncDetails?.ackReason === 'string'
    ? syncDetails.ackReason
    : typeof syncDetails?.message === 'string'
      ? syncDetails.message
      : null;
  const desiredGroups = Array.isArray(syncDetails?.desiredGroups) ? syncDetails.desiredGroups.join(', ') : null;
  const groupsToDisable = Array.isArray(syncDetails?.groupsToDisable) ? syncDetails.groupsToDisable.join(', ') : null;
  const pendingNodes = Array.isArray(syncDetails?.pendingNodes) ? syncDetails.pendingNodes.join(', ') : null;
  const driftedNodes = Array.isArray(syncDetails?.driftedNodes) ? syncDetails.driftedNodes.join(', ') : null;
  const slotMismatches = Array.isArray(syncDetails?.slotMismatches) ? syncDetails.slotMismatches.length : 0;
  const assignmentMismatches = Array.isArray(syncDetails?.assignmentMismatches) ? syncDetails.assignmentMismatches.length : 0;
  const detailMessage = syncDetails?.reason === 'MISSING_NODE_ASSIGNMENTS'
    ? 'Gateway chưa báo cáo node assignment.'
    : syncDetails?.reason === 'SLOTS_NOT_RECONCILED'
      ? 'Gateway chưa xác nhận control-slot.'
      : syncDetails?.reason === 'STALE_ASSIGNMENT_REPORT'
        ? 'Gateway đang báo cáo assignment phiên bản cũ.'
      : syncDetails?.reason === 'NODE_ASSIGNMENT_MISMATCH'
        ? `Node lệch assignment: ${driftedNodes ?? pendingNodes ?? 'chưa rõ'}.`
        : syncDetails?.reason === 'SLOT_MISMATCH'
          ? `Có ${slotMismatches} slot khác cấu hình Gateway.`
          : null;

  return (
    <div className={`mb-3 flex items-center gap-3 rounded-lg border px-4 py-3 text-sm ${content.className}`} role="status">
      <Icon className={`h-5 w-5 shrink-0 ${syncState === 'SYNCING' ? 'animate-spin' : ''}`} />
      <span className="flex-1">{content.label}</span>
      {syncState === 'SYNCING' && desiredGroups && (
        <span className="text-xs font-medium">Group đang đồng bộ: {desiredGroups}</span>
      )}
      {syncState === 'SYNCING' && groupsToDisable && (
        <span className="text-xs font-medium">Tắt group: {groupsToDisable}</span>
      )}
      {failureReason && (syncState === 'DRIFTED' || syncState === 'DRIFTED_LATCHED') && (
        <span className="text-xs font-medium" title={failureReason}>{failureReason}</span>
      )}
      {detailMessage && (syncState === 'DRIFTED' || syncState === 'DRIFTED_LATCHED' || syncState === 'UNCONFIRMED') && (
        <span className="text-xs font-medium" title={`assignment mismatches: ${assignmentMismatches}`}>
          {detailMessage}
        </span>
      )}
      {reportTimedOut && syncState === 'SYNCING' && (
        <span className="text-xs font-medium">Không nhận được xác nhận cấu hình thực tế từ Gateway.</span>
      )}
      {canRetry && selectedDeviceId && (
        <button
          type="button"
          disabled={retry.isPending}
          onClick={() => retry.mutate(selectedDeviceId)}
          className="rounded-md border border-current px-3 py-1.5 font-medium disabled:opacity-50"
        >
          {retry.isPending ? 'Đang gửi...' : 'Thử nạp lại'}
        </button>
      )}
    </div>
  );
}
