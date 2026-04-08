#!/usr/bin/env python3

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
COMPARE_SCRIPT = REPO_ROOT / "examples" / "flagship_wheeled" / "tools" / "compare_runs.py"
NORMALISE_SCRIPT = REPO_ROOT / "examples" / "flagship_wheeled" / "tools" / "normalise_run.py"
PYBULLET_RUN = REPO_ROOT / "examples" / "flagship_wheeled" / "out" / "pybullet_flagship_protocol.json"
WEBOTS_RUN = REPO_ROOT / "examples" / "flagship_wheeled" / "out" / "webots_flagship_protocol.json"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def run_cli(script: Path, *args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(script), *args],
        cwd=REPO_ROOT,
        check=False,
        capture_output=True,
        text=True,
    )


def write_synthetic_ros2_run(path: Path) -> None:
    rows = [
        {
            "schema_version": "ros2_flagship_goal.v1",
            "tick": 1,
            "t_ms": 50,
            "obs": {"obs_schema": "ros2.obs.v1", "collision_imminent": False},
            "bt": {
                "active_path": ["root", "planner"],
                "status_by_node": {"branch": "planner"},
            },
            "planner": {"used": True, "confidence": 0.82},
            "on_tick": {
                "schema_version": "ros2_flagship_goal.v1",
                "goal_dist": 1.20,
                "done": False,
                "shared_action": {"action_schema": "flagship.cmd.v1", "u": [0.22, -0.18]},
            },
        },
        {
            "schema_version": "ros2_flagship_goal.v1",
            "tick": 2,
            "t_ms": 100,
            "obs": {"obs_schema": "ros2.obs.v1", "collision_imminent": False},
            "bt": {
                "active_path": ["root", "direct_goal"],
                "status_by_node": {"branch": "direct_goal"},
            },
            "planner": {"used": False, "confidence": 0.0},
            "on_tick": {
                "schema_version": "ros2_flagship_goal.v1",
                "goal_dist": 0.62,
                "done": False,
                "shared_action": {"linear_x": 0.25, "angular_z": -0.05},
            },
        },
        {
            "schema_version": "ros2_flagship_goal.v1",
            "tick": 3,
            "t_ms": 150,
            "obs": {"obs_schema": "ros2.obs.v1", "collision_imminent": False},
            "bt": {
                "active_path": ["root", "goal_reached"],
                "status_by_node": {"branch": "goal_reached"},
            },
            "planner": {"used": False, "confidence": 0.0},
            "on_tick": {
                "schema_version": "ros2_flagship_goal.v1",
                "goal_dist": 0.18,
                "done": True,
                "shared_action": {"linear_x": 0.0, "angular_z": 0.0},
            },
        },
    ]
    path.write_text(
        "\n".join(json.dumps(row, separators=(",", ":")) for row in rows) + "\n",
        encoding="utf-8",
    )


def main() -> int:
    require(PYBULLET_RUN.is_file(), f"missing fixture: {PYBULLET_RUN}")
    require(WEBOTS_RUN.is_file(), f"missing fixture: {WEBOTS_RUN}")

    with tempfile.TemporaryDirectory() as tmp_dir:
        tmp_path = Path(tmp_dir)
        ros2_raw_path = tmp_path / "ros2_flagship.jsonl"
        ros2_normalised_path = tmp_path / "ros2_flagship.json"
        compare_json_path = tmp_path / "comparison.json"

        write_synthetic_ros2_run(ros2_raw_path)

        normalise_completed = run_cli(
            NORMALISE_SCRIPT,
            "--backend",
            "ros2",
            "--output",
            str(ros2_normalised_path),
            str(ros2_raw_path),
        )
        require(
            normalise_completed.returncode == 0,
            "normalise_run.py failed:\n"
            f"stdout={normalise_completed.stdout}\n"
            f"stderr={normalise_completed.stderr}",
        )

        compare_completed = run_cli(
            COMPARE_SCRIPT,
            "--max-ticks",
            "320",
            "--json-out",
            str(compare_json_path),
            str(PYBULLET_RUN),
            str(WEBOTS_RUN),
            str(ros2_normalised_path),
        )
        require(
            compare_completed.returncode == 0,
            "compare_runs.py failed:\n"
            f"stdout={compare_completed.stdout}\n"
            f"stderr={compare_completed.stderr}",
        )

        payload = json.loads(compare_json_path.read_text(encoding="utf-8"))
        require(payload["schema_version"] == "flagship.comparison.v1", "unexpected comparison schema")

        baseline_final = payload["baseline"]["final"]
        require(baseline_final["goal_reached"] is True, "baseline run should reach the goal")
        require(baseline_final["status"] == "success", "baseline run should finish with success")

        comparisons = payload["comparisons"]
        require(len(comparisons) == 2, "expected Webots and ROS2 candidate comparisons")
        for result in comparisons:
            candidate_backend = result["candidate_backend"]
            candidate_final = result["candidate_final"]
            require(
                result["same_goal_reached"] is True,
                f"{candidate_backend}: goal-reaching invariant should hold across backends",
            )
            require(
                result["same_final_status"] is True,
                f"{candidate_backend}: final status invariant should hold across backends",
            )
            require(
                candidate_final["goal_reached"] is True,
                f"{candidate_backend}: candidate run should reach the goal",
            )
            require(
                candidate_final["status"] == "success",
                f"{candidate_backend}: candidate run should finish with success",
            )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
