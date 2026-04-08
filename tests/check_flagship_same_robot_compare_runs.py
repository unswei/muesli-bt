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


def write_jsonl_rows(path: Path, rows: list[dict[str, object]]) -> None:
    path.write_text(
        "\n".join(json.dumps(row, separators=(",", ":")) for row in rows) + "\n",
        encoding="utf-8",
    )


def write_synthetic_webots_epuck_run(path: Path) -> None:
    rows = [
        {
            "schema_version": "flagship_goal.v1",
            "tick": 1,
            "t_ms": 50,
            "obs": {"goal_dist": 1.10, "obs_schema": "epuck.goal.obs.v1"},
            "bt": {
                "active_path": ["root", "planner"],
                "status_by_node": {"branch": "planner"},
            },
            "on_tick": {
                "done": False,
                "planner": {"used": True, "confidence": 0.78},
                "bt": {
                    "active_path": ["root", "planner"],
                    "status_by_node": {"branch": "planner"},
                },
                "shared_action": {"linear_x": 0.72, "angular_z": 0.18},
            },
        },
        {
            "schema_version": "flagship_goal.v1",
            "tick": 2,
            "t_ms": 100,
            "obs": {"goal_dist": 0.58, "obs_schema": "epuck.goal.obs.v1"},
            "bt": {
                "active_path": ["root", "direct_goal"],
                "status_by_node": {"branch": "direct_goal"},
            },
            "on_tick": {
                "done": False,
                "planner": {"used": False, "confidence": 0.0},
                "bt": {
                    "active_path": ["root", "direct_goal"],
                    "status_by_node": {"branch": "direct_goal"},
                },
                "shared_action": {"linear_x": 0.48, "angular_z": 0.05},
            },
        },
        {
            "schema_version": "flagship_goal.v1",
            "tick": 3,
            "t_ms": 150,
            "obs": {"goal_dist": 0.21, "obs_schema": "epuck.goal.obs.v1"},
            "bt": {
                "active_path": ["root", "goal_reached"],
                "status_by_node": {"branch": "goal_reached"},
            },
            "on_tick": {
                "done": True,
                "planner": {"used": False, "confidence": 0.0},
                "bt": {
                    "active_path": ["root", "goal_reached"],
                    "status_by_node": {"branch": "goal_reached"},
                },
                "shared_action": {"linear_x": 0.0, "angular_z": 0.0},
            },
        },
    ]
    write_jsonl_rows(path, rows)


def write_synthetic_pybullet_epuck_run(path: Path) -> None:
    rows = [
        {
            "schema_version": "pybullet_epuck_goal.v1",
            "run_id": "pybullet_epuck_synth_goal",
            "tick_index": 1,
            "sim_time_s": 0.05,
            "distance_to_goal": 1.08,
            "goal_reached": False,
            "collision_imminent": False,
            "bt": {
                "active_path": ["root", "planner"],
                "node_status": {"branch": "planner"},
            },
            "planner": {"used": True, "confidence": 0.81},
            "shared_action": {"linear_x": 0.70, "angular_z": 0.20},
        },
        {
            "schema_version": "pybullet_epuck_goal.v1",
            "run_id": "pybullet_epuck_synth_goal",
            "tick_index": 2,
            "sim_time_s": 0.10,
            "distance_to_goal": 0.60,
            "goal_reached": False,
            "collision_imminent": False,
            "bt": {
                "active_path": ["root", "direct_goal"],
                "node_status": {"branch": "direct_goal"},
            },
            "shared_action": {"linear_x": 0.46, "angular_z": 0.06},
        },
        {
            "schema_version": "pybullet_epuck_goal.v1",
            "run_id": "pybullet_epuck_synth_goal",
            "tick_index": 3,
            "sim_time_s": 0.15,
            "distance_to_goal": 0.20,
            "goal_reached": True,
            "collision_imminent": False,
            "bt": {
                "active_path": ["root", "goal_reached"],
                "node_status": {"branch": "goal_reached"},
            },
            "shared_action": {"linear_x": 0.0, "angular_z": 0.0},
        },
    ]
    write_jsonl_rows(path, rows)


def main() -> int:
    with tempfile.TemporaryDirectory() as tmp_dir:
        tmp_path = Path(tmp_dir)
        webots_raw_path = tmp_path / "webots_epuck_flagship.jsonl"
        pybullet_raw_path = tmp_path / "pybullet_epuck_flagship.jsonl"
        webots_normalised_path = tmp_path / "webots_epuck_flagship.json"
        pybullet_normalised_path = tmp_path / "pybullet_epuck_flagship.json"
        compare_json_path = tmp_path / "same_robot_comparison.json"

        write_synthetic_webots_epuck_run(webots_raw_path)
        write_synthetic_pybullet_epuck_run(pybullet_raw_path)

        for backend, raw_path, output_path in (
            ("webots", webots_raw_path, webots_normalised_path),
            ("pybullet", pybullet_raw_path, pybullet_normalised_path),
        ):
            normalise_completed = run_cli(
                NORMALISE_SCRIPT,
                "--backend",
                backend,
                "--output",
                str(output_path),
                str(raw_path),
            )
            require(
                normalise_completed.returncode == 0,
                f"normalise_run.py failed for {backend}:\n"
                f"stdout={normalise_completed.stdout}\n"
                f"stderr={normalise_completed.stderr}",
            )

        compare_completed = run_cli(
            COMPARE_SCRIPT,
            "--max-ticks",
            "320",
            "--json-out",
            str(compare_json_path),
            str(webots_normalised_path),
            str(pybullet_normalised_path),
        )
        require(
            compare_completed.returncode == 0,
            "compare_runs.py failed:\n"
            f"stdout={compare_completed.stdout}\n"
            f"stderr={compare_completed.stderr}",
        )

        payload = json.loads(compare_json_path.read_text(encoding="utf-8"))
        require(payload["schema_version"] == "flagship.comparison.v1", "unexpected comparison schema")
        require(payload["baseline"]["backend"] == "webots", "strict track should use Webots as baseline")
        require(payload["baseline"]["final"]["goal_reached"] is True, "Webots baseline should reach the goal")
        require(payload["baseline"]["final"]["status"] == "success", "Webots baseline should finish with success")

        comparisons = payload["comparisons"]
        require(len(comparisons) == 1, "expected one PyBullet e-puck candidate comparison")
        result = comparisons[0]
        require(result["candidate_backend"] == "pybullet", "expected PyBullet e-puck candidate")
        require(result["same_goal_reached"] is True, "strict track should preserve goal-reaching")
        require(result["same_final_status"] is True, "strict track should preserve final status")
        require(result["branches"]["same_sequence"] is True, "strict track should preserve branch order")
        require(
            abs(float(result["branches"]["family_overlap"]) - 1.0) <= 1.0e-9,
            "strict track should use the same branch family",
        )
        require(result["branches"]["terminal_branch_match"] is True, "strict track should end on the same branch")
        require(
            float(result["progress"]["reduction_ratio_delta"]) <= 0.08,
            "strict track progress ratio delta should stay bounded",
        )

        aligned_debug = result["aligned_debug"]
        require(
            float(aligned_debug["branch_trace"]["agreement_ratio"]) == 1.0,
            "strict track aligned branch trace should agree fully",
        )
        require(float(aligned_debug["goal_dist_mae"]) <= 0.05, "strict track goal-distance drift is too large")
        require(
            float(aligned_debug["shared_action"]["linear_x_mae"]) <= 0.05,
            "strict track linear_x drift is too large",
        )
        require(
            float(aligned_debug["shared_action"]["angular_z_mae"]) <= 0.05,
            "strict track angular_z drift is too large",
        )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
