# Webots e-puck Foraging (Collect and Return)

This demo uses the canonical `env.*` interface with BT sequencing:

- search for pucks
- approach target puck (planner-assisted)
- return to base when carrying (planner-assisted)
- recover on imminent collision

The scene uses simple coloured puck solids and a base marker; the backend tracks carry/delivery state.

## Folder layout

- `worlds/epuck_foraging.wbt`: base + pucks + simple obstacles.
- `controllers/muesli_epuck/muesli_epuck.cpp`: tiny wrapper to the shared Webots controller.
- `lisp/main.lisp`: loop entrypoint and behaviour logic.
- `lisp/bt_foraging.lisp`: BT structure.
- `logs/`: JSONL output.
- `out/`: plots and DOT renders.

## Build controller binary

From repo root:

```bash
cmake --preset dev -DMUESLI_BT_BUILD_WEBOTS_EXAMPLES=ON
cmake --build --preset dev -j --target muesli_webots_epuck_foraging
```

## Open and run in Webots

1. Open `examples/webots_epuck_foraging/worlds/epuck_foraging.wbt` in Webots.
2. Ensure controller is `muesli_epuck`.
3. Press play.

### Headless / batch run (if supported)

```bash
"$WEBOTS_HOME/webots" --batch --mode=fast --stdout --stderr \
  examples/webots_epuck_foraging/worlds/epuck_foraging.wbt
```

## Logs

Default log file:

- `examples/webots_epuck_foraging/logs/foraging.jsonl`

Schema notes:

- `schema_version: "epuck_demo.v1"`
- per tick includes `bt.active_path`, `obs.collected`, `obs.carrying`, `obs.target_dist`, `planner.*`, `budget.*`

## Visualisation

BT timeline:

```bash
python3 examples/_tools/plot_bt_timeline.py \
  examples/webots_epuck_foraging/logs/foraging.jsonl \
  --out examples/webots_epuck_foraging/out/bt_timeline.png
```

Planner/metric plots:

```bash
python3 examples/_tools/plot_planner_root.py \
  examples/webots_epuck_foraging/logs/foraging.jsonl \
  --every 30 --k 5 \
  --out_dir examples/webots_epuck_foraging/out
```

Success-rate plot across runs (for example, multiple seeds):

```bash
python3 examples/_tools/plot_success_rate.py \
  examples/webots_epuck_foraging/logs/foraging_seed_*.jsonl \
  --metric obs.collected \
  --threshold 3 \
  --out examples/webots_epuck_foraging/out/success_rate.png
```

## BT DOT export

`lisp/main.lisp` exports at startup:

- `examples/webots_epuck_foraging/out/tree.dot`

Render with Graphviz:

```bash
dot -Tpng examples/webots_epuck_foraging/out/tree.dot \
  -o examples/webots_epuck_foraging/out/tree.png
```
