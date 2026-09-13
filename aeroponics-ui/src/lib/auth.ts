/**
 * Authentication Client Utilities
 * Handles 401 unauthorized redirects and session cleanup.
 */

let isLoggingOut = false;

/**
 * Global handler for 401 Unauthorized responses.
 * Clears httpOnly token cookie and redirects to login.
 * Includes singleton guard to prevent infinite redirect loops.
 */
export async function handleUnauthorized(): Promise<void> {
  if (typeof window === 'undefined' || isLoggingOut) return;
  isLoggingOut = true;

  try {
    await fetch('/api/clear-token', { method: 'POST' });
  } catch {
    // Ignore network failure when clearing token during emergency redirect
  } finally {
    const currentPath = window.location.pathname;
    const loginUrl = currentPath && currentPath !== '/login'
      ? `/login?from=${encodeURIComponent(currentPath)}`
      : '/login';
    window.location.href = loginUrl;
  }
}

/**
 * Reset logging out flag (for testing purposes)
 */
export function _resetLoggingOutState(): void {
  isLoggingOut = false;
}
