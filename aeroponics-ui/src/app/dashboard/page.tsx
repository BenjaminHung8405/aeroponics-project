import type { Metadata } from 'next';
import { SeasonPanel } from '../../components/dashboard/SeasonPanel';
import { GroupGrid } from '../../components/dashboard/GroupGrid';
import { NodeGrid } from '../../components/dashboard/NodeGrid';
import { TreatmentPanel } from '../../components/dashboard/TreatmentPanel';
import { MeasurementPanel } from '../../components/dashboard/MeasurementPanel';

export const metadata: Metadata = {
  title: 'Bảng Điều Khiển — Aeroponics Smart Farm',
  description: 'Giám sát viễn thám và điều khiển khí canh đa trạm thời gian thực',
};

/**
 * Dashboard Page — Main telemetry & control interface.
 *
 * Implements Track S4-D UI Components:
 *  - SeasonPanel     (S4-D1): Active season info + End Season modal + Empty CTA form
 *  - GroupGrid       (S4-D2): 4 Timer Groups with Sun/Moon phase and countdown ticker
 *  - NodeGrid        (S4-D3): 4 Actuator Nodes with staleness dot, flow rate, outcome badge, active glow
 *  - TreatmentPanel  (S4-D4): Treatment recipes + timing version parameters + publish action
 *  - MeasurementPanel(S4-D5): On-demand Tuya PH-W218 measurement + 60s cooldown + history table
 *
 * Hard Rules satisfied:
 *  - S4-SEASON-10: Handles season === null gracefully with CTA form
 *  - S4-NULL-06: Null-safe rendering everywhere
 *  - S4-DS-MOBILE-17: Mobile-first responsive layout (375px to 1440px+)
 */
export default function DashboardPage() {
  return (
    <div className="space-y-6">
      {/* 1. SeasonPanel — Active season or Empty State CTA (Track S4-D1) */}
      <SeasonPanel />

      {/* 2. GroupGrid — 4 Timer Groups (Track S4-D2) */}
      <GroupGrid />

      {/* 3. NodeGrid — 4 Actuator Nodes (Track S4-D3) */}
      <NodeGrid />

      {/* 4. Bottom Grid: Treatment Recipes & Tuya Water Quality Monitoring (Track S4-D4 & S4-D5) */}
      <div className="grid grid-cols-1 lg:grid-cols-2 gap-6">
        <TreatmentPanel />
        <MeasurementPanel />
      </div>
    </div>
  );
}
