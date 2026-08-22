#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FW_DIR="$ROOT/aeroponics-firmware"
SRC_DIR="$FW_DIR/src"
INC_DIR="$FW_DIR/include"
PLATFORMIO_INI="$FW_DIR/platformio.ini"
MAIN_CPP="$SRC_DIR/main.cpp"

echo "=== [R6-M] Checking Production Architecture & Clean Separation ==="

# 1. Check that main.cpp composition root does not include or instantiate RelayController or legacy ScheduleManager
if grep -E '(RelayController|#include "schedule_manager.h"|IRelayOutput)' "$MAIN_CPP" >/dev/null 2>&1; then
    echo "FAIL: main.cpp contains references to legacy RelayController, IRelayOutput, or schedule_manager.h" >&2
    exit 1
fi
echo "[OK] Gateway main.cpp composition root does not construct or include legacy RelayController or ScheduleManager."

# 2. Check that production src/ and include/ (excluding prototype/ and integration/) contain zero legacy direct relay symbols
LEGACY_SYMBOLS='(RelayController|IRelayOutput|TOTAL_RELAYS|RELAY1_GPIO|RELAY2_GPIO|RELAY3_GPIO|RELAY4_GPIO|relay_profiles|relay_events)'

if command -v rg >/dev/null 2>&1; then
    MATCHES=$(rg -n -E "$LEGACY_SYMBOLS" "$SRC_DIR" "$INC_DIR" --glob '!prototype/**' --glob '!integration/**' || true)
else
    MATCHES=$(grep -r -n -E "$LEGACY_SYMBOLS" --exclude-dir=prototype --exclude-dir=integration "$SRC_DIR" "$INC_DIR" 2>/dev/null || true)
fi

if [ -n "$MATCHES" ]; then
    echo "FAIL: Found legacy relay symbols in production source or header files:" >&2
    echo "$MATCHES" >&2
    exit 1
fi
echo "[OK] Production sources and headers are 100% clean of legacy direct relay symbols."

# 3. Check that platformio.ini excludes prototype/ and integration/ from production environments
if ! sed -n '/^\[env:esp32-s3-devkitc-1\]/,/^\[/p' "$PLATFORMIO_INI" | grep -Fq -- '-<prototype/>'; then
    echo "FAIL: [env:esp32-s3-devkitc-1] in platformio.ini does not exclude -<prototype/>" >&2
    exit 1
fi
if ! sed -n '/^\[env:native\]/,/^\[/p' "$PLATFORMIO_INI" | grep -Fq -- '-<prototype/>'; then
    echo "FAIL: [env:native] in platformio.ini does not exclude -<prototype/>" >&2
    exit 1
fi
echo "[OK] platformio.ini properly isolates production build filters (-<prototype/>, -<integration/>)."

# 4. Check that prototype code is properly preserved in prototype/legacy_relay/ for rollback
if [ ! -f "$SRC_DIR/prototype/legacy_relay/relay_controller.cpp" ] || \
   [ ! -f "$SRC_DIR/prototype/legacy_relay/schedule_manager.cpp" ] || \
   [ ! -f "$INC_DIR/prototype/legacy_relay/relay_controller.h" ] || \
   [ ! -f "$INC_DIR/prototype/legacy_relay/schedule_manager.h" ]; then
    echo "FAIL: Prototype archive files are missing; rollback integrity compromised" >&2
    exit 1
fi
echo "[OK] Prototype legacy relay implementation is intact in prototype/legacy_relay/ for hardware rig rollback."

echo "=== [R6-M] PASS: Clean production architecture and prototype isolation verified! ==="
