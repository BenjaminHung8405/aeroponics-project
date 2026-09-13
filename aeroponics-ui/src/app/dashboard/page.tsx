import type { Metadata } from 'next';

export const metadata: Metadata = {
  title: 'Dashboard — Aeroponics Smart Farm',
};

/**
 * Dashboard Page — Main telemetry & control interface.
 *
 * Tất cả panels sẽ được implement ở Track D:
 *  - SeasonPanel  (D-1): Active season info + End Season modal + Empty CTA
 *  - GroupGrid    (D-2): 4 Timer Groups — treatment, phase, countdown
 *  - NodeGrid     (D-3): 4 Actuator Nodes — staleness, outcome, flow
 *  - TreatmentPanel (D-4): Treatment list + version management
 *  - MeasurementPanel (D-5): On-demand Tuya sensor trigger
 *
 * Hard Rules:
 *  - S4-SEASON-10: Nếu không có active season → hiển thị CTA (SeasonPanel xử lý)
 *  - S4-NULL-06: Null-safe rendering — các panel handle null gracefully
 *  - S4-DS-MOBILE-17: Mobile-first grid (1-col mobile, 4-col desktop)
 */
export default function DashboardPage() {
  return (
    <div className="space-y-6">
      {/* SeasonPanel — Track D-1 */}
      <div className="glass-card p-4">
        <p className="text-text-subtle text-sm">[SeasonPanel — Track D-1]</p>
      </div>

      {/* GroupGrid (4 Timer Groups) — Track D-2 */}
      <section>
        <h2 className="text-text-muted text-sm font-semibold uppercase tracking-wide mb-3">
          Timer Groups
        </h2>
        <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-4 gap-4">
          {[1, 2, 3, 4].map((i) => (
            <div key={i} className="glass-card p-4 min-h-[180px]">
              <p className="text-text-subtle text-sm">[GroupCard {i} — Track D-2]</p>
            </div>
          ))}
        </div>
      </section>

      {/* NodeGrid (4 Actuator Nodes) — Track D-3 */}
      <section>
        <h2 className="text-text-muted text-sm font-semibold uppercase tracking-wide mb-3">
          Actuator Nodes
        </h2>
        <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-4 gap-4">
          {[1, 2, 3, 4].map((i) => (
            <div key={i} className="glass-card p-4 min-h-[180px]">
              <p className="text-text-subtle text-sm">[NodeCard {i} — Track D-3]</p>
            </div>
          ))}
        </div>
      </section>

      {/* Bottom panels row */}
      <div className="grid grid-cols-1 lg:grid-cols-2 gap-4">
        {/* TreatmentPanel — Track D-4 */}
        <div className="glass-card p-4">
          <p className="text-text-subtle text-sm">[TreatmentPanel — Track D-4]</p>
        </div>

        {/* MeasurementPanel — Track D-5 */}
        <div className="glass-card p-4">
          <p className="text-text-subtle text-sm">[MeasurementPanel — Track D-5]</p>
        </div>
      </div>
    </div>
  );
}
