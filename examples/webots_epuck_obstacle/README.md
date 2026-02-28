# Webots e-puck Obstacle Avoidance + Wall Following

This demo uses the canonical `env.*` interface and a Lisp BT to switch between:

- collision avoidance
- wall following
- roaming

## Folder layout

- `worlds/epuck_obstacle_arena.wbt`: obstacle arena world.
- `controllers/muesli_epuck/muesli_epuck.cpp`: tiny wrapper that includes the shared Webots controller implementation.
- `../webots_epuck_common/muesli_epuck_controller_impl.cpp`: shared controller + `env` backend adapter used by both e-puck demos.
- `lisp/main.mueslisp`: loop entrypoint and behaviour logic.
- `lisp/bt_obstacle_wallfollow.mueslisp`: BT structure.
- `logs/`: JSONL output.
- `out/`: plots and DOT renders.

## Build controller binary

From repo root:

```bash
cmake --preset dev \
  -DMUESLI_BT_BUILD_WEBOTS_EXAMPLES=ON
cmake --build --preset dev -j \
  --target muesli_webots_epuck_obstacle
```

If CMake cannot locate Webots automatically, set `WEBOTS_HOME` first (for example on macOS:
`export WEBOTS_HOME=/Applications/Webots.app/Contents`).

## Open and run in Webots

1. Open `examples/webots_epuck_obstacle/worlds/epuck_obstacle_arena.wbt` in Webots.
2. Ensure controller is `muesli_epuck`.
3. Press play.

### Headless / batch run (if supported by your Webots install)

```bash
"$WEBOTS_HOME/webots" --batch --mode=fast --stdout --stderr \
  examples/webots_epuck_obstacle/worlds/epuck_obstacle_arena.wbt
```

## Logs

Default log file:

- `examples/webots_epuck_obstacle/logs/obstacle.jsonl`

Schema notes:

- `schema_version: "epuck_demo.v1"`
- per tick includes `tick`, `t_ms`, `bt.active_path`, `obs`, `action.u`, `budget.tick_budget_ms`, `budget.tick_time_ms`

## Behaviour tuning notes

Current obstacle behaviour is tuned for the included arena and e-puck proximity normalisation (`raw / 4096.0`):

- collision branch triggers when `pmax > 0.44` or front pair sum `> 0.60`
- wall-follow branch triggers when side/front activity `> 0.50`
- roam uses lower base speed with mild steering noise

If the robot still drives into a wall for your machine/Webots version, retune the thresholds in
`lisp/main.mueslisp`:

- increase collision sensitivity by lowering `0.44` or `0.60`
- reduce wall-follow sensitivity by raising `0.50`
- lower roam base speed (`base`) for a more conservative gait

When tuning, clear old logs first so plots and stats reflect one run only:

```bash
rm -f examples/webots_epuck_obstacle/logs/obstacle.jsonl
```

## Plot generation

BT active path timeline:

```bash
python3 examples/_tools/plot_bt_timeline.py \
  examples/webots_epuck_obstacle/logs/obstacle.jsonl \
  --out examples/webots_epuck_obstacle/out/bt_timeline.png
```

Signals + budget plots:

```bash
python3 examples/_tools/plot_planner_root.py \
  examples/webots_epuck_obstacle/logs/obstacle.jsonl \
  --out_dir examples/webots_epuck_obstacle/out
```

## BT DOT export

`lisp/main.mueslisp` exports at startup:

- `examples/webots_epuck_obstacle/out/tree.dot`

Render with Graphviz:

```bash
dot -Tpng examples/webots_epuck_obstacle/out/tree.dot \
  -o examples/webots_epuck_obstacle/out/tree.png
```
