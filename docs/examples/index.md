# Examples Overview

This section points to runnable examples with a Lisp-first workflow.

Use this page when you want to choose the next example deliberately instead of opening the whole catalogue at once.
The examples range from tiny REPL scripts to simulator-backed demos, and they are not all aimed at the same stage of learning.

## Start Here

Recommended first path:

1. [muslisp basics](lisp-basics.md) - beginner, pure Lisp, no BT semantics yet
2. [hello BT](hello-bt.md) - beginner, first tree, first repeated ticks
3. [minimal real BT](minimal-real-bt.md) - beginner, guarded task logic and fallback command
4. [Memoryful Sequence Demo](memoryful-sequence-demo.md) - semantics, shows why memory matters across ticks
5. [e-puck-style goal seeking](pybullet-epuck-goal.md) - simulator, first backend-backed success path
6. [Reactive Guard Demo](reactive-guard-demo.md) - intermediate, reactive guards and cancellation

If you already know BTs but not the runtime shape, read [How Execution Works](../getting-oriented/how-execution-works.md) before moving to the simulator examples.

## How To Read These Examples

The small scripted examples are best for learning one idea at a time.
The simulator and integration examples are best once you already recognise the BT and blackboard patterns from the smaller pages.

Most example pages embed source directly from the repository, so the docs stay aligned with runnable files.
That makes the examples reliable as reference material, but it also means you should use the page wrapper to understand what to notice before reading the code.

## Best First Pure Lisp Example

- [muslisp basics](lisp-basics.md) - beginner, introduces `define`, quoting, and list-shaped code before BT syntax appears
- [hello BT](hello-bt.md) - beginner, first BT example after the Lisp basics page
- [minimal real BT](minimal-real-bt.md) - beginner, first guarded fallback shape with a canonical log file

## Best First Simulator Example

- [e-puck-style goal seeking](pybullet-epuck-goal.md) - simulator, quickest path to a backend-backed run

Read this after [hello BT](hello-bt.md) if you want to see the same overall loop with observations, actions, and logs.

Most examples follow the same pattern:

1. edit BT language + Lisp files
2. run through a backend (simulator or robot)
3. inspect JSONL logs and plots
4. render BT DOT for structure debugging
5. inspect source directly from docs pages (inline for short examples, linked full-source pages for longer demos)

## Webots

- [e-puck goal seeking](webots-epuck-goal-seeking.md) - simulator
- [from educational goal BT to reusable wrapper](webots-epuck-goal-transition.md) - intermediate simulator
- [e-puck obstacle avoidance and wall following](webots-epuck-obstacle-wall-following.md) - intermediate simulator
- [e-puck line following](webots-epuck-line-following.md) - intermediate simulator

## PyBullet

- [e-puck-style goal seeking](pybullet-epuck-goal.md) - beginner simulator
- [racecar](pybullet-racecar.md) - advanced simulator

## Cross-transport

- [flagship comparison](cross-transport-flagship-comparison.md) - advanced comparison

## Advanced Integrations

- [H1 locomotion demo](isaac-h1-ros2-demo.md) - advanced integration
- [TurtleBot3 ROS2 demo](isaac-wheeled-ros2-showcase.md) - advanced integration
- [ROS2 tutorial](../integration/ros2-tutorial.md) - advanced integration

## More Scripted Examples

For pure Lisp algorithms and smaller BT samples, see:

- [examples directory overview](https://github.com/unswei/muesli-bt/blob/main/examples/README.md)
- [Memoryful Sequence Demo](memoryful-sequence-demo.md) - semantics
- [Reactive Guard Demo](reactive-guard-demo.md) - intermediate semantics
- [minimal real BT](minimal-real-bt.md) - task-shaped BT without a simulator
- [A* Search](a-star-search.md) - planning algorithm
- [Dijkstra + PQ](dijkstra-pq.md) - planning algorithm
- [PRM + PQ](prm-pq.md) - planning algorithm

Each scripted example page includes executable source directly from `examples/repl_scripts/*.lisp`
using MkDocs snippets, so docs stay synced with runnable files.
