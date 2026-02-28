#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from collections import defaultdict
from pathlib import Path
from typing import Dict, List

import matplotlib.pyplot as plt


def load_jsonl(path: Path) -> List[Dict[str, object]]:
    rows: List[Dict[str, object]] = []
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line:
            continue
        rows.append(json.loads(line))
    return rows


def save_budget_adherence(rows: List[Dict[str, object]], out_dir: Path) -> None:
    planner_rows = [row for row in rows if isinstance(row.get("planner"), dict)]
    if not planner_rows:
        return

    ticks = [int(row["tick_index"]) for row in planner_rows]
    times = [float(row["planner"]["time_used_ms"]) for row in planner_rows]  # type: ignore[index]
    budgets = [float(row["planner"]["budget_ms"]) for row in planner_rows]  # type: ignore[index]
    budget_line = budgets[0] if budgets else 0.0

    fig, (ax_line, ax_hist) = plt.subplots(2, 1, figsize=(10, 7), constrained_layout=True)
    ax_line.plot(ticks, times, color="#007f5f", linewidth=1.7, label="time_used_ms")
    ax_line.axhline(budget_line, color="#d62828", linestyle="--", linewidth=1.2, label=f"budget={budget_line:.2f}ms")
    ax_line.set_title("Planner Budget Adherence")
    ax_line.set_xlabel("tick_index")
    ax_line.set_ylabel("time_used_ms")
    ax_line.legend()
    ax_line.grid(alpha=0.25)

    ax_hist.hist(times, bins=24, color="#457b9d", alpha=0.85)
    ax_hist.set_title("Planner time_used_ms Histogram")
    ax_hist.set_xlabel("time_used_ms")
    ax_hist.set_ylabel("count")
    ax_hist.grid(alpha=0.25)

    out_path = out_dir / "budget_adherence.png"
    fig.savefig(out_path, dpi=140)
    plt.close(fig)


def save_planner_growth(rows: List[Dict[str, object]], out_dir: Path) -> None:
    planner_rows = [row for row in rows if isinstance(row.get("planner"), dict)]
    if not planner_rows:
        return

    ticks = [int(row["tick_index"]) for row in planner_rows]
    root_visits = [int(row["planner"]["root_visits"]) for row in planner_rows]  # type: ignore[index]
    root_children = [int(row["planner"]["root_children"]) for row in planner_rows]  # type: ignore[index]
    widen_added = [int(row["planner"]["widen_added"]) for row in planner_rows]  # type: ignore[index]

    fig, (ax_a, ax_b) = plt.subplots(2, 1, figsize=(10, 7), constrained_layout=True)
    ax_a.plot(ticks, root_visits, color="#2a9d8f", linewidth=1.7, label="root_visits")
    ax_a.plot(ticks, root_children, color="#f4a261", linewidth=1.7, label="root_children")
    ax_a.set_title("Planner Growth")
    ax_a.set_xlabel("tick_index")
    ax_a.set_ylabel("count")
    ax_a.legend()
    ax_a.grid(alpha=0.25)

    ax_b.bar(ticks, widen_added, color="#e76f51", width=0.8)
    ax_b.set_title("Progressive Widening Additions per Tick")
    ax_b.set_xlabel("tick_index")
    ax_b.set_ylabel("widen_added")
    ax_b.grid(alpha=0.25)

    out_path = out_dir / "planner_growth.png"
    fig.savefig(out_path, dpi=140)
    plt.close(fig)


def save_confidence(rows: List[Dict[str, object]], out_dir: Path) -> None:
    planner_rows = [row for row in rows if isinstance(row.get("planner"), dict)]
    if not planner_rows:
        return

    ticks = [int(row["tick_index"]) for row in planner_rows]
    confidence = [float(row["planner"]["confidence"]) for row in planner_rows]  # type: ignore[index]

    fig, ax = plt.subplots(figsize=(10, 3.6), constrained_layout=True)
    ax.plot(ticks, confidence, color="#264653", linewidth=1.7)
    ax.set_title("Planner Confidence")
    ax.set_xlabel("tick_index")
    ax.set_ylabel("confidence")
    ax.grid(alpha=0.25)
    fig.savefig(out_dir / "confidence.png", dpi=140)
    plt.close(fig)


def save_action_trace(rows: List[Dict[str, object]], out_dir: Path) -> None:
    ticks = [int(row["tick_index"]) for row in rows]
    steer = [float(row["action"]["steering"]) for row in rows]  # type: ignore[index]
    throttle = [float(row["action"]["throttle"]) for row in rows]  # type: ignore[index]

    fig, (ax_s, ax_t) = plt.subplots(2, 1, figsize=(10, 6), constrained_layout=True)
    ax_s.plot(ticks, steer, color="#e63946", linewidth=1.5)
    ax_s.set_title("Steering Trace")
    ax_s.set_xlabel("tick_index")
    ax_s.set_ylabel("steering")
    ax_s.grid(alpha=0.25)

    ax_t.plot(ticks, throttle, color="#1d3557", linewidth=1.5)
    ax_t.set_title("Throttle Trace")
    ax_t.set_xlabel("tick_index")
    ax_t.set_ylabel("throttle")
    ax_t.grid(alpha=0.25)

    fig.savefig(out_dir / "action_traces.png", dpi=140)
    plt.close(fig)


def save_top_k_distribution(rows: List[Dict[str, object]], out_dir: Path) -> None:
    planner_rows = [row for row in rows if isinstance(row.get("planner"), dict)]
    totals: Dict[str, int] = defaultdict(int)
    for row in planner_rows:
        planner = row["planner"]  # type: ignore[assignment]
        top_k = planner.get("top_k", [])  # type: ignore[union-attr]
        if not isinstance(top_k, list):
            continue
        for candidate in top_k:
            if not isinstance(candidate, dict):
                continue
            action = candidate.get("action")
            if not isinstance(action, dict):
                continue
            steering = float(action.get("steering", 0.0))
            throttle = float(action.get("throttle", 0.0))
            label = f"s={steering:+.2f}, t={throttle:.2f}"
            totals[label] += int(candidate.get("visits", 0))

    if not totals:
        return

    top_items = sorted(totals.items(), key=lambda item: item[1], reverse=True)[:12]
    labels = [item[0] for item in top_items]
    visits = [item[1] for item in top_items]

    fig, ax = plt.subplots(figsize=(11, 4.8), constrained_layout=True)
    ax.bar(labels, visits, color="#588157")
    ax.set_title("Aggregate Top-k Visit Distribution")
    ax.set_xlabel("action bins")
    ax.set_ylabel("aggregated visits")
    ax.tick_params(axis="x", rotation=35)
    ax.grid(alpha=0.25, axis="y")
    fig.savefig(out_dir / "topk_visit_distribution.png", dpi=140)
    plt.close(fig)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Generate plots from racecar demo JSONL logs.")
    parser.add_argument("log_jsonl", type=Path, help="Path to a racecar demo JSONL log.")
    parser.add_argument("--out", type=Path, default=Path("out"), help="Directory for plot outputs.")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    rows = load_jsonl(args.log_jsonl)
    if not rows:
        raise RuntimeError(f"No rows found in {args.log_jsonl}")
    args.out.mkdir(parents=True, exist_ok=True)

    save_budget_adherence(rows, args.out)
    save_planner_growth(rows, args.out)
    save_confidence(rows, args.out)
    save_action_trace(rows, args.out)
    save_top_k_distribution(rows, args.out)
    print(f"Saved plots to: {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
