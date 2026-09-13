'use client';

import React, { useState, useEffect } from 'react';
import {
  useLatestMeasurement,
  useMeasurementHistory,
  useTriggerMeasurement,
} from '../../hooks/queries/useMeasurement';
import {
  Thermometer,
  Loader2,
  Droplets,
  Activity,
  Battery,
  AlertTriangle,
  Clock,
  Sparkles,
} from 'lucide-react';

/**
 * MeasurementPanel Component
 * Follows:
 *  - S4-D5: On-demand water quality monitoring with Tuya PH-W218 sensor
 *  - S4-ON-DEMAND-11: On-demand measurement only, no recurring loop polling (100% manual request)
 *  - S4-DS-ICON-14: Lucide Thermometer icon (Zero emoji)
 *  - S4-DS-TOUCH-15: Trigger button min-h-[48px], active:scale-95
 *  - 429 Cooldown countdown timer (60s) to protect sensor probes
 *  - Responsive history table with overflow-x-auto
 */
export function MeasurementPanel() {
  const { data: latestReading, isLoading: isLatestLoading } = useLatestMeasurement();
  const { data: historyData, isLoading: isHistoryLoading } = useMeasurementHistory({ limit: 10 });
  const triggerMutation = useTriggerMeasurement();

  // 60-second cooldown timer state machine
  const [cooldownRemaining, setCooldownRemaining] = useState<number>(0);

  useEffect(() => {
    if (cooldownRemaining <= 0) return;

    const timer = setInterval(() => {
      setCooldownRemaining((prev) => Math.max(0, prev - 1));
    }, 1000);

    return () => clearInterval(timer);
  }, [cooldownRemaining]);

  const handleTrigger = async () => {
    if (cooldownRemaining > 0 || triggerMutation.isPending) return;

    try {
      await triggerMutation.mutateAsync({ trigger_type: 'ON_DEMAND' });
      // Start 60s cooldown to protect Tuya socket & probe
      setCooldownRemaining(60);
    } catch (err: any) {
      // If HTTP 429 (rate limited / cooldown active), start 60s cooldown
      if (err?.message?.includes('429') || err?.status === 429) {
        setCooldownRemaining(60);
      }
    }
  };

  const isButtonDisabled = triggerMutation.isPending || cooldownRemaining > 0;

  // Format timestamp helper
  const formatTime = (isoString?: string | null) => {
    if (!isoString) return '—';
    try {
      const d = new Date(isoString);
      if (Number.isNaN(d.getTime())) return '—';
      return d.toLocaleString('vi-VN', {
        timeZone: 'Asia/Ho_Chi_Minh',
        dateStyle: 'short',
        timeStyle: 'medium',
      });
    } catch {
      return isoString;
    }
  };

  return (
    <section id="measurement-panel" aria-labelledby="measurement-heading" className="glass-card p-5 space-y-5">
      {/* Header with Title and "Đo ngay" Button */}
      <div className="flex flex-col sm:flex-row sm:items-center justify-between gap-3 border-b border-border/20 pb-3">
        <div className="flex items-center gap-2.5">
          <div className="p-2 rounded-xl bg-primary/15 border border-primary/30 text-primary">
            <Thermometer size={20} aria-hidden="true" />
          </div>
          <div>
            <h2 id="measurement-heading" className="text-base sm:text-lg font-bold text-text">
              Giám Sát Dung Dịch Khí Canh (Tuya PH-W218)
            </h2>
            <p className="text-xs text-text-muted">
              Đo lường tức thời theo yêu cầu (On-Demand) — Bảo vệ đầu dò cảm biến
            </p>
          </div>
        </div>

        {/* Primary Action Button: "Đo ngay" */}
        <button
          type="button"
          onClick={handleTrigger}
          disabled={isButtonDisabled}
          className="btn-primary w-full sm:w-auto inline-flex items-center justify-center gap-2 px-5 py-2.5 rounded-xl bg-primary hover:bg-primary/90 disabled:bg-surface disabled:text-text-subtle disabled:border-border/30 active:scale-95 text-background font-bold text-sm shadow-lg shadow-primary/20 min-h-[48px] cursor-pointer transition-all duration-150"
          aria-label="Kích hoạt phiên đo lường chất lượng nước ngay lập tức"
        >
          {triggerMutation.isPending ? (
            <>
              <Loader2 size={18} className="animate-spin" aria-hidden="true" />
              <span>Đang đo mẫu nước...</span>
            </>
          ) : cooldownRemaining > 0 ? (
            <>
              <Clock size={18} aria-hidden="true" />
              <span>Nghỉ cảm biến ({cooldownRemaining}s)</span>
            </>
          ) : (
            <>
              <Thermometer size={18} aria-hidden="true" />
              <span>Đo ngay</span>
            </>
          )}
        </button>
      </div>

      {/* Error Banner */}
      {triggerMutation.isError && (
        <div className="p-3 rounded-xl bg-danger/15 border border-danger/30 text-danger text-xs flex items-center gap-2">
          <AlertTriangle size={16} className="shrink-0" aria-hidden="true" />
          <span>
            {triggerMutation.error?.message?.includes('429')
              ? 'Thiết bị đang trong thời gian làm nguội 60s. Vui lòng chờ trước khi đo lại.'
              : `Lỗi kích hoạt đo: ${triggerMutation.error?.message || 'Không thể kết nối cảm biến'}`}
          </span>
        </div>
      )}

      {/* 7 Sensor Readings Grid */}
      <div>
        <div className="flex items-center justify-between mb-2.5">
          <span className="text-xs font-semibold uppercase tracking-wider text-text-muted flex items-center gap-1.5">
            <Activity size={14} className="text-primary" aria-hidden="true" />
            <span>Thông số đo lường mới nhất</span>
          </span>
          {latestReading?.time && (
            <span className="text-[11px] text-text-subtle font-mono tabular-nums">
              Cập nhật: {formatTime(latestReading.time)}
            </span>
          )}
        </div>

        {isLatestLoading ? (
          <div className="grid grid-cols-2 sm:grid-cols-4 gap-3 animate-pulse">
            {[1, 2, 3, 4, 5, 6, 7].map((i) => (
              <div key={i} className="h-20 bg-surface/50 rounded-xl" />
            ))}
          </div>
        ) : (
          <div className="grid grid-cols-2 sm:grid-cols-4 gap-3">
            {/* 1. pH */}
            <div className="p-3 rounded-xl bg-background/60 border border-border/30">
              <span className="block text-[11px] text-text-muted font-medium mb-1">Độ pH</span>
              <div className="font-mono tabular-nums text-xl sm:text-2xl font-bold text-primary">
                {latestReading?.ph != null ? latestReading.ph.toFixed(2) : '—'}
              </div>
              <span className="block text-[10px] text-text-subtle mt-0.5">Tiêu chuẩn: [5.5 - 6.5]</span>
            </div>

            {/* 2. EC */}
            <div className="p-3 rounded-xl bg-background/60 border border-border/30">
              <span className="block text-[11px] text-text-muted font-medium mb-1">Độ Dẫn EC</span>
              <div className="font-mono tabular-nums text-xl sm:text-2xl font-bold text-secondary">
                {latestReading?.ec != null ? latestReading.ec.toFixed(2) : '—'}{' '}
                <span className="text-xs font-normal text-text-muted">mS/cm</span>
              </div>
              <span className="block text-[10px] text-text-subtle mt-0.5">Tiêu chuẩn: [1.2 - 2.0]</span>
            </div>

            {/* 3. TDS */}
            <div className="p-3 rounded-xl bg-background/60 border border-border/30">
              <span className="block text-[11px] text-text-muted font-medium mb-1">Chỉ Số TDS</span>
              <div className="font-mono tabular-nums text-xl sm:text-2xl font-bold text-text">
                {latestReading?.tds != null ? latestReading.tds.toFixed(0) : '—'}{' '}
                <span className="text-xs font-normal text-text-muted">ppm</span>
              </div>
              <span className="block text-[10px] text-text-subtle mt-0.5">Tổng chất rắn hòa tan</span>
            </div>

            {/* 4. Temperature */}
            <div className="p-3 rounded-xl bg-background/60 border border-border/30">
              <span className="block text-[11px] text-text-muted font-medium mb-1">Nhiệt Độ Nước</span>
              <div className="font-mono tabular-nums text-xl sm:text-2xl font-bold text-accent-amber">
                {latestReading?.temperature_c != null ? latestReading.temperature_c.toFixed(1) : '—'}{' '}
                <span className="text-xs font-normal text-text-muted">°C</span>
              </div>
              <span className="block text-[10px] text-text-subtle mt-0.5">Khoang rễ khí canh</span>
            </div>

            {/* 5. Salinity */}
            <div className="p-3 rounded-xl bg-background/60 border border-border/30">
              <span className="block text-[11px] text-text-muted font-medium mb-1">Độ Mặn</span>
              <div className="font-mono tabular-nums text-xl sm:text-2xl font-bold text-text">
                {latestReading?.salinity != null ? latestReading.salinity.toFixed(2) : '—'}{' '}
                <span className="text-xs font-normal text-text-muted">‰</span>
              </div>
              <span className="block text-[10px] text-text-subtle mt-0.5">Nồng độ muối hòa tan</span>
            </div>

            {/* 6. ORP */}
            <div className="p-3 rounded-xl bg-background/60 border border-border/30">
              <span className="block text-[11px] text-text-muted font-medium mb-1">Oxy Hóa Khử ORP</span>
              <div className="font-mono tabular-nums text-xl sm:text-2xl font-bold text-accent-indigo">
                {latestReading?.orp != null ? latestReading.orp.toFixed(0) : '—'}{' '}
                <span className="text-xs font-normal text-text-muted">mV</span>
              </div>
              <span className="block text-[10px] text-text-subtle mt-0.5">Điện thế khử oxy hóa</span>
            </div>

            {/* 7. Turbidity */}
            <div className="p-3 rounded-xl bg-background/60 border border-border/30">
              <span className="block text-[11px] text-text-muted font-medium mb-1">Độ Đục</span>
              <div className="font-mono tabular-nums text-xl sm:text-2xl font-bold text-text">
                {latestReading?.turbidity != null ? latestReading.turbidity.toFixed(1) : '—'}{' '}
                <span className="text-xs font-normal text-text-muted">NTU</span>
              </div>
              <span className="block text-[10px] text-text-subtle mt-0.5">Độ trong dung dịch</span>
            </div>

            {/* 8. Battery / Diagnostics */}
            <div className="p-3 rounded-xl bg-background/60 border border-border/30">
              <span className="block text-[11px] text-text-muted font-medium mb-1 flex items-center gap-1">
                <Battery size={13} className="text-primary" aria-hidden="true" />
                <span>Nguồn Điện Cảm Biến</span>
              </span>
              <div className="font-mono tabular-nums text-xl sm:text-2xl font-bold text-text">
                {latestReading?.battery_pct != null ? `${latestReading.battery_pct}%` : 'AC Line'}
              </div>
              <span className="block text-[10px] text-text-subtle mt-0.5">Nguồn cấp liên tục</span>
            </div>
          </div>
        )}
      </div>

      {/* Measurement History Table (overflow-x-auto protects mobile 375px) */}
      <div className="space-y-2 pt-2 border-t border-border/20">
        <div className="flex items-center justify-between">
          <h3 className="text-xs sm:text-sm font-bold text-text">
            Lịch Sử Đo Gần Đây
          </h3>
          <span className="text-[11px] text-text-muted">
            {historyData?.total ? `${historyData.total} lần đo` : 'Chưa có bản ghi'}
          </span>
        </div>

        <div className="overflow-x-auto rounded-xl border border-border/30">
          <table className="w-full text-left text-xs whitespace-nowrap">
            <thead className="bg-surface/80 border-b border-border/30 text-text-muted uppercase text-[10px] font-semibold tracking-wider">
              <tr>
                <th className="px-3.5 py-2.5">Thời Gian</th>
                <th className="px-3.5 py-2.5">pH</th>
                <th className="px-3.5 py-2.5">EC (mS/cm)</th>
                <th className="px-3.5 py-2.5">TDS (ppm)</th>
                <th className="px-3.5 py-2.5">Nhiệt Độ (°C)</th>
                <th className="px-3.5 py-2.5">Loại Đo</th>
                <th className="px-3.5 py-2.5">Người Đo</th>
              </tr>
            </thead>
            <tbody className="divide-y divide-border/20 font-mono tabular-nums bg-background/30">
              {isHistoryLoading ? (
                <tr>
                  <td colSpan={7} className="px-3.5 py-6 text-center text-text-subtle font-sans">
                    <Loader2 size={16} className="animate-spin inline-block mr-2 text-primary" aria-hidden="true" />
                    Đang tải lịch sử đo...
                  </td>
                </tr>
              ) : historyData?.items && historyData.items.length > 0 ? (
                historyData.items.map((item) => (
                  <tr key={item.id} className="hover:bg-surface/50 transition-colors">
                    <td className="px-3.5 py-2.5 text-text font-sans">
                      {formatTime(item.time)}
                    </td>
                    <td className="px-3.5 py-2.5 text-primary font-bold">
                      {item.ph != null ? item.ph.toFixed(2) : '—'}
                    </td>
                    <td className="px-3.5 py-2.5 text-secondary">
                      {item.ec != null ? item.ec.toFixed(2) : '—'}
                    </td>
                    <td className="px-3.5 py-2.5 text-text">
                      {item.tds != null ? item.tds.toFixed(0) : '—'}
                    </td>
                    <td className="px-3.5 py-2.5 text-accent-amber">
                      {item.temperature_c != null ? item.temperature_c.toFixed(1) : '—'}
                    </td>
                    <td className="px-3.5 py-2.5 text-text-muted font-sans text-[11px]">
                      <span className="px-2 py-0.5 rounded bg-surface border border-border/30">
                        {item.trigger_type || 'ON_DEMAND'}
                      </span>
                    </td>
                    <td className="px-3.5 py-2.5 text-text-subtle font-sans text-[11px]">
                      {item.operator || 'Hệ thống'}
                    </td>
                  </tr>
                ))
              ) : (
                <tr>
                  <td colSpan={7} className="px-3.5 py-6 text-center text-text-subtle font-sans">
                    Chưa có dữ liệu lịch sử đo lường. Hãy nhấn &quot;Đo ngay&quot; để lấy mẫu đầu tiên.
                  </td>
                </tr>
              )}
            </tbody>
          </table>
        </div>
      </div>
    </section>
  );
}
