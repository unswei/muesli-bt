# PyBullet Racecar Demo (muesli-bt v1.2 showcase)

This example provides a cross-platform visual PyBullet demo with:

- manual driving
- BT-style control loops
- bounded-time continuous-action MCTS planning
- stable JSONL logging + run metadata
- BT DOT export + Graphviz rendering

## Prerequisites

- Linux or macOS
- Python 3.11+
- muesli-bt built (`./build/dev/muslisp`) for BT DOT export
- optional Graphviz (`dot`) for SVG/PNG rendering

## Install

```bash
cd examples/pybullet_racecar
python -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

## Run Modes

From repo root:

### Step 1: Manual visual bring-up

```bash
python examples/pybullet_racecar/run_demo.py --mode manual
```

Controls: arrows or `WASD`/`IJKL`. If keyboard focus is flaky, use the on-screen steering/throttle sliders.
If you want more/less speed, tune `--max-speed` (default is `60.0`).
Keyboard backend can be forced with `--keyboard-backend pynput` (or `pybullet`).
On macOS GUI runs, `pynput` is disabled due a PyBullet SIGTRAP interaction; use `pybullet` backend + sliders.
Manual mode runs at 4x real-time by default (`--manual-realtime-speed 4.0`).

### Step 2: Basic BT-style constant drive

```bash
python examples/pybullet_racecar/run_demo.py --mode bt_basic
```

### Step 3: Obstacles + goal with heuristic BT switching

```bash
python examples/pybullet_racecar/run_demo.py --mode bt_obstacles
```

### Step 4: Hybrid BT + bounded-time MCTS planning

```bash
python examples/pybullet_racecar/run_demo.py --mode bt_planner --budget-ms 20
```

Budget sweep example:

```bash
python examples/pybullet_racecar/run_demo.py --mode bt_planner --budget-ms 5
python examples/pybullet_racecar/run_demo.py --mode bt_planner --budget-ms 10
python examples/pybullet_racecar/run_demo.py --mode bt_planner --budget-ms 20
python examples/pybullet_racecar/run_demo.py --mode bt_planner --budget-ms 50
```

Headless (CI/remote):

```bash
python examples/pybullet_racecar/run_demo.py --mode bt_planner --headless --no-sleep
```

## Logs and Metadata

Each run writes:

- `examples/pybullet_racecar/logs/<run_id>.jsonl`
- `examples/pybullet_racecar/logs/<run_id>.run_metadata.json`

See schema docs:

- `examples/pybullet_racecar/docs/log_schema.md`

## Plotting

```bash
python examples/pybullet_racecar/scripts/plot_logs.py \
  examples/pybullet_racecar/logs/<run_id>.jsonl \
  --out examples/pybullet_racecar/out/
```

Outputs include:

- budget adherence + histogram
- planner growth
- confidence
- action traces
- aggregate top-k visit distribution (if available)

## BT DOT Export

This uses the new `bt.export-dot` builtin in `muslisp`.

```bash
python examples/pybullet_racecar/scripts/render_bt_dot.py \
  --out examples/pybullet_racecar/out/bt.svg
```

If Graphviz is unavailable, the script still generates a `.dot` file.

## Files

- `run_demo.py`: demo entrypoint and controller/planner loop
- `bt/racecar_bt.mueslisp`: canonical BT DSL for diagram export
- `scripts/plot_logs.py`: log-to-plot utility
- `scripts/render_bt_dot.py`: DOT/SVG export helper
- `docs/demo.md`: long-form walkthrough
- `docs/log_schema.md`: stable log schema
- `docs/bt_nodes.md`: node-level behavior summary
