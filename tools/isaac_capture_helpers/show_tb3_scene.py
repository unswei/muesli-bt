#!/usr/bin/env python3
"""Stage a simple TurtleBot3 Isaac scene for manual capture.

This is an auxiliary operator script for remote Isaac hosts. It is useful for
manual media capture and display-path debugging, not for the main ROS 2 demo.
"""

import math
import time

import numpy as np
from isaacsim import SimulationApp

simulation_app = SimulationApp(
    {"headless": False, "renderer": "RayTracedLighting", "width": 1280, "height": 720}
)

from pxr import Gf, UsdGeom
from isaacsim.core.prims import SingleXFormPrim
from isaacsim.core.utils.numpy.rotations import euler_angles_to_quats
from isaacsim.core.utils.prims import create_prim
from isaacsim.core.utils.stage import add_reference_to_stage, get_current_stage
from isaacsim.core.utils.viewports import set_camera_view


def bezier(p0, p1, p2, p3, t):
    return ((1 - t) ** 3) * p0 + 3 * ((1 - t) ** 2) * t * p1 + 3 * (1 - t) * (t**2) * p2 + (t**3) * p3


def bezier_tangent(p0, p1, p2, p3, t):
    return 3 * ((1 - t) ** 2) * (p1 - p0) + 6 * (1 - t) * t * (p2 - p1) + 3 * (t**2) * (p3 - p2)


def colour_prim(path, rgb):
    stage = get_current_stage()
    prim = stage.GetPrimAtPath(path)
    UsdGeom.Gprim(prim).CreateDisplayColorAttr([Gf.Vec3f(*rgb)])


def create_box(path, position, scale, colour):
    create_prim(path, prim_type="Cube", translation=position, scale=scale)
    colour_prim(path, colour)


def create_goal(path, position, radius, colour):
    create_prim(path, prim_type="Sphere", translation=position)
    stage = get_current_stage()
    prim = stage.GetPrimAtPath(path)
    UsdGeom.Sphere(prim).GetRadiusAttr().Set(radius)
    colour_prim(path, colour)


def main():
    add_reference_to_stage(
        "/home/deploy/turtlebot3_imported/turtlebot3_burger/turtlebot3_burger.usda",
        "/World/TurtleBot",
    )

    create_prim("/World/Floor", prim_type="Cube", translation=(0.0, 0.0, -0.06), scale=(8.0, 8.0, 0.1))
    colour_prim("/World/Floor", (0.87, 0.84, 0.79))
    create_box("/World/ObstacleA", position=(0.0, -0.1, 0.18), scale=(0.55, 0.55, 0.36), colour=(0.82, 0.49, 0.24))
    create_box("/World/ObstacleB", position=(0.95, 0.55, 0.14), scale=(0.35, 0.35, 0.28), colour=(0.91, 0.68, 0.31))
    create_box("/World/ObstacleC", position=(-0.9, 0.65, 0.22), scale=(0.42, 0.42, 0.44), colour=(0.76, 0.35, 0.26))
    create_goal("/World/Goal", position=(1.55, 0.95, 0.12), radius=0.12, colour=(0.2, 0.78, 0.38))

    set_camera_view(eye=[3.0, -2.4, 1.8], target=[0.15, 0.08, 0.2], camera_prim_path="/OmniverseKit_Persp")

    robot = SingleXFormPrim("/World/TurtleBot")
    p0 = np.array([-1.7, -0.85, 0.02], dtype=np.float32)
    p1 = np.array([-0.9, -1.05, 0.02], dtype=np.float32)
    p2 = np.array([0.35, 0.25, 0.02], dtype=np.float32)
    p3 = np.array([1.45, 0.9, 0.02], dtype=np.float32)

    for _ in range(180):
        simulation_app.update()
        time.sleep(1.0 / 30.0)

    for i in range(240):
        t = i / 239.0
        position = bezier(p0, p1, p2, p3, t)
        tangent = bezier_tangent(p0, p1, p2, p3, t)
        yaw = math.atan2(float(tangent[1]), float(tangent[0]))
        orientation = euler_angles_to_quats(np.array([0.0, 0.0, yaw], dtype=np.float32))
        robot.set_world_pose(position=position, orientation=orientation)
        simulation_app.update()
        time.sleep(1.0 / 24.0)

    for _ in range(48):
        simulation_app.update()
        time.sleep(1.0 / 24.0)

    simulation_app.close()


if __name__ == "__main__":
    main()
