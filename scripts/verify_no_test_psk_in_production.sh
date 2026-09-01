#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
production="$root/aeroponics-firmware/src"
if rg -n 'A5A5A5A5|5A5A5A5A|01234567|89ABCDEF|0xA5|0x5A|0x67|0xEF' "$production"; then
  echo 'ERROR: test PSK literal found in production source' >&2
  exit 1
fi
echo 'PASS: production source contains no test PSK literal'
