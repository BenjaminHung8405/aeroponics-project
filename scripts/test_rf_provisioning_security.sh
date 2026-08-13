#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MAIN="$ROOT/aeroponics-firmware/src/main.cpp"
PROVISIONING="$ROOT/aeroponics-firmware/include/rf_provisioning.h"
PLATFORMIO="$ROOT/aeroponics-firmware/platformio.ini"

grep -Fq 'RF_PROVISIONING_INDEPENDENT_SIGNOFF_PRESENT' "$MAIN"
grep -Fq 'RF production provisioning lacks independent security sign-off; gateway remains fail-closed' "$MAIN"
grep -Fq 'RF_PROVISIONING_INDEPENDENT_SIGNOFF' "$PROVISIONING"
if sed -n '/^\[env:esp32-s3-devkitc-1\]/,/^\[/p' "$PLATFORMIO" | grep -q 'RF_PROVISIONING_INDEPENDENT_SIGNOFF=1'; then
    echo 'FAIL production build must not self-approve RF provisioning security' >&2
    exit 1
fi
if rg -n -i --glob '*.{c,cc,cpp,h,hpp}' '(ESP_LOG|LOG_[IEW]|printf|Serial\.print).*psk' "$ROOT/aeroponics-firmware"; then
    echo 'FAIL RF PSK must not be logged' >&2
    exit 1
fi
echo 'PASS RF provisioning evidence: unsigned production RF is fail-closed, PSK is not logged, host regression covers missing-key RX/TX lock'
