# Webots e-puck Pursuit-Evasion (Tag)

This demo uses the canonical `env.*` interface with BT mode switching:

- search
- pursue (planner-assisted)
- block (close-range interception)
- recover (collision safety)

The world includes a hunter e-puck and an evader e-puck.

## Folder layout

- `worlds/epuck_tag.wbt`: pursuit-evasion arena.
- `controllers/muesli_epuck/muesli_epuck.cpp`: tiny wrapper to the shared Webots controller.
- `lisp/main.mueslisp`: loop entrypoint and behaviour logic.
- `lisp/bt_tag.mueslisp`: BT structure.
- `logs/`: JSONL output.
- `out/`: plots and DOT renders.

## Build controller binary

From repo root:

```bash
cmake --preset dev -DMUESLI_BT_BUILD_WEBOTS_EXAMPLES=ON
cmake --build --preset dev -j --target muesli_webots_epuck_tag
```

## Open and run in Webots

1. Open `examples/webots_epuck_tag/worlds/epuck_tag.wbt` in Webots.
2. Ensure the hunter controller is `muesli_epuck`.
3. Press play.

### Headless / batch run (if supported)

```bash
"$WEBOTS_HOME/webots" --batch --mode=fast --stdout --stderr \
  examples/webots_epuck_tag/worlds/epuck_tag.wbt
```

## Logs

Default log file:

- `examples/webots_epuck_tag/logs/tag.jsonl`

Schema notes:

- `schema_version: "epuck_demo.v1"`
- per tick includes `obs.evader_dist`, `obs.intercepts`, `planner.*`, `budget.*`

## Visualisation

BT timeline:

```bash
python3 examples/_tools/plot_bt_timeline.py \
  examples/webots_epuck_tag/logs/tag.jsonl \
  --out examples/webots_epuck_tag/out/bt_timeline.png
```

Planner/metric plots:

```bash
python3 examples/_tools/plot_planner_root.py \
  examples/webots_epuck_tag/logs/tag.jsonl \
  --every 25 --k 5 \
  --out_dir examples/webots_epuck_tag/out
```

Success-rate plot across runs (for example, multiple seeds):

```bash
python3 examples/_tools/plot_success_rate.py \
  examples/webots_epuck_tag/logs/tag_seed_*.jsonl \
  --metric obs.intercepts \
  --threshold 5 \
  --out examples/webots_epuck_tag/out/success_rate.png
```

## BT DOT export

`lisp/main.mueslisp` exports at startup:

- `examples/webots_epuck_tag/out/tree.dot`

Render with Graphviz:

```bash
dot -Tpng examples/webots_epuck_tag/out/tree.dot \
  -o examples/webots_epuck_tag/out/tree.png
```
