# Webots: e-puck Goal Seeking

## what this is

This example runs a wheeled goal-seeking behaviour in a cluttered Webots arena.

It now includes two useful entrypoints:

- `lisp/educational_goal_bt.lisp`: a single-file educational version where the visible robot modes live in one Lisp file
- `lisp/flagship_entry.lisp`: a reusable wrapper version of the same goal-seeking behaviour

## when to use it

Use this example when you want to:

- teach the robot behaviour from one Lisp file first
- inspect the current goal-seeking Webots path
- compare a wheeled planner-assisted behaviour against the PyBullet racecar path
- work with a behaviour that can be expressed from pose, heading, goal, and bounded obstacle signals

## how it works

The demo uses the checked-in Webots e-puck controller and the canonical `env.*` loop.

At each tick:

1. Webots publishes the current observation map.
2. Lisp code derives branch inputs and candidate actions.
3. The BT chooses the current branch.
4. The selected wheel action is written back through the Webots backend.

The educational entrypoint makes the robot modes explicit:

- stop at the goal
- reverse left if the front is blocked and the stronger obstacle is on the right
- reverse right if the front is blocked and the stronger obstacle is on the left
- arc away from a blocked side
- call the planner when the route is open enough to make progress
- fall back to a direct goal-following action

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
- `examples/webots_epuck_goal/lisp/educational_goal_bt.lisp`
- `examples/webots_epuck_goal/lisp/flagship_entry.lisp`
- `examples/webots_epuck_goal/lisp/bt_goal_seek.lisp`
- `examples/webots_epuck_goal/worlds/epuck_goal_cluttered.wbt`

## example

Educational BT source:

```lisp
--8<-- "examples/webots_epuck_goal/lisp/educational_goal_bt.lisp"
```

Expected outputs:

- log file: `examples/webots_epuck_goal/logs/goal.jsonl`
- educational log file: `examples/webots_epuck_goal/logs/goal_educational.jsonl`
- flagship log file: `examples/webots_epuck_goal/logs/flagship_goal.jsonl`
- DOT export: `examples/webots_epuck_goal/out/tree.dot`
- educational DOT export: `examples/webots_epuck_goal/out/educational_tree.dot`

To run the educational version:

```bash
MUESLI_BT_WEBOTS_LISP_ENTRY=lisp/educational_goal_bt.lisp \
  "$WEBOTS_HOME/webots" --batch --mode=fast --stdout --stderr \
  examples/webots_epuck_goal/worlds/epuck_goal_cluttered.wbt
```

To run the reusable wrapper version instead of the legacy `main.lisp` entrypoint:

```bash
MUESLI_BT_WEBOTS_LISP_ENTRY=lisp/flagship_entry.lisp \
  "$WEBOTS_HOME/webots" --batch --mode=fast --stdout --stderr \
  examples/webots_epuck_goal/worlds/epuck_goal_cluttered.wbt
```

The wrapper version keeps running after first success and writes zero shared action after goal arrival until its configured `max_ticks`. That makes the log suitable for post-success hold checks.

## moving from the educational version to the wrapper version

The educational entrypoint is intentionally direct. The visible robot behaviour is encoded in one file and the branch names map closely to what the robot does on screen.

The wrapper version changes the structure in stages:

1. It extracts backend-neutral helpers such as goal checks and direct-goal commands.
2. It reduces the visible avoidance branch to a shared `collision_imminent -> act_avoid` interface.
3. It moves from wheel-speed output to a shared command surface.
4. It keeps planning inside the BT, but makes the wrapper responsible for backend-specific observation shaping and actuator conversion.

That makes the wrapper version easier to reuse across backends, but less explicit as a teaching example.

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

- The educational entrypoint is clearer to read, but it is intentionally less reusable than the wrapper version.
- This demo still uses Webots-specific observation shaping from e-puck sensors.
- The current action output is wheel-speed based, not the ROS2 `Twist` shape.
- If you want a backend-specific sensor demo, use the line-following or obstacle pages instead.

## see also

- [From educational goal BT to reusable wrapper](webots-epuck-goal-transition.md)
- [PyBullet: racecar](pybullet-racecar.md)
- [ROS2 tutorial](../integration/ros2-tutorial.md)
- [examples directory overview](https://github.com/unswei/muesli-bt/blob/main/examples/README.md)
