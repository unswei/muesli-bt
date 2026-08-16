#!/usr/bin/env python3
"""Stage the fixed one-K1, one-ball scene for a paper-video shot."""

from __future__ import annotations

import argparse
import asyncio
import json
import pathlib
from typing import Any


class StagingError(RuntimeError):
    """The paper-video scene configuration or physics command failed."""


SUPPORTED_SHOT_SCHEMAS = {
    "humanoid.paper_video_shot.v1",
    "humanoid.paper_video_comparison.v1",
    "humanoid.paper_video_emergency.v1",
}


def _read_shot(path: pathlib.Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except Exception as exc:
        raise StagingError(f"failed to read shot configuration: {exc}") from exc
    if (
        not isinstance(value, dict)
        or value.get("schema_version") not in SUPPORTED_SHOT_SCHEMAS
    ):
        raise StagingError("unsupported shot configuration")
    return value


def build_stage_commands(shot: dict[str, Any]) -> tuple[float, list[dict[str, Any]]]:
    scene = shot.get("simulator_scene")
    if not isinstance(scene, dict):
        raise StagingError("shot has no simulator scene configuration")
    robot_name = scene.get("robot_name")
    robot_body_id = scene.get("robot_body_id")
    ball_body_id = scene.get("ball_body_id")
    reset_wait = scene.get("reset_wait_seconds")
    if not isinstance(robot_name, str) or not robot_name:
        raise StagingError("robot name is invalid")
    if (
        isinstance(robot_body_id, bool)
        or not isinstance(robot_body_id, int)
        or isinstance(ball_body_id, bool)
        or not isinstance(ball_body_id, int)
    ):
        raise StagingError("simulator body IDs must be integers")
    if (
        isinstance(reset_wait, bool)
        or not isinstance(reset_wait, (int, float))
        or float(reset_wait) < 1.0
    ):
        raise StagingError("robot reset wait must be at least one second")

    robot_position = scene.get("robot_start_position_m")
    robot_quaternion = scene.get("robot_start_quaternion_wxyz")
    ball_position = scene.get(
        "original_ball_position_m",
        scene.get("ball_position_m", scene.get("ball_a_position_m")),
    )
    if not isinstance(robot_position, list) or len(robot_position) != 3:
        raise StagingError("robot start position must contain three values")
    if not isinstance(robot_quaternion, list) or len(robot_quaternion) != 4:
        raise StagingError("robot start quaternion must contain four values")
    if not isinstance(ball_position, list) or len(ball_position) != 3:
        raise StagingError("ball position must contain three values")

    commands = [
        {"command": "reset_robot", "params": {"robot_name": robot_name}},
        {
            "command": "set_body_position",
            "params": {
                "body_id": robot_body_id,
                "position": robot_position,
                "is_dragging": True,
            },
        },
        {
            "command": "set_body_rotation",
            "params": {"body_id": robot_body_id, "quat": robot_quaternion},
        },
        {
            "command": "set_body_position",
            "params": {
                "body_id": robot_body_id,
                "is_dragging": False,
                "zero_velocity": True,
            },
        },
        {
            "command": "set_body_position",
            "params": {
                "body_id": ball_body_id,
                "position": ball_position,
                "is_dragging": True,
            },
        },
        {
            "command": "set_body_position",
            "params": {
                "body_id": ball_body_id,
                "is_dragging": False,
                "zero_velocity": True,
            },
        },
    ]
    return float(reset_wait), commands


async def _send(websocket: Any, command: dict[str, Any]) -> None:
    await websocket.send(
        json.dumps(
            {
                "type": "command",
                "command": command["command"],
                "params": command["params"],
            }
        )
    )
    await asyncio.sleep(0.1)


async def stage(shot: dict[str, Any], websocket_url: str) -> None:
    import websockets

    reset_wait, commands = build_stage_commands(shot)
    async with websockets.connect(websocket_url) as websocket:
        await _send(websocket, commands[0])
        await asyncio.sleep(reset_wait)
        for command in commands[1:]:
            await _send(websocket, command)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--shot",
        type=pathlib.Path,
        default=pathlib.Path(__file__).with_name("t1_prototype.json"),
    )
    parser.add_argument("--websocket", default="ws://127.0.0.1:8788")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        asyncio.run(stage(_read_shot(args.shot), args.websocket))
    except StagingError as exc:
        print(f"paper-video scene staging error: {exc}")
        return 2
    print("PASS staged one K1 and one ball for the paper-video shot")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
