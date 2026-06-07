#!/usr/bin/env python3

from __future__ import annotations

import argparse
import filecmp
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
FIXTURE_ROOT = REPO_ROOT / "fixtures" / "dsl" / "wheeled_flagship_nav_capability"
LOG_VALIDATOR = REPO_ROOT / "tools" / "validate_log.py"
DEFAULT_MUSLISP_CANDIDATES = [
    REPO_ROOT / "build" / "core-only" / "muslisp",
    REPO_ROOT / "build" / "dev" / "muslisp",
]
SCENARIOS = ["accepted_success", "rejected", "timeout", "cancel_on_collision"]
BASE_UNIX_MS = 1_761_000_000_000


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Verify or write wheeled flagship navigation-capability evidence.")
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true", help="regenerate and compare with checked-in artefacts")
    mode.add_argument("--write", action="store_true", help="regenerate checked-in artefacts")
    parser.add_argument("--muslisp", type=Path, help="path to a built muslisp executable")
    parser.add_argument("--out-dir", type=Path, help="output directory for --write")
    return parser.parse_args()


def find_muslisp(explicit: Path | None) -> Path:
    candidates = [explicit] if explicit is not None else DEFAULT_MUSLISP_CANDIDATES
    for candidate in candidates:
        if candidate is not None and candidate.is_file():
            return candidate
    searched = ", ".join(str(path) for path in candidates if path is not None)
    raise RuntimeError("muslisp executable is missing. Build core-only first. Searched: " + searched)


def run_checked(args: list[str], context: str) -> subprocess.CompletedProcess[str]:
    completed = subprocess.run(args, cwd=REPO_ROOT, check=False, capture_output=True, text=True)
    if completed.returncode != 0:
        raise RuntimeError(f"{context} failed:\nstdout={completed.stdout}\nstderr={completed.stderr}")
    return completed


def validate_event_log(path: Path) -> None:
    completed = subprocess.run(
        [sys.executable, str(LOG_VALIDATOR), str(path)],
        cwd=REPO_ROOT,
        check=False,
        capture_output=True,
        text=True,
    )
    if completed.returncode == 0:
        return
    if "jsonschema is required" not in completed.stderr:
        raise RuntimeError(f"validating {path} failed:\nstdout={completed.stdout}\nstderr={completed.stderr}")

    for index, line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        if not line.strip():
            continue
        event = json.loads(line)
        if event.get("schema") != "mbt.evt.v1":
            raise AssertionError(f"{path}:{index}: schema mismatch")
        if event.get("contract_version") != "1.0.0":
            raise AssertionError(f"{path}:{index}: contract_version mismatch")
        if event.get("type") not in {"cap_call_start", "cap_call_end"}:
            raise AssertionError(f"{path}:{index}: unexpected event type")
        data = event.get("data", {})
        for field in ["request_id", "capability", "operation", "deadline_ms", "adapter"]:
            if field not in data:
                raise AssertionError(f"{path}:{index}: missing data.{field}")
        if event.get("type") == "cap_call_end":
            for field in ["status", "host_reached", "request_hash", "response_hash"]:
                if field not in data:
                    raise AssertionError(f"{path}:{index}: missing data.{field}")


def lisp_string(text: str) -> str:
    return json.dumps(text)


def scenario_script(name: str, raw_events: Path) -> str:
    prefix = [
        "(begin",
        "  (events.enable #t)",
        f"  (events.set-path {lisp_string(str(raw_events))})",
        '  (load "examples/flagship_wheeled/lisp/bt_goal_flagship_nav_capability.lisp")',
        "  (define inst (bt.new-instance wheeled-goal-flagship-nav-capability))",
    ]
    suffix = [")"]
    if name == "accepted_success":
        body = [
            '  (bt.tick inst \'((goal_reached #f) (collision_imminent #f) (nav_goal_x 1.0) (nav_goal_y 2.0) (nav_mock_status "accepted")))',
            '  (bt.tick inst \'((goal_reached #f) (collision_imminent #f) (nav_mock_status "ok")))',
        ]
    elif name == "rejected":
        body = [
            '  (bt.tick inst \'((goal_reached #f) (collision_imminent #f) (nav_goal_x 1.0) (nav_goal_y 2.0) (nav_mock_status "rejected")))',
        ]
    elif name == "timeout":
        body = [
            '  (bt.tick inst \'((goal_reached #f) (collision_imminent #f) (nav_goal_x 1.0) (nav_goal_y 2.0) (nav_mock_status "timeout")))',
        ]
    elif name == "cancel_on_collision":
        body = [
            '  (bt.tick inst \'((goal_reached #f) (collision_imminent #f) (nav_goal_x 1.0) (nav_goal_y 2.0) (nav_mock_status "accepted")))',
            "  (bt.tick inst '((goal_reached #f) (collision_imminent #t) (act_avoid (0.10 -0.35))))",
        ]
    else:
        raise AssertionError(f"unknown scenario: {name}")
    return "\n".join(prefix + body + suffix) + "\n"


def is_cap_event(event: dict) -> bool:
    return event.get("type") in {"cap_call_start", "cap_call_end"}


def normalise_event(event: dict, scenario: str, ordinal: int) -> dict:
    out = dict(event)
    out["run_id"] = f"fixture-wheeled-flagship-nav-capability-{scenario}"
    out["unix_ms"] = BASE_UNIX_MS + ordinal - 1
    out["seq"] = ordinal
    return out


def write_jsonl(path: Path, events: list[dict]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("".join(json.dumps(event, separators=(",", ":")) + "\n" for event in events), encoding="utf-8")


def read_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def generate_scenario(muslisp: Path, out_root: Path, scenario: str, work_root: Path) -> dict:
    raw_events = work_root / scenario / "raw_events.jsonl"
    script = work_root / scenario / "run.lisp"
    script.parent.mkdir(parents=True, exist_ok=True)
    script.write_text(scenario_script(scenario, raw_events), encoding="utf-8")
    run_checked([str(muslisp), str(script)], f"running {scenario} evidence script")

    raw = [json.loads(line) for line in raw_events.read_text(encoding="utf-8").splitlines() if line.strip()]
    cap_events = [event for event in raw if is_cap_event(event)]
    events = [normalise_event(event, scenario, index + 1) for index, event in enumerate(cap_events)]
    if not events:
        raise AssertionError(f"{scenario}: no capability-call events produced")
    scenario_dir = out_root / scenario
    write_jsonl(scenario_dir / "events.jsonl", events)
    validate_event_log(scenario_dir / "events.jsonl")

    end_events = [event for event in events if event.get("type") == "cap_call_end"]
    statuses = [event.get("data", {}).get("status") for event in end_events]
    host_reached = [event.get("data", {}).get("host_reached") for event in end_events]
    operations = [event.get("data", {}).get("operation") for event in end_events]
    if scenario == "accepted_success" and statuses != ["accepted", "ok"]:
        raise AssertionError(f"{scenario}: status mismatch {statuses}")
    if scenario == "rejected" and statuses != ["rejected"]:
        raise AssertionError(f"{scenario}: status mismatch {statuses}")
    if scenario == "timeout" and statuses != ["timeout"]:
        raise AssertionError(f"{scenario}: status mismatch {statuses}")
    if scenario == "cancel_on_collision" and statuses != ["accepted", "cancelled"]:
        raise AssertionError(f"{scenario}: status mismatch {statuses}")

    return {
        "name": scenario,
        "events": f"{scenario}/events.jsonl",
        "operations": operations,
        "statuses": statuses,
        "host_reached": host_reached,
        "adapter": "mock-nav2",
    }


def write_report(out_root: Path, scenarios: list[dict]) -> None:
    report = {
        "schema_version": "wheeled_flagship_nav_capability_report.v1",
        "variant": "wheeled-goal-flagship-nav-capability",
        "capability": "cap.navigation.v1",
        "adapter": "mock-nav2",
        "real_nav2_stack": False,
        "scenarios": scenarios,
    }
    manifest = {
        "schema_version": "wheeled_flagship_nav_capability_manifest.v1",
        "variant": "wheeled-goal-flagship-nav-capability",
        "bt_source": "examples/flagship_wheeled/lisp/bt_goal_flagship_nav_capability.lisp",
        "event_log_schema": "mbt.evt.v1",
        "capability": "cap.navigation.v1",
        "core_adapter": "mock-nav2",
        "ros2_fake_server_extension": "covered by ROS2-gated unit tests",
        "real_nav2_stack": False,
        "scenarios": scenarios,
    }
    out_root.mkdir(parents=True, exist_ok=True)
    (out_root / "wheeled_flagship_nav_capability_report.json").write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    (out_root / "evidence_manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )


def generate(muslisp: Path, out_root: Path) -> None:
    if out_root.exists():
        shutil.rmtree(out_root)
    out_root.mkdir(parents=True)
    with tempfile.TemporaryDirectory() as tmp:
        work_root = Path(tmp)
        summaries = [generate_scenario(muslisp, out_root, scenario, work_root) for scenario in SCENARIOS]
    write_report(out_root, summaries)


def validate_fixture_tree(root: Path) -> None:
    manifest = read_json(root / "evidence_manifest.json")
    report = read_json(root / "wheeled_flagship_nav_capability_report.json")
    if manifest.get("schema_version") != "wheeled_flagship_nav_capability_manifest.v1":
        raise AssertionError("manifest schema version mismatch")
    if report.get("schema_version") != "wheeled_flagship_nav_capability_report.v1":
        raise AssertionError("report schema version mismatch")
    scenarios = {scenario.get("name"): scenario for scenario in report.get("scenarios", [])}
    if sorted(scenarios) != sorted(SCENARIOS):
        raise AssertionError(f"unexpected scenario set: {sorted(scenarios)}")
    for scenario in SCENARIOS:
        events = root / scenario / "events.jsonl"
        if not events.is_file():
            raise AssertionError(f"{scenario}: missing events.jsonl")
        validate_event_log(events)


def compare_trees(expected: Path, actual: Path) -> None:
    if not expected.is_dir():
        raise RuntimeError(f"checked-in fixture directory is missing: {expected}")
    comparison = filecmp.dircmp(expected, actual)
    problems: list[str] = []

    def walk(cmp: filecmp.dircmp, rel: Path) -> None:
        for name in cmp.left_only:
            problems.append(f"missing generated file: {(rel / name).as_posix()}")
        for name in cmp.right_only:
            problems.append(f"unexpected generated file: {(rel / name).as_posix()}")
        for name in cmp.diff_files:
            problems.append(f"content drift: {(rel / name).as_posix()}")
        for name, subcmp in cmp.subdirs.items():
            walk(subcmp, rel / name)

    walk(comparison, Path())
    if problems:
        joined = "\n".join(problems)
        raise AssertionError(
            "wheeled flagship navigation-capability evidence drifted. Run "
            "`python3 tools/run_wheeled_flagship_nav_capability_evidence.py --write` and review artefacts.\n" + joined
        )


def main() -> int:
    args = parse_args()
    try:
        muslisp = find_muslisp(args.muslisp)
        if args.write:
            out_root = args.out_dir or FIXTURE_ROOT
            generate(muslisp, out_root)
            validate_fixture_tree(out_root)
        else:
            with tempfile.TemporaryDirectory() as tmp:
                generated = Path(tmp) / "wheeled_flagship_nav_capability"
                generate(muslisp, generated)
                validate_fixture_tree(generated)
                compare_trees(FIXTURE_ROOT, generated)
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
