import type { NextConfig } from 'next';

const nextConfig: NextConfig = {
  /**
   * output: 'standalone' — Next.js generates a self-contained server.js
   * that includes only necessary runtime files. Used in Docker multi-stage
   * builder → runner stage (Stage 3 of Dockerfile).
   */
  output: 'standalone',

  /** Hide X-Powered-By: Next.js header for security hardening */
  poweredByHeader: false,

  images: {
    formats: ['image/avif', 'image/webp'],
  },

  /**
   * Reverse proxy fallback:
   * Chuyển tiếp các request /api sang backend NestJS khi truy cập trực tiếp qua domain UI
   */
  async rewrites() {
    const backendUrl = process.env.INTERNAL_BACKEND_URL || 'http://aero-backend:3001';
    return [
      {
        source: '/api/auth/login',
        destination: `${backendUrl}/api/auth/login`,
      },
      {
        source: '/api/:path((?!set-token|clear-token).*)',
        destination: `${backendUrl}/api/:path*`,
      },
    ];
  },
};

export default nextConfig;
