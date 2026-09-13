/**
 * Type-Safe API Client for Aeroponics Smart Farm
 * Adheres to:
 *  - S4-AUTH-02: Zero token in localStorage, cookie-based authentication.
 *  - S4-AUTH-03: Auto-logout on 401 without infinite loops.
 *  - S4-API-05: Dynamic base URL with /api fallback (zero hardcoded host/port).
 */

import { handleUnauthorized } from './auth';

export class ApiError extends Error {
  readonly status: number;
  readonly statusText: string;
  readonly data?: any;

  constructor(status: number, statusText: string, data?: any) {
    let msg = `API Error ${status}: ${statusText}`;
    if (data?.message) {
      msg = Array.isArray(data.message) ? data.message.join(', ') : data.message;
    }
    super(msg);
    this.name = 'ApiError';
    this.status = status;
    this.statusText = statusText;
    this.data = data;
  }
}

/**
 * Builds the fully-qualified API URL.
 * Supports /api fallback, relative paths, and dev base URL overrides seamlessly.
 */
export function buildApiUrl(path: string): string {
  const rawBase = (process.env.NEXT_PUBLIC_API_URL || '').trim().replace(/\/+$/, '');
  const normalizedPath = path.startsWith('/') ? path : `/${path}`;

  // If no external API base is configured, route to same-origin /api (Nginx reverse proxy)
  if (!rawBase) {
    if (normalizedPath.startsWith('/api/') || normalizedPath === '/api') {
      return normalizedPath;
    }
    return `/api${normalizedPath}`;
  }

  // If base URL already ends with /api
  if (rawBase.endsWith('/api')) {
    if (normalizedPath.startsWith('/api/')) {
      return `${rawBase}${normalizedPath.substring(4)}`;
    }
    return `${rawBase}${normalizedPath}`;
  }

  // Base URL without /api (e.g. custom remote host)
  if (normalizedPath.startsWith('/api/') || normalizedPath === '/api') {
    return `${rawBase}${normalizedPath}`;
  }
  return `${rawBase}/api${normalizedPath}`;
}

/**
 * Standard fetch wrapper with credentials: 'include', automatic 401 interception,
 * and type-safe JSON response handling.
 */
export async function apiFetch<T>(
  path: string,
  options: RequestInit = {},
): Promise<T> {
  const url = buildApiUrl(path);

  const headers = new Headers(options.headers || {});
  if (!headers.has('Accept')) {
    headers.set('Accept', 'application/json');
  }

  // If request has a body and no Content-Type specified, default to application/json
  if (options.body && !headers.has('Content-Type')) {
    headers.set('Content-Type', 'application/json');
  }

  const finalOptions: RequestInit = {
    ...options,
    headers,
    // MANDATORY S4-AUTH-02: Send httpOnly cookies automatically
    credentials: 'include',
  };

  const response = await fetch(url, finalOptions);

  if (!response.ok) {
    let errorData: any = null;
    try {
      errorData = await response.json();
    } catch {
      // Body may not be valid JSON
    }

    // Intercept 401 Unauthorized -> Clear session and redirect to /login
    if (response.status === 401) {
      await handleUnauthorized();
    }

    throw new ApiError(response.status, response.statusText, errorData);
  }

  // Handle 204 No Content
  if (response.status === 204) {
    return null as T;
  }

  const contentType = response.headers.get('content-type');
  if (contentType && contentType.includes('application/json')) {
    return (await response.json()) as T;
  }

  return (await response.text()) as unknown as T;
}
