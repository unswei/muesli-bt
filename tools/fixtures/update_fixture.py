#!/usr/bin/env python3
"""Update deterministic runtime-contract fixture bundles with provenance."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import subprocess
from datetime import datetime, timezone


CONTRACT_VERSION = "1.0.0"
CONTRACT_ID = "runtime-contract-v1.0.0"
SCHEMA_NAME = "mbt.evt.v1"
SCHEMA_PATH = "schemas/event_log/v1/mbt.evt.v1.schema.json"
FIXTURE_ROOT = pathlib.Path("fixtures")


def event(
    *,
    event_type: str,
    run_id: str,
    unix_ms: int,
    seq: int,
    data: dict,
    tick: int | None = None,
) -> dict:
    out = {
        "schema": SCHEMA_NAME,
        "contract_version": CONTRACT_VERSION,
        "type": event_type,
        "run_id": run_id,
        "unix_ms": unix_ms,
        "seq": seq,
        "data": data,
    }
    if tick is not None:
        out["tick"] = tick
    return out


def git_sha() -> str:
    try:
        return (
            subprocess.check_output(["git", "rev-parse", "HEAD"], text=True, stderr=subprocess.DEVNULL).strip()
        )
    except Exception:
        return "unknown"


def git_commit_time_utc_iso8601() -> str:
    try:
        epoch = int(
            subprocess.check_output(
                ["git", "show", "-s", "--format=%ct", "HEAD"],
                text=True,
                stderr=subprocess.DEVNULL,
            ).strip()
        )
        return datetime.fromtimestamp(epoch, timezone.utc).isoformat().replace("+00:00", "Z")
    except Exception:
        return "unknown"


def fixture_definitions() -> dict[str, dict]:
    return {
        "budget-warning-case": {
            "config": {
                "scenario": "planner_decision_blocked_on_budget",
                "tick_budget_ms": 2,
                "deterministic_mode": True,
            },
            "seed": {"planner_seed": 424242, "vla_seed": 0, "run_seed": 11},
            "events": [
                event(
                    event_type="run_start",
                    run_id="fixture-budget-warning",
                    unix_ms=1735689605000,
                    seq=1,
                    data={
                        "git_sha": "fixture",
                        "host": {"name": "muesli-bt", "version": "0.1.0", "platform": "linux"},
                        "contract_version": CONTRACT_VERSION,
                        "contract_id": CONTRACT_ID,
                        "tick_hz": 50.0,
                        "tree_hash": "fnv1a64:0101010101010101",
                        "capabilities": {"reset": True},
                    },
                ),
                event(
                    event_type="tick_begin",
                    run_id="fixture-budget-warning",
                    unix_ms=1735689605001,
                    seq=2,
                    tick=1,
                    data={"tick_budget_ms": 2},
                ),
                event(
                    event_type="budget_warning",
                    run_id="fixture-budget-warning",
                    unix_ms=1735689605002,
                    seq=3,
                    tick=1,
                    data={
                        "decision_point": "planner_call_start",
                        "remaining_ms": -3.0,
                        "threshold_ms": 0.25,
                        "reason": "insufficient_budget",
                        "node_id": 2,
                    },
                ),
                event(
                    event_type="tick_end",
                    run_id="fixture-budget-warning",
                    unix_ms=1735689605003,
                    seq=4,
                    tick=1,
                    data={"root_status": "failure", "tick_ms": 5.0, "tick_budget_ms": 2.0},
                ),
            ],
            "expected_metrics": {
                "event_count": 4,
                "required_types": ["run_start", "tick_begin", "budget_warning", "tick_end"],
                "absent_types": ["planner_call_start"],
            },
        },
        "deadline-cancel-case": {
            "config": {
                "scenario": "deadline_overrun_cancels_active_async_jobs",
                "tick_budget_ms": 2,
                "deterministic_mode": True,
            },
            "seed": {"planner_seed": 424242, "vla_seed": 99, "run_seed": 12},
            "events": [
                event(
                    event_type="run_start",
                    run_id="fixture-deadline-cancel",
                    unix_ms=1735689605100,
                    seq=1,
                    data={
                        "git_sha": "fixture",
                        "host": {"name": "muesli-bt", "version": "0.1.0", "platform": "linux"},
                        "contract_version": CONTRACT_VERSION,
                        "contract_id": CONTRACT_ID,
                        "tick_hz": 50.0,
                        "tree_hash": "fnv1a64:0202020202020202",
                        "capabilities": {"reset": True},
                    },
                ),
                event(
                    event_type="tick_begin",
                    run_id="fixture-deadline-cancel",
                    unix_ms=1735689605101,
                    seq=2,
                    tick=2,
                    data={"tick_budget_ms": 2},
                ),
                event(
                    event_type="vla_submit",
                    run_id="fixture-deadline-cancel",
                    unix_ms=1735689605102,
                    seq=3,
                    tick=2,
                    data={"job_id": "17", "node_id": 4, "status": "submitted"},
                ),
                event(
                    event_type="deadline_exceeded",
                    run_id="fixture-deadline-cancel",
                    unix_ms=1735689605103,
                    seq=4,
                    tick=2,
                    data={"source": "tick_end", "tick_budget_ms": 2.0, "tick_elapsed_ms": 8.0},
                ),
                event(
                    event_type="async_cancel_requested",
                    run_id="fixture-deadline-cancel",
                    unix_ms=1735689605104,
                    seq=5,
                    tick=2,
                    data={"job_id": "17", "node_id": 4, "reason": "tick_deadline_exceeded"},
                ),
                event(
                    event_type="async_cancel_acknowledged",
                    run_id="fixture-deadline-cancel",
                    unix_ms=1735689605105,
                    seq=6,
                    tick=2,
                    data={
                        "job_id": "17",
                        "node_id": 4,
                        "accepted": True,
                        "reason": "tick_deadline_exceeded",
                    },
                ),
                event(
                    event_type="tick_end",
                    run_id="fixture-deadline-cancel",
                    unix_ms=1735689605106,
                    seq=7,
                    tick=2,
                    data={"root_status": "failure", "tick_ms": 8.0, "tick_budget_ms": 2.0},
                ),
            ],
            "expected_metrics": {
                "event_count": 7,
                "required_types": [
                    "run_start",
                    "tick_begin",
                    "vla_submit",
                    "deadline_exceeded",
                    "async_cancel_requested",
                    "async_cancel_acknowledged",
                    "tick_end",
                ],
            },
        },
        "determinism-replay-case": {
            "config": {
                "scenario": "deterministic_trace_replay",
                "tick_budget_ms": 20,
                "deterministic_mode": True,
            },
            "seed": {"planner_seed": 424242, "vla_seed": 0, "run_seed": 13},
            "events": [
                event(
                    event_type="run_start",
                    run_id="fixture-determinism-replay",
                    unix_ms=1735689605200,
                    seq=1,
                    data={
                        "git_sha": "fixture",
                        "host": {"name": "muesli-bt", "version": "0.1.0", "platform": "linux"},
                        "contract_version": CONTRACT_VERSION,
                        "contract_id": CONTRACT_ID,
                        "tick_hz": 20.0,
                        "tree_hash": "fnv1a64:0303030303030303",
                        "capabilities": {"reset": True},
                    },
                ),
                event(
                    event_type="tick_begin",
                    run_id="fixture-determinism-replay",
                    unix_ms=1735689605201,
                    seq=2,
                    tick=1,
                    data={"tick_budget_ms": 20},
                ),
                event(
                    event_type="node_enter",
                    run_id="fixture-determinism-replay",
                    unix_ms=1735689605202,
                    seq=3,
                    tick=1,
                    data={"node_id": 1},
                ),
                event(
                    event_type="planner_call_start",
                    run_id="fixture-determinism-replay",
                    unix_ms=1735689605203,
                    seq=4,
                    tick=1,
                    data={"node_id": 2, "planner": "mcts", "budget_ms": 12},
                ),
                event(
                    event_type="planner_call_end",
                    run_id="fixture-determinism-replay",
                    unix_ms=1735689605204,
                    seq=5,
                    tick=1,
                    data={"node_id": 2, "planner": "mcts", "status": "ok", "time_used_ms": 2.0, "work_done": 64},
                ),
                event(
                    event_type="node_exit",
                    run_id="fixture-determinism-replay",
                    unix_ms=1735689605205,
                    seq=6,
                    tick=1,
                    data={"node_id": 1, "status": "running", "dur_ms": 2.2},
                ),
                event(
                    event_type="tick_end",
                    run_id="fixture-determinism-replay",
                    unix_ms=1735689605206,
                    seq=7,
                    tick=1,
                    data={"root_status": "running", "tick_ms": 2.3, "tick_budget_ms": 20.0},
                ),
                event(
                    event_type="tick_begin",
                    run_id="fixture-determinism-replay",
                    unix_ms=1735689605207,
                    seq=8,
                    tick=2,
                    data={"tick_budget_ms": 20},
                ),
                event(
                    event_type="node_enter",
                    run_id="fixture-determinism-replay",
                    unix_ms=1735689605208,
                    seq=9,
                    tick=2,
                    data={"node_id": 1},
                ),
                event(
                    event_type="planner_call_start",
                    run_id="fixture-determinism-replay",
                    unix_ms=1735689605209,
                    seq=10,
                    tick=2,
                    data={"node_id": 2, "planner": "mcts", "budget_ms": 12},
                ),
                event(
                    event_type="planner_call_end",
                    run_id="fixture-determinism-replay",
                    unix_ms=1735689605210,
                    seq=11,
                    tick=2,
                    data={"node_id": 2, "planner": "mcts", "status": "ok", "time_used_ms": 2.0, "work_done": 64},
                ),
                event(
                    event_type="node_exit",
                    run_id="fixture-determinism-replay",
                    unix_ms=1735689605211,
                    seq=12,
                    tick=2,
                    data={"node_id": 1, "status": "success", "dur_ms": 2.1},
                ),
                event(
                    event_type="tick_end",
                    run_id="fixture-determinism-replay",
                    unix_ms=1735689605212,
                    seq=13,
                    tick=2,
                    data={"root_status": "success", "tick_ms": 2.2, "tick_budget_ms": 20.0},
                ),
            ],
            "expected_metrics": {
                "event_count": 13,
                "required_types": [
                    "run_start",
                    "planner_call_start",
                    "planner_call_end",
                    "node_enter",
                    "node_exit",
                    "tick_begin",
                    "tick_end",
                ],
                "node_status_sequence": ["1:running", "1:success"],
            },
        },
    }


def write_json(path: pathlib.Path, payload: dict) -> None:
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def write_events(path: pathlib.Path, events: list[dict]) -> str:
    lines = [json.dumps(ev, separators=(",", ":"), sort_keys=False) for ev in events]
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    digest = hashlib.sha256()
    digest.update(("\n".join(lines) + "\n").encode("utf-8"))
    return digest.hexdigest()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Update deterministic fixture bundles.")
    parser.add_argument(
        "--fixture",
        action="append",
        default=[],
        help="Optional fixture name filter. May be passed multiple times.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    definitions = fixture_definitions()
    selected = set(args.fixture)
    if selected:
        unknown = selected - set(definitions)
        if unknown:
            raise SystemExit(f"unknown fixture(s): {', '.join(sorted(unknown))}")

    FIXTURE_ROOT.mkdir(parents=True, exist_ok=True)
    sha = git_sha()
    commit_time = git_commit_time_utc_iso8601()

    for name, definition in definitions.items():
        if selected and name not in selected:
            continue

        fixture_dir = FIXTURE_ROOT / name
        fixture_dir.mkdir(parents=True, exist_ok=True)

        write_json(fixture_dir / "config.json", definition["config"])
        write_json(fixture_dir / "seed.json", definition["seed"])
        digest = write_events(fixture_dir / "events.jsonl", definition["events"])

        expected = dict(definition["expected_metrics"])
        expected["events_sha256"] = digest
        write_json(fixture_dir / "expected_metrics.json", expected)

        manifest = {
            "fixture_name": name,
            "schema": SCHEMA_NAME,
            "schema_path": SCHEMA_PATH,
            "contract_version": CONTRACT_VERSION,
            "contract_id": CONTRACT_ID,
            "generator": "tools/fixtures/update_fixture.py",
            "generated_from_commit_time_utc": commit_time,
            "git_sha": sha,
            "provenance_model": "deterministic-from-git",
        }
        write_json(fixture_dir / "manifest.json", manifest)

        print(f"updated {fixture_dir}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
