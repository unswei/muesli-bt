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
import threading
import time
from pathlib import Path
from typing import Any, Dict, List, Optional, Sequence, Set, Tuple

import pybullet as p
import pybullet_data


SCHEMA_VERSION = "racecar_demo.v1"
PLANNER_SCHEMA_VERSION = "planner.v1"

REQUIRED_LOG_FIELDS = {
    "schema_version",
    "run_id",
    "tick_index",
    "sim_time_s",
    "wall_time_s",
    "mode",
    "state",
    "goal",
    "distance_to_goal",
    "collision_imminent",
    "action",
    "collisions_total",
    "goal_reached",
}
OPTIONAL_LOG_FIELDS = {"bt", "planner"}

STEERING_JOINTS = (4, 6)
DRIVE_JOINTS = (2, 3, 5, 7)


def clamp(value: float, lo: float, hi: float) -> float:
    return max(lo, min(hi, value))


def now_utc_iso8601() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


@dataclasses.dataclass
class Action:
    steering: float
    throttle: float


@dataclasses.dataclass
class CarState:
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


def validate_log_record_v1(record: Dict[str, object]) -> None:
    missing = REQUIRED_LOG_FIELDS.difference(record.keys())
    if missing:
        raise ValueError(f"racecar_demo.v1 missing required fields: {sorted(missing)}")
    extra = set(record.keys()).difference(REQUIRED_LOG_FIELDS.union(OPTIONAL_LOG_FIELDS))
    if extra:
        raise ValueError(f"racecar_demo.v1 unexpected top-level fields: {sorted(extra)}")
    if record.get("schema_version") != SCHEMA_VERSION:
        raise ValueError(f"racecar_demo.v1 schema_version mismatch: {record.get('schema_version')}")


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


def car_state(client_id: int, car_id: int) -> CarState:
    position, orientation = p.getBasePositionAndOrientation(car_id, physicsClientId=client_id)
    linear_velocity, _ = p.getBaseVelocity(car_id, physicsClientId=client_id)
    yaw = p.getEulerFromQuaternion(orientation)[2]
    forward_x = math.cos(yaw)
    forward_y = math.sin(yaw)
    speed = (linear_velocity[0] * forward_x) + (linear_velocity[1] * forward_y)
    return CarState(x=position[0], y=position[1], yaw=yaw, speed=speed)


def apply_action(
    client_id: int,
    car_id: int,
    action: Action,
    max_speed: float,
    steering_force: float,
    drive_force: float,
) -> None:
    steering = clamp(action.steering, -1.0, 1.0)
    throttle = clamp(action.throttle, -1.0, 1.0)
    target_velocity = max_speed * throttle
    target_steer = 0.55 * steering

    for joint in STEERING_JOINTS:
        p.setJointMotorControl2(
            car_id,
            joint,
            p.POSITION_CONTROL,
            targetPosition=target_steer,
            force=steering_force,
            physicsClientId=client_id,
        )

    for joint in DRIVE_JOINTS:
        p.setJointMotorControl2(
            car_id,
            joint,
            p.VELOCITY_CONTROL,
            targetVelocity=target_velocity,
            force=drive_force,
            physicsClientId=client_id,
        )


def update_follow_camera(
    client_id: int,
    state: CarState,
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


def make_box_obstacle(client_id: int, center_xy: Tuple[float, float], half_xy: Tuple[float, float]) -> Obstacle:
    half_x, half_y = half_xy
    collision_id = p.createCollisionShape(
        p.GEOM_BOX,
        halfExtents=[half_x, half_y, 0.35],
        physicsClientId=client_id,
    )
    visual_id = p.createVisualShape(
        p.GEOM_BOX,
        halfExtents=[half_x, half_y, 0.35],
        rgbaColor=[0.8, 0.25, 0.2, 0.95],
        physicsClientId=client_id,
    )
    body_id = p.createMultiBody(
        baseMass=0.0,
        baseCollisionShapeIndex=collision_id,
        baseVisualShapeIndex=visual_id,
        basePosition=[center_xy[0], center_xy[1], 0.35],
        physicsClientId=client_id,
    )
    return Obstacle(center_x=center_xy[0], center_y=center_xy[1], half_x=half_x, half_y=half_y, body_id=body_id)


def add_goal_marker(client_id: int, goal_xy: Tuple[float, float]) -> int:
    visual_id = p.createVisualShape(
        p.GEOM_CYLINDER,
        radius=0.30,
        length=0.02,
        rgbaColor=[0.2, 0.9, 0.2, 0.90],
        physicsClientId=client_id,
    )
    return p.createMultiBody(
        baseMass=0.0,
        baseVisualShapeIndex=visual_id,
        basePosition=[goal_xy[0], goal_xy[1], 0.02],
        physicsClientId=client_id,
    )


def raycast_observation(
    client_id: int,
    state: CarState,
    ray_angles_deg: Sequence[float],
    ray_length: float,
) -> Tuple[List[float], List[Tuple[Tuple[float, float, float], Tuple[float, float, float], float]]]:
    rays_from = []
    rays_to = []
    base_from = (state.x, state.y, 0.20)
    for angle_deg in ray_angles_deg:
        angle = state.yaw + math.radians(angle_deg)
        end = (
            state.x + ray_length * math.cos(angle),
            state.y + ray_length * math.sin(angle),
            0.20,
        )
        rays_from.append(base_from)
        rays_to.append(end)

    results = p.rayTestBatch(rays_from, rays_to, physicsClientId=client_id)
    distances: List[float] = []
    segments: List[Tuple[Tuple[float, float, float], Tuple[float, float, float], float]] = []
    for ray_from, ray_to, result in zip(rays_from, rays_to, results):
        fraction = result[2]
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


def draw_debug(
    client_id: int,
    state: CarState,
    goal_xy: Tuple[float, float],
    ray_segments: Sequence[Tuple[Tuple[float, float, float], Tuple[float, float, float], float]],
    debug_items: List[int],
) -> List[int]:
    for item in debug_items:
        p.removeUserDebugItem(item, physicsClientId=client_id)

    new_items: List[int] = []
    goal_line_id = p.addUserDebugLine(
        [state.x, state.y, 0.3],
        [goal_xy[0], goal_xy[1], 0.3],
        [0.2, 0.9, 0.2],
        lineWidth=2.0,
        lifeTime=0.12,
        physicsClientId=client_id,
    )
    new_items.append(goal_line_id)

    for ray_from, ray_to, fraction in ray_segments:
        color = [0.2, 0.8, 1.0] if fraction >= 0.999 else [1.0, 0.2, 0.2]
        line_id = p.addUserDebugLine(
            list(ray_from),
            list(ray_to),
            color,
            lineWidth=1.5,
            lifeTime=0.12,
            physicsClientId=client_id,
        )
        new_items.append(line_id)

    return new_items


def _update_key_flag(events: Dict[int, int], keys: Sequence[int], current: bool) -> bool:
    state = current
    for key in keys:
        mask = events.get(key, 0)
        if mask & p.KEY_WAS_TRIGGERED:
            state = True
        if mask & p.KEY_WAS_RELEASED:
            state = False
        if mask & p.KEY_IS_DOWN:
            state = True
    return state


def poll_pybullet_key_state(
    client_id: int,
    key_state: Dict[str, bool],
) -> Tuple[Action, Dict[str, bool], Dict[int, int]]:
    events = p.getKeyboardEvents(physicsClientId=client_id)

    throttle_up_keys = (p.B3G_UP_ARROW, ord("w"), ord("W"), ord("i"), ord("I"))
    throttle_down_keys = (p.B3G_DOWN_ARROW, ord("s"), ord("S"), ord("k"), ord("K"))
    steer_left_keys = (p.B3G_LEFT_ARROW, ord("a"), ord("A"), ord("j"), ord("J"))
    steer_right_keys = (p.B3G_RIGHT_ARROW, ord("d"), ord("D"), ord("l"), ord("L"))
    brake_keys = (ord(" "),)

    updated = dict(key_state)
    updated["forward"] = _update_key_flag(events, throttle_up_keys, updated["forward"])
    updated["backward"] = _update_key_flag(events, throttle_down_keys, updated["backward"])
    updated["left"] = _update_key_flag(events, steer_left_keys, updated["left"])
    updated["right"] = _update_key_flag(events, steer_right_keys, updated["right"])
    updated["brake"] = _update_key_flag(events, brake_keys, updated.get("brake", False))
    if updated["brake"]:
        updated["forward"] = False
        updated["backward"] = False
        updated["left"] = False
        updated["right"] = False

    return action_from_key_state(updated), updated, events


class PynputKeyboardProvider:
    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._pressed: Set[str] = set()
        self._listener = None

        try:
            from pynput import keyboard as pynput_keyboard  # type: ignore
        except Exception:
            return

        self._keyboard = pynput_keyboard
        self._listener = pynput_keyboard.Listener(on_press=self._on_press, on_release=self._on_release)
        self._listener.daemon = True
        self._listener.start()

    @property
    def available(self) -> bool:
        return self._listener is not None

    def close(self) -> None:
        if self._listener is not None:
            self._listener.stop()

    def snapshot(self) -> Dict[str, bool]:
        with self._lock:
            return {
                "forward": "forward" in self._pressed,
                "backward": "backward" in self._pressed,
                "left": "left" in self._pressed,
                "right": "right" in self._pressed,
                "brake": "brake" in self._pressed,
            }

    def _key_to_action(self, key: object) -> Optional[str]:
        keyboard = getattr(self, "_keyboard", None)
        if keyboard is None:
            return None

        try:
            if key == keyboard.Key.up:
                return "forward"
            if key == keyboard.Key.down:
                return "backward"
            if key == keyboard.Key.left:
                return "left"
            if key == keyboard.Key.right:
                return "right"
            if key == keyboard.Key.space:
                return "brake"
        except Exception:
            return None

        char = getattr(key, "char", None)
        if not isinstance(char, str):
            return None
        lower = char.lower()
        if lower in {"w", "i"}:
            return "forward"
        if lower in {"s", "k"}:
            return "backward"
        if lower in {"a", "j"}:
            return "left"
        if lower in {"d", "l"}:
            return "right"
        return None

    def _on_press(self, key: object) -> None:
        action = self._key_to_action(key)
        if action is None:
            return
        with self._lock:
            self._pressed.add(action)

    def _on_release(self, key: object) -> None:
        action = self._key_to_action(key)
        if action is None:
            return
        with self._lock:
            self._pressed.discard(action)


def action_from_key_state(key_state: Dict[str, bool]) -> Action:
    if key_state.get("brake", False):
        return Action(steering=0.0, throttle=0.0)
    throttle = float(int(bool(key_state.get("forward"))) - int(bool(key_state.get("backward"))))
    steering = float(int(bool(key_state.get("left"))) - int(bool(key_state.get("right"))))
    return Action(steering=steering, throttle=throttle)


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


def build_muesli_bt_dsl(mode: str, args: argparse.Namespace) -> str:
    if mode == "bt_basic":
        return "(seq (act constant-drive action 0.0 0.45) (act apply-action action) (running))"
    if mode == "bt_obstacles":
        return (
            "(sel "
            "  (seq (cond goal-reached-racecar) (succeed)) "
            "  (sel "
            "    (seq (cond collision-imminent) (act avoid-obstacle rays action) (running)) "
            "    (seq (act drive-to-goal goal action) (act apply-action action) (running))))"
        )
    if mode == "bt_planner":
        budget_ms = max(1, int(round(args.budget_ms)))
        return (
            "(sel "
            "  (seq (cond goal-reached-racecar) (succeed)) "
            "  (sel "
            "    (seq (cond collision-imminent) (act avoid-obstacle rays action) (running)) "
            "    (seq "
            "      (plan-action "
            "        :name \"racecar-plan\" "
            f"        :budget_ms {budget_ms} "
            f"        :iters_max {int(args.iters_max)} "
            f"        :max_depth {int(args.max_depth)} "
            f"        :gamma {float(args.gamma)} "
            f"        :pw_k {float(args.pw_k)} "
            f"        :pw_alpha {float(args.pw_alpha)} "
            "        :model_service \"racecar-kinematic-v1\" "
            "        :state_key state "
            "        :action_key action "
            "        :meta_key plan-meta "
            "        :top_k 5) "
            "      (act apply-action action) "
            "      (running))))"
        )
    raise ValueError(f"Unsupported BT mode: {mode}")


def planner_payload_from_meta(meta_json: str) -> Optional[Dict[str, object]]:
    try:
        meta = json.loads(meta_json)
    except Exception:
        return None
    if not isinstance(meta, dict):
        return None

    action_list = meta.get("action", [0.0, 0.0])
    if not isinstance(action_list, list):
        action_list = [0.0, 0.0]
    steering = float(action_list[0]) if len(action_list) > 0 else 0.0
    throttle = float(action_list[1]) if len(action_list) > 1 else 0.0
    return {
        "schema_version": PLANNER_SCHEMA_VERSION,
        "budget_ms": float(meta.get("budget_ms", 0.0)),
        "time_used_ms": float(meta.get("time_used_ms", 0.0)),
        "iters": int(meta.get("iters", 0)),
        "root_visits": int(meta.get("root_visits", 0)),
        "root_children": int(meta.get("root_children", 0)),
        "widen_added": int(meta.get("widen_added", 0)),
        "depth_max": int(meta.get("depth_max", 0)),
        "depth_mean": float(meta.get("depth_mean", 0.0)),
        "status": str(meta.get("status", "noaction")),
        "confidence": float(meta.get("confidence", 0.0)),
        "value_est": float(meta.get("value_est", 0.0)),
        "action": {"steering": steering, "throttle": throttle},
        "top_k": [],
    }


class BridgeRacecarSimAdapter:
    def __init__(
        self,
        client_id: int,
        car_id: int,
        goal_xy: Tuple[float, float],
        obstacles: Sequence[Obstacle],
        sink: JsonlSink,
        run_id: str,
        mode: str,
        max_speed: float,
        steering_force: float,
        drive_force: float,
        tick_hz: float,
        draw_debug_enabled: bool,
        follow_camera: bool,
        camera_distance: float,
        camera_yaw: float,
        camera_pitch: float,
        camera_target_z: float,
    ) -> None:
        self.client_id = client_id
        self.car_id = car_id
        self.goal_xy = goal_xy
        self.obstacles = list(obstacles)
        self.obstacle_body_ids = {obs.body_id for obs in obstacles}
        self.sink = sink
        self.run_id = run_id
        self.mode = mode
        self.max_speed = max_speed
        self.steering_force = steering_force
        self.drive_force = drive_force
        self.tick_hz = tick_hz
        self.draw_debug_enabled = draw_debug_enabled
        self.follow_camera = follow_camera
        self.camera_distance = camera_distance
        self.camera_yaw = camera_yaw
        self.camera_pitch = camera_pitch
        self.camera_target_z = camera_target_z

        self.ray_angles = [-45.0, -25.0, -10.0, 0.0, 10.0, 25.0, 45.0]
        self.ray_length = 3.0
        self.collision_count = 0
        self.wall_start = time.perf_counter()
        self.debug_items: List[int] = []
        self._last_state: Optional[CarState] = None
        self._last_ray_segments: List[Tuple[Tuple[float, float, float], Tuple[float, float, float], float]] = []
        self._stop_requested = False

    def reset(self) -> None:
        p.resetBasePositionAndOrientation(self.car_id, [0.0, 0.0, 0.20], [0.0, 0.0, 0.0, 1.0], physicsClientId=self.client_id)
        p.resetBaseVelocity(self.car_id, [0.0, 0.0, 0.0], [0.0, 0.0, 0.0], physicsClientId=self.client_id)
        self.collision_count = 0
        self.wall_start = time.perf_counter()
        self.debug_items = []
        self._last_state = None
        self._last_ray_segments = []
        self._stop_requested = False
        if self.follow_camera:
            self._update_camera(car_state(self.client_id, self.car_id))

    def get_state(self) -> Dict[str, object]:
        state = car_state(self.client_id, self.car_id)
        if self.follow_camera:
            self._update_camera(state)
        ray_distances, ray_segments = raycast_observation(self.client_id, state, self.ray_angles, self.ray_length)
        self._last_state = state
        self._last_ray_segments = ray_segments
        collision_imminent = min(ray_distances) < 0.9 if ray_distances else False
        state_vec = [
            float(state.x),
            float(state.y),
            float(state.yaw),
            float(state.speed),
            float(self.goal_xy[0]),
            float(self.goal_xy[1]),
            *[float(v) for v in ray_distances],
        ]
        return {
            "state_schema": "racecar_state.v1",
            "state_vec": state_vec,
            "x": float(state.x),
            "y": float(state.y),
            "yaw": float(state.yaw),
            "speed": float(state.speed),
            "rays": [float(v) for v in ray_distances],
            "goal": [float(self.goal_xy[0]), float(self.goal_xy[1])],
            "collision_imminent": bool(collision_imminent),
            "collision_count": int(self.collision_count),
            "t_ms": int((time.perf_counter() - self.wall_start) * 1000.0),
        }

    def apply_action(self, action: Tuple[float, float]) -> None:
        steering = float(action[0])
        throttle = float(action[1])
        globals()["apply_action"](
            self.client_id,
            self.car_id,
            Action(steering=steering, throttle=throttle),
            max_speed=self.max_speed,
            steering_force=self.steering_force,
            drive_force=self.drive_force,
        )

    def step(self, steps: int) -> None:
        step_count = max(1, int(steps))
        for _ in range(step_count):
            p.stepSimulation(physicsClientId=self.client_id)
            contacts = p.getContactPoints(bodyA=self.car_id, physicsClientId=self.client_id)
            if any(cp[2] in self.obstacle_body_ids for cp in contacts):
                self.collision_count += 1
        if self.follow_camera:
            self._update_camera(car_state(self.client_id, self.car_id))

    def debug_draw(self) -> None:
        if not self.draw_debug_enabled:
            return
        if self._last_state is None:
            return
        self.debug_items = draw_debug(
            self.client_id,
            self._last_state,
            self.goal_xy,
            self._last_ray_segments,
            self.debug_items,
        )

    def stop_requested(self) -> bool:
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

    def on_tick_record(self, payload: Dict[str, object]) -> None:
        record: Dict[str, object] = {
            "schema_version": SCHEMA_VERSION,
            "run_id": self.run_id,
            "tick_index": int(payload["tick_index"]),
            "sim_time_s": float(payload["sim_time_s"]),
            "wall_time_s": float(payload["wall_time_s"]),
            "mode": self.mode,
            "state": payload["state"],
            "goal": payload["goal"],
            "distance_to_goal": float(payload["distance_to_goal"]),
            "collision_imminent": bool(payload["collision_imminent"]),
            "action": payload["action"],
            "collisions_total": int(payload["collisions_total"]),
            "goal_reached": bool(payload["goal_reached"]),
        }

        bt_status = payload.get("bt_status")
        if isinstance(bt_status, str):
            record["bt"] = {"status": bt_status, "active_path": [], "node_status": {}}

        planner_meta = payload.get("planner_meta_json")
        if isinstance(planner_meta, str) and planner_meta:
            planner = planner_payload_from_meta(planner_meta)
            if planner is not None:
                record["planner"] = planner

        validate_log_record_v1(record)
        self.sink.write(record)

    def _update_camera(self, state: CarState) -> None:
        update_follow_camera(
            self.client_id,
            state,
            distance=self.camera_distance,
            yaw=self.camera_yaw,
            pitch=self.camera_pitch,
            target_z=self.camera_target_z,
        )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="PyBullet racecar showcase demo for muesli-bt style BT + bounded-time MCTS.")
    parser.add_argument("--mode", choices=("manual", "bt_basic", "bt_obstacles", "bt_planner"), default="manual")
    parser.add_argument("--duration-sec", type=float, default=35.0)
    parser.add_argument("--physics-hz", type=float, default=240.0)
    parser.add_argument("--tick-hz", type=float, default=20.0)
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("--headless", action="store_true", help="Run in DIRECT mode without GUI.")
    parser.add_argument("--no-sleep", action="store_true", help="Disable wall-clock sleeps between physics steps.")
    parser.add_argument("--max-speed", type=float, default=60.0, help="Wheel target velocity scale.")
    parser.add_argument("--steering-force", type=float, default=30.0, help="Steering motor force.")
    parser.add_argument("--drive-force", type=float, default=35.0, help="Drive motor force.")
    parser.add_argument("--goal-x", type=float, default=7.0)
    parser.add_argument("--goal-y", type=float, default=3.0)
    parser.add_argument("--budget-ms", type=float, default=20.0, help="Planner budget per tick in milliseconds.")
    parser.add_argument("--iters-max", type=int, default=1200, help="Planner maximum iterations per tick.")
    parser.add_argument("--max-depth", type=int, default=18, help="Planner rollout depth.")
    parser.add_argument("--pw-k", type=float, default=2.0, help="Progressive widening k.")
    parser.add_argument("--pw-alpha", type=float, default=0.5, help="Progressive widening alpha.")
    parser.add_argument("--gamma", type=float, default=0.96, help="Planner discount factor.")
    parser.add_argument(
        "--keyboard-backend",
        choices=("auto", "pybullet", "pynput"),
        default="auto",
        help="Manual mode keyboard input backend.",
    )
    parser.add_argument(
        "--manual-realtime-speed",
        type=float,
        default=4.0,
        help="Manual mode simulation speed multiplier (1.0 = real-time).",
    )
    parser.add_argument(
        "--manual-action-scale",
        type=float,
        default=4.0,
        help="Manual mode actuator scaling (multiplies max-speed and drive-force).",
    )
    parser.add_argument(
        "--bt-sim-speed",
        type=float,
        default=1.0,
        help="BT mode simulation speed multiplier (scales physics steps per BT tick).",
    )
    parser.add_argument(
        "--follow-camera",
        dest="follow_camera",
        action="store_true",
        default=True,
        help="Keep the camera locked to the car in GUI mode.",
    )
    parser.add_argument(
        "--no-follow-camera",
        dest="follow_camera",
        action="store_false",
        help="Disable camera follow behavior.",
    )
    parser.add_argument("--camera-distance", type=float, default=3.8, help="Follow-camera distance.")
    parser.add_argument("--camera-yaw", type=float, default=50.0, help="Follow-camera yaw angle in degrees.")
    parser.add_argument("--camera-pitch", type=float, default=-35.0, help="Follow-camera pitch angle in degrees.")
    parser.add_argument("--camera-target-z", type=float, default=0.35, help="Follow-camera target Z.")
    parser.add_argument("--log-path", type=Path, default=None, help="Optional explicit JSONL log file path.")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    random.seed(args.seed)

    root_dir = Path(__file__).resolve().parent
    repo_root = root_dir.parent.parent
    logs_dir = root_dir / "logs"
    logs_dir.mkdir(parents=True, exist_ok=True)
    out_dir = root_dir / "out"
    out_dir.mkdir(parents=True, exist_ok=True)

    run_id = f"{time.strftime('%Y%m%d_%H%M%S')}_{args.mode}_seed{args.seed}"
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
        car_id = p.loadURDF("racecar/racecar.urdf", [0.0, 0.0, 0.20], physicsClientId=client_id)
        if not args.headless and args.follow_camera:
            update_follow_camera(
                client_id,
                car_state(client_id, car_id),
                distance=args.camera_distance,
                yaw=args.camera_yaw,
                pitch=args.camera_pitch,
                target_z=args.camera_target_z,
            )

        goal_xy = (args.goal_x, args.goal_y)
        goal_body_id = add_goal_marker(client_id, goal_xy)
        _ = goal_body_id

        obstacles: List[Obstacle] = []
        if args.mode in ("bt_obstacles", "bt_planner"):
            obstacle_specs = [
                ((2.0, 0.9), (0.40, 0.55)),
                ((3.7, -0.9), (0.45, 0.45)),
                ((5.0, 1.1), (0.35, 0.70)),
                ((6.0, -0.3), (0.30, 0.45)),
            ]
            obstacles = [make_box_obstacle(client_id, center, half) for center, half in obstacle_specs]
        obstacle_body_ids = {obs.body_id for obs in obstacles}

        if args.mode != "manual":
            bridge = import_bridge_module(repo_root)
            runtime = bridge.Runtime()
            runtime.reset()

            adapter = BridgeRacecarSimAdapter(
                client_id=client_id,
                car_id=car_id,
                goal_xy=goal_xy,
                obstacles=obstacles,
                sink=sink,
                run_id=run_id,
                mode=args.mode,
                max_speed=args.max_speed,
                steering_force=args.steering_force,
                drive_force=args.drive_force,
                tick_hz=args.tick_hz,
                draw_debug_enabled=not args.headless,
                follow_camera=bool((not args.headless) and args.follow_camera),
                camera_distance=args.camera_distance,
                camera_yaw=args.camera_yaw,
                camera_pitch=args.camera_pitch,
                camera_target_z=args.camera_target_z,
            )
            runtime.install_racecar_demo_extensions(adapter)

            tree_dsl = build_muesli_bt_dsl(args.mode, args)
            tree_handle = runtime.compile_bt(tree_dsl)
            instance_handle = runtime.new_instance(tree_handle)

            steps_per_tick = max(1, int(round((args.physics_hz / args.tick_hz) * max(1.0, args.bt_sim_speed))))
            max_ticks = max(1, int(round(args.duration_sec * args.tick_hz)))
            loop_result = runtime.run_loop(
                instance_handle,
                {
                    "tick_hz": float(args.tick_hz),
                    "max_ticks": int(max_ticks),
                    "state_key": "state",
                    "action_key": "action",
                    "steps_per_tick": int(steps_per_tick),
                    "safe_action": [0.0, 0.0],
                    "draw_debug": bool(not args.headless),
                    "mode": args.mode,
                    "planner_meta_key": "plan-meta",
                    "run_id": run_id,
                },
            )

            summary = {
                "run_id": run_id,
                "mode": args.mode,
                "status": loop_result.get("status", "error"),
                "reason": loop_result.get("reason", ""),
                "ticks": int(loop_result.get("ticks", 0)),
                "collisions_total": int(loop_result.get("collisions_total", 0)),
                "goal_reached": bool(loop_result.get("goal_reached", False)),
                "fallback_count": int(loop_result.get("fallback_count", 0)),
                "log_path": str(log_path),
                "metadata_path": str(metadata_path),
            }
            print(json.dumps(summary, indent=2))
            return 0

        ray_angles = [-45.0, -25.0, -10.0, 0.0, 10.0, 25.0, 45.0]
        ray_length = 3.0

        current_action = Action(steering=0.0, throttle=0.0)
        manual_steering_slider_id: Optional[int] = None
        manual_throttle_slider_id: Optional[int] = None
        manual_key_state = {"forward": False, "backward": False, "left": False, "right": False, "brake": False}
        manual_input_debug_id = -1
        manual_keyboard_backend = "pybullet"
        manual_pynput: Optional[PynputKeyboardProvider] = None
        if args.mode == "manual" and not args.headless:
            pynput_supported_in_this_mode = not (sys.platform == "darwin")
            if args.keyboard_backend in {"auto", "pynput"}:
                if not pynput_supported_in_this_mode:
                    print(
                        "pynput backend is disabled on macOS GUI runs because it can trigger "
                        "a SIGTRAP with PyBullet. Falling back to pybullet backend."
                    )
                else:
                    manual_pynput = PynputKeyboardProvider()
                    if manual_pynput.available:
                        manual_keyboard_backend = "pynput"
                    elif args.keyboard_backend == "pynput":
                        print("Requested --keyboard-backend=pynput, but pynput is not available. Falling back to pybullet.")
            if args.keyboard_backend == "pybullet":
                manual_keyboard_backend = "pybullet"

            manual_steering_slider_id = p.addUserDebugParameter(
                "manual_steering_slider",
                -1.0,
                1.0,
                0.0,
                physicsClientId=client_id,
            )
            manual_throttle_slider_id = p.addUserDebugParameter(
                "manual_throttle_slider",
                -1.0,
                1.0,
                0.0,
                physicsClientId=client_id,
            )
            print(
                "Manual controls: click the PyBullet window and use arrows or WASD/IJKL. "
                "If keyboard input is not captured, use sliders fallback. "
                f"backend={manual_keyboard_backend} manual_action_scale={max(0.1, float(args.manual_action_scale)):.2f}"
            )

        tick_every_n = max(1, int(round(args.physics_hz / args.tick_hz)))
        realtime_speed = args.manual_realtime_speed
        realtime_speed = max(1.0, realtime_speed)
        manual_action_scale = max(0.1, float(args.manual_action_scale))
        max_steps = int(args.duration_sec * args.physics_hz * realtime_speed)

        tick_index = 0
        collision_count = 0
        success_tick: Optional[int] = None
        wall_start = time.perf_counter()

        for step_idx in range(max_steps):
            p.stepSimulation(physicsClientId=client_id)

            if step_idx % tick_every_n == 0:
                tick_index += 1
                sim_time_s = step_idx / args.physics_hz
                state = car_state(client_id, car_id)
                distance_to_goal = math.hypot(goal_xy[0] - state.x, goal_xy[1] - state.y)

                ray_distances, ray_segments = raycast_observation(client_id, state, ray_angles, ray_length)
                collision_imminent = min(ray_distances) < 0.9

                key_events: Dict[int, int] = {}
                if manual_keyboard_backend == "pynput" and manual_pynput is not None and manual_pynput.available:
                    snapshot = manual_pynput.snapshot()
                    manual_key_state = {
                        "forward": bool(snapshot.get("forward", False)),
                        "backward": bool(snapshot.get("backward", False)),
                        "left": bool(snapshot.get("left", False)),
                        "right": bool(snapshot.get("right", False)),
                        "brake": bool(snapshot.get("brake", False)),
                    }
                    keyboard_control = action_from_key_state(snapshot)
                else:
                    keyboard_control, manual_key_state, key_events = poll_pybullet_key_state(client_id, manual_key_state)

                keyboard_active = any(manual_key_state.values())
                if keyboard_active:
                    current_action = keyboard_control
                    control_source = "keyboard"
                elif manual_steering_slider_id is not None and manual_throttle_slider_id is not None:
                    current_action = Action(
                        steering=p.readUserDebugParameter(manual_steering_slider_id, physicsClientId=client_id),
                        throttle=p.readUserDebugParameter(manual_throttle_slider_id, physicsClientId=client_id),
                    )
                    control_source = "slider"
                else:
                    current_action = keyboard_control
                    control_source = "keyboard"

                if not args.headless:
                    status_text = (
                        f"control={control_source} backend={manual_keyboard_backend}  "
                        f"keys[f={int(manual_key_state['forward'])},b={int(manual_key_state['backward'])},"
                        f"l={int(manual_key_state['left'])},r={int(manual_key_state['right'])},"
                        f"br={int(manual_key_state['brake'])}]  "
                        f"events={len(key_events)}  "
                        f"throttle={current_action.throttle:+.2f} steer={current_action.steering:+.2f}"
                    )
                    manual_input_debug_id = p.addUserDebugText(
                        status_text,
                        [state.x - 0.6, state.y + 0.7, 0.75],
                        textColorRGB=[0.95, 0.95, 0.95],
                        textSize=1.2,
                        lifeTime=0.12,
                        replaceItemUniqueId=manual_input_debug_id,
                        physicsClientId=client_id,
                    )
                if not args.headless and args.follow_camera:
                    update_follow_camera(
                        client_id,
                        state,
                        distance=args.camera_distance,
                        yaw=args.camera_yaw,
                        pitch=args.camera_pitch,
                        target_z=args.camera_target_z,
                    )

                apply_action(
                    client_id,
                    car_id,
                    current_action,
                    max_speed=args.max_speed * manual_action_scale,
                    steering_force=args.steering_force,
                    drive_force=args.drive_force * manual_action_scale,
                )

                contacts = p.getContactPoints(bodyA=car_id, physicsClientId=client_id)
                if any(cp[2] in obstacle_body_ids for cp in contacts):
                    collision_count += 1

                if success_tick is None and distance_to_goal < 0.6:
                    success_tick = tick_index

                record = {
                    "schema_version": SCHEMA_VERSION,
                    "run_id": run_id,
                    "tick_index": tick_index,
                    "sim_time_s": sim_time_s,
                    "wall_time_s": time.perf_counter() - wall_start,
                    "mode": args.mode,
                    "state": dataclasses.asdict(state),
                    "goal": {"x": goal_xy[0], "y": goal_xy[1]},
                    "distance_to_goal": distance_to_goal,
                    "collision_imminent": collision_imminent,
                    "action": {"steering": current_action.steering, "throttle": current_action.throttle},
                    "collisions_total": collision_count,
                    "goal_reached": bool(success_tick is not None),
                }
                validate_log_record_v1(record)
                sink.write(record)

            if not args.no_sleep:
                time.sleep(1.0 / (args.physics_hz * realtime_speed))

        summary = {
            "run_id": run_id,
            "mode": args.mode,
            "ticks": tick_index,
            "collisions_total": collision_count,
            "goal_reached": success_tick is not None,
            "goal_tick": success_tick,
            "log_path": str(log_path),
            "metadata_path": str(metadata_path),
        }
        print(json.dumps(summary, indent=2))
        return 0

    finally:
        if "manual_pynput" in locals() and manual_pynput is not None:
            manual_pynput.close()
        sink.close()
        p.disconnect(physicsClientId=client_id)


if __name__ == "__main__":
    raise SystemExit(main())
