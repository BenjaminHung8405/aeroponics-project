'use client';

import React, { useState } from 'react';
import {
  useActiveSeason,
  useCreateSeason,
} from '../../hooks/queries/useSeason';
import { EndSeasonModal } from './EndSeasonModal';
import { AlertBanner } from '../common/AlertBanner';
import { useToast } from '../common/Toast';
import { SUCCESS_MESSAGES } from '../../lib/messages';
import {
  Calendar,
  Clock,
  Plus,
  AlertTriangle,
  Loader2,
  CheckCircle2,
  Sparkles,
} from 'lucide-react';

/**
 * SeasonPanel Component
 * Hard Rule S4-SEASON-10:
 *  - Renders active season info (name, start date, days elapsed, targets)
 *  - Empty state: Renders inline CTA form to create a new season (NEVER crashes or renders blank)
 * Hard Rule S4-DS-TOUCH-15:
 *  - Buttons have min-h-[48px] touch targets and active:scale-95 micro-interaction
 * Hard Rule S4-DS-ICON-14:
 *  - Zero emoji, 100% Lucide SVG components
 */
export function SeasonPanel() {
  const { toast } = useToast();
  const { data: season, isLoading, isError, error } = useActiveSeason();
  const createSeasonMutation = useCreateSeason();

  // Create season form state
  const [newSeasonName, setNewSeasonName] = useState('');
  const [targetEc, setTargetEc] = useState('1.6');
  const [targetPh, setTargetPh] = useState('6.0');
  const [isEndModalOpen, setIsEndModalOpen] = useState(false);

  const clearCreateError = () => {
    if (createSeasonMutation.isError) createSeasonMutation.reset();
  };

  const handleCreateSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    const trimmedName = newSeasonName.trim();
    if (!trimmedName) return;

    try {
      await createSeasonMutation.mutateAsync({
        name: trimmedName,
        target_ec: targetEc ? parseFloat(targetEc) : undefined,
        target_ph: targetPh ? parseFloat(targetPh) : undefined,
      });
      toast.success(SUCCESS_MESSAGES.CREATE_SEASON(trimmedName));
      setNewSeasonName('');
    } catch {
      // Error handled by AlertBanner
    }
  };

  if (isLoading) {
    return (
      <div className="glass-card p-5 animate-pulse">
        <div className="h-6 w-48 bg-surface-hover rounded-lg mb-3" />
        <div className="h-4 w-72 bg-surface-hover/60 rounded-lg mb-2" />
        <div className="h-4 w-36 bg-surface-hover/40 rounded-lg" />
      </div>
    );
  }

  if (isError) {
    return (
      <AlertBanner
        error={error}
        fallbackContext="Không thể tải thông tin vụ mùa hiện tại"
      />
    );
  }


  // Active Season View
  if (season) {
    const rawStartDate = season.started_at || season.start_date || '';
    const startDate = new Date(rawStartDate);
    const isValidDate = rawStartDate !== '' && !Number.isNaN(startDate.getTime());
    const formattedDate = isValidDate
      ? startDate.toLocaleDateString('vi-VN', {
          year: 'numeric',
          month: '2-digit',
          day: '2-digit',
        })
      : (rawStartDate || '--');

    const daysElapsed = isValidDate
      ? Math.max(1, Math.floor((Date.now() - startDate.getTime()) / (1000 * 60 * 60 * 24)))
      : 1;

    return (
      <>
        <section aria-labelledby="season-heading" className="glass-card p-5">
          <div className="flex flex-col md:flex-row md:items-center justify-between gap-4">
            <div className="space-y-1.5">
              <div className="flex items-center gap-2.5">
                <h2 id="season-heading" className="text-lg sm:text-xl font-bold text-text">
                  Vụ Mùa Hiện Tại
                </h2>
                <span className="inline-flex items-center gap-1 px-2.5 py-0.5 rounded-full text-xs font-semibold bg-primary/15 text-primary border border-primary/30">
                  <CheckCircle2 size={12} aria-hidden="true" />
                  <span>Đang hoạt động</span>
                </span>
              </div>

              <p className="text-base sm:text-lg font-semibold text-primary">
                {season.name}
              </p>

              <div className="flex flex-wrap items-center gap-x-4 gap-y-1 text-xs text-text-muted">
                <span className="inline-flex items-center gap-1">
                  <Calendar size={13} aria-hidden="true" />
                  <span>Bắt đầu: <strong className="text-text font-medium">{formattedDate}</strong></span>
                </span>
                <span className="inline-flex items-center gap-1">
                  <Clock size={13} aria-hidden="true" />
                  <span>Thời gian: <strong className="text-text font-mono tabular-nums font-bold">{daysElapsed} ngày</strong></span>
                </span>
                {season.target_ec != null && (
                  <span>Mục tiêu EC: <strong className="text-text font-mono tabular-nums">{season.target_ec} mS/cm</strong></span>
                )}
                {season.target_ph != null && (
                  <span>Mục tiêu pH: <strong className="text-text font-mono tabular-nums">{season.target_ph}</strong></span>
                )}
              </div>
            </div>

            <div className="flex items-center">
              <button
                type="button"
                onClick={() => setIsEndModalOpen(true)}
                className="btn-primary w-full md:w-auto inline-flex items-center justify-center gap-2 px-5 py-2.5 rounded-xl bg-danger/15 hover:bg-danger/25 active:scale-95 text-danger border border-danger/40 text-sm font-bold cursor-pointer transition-all duration-150 min-h-[48px]"
                aria-label={`Kết thúc vụ mùa ${season.name}`}
              >
                <AlertTriangle size={16} aria-hidden="true" />
                <span>Kết thúc vụ mùa</span>
              </button>
            </div>
          </div>
        </section>

        <EndSeasonModal
          season={season}
          isOpen={isEndModalOpen}
          onClose={() => setIsEndModalOpen(false)}
        />
      </>
    );
  }

  // Empty State View (Hard Rule S4-SEASON-10: CTA Inline Form)
  return (
    <section aria-labelledby="season-empty-heading" className="glass-card p-5 sm:p-6 border-primary/30 bg-surface/80">
      <div className="flex flex-col lg:flex-row lg:items-center justify-between gap-6">
        <div className="max-w-xl">
          <div className="inline-flex items-center gap-1.5 px-2.5 py-1 rounded-full text-xs font-semibold bg-accent-amber/15 text-accent-amber border border-accent-amber/30 mb-2">
            <Sparkles size={13} aria-hidden="true" />
            <span>Chưa có mùa vụ hoạt động</span>
          </div>
          <h2 id="season-empty-heading" className="text-lg sm:text-xl font-bold text-text">
            Khởi Tạo Vụ Mùa Khí Canh Mới
          </h2>
          <p className="text-xs sm:text-sm text-text-muted mt-1 leading-relaxed">
            Hệ thống đang ở trạng thái chờ. Hãy khởi tạo một vụ mùa để kích hoạt điều phối chu kỳ phun sương,
            phân bổ trạm phun khí canh và bắt đầu ghi nhận dữ liệu viễn thám.
          </p>
        </div>

        <form onSubmit={handleCreateSubmit} className="w-full lg:w-auto flex flex-col sm:flex-row items-stretch sm:items-end gap-3">
          <div className="flex-1 sm:w-64">
            <label htmlFor="create-season-name" className="block text-xs font-semibold uppercase tracking-wider text-text-muted mb-1.5">
              Tên vụ mùa mới <span className="text-danger">*</span>
            </label>
            <input
              id="create-season-name"
              type="text"
              required
              value={newSeasonName}
              onChange={(e) => {
                clearCreateError();
                setNewSeasonName(e.target.value);
              }}
              placeholder="VD: Vụ Xà Lách Thu 2026"
              className="w-full px-3.5 py-2.5 rounded-xl bg-background/80 border border-border/50 text-text text-sm placeholder:text-text-subtle focus:outline-none focus:border-primary transition-colors min-h-[44px]"
            />
          </div>

          <div className="w-28">
            <label htmlFor="create-season-ec" className="block text-xs font-semibold uppercase tracking-wider text-text-muted mb-1.5">
              Mục tiêu EC
            </label>
            <input
              id="create-season-ec"
              type="number"
              step="0.1"
              min="0"
              max="5"
              value={targetEc}
              onChange={(e) => {
                clearCreateError();
                setTargetEc(e.target.value);
              }}
              className="w-full px-3 py-2.5 rounded-xl bg-background/80 border border-border/50 text-text text-sm font-mono tabular-nums focus:outline-none focus:border-primary transition-colors min-h-[44px]"
            />
          </div>

          <div className="w-28">
            <label htmlFor="create-season-ph" className="block text-xs font-semibold uppercase tracking-wider text-text-muted mb-1.5">
              Mục tiêu pH
            </label>
            <input
              id="create-season-ph"
              type="number"
              step="0.1"
              min="0"
              max="14"
              value={targetPh}
              onChange={(e) => {
                clearCreateError();
                setTargetPh(e.target.value);
              }}
              className="w-full px-3 py-2.5 rounded-xl bg-background/80 border border-border/50 text-text text-sm font-mono tabular-nums focus:outline-none focus:border-primary transition-colors min-h-[44px]"
            />
          </div>

          <button
            type="submit"
            disabled={createSeasonMutation.isPending || !newSeasonName.trim()}
            className="btn-primary inline-flex items-center justify-center gap-2 px-6 py-2.5 rounded-xl bg-primary hover:bg-primary/90 disabled:opacity-50 active:scale-95 text-background font-bold text-sm shadow-lg shadow-primary/30 cursor-pointer transition-all duration-150 min-h-[48px]"
          >
            {createSeasonMutation.isPending ? (
              <>
                <Loader2 size={16} className="animate-spin" aria-hidden="true" />
                <span>Đang tạo...</span>
              </>
            ) : (
              <>
                <Plus size={18} aria-hidden="true" />
                <span>Tạo mùa vụ mới</span>
              </>
            )}
          </button>
        </form>
      </div>

      {createSeasonMutation.isError && (
        <AlertBanner
          error={createSeasonMutation.error}
          fallbackContext="Không thể khởi tạo vụ mùa mới"
          className="mt-3"
        />
      )}
    </section>
  );
}

