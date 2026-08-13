#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT/scripts/lib/safe-env.sh"
tmp_env="$(mktemp)"
trap 'rm -f "$tmp_env"' EXIT
cp "$ROOT/.env.example" "$tmp_env"
sed -i.bak \
    -e 's/CHANGE_ME_DB_PASSWORD/test-db-password/' \
    -e 's/CHANGE_ME_ADMIN_PASSWORD/test-admin-password/' \
    -e 's/CHANGE_ME_DEVICE_PASSWORD/test-device-password/' \
    -e 's/CHANGE_ME_BACKEND_PASSWORD/test-backend-password/' \
    -e 's/CHANGE_ME_MIN_32_CHARS_RANDOM_STRING/test-jwt-secret-with-at-least-32-chars/' \
    "$tmp_env"
rm -f "$tmp_env.bak"
load_safe_env_file "$tmp_env"
[[ "$MQTT_PORT" == "1883" && "$TUYA_ON_DEMAND_TIMEOUT_MS" == "5000" && "$MQTT_DEVICE_ID" == "$MQTT_DEVICE_USER" && "$DEVICE_ID" == "$MQTT_DEVICE_USER" ]]
echo "PASS safe .env parser accepts complete .env.example contract"
