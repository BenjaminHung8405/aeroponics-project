#!/usr/bin/env python3
"""
AGU-Aeroponics Control Entry Point for CP2102.
Bridges to tools/test_agu_rf_e2e.py to provide complete control on macOS.
"""

import os
import sys

# Ensure repository root is on sys.path
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(SCRIPT_DIR)
if PROJECT_ROOT not in sys.path:
    sys.path.insert(0, PROJECT_ROOT)

from tools.test_agu_rf_e2e import main

if __name__ == "__main__":
    main()
