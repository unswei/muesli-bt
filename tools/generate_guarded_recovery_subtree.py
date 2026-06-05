#!/usr/bin/env python3
"""Generate deterministic guarded recovery subtree evidence artefacts.

This is the first narrow generated-subtree slice. It produces Lisp BT data from
checked-in context, validates it with the generated-fragment validator, and
writes canonical lifecycle events that can be schema and trace checked.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

from validate_generated_bt_fragment import ValidationError, validate_fixture_dir, validate_fragment


SCHEMA = "mbt.evt.v1"
CONTRACT_VERSION = "1.0.0"
BASE_UNIX_MS = 1735689606000
RUN_ID = "fixture-generated-guarded-recovery"
OLD_TREE_HASH = "fnv1a64:9999999999999999"


def require_context(context: dict[str, Any]) -> None:
    required = {
        "schema_version": "generated_guarded_recovery.context.v1",
        "scenario": "blocked_path",
        "generator": "deterministic-template-v1",
    }
    for key, expected in required.items():
        if context.get(key) != expected:
            raise RuntimeError(f"context {key} must be {expected!r}")
    if context.get("blocked_path") is not True:
        raise RuntimeError("context blocked_path must be true for this fixture")
    if context.get("observation_fresh") is not True:
        raise RuntimeError("context observation_fresh must be true for this fixture")


def fragment_from_context(context: dict[str, Any]) -> str:
    planner = str(context.get("planner", "mcts"))
    budget_ms = int(context.get("budget_ms", 20))
    work_max = int(context.get("work_max", 64))
    return f"""(reactive-sel
  (seq
    (cond blocked-path?)
    (cond observation-fresh?)
    (plan-action
      :name "recovery-turn"
      :planner :{planner}
      :budget_ms {budget_ms}
      :work_max {work_max}
      :state_key recovery-state
      :action_key recovery-action)
    (act execute-recovery-turn)
    (cond recovery-exit?))
  (act safe-stop))
"""


def event(seq: int, event_type: str, data: dict[str, Any], tick: int | None = None) -> dict[str, Any]:
    payload: dict[str, Any] = {
        "schema": SCHEMA,
        "contract_version": CONTRACT_VERSION,
        "type": event_type,
        "run_id": RUN_ID,
        "unix_ms": BASE_UNIX_MS + seq - 1,
        "seq": seq,
        "data": data,
    }
    if tick is not None:
        payload["tick"] = tick
    return payload


def make_events(validation: dict[str, Any], context: dict[str, Any]) -> list[dict[str, Any]]:
    source_hash = str(validation["source_hash"])
    canonical_hash = str(validation["canonical_dsl_hash"])
    common = {
        "fragment_id": "generated_guarded_recovery.blocked_path",
        "generator": context["generator"],
        "source_hash": source_hash,
        "canonical_dsl_hash": canonical_hash,
        "validation_rule_set": "generated-fragment-policy.v1",
    }
    return [
        event(
            1,
            "run_start",
            {
                "git_sha": "fixture",
                "host": {"name": "muesli-bt", "version": "0.8.0", "platform": "fixture"},
                "contract_version": CONTRACT_VERSION,
                "contract_id": "runtime-contract-v1.0.0",
                "tick_hz": 20.0,
                "tree_hash": OLD_TREE_HASH,
                "capabilities": {"reset": True},
            },
        ),
        event(
            2,
            "dsl_fragment_generated",
            {
                **common,
                "trigger": "blocked_path",
                "blocked_path": True,
                "observation_fresh": True,
            },
        ),
        event(
            3,
            "dsl_fragment_normalised",
            {
                **common,
                "canonical_dsl": validation["canonical_dsl"],
                "node_count": validation["node_count"],
            },
        ),
        event(
            4,
            "dsl_fragment_validation_ok",
            {
                **common,
                "status": "accepted",
                "callbacks": validation["callbacks"],
                "capabilities": validation["capabilities"],
                "long_running_nodes": validation["long_running_nodes"],
                "fallback_policy": validation["fallback_policy"],
            },
        ),
        event(
            5,
            "dsl_fragment_compiled",
            {
                **common,
                "status": "compiled",
                "tree_hash": canonical_hash,
                "compile_ms": 0.1,
            },
        ),
        event(6, "tick_begin", {"tick_budget_ms": 20.0}, tick=1),
        event(
            7,
            "subtree_install_requested",
            {
                **common,
                "old_subtree_hash": OLD_TREE_HASH,
                "new_subtree_hash": canonical_hash,
                "install_mode": "next_tick_boundary",
            },
            tick=1,
        ),
        event(
            8,
            "subtree_installed",
            {
                **common,
                "old_subtree_hash": OLD_TREE_HASH,
                "new_subtree_hash": canonical_hash,
                "install_tick": 1,
                "install_mode": "next_tick_boundary",
            },
            tick=1,
        ),
        event(9, "tick_end", {"root_status": "running", "tick_ms": 0.5, "tick_budget_ms": 20.0}, tick=1),
        event(10, "tick_begin", {"tick_budget_ms": 20.0}, tick=2),
        event(
            11,
            "subtree_rollback_requested",
            {
                **common,
                "slot": "recovery-policy",
                "old_subtree_hash": OLD_TREE_HASH,
                "current_subtree_hash": canonical_hash,
                "rollback_target_hash": OLD_TREE_HASH,
                "install_mode": "next_tick_boundary",
            },
            tick=2,
        ),
        event(
            12,
            "subtree_rolled_back",
            {
                **common,
                "slot": "recovery-policy",
                "previous_subtree_hash": canonical_hash,
                "restored_subtree_hash": OLD_TREE_HASH,
                "rollback_tick": 2,
                "install_mode": "next_tick_boundary",
            },
            tick=2,
        ),
        event(13, "tick_end", {"root_status": "running", "tick_ms": 0.4, "tick_budget_ms": 20.0}, tick=2),
        event(
            14,
            "subtree_replay_loaded",
            {
                **common,
                "source": "canonical_dsl_artifact",
                "replay_status": "loaded",
            },
        ),
        event(
            15,
            "run_end",
            {
                "status": "ok",
                "summary": {
                    "generated_fragments": 1,
                    "accepted_fragments": 1,
                    "rejected_fragments": 0,
                    "installed_subtrees": 1,
                    "rolled_back_subtrees": 1,
                },
            },
        ),
    ]


def write_json(path: Path, payload: Any) -> None:
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def write_jsonl(path: Path, events: list[dict[str, Any]]) -> None:
    path.write_text(
        "".join(json.dumps(event, sort_keys=True, separators=(",", ":")) + "\n" for event in events),
        encoding="utf-8",
    )


def generate(context_path: Path, out_dir: Path) -> dict[str, Any]:
    context = json.loads(context_path.read_text(encoding="utf-8"))
    require_context(context)
    out_dir.mkdir(parents=True, exist_ok=True)

    fragment_path = out_dir / "fragment.lisp"
    expected_path = out_dir / "expected.json"
    fragment_path.write_text(fragment_from_context(context), encoding="utf-8")

    validation = validate_fragment(fragment_path)
    expected = {
        "ok": True,
        "code": "accepted",
        "canonical_dsl": validation["canonical_dsl"],
        "canonical_dsl_hash": validation["canonical_dsl_hash"],
        "node_count": validation["node_count"],
        "long_running_nodes": validation["long_running_nodes"],
        "fallback_policy": validation["fallback_policy"],
    }
    write_json(expected_path, expected)
    validate_fixture_dir(out_dir)

    (out_dir / "canonical_fragment.lisp").write_text(str(validation["canonical_dsl"]) + "\n", encoding="utf-8")
    write_json(out_dir / "validation_report.json", validation)

    events = make_events(validation, context)
    write_jsonl(out_dir / "events.jsonl", events)
    replay_report = {
        "schema_version": "generated_guarded_recovery.replay_report.v1",
        "passed": True,
        "checks": {
            "fragment_generated": True,
            "normalised": True,
            "validation_accepted": True,
            "installed_at_tick_boundary": True,
            "rollback_restored_previous_hash": True,
            "replay_loaded_same_canonical_hash": True,
        },
        "canonical_dsl_hash": validation["canonical_dsl_hash"],
        "source_hash": validation["source_hash"],
        "events": {
            "path": "events.jsonl",
            "count": len(events),
            "required_types": [
                "dsl_fragment_generated",
                "dsl_fragment_normalised",
                "dsl_fragment_validation_ok",
                "dsl_fragment_compiled",
                "subtree_install_requested",
                "subtree_installed",
                "subtree_rollback_requested",
                "subtree_rolled_back",
                "subtree_replay_loaded",
            ],
        },
    }
    write_json(out_dir / "replay_report.json", replay_report)
    return {
        "out_dir": str(out_dir),
        "canonical_dsl_hash": validation["canonical_dsl_hash"],
        "source_hash": validation["source_hash"],
        "event_count": len(events),
    }


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="Generate guarded recovery subtree fixture artefacts.")
    parser.add_argument(
        "--context",
        default="fixtures/dsl/generated_guarded_recovery/context-blocked-path.json",
        help="Context JSON file.",
    )
    parser.add_argument(
        "--out-dir",
        default="fixtures/dsl/generated_guarded_recovery/accepted-blocked-path",
        help="Output fixture directory.",
    )
    parser.add_argument("--json", action="store_true", help="Emit machine-readable summary.")
    args = parser.parse_args(argv)

    try:
        summary = generate(Path(args.context), Path(args.out_dir))
    except (OSError, RuntimeError, ValidationError, json.JSONDecodeError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    if args.json:
        print(json.dumps(summary, indent=2, sort_keys=True))
    else:
        print(
            "generated guarded recovery subtree: "
            f"{summary['canonical_dsl_hash']} ({summary['event_count']} events)"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
