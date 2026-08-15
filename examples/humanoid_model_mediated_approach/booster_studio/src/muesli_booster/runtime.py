"""Live ROS 2 and BoosterOS plumbing for the fail-closed adapter.

Imports of environment-owned packages are deliberately delayed until
``start()``. Offline policy tests therefore need neither ROS 2 nor BoosterOS.
"""

from __future__ import annotations

import math
import os
import pathlib
import threading
import time
from typing import Any

from .adapter import AdapterConfig, AdapterState, BallObservation, RobotPose
from .bridge_server import BridgeServer
from .trial_runner import TRIAL_FILES, NativeTrialSupervisor, TrialError


def _default_payload_root(
    module_path: pathlib.Path | None = None,
    working_directory: pathlib.Path | None = None,
) -> pathlib.Path:
    """Locate the Studio-packaged native payload or its source-tree location."""
    module_path = (module_path or pathlib.Path(__file__)).resolve()
    working_directory = (working_directory or pathlib.Path.cwd()).resolve()
    candidates: list[pathlib.Path] = []
    for anchor in (module_path.parent, working_directory):
        for parent in (anchor, *anchor.parents):
            candidate = parent / "res" / "native_payload"
            if candidate not in candidates:
                candidates.append(candidate)
    for candidate in candidates:
        if (candidate / "manifest.json").is_file():
            return candidate

    # ``runtime.py`` lives below ``src/muesli_booster`` in a source checkout.
    # Returning this stable path gives payload verification a useful error when
    # the generated resource has not been built yet.
    return module_path.parents[2] / "res" / "native_payload"


def _env_bool(name: str, default: bool = False) -> bool:
    value = os.environ.get(name)
    if value is None:
        return default
    normalised = value.strip().lower()
    if normalised in {"1", "true", "yes", "on"}:
        return True
    if normalised in {"0", "false", "no", "off"}:
        return False
    raise ValueError(f"{name} must be a boolean")


def _env_float(name: str, default: float) -> float:
    value = float(os.environ.get(name, default))
    if not math.isfinite(value):
        raise ValueError(f"{name} must be finite")
    return value


class BoosterRuntime:
    """Connect observations, synchronous dispatch and bounded velocity output."""

    def __init__(self, logger: Any) -> None:
        self._logger = logger
        self._team_id = int(os.environ.get("MUESLI_BOOSTER_TEAM_ID", "1"))
        self._robot_name = os.environ.get("MUESLI_BOOSTER_ROBOT_NAME", "robot1")
        self._robot_gait = os.environ.get("MUESLI_BOOSTER_GAIT", "default").strip()
        if not self._robot_gait:
            raise ValueError("MUESLI_BOOSTER_GAIT must not be empty")
        self._control_hz = _env_float("MUESLI_BOOSTER_CONTROL_HZ", 20.0)
        motion_enabled = _env_bool("MUESLI_BOOSTER_MOTION_ENABLED", False)
        self._state = AdapterState(
            AdapterConfig(
                context_change_threshold_m=_env_float(
                    "MUESLI_BOOSTER_CONTEXT_THRESHOLD_M", 0.15
                ),
                ball_observation_max_age_s=_env_float(
                    "MUESLI_BOOSTER_BALL_MAX_AGE_S", 0.5
                ),
                robot_pose_max_age_s=_env_float(
                    "MUESLI_BOOSTER_ROBOT_POSE_MAX_AGE_S", 0.5
                ),
                min_field_x_m=_env_float("MUESLI_BOOSTER_MIN_FIELD_X_M", -6.5),
                max_field_x_m=_env_float("MUESLI_BOOSTER_MAX_FIELD_X_M", 6.5),
                min_field_y_m=_env_float("MUESLI_BOOSTER_MIN_FIELD_Y_M", -4.0),
                max_field_y_m=_env_float("MUESLI_BOOSTER_MAX_FIELD_Y_M", 4.0),
                motion_enabled=motion_enabled,
            )
        )
        socket_path = os.environ.get(
            "MUESLI_BOOSTER_BRIDGE_SOCKET", "/tmp/muesli-booster-bridge.sock"
        )
        self._bridge = BridgeServer(socket_path, self._state)
        self._motion_enabled = motion_enabled
        payload_root = pathlib.Path(
            os.environ.get(
                "MUESLI_BOOSTER_NATIVE_PAYLOAD_ROOT", str(_default_payload_root())
            )
        )
        evidence_root = pathlib.Path(
            os.environ.get("MUESLI_BOOSTER_EVIDENCE_ROOT", "/tmp/muesli-humanoid-runs")
        )
        self._trial_supervisor = NativeTrialSupervisor(
            payload_root=payload_root,
            evidence_root=evidence_root,
            bridge_socket=socket_path,
            state=self._state,
            logger=logger,
        )
        self._autostart_trial = os.environ.get(
            "MUESLI_BOOSTER_AUTOSTART_TRIAL", ""
        ).strip()
        if self._autostart_trial and self._autostart_trial not in TRIAL_FILES:
            raise ValueError(
                "MUESLI_BOOSTER_AUTOSTART_TRIAL must be T1, T2a, T2b or T3"
            )
        self._trial_startup_timeout_s = _env_float(
            "MUESLI_BOOSTER_TRIAL_STARTUP_TIMEOUT_S", 30.0
        )
        if self._trial_startup_timeout_s <= 0.0:
            raise ValueError("MUESLI_BOOSTER_TRIAL_STARTUP_TIMEOUT_S must be positive")
        self._stop = threading.Event()
        self._spin_thread: threading.Thread | None = None
        self._control_thread: threading.Thread | None = None
        self._autostart_thread: threading.Thread | None = None
        self._robot_lock = threading.RLock()
        self._robot: Any = None
        self._node: Any = None
        self._executor: Any = None
        self._ros_context: Any = None
        self._owns_ros_context = False

    def start(self) -> None:
        if self._control_thread is not None:
            return
        import rclpy
        from geometry_msgs.msg import Pose2D
        from rclpy.executors import SingleThreadedExecutor
        from rclpy.qos import QoSProfile, ReliabilityPolicy
        from std_msgs.msg import Bool, String

        context = rclpy.get_default_context()
        self._ros_context = context
        if not rclpy.ok(context=context):
            context.init(args=None, initialize_logging=False)
            self._owns_ros_context = True
        self._node = rclpy.create_node("muesli_booster_host", context=context)
        qos = QoSProfile(depth=1, reliability=ReliabilityPolicy.RELIABLE)
        ball_topic = f"/team{self._team_id}/soccer/sim/ground_truth/ball"
        robot_topic = f"/team{self._team_id}/{self._robot_name}/soccer/sim/ground_truth/robot_pose"
        self._node.create_subscription(Pose2D, ball_topic, self._on_ball, qos)
        self._node.create_subscription(Pose2D, robot_topic, self._on_robot_pose, qos)
        self._node.create_subscription(
            Bool, "/muesli/emergency", self._on_emergency, qos
        )
        self._node.create_subscription(
            Bool, "/muesli/motion_arm", self._on_motion_arm, qos
        )
        self._node.create_subscription(
            String, "/muesli/trial_command", self._on_trial_command, qos
        )
        self._executor = SingleThreadedExecutor(context=context)
        self._executor.add_node(self._node)

        if self._motion_enabled:
            self._robot = self._create_robot_backend()
        self._state.set_robot_stable(False)
        if self._robot is not None:
            self._poll_stability(self._robot)
        self._bridge.start()
        self._stop.clear()
        self._spin_thread = threading.Thread(
            target=self._spin, name="muesli_ros_spin", daemon=True
        )
        self._control_thread = threading.Thread(
            target=self._control_loop, name="muesli_booster_control", daemon=True
        )
        self._spin_thread.start()
        self._control_thread.start()
        if self._autostart_trial:
            self._autostart_thread = threading.Thread(
                target=self._start_trial_when_ready,
                name="muesli_trial_autostart",
                daemon=True,
            )
            self._autostart_thread.start()
        bridge_path = os.environ.get(
            "MUESLI_BOOSTER_BRIDGE_SOCKET", "/tmp/muesli-booster-bridge.sock"
        )
        self._logger.info(
            f"muesli Booster host started; motion_enabled={self._motion_enabled} "
            f"gait={self._robot_gait} bridge={bridge_path}"
        )

    def stop(self) -> None:
        self._stop.set()
        self._trial_supervisor.stop()
        self._bridge.stop()
        if self._executor is not None:
            self._executor.shutdown()
        for thread in (self._spin_thread, self._control_thread, self._autostart_thread):
            if thread is not None and thread.is_alive():
                thread.join(timeout=2.0)
        self._spin_thread = None
        self._control_thread = None
        self._autostart_thread = None
        self._motion_enabled = False
        self._state.set_motion_enabled(False)
        self._disable_robot_backend()
        if self._node is not None:
            self._node.destroy_node()
        if self._owns_ros_context and self._ros_context is not None:
            self._ros_context.shutdown()
        self._node = None
        self._executor = None
        self._state.set_robot_stable(False)

    def start_trial(self, trial_id: str, run_id: str | None = None) -> pathlib.Path:
        """Start one native trial after the live host has become ready."""
        return self._trial_supervisor.start(trial_id, run_id)

    @property
    def motion_enabled(self) -> bool:
        return self._state.motion_enabled

    def arm_motion(self) -> None:
        """Create the Booster backend after an explicit operator action."""
        if self._control_thread is None:
            raise RuntimeError("Booster host is not active")
        with self._robot_lock:
            if self._robot is not None:
                return
            robot = self._create_robot_backend()
            self._robot = robot
            self._motion_enabled = True
            self._state.set_motion_enabled(True)
            self._poll_stability(robot)
        self._logger.info("muesli Booster motion armed by operator")

    def disarm_motion(self) -> None:
        """Stop the trial and robot backend, revoking any walking target."""
        self._trial_supervisor.stop()
        self._motion_enabled = False
        self._state.set_motion_enabled(False)
        self._state.set_robot_stable(False)
        self._disable_robot_backend()
        self._logger.info("muesli Booster motion disarmed by operator")

    def set_emergency(self, enabled: bool) -> None:
        """Set or clear the controlled software emergency used by T3."""
        self._state.set_emergency(enabled)
        self._logger.info(f"muesli Booster software emergency={enabled}")

    def _create_robot_backend(self) -> Any:
        from boosteros.robots.booster import BoosterRobot

        robot = BoosterRobot(
            virtual_robot_name=self._robot_name,
            enable_tf_listener=False,
            timeout=10.0,
        )
        try:
            robot.set_gait(self._robot_gait)
            robot.set_mode("walk")
        except Exception:
            close = getattr(robot, "_close", None)
            if callable(close):
                close()
            raise
        return robot

    def _disable_robot_backend(self) -> None:
        with self._robot_lock:
            robot = self._robot
            self._robot = None
            if robot is None:
                return
            try:
                robot.set_velocity(vx=0.0, vy=0.0, vyaw=0.0)
            finally:
                close = getattr(robot, "_close", None)
                if callable(close):
                    close()

    def _start_trial_when_ready(self) -> None:
        deadline = time.monotonic() + self._trial_startup_timeout_s
        while not self._stop.is_set() and time.monotonic() < deadline:
            snapshot = self._state.snapshot(time.monotonic())
            if (
                snapshot.ball_available
                and snapshot.robot_pose is not None
                and snapshot.robot_stable
                and not snapshot.emergency
            ):
                try:
                    run_dir = self.start_trial(self._autostart_trial)
                    self._logger.info(f"muesli trial evidence directory: {run_dir}")
                except TrialError as exc:
                    self._state.latch_runtime_fault()
                    self._logger.error(f"muesli native trial did not start: {exc}")
                return
            self._stop.wait(0.1)
        if not self._stop.is_set():
            self._logger.error(
                "muesli native trial did not start before the host-readiness timeout"
            )

    def _spin(self) -> None:
        try:
            self._executor.spin()
        except Exception as exc:  # noqa: BLE001 - environment-owned executor boundary
            if not self._stop.is_set():
                self._logger.error(f"muesli ROS spin failed: {exc}")
                self._state.set_robot_stable(False)

    def _control_loop(self) -> None:
        period = 1.0 / max(1.0, self._control_hz)
        next_stability_poll = 0.0
        while not self._stop.is_set():
            started = time.monotonic()
            with self._robot_lock:
                robot = self._robot
                if robot is not None and started >= next_stability_poll:
                    self._poll_stability(robot)
                    next_stability_poll = started + 0.5
            command = self._state.velocity_command(started)
            with self._robot_lock:
                robot = self._robot
                if robot is not None:
                    try:
                        robot.set_velocity(
                            vx=command.vx_mps,
                            vy=command.vy_mps,
                            vyaw=command.vyaw_rps,
                        )
                    except Exception as exc:  # noqa: BLE001 - Booster SDK boundary
                        self._state.set_robot_stable(False)
                        self._logger.error(f"Booster set_velocity failed: {exc}")
            self._stop.wait(max(0.0, period - (time.monotonic() - started)))

    def _poll_stability(self, robot: Any) -> None:
        try:
            state = robot.get_fall_down_state()
            state_name = getattr(state, "state", None)
            self._state.set_robot_stable(state_name == "normal")
        except Exception as exc:  # noqa: BLE001 - Booster SDK boundary
            self._state.set_robot_stable(False)
            self._logger.error(f"Booster stability query failed: {exc}")

    def _on_ball(self, message: Any) -> None:
        self._state.observe_ball(
            BallObservation(float(message.x), float(message.y), 0.0, time.monotonic())
        )

    def _on_robot_pose(self, message: Any) -> None:
        self._state.observe_robot(
            RobotPose(
                float(message.x),
                float(message.y),
                float(message.theta),
                time.monotonic(),
            )
        )

    def _on_emergency(self, message: Any) -> None:
        self._state.set_emergency(bool(message.data))

    def _on_motion_arm(self, message: Any) -> None:
        try:
            if bool(message.data):
                self.arm_motion()
            else:
                self.disarm_motion()
        except Exception as exc:  # noqa: BLE001 - ROS/Booster SDK callback boundary
            self._logger.error(f"motion arming command failed: {exc}")

    def _on_trial_command(self, message: Any) -> None:
        trial_id = str(message.data).strip()
        if trial_id not in TRIAL_FILES:
            self._logger.error(f"unsupported muesli trial command: {trial_id}")
            return
        try:
            run_dir = self.start_trial(trial_id)
            self._logger.info(f"muesli trial evidence directory: {run_dir}")
        except TrialError as exc:
            self._logger.error(f"{trial_id} did not start: {exc}")
