"""Live ROS 2 and BoosterOS plumbing for the fail-closed adapter.

Imports of environment-owned packages are deliberately delayed until
``start()``. Offline policy tests therefore need neither ROS 2 nor BoosterOS.
"""

from __future__ import annotations

import math
import os
import threading
import time
from typing import Any

from .adapter import AdapterConfig, AdapterState, BallObservation, RobotPose
from .bridge_server import BridgeServer


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
        self._stop = threading.Event()
        self._spin_thread: threading.Thread | None = None
        self._control_thread: threading.Thread | None = None
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
        from std_msgs.msg import Bool

        context = rclpy.get_default_context()
        self._ros_context = context
        if not rclpy.ok(context=context):
            context.init(args=None, initialize_logging=False)
            self._owns_ros_context = True
        self._node = rclpy.create_node("muesli_booster_host", context=context)
        qos = QoSProfile(depth=1, reliability=ReliabilityPolicy.RELIABLE)
        ball_topic = f"/team{self._team_id}/soccer/sim/ground_truth/ball"
        robot_topic = (
            f"/team{self._team_id}/{self._robot_name}/soccer/sim/ground_truth/robot_pose"
        )
        self._node.create_subscription(Pose2D, ball_topic, self._on_ball, qos)
        self._node.create_subscription(Pose2D, robot_topic, self._on_robot_pose, qos)
        self._node.create_subscription(Bool, "/muesli/emergency", self._on_emergency, qos)
        self._executor = SingleThreadedExecutor(context=context)
        self._executor.add_node(self._node)

        if self._motion_enabled:
            from boosteros.robots.booster import BoosterRobot

            self._robot = BoosterRobot(
                virtual_robot_name=self._robot_name,
                enable_tf_listener=False,
                timeout=10.0,
            )
            self._robot.set_gait("soccer")
            self._robot.set_mode("walk")
        self._state.set_robot_stable(False)
        if self._robot is not None:
            self._poll_stability()
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
        bridge_path = os.environ.get(
            "MUESLI_BOOSTER_BRIDGE_SOCKET", "/tmp/muesli-booster-bridge.sock"
        )
        self._logger.info(
            "muesli Booster host started; motion_enabled=%s bridge=%s"
            % (self._motion_enabled, bridge_path)
        )

    def stop(self) -> None:
        self._stop.set()
        self._bridge.stop()
        if self._executor is not None:
            self._executor.shutdown()
        for thread in (self._spin_thread, self._control_thread):
            if thread is not None and thread.is_alive():
                thread.join(timeout=2.0)
        self._spin_thread = None
        self._control_thread = None
        if self._robot is not None:
            try:
                self._robot.set_velocity(vx=0.0, vy=0.0, vyaw=0.0)
            finally:
                close = getattr(self._robot, "_close", None)
                if callable(close):
                    close()
        self._robot = None
        if self._node is not None:
            self._node.destroy_node()
        if self._owns_ros_context and self._ros_context is not None:
            self._ros_context.shutdown()
        self._node = None
        self._executor = None
        self._state.set_robot_stable(False)

    def _spin(self) -> None:
        try:
            self._executor.spin()
        except Exception as exc:
            if not self._stop.is_set():
                self._logger.error(f"muesli ROS spin failed: {exc}")
                self._state.set_robot_stable(False)

    def _control_loop(self) -> None:
        period = 1.0 / max(1.0, self._control_hz)
        next_stability_poll = 0.0
        while not self._stop.is_set():
            started = time.monotonic()
            if self._robot is not None and started >= next_stability_poll:
                self._poll_stability()
                next_stability_poll = started + 0.5
            command = self._state.velocity_command(started)
            if self._robot is not None:
                try:
                    self._robot.set_velocity(
                        vx=command.vx_mps, vy=command.vy_mps, vyaw=command.vyaw_rps
                    )
                except Exception as exc:
                    self._state.set_robot_stable(False)
                    self._logger.error(f"Booster set_velocity failed: {exc}")
            self._stop.wait(max(0.0, period - (time.monotonic() - started)))

    def _poll_stability(self) -> None:
        try:
            state = self._robot.get_fall_down_state()
            state_name = getattr(state, "state", None)
            self._state.set_robot_stable(state_name == "normal")
        except Exception as exc:
            self._state.set_robot_stable(False)
            self._logger.error(f"Booster stability query failed: {exc}")

    def _on_ball(self, message: Any) -> None:
        self._state.observe_ball(
            BallObservation(float(message.x), float(message.y), 0.0, time.monotonic())
        )

    def _on_robot_pose(self, message: Any) -> None:
        self._state.observe_robot(
            RobotPose(float(message.x), float(message.y), float(message.theta), time.monotonic())
        )

    def _on_emergency(self, message: Any) -> None:
        self._state.set_emergency(bool(message.data))
