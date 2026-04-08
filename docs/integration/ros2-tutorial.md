# ros2 tutorial

## what this is

This page is the supported end-to-end ROS2 path for `muesli-bt`.

It shows one complete workflow on the released baseline:

1. build the ROS2-enabled runner
2. feed odometry into the flagship wrapper
3. run the BT through `muslisp_ros2`
4. validate the canonical event log
5. inspect the normalised run output

The aim is to give a new user one path that works from setup through verification.

## when to use it

Use this page when you want to:

- bring up the first supported ROS2 integration on Ubuntu 22.04 + Humble
- run the shared wheeled flagship through the released `Odometry` -> `Twist` transport
- verify that the run produced a valid canonical `mbt.evt.v1` log
- inspect the secondary flagship run log with the shared normalisation tool

If you need the backend design boundary and non-goals, use [ros2 backend scope](ros2-backend-scope.md) after this page.

## how it works

The current ROS2 adaptor stays intentionally thin.

`muesli-bt` still owns:

- BT semantics
- planner semantics
- async semantics
- canonical event logging

ROS2 owns:

- message transport
- topic wiring
- host-side timing and integration

The supported path in this tutorial uses:

- input topic: `/robot/odom`
- input type: `nav_msgs/msg/Odometry`
- output topic: `/robot/cmd_vel`
- output type: `geometry_msgs/msg/Twist`
- runner: `muslisp_ros2`
- Lisp entrypoint: `examples/repl_scripts/ros2-flagship-goal.lisp`

For a deterministic bring-up, this tutorial uses the checked-in odometry publisher:

- `examples/repl_scripts/ros2_flagship_test_publisher.py`

That publisher gives the ROS2 backend a simple goal-directed trajectory so the wrapper can run without requiring a simulator first.

## api / syntax

### supported baseline

The current supported baseline is:

- OS: Ubuntu 22.04
- ROS distro: ROS 2 Humble
- attach path: `(env.attach "ros2")`
- transport: `nav_msgs/msg/Odometry` in, `geometry_msgs/msg/Twist` out
- reset policy: `reset_mode="unsupported"`
- canonical event log artefact: `build/linux-ros2/ros2-flagship-goal/events.jsonl`

### build the ROS2 runner

```bash
source /opt/ros/humble/setup.bash
cmake -S . -B build/linux-ros2 -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DMUESLI_BT_BUILD_INTEGRATION_ROS2=ON \
  -DMUESLI_BT_BUILD_INTEGRATION_PYBULLET=OFF \
  -DMUESLI_BT_BUILD_INTEGRATION_WEBOTS=OFF \
  -DMUESLI_BT_BUILD_PYTHON_BRIDGE=OFF \
  -DMUESLI_BT_BUILD_WEBOTS_EXAMPLES=OFF
cmake --build build/linux-ros2 --parallel --target muslisp_ros2
```

### terminal 1: publish test odometry

```bash
source /opt/ros/humble/setup.bash
python3 examples/repl_scripts/ros2_flagship_test_publisher.py \
  --topic /robot/odom \
  --tick-hz 20 \
  --duration-sec 6 \
  --goal-x 1.0 \
  --goal-y 0.0
```

This publisher sends a simple trajectory from the origin to the configured goal.

### terminal 2: observe commanded motion

```bash
source /opt/ros/humble/setup.bash
ros2 topic echo /robot/cmd_vel
```

This is optional, but useful while bringing the path up.

### terminal 3: run the flagship wrapper

```bash
source /opt/ros/humble/setup.bash
./build/linux-ros2/muslisp_ros2 examples/repl_scripts/ros2-flagship-goal.lisp
```

The wrapper:

- attaches the `ros2` backend
- configures `/robot/odom` and `/robot/cmd_vel`
- derives shared wheeled flagship keys from odometry and fixed scenario geometry
- runs the shared BT from `examples/flagship_wheeled/lisp/bt_goal_flagship.lisp`
- writes both the secondary run-loop log and the canonical event log

### validate the canonical event log

After the run completes, validate the canonical log:

```bash
python3 tools/validate_log.py build/linux-ros2/ros2-flagship-goal/events.jsonl
```

Expected result:

```text
event log validation passed
```

### inspect the flagship run log

The flagship wrapper also writes a secondary run log:

```text
build/linux-ros2/ros2-flagship-goal.jsonl
```

Normalise that run into the shared comparison schema:

```bash
python3 examples/flagship_wheeled/tools/normalise_run.py \
  --backend ros2 \
  --output build/linux-ros2/ros2-flagship-goal.normalised.json \
  build/linux-ros2/ros2-flagship-goal.jsonl
```

This lets you inspect:

- branch choices
- shared action output
- final outcome
- goal-distance progression

## example

### expected artefacts

After a successful run, these files should exist:

- `build/linux-ros2/ros2-flagship-goal.jsonl`
- `build/linux-ros2/ros2-flagship-goal/events.jsonl`
- `build/linux-ros2/ros2-flagship-goal.normalised.json` after normalisation

### expected outcome

The run should:

- publish `Twist` commands on `/robot/cmd_vel`
- stop once the wrapper decides the goal has been reached
- produce a valid canonical `mbt.evt.v1` event log
- produce a normalised flagship summary with `goal_reached = true`

### source

The full flagship ROS2 wrapper source is shown on:

- [ros2 flagship source](ros2-flagship-source.md)

## gotchas

- Use `muslisp_ros2`, not plain `muslisp`. The ROS2 extension is registered by the ROS2-enabled runner.
- Start the odometry publisher before the runner. Otherwise the first tick can fall back before transport data arrives.
- The supported live path is still `reset_mode="unsupported"`. Use a harness for multi-episode reset tests.
- Keep the topic namespace on `/robot` unless you also change the wrapper configuration.
- Validate the canonical event log from `events.jsonl`. The secondary run log is useful, but it is not the primary contract artefact.

## see also

- [ros2 flagship source](ros2-flagship-source.md)
- [ros2 backend scope](ros2-backend-scope.md)
- [cross-transport flagship for v0.5](cross-transport-flagship.md)
- [conformance levels](../contracts/conformance.md)
