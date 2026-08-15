from __future__ import annotations

import copy
import importlib.util
import json
import pathlib
import sys
import tempfile
import unittest

PROJECT = pathlib.Path(__file__).resolve().parents[1]
MODULE_PATH = PROJECT / "video" / "render_t1_prototype.py"
SPEC = importlib.util.spec_from_file_location("render_t1_prototype", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
prototype = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = prototype
SPEC.loader.exec_module(prototype)
STAGE_MODULE_PATH = PROJECT / "video" / "stage_t1_scene.py"
STAGE_SPEC = importlib.util.spec_from_file_location("stage_t1_scene", STAGE_MODULE_PATH)
assert STAGE_SPEC is not None and STAGE_SPEC.loader is not None
staging = importlib.util.module_from_spec(STAGE_SPEC)
sys.modules[STAGE_SPEC.name] = staging
STAGE_SPEC.loader.exec_module(staging)


def event(sequence: int, unix_ms: int, event_type: str, data: dict) -> dict:
    return {
        "schema": "mbt.evt.v1",
        "run_id": "paper-t1",
        "seq": sequence,
        "unix_ms": unix_ms,
        "type": event_type,
        "data": data,
    }


def accepted_t1_events() -> list[dict]:
    return [
        event(1, 1_001_000, "run_start", {}),
        event(
            2,
            1_002_000,
            "bb_write",
            {"key": "ball-state", "preview": [0.5, 0.0, 0.0]},
        ),
        event(
            3,
            1_004_000,
            "vla_submit",
            {
                "job_id": "1",
                "generation": 1,
                "captured_context_id": "ball-0001",
            },
        ),
        event(
            4,
            1_006_500,
            "vla_result",
            {
                "job_id": "1",
                "generation": 1,
                "captured_context_id": "ball-0001",
                "current_context_id": "ball-0001",
                "decision": "accepted",
            },
        ),
        event(
            5,
            1_006_502,
            "walking_target_dispatch",
            {
                "job_id": "1",
                "generation": 1,
                "captured_context_id": "ball-0001",
                "current_context_id": "ball-0001",
                "decision": "accepted",
                "target": {
                    "frame_id": "ball_context",
                    "x_m": -0.45,
                    "y_m": 0.08,
                    "yaw_rad": 0.0,
                },
            },
        ),
        event(6, 1_006_600, "run_end", {}),
    ]


def shot() -> dict:
    return {
        "duration_seconds": 11.0,
        "request_lead_seconds": 3.0,
        "output": {"width": 1920, "height": 1080, "fps": 30},
        "fixed_camera_pixel_calibration": {
            "robot_start": [800, 700],
            "current_target": [960, 720],
        },
    }


class T1PrototypeTests(unittest.TestCase):
    def test_scene_staging_accepts_the_t2_comparison_schema(self) -> None:
        configured_shot = shot()
        configured_shot["schema_version"] = (
            "humanoid.paper_video_comparison.v1"
        )
        with tempfile.TemporaryDirectory() as temporary_directory:
            path = pathlib.Path(temporary_directory) / "shot.json"
            path.write_text(json.dumps(configured_shot), encoding="utf-8")

            loaded = staging._read_shot(path)

        self.assertEqual(
            loaded["schema_version"], "humanoid.paper_video_comparison.v1"
        )

    def test_scene_staging_uses_the_frozen_robot_and_ball_poses(self) -> None:
        configured_shot = shot()
        configured_shot["simulator_scene"] = {
            "robot_name": "robot1",
            "robot_body_id": 3,
            "ball_body_id": 141,
            "robot_start_position_m": [-0.6, 0.0, 0.545],
            "robot_start_quaternion_wxyz": [1.0, 0.0, 0.0, 0.0],
            "ball_position_m": [0.5, 0.0, 0.11],
            "reset_wait_seconds": 8.0,
        }

        reset_wait, commands = staging.build_stage_commands(configured_shot)

        self.assertEqual(reset_wait, 8.0)
        self.assertEqual(commands[0]["command"], "reset_robot")
        self.assertEqual(commands[0]["params"]["robot_name"], "robot1")
        self.assertEqual(commands[1]["params"]["position"], [-0.6, 0.0, 0.545])
        self.assertEqual(commands[4]["params"]["body_id"], 141)
        self.assertEqual(commands[4]["params"]["position"], [0.5, 0.0, 0.11])

    def test_timeline_aligns_capture_and_derives_field_target(self) -> None:
        capture = {
            "schema_version": "humanoid.clean_simulator_capture.v1",
            "first_frame_epoch": 1000.0,
            "last_frame_epoch": 1020.0,
        }
        timeline = prototype.build_timeline(accepted_t1_events(), capture, shot())

        self.assertAlmostEqual(timeline.clip_start_seconds, 1.0)
        self.assertAlmostEqual(timeline.submit_seconds, 3.0)
        self.assertAlmostEqual(timeline.accept_seconds, 5.5)
        self.assertAlmostEqual(timeline.dispatch_seconds, 5.502)
        self.assertEqual(timeline.context_id, "ball-0001")
        for observed, expected in zip(
            timeline.target_field_position_m, (0.05, 0.08, 0.0), strict=True
        ):
            self.assertAlmostEqual(observed, expected)

        overlay = prototype.generate_ass(timeline, shot())
        self.assertIn("MODEL REQUEST PENDING", overlay)
        self.assertIn("CURRENT RESULT ACCEPTED", overlay)
        self.assertIn("WALKING TARGET DISPATCHED EXACTLY ONCE", overlay)
        self.assertNotIn("BALL A", overlay)
        self.assertNotIn("CONTEXT A", overlay)
        self.assertIn(r"\pos(960,720)", overlay)

    def test_timeline_rejects_a_changed_context(self) -> None:
        events = copy.deepcopy(accepted_t1_events())
        events[3]["data"]["current_context_id"] = "ball-0002"
        capture = {
            "schema_version": "humanoid.clean_simulator_capture.v1",
            "first_frame_epoch": 1000.0,
            "last_frame_epoch": 1020.0,
        }

        with self.assertRaisesRegex(
            prototype.PrototypeError, "not current and invocation-correlated"
        ):
            prototype.build_timeline(events, capture, shot())


if __name__ == "__main__":
    unittest.main()
