'use client';

import { useState, useCallback } from 'react';

export interface LoginCredentials {
  username: string;
  password: string;
}

export interface UseAuthReturn {
  login: (credentials: LoginCredentials) => Promise<{ success: boolean; error?: string }>;
  logout: () => Promise<void>;
  isLoading: boolean;
  error: string | null;
  clearError: () => void;
}

import { handleUnauthorized } from '../lib/auth';
export { handleUnauthorized };

/**
 * useAuth Hook — Manages authentication lifecycle in Next.js client components.
 * Adheres to Hard Rule S4-AUTH-02 (zero token in localStorage) and S4-API-05 (dynamic URL fallback).
 */
export function useAuth(): UseAuthReturn {
  const [isLoading, setIsLoading] = useState<boolean>(false);
  const [error, setError] = useState<string | null>(null);

  const clearError = useCallback(() => setError(null), []);

  const login = useCallback(async (credentials: LoginCredentials) => {
    setIsLoading(true);
    setError(null);

    try {
      const apiBase = process.env.NEXT_PUBLIC_API_URL?.replace(/\/$/, '') || '';
      const loginEndpoint = `${apiBase}/api/auth/login`;

      const res = await fetch(loginEndpoint, {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
        },
        body: JSON.stringify(credentials),
      });

      if (!res.ok) {
        let errorMsg = 'Đăng nhập không thành công. Vui lòng kiểm tra lại thông tin.';
        try {
          const errData = await res.json();
          if (errData?.message) {
            errorMsg = Array.isArray(errData.message)
              ? errData.message.join(', ')
              : errData.message;
          }
        } catch {
          // Fallback to default message if response is not JSON
        }
        setError(errorMsg);
        return { success: false, error: errorMsg };
      }

      const data = await res.json();
      const accessToken = data.access_token;

      if (!accessToken || typeof accessToken !== 'string') {
        const errorMsg = 'Phản hồi từ máy chủ không hợp lệ.';
        setError(errorMsg);
        return { success: false, error: errorMsg };
      }

      // Set httpOnly cookie via Next.js Route Handler
      const cookieRes = await fetch('/api/set-token', {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
        },
        body: JSON.stringify({ token: accessToken }),
      });

      if (!cookieRes.ok) {
        const errorMsg = 'Lỗi thiết lập phiên đăng nhập an toàn.';
        setError(errorMsg);
        return { success: false, error: errorMsg };
      }

      return { success: true };
    } catch {
      const errorMsg = 'Lỗi kết nối máy chủ. Vui lòng thử lại sau.';
      setError(errorMsg);
      return { success: false, error: errorMsg };
    } finally {
      setIsLoading(false);
    }
  }, []);

  const logout = useCallback(async () => {
    setIsLoading(true);
    try {
      await fetch('/api/clear-token', { method: 'POST' });
    } catch {
      // Ignore network errors on logout
    } finally {
      setIsLoading(false);
      window.location.href = '/login';
    }
  }, []);

  return {
    login,
    logout,
    isLoading,
    error,
    clearError,
  };
}
