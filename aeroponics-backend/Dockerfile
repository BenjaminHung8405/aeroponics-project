# ==========================================
# Multi-Stage Dockerfile for NestJS Backend
# ==========================================

# --- Stage 1: Builder ---
FROM node:20-alpine AS builder

WORKDIR /app

# Enable Corepack for pnpm support
RUN corepack enable && corepack prepare pnpm@10.12.4 --activate

# Copy dependency definition files
COPY package.json pnpm-lock.yaml* ./

# Install dependencies (including devDependencies needed for build). This
# repository has no pnpm lockfile; avoid implying frozen-lockfile determinism.
RUN pnpm install --no-frozen-lockfile

# Copy application source code
COPY . .

# Build NestJS production bundle (dist/)
RUN pnpm run build

# Prune non-production dependencies
RUN pnpm prune --prod

# --- Stage 2: Runner ---
FROM node:20-alpine AS runner

WORKDIR /app

# Install curl for container healthcheck & operational maintenance
RUN apk add --no-cache curl

# Set production environment
ENV NODE_ENV=production

# Copy minimum production artifacts from builder stage
COPY --from=builder /app/package.json ./package.json
COPY --from=builder /app/node_modules ./node_modules
COPY --from=builder /app/dist ./dist

# Expose HTTP port for API gateway
EXPOSE 3001

# Healthcheck configuration matching docker-compose.yml
HEALTHCHECK --interval=20s --timeout=5s --start-period=60s --retries=5 \
  CMD curl -f http://localhost:3001/health || exit 1

# Launch NestJS production entrypoint
CMD ["node", "dist/main.js"]
