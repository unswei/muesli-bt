#!/usr/bin/env python3

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
import tempfile
from collections import Counter
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[1]
FIXTURE_ROOT = REPO_ROOT / "fixtures" / "dsl" / "generated_guarded_recovery"
FLAGSHIP_ACCEPTED = FIXTURE_ROOT / "flagship-recovery-accepted"
FLAGSHIP_CONTEXT = FIXTURE_ROOT / "context-flagship-blocked-recovery.json"
FLAGSHIP_PROPOSAL_ACCEPTED = FIXTURE_ROOT / "proposal-flagship-accepted"
FLAGSHIP_PROPOSAL_REJECTED = FIXTURE_ROOT / "proposal-flagship-rejected-contract"
FLAGSHIP_SOURCE = REPO_ROOT / "examples" / "flagship_wheeled" / "lisp" / "bt_goal_flagship_generated_recovery.lisp"
CHECKED_MANIFEST = FLAGSHIP_ACCEPTED / "evidence_manifest.json"
GENERATOR = REPO_ROOT / "tools" / "generate_guarded_recovery_subtree.py"
VALIDATOR = REPO_ROOT / "tools" / "validate_generated_bt_fragment.py"
LOG_VALIDATOR = REPO_ROOT / "tools" / "validate_log.py"
EVENT_SCHEMA = REPO_ROOT / "schemas" / "event_log" / "v1" / "mbt.evt.v1.schema.json"


EXPECTED_EVENT_TYPES = [
    "run_start",
    "dsl_fragment_generated",
    "dsl_fragment_normalised",
    "dsl_fragment_validation_ok",
    "dsl_fragment_compiled",
    "tick_begin",
    "subtree_install_requested",
    "subtree_installed",
    "tick_end",
    "tick_begin",
    "subtree_rollback_requested",
    "subtree_rolled_back",
    "tick_end",
    "subtree_replay_loaded",
    "run_end",
]


def repo_path(path: Path) -> str:
    return path.relative_to(REPO_ROOT).as_posix()


def read_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(65536), b""):
            digest.update(chunk)
    return f"sha256:{digest.hexdigest()}"


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


def jsonschema_available() -> bool:
    completed = subprocess.run(
        [sys.executable, "-c", "import jsonschema"],
        cwd=REPO_ROOT,
        check=False,
        capture_output=True,
        text=True,
    )
    return completed.returncode == 0


def proposal_result(path: Path) -> dict[str, Any]:
    completed = run_cli(str(VALIDATOR), str(path), "--json")
    assert_ok(completed, f"validating {repo_path(path)}")
    payload = json.loads(completed.stdout)
    results = payload.get("results", [])
    if len(results) != 1:
        raise AssertionError(f"{repo_path(path)} should produce one validation result")
    return results[0]


def load_events(path: Path) -> list[dict[str, Any]]:
    return [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines() if line.strip()]


def assert_event_schema_shape(events: list[dict[str, Any]]) -> None:
    actual_types = [record.get("type") for record in events]
    if actual_types != EXPECTED_EVENT_TYPES:
        raise AssertionError(f"flagship generated-recovery event order mismatch: {actual_types}")

    schema = read_json(EVENT_SCHEMA)
    allowed_types = set(schema["properties"]["type"]["enum"])
    missing = [event_type for event_type in actual_types if event_type not in allowed_types]
    if missing:
        raise AssertionError(f"event types missing from mbt.evt.v1 schema enum: {missing}")

    seqs = [record.get("seq") for record in events]
    if seqs != list(range(1, len(events) + 1)):
        raise AssertionError(f"event sequence should be contiguous from 1: {seqs}")

    run_ids = {record.get("run_id") for record in events}
    if run_ids != {"fixture-flagship-generated-recovery"}:
        raise AssertionError(f"unexpected run ids: {sorted(run_ids)}")


def assert_flagship_reports() -> tuple[dict[str, Any], dict[str, Any]]:
    replay = read_json(FLAGSHIP_ACCEPTED / "replay_report.json")
    if replay.get("passed") is not True:
        raise AssertionError("flagship replay report should pass")
    if not all(replay.get("checks", {}).values()):
        raise AssertionError("all flagship replay checks should be true")

    comparison = read_json(FLAGSHIP_ACCEPTED / "fixed_vs_generated_report.json")
    if comparison.get("passed") is not True:
        raise AssertionError("flagship fixed-versus-generated comparison should pass")
    if not all(comparison.get("checks", {}).values()):
        raise AssertionError("all flagship comparison checks should be true")
    return replay, comparison


def validate_event_log(path: Path) -> str:
    if jsonschema_available():
        completed = run_cli(str(LOG_VALIDATOR), str(path))
        assert_ok(completed, "validating flagship generated-recovery event log")
    return "schema_enum_checked"


def assert_regenerates() -> None:
    with tempfile.TemporaryDirectory() as tmp_dir:
        generated = Path(tmp_dir) / "flagship-recovery-accepted"
        completed = run_cli(str(GENERATOR), "--context", str(FLAGSHIP_CONTEXT), "--out-dir", str(generated))
        assert_ok(completed, "regenerating flagship generated-recovery evidence")
        for name in (
            "fragment.lisp",
            "canonical_fragment.lisp",
            "validation_report.json",
            "events.jsonl",
            "replay_report.json",
            "fixed_vs_generated_report.json",
        ):
            if (generated / name).read_text(encoding="utf-8") != (FLAGSHIP_ACCEPTED / name).read_text(
                encoding="utf-8"
            ):
                raise AssertionError(f"regenerated flagship {name} does not match checked-in fixture")


def build_manifest() -> dict[str, Any]:
    accepted = proposal_result(FLAGSHIP_PROPOSAL_ACCEPTED)
    rejected = proposal_result(FLAGSHIP_PROPOSAL_REJECTED)

    if accepted.get("status") != "accepted" or accepted.get("reason_code") != "accepted":
        raise AssertionError("flagship accepted proposal should be accepted")
    if accepted.get("fragment_contract") != "guarded-recovery.v1":
        raise AssertionError("flagship accepted proposal should use guarded-recovery.v1")
    if accepted.get("slot") != "recovery-policy":
        raise AssertionError("flagship accepted proposal should target recovery-policy")
    if accepted.get("dry_run_report", {}).get("passed") is not True:
        raise AssertionError("flagship accepted proposal dry-run should pass")
    if accepted.get("host_reached") is not False:
        raise AssertionError("accepted proposal validation should not reach host execution")

    if rejected.get("status") != "rejected" or rejected.get("reason_code") != "excessive_depth":
        raise AssertionError("flagship rejected proposal should fail with excessive_depth")
    if rejected.get("field_path") != "fragment":
        raise AssertionError("flagship rejected proposal should report field_path=fragment")
    if rejected.get("host_reached") is not False:
        raise AssertionError("rejected proposal should not reach host execution")

    replay, comparison = assert_flagship_reports()
    events_path = FLAGSHIP_ACCEPTED / "events.jsonl"
    events = load_events(events_path)
    assert_event_schema_shape(events)
    event_validation = validate_event_log(events_path)
    assert_regenerates()

    counts = Counter(record["type"] for record in events)
    install_event = next(record for record in events if record["type"] == "subtree_installed")
    rollback_event = next(record for record in events if record["type"] == "subtree_rolled_back")
    validation_report = read_json(FLAGSHIP_ACCEPTED / "validation_report.json")

    artefact_paths = {
        "bt_source": FLAGSHIP_SOURCE,
        "context": FLAGSHIP_CONTEXT,
        "fragment": FLAGSHIP_ACCEPTED / "fragment.lisp",
        "canonical_fragment": FLAGSHIP_ACCEPTED / "canonical_fragment.lisp",
        "validation_report": FLAGSHIP_ACCEPTED / "validation_report.json",
        "events": events_path,
        "replay_report": FLAGSHIP_ACCEPTED / "replay_report.json",
        "comparison_report": FLAGSHIP_ACCEPTED / "fixed_vs_generated_report.json",
        "accepted_proposal": FLAGSHIP_PROPOSAL_ACCEPTED / "proposal.json",
        "rejected_proposal": FLAGSHIP_PROPOSAL_REJECTED / "proposal.json",
        "rejected_contract": FLAGSHIP_PROPOSAL_REJECTED / "fragment_contracts.json",
    }

    return {
        "schema_version": "flagship_generated_recovery_evidence_manifest.v1",
        "status": "experimental",
        "variant": "wheeled-goal-flagship-generated-recovery",
        "canonical_baseline_promoted": False,
        "wrappers_promoted": False,
        "slot": "recovery-policy",
        "fragment_contract": "guarded-recovery.v1",
        "install_mode": "at_tick_boundary",
        "fallback": "safe-stop",
        "validation": {
            "accepted_status": accepted["status"],
            "accepted_reason_code": accepted["reason_code"],
            "rejected_status": rejected["status"],
            "rejected_reason_code": rejected["reason_code"],
            "rejected_field_path": rejected["field_path"],
            "event_log_schema": "mbt.evt.v1",
            "event_log_validation": event_validation,
            "regeneration": "passed",
        },
        "proposals": {
            "accepted": {
                "path": repo_path(FLAGSHIP_PROPOSAL_ACCEPTED / "proposal.json"),
                "proposal_id": accepted["proposal_id"],
                "status": accepted["status"],
                "reason_code": accepted["reason_code"],
                "canonical_dsl_hash": accepted["canonical_dsl_hash"],
                "source_hash": accepted["source_hash"],
                "host_reached": accepted["host_reached"],
            },
            "rejected_contract": {
                "path": repo_path(FLAGSHIP_PROPOSAL_REJECTED / "proposal.json"),
                "proposal_id": rejected["proposal_id"],
                "status": rejected["status"],
                "reason_code": rejected["reason_code"],
                "field_path": rejected["field_path"],
                "host_reached": rejected["host_reached"],
            },
        },
        "hashes": {
            "source_hash": accepted["source_hash"],
            "canonical_dsl_hash": accepted["canonical_dsl_hash"],
            "previous_subtree_hash": accepted["rollback_handle"]["previous_subtree_hash"],
            "installed_subtree_hash": install_event["data"]["new_subtree_hash"],
            "restored_subtree_hash": rollback_event["data"]["restored_subtree_hash"],
        },
        "events": {
            "path": repo_path(events_path),
            "run_id": events[0]["run_id"],
            "sha256": sha256_file(events_path),
            "count": len(events),
            "types": EXPECTED_EVENT_TYPES,
            "type_counts": dict(sorted(counts.items())),
        },
        "replay": {
            "path": repo_path(FLAGSHIP_ACCEPTED / "replay_report.json"),
            "sha256": sha256_file(FLAGSHIP_ACCEPTED / "replay_report.json"),
            "passed": replay["passed"],
            "checks": replay["checks"],
        },
        "comparison": {
            "path": repo_path(FLAGSHIP_ACCEPTED / "fixed_vs_generated_report.json"),
            "sha256": sha256_file(FLAGSHIP_ACCEPTED / "fixed_vs_generated_report.json"),
            "passed": comparison["passed"],
            "checks": comparison["checks"],
        },
        "validation_report": {
            "path": repo_path(FLAGSHIP_ACCEPTED / "validation_report.json"),
            "sha256": sha256_file(FLAGSHIP_ACCEPTED / "validation_report.json"),
            "canonical_dsl_hash": validation_report["canonical_dsl_hash"],
            "fallback_policy": validation_report["fallback_policy"],
            "node_count": validation_report["node_count"],
        },
        "artefacts": {
            name: {"path": repo_path(path), "sha256": sha256_file(path)} for name, path in sorted(artefact_paths.items())
        },
        "promotion_boundary": {
            "shared_flagship_baseline_unchanged": True,
            "simulator_wrappers_unchanged": True,
            "ros2_nav2_dependency": False,
            "physical_robot_dependency": False,
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Verify or write the flagship generated-recovery evidence manifest.")
    parser.add_argument(
        "--check",
        action="store_true",
        help="Compare the generated manifest with the checked-in fixture manifest.",
    )
    parser.add_argument(
        "--write-manifest",
        type=Path,
        help="Write the generated manifest to this path.",
    )
    args = parser.parse_args()

    if not args.check and args.write_manifest is None:
        args.check = True

    manifest = build_manifest()

    if args.check:
        expected = read_json(CHECKED_MANIFEST)
        if manifest != expected:
            raise AssertionError(
                "flagship generated-recovery evidence manifest drifted; "
                f"regenerate with: {sys.executable} {repo_path(Path(__file__))} --write-manifest {repo_path(CHECKED_MANIFEST)}"
            )

    if args.write_manifest is not None:
        write_json(args.write_manifest, manifest)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
