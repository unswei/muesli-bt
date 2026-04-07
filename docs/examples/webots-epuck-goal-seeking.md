# Webots: e-puck Goal Seeking

## what this is

This example runs a wheeled goal-seeking behaviour in a cluttered Webots arena.

The BT stays small:

- avoid when collision risk is high
- use bounded planning when a goal is available
- fall back to roam when no goal is active

This is the strongest current starting point for the `v0.5.0` cross-transport flagship because the high-level behaviour maps more cleanly to the released ROS2 `Odometry` -> `Twist` surface than the line-following or wall-following demos do.

## when to use it

Use this example when you want to:

- inspect the current goal-seeking Webots path
- compare a wheeled planner-assisted behaviour against the PyBullet racecar path
- anchor the future “same BT, different IO transport” work on a behaviour that can be expressed from pose, heading, goal, and bounded obstacle signals

## how it works

The demo uses the checked-in Webots e-puck controller and the canonical `env.*` loop.

At each tick:

1. Webots publishes the current observation map.
2. Lisp glue derives planner and branch inputs such as `collision_imminent`, `goal_available`, `planner_state`, and fallback actions.
3. The BT chooses between avoid, plan-to-goal, and roam branches.
4. The selected wheel action is written back through the Webots backend.

The current observation includes a lightweight obstacle signal derived from e-puck proximity sensors together with goal distance and goal bearing.

## api / syntax

Build the controller target:

```bash
cmake --preset dev -DMUESLI_BT_BUILD_WEBOTS_EXAMPLES=ON
cmake --build --preset dev --parallel --target muesli_webots_epuck_goal
```

Run the world:

```bash
"$WEBOTS_HOME/webots" --batch --mode=fast --stdout --stderr \
  examples/webots_epuck_goal/worlds/epuck_goal_cluttered.wbt
```

Key files:

- `examples/webots_epuck_goal/lisp/main.lisp`
- `examples/webots_epuck_goal/lisp/flagship_entry.lisp`
- `examples/webots_epuck_goal/lisp/bt_goal_seek.lisp`
- `examples/webots_epuck_goal/worlds/epuck_goal_cluttered.wbt`

## example

BT source:

```lisp
--8<-- "examples/webots_epuck_goal/lisp/bt_goal_seek.lisp"
```

Expected outputs:

- log file: `examples/webots_epuck_goal/logs/goal.jsonl`
- flagship log file: `examples/webots_epuck_goal/logs/flagship_goal.jsonl`
- DOT export: `examples/webots_epuck_goal/out/tree.dot`

To run the shared `v0.5` flagship wrapper instead of the legacy `main.lisp` entrypoint:

```bash
MUESLI_BT_WEBOTS_LISP_ENTRY=lisp/flagship_entry.lisp \
  "$WEBOTS_HOME/webots" --batch --mode=fast --stdout --stderr \
  examples/webots_epuck_goal/worlds/epuck_goal_cluttered.wbt
```

Useful plots:

```bash
python3 examples/_tools/plot_bt_timeline.py \
  examples/webots_epuck_goal/logs/goal.jsonl \
  --out examples/webots_epuck_goal/out/bt_timeline.png
```

```bash
python3 examples/_tools/plot_planner_root.py \
  examples/webots_epuck_goal/logs/goal.jsonl \
  --every 30 --k 5 \
  --out_dir examples/webots_epuck_goal/out
```

## gotchas

- This demo still uses Webots-specific observation shaping from e-puck sensors. `v0.5.0` should preserve the high-level BT shape while moving backend-specific observation mapping behind each backend adapter.
- The current action output is wheel-speed based, not the ROS2 `Twist` shape. That is one reason this example is a starting point for the shared flagship, not the finished cross-transport artefact itself.
- If you want a backend-specific sensor demo, use the line-following or obstacle pages instead. They are less suitable as the shared `v0.5.0` flagship.

## see also

- [PyBullet: racecar](pybullet-racecar.md)
- [ROS2 tutorial](../integration/ros2-tutorial.md)
- [roadmap to 1.0](../roadmap-to-1.0.md)
- [examples directory overview](https://github.com/unswei/muesli-bt/blob/main/examples/README.md)
