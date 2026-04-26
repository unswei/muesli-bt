#!/usr/bin/env python3
"""Check local links in README.md."""

from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
README = ROOT / "README.md"
LINK_RE = re.compile(r"\[[^\]]+\]\(([^)]+)\)")


def strip_target(target: str) -> str:
    target = target.split("#", 1)[0]
    target = target.split("?", 1)[0]
    return target.strip()


def main() -> int:
    errors: list[str] = []
    for line_no, line in enumerate(README.read_text(encoding="utf-8").splitlines(), 1):
        for match in LINK_RE.finditer(line):
            target = strip_target(match.group(1))
            if not target or target.startswith(("http://", "https://", "mailto:")):
                continue
            if " " in target and not target.startswith("<"):
                errors.append(f"{README}:{line_no}: unescaped space in link target {target!r}")
                continue
            target = target.strip("<>")
            path = (ROOT / target).resolve()
            try:
                path.relative_to(ROOT)
            except ValueError:
                errors.append(f"{README}:{line_no}: link escapes repository: {target}")
                continue
            if not path.exists():
                errors.append(f"{README}:{line_no}: missing link target: {target}")
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print("README links ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
