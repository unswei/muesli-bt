# Webots e-puck Goal Seeking (Cluttered Arena)

This demo uses the canonical `env.*` interface and a Lisp BT with two main modes:

- safety-first obstacle avoidance
- planner-guided goal seeking

The observation includes a lightweight ray-style signal (`lidar_lite`) derived from e-puck proximity sensors.

## Folder layout

- `worlds/epuck_goal_cluttered.wbt`: cluttered arena with a visible goal marker.
- `controllers/muesli_epuck/muesli_epuck.cpp`: tiny wrapper to the shared Webots controller.
- `lisp/main.mueslisp`: loop entrypoint and behaviour logic.
- `lisp/bt_goal_seek.mueslisp`: BT structure.
- `logs/`: JSONL output.
- `out/`: plots and DOT renders.

## Build controller binary

From repo root:

```bash
cmake --preset dev -DMUESLI_BT_BUILD_WEBOTS_EXAMPLES=ON
cmake --build --preset dev -j --target muesli_webots_epuck_goal
```

## Open and run in Webots

1. Open `examples/webots_epuck_goal/worlds/epuck_goal_cluttered.wbt` in Webots.
2. Ensure controller is `muesli_epuck`.
3. Press play.

### Headless / batch run (if supported)

```bash
"$WEBOTS_HOME/webots" --batch --mode=fast --stdout --stderr \
  examples/webots_epuck_goal/worlds/epuck_goal_cluttered.wbt
```

## Logs

Default log file:

- `examples/webots_epuck_goal/logs/goal.jsonl`

Schema notes:

- `schema_version: "epuck_demo.v1"`
- per tick includes `tick`, `t_ms`, `bt.active_path`, `obs.goal_dist`, `action.u`, `budget.*`

## Visualisation

BT timeline:

```bash
python3 examples/_tools/plot_bt_timeline.py \
  examples/webots_epuck_goal/logs/goal.jsonl \
  --out examples/webots_epuck_goal/out/bt_timeline.png
```

Planner/path/action plots:

```bash
python3 examples/_tools/plot_planner_root.py \
  examples/webots_epuck_goal/logs/goal.jsonl \
  --every 30 --k 5 \
  --out_dir examples/webots_epuck_goal/out
```

Key outputs for this demo:

- `path_xy.png`
- `action_scatter.png`
- `progressive_widening.png`
- `goal_distance.png`
- `budget_adherence.png`

## BT DOT export

`lisp/main.mueslisp` exports at startup:

- `examples/webots_epuck_goal/out/tree.dot`

Render with Graphviz:

```bash
dot -Tpng examples/webots_epuck_goal/out/tree.dot \
  -o examples/webots_epuck_goal/out/tree.png
```
