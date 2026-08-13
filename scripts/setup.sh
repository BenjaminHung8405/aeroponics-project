#!/usr/bin/env bash
set -euo pipefail

# ==============================================================================
# AEROPONICS LEAN — 1-Click Setup Script
# ==============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${PROJECT_ROOT}"
source "${PROJECT_ROOT}/scripts/lib/safe-env.sh"

# Formatting & Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[OK]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

echo -e "${BLUE}=====================================================${NC}"
echo -e "${BLUE}   Aeroponics Infrastructure Setup (Sprint 0)       ${NC}"
echo -e "${BLUE}=====================================================${NC}"

# ------------------------------------------------------------------------------
# 1. Check Docker & Docker Compose CLI
# ------------------------------------------------------------------------------
log_info "1/6 Checking Docker & Docker Compose installation..."

if ! command -v docker >/dev/null 2>&1; then
    log_error "Docker CLI is not installed or not in PATH. Please install Docker."
    exit 1
fi

if ! docker info >/dev/null 2>&1; then
    log_error "Docker daemon is not running. Please start Docker Engine / Docker Desktop."
    exit 1
fi

COMPOSE_CMD=""
if docker compose version >/dev/null 2>&1; then
    COMPOSE_CMD="docker compose"
elif command -v docker-compose >/dev/null 2>&1; then
    COMPOSE_CMD="docker-compose"
else
    log_error "Neither 'docker compose' nor 'docker-compose' plugin was found."
    exit 1
fi

log_success "Docker CLI and ${COMPOSE_CMD} are ready."

# ------------------------------------------------------------------------------
# 2. Check and initialize .env file
# ------------------------------------------------------------------------------
log_info "2/6 Checking environment configuration (.env)..."

if [ ! -f ".env" ]; then
    if [ -f ".env.example" ]; then
        cp .env.example .env
        log_warn "File .env not found. Created .env from .env.example."
    else
        log_error "Neither .env nor .env.example found in project root."
        exit 1
    fi
else
    log_success "File .env already exists."
fi

# Parse .env as data, never as shell code.
load_safe_env_file .env

# ------------------------------------------------------------------------------
# 3. Validate secrets in .env
# ------------------------------------------------------------------------------
log_info "3/6 Validating security credentials in .env..."

REQUIRED_VARS=(
    "DB_PASS"
    "MQTT_ADMIN_PASS"
    "MQTT_DEVICE_PASS"
    "MQTT_BACKEND_PASS"
    "JWT_SECRET"
)

INVALID_VARS=()

for var_name in "${REQUIRED_VARS[@]}"; do
    var_val="${!var_name:-}"
    if [ -z "${var_val}" ]; then
        INVALID_VARS+=("${var_name} (Missing/Empty)")
    elif [[ "${var_val}" == *"CHANGE_ME"* ]]; then
        INVALID_VARS+=("${var_name} (Contains placeholder 'CHANGE_ME')")
    fi
done

if [ ${#INVALID_VARS[@]} -ne 0 ]; then
    log_error "Security validation failed! The following variables in .env must be set to real values:"
    for inv in "${INVALID_VARS[@]}"; do
        echo -e "   ${RED}• ${inv}${NC}"
    done
    log_error "Please update your .env file and run ./scripts/setup.sh again."
    exit 1
fi

log_success "All mandatory secrets in .env are validly configured."

# ------------------------------------------------------------------------------
# 4. Ensure directory structure exists
# ------------------------------------------------------------------------------
log_info "4/6 Verifying infrastructure directories..."

mkdir -p mosquitto/config mosquitto/data database

log_success "Directories (mosquitto/config, mosquitto/data, database) verified."

# ------------------------------------------------------------------------------
# 5. Generate mosquitto/config/passwd
# ------------------------------------------------------------------------------
log_info "5/6 Generating Mosquitto password file (mosquitto/config/passwd)..."

# A previous interrupted setup may leave the ignored target as a directory.
# Remove it only when empty; never delete an existing credential file silently.
if [ -d "mosquitto/config/passwd" ]; then
    if ! rmdir "mosquitto/config/passwd" 2>/dev/null; then
        log_error "mosquitto/config/passwd is a non-empty directory; refusing to remove it."
        exit 1
    fi
fi

MQTT_ADMIN_USER="${MQTT_ADMIN_USER:-mqtt_admin}"
MQTT_DEVICE_USER="${MQTT_DEVICE_USER:-esp32_device}"
MQTT_BACKEND_USER="${MQTT_BACKEND_USER:-aero_backend}"

# Check if local mosquitto_passwd is present, otherwise use Docker
if command -v mosquitto_passwd >/dev/null 2>&1; then
    mosquitto_passwd -c -b mosquitto/config/passwd "${MQTT_ADMIN_USER}" "${MQTT_ADMIN_PASS}"
    mosquitto_passwd -b mosquitto/config/passwd "${MQTT_DEVICE_USER}" "${MQTT_DEVICE_PASS}"
    mosquitto_passwd -b mosquitto/config/passwd "${MQTT_BACKEND_USER}" "${MQTT_BACKEND_PASS}"
else
    # Run via Docker eclipse-mosquitto:2.0 container
    docker run --rm -v "${PROJECT_ROOT}/mosquitto/config:/config" eclipse-mosquitto:2.0 \
        mosquitto_passwd -c -b /config/passwd "${MQTT_ADMIN_USER}" "${MQTT_ADMIN_PASS}" >/dev/null 2>&1
    docker run --rm -v "${PROJECT_ROOT}/mosquitto/config:/config" eclipse-mosquitto:2.0 \
        mosquitto_passwd -b /config/passwd "${MQTT_DEVICE_USER}" "${MQTT_DEVICE_PASS}" >/dev/null 2>&1
    docker run --rm -v "${PROJECT_ROOT}/mosquitto/config:/config" eclipse-mosquitto:2.0 \
        mosquitto_passwd -b /config/passwd "${MQTT_BACKEND_USER}" "${MQTT_BACKEND_PASS}" >/dev/null 2>&1
fi

chmod 700 mosquitto/config/passwd
log_success "Mosquitto authentication file generated for users: ${MQTT_ADMIN_USER}, ${MQTT_DEVICE_USER}, ${MQTT_BACKEND_USER}."

# ------------------------------------------------------------------------------
# 6. Check host port conflicts
# ------------------------------------------------------------------------------
log_info "6/6 Checking host port availability..."

PORTS_TO_CHECK=(
    "${MQTT_PORT:-1883}"
    "${MQTT_WS_PORT:-9001}"
    "${BACKEND_PORT:-3001}"
)

check_port_in_use() {
    local port="$1"
    if command -v lsof >/dev/null 2>&1; then
        lsof -iTCP:"${port}" -sTCP:LISTEN >/dev/null 2>&1
    elif command -v nc >/dev/null 2>&1; then
        nc -z 127.0.0.1 "${port}" >/dev/null 2>&1
    else
        return 1
    fi
}

CONFLICT_FOUND=false

for port in "${PORTS_TO_CHECK[@]}"; do
    if check_port_in_use "${port}"; then
        # Check if the port is used by our own aeroponics docker container
        if docker ps --format '{{.Names}} {{.Ports}}' 2>/dev/null | grep -q "aero_" | grep -q ":${port}->"; then
            log_info "Port ${port} is currently bound by an existing aeroponics container."
        else
            log_warn "Port ${port} is currently in use on the host system."
            CONFLICT_FOUND=true
        fi
    else
        log_success "Port ${port} is free."
    fi
done

if [ "${CONFLICT_FOUND}" = true ]; then
    log_warn "One or more required ports are in use by external applications."
    log_warn "If 'docker compose up -d' fails, please free up those ports or adjust .env."
fi

# ------------------------------------------------------------------------------
# Summary & Next Steps
# ------------------------------------------------------------------------------
echo -e "${BLUE}=====================================================${NC}"
echo -e "${GREEN} Setup completed successfully! ${NC}"
echo -e "${BLUE}=====================================================${NC}"
echo -e "Next steps to launch infrastructure:"
echo -e "  1. Start services:     ${YELLOW}${COMPOSE_CMD} up -d${NC}"
echo -e "  2. Verify health:      ${YELLOW}./scripts/health-check.sh${NC}"
echo -e "${BLUE}=====================================================${NC}"
