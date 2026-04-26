#!/usr/bin/env python3
"""Verify canonical ROS-backed replay artefacts from the Linux rosbag L2 suite."""

from __future__ import annotations

import argparse
import json
import math
import pathlib
import sys
from typing import Any


EXPECTED_SCENARIOS = {
    "rosbag-replay-case": {
        "summary": {
            "status": ":stopped",
            "ticks": 2,
            "fallback_count": 0,
            "overrun_count": 0,
            "backend_published_actions": 2,
            "command_count_min": 1,
            "last_command": {"linear_x": 0.25, "linear_y": 0.0, "angular_z": 0.05},
            "event_log_lines": 8,
            "final_obs_x": 1.6,
            "message": "episode_max reached",
        },
        "records": [
            {
                "schema_version": "ros2.l2.v1",
                "tick": 1,
                "t_ms": 1_700_000_000_000,
                "used_fallback": False,
                "overrun": False,
                "pose_x": 0.2,
                "action": {"linear_x": 0.25, "linear_y": 0.0, "angular_z": 0.05},
            },
            {
                "schema_version": "ros2.l2.v1",
                "tick": 2,
                "t_ms": 1_700_000_000_040,
                "used_fallback": False,
                "overrun": False,
                "pose_x": 1.6,
                "action": {"linear_x": 0.25, "linear_y": 0.0, "angular_z": 0.05},
            },
        ],
        "event_counts": {
            "run_start": 1,
            "episode_begin": 1,
            "tick_begin": 2,
            "tick_end": 2,
            "episode_end": 1,
            "run_end": 1,
            "error": 0,
        },
        "time_policy": {
            "time_source": "ros_wall_time",
            "use_sim_time": False,
            "obs_timestamp_source": "message_header_or_node_clock",
        },
        "bag_required": True,
    },
    "rosbag-clamped-action-case": {
        "summary": {
            "status": ":stopped",
            "ticks": 2,
            "fallback_count": 0,
            "overrun_count": 0,
            "backend_published_actions": 2,
            "command_count_min": 1,
            "last_command": {"linear_x": 1.0, "linear_y": -1.0, "angular_z": 1.0},
            "event_log_lines": 8,
            "final_obs_x": 0.8,
            "message": "episode_max reached",
        },
        "records": [
            {
                "schema_version": "ros2.l2.clamp.v1",
                "tick": 1,
                "t_ms": 1_700_000_100_000,
                "used_fallback": False,
                "overrun": False,
                "pose_x": -0.4,
                "action": {"linear_x": 2.5, "linear_y": -2.0, "angular_z": 1.5},
            },
            {
                "schema_version": "ros2.l2.clamp.v1",
                "tick": 2,
                "t_ms": 1_700_000_100_040,
                "used_fallback": False,
                "overrun": False,
                "pose_x": 0.8,
                "action": {"linear_x": 2.5, "linear_y": -2.0, "angular_z": 1.5},
            },
        ],
        "event_counts": {
            "run_start": 1,
            "episode_begin": 1,
            "tick_begin": 2,
            "tick_end": 2,
            "episode_end": 1,
            "run_end": 1,
            "error": 0,
        },
        "time_policy": {
            "time_source": "ros_wall_time",
            "use_sim_time": False,
            "obs_timestamp_source": "message_header_or_node_clock",
        },
        "bag_required": True,
    },
    "rosbag-invalid-action-fallback-case": {
        "summary": {
            "status": ":error",
            "ticks": 1,
            "fallback_count": 1,
            "overrun_count": 0,
            "backend_published_actions": 1,
            "command_count_min": 1,
            "last_command": {"linear_x": 0.0, "linear_y": 0.0, "angular_z": 0.0},
            "event_log_lines": 9,
            "final_obs_x": 0.4,
            "message_contains": "u must be a map",
        },
        "records": [
            {
                "schema_version": "ros2.l2.invalid.v1",
                "tick": 1,
                "t_ms": 1_700_000_200_000,
                "used_fallback": True,
                "overrun": False,
                "pose_x": 0.4,
                "action": {"linear_x": 0.0, "linear_y": 0.0, "angular_z": 0.0},
                "error_contains": "u must be a map",
                "on_tick_u": 1,
            },
        ],
        "event_counts": {
            "run_start": 1,
            "episode_begin": 1,
            "tick_begin": 1,
            "tick_end": 1,
            "host_action_invalid": 1,
            "fallback_used": 1,
            "episode_end": 1,
            "run_end": 1,
            "error": 1,
        },
        "time_policy": {
            "time_source": "ros_wall_time",
            "use_sim_time": False,
            "obs_timestamp_source": "message_header_or_node_clock",
        },
        "bag_required": True,
    },
    "rosbag-preemption-fallback-case": {
        "summary": {
            "status": ":error",
            "ticks": 2,
            "fallback_count": 1,
            "overrun_count": 0,
            "backend_published_actions": 2,
            "command_count_min": 1,
            "last_command": {"linear_x": 0.25, "linear_y": 0.0, "angular_z": 0.05},
            "event_log_lines": 11,
            "final_obs_x": 0.9,
            "message_contains": "u must be a map",
        },
        "records": [
            {
                "schema_version": "ros2.l2.preempt.v1",
                "tick": 1,
                "t_ms": 1_700_000_300_000,
                "used_fallback": False,
                "overrun": False,
                "pose_x": 0.2,
                "action": {"linear_x": 0.25, "linear_y": 0.0, "angular_z": 0.05},
            },
            {
                "schema_version": "ros2.l2.preempt.v1",
                "tick": 2,
                "t_ms": 1_700_000_300_040,
                "used_fallback": True,
                "overrun": False,
                "pose_x": 0.9,
                "action": {"linear_x": 0.25, "linear_y": 0.0, "angular_z": 0.05},
                "error_contains": "u must be a map",
                "on_tick_u": 1,
            },
        ],
        "event_counts": {
            "run_start": 1,
            "episode_begin": 1,
            "tick_begin": 2,
            "tick_end": 2,
            "host_action_invalid": 1,
            "fallback_used": 1,
            "episode_end": 1,
            "run_end": 1,
            "error": 1,
        },
        "time_policy": {
            "time_source": "ros_wall_time",
            "use_sim_time": False,
            "obs_timestamp_source": "message_header_or_node_clock",
        },
        "bag_required": True,
    },
    "reset-unsupported-case": {
        "summary": {
            "status": ":unsupported",
            "ticks": 0,
            "fallback_count": 0,
            "overrun_count": 0,
            "backend_published_actions": 0,
            "command_count_min": 0,
            "last_command": {"linear_x": 0.0, "linear_y": 0.0, "angular_z": 0.0},
            "event_log_lines": 3,
            "final_obs_x": None,
            "message_contains": "episode_max>1 requires env.reset capability",
        },
        "records": [],
        "event_counts": {"run_start": 1, "tick_begin": 0, "tick_end": 0, "run_end": 1, "error": 1},
        "time_policy": {
            "time_source": "ros_wall_time",
            "use_sim_time": False,
            "obs_timestamp_source": "message_header_or_node_clock",
        },
        "bag_required": False,
    },
}


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--artifact-root",
        default="build/conformance-l2-ros2-humble/ros2_l2_artifacts",
        help="Path to the ROS-backed replay artefact root.",
    )
    parser.add_argument(
        "--scenario",
        action="append",
        default=[],
        help="Optional scenario filter. May be passed multiple times.",
    )
    return parser.parse_args(argv)


def load_json(path: pathlib.Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))


def load_jsonl(path: pathlib.Path) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    with path.open("r", encoding="utf-8") as handle:
        for line_no, line in enumerate(handle, start=1):
            text = line.strip()
            if not text:
                continue
            try:
                parsed = json.loads(text)
            except json.JSONDecodeError as exc:
                raise RuntimeError(f"{path}:{line_no}: invalid JSON: {exc}") from exc
            if not isinstance(parsed, dict):
                raise RuntimeError(f"{path}:{line_no}: expected object record")
            records.append(parsed)
    return records


def expect(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def expect_close(actual: float, expected: float, label: str, tol: float = 1e-6) -> None:
    if not math.isclose(actual, expected, rel_tol=0.0, abs_tol=tol):
        raise RuntimeError(f"{label}: expected {expected}, got {actual}")


def expect_summary(summary: dict[str, Any], expected: dict[str, Any], scenario: str) -> None:
    expect(summary.get("scenario") == scenario, f"{scenario}: summary scenario mismatch")
    expect(summary.get("status") == expected["status"], f"{scenario}: summary status mismatch")
    expect(summary.get("ticks") == expected["ticks"], f"{scenario}: summary ticks mismatch")
    expect(summary.get("fallback_count") == expected["fallback_count"], f"{scenario}: summary fallback_count mismatch")
    expect(summary.get("overrun_count") == expected["overrun_count"], f"{scenario}: summary overrun_count mismatch")
    expect(
        summary.get("backend_published_actions") == expected["backend_published_actions"],
        f"{scenario}: summary backend_published_actions mismatch",
    )
    expect(
        int(summary.get("command_count", -1)) >= int(expected["command_count_min"]),
        f"{scenario}: summary command_count below expected minimum",
    )
    expect(summary.get("event_log_lines") == expected["event_log_lines"], f"{scenario}: summary event_log_lines mismatch")
    if expected.get("final_obs_x") is None:
        expect(summary.get("final_obs_x") is None, f"{scenario}: summary final_obs_x should be null")
    else:
        expect_close(float(summary.get("final_obs_x")), float(expected["final_obs_x"]), f"{scenario}: summary final_obs_x")

    message = str(summary.get("message", ""))
    if "message" in expected:
        expect(message == expected["message"], f"{scenario}: summary message mismatch")
    if "message_contains" in expected:
        expect(expected["message_contains"] in message, f"{scenario}: summary message mismatch")

    command = summary.get("last_command")
    expect(isinstance(command, dict), f"{scenario}: summary last_command missing")
    for key, value in expected["last_command"].items():
        expect_close(float(command.get(key)), float(value), f"{scenario}: summary last_command.{key}")


def expect_common_record(record: dict[str, Any], scenario: str, expected: dict[str, Any]) -> None:
    expect(record.get("schema_version") == expected["schema_version"], f"{scenario}: record schema_version mismatch")
    expect(record.get("tick") == expected["tick"], f"{scenario}: record tick mismatch")
    expect(record.get("t_ms") == expected["t_ms"], f"{scenario}: record t_ms mismatch")
    expect(record.get("used_fallback") is expected["used_fallback"], f"{scenario}: record used_fallback mismatch")
    expect(record.get("overrun") is expected["overrun"], f"{scenario}: record overrun mismatch")

    budget = record.get("budget")
    expect(isinstance(budget, dict), f"{scenario}: record budget missing")
    expect_close(float(budget.get("tick_budget_ms")), 200.0, f"{scenario}: record budget.tick_budget_ms")
    expect(float(budget.get("tick_time_ms", -1.0)) >= 0.0, f"{scenario}: record budget.tick_time_ms invalid")

    obs = record.get("obs")
    expect(isinstance(obs, dict), f"{scenario}: record obs missing")
    expect(obs.get("obs_schema") == "ros2.obs.v1", f"{scenario}: record obs_schema mismatch")
    expect(obs.get("state_schema") == "ros2.state.v1", f"{scenario}: record state_schema mismatch")
    state = obs.get("state")
    expect(isinstance(state, dict), f"{scenario}: record state missing")
    expect(state.get("state_schema") == "ros2.state.v1", f"{scenario}: record state.state_schema mismatch")
    pose = state.get("pose")
    expect(isinstance(pose, dict), f"{scenario}: record pose missing")
    expect_close(float(pose.get("x")), float(expected["pose_x"]), f"{scenario}: record pose.x")

    action = record.get("action")
    expect(isinstance(action, dict), f"{scenario}: record action missing")
    expect(action.get("action_schema") == "ros2.action.v1", f"{scenario}: record action_schema mismatch")
    u = action.get("u")
    expect(isinstance(u, dict), f"{scenario}: record action.u missing")
    for key, value in expected["action"].items():
        expect_close(float(u.get(key)), float(value), f"{scenario}: record action.u.{key}")


def expect_event_envelope(event: dict[str, Any], scenario: str, expected_type: str, expected_tick: int | None = None) -> None:
    expect(event.get("schema") == "mbt.evt.v1", f"{scenario}: event schema mismatch")
    expect(event.get("contract_version") == "1.0.0", f"{scenario}: event contract_version mismatch")
    expect(event.get("type") == expected_type, f"{scenario}: event type mismatch")
    expect(isinstance(event.get("seq"), int) and int(event["seq"]) > 0, f"{scenario}: event seq invalid")
    if expected_tick is not None:
        expect(event.get("tick") == expected_tick, f"{scenario}: event tick mismatch")


def select_events(events: list[dict[str, Any]], event_type: str) -> list[dict[str, Any]]:
    return [event for event in events if event.get("type") == event_type]


def expect_run_start_time_policy(event: dict[str, Any], scenario: str, expected: dict[str, Any]) -> None:
    expect_event_envelope(event, f"{scenario} run_start", "run_start")
    data = event.get("data")
    expect(isinstance(data, dict), f"{scenario}: run_start data missing")
    capabilities = data.get("capabilities")
    expect(isinstance(capabilities, dict), f"{scenario}: run_start capabilities missing")
    expect(capabilities.get("time_source") == expected["time_source"], f"{scenario}: time_source mismatch")
    expect(capabilities.get("use_sim_time") is expected["use_sim_time"], f"{scenario}: use_sim_time mismatch")
    expect(
        capabilities.get("obs_timestamp_source") == expected["obs_timestamp_source"],
        f"{scenario}: obs_timestamp_source mismatch",
    )


def verify_bag_dir(path: pathlib.Path, scenario: str) -> None:
    expect(path.is_dir(), f"{scenario}: missing odom_bag directory")
    expect((path / "metadata.yaml").is_file(), f"{scenario}: missing bag metadata.yaml")
    expect(any(child.suffix == ".db3" for child in path.iterdir()), f"{scenario}: missing sqlite3 bag file")


def verify_scenario(root: pathlib.Path, scenario: str, expectation: dict[str, Any]) -> None:
    scenario_dir = root / scenario
    expect(scenario_dir.is_dir(), f"{scenario}: missing artefact directory")

    summary_path = scenario_dir / "summary.json"
    expect(summary_path.is_file(), f"{scenario}: missing summary.json")
    summary = load_json(summary_path)
    expect(isinstance(summary, dict), f"{scenario}: summary is not a JSON object")
    expect_summary(summary, expectation["summary"], scenario)

    event_log_path = scenario_dir / "events.jsonl"
    expect(event_log_path.is_file(), f"{scenario}: missing events.jsonl")
    events = load_jsonl(event_log_path)
    expect(len(events) == expectation["summary"]["event_log_lines"], f"{scenario}: unexpected event log line count")
    for event_type, expected_count in expectation["event_counts"].items():
        actual = len(select_events(events, event_type))
        expect(actual == expected_count, f"{scenario}: unexpected {event_type} count")

    expected_records = expectation["records"]
    tick_end_events = select_events(events, "tick_end")
    expect(len(tick_end_events) == len(expected_records), f"{scenario}: unexpected tick_end count")
    for idx, (event, expected_record) in enumerate(zip(tick_end_events, expected_records, strict=True), start=1):
        expect_event_envelope(event, f"{scenario} tick_end {idx}", "tick_end", expected_record["tick"])
        record = event.get("data")
        expect(isinstance(record, dict), f"{scenario} tick_end {idx}: data missing")
        expect_common_record(record, f"{scenario} record {idx}", expected_record)
        if idx < len(expected_records):
            expect(record.get("status") == "running", f"{scenario} record {idx}: status mismatch")
        elif summary["status"] == ":error":
            expect(record.get("status") == "error", f"{scenario} record {idx}: status mismatch")
        elif summary["status"] == ":ok":
            expect(record.get("status") == "ok", f"{scenario} record {idx}: status mismatch")
        elif expectation["event_counts"].get("episode_end", 0) > 0:
            expect(record.get("status") == "running", f"{scenario} record {idx}: status mismatch")
        else:
            expect(record.get("status") == "stopped", f"{scenario} record {idx}: status mismatch")
        if "error_contains" in expected_record:
            expect(expected_record["error_contains"] in str(record.get("error", "")),
                   f"{scenario} record {idx}: error mismatch")
        else:
            expect("error" not in record, f"{scenario} record {idx}: unexpected error field")
        if "on_tick_u" in expected_record:
            on_tick = record.get("on_tick")
            expect(isinstance(on_tick, dict), f"{scenario} record {idx}: on_tick missing")
            expect(on_tick.get("u") == expected_record["on_tick_u"],
                   f"{scenario} record {idx}: on_tick.u mismatch")

    for idx, event in enumerate(select_events(events, "tick_begin"), start=1):
        expect_event_envelope(event, f"{scenario} tick_begin {idx}", "tick_begin", idx)

    run_start_events = select_events(events, "run_start")
    for idx, event in enumerate(run_start_events, start=1):
        expect_run_start_time_policy(event, f"{scenario} run_start {idx}", expectation["time_policy"])

    for idx, event in enumerate(select_events(events, "episode_begin"), start=1):
        expect_event_envelope(event, f"{scenario} episode_begin {idx}", "episode_begin")

    for idx, event in enumerate(select_events(events, "episode_end"), start=1):
        expect_event_envelope(event, f"{scenario} episode_end {idx}", "episode_end")

    run_end_events = select_events(events, "run_end")
    if run_end_events:
        run_end = run_end_events[-1]
        expect_event_envelope(run_end, f"{scenario} run_end", "run_end")
        data = run_end.get("data")
        expect(isinstance(data, dict), f"{scenario}: run_end data missing")
        expected_status = str(summary["status"]).lstrip(":")
        expect(data.get("status") == expected_status, f"{scenario}: run_end status mismatch")
        if "message" in expectation["summary"]:
            expect(data.get("reason") == expectation["summary"]["message"], f"{scenario}: run_end reason mismatch")
        if "message_contains" in expectation["summary"]:
            expect(expectation["summary"]["message_contains"] in str(data.get("reason", "")),
                   f"{scenario}: run_end reason mismatch")

    error_events = select_events(events, "error")
    if error_events:
        for idx, event in enumerate(error_events, start=1):
            expect_event_envelope(event, f"{scenario} error {idx}", "error")
            data = event.get("data")
            expect(isinstance(data, dict), f"{scenario} error {idx}: data missing")
            expect(data.get("severity") == "error", f"{scenario} error {idx}: severity mismatch")
            if "message_contains" in expectation["summary"]:
                expect(expectation["summary"]["message_contains"] in str(data.get("message", "")),
                       f"{scenario} error {idx}: message mismatch")

    if expectation["bag_required"]:
        verify_bag_dir(scenario_dir / "odom_bag", scenario)
    else:
        expect(not (scenario_dir / "odom_bag").exists(), f"{scenario}: unexpected odom_bag directory")


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    root = pathlib.Path(args.artifact_root)
    if not root.is_dir():
        print(f"error: artefact root not found: {root}", file=sys.stderr)
        return 2

    selected = set(args.scenario)
    scenarios = EXPECTED_SCENARIOS
    if selected:
        unknown = selected - set(EXPECTED_SCENARIOS)
        if unknown:
            print(f"error: unknown scenario(s): {', '.join(sorted(unknown))}", file=sys.stderr)
            return 2
        scenarios = {name: EXPECTED_SCENARIOS[name] for name in sorted(selected)}

    for scenario, expectation in scenarios.items():
        try:
            verify_scenario(root, scenario, expectation)
            print(f"verified {scenario}")
        except RuntimeError as exc:
            print(f"error: {exc}", file=sys.stderr)
            return 1

    print("ROS-backed replay artefact verification passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
