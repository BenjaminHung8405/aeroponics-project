#!/usr/bin/env bash
set -euo pipefail

# ==============================================================================
# AEROPONICS LEAN — Infrastructure Health Check Script
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
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m' # No Color

# Load environment variables as data if .env exists; never execute it.
if [ -f ".env" ]; then
    load_safe_env_file .env
fi

# Mandatory Credential Fail-Closed Validation
DB_USER="${DB_USER:-}"
DB_PASS="${DB_PASS:-}"
DB_NAME="${DB_NAME:-aeroponics}"
MQTT_ADMIN_USER="${MQTT_ADMIN_USER:-}"
MQTT_ADMIN_PASS="${MQTT_ADMIN_PASS:-}"
BACKEND_PORT="${BACKEND_PORT:-3001}"

validate_credentials() {
    local missing=0
    if [ -z "$DB_USER" ]; then
        echo -e "${RED}[ERROR] DB_USER environment variable is missing.${NC}"
        missing=1
    fi
    if [ -z "$DB_PASS" ] || [ "$DB_PASS" = "aeroponics_secret" ] || [ "$DB_PASS" = "CHANGE_ME" ] || [ "$DB_PASS" = "CHANGE_ME_DB_PASSWORD" ]; then
        echo -e "${RED}[ERROR] DB_PASS environment variable is missing or set to an insecure default placeholder.${NC}"
        missing=1
    fi
    if [ -z "$MQTT_ADMIN_USER" ]; then
        echo -e "${RED}[ERROR] MQTT_ADMIN_USER environment variable is missing.${NC}"
        missing=1
    fi
    if [ -z "$MQTT_ADMIN_PASS" ] || [ "$MQTT_ADMIN_PASS" = "admin_secret" ] || [ "$MQTT_ADMIN_PASS" = "CHANGE_ME" ] || [ "$MQTT_ADMIN_PASS" = "CHANGE_ME_ADMIN_PASSWORD" ]; then
        echo -e "${RED}[ERROR] MQTT_ADMIN_PASS environment variable is missing or set to an insecure default placeholder.${NC}"
        missing=1
    fi

    if [ $missing -ne 0 ]; then
        echo -e "${RED}${BOLD}SECURITY FAIL-CLOSED: Infrastructure health check aborted due to missing/insecure credentials.${NC}"
        exit 1
    fi
}

validate_credentials

# Give the backend time to finish its startup healthcheck before evaluating the
# container status. This keeps the pre-check deterministic after a recreate.
echo "Waiting for backend readiness (up to 15 seconds)..."
backend_ready=0
for _ in {1..15}; do
    if command -v curl >/dev/null 2>&1 \
        && curl -fsS "http://localhost:${BACKEND_PORT}/health" 2>/dev/null \
        | grep -q '"status":"ok"'; then
        backend_ready=1
        break
    fi
    sleep 1
done

if [ "$backend_ready" -eq 0 ]; then
    echo -e "${YELLOW}[WARN] Backend did not become ready during the 15-second warmup window; continuing with the full checks.${NC}"
fi

TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0

echo -e "${BLUE}===================================================================================${NC}"
echo -e "${BOLD}${CYAN}                AEROPONICS LEAN — INFRASTRUCTURE HEALTH VERIFICATION                ${NC}"
echo -e "${BLUE}===================================================================================${NC}"
printf "%-12s | %-38s | %-16s | %s\n" "CATEGORY" "TEST ITEM" "TARGET" "RESULT"
echo -e "${BLUE}-------------+----------------------------------------+------------------+---------${NC}"

record_result() {
    local category="$1"
    local test_name="$2"
    local target="$3"
    local status="$4"
    local detail="${5:-}"

    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    if [ "$status" = "PASS" ]; then
        PASSED_TESTS=$((PASSED_TESTS + 1))
        printf "%-12s | %-38s | %-16s | ${GREEN}%-7s${NC} %s\n" "$category" "$test_name" "$target" "[PASS]" "$detail"
    else
        FAILED_TESTS=$((FAILED_TESTS + 1))
        printf "%-12s | %-38s | %-16s | ${RED}%-7s${NC} %s\n" "$category" "$test_name" "$target" "[FAIL]" "$detail"
    fi
}

# ------------------------------------------------------------------------------
# 1. Container Status Checks
# ------------------------------------------------------------------------------
check_container_health() {
    local container_name="$1"
    local display_name="$2"

    if ! command -v docker >/dev/null 2>&1 || ! docker info >/dev/null 2>&1; then
        record_result "Container" "$display_name" "$container_name" "FAIL" "(Docker daemon unreachable)"
        return
    fi

    local health_status
    health_status=$(docker inspect --format='{{if .State.Health}}{{.State.Health.Status}}{{else}}{{.State.Status}}{{end}}' "$container_name" 2>/dev/null || echo "not_found")

    if [ "$health_status" = "healthy" ]; then
        record_result "Container" "$display_name" "$container_name" "PASS" "(status: healthy)"
    elif [ "$health_status" = "running" ]; then
        record_result "Container" "$display_name" "$container_name" "PASS" "(status: running)"
    elif [ "$health_status" = "not_found" ]; then
        record_result "Container" "$display_name" "$container_name" "FAIL" "(container not found)"
    else
        record_result "Container" "$display_name" "$container_name" "FAIL" "(status: $health_status)"
    fi
}

check_container_health "aero_timescaledb" "TimescaleDB Container"
check_container_health "aero_mosquitto" "Mosquitto Broker Container"
check_container_health "aero_backend" "NestJS Backend Container"

# ------------------------------------------------------------------------------
# 2. MQTT Broker Authentication Checks
# ------------------------------------------------------------------------------
check_mqtt_auth() {
    if ! command -v docker >/dev/null 2>&1 || ! docker info >/dev/null 2>&1; then
        record_result "MQTT Auth" "Valid User Login" "port 1883" "FAIL" "(Docker unreachable)"
        record_result "MQTT Auth" "Anonymous Login Blocked" "port 1883" "FAIL" "(Docker unreachable)"
        return
    fi

    # Test Valid Auth
    local valid_auth_code=0
    docker exec aero_mosquitto mosquitto_pub -h localhost -p 1883 \
        -u "$MQTT_ADMIN_USER" -P "$MQTT_ADMIN_PASS" \
        -t 'aeroponics/healthcheck/test' -m 'ping' -q 0 >/dev/null 2>&1 || valid_auth_code=$?

    if [ $valid_auth_code -eq 0 ]; then
        record_result "MQTT Auth" "Valid User Login ($MQTT_ADMIN_USER)" "port 1883" "PASS" "(authenticated)"
    else
        record_result "MQTT Auth" "Valid User Login ($MQTT_ADMIN_USER)" "port 1883" "FAIL" "(auth rejected or container down)"
    fi

    # Test Anonymous Auth (Should be REJECTED)
    local anon_auth_code=0
    docker exec aero_mosquitto mosquitto_pub -h localhost -p 1883 \
        -t 'aeroponics/healthcheck/test' -m 'ping' -q 0 >/dev/null 2>&1 || anon_auth_code=$?

    if [ $anon_auth_code -ne 0 ]; then
        record_result "MQTT Auth" "Anonymous Login Blocked" "port 1883" "PASS" "(rejected as expected)"
    else
        record_result "MQTT Auth" "Anonymous Login Blocked" "port 1883" "FAIL" "(SECURITY LEAK: anonymous allowed!)"
    fi
}

check_mqtt_auth

# ------------------------------------------------------------------------------
# 3. TimescaleDB Database & Schema Checks
# ------------------------------------------------------------------------------
check_timescaledb_schema() {
    if ! command -v docker >/dev/null 2>&1 || ! docker info >/dev/null 2>&1; then
        record_result "TimescaleDB" "TimescaleDB Extension" "db: $DB_NAME" "FAIL" "(Docker unreachable)"
        record_result "TimescaleDB" "Production Tables (11 tables)" "db: $DB_NAME" "FAIL" "(Docker unreachable)"
        record_result "TimescaleDB" "Production Hypertables (5 hypertables)" "db: $DB_NAME" "FAIL" "(Docker unreachable)"
        record_result "TimescaleDB" "Production Constraints" "db: $DB_NAME" "FAIL" "(Docker unreachable)"
        return
    fi

    # 1. Extension Check
    local ext_count
    ext_count=$(docker exec -i aero_timescaledb psql -U "$DB_USER" -d "$DB_NAME" -t -A -c \
        "SELECT count(*) FROM pg_extension WHERE extname IN ('timescaledb', 'pgcrypto');" 2>/dev/null || echo "error")

    if [ "$ext_count" = "2" ]; then
        record_result "TimescaleDB" "TimescaleDB & pgcrypto Extensions" "extension" "PASS" "(extensions enabled)"
    else
        record_result "TimescaleDB" "TimescaleDB & pgcrypto Extensions" "extension" "FAIL" "(extensions missing or query failed)"
    fi

    # 2. Production Tables Check (11 tables)
    local tbl_count
    tbl_count=$(docker exec -i aero_timescaledb psql -U "$DB_USER" -d "$DB_NAME" -t -A -c \
        "SELECT count(*) FROM information_schema.tables WHERE table_schema = 'public' AND table_name IN ('devices', 'seasons', 'treatments', 'treatment_versions', 'timer_groups', 'group_treatment_assignments', 'group_node_assignments', 'sensor_calibrations', 'node_registry', 'device_status', 'tuya_measurement_sessions');" 2>/dev/null || echo "error")

    if [ "$tbl_count" = "11" ]; then
        record_result "TimescaleDB" "Production Tables (11 tables)" "public schema" "PASS" "(all 11 production tables present)"
    else
        record_result "TimescaleDB" "Production Tables (11 tables)" "public schema" "FAIL" "($tbl_count/11 production tables present)"
    fi

    # 3. Production Hypertables Check (5 hypertables)
    local ht_count
    ht_count=$(docker exec -i aero_timescaledb psql -U "$DB_USER" -d "$DB_NAME" -t -A -c \
        "SELECT count(*) FROM _timescaledb_catalog.hypertable WHERE table_name IN ('pump_commands', 'pump_state_events', 'pump_feedback_events', 'flow_events', 'measurement_readings');" 2>/dev/null || echo "error")

    if [ "$ht_count" = "5" ]; then
        record_result "TimescaleDB" "Production Hypertables (5 hypertables)" "timescaledb" "PASS" "(pump_commands, pump_state_events, pump_feedback_events, flow_events, measurement_readings)"
    else
        record_result "TimescaleDB" "Production Hypertables (5 hypertables)" "timescaledb" "FAIL" "($ht_count/5 production hypertables initialized)"
    fi

    # 4. Contract constraints: calibration integrity, active mappings, and event season attribution.
    local constraint_count
    constraint_count=$(docker exec -i aero_timescaledb psql -U "$DB_USER" -d "$DB_NAME" -t -A -c \
        "SELECT count(*) FROM (SELECT 1 WHERE EXISTS (SELECT 1 FROM pg_constraint WHERE conrelid = 'sensor_calibrations'::regclass AND contype = 'u' AND pg_get_constraintdef(oid) LIKE '%node_id, sensor_serial, version_num%') AND EXISTS (SELECT 1 FROM pg_constraint WHERE conrelid = 'sensor_calibrations'::regclass AND contype = 'c' AND pg_get_constraintdef(oid) LIKE '%trial_count%') AND EXISTS (SELECT 1 FROM pg_constraint WHERE conrelid = 'sensor_calibrations'::regclass AND contype = 'c' AND pg_get_constraintdef(oid) LIKE '%pulses_per_litre%') AND (SELECT count(*) FROM pg_trigger WHERE tgname IN ('trg_node_registry_active_calibration', 'trg_flow_events_require_active_calibration', 'trg_pump_commands_require_active_calibration') AND NOT tgisinternal) = 3 AND EXISTS (SELECT 1 FROM pg_indexes WHERE indexname = 'uq_group_node_assignments_one_current_node') AND EXISTS (SELECT 1 FROM pg_indexes WHERE indexname = 'uq_group_treatment_assignments_one_current_group') AND (SELECT count(*) FROM information_schema.columns WHERE table_schema = 'public' AND table_name = 'flow_events' AND column_name = 'sensor_calibration_id' AND is_nullable = 'NO') = 1 AND (SELECT count(*) FROM information_schema.columns WHERE table_schema = 'public' AND table_name IN ('pump_commands', 'pump_state_events', 'pump_feedback_events', 'flow_events') AND column_name = 'season_id' AND is_nullable = 'NO') = 4) contract;" 2>/dev/null || echo "error")
    if [ "$constraint_count" = "1" ]; then
        record_result "TimescaleDB" "Production Constraints" "schema contract" "PASS" "(calibration, active assignments, season attribution)"
    else
        record_result "TimescaleDB" "Production Constraints" "schema contract" "FAIL" "(missing required calibration, assignment, or season constraint)"
    fi
}

check_timescaledb_schema

# ------------------------------------------------------------------------------
# 4. NestJS REST Endpoint Check
# ------------------------------------------------------------------------------
check_backend_rest() {
    local target_url="http://localhost:${BACKEND_PORT}/health"
    local http_code=""
    local response_body=""

    if command -v curl >/dev/null 2>&1; then
        local curl_out
        curl_out=$(curl -s -w "\n%{http_code}" "$target_url" 2>/dev/null || echo "FAIL")
        http_code=$(echo "$curl_out" | tail -n1)
        response_body=$(echo "$curl_out" | sed '$d')
    else
        record_result "REST API" "Backend Health Endpoint" "$target_url" "FAIL" "(curl not installed)"
        return
    fi

    if [ "$http_code" = "200" ] && [[ "$response_body" == *"\"status\""* ]]; then
        record_result "REST API" "Backend Health Endpoint" "$target_url" "PASS" "(200 OK: $response_body)"
    else
        record_result "REST API" "Backend Health Endpoint" "$target_url" "FAIL" "(HTTP: ${http_code:-N/A}, body: ${response_body:-unreachable})"
    fi
}

check_backend_rest

# ------------------------------------------------------------------------------
# Report Summary
# ------------------------------------------------------------------------------
echo -e "${BLUE}===================================================================================${NC}"
if [ $FAILED_TESTS -eq 0 ]; then
    echo -e "${GREEN}${BOLD} VERIFICATION SUCCESS: All ${TOTAL_TESTS} tests passed! Infrastructure is 100% healthy.${NC}"
    echo -e "${BLUE}===================================================================================${NC}"
    exit 0
else
    echo -e "${RED}${BOLD} VERIFICATION FAILED: ${FAILED_TESTS}/${TOTAL_TESTS} tests failed.${NC}"
    echo -e "${BLUE}===================================================================================${NC}"
    exit 1
fi
