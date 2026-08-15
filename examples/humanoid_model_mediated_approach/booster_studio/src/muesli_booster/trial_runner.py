"""Supervise one manifest-bound native humanoid trial process."""

from __future__ import annotations

import datetime as dt
import json
import pathlib
import subprocess
import threading
import time
from collections.abc import Callable
from typing import IO, Any

from .adapter import AdapterState
from .evidence import EvidenceError, write_overlay
from .native_payload import PayloadError, VerifiedPayload, sha256_file, verify_payload

TRIAL_FILES = {
    "T1": "t1_normal_full.json",
    "T2a": "t2a_moved_ball_baseline.json",
    "T2b": "t2b_moved_ball_full.json",
    "T3": "t3_emergency_full.json",
}
LIVE_MANIFEST_SCHEMA = "humanoid.booster_live_trial.v1"


class TrialError(RuntimeError):
    """A live trial cannot start or did not satisfy its process contract."""


def _read_json(path: pathlib.Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except Exception as exc:
        raise TrialError(f"failed to read {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise TrialError(f"expected a JSON object: {path}")
    return value


def _write_json_atomic(path: pathlib.Path, value: object) -> None:
    temporary = path.with_name(f".{path.name}.tmp")
    temporary.write_text(
        json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    temporary.replace(path)


def _bool_text(value: bool) -> str:
    return "true" if value else "false"


def _value(parent: dict[str, Any], key: str) -> str:
    value = parent[key]
    if isinstance(value, bool):
        return _bool_text(value)
    if not isinstance(value, (str, int, float)):
        raise TrialError(f"configuration field {key} must be scalar")
    return str(value)


def build_trial_command(
    payload: VerifiedPayload,
    *,
    trial_id: str,
    run_id: str,
    event_path: pathlib.Path,
    bridge_socket: str,
    motion_enabled: bool,
) -> list[str]:
    trial_file = TRIAL_FILES.get(trial_id)
    if trial_file is None:
        raise TrialError(f"unknown trial ID: {trial_id}")
    common = _read_json(payload.root / "common/configs/common.json")
    trial = _read_json(payload.root / "common/configs" / trial_file)
    if trial.get("trial_id") != trial_id:
        raise TrialError(f"trial configuration identity mismatch for {trial_id}")
    tree_relative = trial.get("bt")
    if tree_relative not in {
        "lisp/bt_deadline_only.lisp",
        "lisp/bt_invocation_scoped.lisp",
    }:
        raise TrialError(f"trial {trial_id} selects an unsupported behaviour tree")
    tree = payload.root / "common" / str(tree_relative)

    proposer = common["proposer"]
    timing = common["timing"]
    pose = proposer["approach_pose"]
    action_space = proposer["request_action_space"]
    frames = common["frames"]
    bounds = common["pose_bounds"]
    perception = common["perception"]
    initial = perception["initial_ball_position_m"]
    moved = perception["moved_ball_position_m"]
    command = [
        str(payload.runner),
        "--tree",
        str(tree),
        "--events",
        str(event_path),
        "--run-id",
        run_id,
        "--trial-id",
        trial_id,
        "--acceptance-policy",
        _value(trial, "acceptance_policy"),
        "--intervention",
        _value(trial, "intervention"),
        "--delay-ms",
        _value(proposer, "artificial_delay_ms"),
        "--deadline-ms",
        _value(timing, "deadline_ms"),
        "--intervention-ms",
        _value(timing, "intervention_after_submit_ms"),
        "--tick-hz",
        _value(timing, "tick_hz"),
        "--seed",
        _value(proposer, "seed"),
        "--pose-x-m",
        _value(pose, "x_m"),
        "--pose-y-m",
        _value(pose, "y_m"),
        "--pose-yaw-rad",
        _value(pose, "yaw_rad"),
        "--action-frame",
        _value(frames, "approach_pose_frame"),
        "--observation-frame",
        _value(frames, "ball_position_frame"),
        "--min-x-m",
        _value(bounds, "min_x_m"),
        "--max-x-m",
        _value(bounds, "max_x_m"),
        "--min-y-m",
        _value(bounds, "min_y_m"),
        "--max-y-m",
        _value(bounds, "max_y_m"),
        "--min-yaw-rad",
        _value(bounds, "min_yaw_rad"),
        "--max-yaw-rad",
        _value(bounds, "max_yaw_rad"),
        "--initial-context-id",
        _value(perception, "initial_context_id"),
        "--moved-context-id",
        _value(perception, "moved_context_id"),
        "--initial-ball-x-m",
        _value(initial, "x_m"),
        "--initial-ball-y-m",
        _value(initial, "y_m"),
        "--initial-ball-z-m",
        _value(initial, "z_m"),
        "--moved-ball-x-m",
        _value(moved, "x_m"),
        "--moved-ball-y-m",
        _value(moved, "y_m"),
        "--moved-ball-z-m",
        _value(moved, "z_m"),
        "--context-change-threshold-m",
        _value(perception, "ball_context_change_threshold_m"),
        "--git-sha",
        payload.source_git_commit,
        "--platform",
        "booster-studio-sim_x86_64",
        "--physical-motion-enabled",
        _bool_text(motion_enabled),
        "--backend-name",
        _value(proposer, "backend"),
        "--model-version",
        _value(proposer, "model"),
        "--instruction",
        _value(proposer, "instruction"),
        "--request-dims",
        _value(action_space, "dims"),
        "--request-bound-lo",
        _value(action_space, "bound_lo"),
        "--request-bound-hi",
        _value(action_space, "bound_hi"),
        "--request-max-delta",
        _value(action_space, "max_delta"),
        "--booster-bridge-socket",
        bridge_socket,
        "--booster-bridge-timeout-ms",
        "100",
    ]
    return command


def _safe_run_id(value: str) -> str:
    if (
        not value
        or len(value) > 160
        or any(
            character
            not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-"
            for character in value
        )
    ):
        raise TrialError("run ID contains unsafe characters")
    return value


class NativeTrialSupervisor:
    """Own the native runner lifecycle and fail closed on process faults."""

    def __init__(
        self,
        *,
        payload_root: pathlib.Path,
        evidence_root: pathlib.Path,
        bridge_socket: str,
        state: AdapterState,
        logger: Any,
        process_factory: Callable[..., subprocess.Popen[str]] = subprocess.Popen,
    ) -> None:
        self._payload_root = payload_root
        self._evidence_root = evidence_root
        self._bridge_socket = bridge_socket
        self._state = state
        self._logger = logger
        self._process_factory = process_factory
        self._lock = threading.RLock()
        self._process: subprocess.Popen[str] | None = None
        self._watcher: threading.Thread | None = None
        self._stopping = False
        self._run_dir: pathlib.Path | None = None
        self._manifest: dict[str, Any] | None = None

    @property
    def running(self) -> bool:
        with self._lock:
            return self._process is not None and self._process.poll() is None

    @property
    def run_directory(self) -> pathlib.Path | None:
        with self._lock:
            return self._run_dir

    def start(self, trial_id: str, run_id: str | None = None) -> pathlib.Path:
        with self._lock:
            if self._process is not None and self._process.poll() is None:
                raise TrialError("a native trial is already running")
            try:
                payload = verify_payload(self._payload_root)
            except PayloadError as exc:
                raise TrialError(str(exc)) from exc
            snapshot = self._state.snapshot(time.monotonic())
            if (
                not self._state.motion_enabled
                or not snapshot.ball_available
                or snapshot.robot_pose is None
                or not snapshot.robot_stable
                or snapshot.emergency
            ):
                raise TrialError(
                    "Booster host is not ready: require armed motion, fresh ball, pose "
                    "and stability"
                )
            if run_id is None:
                stamp = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
                run_id = f"{stamp}-{trial_id.lower()}"
            run_id = _safe_run_id(run_id)
            run_dir = self._evidence_root.resolve() / run_id
            self._evidence_root.resolve().mkdir(parents=True, exist_ok=True)
            run_dir.mkdir()
            event_path = run_dir / "events.jsonl"
            command = build_trial_command(
                payload,
                trial_id=trial_id,
                run_id=run_id,
                event_path=event_path,
                bridge_socket=self._bridge_socket,
                motion_enabled=self._state.motion_enabled,
            )
            self._state.prepare_trial()
            manifest: dict[str, Any] = {
                "schema_version": LIVE_MANIFEST_SCHEMA,
                "status": "running",
                "trial_id": trial_id,
                "run_id": run_id,
                "source_git_commit": payload.source_git_commit,
                "source_git_dirty": payload.source_git_dirty,
                "payload_manifest_sha256": sha256_file(payload.root / "manifest.json"),
                "motion_enabled": self._state.motion_enabled,
                "runner_command": command,
                "event_log": {"path": "events.jsonl", "schema": "mbt.evt.v1"},
            }
            _write_json_atomic(run_dir / "live-manifest.json", manifest)
            try:
                process = self._process_factory(
                    command,
                    cwd=payload.root,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    text=True,
                    bufsize=1,
                )
            except Exception as exc:
                manifest["status"] = "launch_failed"
                manifest["launch_error"] = str(exc)
                _write_json_atomic(run_dir / "live-manifest.json", manifest)
                self._state.latch_runtime_fault()
                raise TrialError(f"failed to launch native trial: {exc}") from exc
            self._process = process
            self._run_dir = run_dir
            self._manifest = manifest
            self._stopping = False
            self._watcher = threading.Thread(
                target=self._watch_process,
                args=(process,),
                name="muesli_native_trial",
                daemon=True,
            )
            self._watcher.start()
            self._logger.info(f"muesli native trial started: {trial_id} ({run_id})")
            return run_dir

    def stop(self) -> None:
        with self._lock:
            process = self._process
            watcher = self._watcher
            self._stopping = True
        if process is not None and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=2.0)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=2.0)
        if watcher is not None and watcher is not threading.current_thread():
            watcher.join(timeout=3.0)
        self._state.cancel_motion()

    def _watch_process(self, process: subprocess.Popen[str]) -> None:
        output: IO[str] | None = process.stdout
        if output is not None:
            for line in output:
                message = line.rstrip()
                if message:
                    self._logger.info(f"native trial: {message}")
        return_code = process.wait()
        with self._lock:
            stopping = self._stopping
            manifest = self._manifest
            run_dir = self._run_dir
            if self._process is process:
                self._process = None
            self._watcher = None
        if return_code != 0 and not stopping:
            self._state.cancel_motion()
            self._state.latch_runtime_fault()
            self._logger.error(
                f"native trial exited unexpectedly with code {return_code}"
            )
        elif stopping:
            self._state.cancel_motion()
        elif return_code == 0:
            self._logger.info("muesli native trial completed")
        if manifest is not None and run_dir is not None:
            manifest["return_code"] = return_code
            manifest["status"] = (
                "stopped" if stopping else "completed" if return_code == 0 else "failed"
            )
            event_path = run_dir / "events.jsonl"
            if event_path.is_file():
                manifest["event_log"]["sha256"] = sha256_file(event_path)
                manifest["event_log"]["size_bytes"] = event_path.stat().st_size
                overlay_path = run_dir / "overlay.ass"
                try:
                    write_overlay(event_path, overlay_path)
                    manifest["overlay"] = {
                        "path": "overlay.ass",
                        "sha256": sha256_file(overlay_path),
                        "alignment": "event_stream_origin",
                    }
                except EvidenceError as exc:
                    manifest["overlay"] = {
                        "path": "overlay.ass",
                        "status": "failed",
                        "reason": str(exc),
                    }
            else:
                manifest["event_log"]["status"] = "missing"
            _write_json_atomic(run_dir / "live-manifest.json", manifest)
