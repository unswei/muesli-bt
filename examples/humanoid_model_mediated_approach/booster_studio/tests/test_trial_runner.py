from __future__ import annotations

import io
import json
import pathlib
import struct
import sys
import tempfile
import time
import unittest

try:
    import jsonschema
except (
    ImportError
):  # pragma: no cover - optional outside the repository test environment
    jsonschema = None


PROJECT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT / "src"))
sys.path.insert(0, str(PROJECT / "tools"))

import build_native_payload as payload_builder
from muesli_booster.adapter import (
    AdapterConfig,
    AdapterState,
    BallObservation,
    RobotPose,
)
from muesli_booster.native_payload import verify_payload
from muesli_booster.trial_runner import (
    NativeTrialSupervisor,
    TrialError,
    build_trial_command,
)


def write_elf(path: pathlib.Path) -> None:
    header = bytearray(64)
    header[:4] = b"\x7fELF"
    header[4:7] = bytes((2, 1, 1))
    struct.pack_into("<H", header, 16, 2)
    struct.pack_into("<H", header, 18, 62)
    path.write_bytes(header + b"trial-runner-test")
    path.chmod(0o755)


def ready_state() -> AdapterState:
    state = AdapterState(AdapterConfig(motion_enabled=True))
    now = time.monotonic()
    state.observe_ball(BallObservation(1.2, -0.35, 0.0, now))
    state.observe_robot(RobotPose(0.0, 0.0, 0.0, now))
    state.set_robot_stable(True)
    return state


class Logger:
    def __init__(self) -> None:
        self.info_messages: list[str] = []
        self.error_messages: list[str] = []

    def info(self, message: str) -> None:
        self.info_messages.append(message)

    def error(self, message: str) -> None:
        self.error_messages.append(message)


class FinishedProcess:
    def __init__(self, return_code: int, output: str = "") -> None:
        self.returncode = return_code
        self.stdout = io.StringIO(output)

    def poll(self) -> int:
        return self.returncode

    def wait(self, timeout: float | None = None) -> int:
        del timeout
        return self.returncode

    def terminate(self) -> None:
        pass

    def kill(self) -> None:
        pass


class NativeTrialSupervisorTests(unittest.TestCase):
    def stage_payload(self, root: pathlib.Path) -> pathlib.Path:
        runner = root / "runner"
        write_elf(runner)
        payload_root = root / "payload"
        payload_builder.stage_payload(runner, payload_root)
        return payload_root

    def test_command_is_bound_to_live_bridge_and_frozen_trial(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            verified = verify_payload(self.stage_payload(root))
            command = build_trial_command(
                verified,
                trial_id="T2b",
                run_id="test-t2b",
                event_path=root / "events.jsonl",
                bridge_socket="/tmp/test-bridge.sock",
                motion_enabled=True,
            )
            self.assertEqual(command[command.index("--intervention") + 1], "moved_ball")
            self.assertEqual(
                command[command.index("--acceptance-policy") + 1], "invocation_scoped"
            )
            self.assertEqual(
                command[command.index("--booster-bridge-socket") + 1],
                "/tmp/test-bridge.sock",
            )
            self.assertEqual(command[command.index("--delay-ms") + 1], "2500")

    def test_success_writes_bound_evidence_manifest(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            payload_root = self.stage_payload(root)
            logger = Logger()
            state = ready_state()

            def factory(command: list[str], **_: object) -> FinishedProcess:
                event_path = pathlib.Path(command[command.index("--events") + 1])
                event_path.write_text(
                    '{"schema":"mbt.evt.v1","type":"run_start","seq":1,"data":{}}\n',
                    encoding="utf-8",
                )
                outcome = state.dispatch(
                    {
                        "schema_version": "humanoid.booster_dispatch_request.v1",
                        "job_id": "job-1",
                        "generation": 1,
                        "captured_context_id": "ball-0001",
                        "target": {
                            "frame_id": "ball_context",
                            "x_m": -0.45,
                            "y_m": 0.08,
                            "yaw_rad": 0.0,
                        },
                    },
                    time.monotonic(),
                )
                self.assertTrue(outcome.accepted)
                return FinishedProcess(0, "REQUEST_SUBMITTED trial=T1\n")

            supervisor = NativeTrialSupervisor(
                payload_root=payload_root,
                evidence_root=root / "evidence",
                bridge_socket="/tmp/test-bridge.sock",
                state=state,
                logger=logger,
                process_factory=factory,
            )
            run_dir = supervisor.start("T1", "test-t1")
            for _ in range(100):
                manifest = json.loads(
                    (run_dir / "live-manifest.json").read_text(encoding="utf-8")
                )
                if manifest["status"] != "running":
                    break
                time.sleep(0.005)

            self.assertEqual(manifest["status"], "completed")
            self.assertEqual(manifest["return_code"], 0)
            self.assertIn("sha256", manifest["event_log"])
            self.assertTrue(
                any("REQUEST_SUBMITTED" in item for item in logger.info_messages)
            )
            self.assertNotEqual(
                state.velocity_command(time.monotonic()).reason, "no_target"
            )
            if jsonschema is not None:
                schema_path = (
                    PROJECT.parents[2]
                    / "schemas/humanoid_booster/v1/live-trial.schema.json"
                )
                schema = json.loads(schema_path.read_text(encoding="utf-8"))
                jsonschema.Draft202012Validator(schema).validate(manifest)

    def test_unexpected_failure_latches_emergency(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            state = ready_state()
            supervisor = NativeTrialSupervisor(
                payload_root=self.stage_payload(root),
                evidence_root=root / "evidence",
                bridge_socket="/tmp/test-bridge.sock",
                state=state,
                logger=Logger(),
                process_factory=lambda *args, **kwargs: FinishedProcess(9, "failed\n"),
            )
            supervisor.start("T3", "test-t3")
            for _ in range(100):
                if state.snapshot(time.monotonic()).emergency:
                    break
                time.sleep(0.005)
            state.set_emergency(False)
            self.assertTrue(state.snapshot(time.monotonic()).emergency)

    def test_unsafe_simulation_baseline_is_explicit_and_t2a_only(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            payload_root = self.stage_payload(root)

            def run(trial_id: str, expected_acceptance: bool) -> dict[str, object]:
                state = ready_state()
                outcome: dict[str, object] = {}

                def factory(command: list[str], **_: object) -> FinishedProcess:
                    event_path = pathlib.Path(command[command.index("--events") + 1])
                    event_path.write_text(
                        '{"schema":"mbt.evt.v1","type":"run_start","seq":1,'
                        '"data":{}}\n',
                        encoding="utf-8",
                    )
                    state.observe_ball(
                        BallObservation(1.5, -0.35, 0.0, time.monotonic())
                    )
                    dispatch = state.dispatch(
                        {
                            "schema_version": (
                                "humanoid.booster_dispatch_request.v1"
                            ),
                            "job_id": "job-1",
                            "generation": 1,
                            "captured_context_id": "ball-0001",
                            "target": {
                                "frame_id": "ball_context",
                                "x_m": -0.45,
                                "y_m": 0.08,
                                "yaw_rad": 0.0,
                            },
                        },
                        time.monotonic(),
                    )
                    outcome["accepted"] = dispatch.accepted
                    outcome["reason"] = dispatch.reason
                    if dispatch.field_target is not None:
                        outcome["target_x_m"] = dispatch.field_target.x_m
                    return FinishedProcess(0)

                supervisor = NativeTrialSupervisor(
                    payload_root=payload_root,
                    evidence_root=root / f"evidence-{trial_id}",
                    bridge_socket="/tmp/test-bridge.sock",
                    state=state,
                    logger=Logger(),
                    process_factory=factory,
                    unsafe_simulation_baseline_enabled=True,
                )
                run_dir = supervisor.start(trial_id, f"test-{trial_id.lower()}")
                for _ in range(100):
                    manifest = json.loads(
                        (run_dir / "live-manifest.json").read_text(
                            encoding="utf-8"
                        )
                    )
                    if manifest["status"] != "running":
                        break
                    time.sleep(0.005)
                self.assertEqual(outcome["accepted"], expected_acceptance)
                return manifest

            baseline_manifest = run("T2a", True)
            full_manifest = run("T2b", False)

            self.assertEqual(
                baseline_manifest["safety_profile"],
                "unsafe_simulation_baseline",
            )
            self.assertEqual(
                full_manifest["safety_profile"], "full_host_envelope"
            )

    def test_disarmed_host_cannot_start_trial(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            state = ready_state()
            state.set_motion_enabled(False)
            supervisor = NativeTrialSupervisor(
                payload_root=self.stage_payload(root),
                evidence_root=root / "evidence",
                bridge_socket="/tmp/test-bridge.sock",
                state=state,
                logger=Logger(),
            )

            with self.assertRaisesRegex(TrialError, "require armed motion"):
                supervisor.start("T1", "disarmed")


if __name__ == "__main__":
    unittest.main()
