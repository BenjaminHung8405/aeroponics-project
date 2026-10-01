'use client';

import React, { useState } from 'react';
import {
  useTreatments,
  usePublishTreatmentVersion,
} from '../../hooks/queries/useTreatments';
import { useAssignGroup } from '../../hooks/queries/useGroups';
import { useAllGroups } from '../../store/useGroupStore';
import { Modal } from '../common/Modal';
import { CreateTreatmentModal } from './CreateTreatmentModal';
import { AlertBanner } from '../common/AlertBanner';
import { useToast } from '../common/Toast';
import { SUCCESS_MESSAGES } from '../../lib/messages';
import {
  Sliders,
  Plus,
  CheckCircle2,
  Clock,
  Archive,
  Loader2,
  Sparkles,
  Leaf,
} from 'lucide-react';
import type { Treatment, TreatmentVersion } from '../../lib/types';
import { useSelectedDevice } from '../../lib/selected-device-context';

/**
 * TreatmentPanel Component
 * Hard Rule S4-D4:
 *  - Lists treatments and their versions with spray/cooldown parameters (day/night)
 *  - Empty state: Displays "Chưa có công thức nào" without crashing
 *  - Allows publishing DRAFT versions into immutable PUBLISHED status
 *  - Touch ergonomics: Buttons have min-h-[44px] / min-h-[48px], active:scale-95
 *  - Hard Rule S4-DS-ICON-14: Zero emoji, 100% Lucide SVG
 */
interface QuickAssignContentProps {
  versionId: number;
  treatmentName: string;
  versionNum: number;
  onClose: () => void;
}

function QuickAssignContent({
  versionId,
  treatmentName,
  versionNum,
  onClose,
}: QuickAssignContentProps) {
  const { toast } = useToast();
  const assignMutation = useAssignGroup();
  const { selectedDeviceId } = useSelectedDevice();
  const allGroups = useAllGroups();
  const [selectedGroupId, setSelectedGroupId] = useState<number>(1);

  // The backend treats node_ids as the full replacement set for the group,
  // so we always send the CURRENT membership. Sending [] would be rejected
  // by AssignGroupDto (@ArrayMinSize(1)), and sending a subset would evict
  // other nodes from the group.
  const selectedGroup = allGroups.find((g) => g.groupId === selectedGroupId) ?? allGroups[0];
  const hasMembers = Boolean(selectedGroup && selectedGroup.nodeIds.length > 0);

  const handleSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    if (!hasMembers || !selectedGroup || !selectedDeviceId) return;
    try {
      await assignMutation.mutateAsync({
        groupId: selectedGroupId,
        deviceId: selectedDeviceId,
        dto: {
          treatment_version_id: versionId,
          node_ids: [...selectedGroup.nodeIds],
        },
      });
      toast.success(
        SUCCESS_MESSAGES.QUICK_ASSIGN_RECIPE_TO_GROUP(
          treatmentName,
          versionNum,
          selectedGroupId,
        ),
      );
      onClose();
    } catch {
      // Handled by AlertBanner
    }
  };

  return (
    <form onSubmit={handleSubmit} className="space-y-4">
      {assignMutation.isError && (
        <AlertBanner
          error={assignMutation.error}
          fallbackContext="Không thể gán công thức"
        />
      )}

      <div>
        <label htmlFor="group-select" className="block text-xs font-semibold text-text mb-1.5">
          Chọn nhóm trạm:
        </label>
        <select
          id="group-select"
          value={selectedGroupId}
          onChange={(e) => setSelectedGroupId(Number(e.target.value))}
          className="w-full bg-background border border-border/40 rounded-xl px-3 py-2.5 text-sm text-text focus:outline-none focus:border-primary transition-colors min-h-[44px]"
        >
          {allGroups.map((g) => (
            <option key={g.groupId} value={g.groupId}>
              {g.name || `Nhóm #${g.groupId}`}{g.status === 'ACTIVE' ? ' (Đang chạy)' : ''}
            </option>
          ))}
        </select>
      </div>

      {selectedGroup && !hasMembers && (
        <div className="p-3 rounded-lg bg-accent-amber/10 border border-accent-amber/30 text-accent-amber text-xs leading-relaxed">
          Nhóm #{selectedGroupId} chưa có trạm nào được gán. Hãy gán trạm cho nhóm
          này trước khi áp dụng công thức.
        </div>
      )}

      <div className="flex justify-end gap-2 pt-2">
        <button
          type="button"
          onClick={onClose}
          className="btn-secondary px-4 py-2 rounded-xl text-xs font-semibold text-text-muted hover:text-text hover:bg-surface"
        >
          Hủy
        </button>
        <button
          type="submit"
          disabled={assignMutation.isPending || !hasMembers}
          className="btn-primary inline-flex items-center justify-center gap-1.5 px-4 py-2 rounded-xl bg-primary hover:bg-primary/90 text-background font-bold text-xs shadow-md shadow-primary/20 disabled:opacity-50 disabled:cursor-not-allowed"
        >
          {assignMutation.isPending && <Loader2 size={14} className="animate-spin" aria-hidden="true" />}
          Xác nhận gán
        </button>
      </div>
    </form>
  );
}

export function TreatmentPanel() {
  const { toast } = useToast();
  const { data: treatmentResponse, isLoading, isError, error } = useTreatments();
  const publishMutation = usePublishTreatmentVersion();

  const [isCreateModalOpen, setIsCreateModalOpen] = useState(false);
  const [selectedTreatmentForVersion, setSelectedTreatmentForVersion] = useState<Treatment | null>(null);
  const [quickAssign, setQuickAssign] = useState<{
    versionId: number;
    treatmentName: string;
    versionNum: number;
  } | null>(null);

  const handleOpenNewVersionModal = (treatment: Treatment) => {
    setSelectedTreatmentForVersion(treatment);
    setIsCreateModalOpen(true);
  };

  const handleOpenNewTreatmentModal = () => {
    setSelectedTreatmentForVersion(null);
    setIsCreateModalOpen(true);
  };

  const handlePublish = async (treatmentId: number, ver: TreatmentVersion) => {
    try {
      await publishMutation.mutateAsync({ treatmentId, versionId: ver.id });
      toast.success(SUCCESS_MESSAGES.PUBLISH_VERSION(ver.version_num));
    } catch {
      // Handled by AlertBanner
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
          <AlertBanner
            error={error}
            fallbackContext="Không thể tải danh mục công thức khí canh"
          />
        )}

        {/* Publish Mutation Error */}
        {publishMutation.isError && (
          <AlertBanner
            error={publishMutation.error}
            fallbackContext="Không thể phát hành phiên bản công thức"
          />
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
                            onClick={() => handlePublish(treatment.id, ver)}
                            disabled={publishMutation.isPending}
                            className="btn-secondary self-start md:self-auto inline-flex items-center gap-1.5 px-3 py-1 rounded-md bg-primary/20 hover:bg-primary/30 text-primary border border-primary/40 text-xs font-semibold cursor-pointer min-h-[44px]"
                            title="Phát hành phiên bản này để có thể gán vào Timer Group"
                          >
                            <CheckCircle2 size={13} aria-hidden="true" />
                            <span>Phát hành (Publish)</span>
                          </button>
                        )}

                        {/* Quick Assign button for PUBLISHED versions */}
                        {ver.status === 'PUBLISHED' && (
                          <button
                            type="button"
                            onClick={() =>
                              setQuickAssign({
                                versionId: ver.id,
                                treatmentName: treatment.name,
                                versionNum: ver.version_num,
                              })
                            }
                            className="btn-secondary self-start md:self-auto inline-flex items-center gap-1.5 px-3 py-1 rounded-md bg-surface/60 hover:bg-surface border border-border/40 text-xs font-medium text-text-muted hover:text-text min-h-[44px] cursor-pointer"
                            title="Gán phiên bản này cho một nhóm trạm"
                          >
                            <Leaf size={12} aria-hidden="true" />
                            <span>Gán nhóm</span>
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

      <Modal
        isOpen={quickAssign !== null}
        onClose={() => setQuickAssign(null)}
        title="Gán Công Thức Vào Nhóm"
        titleId="quick-assign-modal-title"
        maxWidth="sm"
      >
        {quickAssign !== null && (
          <QuickAssignContent
            versionId={quickAssign.versionId}
            treatmentName={quickAssign.treatmentName}
            versionNum={quickAssign.versionNum}
            onClose={() => setQuickAssign(null)}
          />
        )}
      </Modal>
    </>
  );
}
