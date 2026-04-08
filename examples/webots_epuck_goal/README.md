# Webots e-puck Goal Seeking (Cluttered Arena)

This demo uses the canonical `env.*` interface and a Lisp BT with two main modes:

- safety-first obstacle avoidance
- planner-guided goal seeking

It also includes two alternative runnable entrypoints for the same world:

- `lisp/educational_goal_bt.lisp`: a single-file educational BT version with explicit visible robot modes
- `lisp/flagship_entry.lisp`: a reusable wrapper version of the same goal-seeking behaviour

The observation includes a lightweight ray-style signal (`lidar_lite`) derived from e-puck proximity sensors.

## Folder layout

- `worlds/epuck_goal_cluttered.wbt`: cluttered arena with a visible goal marker.
- `controllers/muesli_epuck/muesli_epuck.cpp`: tiny wrapper to the shared Webots controller.
- `lisp/main.lisp`: loop entrypoint and behaviour logic.
- `lisp/educational_goal_bt.lisp`: self-contained educational BT version with explicit reverse, clear, planner, and direct-goal branches.
- `lisp/flagship_entry.lisp`: reusable wrapper entrypoint.
- `lisp/bt_goal_seek.lisp`: BT structure.
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

### Select a different Lisp entrypoint

The shared Webots controller now defaults to `lisp/main.lisp`, but you can override that with `MUESLI_BT_WEBOTS_LISP_ENTRY`.

Run the educational single-file BT version:

```bash
MUESLI_BT_WEBOTS_LISP_ENTRY=lisp/educational_goal_bt.lisp \
  "$WEBOTS_HOME/webots" --batch --mode=fast --stdout --stderr \
  examples/webots_epuck_goal/worlds/epuck_goal_cluttered.wbt
```

Run the reusable wrapper version:

```bash
MUESLI_BT_WEBOTS_LISP_ENTRY=lisp/flagship_entry.lisp \
  "$WEBOTS_HOME/webots" --batch --mode=fast --stdout --stderr \
  examples/webots_epuck_goal/worlds/epuck_goal_cluttered.wbt
```

The wrapper version does not stop at first success. It keeps ticking with a zero command until its configured `max_ticks`, so the log captures both goal arrival and post-success hold behaviour.

## Logs

Default log file:

- `examples/webots_epuck_goal/logs/goal.jsonl`
- educational log file: `examples/webots_epuck_goal/logs/goal_educational.jsonl`
- flagship wrapper log file: `examples/webots_epuck_goal/logs/flagship_goal.jsonl`

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

`lisp/main.lisp` exports at startup:

- `examples/webots_epuck_goal/out/tree.dot`

The educational entrypoint exports:

- `examples/webots_epuck_goal/out/educational_tree.dot`

Render with Graphviz:

```bash
dot -Tpng examples/webots_epuck_goal/out/tree.dot \
  -o examples/webots_epuck_goal/out/tree.png
```
