# Reusable Plot Tools

These scripts are backend-agnostic and read stable per-tick JSONL logs.

## Prerequisites

- Python 3.11+
- `matplotlib`

Install quickly:

```bash
python3 -m pip install matplotlib
```

## `plot_bt_timeline.py`

Plots BT active nodes over time from `bt.active_path`.

```bash
python3 examples/_tools/plot_bt_timeline.py \
  examples/webots_epuck_obstacle/logs/obstacle.jsonl \
  --max_nodes 24 \
  --out examples/webots_epuck_obstacle/out/bt_timeline.png
```

Options:

- `--max_nodes`: cap displayed y-axis node count.
- `--out`: output PNG path.

## `plot_planner_root.py`

Generates budget adherence and planner-root action plots, and emits useful demo signal plots when present.

```bash
python3 examples/_tools/plot_planner_root.py \
  examples/webots_epuck_line/logs/line.jsonl \
  --every 40 \
  --k 5 \
  --out_dir examples/webots_epuck_line/out
```

Outputs include (when fields exist):

- `budget_adherence.png`
- `planner_confidence.png`
- `root_topk_aggregate.png`
- `root_topk_tick_<tick>.png`
- `min_obstacle.png`
- `line_error.png`
- `wheel_speeds.png`
