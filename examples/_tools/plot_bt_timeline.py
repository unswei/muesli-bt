#!/usr/bin/env python3
from __future__ import annotations

import argparse
from collections import Counter
from pathlib import Path
from typing import Dict, List

import matplotlib.pyplot as plt
import numpy as np

from plot_common import ensure_dir, load_jsonl, tick_of


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Plot BT active path timeline from demo JSONL logs.")
    parser.add_argument("log_jsonl", type=Path, help="Path to a demo JSONL file.")
    parser.add_argument("--max_nodes", type=int, default=24, help="Maximum number of nodes to render on the y-axis.")
    parser.add_argument("--out", type=Path, default=Path("out/bt_timeline.png"), help="Output image path.")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    rows = load_jsonl(args.log_jsonl)
    if not rows:
        raise RuntimeError(f"No records found in {args.log_jsonl}")

    ticks: List[int] = []
    paths_by_tick: List[List[str]] = []
    frequency: Counter[str] = Counter()

    for index, row in enumerate(rows):
        tick = tick_of(row, index + 1)
        bt = row.get("bt", {})
        path = bt.get("active_path", []) if isinstance(bt, dict) else []
        if not isinstance(path, list):
            path = []
        path_text = [str(item) for item in path]
        ticks.append(tick)
        paths_by_tick.append(path_text)
        frequency.update(path_text)

    if not frequency:
        raise RuntimeError("No bt.active_path values found in the log file.")

    ordered_nodes = [node for node, _ in frequency.most_common(max(1, args.max_nodes))]
    node_to_row: Dict[str, int] = {name: i for i, name in enumerate(ordered_nodes)}

    data = np.zeros((len(ordered_nodes), len(ticks)), dtype=np.uint8)
    for col, path in enumerate(paths_by_tick):
        for node in path:
            row_index = node_to_row.get(node)
            if row_index is not None:
                data[row_index, col] = 1

    ensure_dir(args.out.parent)
    fig, ax = plt.subplots(figsize=(max(10, len(ticks) * 0.04), max(4, len(ordered_nodes) * 0.35)), constrained_layout=True)
    ax.imshow(data, aspect="auto", interpolation="nearest", cmap="Greens", origin="lower")
    ax.set_title("BT Active Path Timeline")
    ax.set_xlabel("tick")
    ax.set_ylabel("node")

    x_tick_count = min(12, len(ticks))
    x_positions = np.linspace(0, len(ticks) - 1, x_tick_count, dtype=int)
    ax.set_xticks(x_positions)
    ax.set_xticklabels([str(ticks[pos]) for pos in x_positions])

    ax.set_yticks(np.arange(len(ordered_nodes)))
    ax.set_yticklabels(ordered_nodes)

    fig.savefig(args.out, dpi=150)
    plt.close(fig)
    print(f"Saved: {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
