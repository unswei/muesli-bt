# Reusable Plot Tools

These scripts are backend-agnostic and read stable per-tick JSONL logs.

## Prerequisites

- Python 3.11+
- `matplotlib`
- `numpy`

Install quickly:

```bash
python3 -m pip install matplotlib numpy
```

## `plot_bt_timeline.py`

Plots BT active nodes over time from `bt.active_path`.

```bash
python3 examples/_tools/plot_bt_timeline.py \
  examples/webots_epuck_foraging/logs/foraging.jsonl \
  --max_nodes 24 \
  --out examples/webots_epuck_foraging/out/bt_timeline.png
```

Options:

- `--max_nodes`: cap displayed y-axis node count.
- `--out`: output PNG path.

## `plot_planner_root.py`

Generates budget adherence and planner-root action plots, plus common demo metrics.

```bash
python3 examples/_tools/plot_planner_root.py \
  examples/webots_epuck_goal/logs/goal.jsonl \
  --every 40 \
  --k 5 \
  --out_dir examples/webots_epuck_goal/out
```

Outputs include (when fields exist):

- `budget_adherence.png`
- `planner_confidence.png`
- `progressive_widening.png`
- `root_topk_aggregate.png`
- `root_topk_tick_<tick>.png`
- `path_xy.png`
- `action_scatter.png`
- `wheel_speeds.png`
- `min_obstacle.png`
- `line_error.png`
- `goal_distance.png`
- `target_distance.png`
- `evader_distance.png`
- `collected_over_time.png`
- `intercepts_over_time.png`

## `plot_success_rate.py`

Computes success/failure across multiple runs (for example, across seeds).

For foraging success (`collected >= 3`):

```bash
python3 examples/_tools/plot_success_rate.py \
  examples/webots_epuck_foraging/logs/foraging_seed_*.jsonl \
  --metric obs.collected \
  --threshold 3 \
  --out examples/webots_epuck_foraging/out/success_rate.png
```

For pursuit-evasion success (`intercepts >= 5`):

```bash
python3 examples/_tools/plot_success_rate.py \
  examples/webots_epuck_tag/logs/tag_seed_*.jsonl \
  --metric obs.intercepts \
  --threshold 5 \
  --out examples/webots_epuck_tag/out/success_rate.png
```
