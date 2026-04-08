# Webots: From Educational Goal BT To Reusable Wrapper

## what this is

This guide explains how the educational Webots e-puck goal demo evolves into a reusable wrapper version.

The aim is not to compare files line by line.
The aim is to explain why each change exists, what it buys you, and what readability costs it introduces.

## when to use it

Use this guide when you want to:

- learn the educational goal demo first
- understand why the wrapper version is more abstract
- follow a practical path from a clear teaching example to a reusable multi-backend example

## how it works

The transition happens in four steps.

### step 1: start with explicit robot modes

In the educational entrypoint, the visible robot behaviour is named directly in the BT:

- `reverse_left`
- `reverse_right`
- `arc_left`
- `arc_right`
- `plan_goal`
- `direct_goal`

That makes the robot behaviour easy to understand from one file.

See:

- `examples/webots_epuck_goal/lisp/educational_goal_bt.lisp`

### step 2: separate policy from transport

The wrapper version starts to separate backend-specific wheel commands from the higher-level command intent.

Instead of treating left and right wheel speeds as the public action shape, the wrapper version introduces a shared command:

```text
[linear_x, angular_z]
```

That shift matters because the same command shape can be projected into:

- Webots wheel speeds
- ROS 2 `geometry_msgs/msg/Twist`
- PyBullet racecar steering/throttle equivalents

This improves reuse, but it means some behaviour is no longer visible as literal wheel actions in the BT file.

See:

- `examples/flagship_wheeled/lisp/contract_helpers.lisp`
- `examples/webots_epuck_goal/lisp/flagship_entry.lisp`

### step 3: compress explicit obstacle reactions into a shared safety surface

The educational file spells out several obstacle responses.
The wrapper version compresses that visible structure into a thinner shared interface:

- `collision_imminent`
- `act_avoid`

That improves reuse across backends.
It also makes the BT smaller and easier to share.

The trade-off is that the exact steering response is no longer obvious from the BT alone.
It lives in the wrapper layer for each backend.

See:

- `examples/flagship_wheeled/lisp/bt_goal_flagship.lisp`
- `examples/webots_epuck_goal/lisp/flagship_entry.lisp`
- `examples/repl_scripts/ros2-flagship-goal.lisp`

### step 4: keep planning in the BT, move observation shaping to the wrapper

Both versions keep planning inside the BT through `plan-action`.

The main change is where the state comes from.

In the educational version, the same file:

- computes observation summaries
- defines visible robot modes
- calls the planner
- writes the final action

In the wrapper version, the wrapper becomes responsible for:

- deriving a backend-neutral planner state
- providing shared blackboard keys
- converting the chosen shared command back into backend-native actuators

That structure is better once the same behaviour must run across multiple hosts.
It is worse if the only goal is teaching the behaviour to a new reader.

## api / syntax

### educational entrypoint

Run the educational version with:

```bash
MUESLI_BT_WEBOTS_LISP_ENTRY=lisp/educational_goal_bt.lisp \
  "$WEBOTS_HOME/webots" --batch --mode=fast --stdout --stderr \
  examples/webots_epuck_goal/worlds/epuck_goal_cluttered.wbt
```

### wrapper version

Run the wrapper version with:

```bash
MUESLI_BT_WEBOTS_LISP_ENTRY=lisp/flagship_entry.lisp \
  "$WEBOTS_HOME/webots" --batch --mode=fast --stdout --stderr \
  examples/webots_epuck_goal/worlds/epuck_goal_cluttered.wbt
```

### key files

- `examples/webots_epuck_goal/lisp/educational_goal_bt.lisp`
- `examples/webots_epuck_goal/lisp/flagship_entry.lisp`
- `examples/flagship_wheeled/lisp/bt_goal_flagship.lisp`
- `examples/flagship_wheeled/lisp/contract_helpers.lisp`
- `examples/repl_scripts/ros2-flagship-goal.lisp`

## example

If you are teaching the system:

1. start with `educational_goal_bt.lisp`
2. explain each visible branch in the tree
3. show that planning is already integrated with `plan-action`
4. then switch to `flagship_entry.lisp`
5. explain how command shape, obstacle signalling, and observation shaping are being generalised for reuse

That progression keeps the first contact simple without pretending the wrapper version is equally transparent.

## gotchas

- The educational version is intentionally clearer, not more canonical.
- The wrapper version is intentionally more reusable, not more self-explanatory.
- Do not read the wrapper BT alone and assume it contains the whole steering story. Some of that story has moved into the wrapper by design.

## see also

- [Webots: e-puck Goal Seeking](webots-epuck-goal-seeking.md)
- [Isaac Sim: TurtleBot3 ROS2 Demo](isaac-wheeled-ros2-showcase.md)
