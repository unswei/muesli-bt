#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
FIXTURE_ROOT = REPO_ROOT / "fixtures" / "ros2" / "wheeled_flagship_nav2_real_stack"
LOG_VALIDATOR = REPO_ROOT / "tools" / "validate_log.py"
DEFAULT_HELPER_CANDIDATES = [
    REPO_ROOT / "build" / "linux-ros2" / "wheeled_flagship_nav2_real_stack_evidence",
    REPO_ROOT / "build" / "dev" / "wheeled_flagship_nav2_real_stack_evidence",
]
SCENARIOS = ["success", "cancel"]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Capture or verify wheeled flagship cap.navigation.v1 real Nav2 stack evidence."
    )
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true", help="validate the checked-in fixture bundle")
    mode.add_argument("--write", action="store_true", help="capture checked-in artefacts from a live Nav2 stack")
    parser.add_argument("--helper", type=Path, help="path to the ROS2-gated real-stack evidence helper")
    parser.add_argument("--out-dir", type=Path, help="output directory for --write")
    parser.add_argument("--ros-distro", default="humble", help="ROS distribution used for the capture")
    parser.add_argument("--simulator", default="unspecified", help="simulator or Nav2 environment used for the capture")
    parser.add_argument("--action-name", default="/navigate_to_pose", help="Nav2 NavigateToPose action name")
    parser.add_argument("--frame", default="map", help="goal frame")
    parser.add_argument("--goal-x", type=float, default=1.0, help="success scenario goal x")
    parser.add_argument("--goal-y", type=float, default=0.0, help="success scenario goal y")
    parser.add_argument("--goal-yaw", type=float, default=0.0, help="success scenario goal yaw")
    parser.add_argument("--cancel-goal-x", type=float, help="cancel scenario goal x")
    parser.add_argument("--cancel-goal-y", type=float, help="cancel scenario goal y")
    parser.add_argument("--cancel-goal-yaw", type=float, help="cancel scenario goal yaw")
    parser.add_argument("--timeout-ms", type=int, default=2000, help="capability timeout per request")
    parser.add_argument("--tick-period-ms", type=int, default=100, help="delay between BT ticks")
    parser.add_argument("--max-ticks", type=int, default=120, help="maximum ticks per scenario")
    parser.add_argument("--cancel-after-ticks", type=int, default=2, help="ticks before injecting collision in cancel scenario")
    parser.add_argument("--process-timeout-s", type=int, default=120, help="outer timeout for each helper process")
    return parser.parse_args()


def find_helper(explicit: Path | None) -> Path:
    candidates = [explicit] if explicit is not None else DEFAULT_HELPER_CANDIDATES
    for candidate in candidates:
        if candidate is not None and candidate.is_file():
            return candidate
    searched = ", ".join(str(path) for path in candidates if path is not None)
    raise RuntimeError(
        "wheeled flagship Nav2 real-stack evidence helper is missing. Build with "
        "MUESLI_BT_BUILD_INTEGRATION_ROS2=ON, then rerun. Searched: " + searched
    )


def run_checked(args: list[str], context: str, timeout_s: int | None = None) -> subprocess.CompletedProcess[str]:
    try:
        completed = subprocess.run(
            args,
            cwd=REPO_ROOT,
            check=False,
            capture_output=True,
            text=True,
            timeout=timeout_s,
        )
    except subprocess.TimeoutExpired as exc:
        raise RuntimeError(f"{context} timed out after {timeout_s}s:\nstdout={exc.stdout}\nstderr={exc.stderr}") from exc
    if completed.returncode != 0:
        raise RuntimeError(f"{context} failed:\nstdout={completed.stdout}\nstderr={completed.stderr}")
    return completed


def read_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


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
        if event.get("type") not in {"cap_call_start", "cap_call_end"}:
            continue
        data = event.get("data", {})
        for field in ["request_id", "capability", "operation", "deadline_ms", "adapter"]:
            if field not in data:
                raise AssertionError(f"{path}:{index}: missing data.{field}")
        if data.get("adapter") != "nav2":
            raise AssertionError(f"{path}:{index}: capability event should identify adapter=nav2")
        if event.get("type") == "cap_call_end":
            for field in ["status", "host_reached", "request_hash", "response_hash"]:
                if field not in data:
                    raise AssertionError(f"{path}:{index}: missing data.{field}")


def scenario_args(args: argparse.Namespace, scenario: str, out_dir: Path) -> list[str]:
    goal_x = args.cancel_goal_x if scenario == "cancel" and args.cancel_goal_x is not None else args.goal_x
    goal_y = args.cancel_goal_y if scenario == "cancel" and args.cancel_goal_y is not None else args.goal_y
    goal_yaw = args.cancel_goal_yaw if scenario == "cancel" and args.cancel_goal_yaw is not None else args.goal_yaw
    return [
        "--out-dir",
        str(out_dir),
        "--scenario",
        scenario,
        "--action-name",
        args.action_name,
        "--frame",
        args.frame,
        "--goal-x",
        str(goal_x),
        "--goal-y",
        str(goal_y),
        "--goal-yaw",
        str(goal_yaw),
        "--timeout-ms",
        str(args.timeout_ms),
        "--tick-period-ms",
        str(args.tick_period_ms),
        "--max-ticks",
        str(args.max_ticks),
        "--cancel-after-ticks",
        str(args.cancel_after_ticks),
    ]


def read_event_summaries(events_path: Path) -> tuple[list[str], list[bool], list[str]]:
    statuses: list[str] = []
    host_reached: list[bool] = []
    operations: list[str] = []
    for line in events_path.read_text(encoding="utf-8").splitlines():
        if not line.strip():
            continue
        event = json.loads(line)
        if event.get("type") != "cap_call_end":
            continue
        data = event.get("data", {})
        statuses.append(":" + str(data.get("status", "")))
        host_reached.append(bool(data.get("host_reached")))
        operations.append(str(data.get("operation", "")))
        if data.get("adapter") != "nav2":
            raise AssertionError(f"{events_path}: cap_call_end should identify adapter=nav2")
    return statuses, host_reached, operations


def validate_scenario(root: Path, scenario: str) -> dict:
    scenario_dir = root / scenario
    report = read_json(scenario_dir / "scenario_report.json")
    events_path = scenario_dir / "events.jsonl"
    if report.get("schema_version") != "wheeled_flagship_nav2_real_stack_scenario.v1":
        raise AssertionError(f"{scenario}: scenario report schema version mismatch")
    if report.get("variant") != "wheeled-goal-flagship-nav-capability":
        raise AssertionError(f"{scenario}: unexpected variant")
    if report.get("adapter") != "nav2":
        raise AssertionError(f"{scenario}: unexpected adapter")
    if report.get("nav2_stack") is not True or report.get("real_robot") is not False:
        raise AssertionError(f"{scenario}: scenario report should claim a real Nav2 stack but not a robot")
    validate_event_log(events_path)
    statuses, host_reached, operations = read_event_summaries(events_path)
    final_status = report.get("final_status")
    if scenario == "success" and final_status != ":ok":
        raise AssertionError(f"{scenario}: expected final_status=:ok, got {final_status}")
    if scenario == "cancel" and final_status != ":cancelled":
        raise AssertionError(f"{scenario}: expected final_status=:cancelled, got {final_status}")
    if not report.get("host_reached"):
        raise AssertionError(f"{scenario}: expected host_reached=true")
    if not statuses:
        raise AssertionError(f"{scenario}: no cap_call_end events found")
    return {
        "name": scenario,
        "events": f"{scenario}/events.jsonl",
        "scenario_report": f"{scenario}/scenario_report.json",
        "final_status": final_status,
        "final_bt_status": report.get("final_bt_status"),
        "host_reached": report.get("host_reached"),
        "operations": operations,
        "statuses": statuses,
        "event_host_reached": host_reached,
        "job_id_present": bool(report.get("job_id")),
        "request_hash": report.get("request_hash"),
        "response_hash": report.get("response_hash"),
        "progress_summary": {
            "distance_remaining_m": report.get("distance_remaining_m"),
            "number_of_recoveries": report.get("number_of_recoveries"),
            "navigation_time_ms": report.get("navigation_time_ms"),
            "estimated_time_remaining_ms": report.get("estimated_time_remaining_ms"),
        },
    }


def write_reports(root: Path, args: argparse.Namespace, scenarios: list[dict]) -> None:
    report = {
        "schema_version": "wheeled_flagship_nav2_real_stack_report.v1",
        "variant": "wheeled-goal-flagship-nav-capability",
        "capability": "cap.navigation.v1",
        "adapter": "nav2",
        "ros_distro": args.ros_distro,
        "simulator": args.simulator,
        "nav2_action_name": args.action_name,
        "nav2_stack": True,
        "real_robot": False,
        "scenarios": scenarios,
    }
    manifest = {
        "schema_version": "wheeled_flagship_nav2_real_stack_manifest.v1",
        "capture_status": "captured",
        "variant": "wheeled-goal-flagship-nav-capability",
        "bt_source": "examples/flagship_wheeled/lisp/bt_goal_flagship_nav_capability.lisp",
        "event_log_schema": "mbt.evt.v1",
        "capability": "cap.navigation.v1",
        "adapter": "nav2",
        "ros_distro": args.ros_distro,
        "simulator": args.simulator,
        "nav2_action_name": args.action_name,
        "nav2_stack": True,
        "real_robot": False,
        "report": "wheeled_flagship_nav2_real_stack_report.json",
        "scenarios": scenarios,
    }
    (root / "wheeled_flagship_nav2_real_stack_report.json").write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    (root / "evidence_manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )


def write_fixtures(helper: Path, out_root: Path, args: argparse.Namespace) -> None:
    if out_root.exists():
        shutil.rmtree(out_root)
    out_root.mkdir(parents=True)
    scenarios: list[dict] = []
    for scenario in SCENARIOS:
        scenario_dir = out_root / scenario
        run_checked(
            [str(helper), *scenario_args(args, scenario, scenario_dir)],
            f"capturing {scenario} scenario",
            args.process_timeout_s,
        )
        scenarios.append(validate_scenario(out_root, scenario))
    write_reports(out_root, args, scenarios)


def validate_pending_manifest(root: Path, manifest: dict) -> None:
    if manifest.get("schema_version") != "wheeled_flagship_nav2_real_stack_manifest.v1":
        raise AssertionError("manifest schema version mismatch")
    if manifest.get("capture_status") != "pending_real_stack_capture":
        raise AssertionError("unexpected pending manifest capture_status")
    if manifest.get("variant") != "wheeled-goal-flagship-nav-capability":
        raise AssertionError("unexpected pending manifest variant")
    if manifest.get("capability") != "cap.navigation.v1" or manifest.get("adapter") != "nav2":
        raise AssertionError("pending manifest capability metadata mismatch")
    if manifest.get("nav2_stack") is not False or manifest.get("real_robot") is not False:
        raise AssertionError("pending manifest must not claim a captured Nav2 stack or robot")
    if (root / "wheeled_flagship_nav2_real_stack_report.json").exists():
        raise AssertionError("pending capture marker should not include a captured report")


def check_fixtures(root: Path) -> None:
    manifest = read_json(root / "evidence_manifest.json")
    capture_status = manifest.get("capture_status")
    if capture_status == "pending_real_stack_capture":
        validate_pending_manifest(root, manifest)
        print("wheeled flagship Nav2 real-stack evidence is marked pending real-stack capture")
        return
    if capture_status != "captured":
        raise AssertionError(f"unknown capture_status: {capture_status}")
    report = read_json(root / "wheeled_flagship_nav2_real_stack_report.json")
    if report.get("schema_version") != "wheeled_flagship_nav2_real_stack_report.v1":
        raise AssertionError("report schema version mismatch")
    if report.get("nav2_stack") is not True or report.get("real_robot") is not False:
        raise AssertionError("captured report should claim a real Nav2 stack but not a robot")
    scenarios = {scenario.get("name"): scenario for scenario in report.get("scenarios", [])}
    if sorted(scenarios) != sorted(SCENARIOS):
        raise AssertionError(f"unexpected scenario set: {sorted(scenarios)}")
    for scenario in SCENARIOS:
        validate_scenario(root, scenario)


def main() -> int:
    args = parse_args()
    try:
        if args.write:
            helper = find_helper(args.helper)
            write_fixtures(helper, args.out_dir or FIXTURE_ROOT, args)
        else:
            check_fixtures(args.out_dir or FIXTURE_ROOT)
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
