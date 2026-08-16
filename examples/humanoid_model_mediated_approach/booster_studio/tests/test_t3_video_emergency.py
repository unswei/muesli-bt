from __future__ import annotations

import copy
import importlib.util
import json
import pathlib
import sys
import tempfile
import unittest

PROJECT = pathlib.Path(__file__).resolve().parents[1]
MODULE_PATH = PROJECT / "video" / "render_t3_emergency.py"
SPEC = importlib.util.spec_from_file_location("render_t3_emergency", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
emergency_video = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = emergency_video
SPEC.loader.exec_module(emergency_video)


def event(
    sequence: int,
    unix_ms: int,
    event_type: str,
    data: dict,
    *,
    tick: int,
) -> dict:
    return {
        "schema": "mbt.evt.v1",
        "run_id": "paper-t3",
        "seq": sequence,
        "tick": tick,
        "unix_ms": unix_ms,
        "type": event_type,
        "data": data,
    }


def t3_events() -> list[dict]:
    return [
        event(1, 1_001_000, "run_start", {}, tick=0),
        event(
            2,
            1_002_000,
            "bb_write",
            {"key": "ball-state", "preview": [0.5, 0.0, 0.0]},
            tick=1,
        ),
        event(
            3,
            1_002_000,
            "bb_write",
            {"key": "robot-stable", "preview": True},
            tick=1,
        ),
        event(
            4,
            1_002_000,
            "bb_write",
            {"key": "emergency", "preview": False},
            tick=1,
        ),
        event(
            5,
            1_004_000,
            "bb_write",
            {"key": "active-branch", "preview": "model_wait"},
            tick=1,
        ),
        event(
            6,
            1_004_000,
            "bb_write",
            {"key": "request-state", "preview": "running"},
            tick=1,
        ),
        event(
            7,
            1_004_000,
            "vla_submit",
            {
                "acceptance_policy": "invocation_scoped",
                "job_id": "1",
                "generation": 1,
                "captured_context_id": "ball-0001",
            },
            tick=1,
        ),
        event(
            8,
            1_005_100,
            "bb_write",
            {"key": "robot-stable", "preview": False},
            tick=23,
        ),
        event(
            9,
            1_005_100,
            "bb_write",
            {"key": "emergency", "preview": True},
            tick=23,
        ),
        event(
            10,
            1_005_100,
            "bb_write",
            {"key": "active-branch", "preview": "safe_stand"},
            tick=23,
        ),
        event(
            11,
            1_005_100,
            "bb_write",
            {"key": "request-state", "preview": "revoked"},
            tick=23,
        ),
        event(
            12,
            1_005_100,
            "bb_write",
            {"key": "walking-target-state", "preview": "none"},
            tick=23,
        ),
        event(
            13,
            1_005_100,
            "async_authority_revoked",
            {
                "acceptance_policy": "invocation_scoped",
                "job_id": "1",
                "generation": 1,
                "captured_context_id": "ball-0001",
                "authority_state": "revoked",
                "reason": "branch_revoked",
            },
            tick=23,
        ),
        event(
            14,
            1_006_500,
            "vla_result",
            {
                "status": "cancelled",
                "record": {
                    "status": "cancelled",
                    "completion_dropped": True,
                    "response": {
                        "action": {
                            "frame_id": "ball_context",
                            "u": [-0.45, 0.08, 0.0],
                        }
                    },
                },
            },
            tick=1,
        ),
        event(
            15,
            1_006_500,
            "async_completion_dropped",
            {"reason": "completion_after_cancel"},
            tick=1,
        ),
        event(
            16,
            1_006_600,
            "run_end",
            {
                "status": "complete",
                "trial_id": "T3",
                "recording_dispatch_calls": 0,
            },
            tick=51,
        ),
    ]


def capture() -> dict:
    return {
        "schema_version": "humanoid.clean_simulator_capture.v1",
        "first_frame_epoch": 1000.0,
        "last_frame_epoch": 1020.0,
    }


def shot() -> dict:
    return {
        "duration_seconds": 14.0,
        "request_lead_seconds": 3.0,
        "output": {"width": 1920, "height": 1080, "fps": 30},
        "fixed_camera_pixel_calibration": {
            "robot_start": [837, 706],
            "revoked_target": [963, 720],
        },
    }


class T3EmergencyVideoTests(unittest.TestCase):
    def test_canonical_envelope_allows_non_tick_metadata_events(self) -> None:
        events = t3_events()
        events[0].pop("tick")
        with tempfile.TemporaryDirectory() as temporary_directory:
            path = pathlib.Path(temporary_directory) / "events.jsonl"
            path.write_text(
                "".join(json.dumps(item) + "\n" for item in events),
                encoding="utf-8",
            )

            loaded = emergency_video._load_events(path)

        self.assertEqual(len(loaded), len(events))

    def test_timeline_proves_immediate_revocation_and_late_drop(self) -> None:
        timeline = emergency_video.build_timeline(t3_events(), capture(), shot())

        self.assertAlmostEqual(timeline.submit_seconds, 3.0)
        self.assertAlmostEqual(timeline.emergency_seconds, 4.1)
        self.assertAlmostEqual(timeline.completion_seconds, 5.5)
        self.assertEqual(timeline.safe_stand_tick_delta, 0)
        self.assertEqual(timeline.authority_reason, "branch_revoked")
        self.assertEqual(timeline.completion_reason, "completion_after_cancel")
        self.assertEqual(timeline.recording_dispatch_calls, 0)
        for observed, expected in zip(
            timeline.revoked_target_field_position_m,
            (0.05, 0.08, 0.0),
            strict=True,
        ):
            self.assertAlmostEqual(observed, expected)

        overlay = emergency_video.generate_ass(timeline, shot())
        self.assertIn("LIVE BEHAVIOUR TREE", overlay)
        self.assertIn("1  SAFE STAND  ·  ACTIVE", overlay)
        self.assertIn("2  MODEL WAIT  ·  REVOKED", overlay)
        self.assertIn("GHOST TARGET  ·  NEVER DISPATCHED", overlay)
        self.assertIn("WALK DISPATCH COUNT 0", overlay)
        self.assertNotIn("BALL A", overlay)

    def test_rejects_safe_stand_later_than_one_tick(self) -> None:
        events = copy.deepcopy(t3_events())
        events[9]["tick"] = 25

        with self.assertRaisesRegex(
            emergency_video.EmergencyVideoError, "within one BT tick"
        ):
            emergency_video.build_timeline(events, capture(), shot())

    def test_rejects_any_walking_target_dispatch(self) -> None:
        events = copy.deepcopy(t3_events())
        events[-1]["seq"] = 17
        events.insert(
            -1,
            event(
                16,
                1_006_550,
                "walking_target_dispatch",
                {"decision": "rejected"},
                tick=51,
            ),
        )

        with self.assertRaisesRegex(
            emergency_video.EmergencyVideoError,
            "zero walking-target dispatches",
        ):
            emergency_video.build_timeline(events, capture(), shot())

    def test_rejects_a_completion_that_was_not_dropped_after_cancel(self) -> None:
        events = copy.deepcopy(t3_events())
        events[14]["data"]["reason"] = "duplicate_completion"

        with self.assertRaisesRegex(
            emergency_video.EmergencyVideoError, "dropped after authority revocation"
        ):
            emergency_video.build_timeline(events, capture(), shot())

    def test_live_manifest_requires_full_host_safety_envelope(self) -> None:
        timeline = emergency_video.build_timeline(t3_events(), capture(), shot())
        manifest = {
            "schema_version": "humanoid.booster_live_trial.v1",
            "status": "completed",
            "return_code": 0,
            "trial_id": "T3",
            "run_id": timeline.run_id,
            "safety_profile": "unsafe_simulation_baseline",
            "event_log": {"sha256": "sha256:events"},
        }

        with self.assertRaisesRegex(
            emergency_video.EmergencyVideoError, "full host safety envelope"
        ):
            emergency_video.validate_live_manifest(manifest, timeline, "events")


if __name__ == "__main__":
    unittest.main()
