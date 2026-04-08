#!/usr/bin/env python3
"""Run a live TurtleBot3 Isaac scene against the ROS 2 odom/cmd_vel loop and export media."""

from __future__ import annotations

import argparse
import json
import math
import socket
import subprocess
import time
from pathlib import Path
import shutil

import numpy as np


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--usd-path",
        default="/home/deploy/turtlebot3_imported/turtlebot3_burger/turtlebot3_burger.usda",
        help="Path to the imported TurtleBot3 Burger USD.",
    )
    parser.add_argument(
        "--output-dir",
        default="/home/deploy/isaac_tb3_live_ros2",
        help="Directory for rendered frames, clip, and still image.",
    )
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--state-port", type=int, default=37020)
    parser.add_argument("--command-port", type=int, default=37021)
    parser.add_argument("--tick-hz", type=float, default=20.0)
    parser.add_argument("--max-ticks", type=int, default=180)
    parser.add_argument("--command-timeout-s", type=float, default=0.5)
    parser.add_argument("--capture-start-tick", type=int, default=10)
    parser.add_argument("--capture-count", type=int, default=18)
    parser.add_argument("--capture-stride", type=int, default=1)
    parser.add_argument("--fps", type=int, default=12)
    parser.add_argument("--still-index", type=int, default=8)
    parser.add_argument("--warmup-updates", type=int, default=180)
    parser.add_argument("--capture-updates", type=int, default=90)
    return parser.parse_args()


def main() -> None:
    args = parse_args()

    output_dir = Path(args.output_dir).expanduser().resolve()
    frames_dir = output_dir / "frames"
    frames_dir.mkdir(parents=True, exist_ok=True)
    for frame in frames_dir.glob("frame_*.png"):
        frame.unlink()

    video_path = output_dir / "showcase.mp4"
    still_path = output_dir / "scene.png"
    state_log_path = output_dir / "state_trace.jsonl"
    if video_path.exists():
        video_path.unlink()
    if still_path.exists():
        still_path.unlink()
    if state_log_path.exists():
        state_log_path.unlink()

    command_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    command_sock.bind((args.host, args.command_port))
    command_sock.setblocking(False)
    state_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    state_target = (args.host, args.state_port)

    from isaacsim import SimulationApp

    simulation_app = SimulationApp(
        {
            "headless": False,
            "renderer": "RayTracedLighting",
            "width": args.width,
            "height": args.height,
        }
    )

    from pxr import Gf, UsdGeom, UsdLux
    from isaacsim.core.prims import SingleXFormPrim
    from isaacsim.core.utils.numpy.rotations import euler_angles_to_quats
    from isaacsim.core.utils.prims import create_prim
    from isaacsim.core.utils.stage import add_reference_to_stage, get_current_stage
    from isaacsim.core.utils.viewports import set_camera_view
    from omni.kit.viewport.utility import capture_viewport_to_file, get_active_viewport

    def colour_prim(path: str, rgb: tuple[float, float, float]) -> None:
        stage = get_current_stage()
        prim = stage.GetPrimAtPath(path)
        UsdGeom.Gprim(prim).CreateDisplayColorAttr([Gf.Vec3f(*rgb)])

    def create_box(path: str, position: tuple[float, float, float], scale: tuple[float, float, float], colour: tuple[float, float, float]) -> None:
        create_prim(path, prim_type="Cube", translation=position, scale=scale)
        colour_prim(path, colour)

    def create_goal(path: str, position: tuple[float, float, float], radius: float, colour: tuple[float, float, float]) -> None:
        create_prim(path, prim_type="Sphere", translation=position)
        stage = get_current_stage()
        prim = stage.GetPrimAtPath(path)
        UsdGeom.Sphere(prim).GetRadiusAttr().Set(radius)
        colour_prim(path, colour)

    stage = get_current_stage()
    add_reference_to_stage(args.usd_path, "/World/TurtleBot")
    create_prim("/World/Floor", prim_type="Cube", translation=(0.3, -0.02, -0.03), scale=(2.8, 2.2, 0.05))
    colour_prim("/World/Floor", (0.57, 0.59, 0.62))
    create_box("/World/ObstacleA", position=(0.45, 0.18, 0.08), scale=(0.13, 0.13, 0.16), colour=(0.80, 0.44, 0.18))
    create_box("/World/ObstacleB", position=(0.72, -0.14, 0.08), scale=(0.13, 0.13, 0.16), colour=(0.92, 0.70, 0.28))
    create_goal("/World/Goal", position=(1.00, 0.00, 0.08), radius=0.08, colour=(0.16, 0.78, 0.42))

    key = UsdLux.DistantLight.Define(stage, "/World/KeyLight")
    key.CreateIntensityAttr(430)
    key.AddRotateXYZOp().Set((52.0, 0.0, 18.0))
    fill = UsdLux.DomeLight.Define(stage, "/World/DomeLight")
    fill.CreateIntensityAttr(150)
    fill.CreateColorAttr((0.88, 0.91, 1.0))

    set_camera_view(eye=[0.78, -0.92, 0.78], target=[0.58, 0.00, 0.05], camera_prim_path="/OmniverseKit_Persp")

    robot = SingleXFormPrim("/World/TurtleBot")
    x = 0.0
    y = 0.0
    yaw = 0.0
    linear_x = 0.0
    angular_z = 0.0
    vx = 0.0
    vy = 0.0
    last_cmd_time = time.monotonic()
    dt = 1.0 / args.tick_hz
    capture_frame_index = 0

    for _ in range(args.warmup_updates):
        simulation_app.update()

    viewport = get_active_viewport()

    try:
        with state_log_path.open("w", encoding="utf-8") as state_log:
            tick = 0
            while tick < args.max_ticks:
                while True:
                    try:
                        payload, _ = command_sock.recvfrom(65536)
                    except BlockingIOError:
                        break
                    command = json.loads(payload.decode("utf-8"))
                    linear_x = float(command.get("linear_x", 0.0))
                    angular_z = float(command.get("angular_z", 0.0))
                    last_cmd_time = time.monotonic()

                if time.monotonic() - last_cmd_time > args.command_timeout_s:
                    linear_x = 0.0
                    angular_z = 0.0

                yaw += angular_z * dt
                vx = linear_x * math.cos(yaw)
                vy = linear_x * math.sin(yaw)
                x += vx * dt
                y += vy * dt

                orientation = euler_angles_to_quats(np.array([0.0, 0.0, yaw], dtype=np.float32))
                robot.set_world_pose(position=np.array([x, y, 0.02], dtype=np.float32), orientation=orientation)

                t_ms = int(round(tick * dt * 1000.0))
                state = {
                    "tick": tick,
                    "t_ms": t_ms,
                    "pose_x": x,
                    "pose_y": y,
                    "yaw": yaw,
                    "vx": vx,
                    "vy": vy,
                    "wz": angular_z,
                }
                state_sock.sendto(json.dumps(state).encode("utf-8"), state_target)
                state_log.write(json.dumps(state) + "\n")

                simulation_app.update()
                simulation_app.update()

                if tick >= args.capture_start_tick and (tick - args.capture_start_tick) % max(args.capture_stride, 1) == 0:
                    if capture_frame_index < args.capture_count:
                        frame_path = frames_dir / f"frame_{capture_frame_index:04d}.png"
                        capture_viewport_to_file(viewport, file_path=str(frame_path))
                        for _ in range(args.capture_updates):
                            simulation_app.update()
                            if frame_path.exists():
                                break
                        if not frame_path.exists():
                            raise RuntimeError(f"Capture failed for {frame_path}")
                        print(f"captured {frame_path}", flush=True)
                        capture_frame_index += 1

                tick += 1

        if capture_frame_index == 0:
            raise RuntimeError("No frames were captured")

        still_index = min(max(args.still_index, 0), capture_frame_index - 1)
        still_source = frames_dir / f"frame_{still_index:04d}.png"
        shutil.copyfile(still_source, still_path)
        print(f"still written to {still_path}", flush=True)

        subprocess.run(
            [
                "ffmpeg",
                "-y",
                "-framerate",
                str(args.fps),
                "-i",
                str(frames_dir / "frame_%04d.png"),
                "-c:v",
                "libx264",
                "-pix_fmt",
                "yuv420p",
                str(video_path),
            ],
            check=True,
        )
        print(f"video written to {video_path}", flush=True)
    finally:
        simulation_app.close()
        command_sock.close()
        state_sock.close()


if __name__ == "__main__":
    main()
