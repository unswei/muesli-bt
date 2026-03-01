# Webots: e-puck Obstacle Avoidance + Wall Following

## What It Demonstrates

- BT branch switching between roam, wall-follow, and collision-avoid modes
- backend observation mapping from e-puck proximity sensors
- bounded tick budget logging in a simulator loop

## How To Run

Detailed setup/build steps:

- [example README (Webots e-puck obstacle)](https://github.com/unswei/muesli-bt/blob/main/examples/webots_epuck_obstacle/README.md)

Direct run (after building the Webots controller target):

```bash
"$WEBOTS_HOME/webots" --batch --mode=fast --stdout --stderr \
  examples/webots_epuck_obstacle/worlds/epuck_obstacle_arena.wbt
```

## Logs And Plots

- log file: `examples/webots_epuck_obstacle/logs/obstacle.jsonl`
- plot timeline:

```bash
python3 examples/_tools/plot_bt_timeline.py \
  examples/webots_epuck_obstacle/logs/obstacle.jsonl \
  --out examples/webots_epuck_obstacle/out/bt_timeline.png
```

## Key BT Files

- `examples/webots_epuck_obstacle/lisp/main.lisp`
- `examples/webots_epuck_obstacle/lisp/bt_obstacle_wallfollow.lisp`

## Render BT DOT

`main.lisp` exports `out/tree.dot` at startup.

```bash
dot -Tsvg examples/webots_epuck_obstacle/out/tree.dot \
  -o examples/webots_epuck_obstacle/out/tree.svg
```
