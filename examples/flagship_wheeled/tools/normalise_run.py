#!/usr/bin/env python3

"""Normalise flagship backend logs into one shared comparison schema."""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path
from typing import Any, Dict, Iterable, List, Optional


SCHEMA_VERSION = "flagship.normalised_run.v1"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Normalise a backend-specific flagship run into the shared comparison shape."
    )
    parser.add_argument("input", help="Backend-specific JSONL log artefact.")
    parser.add_argument("--backend", required=True, choices=("webots", "pybullet"))
    parser.add_argument("--output", help="Optional JSON output path. Defaults to stdout.")
    return parser.parse_args()


def load_jsonl(path: Path) -> List[Dict[str, Any]]:
    rows: List[Dict[str, Any]] = []
    with path.open(encoding="utf-8") as handle:
        for line_no, raw_line in enumerate(handle, start=1):
            line = raw_line.strip()
            if not line:
                continue
            try:
                payload = json.loads(line)
            except json.JSONDecodeError as exc:
                raise ValueError(f"{path}:{line_no}: invalid JSON: {exc}") from exc
            if not isinstance(payload, dict):
                raise ValueError(f"{path}:{line_no}: expected object per line")
            rows.append(payload)
    if not rows:
        raise ValueError(f"{path}: no records found")
    return rows


def safe_float(value: Any, *, default: float = 0.0) -> float:
    try:
        out = float(value)
    except (TypeError, ValueError):
        return default
    if not math.isfinite(out):
        return default
    return out


def safe_int(value: Any, *, default: int = 0) -> int:
    try:
        return int(value)
    except (TypeError, ValueError):
        return default


def maybe_shared_action(payload: Any) -> Optional[Dict[str, float]]:
    if not isinstance(payload, dict):
        return None
    if "linear_x" in payload or "angular_z" in payload:
        return {
            "linear_x": safe_float(payload.get("linear_x")),
            "angular_z": safe_float(payload.get("angular_z")),
        }
    u = payload.get("u")
    if isinstance(u, list) and len(u) >= 2:
        return {
            "linear_x": safe_float(u[0]),
            "angular_z": safe_float(u[1]),
        }
    return None


def branch_from_pybullet(record: Dict[str, Any]) -> str:
    bt = record.get("bt")
    if not isinstance(bt, dict):
        return "unknown"
    node_status = bt.get("node_status")
    if isinstance(node_status, dict):
        branch = node_status.get("branch")
        if isinstance(branch, str) and branch:
            return branch
    active_path = bt.get("active_path")
    if isinstance(active_path, list) and active_path:
        tail = active_path[-1]
        if isinstance(tail, str):
            return tail
    return "unknown"


def branch_from_webots(record: Dict[str, Any]) -> str:
    bt = record.get("bt")
    if not isinstance(bt, dict):
        return "unknown"
    status_by_node = bt.get("status_by_node")
    if isinstance(status_by_node, dict):
        branch = status_by_node.get("branch")
        if isinstance(branch, str) and branch:
            return branch
    active_path = bt.get("active_path")
    if isinstance(active_path, list) and active_path:
        tail = active_path[-1]
        if isinstance(tail, str):
            return tail
    return "unknown"


def normalise_pybullet_record(record: Dict[str, Any]) -> Dict[str, Any]:
    shared_action = maybe_shared_action(record.get("shared_action"))
    planner = record.get("planner")
    planner_used = isinstance(planner, dict)
    bt = record.get("bt") if isinstance(record.get("bt"), dict) else {}
    return {
        "tick_index": safe_int(record.get("tick_index"), default=0),
        "time_s": safe_float(record.get("sim_time_s")),
        "goal_dist": safe_float(record.get("distance_to_goal"), default=math.inf),
        "goal_reached": bool(record.get("goal_reached")),
        "collision_imminent": bool(record.get("collision_imminent")),
        "branch": branch_from_pybullet(record),
        "active_path": bt.get("active_path", []),
        "shared_action": shared_action,
        "planner_used": planner_used,
    }


def normalise_webots_record(record: Dict[str, Any]) -> Dict[str, Any]:
    obs = record.get("obs") if isinstance(record.get("obs"), dict) else {}
    on_tick = record.get("on_tick") if isinstance(record.get("on_tick"), dict) else {}
    planner = on_tick.get("planner") if isinstance(on_tick.get("planner"), dict) else record.get("planner")
    planner_used = bool(isinstance(planner, dict) and planner.get("used"))
    bt = on_tick.get("bt") if isinstance(on_tick.get("bt"), dict) else record.get("bt")
    shared_action = maybe_shared_action(on_tick.get("shared_action"))
    return {
        "tick_index": safe_int(record.get("tick"), default=0),
        "time_s": safe_float(record.get("t_ms")) / 1000.0,
        "goal_dist": safe_float(obs.get("goal_dist"), default=math.inf),
        "goal_reached": bool(on_tick.get("done")),
        "collision_imminent": False,
        "branch": branch_from_webots({"bt": bt}),
        "active_path": bt.get("active_path", []) if isinstance(bt, dict) else [],
        "shared_action": shared_action,
        "planner_used": planner_used,
    }


def normalise_records(rows: Iterable[Dict[str, Any]], backend: str) -> List[Dict[str, Any]]:
    out: List[Dict[str, Any]] = []
    for record in rows:
        if backend == "pybullet":
            out.append(normalise_pybullet_record(record))
        elif backend == "webots":
            out.append(normalise_webots_record(record))
        else:
            raise ValueError(f"backend not yet supported: {backend}")
    return out


def run_id_from_rows(rows: List[Dict[str, Any]], backend: str, path: Path) -> str:
    if backend == "pybullet":
        run_id = rows[0].get("run_id")
        if isinstance(run_id, str) and run_id:
            return run_id
    return path.stem


def make_summary(ticks: List[Dict[str, Any]]) -> Dict[str, Any]:
    initial_goal_dist = safe_float(ticks[0].get("goal_dist"), default=math.inf)
    final_goal_dist = safe_float(ticks[-1].get("goal_dist"), default=math.inf)
    min_goal_dist = min(safe_float(tick.get("goal_dist"), default=math.inf) for tick in ticks)
    planner_used_ticks = sum(1 for tick in ticks if tick.get("planner_used"))
    return {
        "tick_count": len(ticks),
        "duration_s": safe_float(ticks[-1].get("time_s")),
        "initial_goal_dist": initial_goal_dist,
        "final_goal_dist": final_goal_dist,
        "min_goal_dist": min_goal_dist,
        "planner_used_ticks": planner_used_ticks,
    }


def final_status(ticks: List[Dict[str, Any]]) -> str:
    if ticks[-1].get("goal_reached"):
        return "success"
    return "incomplete"


def normalise_run(path: Path, backend: str) -> Dict[str, Any]:
    rows = load_jsonl(path)
    ticks = normalise_records(rows, backend)
    summary = make_summary(ticks)
    final_tick = ticks[-1]
    return {
        "schema_version": SCHEMA_VERSION,
        "backend": backend,
        "run_id": run_id_from_rows(rows, backend, path),
        "source_path": str(path.resolve()),
        "summary": summary,
        "final": {
            "status": final_status(ticks),
            "tick_index": final_tick.get("tick_index"),
            "goal_reached": bool(final_tick.get("goal_reached")),
            "goal_dist": safe_float(final_tick.get("goal_dist"), default=math.inf),
            "branch": final_tick.get("branch", "unknown"),
        },
        "ticks": ticks,
    }


def main() -> int:
    args = parse_args()
    payload = normalise_run(Path(args.input), args.backend)
    text = json.dumps(payload, indent=2, ensure_ascii=True)
    if args.output:
        output_path = Path(args.output)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(text + "\n", encoding="utf-8")
    else:
        print(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
