#!/usr/bin/env python3
"""Check visible status blocks on future-facing documentation pages."""

from __future__ import annotations

import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
REQUIRED = [
    "docs/integration/host-capability-bundles.md",
    "docs/integration/cap-motion-v1.md",
    "docs/integration/cap-perception-scene-v1.md",
    "docs/bt/vla-integration.md",
    "docs/examples/isaac-h1-ros2-demo.md",
    "docs/examples/isaac-wheeled-ros2-showcase.md",
    "docs/integration/ros2-backend-scope.md",
    "docs/integration/ros2-tutorial.md",
    "docs/internals/runtime-performance.md",
]
TERMS = ("released", "experimental", "contract-only", "planned")


def main() -> int:
    errors: list[str] = []
    for rel in REQUIRED:
        path = ROOT / rel
        text = path.read_text(encoding="utf-8")
        head = "\n".join(text.splitlines()[:12]).lower()
        if "status:" not in head:
            errors.append(f"{path}: missing visible status block near top")
        if not any(term in head for term in TERMS):
            errors.append(f"{path}: status block does not use standard vocabulary")
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print("status labels ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
