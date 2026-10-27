"""FLEX 1.0 — Desktop Application Launcher.

Launch command:
    python main.py

This launches the polished Tkinter desktop interface, connecting the
space-themed GUI to the native C core (`flex_bridge.exe`) and local Ollama.
"""

from __future__ import annotations

import os
import sys
from pathlib import Path

# Ensure repo root is on sys.path
REPO_ROOT = Path(__file__).resolve().parent
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

from backend.app import main

if __name__ == "__main__":
    main()
