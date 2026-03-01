# Webots e-puck Line Following

This demo uses the canonical `env.*` interface with:

- line reacquisition (`line_lost -> search_line`)
- line following
- bounded `plan-action` confidence/top-k logging for follow mode

## Folder layout

- `worlds/epuck_line_follow.wbt`: line-follow world.
- `controllers/muesli_epuck/muesli_epuck.cpp`: tiny wrapper that includes the shared Webots controller implementation.
- `../webots_epuck_common/muesli_epuck_controller_impl.cpp`: shared controller + `env` backend adapter used by both e-puck demos.
- `lisp/main.lisp`: loop entrypoint, control logic, and planner log shaping.
- `lisp/bt_line_follow.lisp`: BT structure.
- `logs/`: JSONL output.
- `out/`: plots and DOT renders.

## Build controller binary

From repo root:

```bash
cmake --preset dev \
  -DMUESLI_BT_BUILD_WEBOTS_EXAMPLES=ON
cmake --build --preset dev -j \
  --target muesli_webots_epuck_line
```

If needed, export `WEBOTS_HOME` before configuring.

## Open and run in Webots

1. Open `examples/webots_epuck_line/worlds/epuck_line_follow.wbt` in Webots.
2. Ensure controller is `muesli_epuck`.
3. Press play.

### Headless / batch run (if supported by your Webots install)

```bash
"$WEBOTS_HOME/webots" --batch --mode=fast --stdout --stderr \
  examples/webots_epuck_line/worlds/epuck_line_follow.wbt
```

## Logs

Default log file:

- `examples/webots_epuck_line/logs/line.jsonl`

Schema notes:

- `schema_version: "epuck_demo.v1"`
- per tick includes `tick`, `t_ms`, `bt.active_path`, `obs.line_error`, `action.u`, `budget.*`
- planner ticks include `planner.used`, `planner.confidence`, `planner.top_k`, `planner.root_visits`, etc.

## Plot generation

BT active path timeline:

```bash
python3 examples/_tools/plot_bt_timeline.py \
  examples/webots_epuck_line/logs/line.jsonl \
  --out examples/webots_epuck_line/out/bt_timeline.png
```

Planner + signal plots:

```bash
python3 examples/_tools/plot_planner_root.py \
  examples/webots_epuck_line/logs/line.jsonl \
  --every 40 --k 5 \
  --out_dir examples/webots_epuck_line/out
```

## BT DOT export

`lisp/main.lisp` exports at startup:

- `examples/webots_epuck_line/out/tree.dot`

Render with Graphviz:

```bash
dot -Tsvg examples/webots_epuck_line/out/tree.dot \
  -o examples/webots_epuck_line/out/tree.svg
```
