#!/usr/bin/env python3
from __future__ import annotations

import json
from pathlib import Path
from typing import Any, Dict, List


JsonMap = Dict[str, Any]


def load_jsonl(path: Path) -> List[JsonMap]:
    rows: List[JsonMap] = []
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line:
            continue
        rows.append(json.loads(line))
    return rows


def ensure_dir(path: Path) -> Path:
    path.mkdir(parents=True, exist_ok=True)
    return path


def tick_of(row: JsonMap, fallback: int) -> int:
    value = row.get("tick", fallback)
    try:
        return int(value)
    except (TypeError, ValueError):
        return fallback
