# embedding a minimal robot loop

## what this is

This tutorial shows the smallest C++ embedding path for a robot or simulator loop.

Status: released for the callback registry, BT tick API, and canonical event log API shown here.

## when to use it

Use this path when you want to connect your own robot code without starting from ROS2, Webots, or PyBullet.

## how it works

The host owns the loop:

1. observe robot state;
2. update stable inputs or callback state;
3. tick the BT instance;
4. read or apply the selected action;
5. step the robot or simulator;
6. inspect canonical event lines when needed.

The BT owns task selection. The host owns sensors, actuators, timing, validation, and safe fallback.

## api / syntax

Compile the example from a configured build tree:

```bash
cmake --preset dev
cmake --build --preset dev -j
c++ -std=c++20 \
  -I include \
  examples/embedding/minimal_robot_loop.cpp \
  build/dev/libmuesli_bt_core.a \
  -o /tmp/muesli-minimal-robot-loop
/tmp/muesli-minimal-robot-loop
```

The example emits canonical event log lines to stdout and exits with status `0` when both branches run.

## example

BT source:

```lisp
--8<-- "examples/embedding/minimal_robot_bt.lisp"
```

C++ host loop:

```cpp
--8<-- "examples/embedding/minimal_robot_loop.cpp"
```

## gotchas

- Register robot conditions and actions explicitly.
- Keep action validation and physical safety in the host.
- Use the blackboard for stable task-level data, not raw high-rate sensor payloads.
- Keep the first loop small before adding planners, model calls, or transport integrations.

## see also

- [writing a backend](../integration/writing-a-backend.md)
- [environment API](../integration/env-api.md)
- [minimal real BT](../examples/minimal-real-bt.md)
- [canonical event log](../observability/event-log.md)
