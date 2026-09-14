'use client';

import React, { useState } from 'react';
import { useRouter, useSearchParams } from 'next/navigation';
import { useAuth } from '@/hooks/useAuth';
import { AlertBanner } from '../common/AlertBanner';
import { Lock, User, Eye, EyeOff, Loader2, AlertTriangle } from 'lucide-react';

export function LoginForm() {
  const router = useRouter();
  const searchParams = useSearchParams();
  const from = searchParams.get('from') || '/dashboard';

  const { login, isLoading, error, clearError } = useAuth();
  const [username, setUsername] = useState('');
  const [password, setPassword] = useState('');
  const [showPassword, setShowPassword] = useState(false);

  const handleSubmit = async (e: React.FormEvent<HTMLFormElement>) => {
    e.preventDefault();
    clearError();

    if (!username.trim() || !password) {
      return;
    }

    const result = await login({
      username: username.trim(),
      password,
    });

    if (result.success) {
      router.push(from);
      router.refresh();
    }
  };

  return (
    <form onSubmit={handleSubmit} className="space-y-5" noValidate>
      {/* Error Alert Banner */}
      {error && (
        <AlertBanner
          error={error}
          fallbackContext="Đăng nhập không thành công"
        />
      )}


      {/* Username Field */}
      <div className="space-y-2">
        <label
          htmlFor="username"
          className="block text-sm font-medium text-text font-sans"
        >
          Tên đăng nhập
        </label>
        <div className="relative">
          <div className="absolute inset-y-0 left-0 pl-3.5 flex items-center pointer-events-none text-text-subtle">
            <User className="w-5 h-5" />
          </div>
          <input
            id="username"
            name="username"
            type="text"
            required
            autoComplete="username"
            disabled={isLoading}
            value={username}
            onChange={(e) => {
              setUsername(e.target.value);
              if (error) clearError();
            }}
            placeholder="admin"
            className="w-full pl-11 pr-4 py-3 bg-surface border border-border rounded-xl text-text placeholder:text-text-subtle text-base font-sans focus:outline-none focus:border-primary focus:ring-1 focus:ring-primary transition-colors disabled:opacity-50 disabled:cursor-not-allowed"
          />
        </div>
      </div>

      {/* Password Field */}
      <div className="space-y-2">
        <label
          htmlFor="password"
          className="block text-sm font-medium text-text font-sans"
        >
          Mật khẩu
        </label>
        <div className="relative">
          <div className="absolute inset-y-0 left-0 pl-3.5 flex items-center pointer-events-none text-text-subtle">
            <Lock className="w-5 h-5" />
          </div>
          <input
            id="password"
            name="password"
            type={showPassword ? 'text' : 'password'}
            required
            autoComplete="current-password"
            disabled={isLoading}
            value={password}
            onChange={(e) => {
              setPassword(e.target.value);
              if (error) clearError();
            }}
            placeholder="••••••••"
            className="w-full pl-11 pr-12 py-3 bg-surface border border-border rounded-xl text-text placeholder:text-text-subtle text-base font-sans focus:outline-none focus:border-primary focus:ring-1 focus:ring-primary transition-colors disabled:opacity-50 disabled:cursor-not-allowed"
          />
          <button
            type="button"
            onClick={() => setShowPassword(!showPassword)}
            disabled={isLoading}
            aria-label={showPassword ? 'Ẩn mật khẩu' : 'Hiển thị mật khẩu'}
            className="absolute inset-y-0 right-0 pr-3 flex items-center justify-center min-h-[44px] min-w-[44px] text-text-subtle hover:text-text focus:outline-none focus:text-primary transition-colors active:scale-95 cursor-pointer disabled:opacity-50"
          >
            {showPassword ? (
              <EyeOff className="w-5 h-5" />
            ) : (
              <Eye className="w-5 h-5" />
            )}
          </button>
        </div>
      </div>

      {/* Submit Button */}
      <button
        type="submit"
        disabled={isLoading || !username.trim() || !password}
        className="w-full min-h-[48px] px-6 py-3 bg-primary hover:bg-secondary text-background font-semibold font-sans rounded-xl transition-all duration-150 flex items-center justify-center gap-2 cursor-pointer active:scale-95 disabled:opacity-50 disabled:cursor-not-allowed disabled:active:scale-100 shadow-lg shadow-primary/20"
      >
        {isLoading ? (
          <>
            <Loader2 className="w-5 h-5 animate-spin" />
            <span>Đang xác thực...</span>
          </>
        ) : (
          <span>Đăng nhập</span>
        )}
      </button>
    </form>
  );
}
