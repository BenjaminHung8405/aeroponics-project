'use client';

import React, { useState } from 'react';
import { Modal } from '../common/Modal';
import { useToast } from '../common/Toast';
import { useScanRfNodes } from '../../hooks/queries/useNodes';
import type { DiscoveredRfNode } from '../../lib/types';
import {
  Radio,
  RotateCcw,
  Loader2,
  AlertTriangle,
  Zap,
  Server,
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

  const [nodes, setNodes] = useState<DiscoveredRfNode[]>([]);
  const [hasScanned, setHasScanned] = useState(false);
  const [lastScanDuration, setLastScanDuration] = useState<number | null>(null);
  const [scanResult, setScanResult] = useState<Awaited<ReturnType<typeof scanMutation.mutateAsync>> | null>(null);

  const handleScan = async () => {
    try {
      const result = await scanMutation.mutateAsync();
      setNodes(result.nodes || []);
      setScanResult(result);
      setLastScanDuration(result.duration_ms);
      setHasScanned(true);

      if (result.status === 'TIMEOUT') {
        toast.error('Gateway không trả kết quả quét trong thời gian cho phép.');
      } else if (result.status === 'FAILED') {
        toast.error(`Quét RF thất bại: ${result.error ?? 'RF_ERROR'}.`);
      } else if (result.nodes?.some((node) => node.online === true)) {
        toast.success(`Đã hoàn tất quét ${result.nodes.length}/4 physical node trong phạm vi vô tuyến.`);
      } else {
        toast.info('Không phát hiện phản hồi từ node nào. Vui lòng kiểm tra nguồn ATmega8/RF.');
      }
    } catch {
      toast.error('Lỗi khi gửi lệnh quét sóng RF tới Gateway ESP32.');
    }
  };

  return (
    <Modal
      isOpen={isOpen}
      onClose={onClose}
      title="Dò Quét Node RF (physical ID 4–7)"
      maxWidth="lg"
    >
      <div className="space-y-6">
        {/* Explanation Card */}
        <div className="p-4 rounded-xl bg-surface/80 border border-border/40 text-xs text-text-muted space-y-1.5 leading-relaxed">
          <div className="flex items-center gap-2 text-primary font-semibold text-sm">
            <Radio size={16} className="text-primary animate-pulse" />
            <span>Giao thức AGU Legacy SCI (Half-duplex, 38400 8N2)</span>
          </div>
          <p>
            Gateway ESP32 sẽ kiểm tra tuần tự các Node physical <strong>4, 5, 6, 7</strong>, đo RTT và
            đồng bộ trạng thái phát hiện về registry. Đây là compatibility mode không có HMAC; luồng scan không đổi ID và không điều khiển bơm.
          </p>
        </div>

        {/* Scan Trigger / Radar Status */}
        <div className="flex items-center justify-between border-b border-border/20 pb-4">
          <div>
            <h3 className="text-sm font-semibold text-text">
              {hasScanned
                ? `Kết quả quét: ${nodes.filter((node) => node.online).length}/4 node phản hồi`
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
            disabled={scanMutation.isPending}
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
              <p className="text-xs text-text-muted">Đang kiểm tra physical ID 4..7 qua AGU Legacy SCI (38400 8N2)</p>
            </div>
          </div>
        )}

        {/* Discovered Nodes List */}
        {!scanMutation.isPending && hasScanned && (
          <div className="space-y-3 max-h-[380px] overflow-y-auto pr-1">
            {scanResult?.status === 'TIMEOUT' ? (
              <div className="py-8 text-center space-y-2 border border-dashed border-warning/50 rounded-xl">
                <AlertTriangle size={28} className="mx-auto text-warning/80" />
                <p className="text-sm font-medium text-text">Gateway không trả kết quả quét</p>
                <p className="text-xs text-text-muted max-w-sm mx-auto">Trạng thái các node chưa xác định. Hãy thử quét lại.</p>
              </div>
            ) : nodes.length === 0 ? (
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

                // Signal evaluation based on RTT
                const isGreat = (node.rtt_ms ?? 9999) <= 35;
                const isGood = (node.rtt_ms ?? 9999) > 35 && (node.rtt_ms ?? 9999) <= 70;

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
                              Đã đăng ký physical ID {node.current_slot ?? node.node_id}
                            </span>
                          ) : (
                            <span className="px-2 py-0.5 rounded-full text-[10px] font-semibold bg-amber-500/10 text-amber-400 border border-amber-500/20">
                              Chưa đăng ký
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
                            RTT: {node.rtt_ms ?? '—'} ms (
                             {node.online === null ? 'Chưa xác định' : isGreat ? 'Rất tốt' : isGood ? 'Ổn định' : 'Chấp nhận được'})
                          </span>
                          <span>•</span>
                          <span className="font-mono text-[11px] text-text-muted/80">
                            {node.protocol} {node.failure_reason ? `• ${node.failure_reason}` : ''}
                          </span>
                        </div>
                      </div>
                    </div>

                    <span className={`px-3 py-1.5 rounded-lg text-xs font-semibold ${
                      node.online === null ? 'bg-slate-500/10 text-slate-300 border border-slate-500/20' : node.online === false ? 'bg-amber-500/10 text-amber-400 border border-amber-500/20' :
                      'bg-emerald-500/10 text-emerald-400 border border-emerald-500/20'
                    }`}>
                      {node.online === null ? 'UNKNOWN' : node.online === false ? 'STALE / OFFLINE' : 'RF ONLINE'}
                    </span>
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
