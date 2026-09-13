'use client';

import React, { useState } from 'react';
import {
  useTreatments,
  usePublishTreatmentVersion,
} from '../../hooks/queries/useTreatments';
import { CreateTreatmentModal } from './CreateTreatmentModal';
import {
  Sliders,
  Plus,
  CheckCircle2,
  Clock,
  Archive,
  Loader2,
  Sparkles,
} from 'lucide-react';
import type { Treatment } from '../../lib/types';

/**
 * TreatmentPanel Component
 * Hard Rule S4-D4:
 *  - Lists treatments and their versions with spray/cooldown parameters (day/night)
 *  - Empty state: Displays "Chưa có công thức nào" without crashing
 *  - Allows publishing DRAFT versions into immutable PUBLISHED status
 *  - Touch ergonomics: Buttons have min-h-[44px] / min-h-[48px], active:scale-95
 *  - Hard Rule S4-DS-ICON-14: Zero emoji, 100% Lucide SVG
 */
export function TreatmentPanel() {
  const { data: treatmentResponse, isLoading, isError, error } = useTreatments();
  const publishMutation = usePublishTreatmentVersion();

  const [isCreateModalOpen, setIsCreateModalOpen] = useState(false);
  const [selectedTreatmentForVersion, setSelectedTreatmentForVersion] = useState<Treatment | null>(null);

  const handleOpenNewVersionModal = (treatment: Treatment) => {
    setSelectedTreatmentForVersion(treatment);
    setIsCreateModalOpen(true);
  };

  const handleOpenNewTreatmentModal = () => {
    setSelectedTreatmentForVersion(null);
    setIsCreateModalOpen(true);
  };

  const handlePublish = async (treatmentId: number, versionId: number) => {
    try {
      await publishMutation.mutateAsync({ treatmentId, versionId });
    } catch {
      // Handled by mutation error state
    }
  };

  const treatments = treatmentResponse?.items ?? [];

  return (
    <>
      <section aria-labelledby="treatment-heading" className="glass-card p-5 space-y-4">
        <div className="flex flex-col sm:flex-row sm:items-center justify-between gap-3 border-b border-border/20 pb-3">
          <div className="flex items-center gap-2.5">
            <div className="p-2 rounded-xl bg-primary/15 border border-primary/30 text-primary">
              <Sliders size={20} aria-hidden="true" />
            </div>
            <div>
              <h2 id="treatment-heading" className="text-base sm:text-lg font-bold text-text">
                Công Thức Khí Canh (Treatment Recipes)
              </h2>
              <p className="text-xs text-text-muted">
                Quản lý thông số phun sương và chu kỳ nghỉ theo pha ánh sáng
              </p>
            </div>
          </div>

          <button
            type="button"
            onClick={handleOpenNewTreatmentModal}
            className="btn-primary inline-flex items-center justify-center gap-1.5 px-4 py-2 rounded-xl bg-primary hover:bg-primary/90 active:scale-95 text-background font-bold text-xs shadow-md shadow-primary/20 min-h-[44px]"
            aria-label="Thêm công thức khí canh mới"
          >
            <Plus size={16} aria-hidden="true" />
            <span>Thêm công thức</span>
          </button>
        </div>

        {/* Loading State */}
        {isLoading && (
          <div className="p-8 flex items-center justify-center gap-2 text-text-muted text-xs">
            <Loader2 size={18} className="animate-spin text-primary" aria-hidden="true" />
            <span>Đang tải danh mục công thức...</span>
          </div>
        )}

        {/* Error State */}
        {isError && (
          <div className="p-3 rounded-lg bg-danger/15 border border-danger/30 text-danger text-xs">
            Lỗi tải công thức: {error?.message || 'Vui lòng kiểm tra lại kết nối'}
          </div>
        )}

        {/* Empty State */}
        {!isLoading && !isError && treatments.length === 0 && (
          <div className="p-8 text-center space-y-3 bg-surface/30 rounded-xl border border-dashed border-border/40">
            <div className="w-12 h-12 mx-auto rounded-full bg-surface-hover flex items-center justify-center text-text-subtle">
              <Sparkles size={24} aria-hidden="true" />
            </div>
            <div>
              <p className="text-sm font-semibold text-text">Chưa có công thức nào</p>
              <p className="text-xs text-text-muted mt-1 max-w-sm mx-auto">
                Hệ thống chưa ghi nhận công thức phun sương nào. Hãy tạo công thức đầu tiên để áp dụng cho các trạm rễ.
              </p>
            </div>
            <button
              type="button"
              onClick={handleOpenNewTreatmentModal}
              className="btn-secondary inline-flex items-center gap-1.5 px-4 py-2 rounded-xl bg-primary/20 hover:bg-primary/30 text-primary border border-primary/40 text-xs font-semibold"
            >
              <Plus size={14} aria-hidden="true" />
              <span>Tạo công thức ngay</span>
            </button>
          </div>
        )}

        {/* Treatment List */}
        {!isLoading && treatments.length > 0 && (
          <div className="space-y-3">
            {treatments.map((treatment) => (
              <div
                key={treatment.id}
                className="p-4 rounded-xl bg-background/50 border border-border/30 hover:border-border/60 transition-colors space-y-3"
              >
                <div className="flex flex-col sm:flex-row sm:items-center justify-between gap-2">
                  <div>
                    <h3 className="text-sm sm:text-base font-bold text-text">
                      {treatment.name}
                    </h3>
                    {treatment.description && (
                      <p className="text-xs text-text-muted mt-0.5">{treatment.description}</p>
                    )}
                  </div>

                  <button
                    type="button"
                    onClick={() => handleOpenNewVersionModal(treatment)}
                    className="btn-secondary self-start sm:self-auto inline-flex items-center gap-1 px-3 py-1.5 rounded-lg bg-surface/80 hover:bg-surface border border-border/40 text-xs font-medium text-text-muted hover:text-text min-h-[44px]"
                  >
                    <Plus size={13} aria-hidden="true" />
                    <span>Thêm phiên bản</span>
                  </button>
                </div>

                {/* Versions List */}
                <div className="space-y-2">
                  {treatment.versions && treatment.versions.length > 0 ? (
                    treatment.versions.map((ver) => (
                      <div
                        key={ver.id}
                        className="flex flex-col md:flex-row md:items-center justify-between gap-2 p-2.5 rounded-lg bg-surface/40 border border-border/20 text-xs"
                      >
                        <div className="flex flex-wrap items-center gap-2">
                          <span className="font-bold text-text">v{ver.version_num}</span>

                          {/* Version Status Badge */}
                          {ver.status === 'PUBLISHED' ? (
                            <span className="inline-flex items-center gap-1 px-2 py-0.5 rounded text-[11px] font-semibold bg-primary/15 text-primary border border-primary/30">
                              <CheckCircle2 size={11} aria-hidden="true" />
                              <span>PUBLISHED</span>
                            </span>
                          ) : ver.status === 'DRAFT' ? (
                            <span className="inline-flex items-center gap-1 px-2 py-0.5 rounded text-[11px] font-semibold bg-accent-amber/15 text-accent-amber border border-accent-amber/30">
                              <Clock size={11} aria-hidden="true" />
                              <span>DRAFT</span>
                            </span>
                          ) : (
                            <span className="inline-flex items-center gap-1 px-2 py-0.5 rounded text-[11px] font-semibold bg-surface text-text-subtle border border-border/30">
                              <Archive size={11} aria-hidden="true" />
                              <span>ARCHIVED</span>
                            </span>
                          )}

                          {/* Timing parameters */}
                          <div className="flex items-center gap-3 font-mono tabular-nums text-text-muted">
                            <span>
                              Ngày: <strong className="text-accent-amber">{ver.spray_day_s}s</strong> phun /{' '}
                              <strong className="text-text">{ver.cooldown_day_s}s</strong> nghỉ
                            </span>
                            <span>|</span>
                            <span>
                              Đêm: <strong className="text-accent-indigo">{ver.spray_night_s}s</strong> phun /{' '}
                              <strong className="text-text">{ver.cooldown_night_s}s</strong> nghỉ
                            </span>
                          </div>
                        </div>

                        {/* Publish button for DRAFT versions */}
                        {ver.status === 'DRAFT' && (
                          <button
                            type="button"
                            onClick={() => handlePublish(treatment.id, ver.id)}
                            disabled={publishMutation.isPending}
                            className="btn-secondary self-start md:self-auto inline-flex items-center gap-1.5 px-3 py-1 rounded-md bg-primary/20 hover:bg-primary/30 text-primary border border-primary/40 text-xs font-semibold cursor-pointer min-h-[44px]"
                            title="Phát hành phiên bản này để có thể gán vào Timer Group"
                          >
                            <CheckCircle2 size={13} aria-hidden="true" />
                            <span>Phát hành (Publish)</span>
                          </button>
                        )}
                      </div>
                    ))
                  ) : (
                    <p className="text-xs text-text-subtle italic">Chưa có phiên bản nào.</p>
                  )}
                </div>
              </div>
            ))}
          </div>
        )}
      </section>

      <CreateTreatmentModal
        isOpen={isCreateModalOpen}
        onClose={() => setIsCreateModalOpen(false)}
        existingTreatment={selectedTreatmentForVersion}
      />
    </>
  );
}
