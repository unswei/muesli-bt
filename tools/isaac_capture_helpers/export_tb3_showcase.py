#!/usr/bin/env python3
"""Render a TurtleBot3 showcase clip directly from the Isaac Sim viewport."""

from __future__ import annotations

import argparse
import math
import shutil
import subprocess
from pathlib import Path

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
        default="/home/deploy/isaac_tb3_showcase",
        help="Directory for rendered frames, the MP4 clip, and the still image.",
    )
    parser.add_argument("--width", type=int, default=1280, help="Viewport width in pixels.")
    parser.add_argument("--height", type=int, default=720, help="Viewport height in pixels.")
    parser.add_argument("--fps", type=int, default=15, help="Output frame rate for the MP4 clip.")
    parser.add_argument("--frames", type=int, default=60, help="Number of rendered frames.")
    parser.add_argument(
        "--clip-start-frame",
        type=int,
        default=30,
        help="First rendered frame to include in the encoded clip.",
    )
    parser.add_argument(
        "--clip-frame-count",
        type=int,
        default=30,
        help="Number of rendered frames to include in the encoded clip.",
    )
    parser.add_argument(
        "--still-index",
        type=int,
        default=45,
        help="Frame index to copy to scene.png.",
    )
    parser.add_argument(
        "--warmup-updates",
        type=int,
        default=180,
        help="Number of app updates before capture starts.",
    )
    parser.add_argument(
        "--capture-updates",
        type=int,
        default=120,
        help="Maximum number of app updates to wait for each captured frame.",
    )
    parser.add_argument(
        "--skip-encode",
        action="store_true",
        help="Render frames and the still image without producing the MP4 clip.",
    )
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
    if video_path.exists():
        video_path.unlink()
    if still_path.exists():
        still_path.unlink()

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

    def bezier(p0: np.ndarray, p1: np.ndarray, p2: np.ndarray, p3: np.ndarray, t: float) -> np.ndarray:
        return ((1 - t) ** 3) * p0 + 3 * ((1 - t) ** 2) * t * p1 + 3 * (1 - t) * (t**2) * p2 + (t**3) * p3

    def bezier_tangent(p0: np.ndarray, p1: np.ndarray, p2: np.ndarray, p3: np.ndarray, t: float) -> np.ndarray:
        return 3 * ((1 - t) ** 2) * (p1 - p0) + 6 * (1 - t) * t * (p2 - p1) + 3 * (t**2) * (p3 - p2)

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
    create_box("/World/ObstacleA", position=(0.60, 0.12, 0.10), scale=(0.22, 0.22, 0.20), colour=(0.80, 0.44, 0.18))
    create_box("/World/ObstacleB", position=(1.04, 0.34, 0.08), scale=(0.15, 0.15, 0.16), colour=(0.92, 0.70, 0.28))
    create_goal("/World/Goal", position=(1.22, 0.12, 0.08), radius=0.08, colour=(0.16, 0.78, 0.42))

    key = UsdLux.DistantLight.Define(stage, "/World/KeyLight")
    key.CreateIntensityAttr(430)
    key.AddRotateXYZOp().Set((52.0, 0.0, 18.0))
    fill = UsdLux.DomeLight.Define(stage, "/World/DomeLight")
    fill.CreateIntensityAttr(150)
    fill.CreateColorAttr((0.88, 0.91, 1.0))

    set_camera_view(eye=[1.15, -1.20, 0.45], target=[0.62, -0.02, 0.08], camera_prim_path="/OmniverseKit_Persp")

    robot = SingleXFormPrim("/World/TurtleBot")
    p0 = np.array([-0.50, -0.45, 0.02], dtype=np.float32)
    p1 = np.array([-0.10, -0.58, 0.02], dtype=np.float32)
    p2 = np.array([0.55, -0.22, 0.02], dtype=np.float32)
    p3 = np.array([1.00, 0.12, 0.02], dtype=np.float32)

    for _ in range(args.warmup_updates):
        simulation_app.update()

    viewport = get_active_viewport()
    for frame_index in range(args.frames):
        t = frame_index / max(args.frames - 1, 1)
        position = bezier(p0, p1, p2, p3, t)
        tangent = bezier_tangent(p0, p1, p2, p3, t)
        yaw = math.atan2(float(tangent[1]), float(tangent[0]))
        orientation = euler_angles_to_quats(np.array([0.0, 0.0, yaw], dtype=np.float32))
        robot.set_world_pose(position=position, orientation=orientation)
        simulation_app.update()
        simulation_app.update()
        frame_path = frames_dir / f"frame_{frame_index:04d}.png"
        capture_viewport_to_file(viewport, file_path=str(frame_path))
        for _ in range(args.capture_updates):
            simulation_app.update()
            if frame_path.exists():
                break
        if not frame_path.exists():
            raise RuntimeError(f"Capture failed for {frame_path}")
        print(f"rendered {frame_path}")

    simulation_app.close()

    still_index = min(max(args.still_index, 0), args.frames - 1)
    shutil.copyfile(frames_dir / f"frame_{still_index:04d}.png", still_path)

    if args.skip_encode:
        return

    clip_start = min(max(args.clip_start_frame, 0), args.frames - 1)
    clip_count = min(args.clip_frame_count, args.frames - clip_start)
    subprocess.run(
        [
            "ffmpeg",
            "-y",
            "-framerate",
            str(args.fps),
            "-start_number",
            str(clip_start),
            "-i",
            str(frames_dir / "frame_%04d.png"),
            "-frames:v",
            str(clip_count),
            "-c:v",
            "libx264",
            "-pix_fmt",
            "yuv420p",
            str(video_path),
        ],
        check=True,
    )


if __name__ == "__main__":
    main()
