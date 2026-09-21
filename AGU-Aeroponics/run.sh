#!/usr/bin/env bash
set -e

# Detect script directory and project root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Prefer python from platformio penv if available (has pyserial), else system python3
if [ -f "$HOME/.platformio/penv/bin/python" ]; then
    PYTHON_BIN="$HOME/.platformio/penv/bin/python"
else
    PYTHON_BIN="$(which python3)"
fi

exec "$PYTHON_BIN" "$SCRIPT_DIR/main.py" "$@"
