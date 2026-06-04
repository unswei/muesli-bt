#!/usr/bin/env python3
"""Reject internal planning phrases from user-facing docs."""

from __future__ import annotations

import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DOCS = ROOT / "docs"
FILES = [ROOT / "README.md", *sorted(DOCS.rglob("*.md"))]

ALLOWED_PREFIXES = (
    "docs/evidence/",
    "docs/internals/",
    "docs/project/",
    "docs/releases/",
)
ALLOWED_FILES = {
    "docs/roadmap-to-1.0.md",
    "docs/limitations-roadmap.md",
    "docs/known-limitations.md",
    "docs/contributing/docs-style-guide.md",
    "docs/todo.md",
}

BANNED_PHRASES = (
    "current slice",
    "not yet good enough",
    "public `v1.0.0` direction",
    "public v1.0.0 direction",
    "useful as a scaffold",
)


def rel(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


def is_allowed(path: Path) -> bool:
    path_rel = rel(path)
    return path_rel in ALLOWED_FILES or any(path_rel.startswith(prefix) for prefix in ALLOWED_PREFIXES)


def main() -> int:
    errors: list[str] = []
    for path in FILES:
        if is_allowed(path):
            continue
        text = path.read_text(encoding="utf-8").lower()
        for phrase in BANNED_PHRASES:
            if phrase.lower() in text:
                errors.append(f"{rel(path)}: remove internal planning phrase: {phrase!r}")
    if errors:
        for error in errors:
            print(f"error: {error}")
        return 1
    print("docs user prose ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
