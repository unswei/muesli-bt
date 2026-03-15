# Examples Overview

This section points to runnable examples with a Lisp-first workflow.

Most examples follow the same pattern:

1. edit BT language + Lisp files
2. run through a backend (simulator or robot)
3. inspect JSONL logs and plots
4. render BT DOT for structure debugging
5. inspect source directly from docs pages (inline for short examples, linked full-source pages for longer demos)

## Webots

- [e-puck obstacle avoidance and wall following](webots-epuck-obstacle-wall-following.md)
- [e-puck line following](webots-epuck-line-following.md)

## PyBullet

- [racecar](pybullet-racecar.md)

## Isaac Sim / ROS2

- [H1 locomotion demo](isaac-h1-ros2-demo.md)

## More Scripted Examples

For pure Lisp algorithms and smaller BT samples, see:

- [examples directory overview](https://github.com/unswei/muesli-bt/blob/main/examples/README.md)
- [Memoryful Sequence Demo](memoryful-sequence-demo.md)
- [Reactive Guard Demo](reactive-guard-demo.md)
- [A* Search](a-star-search.md)
- [Dijkstra + PQ](dijkstra-pq.md)
- [PRM + PQ](prm-pq.md)

Each scripted example page includes executable source directly from `examples/repl_scripts/*.lisp`
using MkDocs snippets, so docs stay synced with runnable files.
