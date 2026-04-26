#!/usr/bin/env python3
"""Check that reference coverage bookkeeping exists and has no unchecked rows."""

from __future__ import annotations

import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
COVERAGE = ROOT / "docs/docs-coverage.md"
REFERENCE = ROOT / "docs/language/reference/index.md"


def main() -> int:
    errors: list[str] = []
    if not COVERAGE.exists():
        errors.append("missing docs/docs-coverage.md")
    if not REFERENCE.exists():
        errors.append("missing docs/language/reference/index.md")
    if COVERAGE.exists():
        text = COVERAGE.read_text(encoding="utf-8")
        if "- [ ]" in text:
            errors.append("docs/docs-coverage.md contains unchecked coverage items")
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print("reference coverage ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
