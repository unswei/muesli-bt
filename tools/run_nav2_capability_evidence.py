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
FIXTURE_ROOT = REPO_ROOT / "fixtures" / "ros2" / "nav2_capability_fake_server"
LOG_VALIDATOR = REPO_ROOT / "tools" / "validate_log.py"
DEFAULT_HELPER_CANDIDATES = [
    REPO_ROOT / "build" / "linux-ros2" / "nav2_capability_evidence",
    REPO_ROOT / "build" / "dev" / "nav2_capability_evidence",
]
EXPECTED_SCENARIOS = [
    "accepted_success",
    "rejected",
    "abort_error",
    "cancelled",
    "unavailable",
    "timeout",
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Verify or write Nav2 fake-action-server capability evidence.")
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true", help="regenerate and compare with checked-in artefacts")
    mode.add_argument("--write", action="store_true", help="regenerate checked-in artefacts")
    parser.add_argument("--helper", type=Path, help="path to the ROS2-gated nav2_capability_evidence helper")
    parser.add_argument("--out-dir", type=Path, help="output directory for --write")
    return parser.parse_args()


def find_helper(explicit: Path | None) -> Path:
    candidates = [explicit] if explicit is not None else DEFAULT_HELPER_CANDIDATES
    for candidate in candidates:
        if candidate is not None and candidate.is_file():
            return candidate
    searched = ", ".join(str(path) for path in candidates if path is not None)
    raise RuntimeError(
        "Nav2 capability evidence helper is missing. Build with "
        "MUESLI_BT_BUILD_INTEGRATION_ROS2=ON, then rerun. Searched: " + searched
    )


def run_checked(args: list[str], context: str) -> subprocess.CompletedProcess[str]:
    completed = subprocess.run(args, cwd=REPO_ROOT, check=False, capture_output=True, text=True)
    if completed.returncode != 0:
        raise RuntimeError(f"{context} failed:\nstdout={completed.stdout}\nstderr={completed.stderr}")
    return completed


def generate(helper: Path, out_dir: Path) -> None:
    if out_dir.exists():
        shutil.rmtree(out_dir)
    out_dir.mkdir(parents=True)
    run_checked([str(helper), "--out-dir", str(out_dir)], "Nav2 capability evidence generation")


def read_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def validate_report(root: Path) -> None:
    manifest = read_json(root / "evidence_manifest.json")
    report = read_json(root / "nav2_capability_report.json")
    if manifest.get("schema_version") != "nav2_capability_evidence_manifest.v1":
        raise AssertionError("manifest schema version mismatch")
    if report.get("schema_version") != "nav2_capability_evidence_report.v1":
        raise AssertionError("report schema version mismatch")
    if manifest.get("real_nav2_stack") is not False or report.get("real_nav2_stack") is not False:
        raise AssertionError("Nav2 fake-server evidence must not claim a real Nav2 stack")
    scenarios = {scenario.get("name"): scenario for scenario in report.get("scenarios", [])}
    if sorted(scenarios) != sorted(EXPECTED_SCENARIOS):
        raise AssertionError(f"unexpected scenario set: {sorted(scenarios)}")

    expect_statuses = {
        "accepted_success": [":accepted", ":running", ":ok"],
        "rejected": [":rejected"],
        "abort_error": [":accepted", ":error"],
        "cancelled": [":accepted", ":cancelled"],
        "unavailable": [":unavailable"],
        "timeout": [":timeout"],
    }
    expect_host = {
        "accepted_success": [True, True, True],
        "rejected": [True],
        "abort_error": [True, True],
        "cancelled": [True, True],
        "unavailable": [False],
        "timeout": [True],
    }
    for name, statuses in expect_statuses.items():
        scenario = scenarios[name]
        if scenario.get("statuses") != statuses:
            raise AssertionError(f"{name}: status mismatch: {scenario.get('statuses')}")
        if scenario.get("host_reached") != expect_host[name]:
            raise AssertionError(f"{name}: host_reached mismatch: {scenario.get('host_reached')}")
        if not scenario.get("request_hashes") or not scenario.get("response_hashes"):
            raise AssertionError(f"{name}: missing request/response hashes")
    if scenarios["accepted_success"].get("fake_server_goal_count") != 1:
        raise AssertionError("accepted_success should send exactly one fake-server goal")
    if "distance_remaining_m=0.75" not in scenarios["accepted_success"].get("progress_summary", ""):
        raise AssertionError("accepted_success should report normalised Nav2 feedback")
    if scenarios["cancelled"].get("fake_server_cancel_count") != 1:
        raise AssertionError("cancelled should observe one fake-server cancel")
    if scenarios["unavailable"].get("fake_server_goal_count") != 0:
        raise AssertionError("unavailable should not reach a fake action server")


def validate_events(root: Path) -> None:
    for scenario in EXPECTED_SCENARIOS:
        path = root / scenario / "events.jsonl"
        if not path.is_file():
            raise AssertionError(f"{scenario}: missing events.jsonl")
        run_checked([sys.executable, str(LOG_VALIDATOR), str(path)], f"validating {scenario} events")
        lines = [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines() if line.strip()]
        if not lines:
            raise AssertionError(f"{scenario}: empty events.jsonl")
        types = {line.get("type") for line in lines}
        if not {"cap_call_start", "cap_call_end"}.issubset(types):
            raise AssertionError(f"{scenario}: missing capability call events")
        for line in lines:
            if line.get("type") in {"cap_call_start", "cap_call_end"} and line.get("data", {}).get("adapter") != "nav2":
                raise AssertionError(f"{scenario}: capability call event should identify adapter=nav2")


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
            "Nav2 capability evidence drifted. Run "
            "`python3 tools/run_nav2_capability_evidence.py --write` in a ROS2 build and review artefacts.\n" + joined
        )


def write_fixtures(helper: Path, out_dir: Path) -> None:
    generate(helper, out_dir)
    validate_report(out_dir)
    validate_events(out_dir)


def check_fixtures(helper: Path) -> None:
    with tempfile.TemporaryDirectory() as tmp:
        generated = Path(tmp) / "nav2_capability_fake_server"
        generate(helper, generated)
        validate_report(generated)
        validate_events(generated)
        compare_trees(FIXTURE_ROOT, generated)


def main() -> int:
    args = parse_args()
    try:
        helper = find_helper(args.helper)
        if args.write:
            write_fixtures(helper, args.out_dir or FIXTURE_ROOT)
        else:
            check_fixtures(helper)
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
