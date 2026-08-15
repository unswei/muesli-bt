from __future__ import annotations

import json
import socket
import sys
import tempfile
import time
import unittest
from pathlib import Path

PROJECT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT / "src"))

from muesli_booster.adapter import (
    DISPATCH_REQUEST_SCHEMA,
    AdapterConfig,
    AdapterState,
    BallContextTracker,
    BallObservation,
    RobotPose,
)
from muesli_booster.bridge_server import BridgeServer
from muesli_booster.runtime import _default_payload_root


def request(context_id: str = "ball-0001", generation: int = 1) -> dict:
    return {
        "op": "dispatch",
        "schema_version": DISPATCH_REQUEST_SCHEMA,
        "job_id": "job-1",
        "generation": generation,
        "captured_context_id": context_id,
        "target": {
            "frame_id": "ball_context",
            "x_m": -0.45,
            "y_m": 0.08,
            "yaw_rad": 0.0,
        },
    }


class BallContextTrackerTests(unittest.TestCase):
    def test_movement_loss_and_reacquisition_advance_context(self) -> None:
        tracker = BallContextTracker(0.15, 0.5)
        self.assertEqual(
            tracker.observe(BallObservation(1.0, 0.0, 0.0, 10.0)), "ball-0001"
        )
        self.assertEqual(
            tracker.observe(BallObservation(1.1, 0.0, 0.0, 10.1)), "ball-0001"
        )
        self.assertEqual(
            tracker.observe(BallObservation(1.3, 0.0, 0.0, 10.2)), "ball-0002"
        )
        tracker.mark_lost()
        self.assertEqual(
            tracker.observe(BallObservation(1.3, 0.0, 0.0, 10.3)), "ball-0003"
        )

    def test_stale_ball_is_unavailable(self) -> None:
        tracker = BallContextTracker(0.15, 0.5)
        tracker.observe(BallObservation(1.0, 0.0, 0.0, 10.0))
        snapshot = tracker.snapshot(
            now=10.6, robot_pose=None, robot_stable=True, emergency=False
        )
        self.assertFalse(snapshot.ball_available)
        self.assertIsNone(snapshot.ball)

    def test_small_steps_accumulate_against_context_anchor(self) -> None:
        tracker = BallContextTracker(0.15, 0.5)
        self.assertEqual(
            tracker.observe(BallObservation(1.0, 0.0, 0.0, 10.0)), "ball-0001"
        )
        self.assertEqual(
            tracker.observe(BallObservation(1.1, 0.0, 0.0, 10.1)), "ball-0001"
        )
        self.assertEqual(
            tracker.observe(BallObservation(1.16, 0.0, 0.0, 10.2)), "ball-0002"
        )

    def test_context_anchors_are_retained_only_within_a_continuous_track(
        self,
    ) -> None:
        tracker = BallContextTracker(0.15, 0.5)
        first = BallObservation(1.0, 0.0, 0.0, 10.0)
        tracker.observe(first)
        tracker.observe(BallObservation(1.3, 0.0, 0.0, 10.1))
        self.assertEqual(tracker.context_anchor("ball-0001"), first)

        tracker.mark_lost()

        self.assertIsNone(tracker.context_anchor("ball-0001"))


class DispatchAndFollowerTests(unittest.TestCase):
    def ready_state(self, *, motion_enabled: bool = True) -> AdapterState:
        state = AdapterState(AdapterConfig(motion_enabled=motion_enabled))
        state.observe_ball(BallObservation(1.2, -0.35, 0.0, 10.0))
        state.observe_robot(RobotPose(0.0, 0.0, 0.0, 10.0))
        state.set_robot_stable(True)
        return state

    def test_dispatch_transforms_target_and_is_exactly_once(self) -> None:
        state = self.ready_state()
        first = state.dispatch(request(), 10.1)
        self.assertTrue(first.accepted)
        self.assertAlmostEqual(first.field_target.x_m, 0.75)
        self.assertAlmostEqual(first.field_target.y_m, -0.27)
        duplicate = state.dispatch(request(), 10.2)
        self.assertFalse(duplicate.accepted)
        self.assertEqual(duplicate.reason, "duplicate_dispatch")

    def test_new_trial_clears_dispatch_identity_and_active_motion(self) -> None:
        state = self.ready_state()
        self.assertTrue(state.dispatch(request(), 10.1).accepted)
        state.prepare_trial()
        self.assertEqual(state.velocity_command(10.2).reason, "no_target")
        self.assertTrue(state.dispatch(request(), 10.2).accepted)

    def test_context_change_and_emergency_reject(self) -> None:
        state = self.ready_state()
        state.observe_ball(BallObservation(1.5, -0.35, 0.0, 10.1))
        stale = state.dispatch(request(), 10.2)
        self.assertEqual(stale.reason, "context_changed")
        state.set_emergency(True)
        emergency = state.dispatch(request("ball-0002", 2), 10.2)
        self.assertEqual(emergency.reason, "robot_unstable")

    def test_explicit_unsafe_simulation_override_dispatches_obsolete_target(
        self,
    ) -> None:
        state = self.ready_state()
        state.observe_ball(BallObservation(1.5, -0.35, 0.0, 10.1))
        state.prepare_trial(unsafe_simulation_stale_dispatch=True)

        stale = state.dispatch(request(), 10.2)

        self.assertTrue(stale.accepted)
        self.assertAlmostEqual(stale.field_target.x_m, 0.75)
        self.assertAlmostEqual(stale.field_target.y_m, -0.27)
        self.assertEqual(state.velocity_command(10.2).reason, "walking")

    def test_unsafe_simulation_override_cannot_cross_ball_track_loss(self) -> None:
        state = self.ready_state()
        state.prepare_trial(unsafe_simulation_stale_dispatch=True)
        state.mark_ball_lost()
        state.observe_ball(BallObservation(1.5, -0.35, 0.0, 10.1))

        stale = state.dispatch(request(), 10.2)

        self.assertFalse(stale.accepted)
        self.assertEqual(stale.reason, "context_changed")

    def test_motion_is_disabled_by_default(self) -> None:
        state = self.ready_state(motion_enabled=False)
        outcome = state.dispatch(request(), 10.1)
        self.assertFalse(outcome.accepted)
        self.assertEqual(outcome.reason, "motion_disabled")

    def test_operator_arming_enables_dispatch_and_disarming_revokes_motion(
        self,
    ) -> None:
        state = self.ready_state(motion_enabled=False)
        state.set_motion_enabled(True)
        self.assertTrue(state.snapshot_payload(10.1)["motion_enabled"])
        self.assertTrue(state.dispatch(request(), 10.1).accepted)

        state.set_motion_enabled(False)

        self.assertFalse(state.snapshot_payload(10.2)["motion_enabled"])
        self.assertEqual(state.velocity_command(10.2).reason, "no_target")
        self.assertEqual(
            state.dispatch(request(generation=2), 10.2).reason, "motion_disabled"
        )

    def test_stale_robot_pose_and_non_numeric_target_reject(self) -> None:
        state = self.ready_state()
        state.observe_ball(BallObservation(1.2, -0.35, 0.0, 10.4))
        stale = state.dispatch(request(), 10.55)
        self.assertEqual(stale.reason, "robot_pose_stale")
        state = self.ready_state()
        malformed = request()
        malformed["target"]["x_m"] = True
        invalid = state.dispatch(malformed, 10.1)
        self.assertEqual(invalid.reason, "invalid_schema")

    def test_follower_bounds_velocity_and_stops_on_context_change(self) -> None:
        state = self.ready_state()
        self.assertTrue(state.dispatch(request(), 10.1).accepted)
        command = state.velocity_command(10.2)
        self.assertLessEqual((command.vx_mps**2 + command.vy_mps**2) ** 0.5, 0.35)
        self.assertEqual(command.reason, "walking")
        state.observe_ball(BallObservation(1.5, -0.35, 0.0, 10.3))
        stopped = state.velocity_command(10.3)
        self.assertEqual(
            (stopped.vx_mps, stopped.vy_mps, stopped.vyaw_rps), (0.0, 0.0, 0.0)
        )
        self.assertEqual(stopped.reason, "context_changed")


@unittest.skipUnless(hasattr(socket, "AF_UNIX"), "Unix sockets are unavailable")
class BridgeServerTests(unittest.TestCase):
    def test_snapshot_and_dispatch_round_trip(self) -> None:
        state = AdapterState(AdapterConfig(motion_enabled=True))
        now = time.monotonic()
        state.observe_ball(BallObservation(1.2, -0.35, 0.0, now))
        state.observe_robot(RobotPose(0.0, 0.0, 0.0, now))
        state.set_robot_stable(True)
        with tempfile.TemporaryDirectory() as directory:
            path = str(Path(directory) / "bridge.sock")
            server = BridgeServer(path, state)
            server.start()
            try:
                snapshot = self._exchange(path, {"op": "snapshot"})
                self.assertEqual(snapshot["ball_context_id"], "ball-0001")
                response = self._exchange(path, request())
                self.assertTrue(response["accepted"])
            finally:
                server.stop()

    @staticmethod
    def _exchange(path: str, payload: dict) -> dict:
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
            client.settimeout(2.0)
            client.connect(path)
            client.sendall(json.dumps(payload).encode("utf-8") + b"\n")
            response = client.makefile("rb").readline()
        return json.loads(response)


class ManifestTests(unittest.TestCase):
    def test_required_booster_files_and_fail_closed_default(self) -> None:
        for relative in (
            "agent.toml",
            "build.toml",
            ".booster-studio/project.json",
            "res/logo.png",
            "src/main.py",
        ):
            self.assertTrue((PROJECT / relative).is_file(), relative)
        agent = (PROJECT / "agent.toml").read_text(encoding="utf-8")
        self.assertIn('entry = "src/main.py:MuesliHumanoidAgent"', agent)
        self.assertIn('models = [ "Booster K1" ]', agent)
        runtime = (PROJECT / "src/muesli_booster/runtime.py").read_text(
            encoding="utf-8"
        )
        self.assertIn('"MUESLI_BOOSTER_MOTION_ENABLED", False', runtime)
        self.assertIn('"MUESLI_BOOSTER_UNSAFE_SIM_BASELINE", False', runtime)
        self.assertIn('"MUESLI_BOOSTER_GAIT", "default"', runtime)
        self.assertIn('Bool, "/muesli/motion_arm"', runtime)
        self.assertIn('String, "/muesli/trial_command"', runtime)
        entry = (PROJECT / "src/main.py").read_text(encoding="utf-8")
        for component_id in (
            "motion_arm",
            "trial_t1",
            "trial_t2a",
            "trial_t2b",
            "trial_t3",
            "software_emergency",
        ):
            self.assertIn(f'"{component_id}"', entry)

    def test_default_payload_root_finds_packaged_resource(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            install_root = Path(directory)
            module = (
                install_root
                / "agent/example/lib/python3.10/site-packages/example/muesli_booster/runtime.py"
            )
            module.parent.mkdir(parents=True)
            module.touch()
            payload = install_root / "res/native_payload"
            payload.mkdir(parents=True)
            (payload / "manifest.json").write_text("{}\n", encoding="utf-8")

            resolved = _default_payload_root(module, install_root / "agent")

            self.assertEqual(resolved, payload.resolve())


if __name__ == "__main__":
    unittest.main()
