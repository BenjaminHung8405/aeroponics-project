/**
 * Dashboard Layout — Protected route group (dashboard).
 *
 * middleware.ts đã guard toàn bộ /dashboard/* trước khi render.
 * Layout này chịu trách nhiệm:
 *  - Sticky header với WS status badge
 *  - WsBanner (disconnect warning) — implement Track D-6
 *  - Main content area với safe-area padding
 *
 * Track D-6 sẽ implement WsBanner component.
 * Track C-2 sẽ implement useWebSocket hook.
 */
export default function DashboardLayout({
  children,
}: {
  children: React.ReactNode;
}) {
  return (
    <div className="min-h-dvh bg-background">
      {/* WsBanner — Track D-6 */}
      {/* <WsBanner /> */}

      {/* Main content */}
      <main className="max-w-7xl mx-auto px-4 sm:px-6 lg:px-8 py-4 pb-[env(safe-area-inset-bottom)]">
        {children}
      </main>
    </div>
  );
}
