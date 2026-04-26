#!/usr/bin/env python3
"""Lightweight local Markdown link checker for docs and root project files."""

from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FILES = [ROOT / "README.md", *sorted((ROOT / "docs").rglob("*.md"))]
LINK_RE = re.compile(r"(?<!!)\[[^\]]+\]\(([^)]+)\)")


def clean(target: str) -> str:
    return target.strip().split("#", 1)[0].split("?", 1)[0].strip("<>")


def should_skip(target: str) -> bool:
    return (
        not target
        or target.startswith(("http://", "https://", "mailto:"))
        or target.startswith("#")
    )


def main() -> int:
    errors: list[str] = []
    for md in FILES:
        text = md.read_text(encoding="utf-8")
        for line_no, line in enumerate(text.splitlines(), 1):
            for match in LINK_RE.finditer(line):
                target = clean(match.group(1))
                if should_skip(target):
                    continue
                path = (md.parent / target).resolve()
                try:
                    path.relative_to(ROOT)
                except ValueError:
                    # MkDocs pages link to the repository README/bench from docs.
                    if not path.exists():
                        errors.append(f"{md}:{line_no}: missing external local link: {target}")
                    continue
                if not path.exists():
                    errors.append(f"{md}:{line_no}: missing link target: {target}")
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print("docs links ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
