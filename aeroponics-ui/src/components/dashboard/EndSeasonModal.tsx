'use client';

import React, { useState } from 'react';
import { Modal } from '../common/Modal';
import { AlertBanner } from '../common/AlertBanner';
import { useToast } from '../common/Toast';
import { SUCCESS_MESSAGES } from '../../lib/messages';
import { useEndSeason } from '../../hooks/queries/useSeason';
import { AlertTriangle, Loader2 } from 'lucide-react';
import type { Season } from '../../lib/types';

interface EndSeasonModalProps {
  season: Season;
  isOpen: boolean;
  onClose: () => void;
}

/**
 * EndSeasonModal Component
 * Confirmation dialog before ending an active agricultural season.
 * Calls PUT /api/season/:id/end with optional summary notes.
 */
export function EndSeasonModal({ season, isOpen, onClose }: EndSeasonModalProps) {
  const { toast } = useToast();
  const [notes, setNotes] = useState<string>('');
  const endSeasonMutation = useEndSeason();

  const handleSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    try {
      await endSeasonMutation.mutateAsync({
        id: season.id,
        dto: {
          end_date: new Date().toISOString(),
          notes: notes.trim() || undefined,
        },
      });
      toast.success(SUCCESS_MESSAGES.END_SEASON(season.name));
      onClose();
    } catch {
      // Error handled by AlertBanner
    }
  };


  return (
    <Modal
      isOpen={isOpen}
      onClose={onClose}
      title="Xác Nhận Kết Thúc Vụ Mùa"
      titleId="end-season-modal-title"
      maxWidth="md"
    >
      <form onSubmit={handleSubmit} className="space-y-4">
        <div className="flex items-start gap-3 p-3 rounded-xl bg-danger/15 border border-danger/40 text-danger text-sm">
          <AlertTriangle size={20} className="shrink-0 mt-0.5" aria-hidden="true" />
          <div>
            <p className="font-semibold">Hành động này không thể hoàn tác!</p>
            <p className="text-xs text-text-muted mt-1">
              Vụ mùa <strong>&quot;{season.name}&quot;</strong> sẽ được đóng lại, toàn bộ lịch trình điều khiển
              và thông số phân nhóm sẽ được chốt lưu vết lịch sử.
            </p>
          </div>
        </div>

        <div>
          <label htmlFor="end-season-notes" className="block text-xs font-semibold uppercase tracking-wider text-text-muted mb-1.5">
            Ghi chú tổng kết vụ mùa (tùy chọn)
          </label>
          <textarea
            id="end-season-notes"
            rows={3}
            value={notes}
            onChange={(e) => {
              if (endSeasonMutation.isError) endSeasonMutation.reset();
              setNotes(e.target.value);
            }}
            placeholder="Ví dụ: Năng suất thu hoạch đạt 95%, chất lượng rễ khí canh phát triển đều..."
            className="w-full px-3.5 py-2.5 rounded-xl bg-background/60 border border-border/40 text-text text-sm placeholder:text-text-subtle focus:outline-none focus:border-primary transition-colors resize-none"
          />
        </div>

        {endSeasonMutation.isError && (
          <AlertBanner
            error={endSeasonMutation.error}
            fallbackContext="Không thể kết thúc vụ mùa"
          />
        )}


        <div className="flex items-center justify-end gap-3 pt-2">
          <button
            type="button"
            onClick={onClose}
            disabled={endSeasonMutation.isPending}
            className="btn-secondary px-4 py-2 rounded-xl bg-surface/60 hover:bg-surface text-text-muted text-sm font-semibold border border-border/40"
          >
            Hủy bỏ
          </button>
          <button
            type="submit"
            disabled={endSeasonMutation.isPending}
            className="btn-primary inline-flex items-center justify-center gap-2 px-5 py-2.5 rounded-xl bg-danger hover:bg-danger/90 active:scale-95 text-text text-sm font-bold shadow-lg shadow-danger/30"
          >
            {endSeasonMutation.isPending ? (
              <>
                <Loader2 size={16} className="animate-spin" aria-hidden="true" />
                <span>Đang xử lý...</span>
              </>
            ) : (
              <span>Xác nhận kết thúc</span>
            )}
          </button>
        </div>
      </form>
    </Modal>
  );
}
