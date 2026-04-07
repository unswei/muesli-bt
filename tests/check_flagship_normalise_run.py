#!/usr/bin/env python3

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
SCRIPT = REPO_ROOT / "examples" / "flagship_wheeled" / "tools" / "normalise_run.py"


def run_cli(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(SCRIPT), *args],
        cwd=REPO_ROOT,
        check=False,
        capture_output=True,
        text=True,
    )


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
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
            "planner": {"used": True, "confidence": 0.81},
            "on_tick": {
                "schema_version": "ros2_flagship_goal.v1",
                "goal_dist": 1.25,
                "done": False,
                "shared_action": {"action_schema": "flagship.cmd.v1", "u": [0.2, -0.35]},
            },
        },
        {
            "schema_version": "ros2_flagship_goal.v1",
            "tick": 2,
            "t_ms": 100,
            "obs": {"obs_schema": "ros2.obs.v1", "collision_imminent": True},
            "bt": {
                "active_path": ["root", "avoid"],
                "status_by_node": {"branch": "avoid"},
            },
            "planner": {"used": False, "confidence": 0.0},
            "on_tick": {
                "schema_version": "ros2_flagship_goal.v1",
                "goal_dist": 0.9,
                "done": True,
                "shared_action": {"linear_x": 0.0, "angular_z": 0.0},
            },
        },
    ]

    with tempfile.TemporaryDirectory() as tmp_dir:
        input_path = Path(tmp_dir) / "ros2_flagship.jsonl"
        output_path = Path(tmp_dir) / "normalised.json"
        input_path.write_text(
            "\n".join(json.dumps(row, separators=(",", ":")) for row in rows) + "\n",
            encoding="utf-8",
        )

        completed = run_cli("--backend", "ros2", "--output", str(output_path), str(input_path))
        require(completed.returncode == 0, f"normalise_run.py failed:\nstdout={completed.stdout}\nstderr={completed.stderr}")

        payload = json.loads(output_path.read_text(encoding="utf-8"))
        require(payload["schema_version"] == "flagship.normalised_run.v1", "unexpected schema_version")
        require(payload["backend"] == "ros2", "backend should be ros2")
        require(payload["summary"]["tick_count"] == 2, "tick_count should be 2")
        require(payload["summary"]["planner_used_ticks"] == 1, "planner_used_ticks should count planner usage")
        require(payload["final"]["status"] == "success", "final status should be success when final done=true")

        ticks = payload["ticks"]
        require(ticks[0]["branch"] == "planner", "first branch should be planner")
        require(abs(ticks[0]["shared_action"]["linear_x"] - 0.2) < 1.0e-9, "u[0] should map to linear_x")
        require(abs(ticks[0]["shared_action"]["angular_z"] + 0.35) < 1.0e-9, "u[1] should map to angular_z")
        require(ticks[1]["collision_imminent"] is True, "collision_imminent should come from obs")
        require(ticks[1]["goal_reached"] is True, "goal_reached should come from on_tick.done")
        require(ticks[1]["branch"] == "avoid", "second branch should be avoid")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
