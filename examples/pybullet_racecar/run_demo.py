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
from typing import Any, Callable, Dict, List, Optional, Sequence, Set, Tuple

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


def wrap_angle(angle: float) -> float:
    while angle > math.pi:
        angle -= 2.0 * math.pi
    while angle < -math.pi:
        angle += 2.0 * math.pi
    return angle


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


@dataclasses.dataclass
class PlannerConfig:
    budget_ms: float = 20.0
    iters_max: int = 1200
    max_depth: int = 18
    gamma: float = 0.96
    c_ucb: float = 1.2
    pw_k: float = 2.0
    pw_alpha: float = 0.5
    dt: float = 0.10
    max_speed: float = 8.0
    max_steer_rad: float = 0.55
    wheel_base: float = 0.35
    collision_margin: float = 0.45
    top_k: int = 5


@dataclasses.dataclass
class PlannerTopChoice:
    action: Action
    visits: int
    q: float


@dataclasses.dataclass
class PlannerStats:
    iters: int = 0
    root_visits: int = 0
    root_children: int = 0
    widen_added: int = 0
    depth_max: int = 0
    depth_mean: float = 0.0
    time_used_ms: float = 0.0
    value_est: float = 0.0
    top_k: List[PlannerTopChoice] = dataclasses.field(default_factory=list)


@dataclasses.dataclass
class PlannerResult:
    status: str
    action: Action
    confidence: float
    stats: PlannerStats


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


STATUS_SUCCESS = "success"
STATUS_FAILURE = "failure"
STATUS_RUNNING = "running"


@dataclasses.dataclass
class TickContext:
    blackboard: Dict[str, object]
    visited_nodes: List[str] = dataclasses.field(default_factory=list)
    node_status: Dict[str, str] = dataclasses.field(default_factory=dict)


class BtNode:
    def __init__(self, name: str) -> None:
        self.name = name

    def tick(self, ctx: TickContext) -> str:
        raise NotImplementedError

    def _record(self, ctx: TickContext, status: str) -> str:
        ctx.visited_nodes.append(self.name)
        ctx.node_status[self.name] = status
        return status


class ConditionNode(BtNode):
    def __init__(self, name: str, fn: Callable[[TickContext], bool]) -> None:
        super().__init__(name)
        self.fn = fn

    def tick(self, ctx: TickContext) -> str:
        return self._record(ctx, STATUS_SUCCESS if self.fn(ctx) else STATUS_FAILURE)


class ActionNode(BtNode):
    def __init__(self, name: str, fn: Callable[[TickContext], str]) -> None:
        super().__init__(name)
        self.fn = fn

    def tick(self, ctx: TickContext) -> str:
        return self._record(ctx, self.fn(ctx))


class SequenceNode(BtNode):
    def __init__(self, name: str, children: Sequence[BtNode]) -> None:
        super().__init__(name)
        self.children = list(children)

    def tick(self, ctx: TickContext) -> str:
        for child in self.children:
            status = child.tick(ctx)
            if status != STATUS_SUCCESS:
                return self._record(ctx, status)
        return self._record(ctx, STATUS_SUCCESS)


class SelectorNode(BtNode):
    def __init__(self, name: str, children: Sequence[BtNode]) -> None:
        super().__init__(name)
        self.children = list(children)

    def tick(self, ctx: TickContext) -> str:
        for child in self.children:
            status = child.tick(ctx)
            if status == STATUS_SUCCESS or status == STATUS_RUNNING:
                return self._record(ctx, status)
        return self._record(ctx, STATUS_FAILURE)


@dataclasses.dataclass
class MctsEdge:
    action: Action
    next_state: CarState
    reward: float
    done: bool
    child: "MctsNode"
    visits: int = 0
    value_sum: float = 0.0


@dataclasses.dataclass
class MctsNode:
    state: CarState
    visits: int = 0
    value_sum: float = 0.0
    edges: List[MctsEdge] = dataclasses.field(default_factory=list)


class ContinuousMctsPlanner:
    def __init__(self, config: PlannerConfig, rng: random.Random) -> None:
        self.cfg = config
        self.rng = rng
        self._goal = (0.0, 0.0)
        self._obstacles: List[Obstacle] = []
        self._depth_sum = 0
        self._depth_count = 0
        self._depth_max = 0
        self._widen_added = 0

    def plan(self, state: CarState, goal_xy: Tuple[float, float], obstacles: Sequence[Obstacle]) -> PlannerResult:
        self._goal = goal_xy
        self._obstacles = list(obstacles)
        self._depth_sum = 0
        self._depth_count = 0
        self._depth_max = 0
        self._widen_added = 0

        root = MctsNode(state=state)
        started = time.perf_counter()
        iterations = 0
        deadline = started + (self.cfg.budget_ms / 1000.0)

        while iterations < self.cfg.iters_max:
            if time.perf_counter() >= deadline:
                break
            self._simulate(root, depth=0)
            iterations += 1

        elapsed_ms = (time.perf_counter() - started) * 1000.0
        timed_out = iterations < self.cfg.iters_max and elapsed_ms >= self.cfg.budget_ms

        if not root.edges:
            stats = PlannerStats(
                iters=iterations,
                root_visits=root.visits,
                root_children=0,
                widen_added=self._widen_added,
                depth_max=self._depth_max,
                depth_mean=(self._depth_sum / self._depth_count) if self._depth_count else 0.0,
                time_used_ms=elapsed_ms,
                value_est=0.0,
                top_k=[],
            )
            return PlannerResult(status="noaction", action=Action(steering=0.0, throttle=0.0), confidence=0.0, stats=stats)

        sorted_edges = sorted(
            root.edges,
            key=lambda edge: (edge.visits, edge.value_sum / edge.visits if edge.visits else -1.0e18),
            reverse=True,
        )
        best = sorted_edges[0]

        top_k: List[PlannerTopChoice] = []
        for edge in sorted_edges[: self.cfg.top_k]:
            q_val = edge.value_sum / edge.visits if edge.visits else 0.0
            top_k.append(PlannerTopChoice(action=edge.action, visits=edge.visits, q=q_val))

        confidence = float(best.visits) / float(max(1, root.visits))
        value_est = best.value_sum / best.visits if best.visits else 0.0
        stats = PlannerStats(
            iters=iterations,
            root_visits=root.visits,
            root_children=len(root.edges),
            widen_added=self._widen_added,
            depth_max=self._depth_max,
            depth_mean=(self._depth_sum / self._depth_count) if self._depth_count else 0.0,
            time_used_ms=elapsed_ms,
            value_est=value_est,
            top_k=top_k,
        )
        return PlannerResult(
            status="timeout" if timed_out else "ok",
            action=best.action,
            confidence=confidence,
            stats=stats,
        )

    def _simulate(self, node: MctsNode, depth: int) -> float:
        if depth >= self.cfg.max_depth:
            self._update_depth_stats(depth)
            return 0.0

        if self._is_goal(node.state) or self._is_collision(node.state):
            self._update_depth_stats(depth)
            return 0.0

        allow_widen = len(node.edges) < max(1, int(self.cfg.pw_k * (max(1, node.visits) ** self.cfg.pw_alpha)))
        if allow_widen:
            action = self._sample_action()
            next_state, reward, done = self._transition(node.state, action)
            edge = MctsEdge(action=action, next_state=next_state, reward=reward, done=done, child=MctsNode(state=next_state))
            node.edges.append(edge)
            self._widen_added += 1
        else:
            edge = self._select_ucb(node)

        if edge.done:
            continuation = 0.0
            depth_used = depth + 1
        else:
            continuation = self._simulate(edge.child, depth + 1)
            depth_used = depth + 1

        total = edge.reward + (self.cfg.gamma * continuation)
        node.visits += 1
        node.value_sum += total
        edge.visits += 1
        edge.value_sum += total
        self._update_depth_stats(depth_used)
        return total

    def _select_ucb(self, node: MctsNode) -> MctsEdge:
        assert node.edges
        log_n = math.log(max(1, node.visits))
        best_edge = node.edges[0]
        best_score = -1.0e30
        for edge in node.edges:
            if edge.visits == 0:
                return edge
            q_value = edge.value_sum / edge.visits
            ucb = q_value + self.cfg.c_ucb * math.sqrt(log_n / edge.visits)
            if ucb > best_score:
                best_score = ucb
                best_edge = edge
        return best_edge

    def _sample_action(self) -> Action:
        return Action(
            steering=self.rng.uniform(-1.0, 1.0),
            throttle=self.rng.uniform(0.15, 1.0),
        )

    def _transition(self, state: CarState, action: Action) -> Tuple[CarState, float, bool]:
        steering = clamp(action.steering, -1.0, 1.0)
        throttle = clamp(action.throttle, 0.0, 1.0)

        accel = (4.0 * throttle) - (1.25 * state.speed)
        speed_next = clamp(state.speed + accel * self.cfg.dt, 0.0, self.cfg.max_speed)
        yaw_rate = 0.0
        if abs(self.cfg.wheel_base) > 1.0e-6:
            yaw_rate = (speed_next / self.cfg.wheel_base) * math.tan(steering * self.cfg.max_steer_rad)
        yaw_next = wrap_angle(state.yaw + (yaw_rate * self.cfg.dt))
        x_next = state.x + speed_next * math.cos(yaw_next) * self.cfg.dt
        y_next = state.y + speed_next * math.sin(yaw_next) * self.cfg.dt

        next_state = CarState(x=x_next, y=y_next, yaw=yaw_next, speed=speed_next)
        dist_before = self._distance_to_goal(state)
        dist_after = self._distance_to_goal(next_state)
        progress_reward = dist_before - dist_after
        control_penalty = 0.02 * ((steering * steering) + (throttle * throttle))
        collision_penalty = 2.5 if self._is_collision(next_state) else 0.0
        goal_bonus = 1.5 if dist_after < 0.6 else 0.0
        reward = progress_reward - control_penalty - collision_penalty + goal_bonus

        done = self._is_collision(next_state) or (dist_after < 0.6)
        return next_state, reward, done

    def _distance_to_goal(self, state: CarState) -> float:
        dx = self._goal[0] - state.x
        dy = self._goal[1] - state.y
        return math.hypot(dx, dy)

    def _is_goal(self, state: CarState) -> bool:
        return self._distance_to_goal(state) < 0.6

    def _is_collision(self, state: CarState) -> bool:
        for obstacle in self._obstacles:
            if abs(state.x - obstacle.center_x) <= (obstacle.half_x + self.cfg.collision_margin) and abs(
                state.y - obstacle.center_y
            ) <= (obstacle.half_y + self.cfg.collision_margin):
                return True
        return False

    def _update_depth_stats(self, depth: int) -> None:
        self._depth_count += 1
        self._depth_sum += depth
        self._depth_max = max(self._depth_max, depth)


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


def apply_action(client_id: int, car_id: int, action: Action, max_speed: float) -> None:
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
            force=30.0,
            physicsClientId=client_id,
        )

    for joint in DRIVE_JOINTS:
        p.setJointMotorControl2(
            car_id,
            joint,
            p.VELOCITY_CONTROL,
            targetVelocity=target_velocity,
            force=35.0,
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


def build_bt_basic(constant_throttle: float) -> BtNode:
    def apply_constant(ctx: TickContext) -> str:
        ctx.blackboard["action"] = Action(steering=0.0, throttle=constant_throttle)
        return STATUS_SUCCESS

    return ActionNode("ApplyConstantDrive", apply_constant)


def build_bt_obstacle_goal() -> BtNode:
    def collision_imminent(ctx: TickContext) -> bool:
        return bool(ctx.blackboard.get("collision_imminent", False))

    def avoid_obstacle(ctx: TickContext) -> str:
        distances = list(ctx.blackboard.get("ray_distances", []))
        angles = list(ctx.blackboard.get("ray_angles_deg", []))
        if not distances or not angles or len(distances) != len(angles):
            ctx.blackboard["action"] = Action(steering=0.0, throttle=0.0)
            return STATUS_FAILURE

        left_clearance = sum(d for d, a in zip(distances, angles) if a > 0.0)
        right_clearance = sum(d for d, a in zip(distances, angles) if a < 0.0)
        steer = 0.7 if left_clearance >= right_clearance else -0.7
        min_dist = min(distances)
        throttle = 0.15 if min_dist < 0.70 else 0.30
        ctx.blackboard["action"] = Action(steering=steer, throttle=throttle)
        return STATUS_SUCCESS

    def drive_to_goal(ctx: TickContext) -> str:
        state: CarState = ctx.blackboard["state"]  # type: ignore[assignment]
        goal = ctx.blackboard["goal_xy"]  # type: ignore[assignment]
        dx = goal[0] - state.x
        dy = goal[1] - state.y
        distance = math.hypot(dx, dy)
        desired_yaw = math.atan2(dy, dx)
        heading_error = wrap_angle(desired_yaw - state.yaw)
        steering = clamp(1.4 * heading_error, -1.0, 1.0)
        throttle = 0.0 if distance < 0.60 else clamp(0.25 + 0.25 * distance, 0.0, 0.75)
        ctx.blackboard["action"] = Action(steering=steering, throttle=throttle)
        return STATUS_SUCCESS

    return SelectorNode(
        "RootSelector",
        [
            SequenceNode(
                "AvoidBranch",
                [
                    ConditionNode("CollisionImminent?", collision_imminent),
                    ActionNode("AvoidObstacle", avoid_obstacle),
                ],
            ),
            SequenceNode("GoalBranch", [ActionNode("DriveToGoal", drive_to_goal)]),
        ],
    )


def build_bt_planner(planner: ContinuousMctsPlanner) -> BtNode:
    def collision_imminent(ctx: TickContext) -> bool:
        return bool(ctx.blackboard.get("collision_imminent", False))

    def avoid_obstacle(ctx: TickContext) -> str:
        distances = list(ctx.blackboard.get("ray_distances", []))
        angles = list(ctx.blackboard.get("ray_angles_deg", []))
        if not distances or not angles or len(distances) != len(angles):
            ctx.blackboard["action"] = Action(steering=0.0, throttle=0.0)
            return STATUS_FAILURE
        left_clearance = sum(d for d, a in zip(distances, angles) if a > 0.0)
        right_clearance = sum(d for d, a in zip(distances, angles) if a < 0.0)
        steer = 0.8 if left_clearance >= right_clearance else -0.8
        min_dist = min(distances)
        throttle = 0.12 if min_dist < 0.80 else 0.30
        ctx.blackboard["action"] = Action(steering=steer, throttle=throttle)
        ctx.blackboard["planner_result"] = None
        return STATUS_SUCCESS

    def plan_action(ctx: TickContext) -> str:
        state: CarState = ctx.blackboard["state"]  # type: ignore[assignment]
        goal_xy = ctx.blackboard["goal_xy"]  # type: ignore[assignment]
        obstacles = ctx.blackboard["obstacles"]  # type: ignore[assignment]
        result = planner.plan(state=state, goal_xy=goal_xy, obstacles=obstacles)
        ctx.blackboard["planner_result"] = result
        if result.status == "noaction":
            ctx.blackboard["action"] = Action(steering=0.0, throttle=0.0)
            return STATUS_FAILURE
        ctx.blackboard["action"] = result.action
        return STATUS_SUCCESS

    def apply_planned_action(ctx: TickContext) -> str:
        if "action" not in ctx.blackboard:
            return STATUS_FAILURE
        return STATUS_SUCCESS

    return SelectorNode(
        "RootSelector",
        [
            SequenceNode(
                "AvoidBranch",
                [
                    ConditionNode("CollisionImminent?", collision_imminent),
                    ActionNode("AvoidObstacle", avoid_obstacle),
                ],
            ),
            SequenceNode(
                "PlannerBranch",
                [
                    ActionNode("PlanActionNode", plan_action),
                    ActionNode("ApplyAction", apply_planned_action),
                ],
            ),
        ],
    )


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
        tick_hz: float,
        draw_debug_enabled: bool,
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
        self.tick_hz = tick_hz
        self.draw_debug_enabled = draw_debug_enabled

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

    def get_state(self) -> Dict[str, object]:
        state = car_state(self.client_id, self.car_id)
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
        )

    def step(self, steps: int) -> None:
        step_count = max(1, int(steps))
        for _ in range(step_count):
            p.stepSimulation(physicsClientId=self.client_id)
            contacts = p.getContactPoints(bodyA=self.car_id, physicsClientId=self.client_id)
            if any(cp[2] in self.obstacle_body_ids for cp in contacts):
                self.collision_count += 1

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
                tick_hz=args.tick_hz,
                draw_debug_enabled=not args.headless,
            )
            runtime.install_racecar_demo_extensions(adapter)

            tree_dsl = build_muesli_bt_dsl(args.mode, args)
            tree_handle = runtime.compile_bt(tree_dsl)
            instance_handle = runtime.new_instance(tree_handle)

            steps_per_tick = max(1, int(round(args.physics_hz / args.tick_hz)))
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
                f"backend={manual_keyboard_backend}"
            )

        tick_every_n = max(1, int(round(args.physics_hz / args.tick_hz)))
        realtime_speed = args.manual_realtime_speed
        realtime_speed = max(1.0, realtime_speed)
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

                apply_action(client_id, car_id, current_action, max_speed=args.max_speed)

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
