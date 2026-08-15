#!/usr/bin/env python3
"""Exercise the native humanoid runner through the SDK-independent bridge."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
EXAMPLE_ROOT = REPO_ROOT / "examples" / "humanoid_model_mediated_approach"
STUDIO_ROOT = EXAMPLE_ROOT / "booster_studio"
sys.path.insert(0, str(STUDIO_ROOT / "src"))

from muesli_booster.adapter import (  # noqa: E402
    AdapterConfig,
    AdapterState,
    BallObservation,
    RobotPose,
)
from muesli_booster.bridge_server import BridgeServer  # noqa: E402


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", required=True, type=Path)
    return parser.parse_args()


def load_events(path: Path) -> list[dict]:
    return [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()]


def main() -> int:
    args = parse_args()
    runner = args.runner.resolve()
    if not runner.is_file():
        raise RuntimeError(f"runner does not exist: {runner}")

    state = AdapterState(AdapterConfig(motion_enabled=True))
    now = time.monotonic()
    state.observe_ball(BallObservation(1.2, -0.35, 0.0, now))
    state.observe_robot(RobotPose(0.0, 0.0, 0.0, now))
    state.set_robot_stable(True)

    with tempfile.TemporaryDirectory(prefix="muesli-booster-runner-") as directory:
        root = Path(directory)
        socket_path = root / "bridge.sock"
        event_path = root / "events.jsonl"
        server = BridgeServer(str(socket_path), state)
        server.start()
        stop_refresh = threading.Event()

        def refresh_observations() -> None:
            while not stop_refresh.wait(0.05):
                observed_at = time.monotonic()
                state.observe_ball(BallObservation(1.2, -0.35, 0.0, observed_at))
                state.observe_robot(RobotPose(0.0, 0.0, 0.0, observed_at))

        refresh_thread = threading.Thread(target=refresh_observations, daemon=True)
        refresh_thread.start()
        try:
            completed = subprocess.run(
                [
                    str(runner),
                    "--tree",
                    str(EXAMPLE_ROOT / "lisp" / "bt_invocation_scoped.lisp"),
                    "--events",
                    str(event_path),
                    "--run-id",
                    "booster-bridge-runner-smoke",
                    "--trial-id",
                    "T1-live-bridge-smoke",
                    "--acceptance-policy",
                    "invocation_scoped",
                    "--intervention",
                    "none",
                    "--delay-ms",
                    "50",
                    "--deadline-ms",
                    "3500",
                    "--intervention-ms",
                    "10",
                    "--tick-hz",
                    "20",
                    "--platform",
                    "local-sdk-independent-bridge-test",
                    "--physical-motion-enabled",
                    "true",
                    "--booster-bridge-socket",
                    str(socket_path),
                    "--booster-bridge-timeout-ms",
                    "500",
                ],
                cwd=REPO_ROOT,
                check=False,
                capture_output=True,
                text=True,
                timeout=10,
            )
        finally:
            stop_refresh.set()
            refresh_thread.join(timeout=1)
            server.stop()

        if completed.returncode != 0:
            diagnostics: list[dict] = []
            if event_path.exists():
                diagnostics = [
                    event
                    for event in load_events(event_path)
                    if event.get("type")
                    in {
                        "vla_submit",
                        "vla_result",
                        "async_authority_revoked",
                        "async_completion_dropped",
                        "walking_target_dispatch",
                    }
                    or (
                        event.get("type") == "bb_write"
                        and event.get("data", {}).get("key")
                        in {"emergency", "robot-stable", "ball-available", "active-branch"}
                    )
                ]
            raise RuntimeError(
                f"runner failed ({completed.returncode}): {completed.stderr.strip()}\n"
                f"stdout: {completed.stdout.strip()}\ndiagnostics: {diagnostics[:40]}"
            )
        events = load_events(event_path)
        dispatches = [event for event in events if event.get("type") == "walking_target_dispatch"]
        if len(dispatches) != 1:
            raise RuntimeError(f"expected one walking dispatch, found {len(dispatches)}")
        data = dispatches[0].get("data", {})
        if data.get("decision") != "accepted" or data.get("dispatch_source") != "host_callback":
            raise RuntimeError(f"unexpected dispatch evidence: {data}")
        if data.get("captured_context_id") != "ball-0001" or data.get(
            "current_context_id"
        ) != "ball-0001":
            raise RuntimeError(f"invocation context was not preserved: {data}")
        starts = [event for event in events if event.get("type") == "run_start"]
        if len(starts) != 1 or starts[0].get("data", {}).get("capabilities", {}).get(
            "physical_motion"
        ) is not True:
            raise RuntimeError("live bridge capability was not recorded")

    print("booster bridge runner smoke test passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
