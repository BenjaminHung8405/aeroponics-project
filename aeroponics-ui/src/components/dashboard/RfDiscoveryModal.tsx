'use client';

import React, { useState } from 'react';
import { Modal } from '../common/Modal';
import { useToast } from '../common/Toast';
import { useScanRfNodes, useClaimNode } from '../../hooks/queries/useNodes';
import type { DiscoveredRfNode } from '../../lib/types';
import {
  Radio,
  RotateCcw,
  Loader2,
  CheckCircle2,
  AlertTriangle,
  Zap,
  Server,
  Layers,
} from 'lucide-react';

interface RfDiscoveryModalProps {
  isOpen: boolean;
  onClose: () => void;
}

/**
 * RfDiscoveryModal Component
 * Allows agricultural operators to perform an Active Probe Sweep on the RF bus
 * to discover AGU legacy nodes 4..7 and evaluate signal RTT.
 */
export function RfDiscoveryModal({ isOpen, onClose }: RfDiscoveryModalProps) {
  const { toast } = useToast();
  const scanMutation = useScanRfNodes();
  const claimMutation = useClaimNode();

  const [nodes, setNodes] = useState<DiscoveredRfNode[]>([]);
  const [hasScanned, setHasScanned] = useState(false);
  const [selectedSlots, setSelectedSlots] = useState<Record<number, number>>({});
  const [claimingNodeId, setClaimingNodeId] = useState<number | null>(null);
  const [lastScanDuration, setLastScanDuration] = useState<number | null>(null);

  const handleScan = async () => {
    try {
      const result = await scanMutation.mutateAsync();
      setNodes(result.nodes || []);
      setLastScanDuration(result.duration_ms);
      setHasScanned(true);

      // Pre-populate target slot selections
      const initialSlots: Record<number, number> = {};
      (result.nodes || []).forEach((n, idx) => {
        initialSlots[n.node_id] = n.current_slot || Math.min(idx + 1, 4);
      });
      setSelectedSlots(initialSlots);

      if (result.nodes?.length > 0) {
        toast.success(`Đã tìm thấy ${result.nodes.length} node ATmega8 trong phạm vi vô tuyến!`);
      } else {
        toast.info('Không phát hiện phản hồi từ node nào. Vui lòng kiểm tra nguồn ATmega8/RF.');
      }
    } catch {
      toast.error('Lỗi khi gửi lệnh quét sóng RF tới Gateway ESP32.');
    }
  };

  const handleClaim = async (fromNodeId: number) => {
    const targetSlot = selectedSlots[fromNodeId] || 1;
    setClaimingNodeId(fromNodeId);

    try {
      await claimMutation.mutateAsync({
        fromNodeId,
        toNodeId: targetSlot,
      });

      toast.success(
        `Đã cấu hình và gán thành công Node #${fromNodeId} vào Trạm Phun #${targetSlot}!`,
      );

      // Update local state to reflect the new assignment
      setNodes((prev) =>
        prev.map((n) =>
          n.node_id === fromNodeId
            ? { ...n, is_assigned: true, current_slot: targetSlot }
            : n.current_slot === targetSlot
            ? { ...n, is_assigned: false, current_slot: undefined }
            : n,
        ),
      );
    } catch (err: any) {
      toast.error(err?.message || 'Không thể gán node. Quá thời gian xác nhận từ ATmega8.');
    } finally {
      setClaimingNodeId(null);
    }
  };

  return (
    <Modal
      isOpen={isOpen}
      onClose={onClose}
      title="Dò Quét & Gán Trạm Phun Khí Canh (RF Discovery)"
      maxWidth="lg"
    >
      <div className="space-y-6">
        {/* Explanation Card */}
        <div className="p-4 rounded-xl bg-surface/80 border border-border/40 text-xs text-text-muted space-y-1.5 leading-relaxed">
          <div className="flex items-center gap-2 text-primary font-semibold text-sm">
            <Radio size={16} className="text-primary animate-pulse" />
            <span>Giao thức Vô tuyến AGU Legacy SCI (Half-duplex)</span>
          </div>
          <p>
            Các bo mạch phun <strong>ATmega8</strong> đóng vai trò Slave thụ động trên tần số vô tuyến
            (38400 baud, 8N2, NetID 123). Khi bấm <strong>&ldquo;Bắt đầu Quét RF&rdquo;</strong>, Gateway ESP32 sẽ
            phát xung kích hoạt tích cực (Active Probe Sweep) để đo độ trễ phản hồi (RTT ms) và nhận dạng ID.
          </p>
        </div>

        {/* Scan Trigger / Radar Status */}
        <div className="flex items-center justify-between border-b border-border/20 pb-4">
          <div>
            <h3 className="text-sm font-semibold text-text">
              {hasScanned
                ? `Kết quả quét: ${nodes.length} thiết bị phản hồi`
                : 'Sẵn sàng kích hoạt quét sóng'}
            </h3>
            <p className="text-xs text-text-muted">
              {lastScanDuration
                ? `Thời gian hoàn tất: ${lastScanDuration} ms`
                : 'Phạm vi quét: RF client IDs 4, 5, 6, 7'}
            </p>
          </div>

          <button
            type="button"
            onClick={handleScan}
            disabled={scanMutation.isPending || claimingNodeId !== null}
            className="flex items-center gap-2 px-4 py-2.5 rounded-lg bg-primary hover:bg-primary-hover text-white text-xs font-semibold shadow-md transition-all active:scale-95 disabled:opacity-50"
          >
            {scanMutation.isPending ? (
              <>
                <Loader2 size={15} className="animate-spin" />
                <span>Đang quét sóng RF...</span>
              </>
            ) : (
              <>
                <RotateCcw size={15} />
                <span>{hasScanned ? 'Quét lại' : 'Bắt đầu Quét RF'}</span>
              </>
            )}
          </button>
        </div>

        {/* Scanning Animation State */}
        {scanMutation.isPending && (
          <div className="py-12 flex flex-col items-center justify-center space-y-4 text-center">
            <div className="relative w-24 h-24 flex items-center justify-center">
              <div className="absolute inset-0 rounded-full border-2 border-primary/20 animate-ping" />
              <div className="absolute inset-2 rounded-full border border-primary/40 animate-pulse" />
              <div className="w-14 h-14 rounded-full bg-primary/10 border border-primary flex items-center justify-center">
                <Radio size={24} className="text-primary animate-pulse" />
              </div>
            </div>
            <div>
              <p className="text-sm font-semibold text-text">Đang phát xung Active Probe...</p>
              <p className="text-xs text-text-muted">Đang truy vấn ID 1..12 qua UART RF 38400 baud</p>
            </div>
          </div>
        )}

        {/* Discovered Nodes List */}
        {!scanMutation.isPending && hasScanned && (
          <div className="space-y-3 max-h-[380px] overflow-y-auto pr-1">
            {nodes.length === 0 ? (
              <div className="py-10 text-center space-y-2 border border-dashed border-border/50 rounded-xl">
                <AlertTriangle size={28} className="mx-auto text-warning/80" />
                <p className="text-sm font-medium text-text">Không tìm thấy Node ATmega8 nào</p>
                <p className="text-xs text-text-muted max-w-sm mx-auto">
                  Hãy kiểm tra cấp nguồn 12V/5V cho bo mạch ATmega8, cáp nối RX/TX module RF và đảm bảo
                  khoảng cách vô tuyến phù hợp.
                </p>
              </div>
            ) : (
              nodes.map((node) => {
                const targetSlot = selectedSlots[node.node_id] || 1;
                const isClaimingThis = claimingNodeId === node.node_id;

                // Signal evaluation based on RTT
                const isGreat = node.rtt_ms <= 35;
                const isGood = node.rtt_ms > 35 && node.rtt_ms <= 70;

                return (
                  <div
                    key={node.node_id}
                    className="p-3.5 rounded-xl bg-surface border border-border/50 hover:border-primary/40 transition-all flex flex-col sm:flex-row sm:items-center justify-between gap-3 shadow-sm"
                  >
                    {/* Node Identification */}
                    <div className="flex items-center gap-3">
                      <div className="w-10 h-10 rounded-lg bg-surface-raised border border-border/40 flex items-center justify-center text-primary font-bold text-sm">
                        #{node.node_id}
                      </div>
                      <div>
                        <div className="flex items-center gap-2">
                          <span className="text-sm font-bold text-text">
                            Node ATmega8 (ID: {node.node_id})
                          </span>
                          {node.is_assigned ? (
                            <span className="px-2 py-0.5 rounded-full text-[10px] font-semibold bg-emerald-500/10 text-emerald-400 border border-emerald-500/20 flex items-center gap-1">
                              <CheckCircle2 size={10} />
                              Đang là Trạm {node.current_slot}
                            </span>
                          ) : (
                            <span className="px-2 py-0.5 rounded-full text-[10px] font-semibold bg-amber-500/10 text-amber-400 border border-amber-500/20">
                              Chưa gán trạm
                            </span>
                          )}
                        </div>
                        <div className="flex items-center gap-3 text-xs text-text-muted mt-1">
                          <span
                            className={`flex items-center gap-1 font-medium ${
                              isGreat ? 'text-emerald-400' : isGood ? 'text-cyan-400' : 'text-amber-400'
                            }`}
                          >
                            <Zap size={12} />
                            RTT: {node.rtt_ms} ms (
                            {isGreat ? 'Rất tốt' : isGood ? 'Ổn định' : 'Chấp nhận được'})
                          </span>
                          <span>•</span>
                          <span className="font-mono text-[11px] text-text-muted/80">
                            {node.protocol}
                          </span>
                        </div>
                      </div>
                    </div>

                    {/* Target Station Slot Selector & Claim Button */}
                    <div className="flex items-center gap-2 self-end sm:self-auto">
                      <div className="flex items-center gap-1 bg-surface-raised/80 p-1 rounded-lg border border-border/30">
                        {[1, 2, 3, 4].map((slot) => (
                          <button
                            key={slot}
                            type="button"
                            onClick={() =>
                              setSelectedSlots((prev) => ({
                                ...prev,
                                [node.node_id]: slot,
                              }))
                            }
                            className={`px-2.5 py-1 text-xs font-semibold rounded-md transition-all ${
                              targetSlot === slot
                                ? 'bg-primary text-white shadow-sm'
                                : 'text-text-muted hover:text-text'
                            }`}
                          >
                            Trạm {slot}
                          </button>
                        ))}
                      </div>

                      <button
                        type="button"
                        onClick={() => handleClaim(node.node_id)}
                        disabled={isClaimingThis || claimingNodeId !== null}
                        className="px-3.5 py-1.5 rounded-lg bg-emerald-600 hover:bg-emerald-500 text-white text-xs font-semibold shadow transition-all active:scale-95 disabled:opacity-50 flex items-center gap-1.5 min-h-[36px]"
                      >
                        {isClaimingThis ? (
                          <>
                            <Loader2 size={13} className="animate-spin" />
                            <span>Đang gán...</span>
                          </>
                        ) : (
                          <>
                            <Layers size={13} />
                            <span>Gán vào Trạm {targetSlot}</span>
                          </>
                        )}
                      </button>
                    </div>
                  </div>
                );
              })
            )}
          </div>
        )}

        {/* Initial Prompt before first scan */}
        {!hasScanned && !scanMutation.isPending && (
          <div className="py-10 text-center space-y-3">
            <div className="w-14 h-14 rounded-full bg-primary/10 border border-primary/30 flex items-center justify-center mx-auto text-primary">
              <Server size={24} />
            </div>
            <div>
              <p className="text-sm font-semibold text-text">Chưa thực hiện quét tín hiệu</p>
              <p className="text-xs text-text-muted max-w-sm mx-auto">
                Bấm nút <strong>&ldquo;Bắt đầu Quét RF&rdquo;</strong> phía trên để phát hiện các node ATmega8 trong
                phạm vi không gian xung quanh trạm.
              </p>
            </div>
          </div>
        )}
      </div>
    </Modal>
  );
}
