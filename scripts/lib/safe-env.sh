#!/usr/bin/env bash
# Shared data-only .env parser. Do not source untrusted environment files.

readonly AERO_ENV_ALLOWED_KEYS='^(DB_USER|DB_PASS|DB_NAME|MQTT_PORT|MQTT_WS_PORT|MQTT_ADMIN_USER|MQTT_ADMIN_PASS|MQTT_DEVICE_USER|MQTT_DEVICE_PASS|MQTT_BACKEND_USER|MQTT_BACKEND_PASS|BACKEND_PORT|JWT_SECRET|TUYA_DEVICE_IP|TUYA_DEVICE_ID|TUYA_LOCAL_KEY|TUYA_SENSOR_ID|TUYA_ON_DEMAND_TIMEOUT_MS|WIFI_SSID|WIFI_PASSWORD|DEVICE_ID)$'

load_safe_env_file() {
    local file="$1" line key value
    while IFS= read -r line || [ -n "$line" ]; do
        [[ -z "$line" || "$line" =~ ^[[:space:]]*# ]] && continue
        if [[ ! "$line" =~ ^([A-Z][A-Z0-9_]*)=([^$'\r\n']*)$ ]]; then
            echo "Unsafe or malformed .env entry rejected." >&2
            return 1
        fi
        key="${BASH_REMATCH[1]}"; value="${BASH_REMATCH[2]}"
        if [[ ! "$key" =~ $AERO_ENV_ALLOWED_KEYS ]]; then
            echo "Unsupported .env key rejected: $key" >&2
            return 1
        fi
        if [[ "$value" == *'$('* || "$value" == *'`'* || "$value" == *';'* || "$value" == *'&'* || "$value" == *'|'* ]]; then
            echo "Unsafe .env value rejected." >&2
            return 1
        fi
        printf -v "$key" '%s' "$value"
        export "$key"
    done < "$file"
}
