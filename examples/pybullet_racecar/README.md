# PyBullet Racecar Demo (muesli-bt v1.2 showcase)

This example provides a cross-platform visual PyBullet demo with:

- manual driving
- muesli-bt executed control loops in BT modes
- bounded-time continuous-action MCTS planning via `plan-action` node
- stable JSONL logging + run metadata
- BT DOT export + Graphviz rendering

## Prerequisites

- Linux or macOS
- Python 3.11+
- CMake + Ninja + C++20 toolchain
- optional Graphviz (`dot`) for SVG/PNG rendering

## Install

From repo root (recommended):

```bash
make demo-setup
```

This builds:

- `muslisp` runtime
- Python bridge module `muesli_bt_bridge`
- demo Python dependencies (including PyBullet)

Manual equivalent:

```bash
python3.11 -m venv .venv-py311
.venv-py311/bin/python -m pip install -r examples/pybullet_racecar/requirements.txt
cmake --preset dev \
  -DMUESLI_BT_BUILD_PYTHON_BRIDGE=ON \
  -DPython3_EXECUTABLE="$(pwd)/.venv-py311/bin/python" \
  -DPython3_FIND_VIRTUALENV=ONLY \
  -DPython3_FIND_STRATEGY=LOCATION
cmake --build --preset dev -j
```

## Run Modes

From repo root (recommended):

```bash
make demo-run MODE=manual
make demo-run MODE=bt_basic
make demo-run MODE=bt_obstacles
make demo-run MODE=bt_planner
```

Or direct command:

### Step 1: Manual visual bring-up

```bash
PYTHONPATH=build/dev/python \
  .venv-py311/bin/python examples/pybullet_racecar/run_demo.py --mode manual
```

Controls: arrows or `WASD`/`IJKL`. If keyboard focus is flaky, use the on-screen steering/throttle sliders.
Manual mode defaults to a 4x actuator boost (`--manual-action-scale 4.0`) and 4x real-time pacing.
If you want more/less speed, tune `--manual-action-scale` and/or `--max-speed`.
Keyboard backend can be forced with `--keyboard-backend pynput` (or `pybullet`).
On macOS GUI runs, `pynput` is disabled due a PyBullet SIGTRAP interaction; use `pybullet` backend + sliders.
Camera follow is enabled by default in GUI (`--no-follow-camera` to disable).

### Step 2: Basic BT-style constant drive

```bash
PYTHONPATH=build/dev/python \
  .venv-py311/bin/python examples/pybullet_racecar/run_demo.py --mode bt_basic
```

### Step 3: Obstacles + goal with heuristic BT switching

```bash
PYTHONPATH=build/dev/python \
  .venv-py311/bin/python examples/pybullet_racecar/run_demo.py --mode bt_obstacles
```

### Step 4: Hybrid BT + bounded-time MCTS planning

```bash
PYTHONPATH=build/dev/python \
  .venv-py311/bin/python examples/pybullet_racecar/run_demo.py --mode bt_planner --budget-ms 20
```

BT modes keep default pacing (`--bt-sim-speed 1.0`). Increase this value to make BT simulation advance faster per tick.

Budget sweep example:

```bash
PYTHONPATH=build/dev/python .venv-py311/bin/python examples/pybullet_racecar/run_demo.py --mode bt_planner --budget-ms 5
PYTHONPATH=build/dev/python .venv-py311/bin/python examples/pybullet_racecar/run_demo.py --mode bt_planner --budget-ms 10
PYTHONPATH=build/dev/python .venv-py311/bin/python examples/pybullet_racecar/run_demo.py --mode bt_planner --budget-ms 20
PYTHONPATH=build/dev/python .venv-py311/bin/python examples/pybullet_racecar/run_demo.py --mode bt_planner --budget-ms 50
```

Headless (CI/remote):

```bash
PYTHONPATH=build/dev/python \
  .venv-py311/bin/python examples/pybullet_racecar/run_demo.py --mode bt_planner --headless --no-sleep
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

- `run_demo.py`: demo entrypoint, PyBullet sim adapter, manual controls
- `native/`: demo-native C++/pybind pieces (`env.pybullet.run-loop`, racecar model, Python bridge module)
- `bt/racecar_bt.mueslisp`: canonical BT DSL for diagram export
- `scripts/plot_logs.py`: log-to-plot utility
- `scripts/render_bt_dot.py`: DOT/SVG export helper
- `docs/demo.md`: long-form walkthrough
- `docs/log_schema.md`: stable log schema
- `docs/bt_nodes.md`: node-level behavior summary
