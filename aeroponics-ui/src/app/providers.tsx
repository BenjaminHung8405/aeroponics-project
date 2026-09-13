'use client';

import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { useState, type ReactNode } from 'react';

/**
 * Providers — Client-side provider wrapper for the App Router.
 *
 * Wraps TanStack Query's QueryClientProvider so server components
 * (layout.tsx) can remain pure server components while still giving
 * children access to the query client.
 *
 * staleTime default 30s — matches sprint_4.md §C-4 TanStack Query Hooks.
 */
export function Providers({ children }: { children: ReactNode }) {
  const [queryClient] = useState(
    () =>
      new QueryClient({
        defaultOptions: {
          queries: {
            staleTime: 30_000,
            refetchOnWindowFocus: false,
          },
        },
      }),
  );

  return (
    <QueryClientProvider client={queryClient}>{children}</QueryClientProvider>
  );
}
