#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path
from typing import Any, Dict, List, Tuple

import matplotlib.pyplot as plt

from plot_common import ensure_dir, load_jsonl, nested_get


JsonMap = Dict[str, Any]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Plot success rate across multiple JSONL runs.")
    parser.add_argument("logs", nargs="+", type=Path, help="Run JSONL paths (one per seed/run).")
    parser.add_argument("--metric", default="obs.collected", help="Nested metric key path used for success.")
    parser.add_argument("--threshold", type=float, default=1.0, help="Success threshold on the metric.")
    parser.add_argument("--out", type=Path, default=Path("out/success_rate.png"), help="Output figure path.")
    parser.add_argument("--csv", type=Path, default=None, help="Optional CSV output path.")
    return parser.parse_args()


def as_float(value: Any) -> float | None:
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def run_seed(rows: List[JsonMap], fallback: int) -> str:
    if not rows:
        return str(fallback)
    seed = nested_get(rows[0], "obs.seed")
    if seed is None:
        return str(fallback)
    return str(seed)


def run_metric_max(rows: List[JsonMap], metric: str) -> float:
    best = float("-inf")
    for row in rows:
        value = as_float(nested_get(row, metric))
        if value is None:
            continue
        if value > best:
            best = value
    if best == float("-inf"):
        return 0.0
    return best


def main() -> int:
    args = parse_args()

    labels: List[str] = []
    successes: List[int] = []
    maxima: List[float] = []

    for i, log_path in enumerate(args.logs, start=1):
        rows = load_jsonl(log_path)
        if not rows:
            continue
        metric_max = run_metric_max(rows, args.metric)
        success = 1 if metric_max >= args.threshold else 0
        labels.append(run_seed(rows, i))
        successes.append(success)
        maxima.append(metric_max)

    if not labels:
        raise RuntimeError("No usable run logs were provided.")

    rate = sum(successes) / float(len(successes))

    out_path = args.out
    ensure_dir(out_path.parent)

    fig, ax = plt.subplots(figsize=(max(7.5, 0.55 * len(labels) + 4.0), 4.6), constrained_layout=True)
    colours = ["#2a9d8f" if s else "#bb3e03" for s in successes]
    bars = ax.bar(labels, successes, color=colours)
    ax.set_ylim(0.0, 1.1)
    ax.set_xlabel("seed")
    ax.set_ylabel("success")
    ax.set_title(f"Success Rate ({sum(successes)}/{len(successes)} = {rate:.1%})")
    ax.grid(alpha=0.25, axis="y")

    for bar, metric_max in zip(bars, maxima):
        ax.text(bar.get_x() + bar.get_width() * 0.5,
                bar.get_height() + 0.03,
                f"{metric_max:.2f}",
                ha="center",
                va="bottom",
                fontsize=8)

    fig.savefig(out_path, dpi=150)
    plt.close(fig)

    if args.csv is not None:
        ensure_dir(args.csv.parent)
        with args.csv.open("w", encoding="utf-8") as handle:
            handle.write("seed,success,metric_max\n")
            for label, success, metric_max in zip(labels, successes, maxima):
                handle.write(f"{label},{success},{metric_max:.6f}\n")

    print(f"Saved success-rate plot: {out_path}")
    print(f"Success rate: {rate:.3f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
