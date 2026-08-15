from __future__ import annotations

import copy
import importlib.util
import pathlib
import sys
import unittest

PROJECT = pathlib.Path(__file__).resolve().parents[1]
MODULE_PATH = PROJECT / "video" / "render_t2_comparison.py"
SPEC = importlib.util.spec_from_file_location("render_t2_comparison", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
comparison = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = comparison
SPEC.loader.exec_module(comparison)


def event(sequence: int, unix_ms: int, event_type: str, data: dict) -> dict:
    return {
        "schema": "mbt.evt.v1",
        "run_id": "paper-t2",
        "seq": sequence,
        "unix_ms": unix_ms,
        "type": event_type,
        "data": data,
    }


def t2_events(role: str) -> list[dict]:
    policy = "deadline_only" if role == "baseline" else "invocation_scoped"
    trial_id = "T2a" if role == "baseline" else "T2b"
    decision = "accepted" if role == "baseline" else "rejected"
    reason = "" if role == "baseline" else "context_changed"
    events = [
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
                "acceptance_policy": policy,
                "job_id": "1",
                "generation": 1,
                "captured_context_id": "ball-0001",
            },
        ),
        event(
            4,
            1_005_100,
            "bb_write",
            {"key": "ball-context-id", "preview": "ball-0002"},
        ),
        event(
            5,
            1_005_100,
            "bb_write",
            {"key": "ball-state", "preview": [1.1, 0.65, 0.0]},
        ),
        event(
            6,
            1_006_500,
            "vla_result",
            {
                "acceptance_policy": policy,
                "job_id": "1",
                "generation": 1,
                "captured_context_id": "ball-0001",
                "current_context_id": "ball-0002",
                "decision": decision,
                "reason": reason,
            },
        ),
    ]
    if role == "baseline":
        events.append(
            event(
                7,
                1_006_502,
                "walking_target_dispatch",
                {
                    "job_id": "1",
                    "generation": 1,
                    "captured_context_id": "ball-0001",
                    "current_context_id": "ball-0002",
                    "decision": "accepted",
                    "reason": "",
                    "target": {
                        "frame_id": "ball_context",
                        "x_m": -0.45,
                        "y_m": 0.08,
                        "yaw_rad": 0.0,
                    },
                },
            )
        )
    else:
        events.append(
            event(
                7,
                1_006_502,
                "bb_write",
                {
                    "key": "candidate-walking-target",
                    "preview": [-0.45, 0.08, 0.0],
                },
            )
        )
    events.append(
        event(
            8,
            1_006_600,
            "run_end",
            {
                "trial_id": trial_id,
                "recording_dispatch_calls": 1 if role == "baseline" else 0,
            },
        )
    )
    return events


def capture() -> dict:
    return {
        "schema_version": "humanoid.clean_simulator_capture.v1",
        "first_frame_epoch": 1000.0,
        "last_frame_epoch": 1020.0,
    }


def recovery_events(context_id: str = "ball-0002") -> list[dict]:
    return [
        event(1, 1_006_800, "run_start", {}),
        event(
            2,
            1_006_900,
            "bb_write",
            {"key": "ball-state", "preview": [1.1, 0.65, 0.0]},
        ),
        event(
            3,
            1_007_000,
            "vla_submit",
            {
                "acceptance_policy": "invocation_scoped",
                "job_id": "1",
                "generation": 1,
                "captured_context_id": context_id,
            },
        ),
        event(
            4,
            1_009_500,
            "vla_result",
            {
                "acceptance_policy": "invocation_scoped",
                "job_id": "1",
                "generation": 1,
                "captured_context_id": context_id,
                "current_context_id": context_id,
                "decision": "accepted",
                "reason": "",
            },
        ),
        event(
            5,
            1_009_502,
            "walking_target_dispatch",
            {
                "job_id": "1",
                "generation": 1,
                "captured_context_id": context_id,
                "current_context_id": context_id,
                "decision": "accepted",
                "reason": "",
                "target": {
                    "frame_id": "ball_context",
                    "x_m": -0.45,
                    "y_m": 0.08,
                    "yaw_rad": 0.0,
                },
            },
        ),
        event(
            6,
            1_009_600,
            "run_end",
            {"trial_id": "T1", "recording_dispatch_calls": 1},
        ),
    ]


def shot() -> dict:
    return {
        "duration_seconds": 11.0,
        "request_lead_seconds": 3.0,
        "output": {
            "width": 1920,
            "height": 1080,
            "panel_width": 960,
            "fps": 30,
        },
        "fixed_camera_panel_calibration": {
            "robot_start": [365, 706],
            "target_a": [483, 720],
            "target_b": [691, 701],
        },
    }


class T2ComparisonTests(unittest.TestCase):
    def test_matched_timelines_encode_the_baseline_and_full_outcomes(self) -> None:
        baseline = comparison.build_trial_timeline(
            t2_events("baseline"), capture(), shot(), role="baseline"
        )
        full = comparison.build_trial_timeline(
            t2_events("full"), capture(), shot(), role="full"
        )
        full_recovery = comparison.build_recovery_timeline(
            recovery_events(), capture(), full
        )
        comparison.validate_matched_trials(baseline, full, full_recovery)

        self.assertAlmostEqual(baseline.submit_seconds, 3.0)
        self.assertAlmostEqual(baseline.move_seconds, 4.1)
        self.assertAlmostEqual(baseline.decision_seconds, 5.5)
        self.assertEqual(baseline.runtime_decision, "accepted")
        self.assertEqual(baseline.dispatch_decision, "accepted")
        self.assertEqual(full.runtime_decision, "rejected")
        self.assertIsNone(full.dispatch_decision)
        self.assertEqual(full.runtime_reason, "context_changed")
        self.assertEqual(baseline.recording_dispatch_calls, 1)
        self.assertEqual(full_recovery.recording_dispatch_calls, 1)
        self.assertEqual(full_recovery.context_id, "ball-0002")
        self.assertAlmostEqual(full_recovery.submit_seconds, 6.0)
        self.assertAlmostEqual(full_recovery.accept_seconds, 8.5)
        for observed, expected in zip(
            baseline.target_a_field_position_m,
            (0.05, 0.08, 0.0),
            strict=True,
        ):
            self.assertAlmostEqual(observed, expected)

        overlay = comparison.generate_ass(baseline, full, full_recovery, shot())
        self.assertIn("STALE RESULT ACCEPTED  →  DISPATCHED", overlay)
        self.assertIn("STALE RESULT REJECTED  →  NO DISPATCH", overlay)
        self.assertIn("BASELINE WALKS TO OBSOLETE TARGET A", overlay)
        self.assertIn("FULL SYSTEM WALKS TO CURRENT TARGET B", overlay)
        self.assertNotIn("BALL A", overlay)
        self.assertNotIn("BALL B", overlay)
        self.assertIn(r"\pos(483,720)", overlay)
        self.assertIn(r"\pos(1443,720)", overlay)

    def test_baseline_refuses_a_runtime_rejection(self) -> None:
        events = copy.deepcopy(t2_events("baseline"))
        events[5]["data"]["decision"] = "rejected"
        events[5]["data"]["reason"] = "context_changed"

        with self.assertRaisesRegex(
            comparison.ComparisonError, "runtime must accept"
        ):
            comparison.build_trial_timeline(
                events, capture(), shot(), role="baseline"
            )

    def test_full_refuses_any_walking_target_dispatch(self) -> None:
        events = copy.deepcopy(t2_events("full"))
        events[6] = event(
            7,
            1_006_502,
            "walking_target_dispatch",
            {"decision": "rejected", "reason": "context_changed"},
        )

        with self.assertRaisesRegex(comparison.ComparisonError, "before.*dispatch"):
            comparison.build_trial_timeline(events, capture(), shot(), role="full")

    def test_baseline_requires_one_obsolete_backend_dispatch_call(self) -> None:
        events = copy.deepcopy(t2_events("baseline"))
        events[-1]["data"]["recording_dispatch_calls"] = 0

        with self.assertRaisesRegex(
            comparison.ComparisonError, "T2a must contain 1 backend dispatch calls"
        ):
            comparison.build_trial_timeline(
                events, capture(), shot(), role="baseline"
            )

    def test_baseline_manifest_must_declare_unsafe_simulation_profile(self) -> None:
        baseline = comparison.build_trial_timeline(
            t2_events("baseline"), capture(), shot(), role="baseline"
        )
        manifest = {
            "schema_version": "humanoid.booster_live_trial.v1",
            "status": "completed",
            "return_code": 0,
            "trial_id": "T2a",
            "run_id": baseline.run_id,
            "safety_profile": "full_host_envelope",
            "event_log": {"sha256": "sha256:events"},
        }

        with self.assertRaisesRegex(
            comparison.ComparisonError, "unsafe simulation profile"
        ):
            comparison.validate_unsafe_baseline_manifest(
                manifest, baseline, "events"
            )

    def test_recovery_refuses_a_request_for_the_old_context(self) -> None:
        baseline = comparison.build_trial_timeline(
            t2_events("baseline"), capture(), shot(), role="baseline"
        )

        with self.assertRaisesRegex(
            comparison.ComparisonError, "current ball context"
        ):
            comparison.build_recovery_timeline(
                recovery_events("ball-0001"), capture(), baseline
            )


if __name__ == "__main__":
    unittest.main()
