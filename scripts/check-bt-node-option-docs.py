#!/usr/bin/env python3
"""Check that public BT node option schemas are reflected in docs."""

from __future__ import annotations

import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCHEMA_DIR = ROOT / "schemas" / "bt_node_options" / "v1"
DOC_BY_NODE = {
    "plan-action": ROOT / "docs" / "planning" / "plan-action-node.md",
    "vla-request": ROOT / "docs" / "bt" / "vla-nodes.md",
    "vla-wait": ROOT / "docs" / "bt" / "vla-nodes.md",
    "vla-cancel": ROOT / "docs" / "bt" / "vla-nodes.md",
}


def main() -> int:
    errors: list[str] = []
    for node, doc_path in DOC_BY_NODE.items():
        schema_path = SCHEMA_DIR / f"{node}.schema.json"
        if not schema_path.is_file():
            errors.append(f"missing schema: {schema_path.relative_to(ROOT)}")
            continue
        if not doc_path.is_file():
            errors.append(f"missing docs: {doc_path.relative_to(ROOT)}")
            continue
        schema = json.loads(schema_path.read_text(encoding="utf-8"))
        doc = doc_path.read_text(encoding="utf-8")
        for option in sorted(schema.get("properties", {})):
            if option not in doc:
                errors.append(f"{doc_path.relative_to(ROOT)}: missing documented option {option} for {node}")
        schema_ref = schema_path.relative_to(ROOT).as_posix()
        if schema_ref not in doc:
            errors.append(f"{doc_path.relative_to(ROOT)}: missing schema reference {schema_ref}")
    if errors:
        for error in errors:
            print(f"error: {error}")
        return 1
    print("BT node option docs coverage ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
