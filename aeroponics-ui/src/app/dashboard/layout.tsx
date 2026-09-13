import React from 'react';
import { WsBanner } from '../../components/common/WsBanner';
import { Header } from '../../components/layout/Header';
import { MobileActionBar } from '../../components/layout/MobileActionBar';

/**
 * Dashboard Layout — Protected route group (dashboard).
 *
 * middleware.ts đã guard toàn bộ /dashboard/* trước khi render.
 * Layout này chịu trách nhiệm:
 *  - WsBanner (disconnect warning, auto-hide, backoff countdown)
 *  - Header (brand, live ICT clock, telemetry status, logout)
 *  - Main content area với iOS safe-area padding & mobile action bar clearance
 *  - MobileActionBar (sticky bottom bar cho mobile viewports < 768px per dashboard.md §5)
 */
export default function DashboardLayout({
  children,
}: {
  children: React.ReactNode;
}) {
  return (
    <div className="min-h-dvh bg-background text-text flex flex-col">
      {/* WsBanner — Track S4-D6 (Hard Rule S4-BANNER-19) */}
      <WsBanner />

      {/* Main content area */}
      <div className="flex-1 max-w-7xl w-full mx-auto px-4 sm:px-6 lg:px-8 py-4 pb-[max(84px,calc(76px+env(safe-area-inset-bottom)))] md:pb-[max(24px,env(safe-area-inset-bottom))]">
        {/* Header — Brand identity, live ICT clock, WS status */}
        <Header />

        {/* Dashboard page content */}
        <main>{children}</main>
      </div>

      {/* Mobile Sticky Action Bar — Track S4-E (MASTER.md & dashboard.md §5) */}
      <MobileActionBar />
    </div>
  );
}
