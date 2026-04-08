#!/usr/bin/env python3
from __future__ import annotations

import argparse
import dataclasses
import importlib
import json
import math
import platform
import random
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Any, Dict, List, Optional, Sequence, Tuple

import numpy as np
import pybullet as p
import pybullet_data
from PIL import Image


SCHEMA_VERSION = "pybullet_epuck_goal.v1"
PLANNER_SCHEMA_VERSION = "planner.v1"


def clamp(value: float, lo: float, hi: float) -> float:
    return max(lo, min(hi, value))


def wrap_angle(angle: float) -> float:
    while angle > math.pi:
        angle -= 2.0 * math.pi
    while angle < -math.pi:
        angle += 2.0 * math.pi
    return angle


def now_utc_iso8601() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


def get_git_commit(repo_root: Path) -> str:
    try:
        result = subprocess.run(
            ["git", "rev-parse", "HEAD"],
            cwd=repo_root,
            check=True,
            capture_output=True,
            text=True,
        )
    except Exception:
        return "unknown"
    return result.stdout.strip() or "unknown"


class JsonlSink:
    def __init__(self, path: Path) -> None:
        self.path = path
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self._file = self.path.open("a", encoding="utf-8")

    def write(self, record: Dict[str, object]) -> None:
        self._file.write(json.dumps(record, separators=(",", ":"), ensure_ascii=True))
        self._file.write("\n")
        self._file.flush()

    def close(self) -> None:
        self._file.close()


def import_bridge_module(repo_root: Path) -> Any:
    module_name = "muesli_bt_bridge"
    try:
        return importlib.import_module(module_name)
    except Exception:
        candidate = repo_root / "build" / "dev" / "python"
        if candidate.exists():
            sys.path.insert(0, str(candidate))
            try:
                return importlib.import_module(module_name)
            except Exception as exc:
                raise RuntimeError(
                    f"Failed to import {module_name} from {candidate}. "
                    "Run `make demo-setup` first."
                ) from exc
        raise RuntimeError(
            f"Failed to import {module_name}. "
            "Build/install the bridge first (recommended: `make demo-setup`)."
        )


def planner_payload_from_meta(meta_json: str) -> Optional[Dict[str, object]]:
    try:
        meta = json.loads(meta_json)
    except Exception:
        return None
    if not isinstance(meta, dict):
        return None

    action_u: List[float] = [0.0, 0.0]
    raw_action = meta.get("action")
    action_schema = ""
    if isinstance(raw_action, dict):
        raw_u = raw_action.get("u")
        action_schema = str(raw_action.get("action_schema", ""))
        if isinstance(raw_u, list):
            action_u = [float(v) for v in raw_u[:2]]
    elif isinstance(raw_action, list):
        action_u = [float(v) for v in raw_action[:2]]
    while len(action_u) < 2:
        action_u.append(0.0)

    payload: Dict[str, object] = {
        "schema_version": PLANNER_SCHEMA_VERSION,
        "planner": str(meta.get("planner", "")),
        "status": str(meta.get("status", "noaction")),
        "budget_ms": float(meta.get("budget_ms", 0.0)),
        "time_used_ms": float(meta.get("time_used_ms", 0.0)),
        "work_done": int(meta.get("work_done", meta.get("iters", 0))),
        "confidence": float(meta.get("confidence", 0.0)),
        "action": {"linear_x": action_u[0], "angular_z": action_u[1]},
    }
    if action_schema == "flagship.cmd.v1":
        payload["shared_action"] = {"linear_x": action_u[0], "angular_z": action_u[1]}
    trace = meta.get("trace")
    if isinstance(trace, dict):
        payload["trace"] = trace
    if "overrun" in meta:
        payload["overrun"] = bool(meta.get("overrun"))
    if isinstance(meta.get("note"), str) and meta.get("note"):
        payload["note"] = str(meta.get("note"))
    return payload


@dataclasses.dataclass
class RobotState:
    x: float
    y: float
    yaw: float
    speed: float


@dataclasses.dataclass
class Obstacle:
    center_x: float
    center_y: float
    half_x: float
    half_y: float
    body_id: int


@dataclasses.dataclass
class WheelCommand:
    left_rad_s: float
    right_rad_s: float


def add_goal_marker(client_id: int, goal_xy: Tuple[float, float]) -> int:
    visual_id = p.createVisualShape(
        p.GEOM_CYLINDER,
        radius=0.05,
        length=0.01,
        rgbaColor=[0.2, 0.9, 0.2, 0.95],
        physicsClientId=client_id,
    )
    return p.createMultiBody(
        baseMass=0.0,
        baseVisualShapeIndex=visual_id,
        basePosition=[goal_xy[0], goal_xy[1], 0.01],
        physicsClientId=client_id,
    )


def make_box_obstacle(client_id: int, center_xy: Tuple[float, float], half_xy: Tuple[float, float]) -> Obstacle:
    half_x, half_y = half_xy
    collision_id = p.createCollisionShape(
        p.GEOM_BOX,
        halfExtents=[half_x, half_y, 0.05],
        physicsClientId=client_id,
    )
    visual_id = p.createVisualShape(
        p.GEOM_BOX,
        halfExtents=[half_x, half_y, 0.05],
        rgbaColor=[0.82, 0.28, 0.22, 0.95],
        physicsClientId=client_id,
    )
    body_id = p.createMultiBody(
        baseMass=0.0,
        baseCollisionShapeIndex=collision_id,
        baseVisualShapeIndex=visual_id,
        basePosition=[center_xy[0], center_xy[1], 0.05],
        physicsClientId=client_id,
    )
    return Obstacle(center_x=center_xy[0], center_y=center_xy[1], half_x=half_x, half_y=half_y, body_id=body_id)


def create_epuck_surrogate(client_id: int, start_xy: Tuple[float, float]) -> int:
    collision_id = p.createCollisionShape(
        p.GEOM_BOX,
        halfExtents=[0.035, 0.035, 0.025],
        physicsClientId=client_id,
    )
    visual_id = p.createVisualShape(
        p.GEOM_CYLINDER,
        radius=0.035,
        length=0.05,
        rgbaColor=[0.28, 0.39, 0.92, 0.97],
        physicsClientId=client_id,
    )
    body_id = p.createMultiBody(
        baseMass=0.20,
        baseCollisionShapeIndex=collision_id,
        baseVisualShapeIndex=visual_id,
        basePosition=[start_xy[0], start_xy[1], 0.03],
        baseOrientation=p.getQuaternionFromEuler([0.0, 0.0, 0.0]),
        physicsClientId=client_id,
    )
    p.changeDynamics(
        body_id,
        -1,
        lateralFriction=1.2,
        spinningFriction=0.01,
        rollingFriction=0.0,
        linearDamping=0.10,
        angularDamping=0.18,
        physicsClientId=client_id,
    )
    return body_id


def robot_state(client_id: int, robot_id: int) -> RobotState:
    position, orientation = p.getBasePositionAndOrientation(robot_id, physicsClientId=client_id)
    linear_velocity, _ = p.getBaseVelocity(robot_id, physicsClientId=client_id)
    yaw = p.getEulerFromQuaternion(orientation)[2]
    forward_x = math.cos(yaw)
    forward_y = math.sin(yaw)
    speed = (linear_velocity[0] * forward_x) + (linear_velocity[1] * forward_y)
    return RobotState(x=position[0], y=position[1], yaw=yaw, speed=speed)


def update_follow_camera(
    client_id: int,
    state: RobotState,
    distance: float,
    yaw: float,
    pitch: float,
    target_z: float,
) -> None:
    p.resetDebugVisualizerCamera(
        cameraDistance=float(distance),
        cameraYaw=float(yaw),
        cameraPitch=float(pitch),
        cameraTargetPosition=[float(state.x), float(state.y), float(target_z)],
        physicsClientId=client_id,
    )


def write_camera_png(
    client_id: int,
    target_xy: Tuple[float, float],
    *,
    width: int,
    height: int,
    distance: float,
    yaw: float,
    pitch: float,
    target_z: float,
    out_path: Path,
) -> None:
    view = p.computeViewMatrixFromYawPitchRoll(
        cameraTargetPosition=[float(target_xy[0]), float(target_xy[1]), float(target_z)],
        distance=float(distance),
        yaw=float(yaw),
        pitch=float(pitch),
        roll=0.0,
        upAxisIndex=2,
        physicsClientId=client_id,
    )
    projection = p.computeProjectionMatrixFOV(
        fov=60.0,
        aspect=float(width) / float(height),
        nearVal=0.02,
        farVal=5.0,
    )
    _, _, rgba, _, _ = p.getCameraImage(
        width=width,
        height=height,
        viewMatrix=view,
        projectionMatrix=projection,
        renderer=p.ER_TINY_RENDERER,
        physicsClientId=client_id,
    )
    rgba_array = np.asarray(rgba, dtype=np.uint8).reshape((height, width, 4))
    image = Image.fromarray(rgba_array[:, :, :3], mode="RGB")
    out_path.parent.mkdir(parents=True, exist_ok=True)
    image.save(out_path)


def write_camera_mp4(
    client_id: int,
    frame_targets_xy: Sequence[Tuple[float, float]],
    *,
    width: int,
    height: int,
    distance: float,
    yaw: float,
    pitch: float,
    target_z: float,
    fps: int,
    out_path: Path,
) -> None:
    if len(frame_targets_xy) < 2:
        raise ValueError("Need at least two frames to write an MP4 clip.")
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="pybullet_epuck_video_") as tmp_dir:
        frames_dir = Path(tmp_dir)
        for frame_index, frame_target_xy in enumerate(frame_targets_xy):
            frame_path = frames_dir / f"frame_{frame_index:05d}.png"
            write_camera_png(
                client_id,
                frame_target_xy,
                width=width,
                height=height,
                distance=distance,
                yaw=yaw,
                pitch=pitch,
                target_z=target_z,
                out_path=frame_path,
            )
        try:
            subprocess.run(
                [
                    "ffmpeg",
                    "-y",
                    "-loglevel",
                    "error",
                    "-framerate",
                    str(max(1, fps)),
                    "-i",
                    str(frames_dir / "frame_%05d.png"),
                    "-c:v",
                    "libx264",
                    "-pix_fmt",
                    "yuv420p",
                    str(out_path),
                ],
                check=True,
            )
        except FileNotFoundError as exc:
            raise RuntimeError("ffmpeg is required for --video-path export.") from exc


def raycast_observation(
    client_id: int,
    ignore_body_id: int,
    state: RobotState,
    ray_angles_deg: Sequence[float],
    ray_length: float,
) -> Tuple[List[float], List[Tuple[Tuple[float, float, float], Tuple[float, float, float], float]]]:
    rays_from = []
    rays_to = []
    base_from = (state.x, state.y, 0.04)
    for angle_deg in ray_angles_deg:
        angle = state.yaw + math.radians(angle_deg)
        end = (
            state.x + ray_length * math.cos(angle),
            state.y + ray_length * math.sin(angle),
            0.04,
        )
        rays_from.append(base_from)
        rays_to.append(end)

    results = p.rayTestBatch(rays_from, rays_to, physicsClientId=client_id)
    distances: List[float] = []
    segments: List[Tuple[Tuple[float, float, float], Tuple[float, float, float], float]] = []
    for ray_from, ray_to, result in zip(rays_from, rays_to, results):
        fraction = result[2]
        if int(result[0]) == int(ignore_body_id):
            fraction = 1.0
        if fraction < 0.0:
            fraction = 1.0
        distances.append(ray_length * fraction)
        hit = (
            ray_from[0] + (ray_to[0] - ray_from[0]) * fraction,
            ray_from[1] + (ray_to[1] - ray_from[1]) * fraction,
            ray_from[2] + (ray_to[2] - ray_from[2]) * fraction,
        )
        segments.append((ray_from, hit, fraction))
    return distances, segments


class PyBulletEpuckGoalSim:
    def __init__(
        self,
        client_id: int,
        robot_id: int,
        goal_xy: Tuple[float, float],
        obstacles: Sequence[Obstacle],
        physics_hz: float,
        ray_length: float,
        follow_camera: bool,
        camera_distance: float,
        camera_yaw: float,
        camera_pitch: float,
        camera_target_z: float,
        headless: bool,
    ) -> None:
        self.client_id = client_id
        self.robot_id = robot_id
        self.goal_xy = goal_xy
        self.obstacles = list(obstacles)
        self.obstacle_body_ids = {obs.body_id for obs in obstacles}
        self.physics_hz = physics_hz
        self.dt = 1.0 / physics_hz
        self.ray_angles = [-150.0, -95.0, -45.0, -15.0, 15.0, 45.0, 95.0, 150.0]
        self.ray_length = ray_length
        self.follow_camera = follow_camera
        self.camera_distance = camera_distance
        self.camera_yaw = camera_yaw
        self.camera_pitch = camera_pitch
        self.camera_target_z = camera_target_z
        self.headless = headless

        self.start_xy = (-0.42, -0.40)
        self.start_yaw = 0.40
        self.body_radius = 0.035
        self.wheel_radius = 0.020
        self.half_track = 0.026
        self.max_linear_speed = 0.22
        self.max_angular_speed = 5.0
        self.goal_tolerance = 0.06
        self.collision_count = 0
        self.sim_step_count = 0
        self.wall_start = time.perf_counter()
        self.current_wheel_cmd = WheelCommand(0.0, 0.0)
        self._last_state: Optional[RobotState] = None
        self._last_ray_segments: List[Tuple[Tuple[float, float, float], Tuple[float, float, float], float]] = []
        self._stop_requested = False

    def reset(self) -> None:
        p.resetBasePositionAndOrientation(
            self.robot_id,
            [self.start_xy[0], self.start_xy[1], 0.03],
            p.getQuaternionFromEuler([0.0, 0.0, self.start_yaw]),
            physicsClientId=self.client_id,
        )
        p.resetBaseVelocity(
            self.robot_id,
            [0.0, 0.0, 0.0],
            [0.0, 0.0, 0.0],
            physicsClientId=self.client_id,
        )
        self.collision_count = 0
        self.sim_step_count = 0
        self.wall_start = time.perf_counter()
        self.current_wheel_cmd = WheelCommand(0.0, 0.0)
        self._last_state = None
        self._last_ray_segments = []
        self._stop_requested = False
        if self.follow_camera and not self.headless:
            update_follow_camera(
                self.client_id,
                robot_state(self.client_id, self.robot_id),
                distance=self.camera_distance,
                yaw=self.camera_yaw,
                pitch=self.camera_pitch,
                target_z=self.camera_target_z,
            )

    def observe(self) -> Dict[str, object]:
        state = robot_state(self.client_id, self.robot_id)
        ray_distances, ray_segments = raycast_observation(
            self.client_id,
            self.robot_id,
            state,
            self.ray_angles,
            self.ray_length,
        )
        self._last_state = state
        self._last_ray_segments = ray_segments

        goal_dx = self.goal_xy[0] - state.x
        goal_dy = self.goal_xy[1] - state.y
        goal_dist = math.hypot(goal_dx, goal_dy)
        goal_bearing = wrap_angle(math.atan2(goal_dy, goal_dx) - state.yaw)
        min_ray = min(ray_distances) if ray_distances else self.ray_length
        obstacle_front = clamp(1.0 - (min_ray / self.ray_length), 0.0, 1.0)
        speed_norm = clamp(abs(state.speed) / self.max_linear_speed, 0.0, 1.0)
        goal_reached = goal_dist <= self.goal_tolerance
        collision_imminent = min_ray <= 0.09

        left_clear = sum(ray_distances[4:7])
        right_clear = sum(ray_distances[1:4])
        avoid_turn = 0.90 if left_clear >= right_clear else -0.90
        avoid_linear = -0.10 if min_ray <= 0.06 else 0.08
        direct_linear = 0.0 if goal_dist <= self.goal_tolerance else clamp(0.20 + (0.35 * goal_dist), 0.0, 0.85)
        direct_turn = clamp(0.95 * goal_bearing, -1.0, 1.0)

        return {
            "state": state,
            "goal_dist": goal_dist,
            "goal_bearing": goal_bearing,
            "obstacle_front": obstacle_front,
            "goal_reached": goal_reached,
            "collision_imminent": collision_imminent,
            "planner_state": [goal_dist, goal_bearing, obstacle_front, speed_norm],
            "act_avoid": [avoid_linear, avoid_turn],
            "act_goal_direct": [direct_linear, direct_turn],
            "t_ms": int(round((self.sim_step_count / self.physics_hz) * 1000.0)),
        }

    def apply_shared_action(self, shared_action: Sequence[float]) -> WheelCommand:
        linear_norm = clamp(float(shared_action[0]), -1.0, 1.0)
        angular_norm = clamp(float(shared_action[1]), -1.0, 1.0)
        linear_m_s = self.max_linear_speed * linear_norm
        angular_rad_s = self.max_angular_speed * angular_norm
        left_linear = linear_m_s - (self.half_track * angular_rad_s)
        right_linear = linear_m_s + (self.half_track * angular_rad_s)
        wheel_cmd = WheelCommand(
            left_rad_s=left_linear / self.wheel_radius,
            right_rad_s=right_linear / self.wheel_radius,
        )
        self.current_wheel_cmd = wheel_cmd
        return wheel_cmd

    def step(self, steps: int) -> None:
        step_count = max(1, int(steps))
        for _ in range(step_count):
            state = robot_state(self.client_id, self.robot_id)
            linear_m_s = 0.5 * (self.current_wheel_cmd.left_rad_s + self.current_wheel_cmd.right_rad_s) * self.wheel_radius
            angular_rad_s = (
                (self.current_wheel_cmd.right_rad_s - self.current_wheel_cmd.left_rad_s)
                * self.wheel_radius
                / (2.0 * self.half_track)
            )
            p.resetBaseVelocity(
                self.robot_id,
                [linear_m_s * math.cos(state.yaw), linear_m_s * math.sin(state.yaw), 0.0],
                [0.0, 0.0, angular_rad_s],
                physicsClientId=self.client_id,
            )
            p.stepSimulation(physicsClientId=self.client_id)
            self.sim_step_count += 1
            contacts = p.getContactPoints(bodyA=self.robot_id, physicsClientId=self.client_id)
            if any(cp[2] in self.obstacle_body_ids for cp in contacts):
                self.collision_count += 1
        if self.follow_camera and not self.headless:
            update_follow_camera(
                self.client_id,
                robot_state(self.client_id, self.robot_id),
                distance=self.camera_distance,
                yaw=self.camera_yaw,
                pitch=self.camera_pitch,
                target_z=self.camera_target_z,
            )

    def stop_requested(self) -> bool:
        if self.headless:
            return False
        if self._stop_requested:
            return True
        events = p.getKeyboardEvents(physicsClientId=self.client_id)
        esc = events.get(27, 0)
        q = events.get(ord("q"), 0)
        q_upper = events.get(ord("Q"), 0)
        if (esc & p.KEY_WAS_TRIGGERED) or (q & p.KEY_WAS_TRIGGERED) or (q_upper & p.KEY_WAS_TRIGGERED):
            self._stop_requested = True
            return True
        return False


def branch_payload(branch: str, bt_status: str) -> Dict[str, object]:
    return {
        "status": bt_status,
        "active_path": ["root", branch],
        "node_status": {
            "root": "success" if branch == "goal_reached" else bt_status,
            "branch": branch,
        },
    }


def infer_branch(
    obs: Dict[str, object],
    action_cmd: Optional[Sequence[float]],
    planner_action: Optional[Sequence[float]],
) -> str:
    if bool(obs["goal_reached"]):
        return "goal_reached"
    if bool(obs["collision_imminent"]):
        return "avoid"
    if action_cmd is not None and planner_action is not None:
        if len(action_cmd) >= 2 and len(planner_action) >= 2:
            if all(abs(float(a) - float(b)) <= 1.0e-9 for a, b in zip(action_cmd[:2], planner_action[:2])):
                return "planner"
    return "direct_goal"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="PyBullet e-puck-style differential-drive flagship demo."
    )
    parser.add_argument("--duration-sec", type=float, default=16.0)
    parser.add_argument("--physics-hz", type=float, default=240.0)
    parser.add_argument("--tick-hz", type=float, default=20.0)
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("--headless", action="store_true", help="Run in DIRECT mode without GUI.")
    parser.add_argument("--goal-x", type=float, default=0.37)
    parser.add_argument("--goal-y", type=float, default=0.38)
    parser.add_argument("--ray-length", type=float, default=0.22)
    parser.add_argument(
        "--with-default-obstacles",
        action="store_true",
        help="Enable the checked-in clutter layout instead of the default clear path.",
    )
    parser.add_argument(
        "--follow-camera",
        dest="follow_camera",
        action="store_true",
        default=True,
        help="Keep the camera locked to the robot in GUI mode.",
    )
    parser.add_argument(
        "--no-follow-camera",
        dest="follow_camera",
        action="store_false",
        help="Disable camera follow behaviour.",
    )
    parser.add_argument("--camera-distance", type=float, default=0.95)
    parser.add_argument("--camera-yaw", type=float, default=40.0)
    parser.add_argument("--camera-pitch", type=float, default=-58.0)
    parser.add_argument("--camera-target-z", type=float, default=0.06)
    parser.add_argument("--log-path", type=Path, default=None, help="Optional explicit JSONL log file path.")
    parser.add_argument("--screenshot-path", type=Path, default=None, help="Optional PNG path for a final scene preview.")
    parser.add_argument("--screenshot-target-x", type=float, default=None, help="Optional camera target x for screenshot export.")
    parser.add_argument("--screenshot-target-y", type=float, default=None, help="Optional camera target y for screenshot export.")
    parser.add_argument("--video-path", type=Path, default=None, help="Optional MP4 path for a short overview clip.")
    parser.add_argument("--video-fps", type=int, default=12, help="Frame rate for MP4 export.")
    parser.add_argument("--video-every-nth-tick", type=int, default=2, help="Capture one video frame every N BT ticks.")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    random.seed(args.seed)

    root_dir = Path(__file__).resolve().parent
    repo_root = root_dir.parent.parent
    logs_dir = root_dir / "logs"
    logs_dir.mkdir(parents=True, exist_ok=True)

    run_id = f"{time.strftime('%Y%m%d_%H%M%S')}_bt_flagship_seed{args.seed}"
    log_path = args.log_path if args.log_path else (logs_dir / f"{run_id}.jsonl")
    metadata_path = log_path.with_suffix(".run_metadata.json")

    connection_mode = p.DIRECT if args.headless else p.GUI
    client_id = p.connect(connection_mode)
    if client_id < 0:
        raise RuntimeError("Failed to connect to PyBullet.")

    sink = JsonlSink(log_path)
    config_for_metadata = {
        key: (str(value) if isinstance(value, Path) else value)
        for key, value in vars(args).items()
    }
    metadata = {
        "schema_version": SCHEMA_VERSION,
        "run_id": run_id,
        "created_utc": now_utc_iso8601(),
        "git_commit": get_git_commit(repo_root),
        "platform": platform.platform(),
        "python_version": sys.version.split()[0],
        "pybullet_api_version": p.getAPIVersion(),
        "pybullet_version": getattr(p, "__version__", "unknown"),
        "seed": args.seed,
        "config": config_for_metadata,
    }
    metadata_path.write_text(json.dumps(metadata, indent=2), encoding="utf-8")

    try:
        p.setAdditionalSearchPath(pybullet_data.getDataPath(), physicsClientId=client_id)
        p.setGravity(0.0, 0.0, -9.81, physicsClientId=client_id)
        p.setTimeStep(1.0 / args.physics_hz, physicsClientId=client_id)
        p.loadURDF("plane.urdf", physicsClientId=client_id)

        robot_id = create_epuck_surrogate(client_id, (-0.42, -0.40))
        goal_xy = (args.goal_x, args.goal_y)
        _ = add_goal_marker(client_id, goal_xy)

        obstacle_specs = []
        if args.with_default_obstacles:
            obstacle_specs = [
                ((-0.08, -0.03), (0.03, 0.09)),
                ((0.14, 0.12), (0.03, 0.09)),
            ]
        obstacles = [make_box_obstacle(client_id, center, half) for center, half in obstacle_specs]

        sim = PyBulletEpuckGoalSim(
            client_id=client_id,
            robot_id=robot_id,
            goal_xy=goal_xy,
            obstacles=obstacles,
            physics_hz=args.physics_hz,
            ray_length=args.ray_length,
            follow_camera=bool((not args.headless) and args.follow_camera),
            camera_distance=args.camera_distance,
            camera_yaw=args.camera_yaw,
            camera_pitch=args.camera_pitch,
            camera_target_z=args.camera_target_z,
            headless=args.headless,
        )
        sim.reset()

        bridge = import_bridge_module(repo_root)
        runtime = bridge.Runtime()
        runtime.reset()
        tree_handle = runtime.load_bt_dsl(str(root_dir / "bt" / "flagship_entry.lisp"))
        instance_handle = runtime.new_instance(tree_handle)

        steps_per_tick = max(1, int(round(args.physics_hz / args.tick_hz)))
        max_ticks = max(1, int(round(args.duration_sec * args.tick_hz)))
        wall_start = time.perf_counter()

        summary = {
            "run_id": run_id,
            "mode": "bt_flagship",
            "status": "stopped",
            "reason": "max ticks reached",
            "ticks": 0,
            "goal_reached": False,
            "collisions_total": 0,
            "log_path": str(log_path),
            "metadata_path": str(metadata_path),
        }
        video_targets_xy: List[Tuple[float, float]] = []

        for tick_index in range(1, max_ticks + 1):
            if sim.stop_requested():
                summary["reason"] = "stop requested"
                break

            obs = sim.observe()
            bb_inputs = {
                "goal_reached": bool(obs["goal_reached"]),
                "collision_imminent": bool(obs["collision_imminent"]),
                "planner_state": obs["planner_state"],
                "act_avoid": obs["act_avoid"],
                "act_goal_direct": obs["act_goal_direct"],
                "planner_action": None,
                "plan-meta": None,
                "action_cmd": None,
            }
            bt_status = runtime.tick(instance_handle, bb_inputs)
            action_cmd = runtime.blackboard_get(instance_handle, "action_cmd")
            planner_action = runtime.blackboard_get(instance_handle, "planner_action")
            planner_meta = runtime.blackboard_get(instance_handle, "plan-meta")

            if bool(obs["goal_reached"]):
                shared_action = [0.0, 0.0]
            elif isinstance(action_cmd, list) and len(action_cmd) >= 2:
                shared_action = [float(action_cmd[0]), float(action_cmd[1])]
            else:
                shared_action = [0.0, 0.0]

            wheel_cmd = sim.apply_shared_action(shared_action)
            branch = infer_branch(
                obs,
                action_cmd if isinstance(action_cmd, list) else None,
                planner_action if isinstance(planner_action, list) else None,
            )
            planner = planner_payload_from_meta(planner_meta) if isinstance(planner_meta, str) else None
            state = obs["state"]
            assert isinstance(state, RobotState)

            record: Dict[str, object] = {
                "schema_version": SCHEMA_VERSION,
                "run_id": run_id,
                "tick_index": tick_index,
                "sim_time_s": float(obs["t_ms"]) / 1000.0,
                "wall_time_s": time.perf_counter() - wall_start,
                "mode": "bt_flagship",
                "state": {
                    "x": state.x,
                    "y": state.y,
                    "yaw": state.yaw,
                    "speed": state.speed,
                },
                "goal": {"x": goal_xy[0], "y": goal_xy[1]},
                "distance_to_goal": float(obs["goal_dist"]),
                "collision_imminent": bool(obs["collision_imminent"]),
                "action": {
                    "left_wheel_rad_s": wheel_cmd.left_rad_s,
                    "right_wheel_rad_s": wheel_cmd.right_rad_s,
                },
                "shared_action": {
                    "linear_x": shared_action[0],
                    "angular_z": shared_action[1],
                },
                "collisions_total": sim.collision_count,
                "goal_reached": bool(obs["goal_reached"]),
                "bt": branch_payload(branch, str(bt_status)),
            }
            if planner is not None:
                record["planner"] = planner
            sink.write(record)
            if args.video_path is not None and (tick_index % max(1, args.video_every_nth_tick) == 0):
                if args.screenshot_target_x is not None and args.screenshot_target_y is not None:
                    video_targets_xy.append((float(args.screenshot_target_x), float(args.screenshot_target_y)))
                else:
                    video_targets_xy.append((state.x, state.y))

            sim.step(steps_per_tick)

            summary["ticks"] = tick_index
            summary["goal_reached"] = bool(obs["goal_reached"])
            summary["collisions_total"] = sim.collision_count
            if bool(obs["goal_reached"]):
                summary["status"] = "success"
                summary["reason"] = "goal reached"
                break

        final_state = robot_state(client_id, robot_id)
        summary["final_pose"] = {
            "x": final_state.x,
            "y": final_state.y,
            "yaw": final_state.yaw,
        }
        summary["final_distance_to_goal"] = math.hypot(goal_xy[0] - final_state.x, goal_xy[1] - final_state.y)

        if args.screenshot_path is not None:
            screenshot_target_xy = (final_state.x, final_state.y)
            if args.screenshot_target_x is not None and args.screenshot_target_y is not None:
                screenshot_target_xy = (float(args.screenshot_target_x), float(args.screenshot_target_y))
            write_camera_png(
                client_id,
                screenshot_target_xy,
                width=1280,
                height=720,
                distance=args.camera_distance,
                yaw=args.camera_yaw,
                pitch=args.camera_pitch,
                target_z=args.camera_target_z,
                out_path=args.screenshot_path,
            )
            summary["screenshot_path"] = str(args.screenshot_path)
        if args.video_path is not None:
            if args.screenshot_target_x is not None and args.screenshot_target_y is not None:
                video_targets_xy.append((float(args.screenshot_target_x), float(args.screenshot_target_y)))
            else:
                video_targets_xy.append((final_state.x, final_state.y))
            write_camera_mp4(
                client_id,
                video_targets_xy,
                width=1280,
                height=720,
                distance=args.camera_distance,
                yaw=args.camera_yaw,
                pitch=args.camera_pitch,
                target_z=args.camera_target_z,
                fps=args.video_fps,
                out_path=args.video_path,
            )
            summary["video_path"] = str(args.video_path)

        print(json.dumps(summary, indent=2))
        return 0
    finally:
        sink.close()
        p.disconnect(client_id)


if __name__ == "__main__":
    raise SystemExit(main())
