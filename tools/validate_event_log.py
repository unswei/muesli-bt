#!/usr/bin/env python3
"""Backward-compatible wrapper for tools/validate_log.py."""

from __future__ import annotations

import pathlib
import sys

_TOOLS_DIR = pathlib.Path(__file__).resolve().parent
if str(_TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(_TOOLS_DIR))

from validate_log import main  # noqa: E402


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
