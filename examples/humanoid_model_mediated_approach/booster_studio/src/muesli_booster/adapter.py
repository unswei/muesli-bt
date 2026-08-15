"""Platform-independent context, dispatch and walking-controller policy.

This module deliberately imports neither ROS 2 nor BoosterOS. The live runtime
injects observations and forwards the resulting bounded body-frame velocity.
The muesli runtime remains responsible for invocation authority and canonical
``mbt.evt.v1`` evidence. This adapter is a second, synchronous host envelope.
"""

from __future__ import annotations

import math
import threading
from collections.abc import Mapping
from dataclasses import dataclass
from typing import Any

DISPATCH_REQUEST_SCHEMA = "humanoid.booster_dispatch_request.v1"
DISPATCH_RESPONSE_SCHEMA = "humanoid.booster_dispatch_response.v1"
SNAPSHOT_RESPONSE_SCHEMA = "humanoid.booster_snapshot.v1"


def _finite(*values: float) -> bool:
    return all(math.isfinite(value) for value in values)


def _clamp(value: float, lower: float, upper: float) -> float:
    return max(lower, min(upper, value))


def _normalise_angle(value: float) -> float:
    return math.atan2(math.sin(value), math.cos(value))


@dataclass(frozen=True)
class AdapterConfig:
    context_change_threshold_m: float = 0.15
    ball_observation_max_age_s: float = 0.5
    robot_pose_max_age_s: float = 0.5
    min_offset_x_m: float = -1.0
    max_offset_x_m: float = 1.0
    min_offset_y_m: float = -1.0
    max_offset_y_m: float = 1.0
    min_yaw_rad: float = -math.pi
    max_yaw_rad: float = math.pi
    min_field_x_m: float = -6.5
    max_field_x_m: float = 6.5
    min_field_y_m: float = -4.0
    max_field_y_m: float = 4.0
    arrival_distance_m: float = 0.12
    arrival_yaw_rad: float = 0.10
    omni_distance_m: float = 0.8
    turn_threshold_rad: float = 0.35
    linear_gain: float = 0.8
    angular_gain: float = 1.5
    max_linear_mps: float = 0.35
    max_angular_rps: float = 0.8
    motion_enabled: bool = False

    def __post_init__(self) -> None:
        positive = (
            self.context_change_threshold_m,
            self.ball_observation_max_age_s,
            self.robot_pose_max_age_s,
            self.arrival_distance_m,
            self.arrival_yaw_rad,
            self.omni_distance_m,
            self.turn_threshold_rad,
            self.linear_gain,
            self.angular_gain,
            self.max_linear_mps,
            self.max_angular_rps,
        )
        if not _finite(*positive) or any(value <= 0.0 for value in positive):
            raise ValueError(
                "adapter thresholds, gains and limits must be finite and positive"
            )
        ordered = (
            (self.min_offset_x_m, self.max_offset_x_m),
            (self.min_offset_y_m, self.max_offset_y_m),
            (self.min_yaw_rad, self.max_yaw_rad),
            (self.min_field_x_m, self.max_field_x_m),
            (self.min_field_y_m, self.max_field_y_m),
        )
        if any(not _finite(lo, hi) or lo > hi for lo, hi in ordered):
            raise ValueError("adapter bounds must be finite and ordered")


@dataclass(frozen=True)
class BallObservation:
    x_m: float
    y_m: float
    z_m: float
    observed_at: float


@dataclass(frozen=True)
class RobotPose:
    x_m: float
    y_m: float
    yaw_rad: float
    observed_at: float


@dataclass(frozen=True)
class ApproachPose:
    frame_id: str
    x_m: float
    y_m: float
    yaw_rad: float


@dataclass(frozen=True)
class BridgeSnapshot:
    ball_context_id: str
    ball: BallObservation | None
    ball_available: bool
    robot_pose: RobotPose | None
    robot_stable: bool
    emergency: bool


@dataclass(frozen=True)
class DispatchOutcome:
    accepted: bool
    reason: str
    field_target: ApproachPose | None = None


@dataclass(frozen=True)
class VelocityCommand:
    vx_mps: float
    vy_mps: float
    vyaw_rps: float
    reason: str


@dataclass(frozen=True)
class _ActiveTarget:
    key: tuple[str, int]
    context_id: str
    target: ApproachPose


class BallContextTracker:
    """Assign monotonic context IDs to a fresh ball track."""

    def __init__(self, threshold_m: float, max_age_s: float) -> None:
        if (
            not _finite(threshold_m, max_age_s)
            or threshold_m <= 0.0
            or max_age_s <= 0.0
        ):
            raise ValueError("ball context threshold and maximum age must be positive")
        self._threshold_m = threshold_m
        self._max_age_s = max_age_s
        self._sequence = 0
        self._context_id = ""
        self._ball: BallObservation | None = None
        self._context_anchor: BallObservation | None = None
        self._advance_on_observation = True

    @property
    def context_id(self) -> str:
        return self._context_id

    def observe(self, observation: BallObservation) -> str:
        if not _finite(
            observation.x_m, observation.y_m, observation.z_m, observation.observed_at
        ):
            raise ValueError("ball observation must be finite")
        previous = self._ball
        if previous is not None and observation.observed_at < previous.observed_at:
            raise ValueError("ball observation timestamps must be monotonic")
        gap_is_loss = (
            previous is not None
            and observation.observed_at - previous.observed_at > self._max_age_s
        )
        moved = (
            self._context_anchor is not None
            and math.dist(
                (
                    self._context_anchor.x_m,
                    self._context_anchor.y_m,
                    self._context_anchor.z_m,
                ),
                (observation.x_m, observation.y_m, observation.z_m),
            )
            > self._threshold_m
        )
        if self._advance_on_observation or gap_is_loss or moved:
            self._sequence += 1
            self._context_id = f"ball-{self._sequence:04d}"
            self._context_anchor = observation
        self._advance_on_observation = False
        self._ball = observation
        return self._context_id

    def mark_lost(self) -> None:
        self._ball = None
        self._context_anchor = None
        self._advance_on_observation = True

    def snapshot(
        self,
        *,
        now: float,
        robot_pose: RobotPose | None,
        robot_stable: bool,
        emergency: bool,
    ) -> BridgeSnapshot:
        if not math.isfinite(now):
            raise ValueError("snapshot time must be finite")
        ball = self._ball
        age = math.inf if ball is None else now - ball.observed_at
        available = ball is not None and 0.0 <= age <= self._max_age_s
        return BridgeSnapshot(
            ball_context_id=self._context_id,
            ball=ball if available else None,
            ball_available=available,
            robot_pose=robot_pose,
            robot_stable=bool(robot_stable and not emergency),
            emergency=bool(emergency),
        )


class AdapterState:
    """Thread-safe host state, synchronous dispatch gate and pose follower."""

    def __init__(self, config: AdapterConfig) -> None:
        self.config = config
        self._tracker = BallContextTracker(
            config.context_change_threshold_m, config.ball_observation_max_age_s
        )
        self._lock = threading.RLock()
        self._robot_pose: RobotPose | None = None
        self._robot_stable = False
        self._emergency = False
        self._runtime_fault = False
        self._accepted_keys: set[tuple[str, int]] = set()
        self._active_target: _ActiveTarget | None = None

    def observe_ball(self, observation: BallObservation) -> str:
        with self._lock:
            return self._tracker.observe(observation)

    def mark_ball_lost(self) -> None:
        with self._lock:
            self._tracker.mark_lost()
            self._active_target = None

    def observe_robot(self, pose: RobotPose) -> None:
        if not _finite(pose.x_m, pose.y_m, pose.yaw_rad, pose.observed_at):
            raise ValueError("robot pose must be finite")
        with self._lock:
            if (
                self._robot_pose is not None
                and pose.observed_at < self._robot_pose.observed_at
            ):
                raise ValueError("robot pose timestamps must be monotonic")
            self._robot_pose = pose

    def set_robot_stable(self, stable: bool) -> None:
        with self._lock:
            self._robot_stable = bool(stable)
            if not stable:
                self._active_target = None

    def set_emergency(self, emergency: bool) -> None:
        with self._lock:
            self._emergency = bool(emergency)
            if emergency:
                self._active_target = None

    def latch_runtime_fault(self) -> None:
        """Latch an internal supervisor fault until the agent is recreated."""
        with self._lock:
            self._runtime_fault = True
            self._active_target = None

    def prepare_trial(self) -> None:
        """Clear per-process dispatch authority before starting a new trial."""
        with self._lock:
            self._accepted_keys.clear()
            self._active_target = None

    def cancel_motion(self) -> None:
        """Remove the active walking target without changing observation state."""
        with self._lock:
            self._active_target = None

    def snapshot(self, now: float) -> BridgeSnapshot:
        with self._lock:
            pose = self._robot_pose
            if pose is not None:
                age = now - pose.observed_at
                if age < 0.0 or age > self.config.robot_pose_max_age_s:
                    pose = None
            return self._tracker.snapshot(
                now=now,
                robot_pose=pose,
                robot_stable=self._robot_stable,
                emergency=self._emergency or self._runtime_fault,
            )

    def dispatch(self, request: Mapping[str, Any], now: float) -> DispatchOutcome:
        with self._lock:
            snapshot = self.snapshot(now)
            outcome = self._validate_dispatch(request, snapshot)
            if outcome.accepted and outcome.field_target is not None:
                key = (str(request["job_id"]), int(request["generation"]))
                self._accepted_keys.add(key)
                self._active_target = _ActiveTarget(
                    key=key,
                    context_id=snapshot.ball_context_id,
                    target=outcome.field_target,
                )
            return outcome

    def _validate_dispatch(
        self, request: Mapping[str, Any], snapshot: BridgeSnapshot
    ) -> DispatchOutcome:
        if request.get("schema_version") != DISPATCH_REQUEST_SCHEMA:
            return DispatchOutcome(False, "invalid_schema")
        job_id = request.get("job_id")
        generation = request.get("generation")
        captured_context_id = request.get("captured_context_id")
        target_value = request.get("target")
        if (
            not isinstance(job_id, str)
            or not job_id
            or isinstance(generation, bool)
            or not isinstance(generation, int)
            or generation <= 0
            or not isinstance(captured_context_id, str)
            or not isinstance(target_value, Mapping)
        ):
            return DispatchOutcome(False, "invalid_schema")
        key = (job_id, generation)
        if key in self._accepted_keys:
            return DispatchOutcome(False, "duplicate_dispatch")
        if not self.config.motion_enabled:
            return DispatchOutcome(False, "motion_disabled")
        if snapshot.emergency or not snapshot.robot_stable:
            return DispatchOutcome(False, "robot_unstable")
        if not snapshot.ball_available or snapshot.ball is None:
            return DispatchOutcome(False, "ball_stale")
        if captured_context_id != snapshot.ball_context_id:
            return DispatchOutcome(False, "context_changed")
        if snapshot.robot_pose is None:
            return DispatchOutcome(False, "robot_pose_stale")
        frame_id = target_value.get("frame_id")
        x_value = target_value.get("x_m")
        y_value = target_value.get("y_m")
        yaw_value = target_value.get("yaw_rad")
        if (
            not isinstance(frame_id, str)
            or isinstance(x_value, bool)
            or not isinstance(x_value, (int, float))
            or isinstance(y_value, bool)
            or not isinstance(y_value, (int, float))
            or isinstance(yaw_value, bool)
            or not isinstance(yaw_value, (int, float))
        ):
            return DispatchOutcome(False, "invalid_schema")
        try:
            target = ApproachPose(
                frame_id=frame_id,
                x_m=float(x_value),
                y_m=float(y_value),
                yaw_rad=float(yaw_value),
            )
        except (TypeError, ValueError):
            return DispatchOutcome(False, "invalid_schema")
        if target.frame_id != "ball_context":
            return DispatchOutcome(False, "invalid_frame")
        if not _finite(target.x_m, target.y_m, target.yaw_rad):
            return DispatchOutcome(False, "invalid_pose")
        if not (
            self.config.min_offset_x_m <= target.x_m <= self.config.max_offset_x_m
            and self.config.min_offset_y_m <= target.y_m <= self.config.max_offset_y_m
            and self.config.min_yaw_rad <= target.yaw_rad <= self.config.max_yaw_rad
        ):
            return DispatchOutcome(False, "invalid_pose")

        # The ball context is field-aligned in the frozen experiment contract,
        # so field_T_ball_context has translation only.
        field_target = ApproachPose(
            frame_id="field",
            x_m=snapshot.ball.x_m + target.x_m,
            y_m=snapshot.ball.y_m + target.y_m,
            yaw_rad=_normalise_angle(target.yaw_rad),
        )
        if not (
            self.config.min_field_x_m <= field_target.x_m <= self.config.max_field_x_m
            and self.config.min_field_y_m
            <= field_target.y_m
            <= self.config.max_field_y_m
        ):
            return DispatchOutcome(False, "outside_operating_area")
        return DispatchOutcome(True, "", field_target)

    def velocity_command(self, now: float) -> VelocityCommand:
        with self._lock:
            snapshot = self.snapshot(now)
            active = self._active_target
            if active is None:
                return VelocityCommand(0.0, 0.0, 0.0, "no_target")
            if snapshot.emergency or not snapshot.robot_stable:
                self._active_target = None
                return VelocityCommand(0.0, 0.0, 0.0, "robot_unstable")
            if not snapshot.ball_available:
                self._active_target = None
                return VelocityCommand(0.0, 0.0, 0.0, "ball_stale")
            if snapshot.ball_context_id != active.context_id:
                self._active_target = None
                return VelocityCommand(0.0, 0.0, 0.0, "context_changed")
            pose = snapshot.robot_pose
            if (
                pose is None
                or now - pose.observed_at < 0.0
                or (now - pose.observed_at > self.config.robot_pose_max_age_s)
            ):
                self._active_target = None
                return VelocityCommand(0.0, 0.0, 0.0, "robot_pose_stale")

            dx = active.target.x_m - pose.x_m
            dy = active.target.y_m - pose.y_m
            distance = math.hypot(dx, dy)
            target_yaw_error = _normalise_angle(active.target.yaw_rad - pose.yaw_rad)
            if distance <= self.config.arrival_distance_m:
                if abs(target_yaw_error) <= self.config.arrival_yaw_rad:
                    self._active_target = None
                    return VelocityCommand(0.0, 0.0, 0.0, "arrived")
                return VelocityCommand(
                    0.0,
                    0.0,
                    _clamp(
                        self.config.angular_gain * target_yaw_error,
                        -self.config.max_angular_rps,
                        self.config.max_angular_rps,
                    ),
                    "aligning",
                )

            heading = math.atan2(dy, dx)
            heading_error = _normalise_angle(heading - pose.yaw_rad)
            if distance > self.config.omni_distance_m and abs(heading_error) > (
                self.config.turn_threshold_rad
            ):
                return VelocityCommand(
                    0.0,
                    0.0,
                    _clamp(
                        self.config.angular_gain * heading_error,
                        -self.config.max_angular_rps,
                        self.config.max_angular_rps,
                    ),
                    "turning",
                )

            cos_yaw = math.cos(pose.yaw_rad)
            sin_yaw = math.sin(pose.yaw_rad)
            vx = self.config.linear_gain * (dx * cos_yaw + dy * sin_yaw)
            vy = self.config.linear_gain * (-dx * sin_yaw + dy * cos_yaw)
            speed = math.hypot(vx, vy)
            if speed > self.config.max_linear_mps:
                scale = self.config.max_linear_mps / speed
                vx *= scale
                vy *= scale
            vyaw = _clamp(
                self.config.angular_gain * target_yaw_error,
                -self.config.max_angular_rps,
                self.config.max_angular_rps,
            )
            return VelocityCommand(vx, vy, vyaw, "walking")

    def snapshot_payload(self, now: float) -> dict[str, Any]:
        snapshot = self.snapshot(now)
        ball = snapshot.ball
        robot = snapshot.robot_pose
        return {
            "schema_version": SNAPSHOT_RESPONSE_SCHEMA,
            "ball_context_id": snapshot.ball_context_id,
            "ball_available": snapshot.ball_available,
            "ball_position_m": None if ball is None else [ball.x_m, ball.y_m, ball.z_m],
            "robot_pose": None
            if robot is None
            else {
                "frame_id": "field",
                "x_m": robot.x_m,
                "y_m": robot.y_m,
                "yaw_rad": robot.yaw_rad,
            },
            "robot_stable": snapshot.robot_stable,
            "emergency": snapshot.emergency,
            "motion_enabled": self.config.motion_enabled,
        }

    def dispatch_payload(
        self, request: Mapping[str, Any], now: float
    ) -> dict[str, Any]:
        outcome = self.dispatch(request, now)
        target = outcome.field_target
        return {
            "schema_version": DISPATCH_RESPONSE_SCHEMA,
            "accepted": outcome.accepted,
            "reason": outcome.reason,
            "field_target": None
            if target is None
            else {
                "frame_id": target.frame_id,
                "x_m": target.x_m,
                "y_m": target.y_m,
                "yaw_rad": target.yaw_rad,
            },
        }
