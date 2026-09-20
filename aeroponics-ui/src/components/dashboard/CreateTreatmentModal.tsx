'use client';

import React, { useState } from 'react';
import { Modal } from '../common/Modal';
import { AlertBanner } from '../common/AlertBanner';
import { useToast } from '../common/Toast';
import { SUCCESS_MESSAGES } from '../../lib/messages';
import {
  useCreateTreatment,
  useCreateTreatmentVersion,
} from '../../hooks/queries/useTreatments';
import { Plus, Loader2 } from 'lucide-react';
import type { Treatment } from '../../lib/types';


interface CreateTreatmentModalProps {
  isOpen: boolean;
  onClose: () => void;
  existingTreatment?: Treatment | null; // If provided, creates a new version for this treatment
}

/**
 * CreateTreatmentModal Component
 * Supports creating a new treatment recipe OR appending a new version to an existing recipe.
 */
export function CreateTreatmentModal({
  isOpen,
  onClose,
  existingTreatment,
}: CreateTreatmentModalProps) {
  const { toast } = useToast();
  const createTreatmentMutation = useCreateTreatment();
  const createVersionMutation = useCreateTreatmentVersion();

  const [name, setName] = useState('');
  const [sprayDay, setSprayDay] = useState('15');
  const [cooldownDay, setCooldownDay] = useState('180');
  const [sprayNight, setSprayNight] = useState('10');
  const [cooldownNight, setCooldownNight] = useState('300');

  const isNewVersionMode = Boolean(existingTreatment);

  const clearErrors = () => {
    if (createTreatmentMutation.isError) createTreatmentMutation.reset();
    if (createVersionMutation.isError) createVersionMutation.reset();
  };

  const handleSubmit = async (e: React.FormEvent) => {
    e.preventDefault();

    const timing = {
      spray_day_s: Math.max(1, parseInt(sprayDay, 10) || 15),
      cooldown_day_s: Math.max(1, parseInt(cooldownDay, 10) || 180),
      spray_night_s: Math.max(1, parseInt(sprayNight, 10) || 10),
      cooldown_night_s: Math.max(1, parseInt(cooldownNight, 10) || 300),
    };

    try {
      if (isNewVersionMode && existingTreatment) {
        const result = await createVersionMutation.mutateAsync({
          treatmentId: existingTreatment.id,
          dto: timing,
        });
        const verNum = (result as any)?.version_num ?? ((existingTreatment.versions?.length || 0) + 1);
        toast.success(SUCCESS_MESSAGES.CREATE_VERSION(existingTreatment.name, verNum));
      } else {
        const trimmedName = name.trim();
        await createTreatmentMutation.mutateAsync({
          name: trimmedName,
          spray_day_s: timing.spray_day_s,
          cooldown_day_s: timing.cooldown_day_s,
          spray_night_s: timing.spray_night_s,
          cooldown_night_s: timing.cooldown_night_s,
        });
        toast.success(SUCCESS_MESSAGES.CREATE_TREATMENT(trimmedName));
      }

      setName('');
      onClose();
    } catch {
      // Handled by AlertBanner
    }
  };

  const isPending = createTreatmentMutation.isPending || createVersionMutation.isPending;

  return (
    <Modal
      isOpen={isOpen}
      onClose={onClose}
      title={
        isNewVersionMode
          ? `Tạo Phiên Bản Mới — ${existingTreatment?.name}`
          : 'Tạo Công Thức Khí Canh Mới'
      }
      titleId="create-treatment-modal-title"
      maxWidth="md"
    >
      <form onSubmit={handleSubmit} className="space-y-4">
        {!isNewVersionMode && (
          <>
            <div>
              <label
                htmlFor="treatment-name"
                className="block text-xs font-semibold uppercase tracking-wider text-text-muted mb-1.5"
              >
                Tên công thức <span className="text-danger">*</span>
              </label>
              <input
                id="treatment-name"
                type="text"
                required
                value={name}
                onChange={(e) => {
                  clearErrors();
                  setName(e.target.value);
                }}
                placeholder="VD: Rau Muống Giai Đoạn Nuôi Cây"
                className="w-full px-3.5 py-2.5 rounded-xl bg-background/80 border border-border/50 text-text text-sm focus:outline-none focus:border-primary transition-colors min-h-[44px]"
              />
            </div>

          </>
        )}

        {/* Timing parameters for Day and Night */}
        <div className="p-3.5 rounded-xl bg-surface/50 border border-border/30 space-y-3">
          <span className="block text-xs font-bold uppercase tracking-wider text-primary">
            Thông Số Chu Kỳ Phun Sương / Nghỉ (Giây)
          </span>

          <div className="grid grid-cols-2 gap-3">
            <div>
              <label
                htmlFor="spray-day"
                className="block text-[11px] font-semibold text-accent-amber mb-1"
              >
                Pha Ngày — Phun (s)
              </label>
              <input
                id="spray-day"
                type="number"
                min="1"
                required
                value={sprayDay}
                onChange={(e) => {
                  clearErrors();
                  setSprayDay(e.target.value);
                }}
                className="w-full px-3 py-2 rounded-lg bg-background border border-border/40 text-text text-sm font-mono tabular-nums focus:border-primary focus:outline-none min-h-[44px]"
              />
            </div>

            <div>
              <label
                htmlFor="cooldown-day"
                className="block text-[11px] font-semibold text-accent-amber mb-1"
              >
                Pha Ngày — Nghỉ (s)
              </label>
              <input
                id="cooldown-day"
                type="number"
                min="1"
                required
                value={cooldownDay}
                onChange={(e) => {
                  clearErrors();
                  setCooldownDay(e.target.value);
                }}
                className="w-full px-3 py-2 rounded-lg bg-background border border-border/40 text-text text-sm font-mono tabular-nums focus:border-primary focus:outline-none min-h-[44px]"
              />
            </div>

            <div>
              <label
                htmlFor="spray-night"
                className="block text-[11px] font-semibold text-accent-indigo mb-1"
              >
                Pha Đêm — Phun (s)
              </label>
              <input
                id="spray-night"
                type="number"
                min="1"
                required
                value={sprayNight}
                onChange={(e) => {
                  clearErrors();
                  setSprayNight(e.target.value);
                }}
                className="w-full px-3 py-2 rounded-lg bg-background border border-border/40 text-text text-sm font-mono tabular-nums focus:border-primary focus:outline-none min-h-[44px]"
              />
            </div>

            <div>
              <label
                htmlFor="cooldown-night"
                className="block text-[11px] font-semibold text-accent-indigo mb-1"
              >
                Pha Đêm — Nghỉ (s)
              </label>
              <input
                id="cooldown-night"
                type="number"
                min="1"
                required
                value={cooldownNight}
                onChange={(e) => {
                  clearErrors();
                  setCooldownNight(e.target.value);
                }}
                className="w-full px-3 py-2 rounded-lg bg-background border border-border/40 text-text text-sm font-mono tabular-nums focus:border-primary focus:outline-none min-h-[44px]"
              />
            </div>
          </div>
        </div>

        {(createTreatmentMutation.isError || createVersionMutation.isError) && (
          <AlertBanner
            error={createTreatmentMutation.error || createVersionMutation.error}
            fallbackContext="Không thể lưu công thức khí canh"
          />
        )}


        <div className="flex items-center justify-end gap-3 pt-2">
          <button
            type="button"
            onClick={onClose}
            disabled={isPending}
            className="btn-secondary px-4 py-2 rounded-xl bg-surface/60 hover:bg-surface text-text-muted text-sm font-semibold border border-border/40 min-h-[44px]"
          >
            Hủy
          </button>
          <button
            type="submit"
            disabled={isPending || (!isNewVersionMode && !name.trim())}
            className="btn-primary inline-flex items-center justify-center gap-2 px-5 py-2.5 rounded-xl bg-primary hover:bg-primary/90 disabled:opacity-50 text-background font-bold text-sm shadow-lg shadow-primary/30 min-h-[48px]"
          >
            {isPending ? (
              <>
                <Loader2 size={16} className="animate-spin" aria-hidden="true" />
                <span>Đang lưu...</span>
              </>
            ) : (
              <>
                <Plus size={16} aria-hidden="true" />
                <span>{isNewVersionMode ? 'Tạo phiên bản' : 'Tạo công thức'}</span>
              </>
            )}
          </button>
        </div>
      </form>
    </Modal>
  );
}
