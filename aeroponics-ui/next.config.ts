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
};

export default nextConfig;
