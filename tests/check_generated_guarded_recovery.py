#!/usr/bin/env python3

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
GENERATOR = REPO_ROOT / "tools" / "generate_guarded_recovery_subtree.py"
VALIDATOR = REPO_ROOT / "tools" / "validate_generated_bt_fragment.py"
LOG_VALIDATOR = REPO_ROOT / "tools" / "validate_log.py"
TRACE_VALIDATOR = REPO_ROOT / "tools" / "validate_trace.py"
SCHEMA = REPO_ROOT / "schemas" / "event_log" / "v1" / "mbt.evt.v1.schema.json"
FIXTURE_ROOT = REPO_ROOT / "fixtures" / "dsl" / "generated_guarded_recovery"
ACCEPTED = FIXTURE_ROOT / "accepted-blocked-path"
CONTEXT = FIXTURE_ROOT / "context-blocked-path.json"


def run_cli(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, *args],
        cwd=REPO_ROOT,
        check=False,
        capture_output=True,
        text=True,
    )


def assert_ok(completed: subprocess.CompletedProcess[str], context: str) -> None:
    if completed.returncode != 0:
        raise AssertionError(f"{context} failed:\nstdout={completed.stdout}\nstderr={completed.stderr}")


def read_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def event_types(path: Path) -> list[str]:
    return [json.loads(line)["type"] for line in path.read_text(encoding="utf-8").splitlines() if line.strip()]


def jsonschema_available() -> bool:
    completed = subprocess.run(
        [sys.executable, "-c", "import jsonschema"],
        cwd=REPO_ROOT,
        check=False,
        capture_output=True,
        text=True,
    )
    return completed.returncode == 0


def assert_event_types_in_schema(types: list[str]) -> None:
    schema = read_json(SCHEMA)
    allowed = set(schema["properties"]["type"]["enum"])
    missing = [event_type for event_type in types if event_type not in allowed]
    if missing:
        raise AssertionError(f"event types missing from schema enum: {missing}")


def main() -> int:
    completed = run_cli(str(VALIDATOR), str(FIXTURE_ROOT))
    assert_ok(completed, "generated guarded recovery fixture validation")

    completed = run_cli(str(TRACE_VALIDATOR), "check", str(ACCEPTED / "events.jsonl"))
    assert_ok(completed, "generated guarded recovery trace validation")

    replay = read_json(ACCEPTED / "replay_report.json")
    if replay.get("passed") is not True:
        raise AssertionError("replay report should pass")
    if not all(replay.get("checks", {}).values()):
        raise AssertionError("all replay report checks should be true")

    expected_types = [
        "run_start",
        "dsl_fragment_generated",
        "dsl_fragment_normalised",
        "dsl_fragment_validation_ok",
        "dsl_fragment_compiled",
        "tick_begin",
        "subtree_install_requested",
        "subtree_installed",
        "tick_end",
        "subtree_replay_loaded",
        "run_end",
    ]
    actual_types = event_types(ACCEPTED / "events.jsonl")
    if actual_types != expected_types:
        raise AssertionError(f"generated lifecycle event order mismatch: {actual_types}")
    assert_event_types_in_schema(actual_types)

    if jsonschema_available():
        completed = run_cli(str(LOG_VALIDATOR), str(ACCEPTED / "events.jsonl"))
        assert_ok(completed, "generated guarded recovery event schema validation")

    validation = read_json(ACCEPTED / "validation_report.json")
    if validation.get("fallback_policy") != "required_and_present":
        raise AssertionError("accepted fragment should have required fallback policy")
    if validation.get("long_running_nodes") != ["plan-action"]:
        raise AssertionError("accepted fragment should include exactly one plan-action")

    with tempfile.TemporaryDirectory() as tmp_dir:
        generated = Path(tmp_dir) / "accepted-blocked-path"
        completed = run_cli(str(GENERATOR), "--context", str(CONTEXT), "--out-dir", str(generated))
        assert_ok(completed, "regenerating guarded recovery fixture")
        for name in ("canonical_fragment.lisp", "validation_report.json", "events.jsonl", "replay_report.json"):
            if (generated / name).read_text(encoding="utf-8") != (ACCEPTED / name).read_text(encoding="utf-8"):
                raise AssertionError(f"regenerated {name} does not match checked-in fixture")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
