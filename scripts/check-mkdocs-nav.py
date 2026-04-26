#!/usr/bin/env python3
"""Check that MkDocs navigation targets exist and core visitor pages are present."""

from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DOCS = ROOT / "docs"
MKDOCS = ROOT / "mkdocs.yml"
TARGET_RE = re.compile(r":\s+([^:\n]+\.md)\s*$")
REQUIRED = {
    "getting-oriented/choose-your-path.md",
    "getting-started-10min.md",
    "getting-oriented/why-not-btcpp.md",
    "getting-oriented/observability-first.md",
    "tutorials/runtime-contract-in-practice.md",
    "evidence/index.md",
    "known-limitations.md",
    "contributing/docs-style-guide.md",
}


def main() -> int:
    text = MKDOCS.read_text(encoding="utf-8")
    errors: list[str] = []
    targets: set[str] = set()
    for line_no, line in enumerate(text.splitlines(), 1):
        match = TARGET_RE.search(line)
        if not match:
            continue
        target = match.group(1).strip()
        targets.add(target)
        if target.startswith("../"):
            errors.append(f"{MKDOCS}:{line_no}: nav target must stay inside docs/: {target}")
            continue
        if not (DOCS / target).exists():
            errors.append(f"{MKDOCS}:{line_no}: missing nav target: {target}")
    missing_required = sorted(REQUIRED - targets)
    for target in missing_required:
        errors.append(f"{MKDOCS}: missing required visitor page in nav: {target}")
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print("mkdocs nav ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
