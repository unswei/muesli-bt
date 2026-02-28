#!/usr/bin/env python3
from __future__ import annotations

import argparse
from collections import defaultdict
from pathlib import Path
from typing import Any, Dict, List, Tuple

import matplotlib.pyplot as plt

from plot_common import ensure_dir, load_jsonl, nested_get, tick_of


JsonMap = Dict[str, Any]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Plot planner root distribution and budget adherence from demo JSONL logs.")
    parser.add_argument("log_jsonl", type=Path, help="Path to demo JSONL.")
    parser.add_argument("--every", type=int, default=40, help="Tick stride for per-tick top-k bar charts.")
    parser.add_argument("--k", type=int, default=5, help="Top-k actions to plot.")
    parser.add_argument("--out_dir", type=Path, default=Path("out"), help="Output directory for figures.")
    return parser.parse_args()


def as_float(value: Any) -> float | None:
    try:
        out = float(value)
    except (TypeError, ValueError):
        return None
    return out


def planner_rows(rows: List[JsonMap]) -> List[Tuple[int, JsonMap]]:
    out: List[Tuple[int, JsonMap]] = []
    for i, row in enumerate(rows):
        planner = row.get("planner")
        if isinstance(planner, dict):
            out.append((tick_of(row, i + 1), planner))
    return out


def plot_budget(rows: List[JsonMap], out_dir: Path) -> None:
    ticks: List[int] = []
    time_used_ms: List[float] = []
    budget_ms: List[float] = []

    for i, row in enumerate(rows):
        budget = row.get("budget")
        if not isinstance(budget, dict):
            continue
        tick_time = as_float(budget.get("tick_time_ms"))
        tick_budget = as_float(budget.get("tick_budget_ms"))
        if tick_time is None or tick_budget is None:
            continue
        ticks.append(tick_of(row, i + 1))
        time_used_ms.append(tick_time)
        budget_ms.append(tick_budget)

    if not ticks:
        return

    fig, (ax_line, ax_hist) = plt.subplots(2, 1, figsize=(10, 7), constrained_layout=True)
    ax_line.plot(ticks, time_used_ms, color="#0a9396", linewidth=1.7, label="tick_time_ms")
    ax_line.plot(ticks, budget_ms, color="#bb3e03", linewidth=1.3, linestyle="--", label="tick_budget_ms")
    ax_line.set_title("Tick Budget Adherence")
    ax_line.set_xlabel("tick")
    ax_line.set_ylabel("ms")
    ax_line.grid(alpha=0.25)
    ax_line.legend()

    ax_hist.hist(time_used_ms, bins=24, color="#5e548e", alpha=0.85)
    ax_hist.set_title("Tick Time Histogram")
    ax_hist.set_xlabel("tick_time_ms")
    ax_hist.set_ylabel("count")
    ax_hist.grid(alpha=0.25)

    fig.savefig(out_dir / "budget_adherence.png", dpi=150)
    plt.close(fig)


def plot_planner_confidence(rows: List[Tuple[int, JsonMap]], out_dir: Path) -> None:
    points: List[Tuple[int, float]] = []
    for tick, planner in rows:
        if not bool(planner.get("used", False)):
            continue
        conf = as_float(planner.get("confidence"))
        if conf is None:
            continue
        points.append((tick, conf))
    if not points:
        return

    ticks = [tick for tick, _ in points]
    confidence = [conf for _, conf in points]

    fig, ax = plt.subplots(figsize=(10, 3.8), constrained_layout=True)
    ax.plot(ticks, confidence, color="#1d3557", linewidth=1.7)
    ax.set_title("Planner Confidence")
    ax.set_xlabel("tick")
    ax.set_ylabel("confidence")
    ax.grid(alpha=0.25)
    fig.savefig(out_dir / "planner_confidence.png", dpi=150)
    plt.close(fig)


def plot_progressive_widening(rows: List[Tuple[int, JsonMap]], out_dir: Path) -> None:
    ticks: List[int] = []
    widen_added: List[float] = []
    root_children: List[float] = []

    for tick, planner in rows:
        if not bool(planner.get("used", False)):
            continue
        wa = as_float(planner.get("widen_added"))
        rc = as_float(planner.get("root_children"))
        if wa is None and rc is None:
            continue
        ticks.append(tick)
        widen_added.append(wa if wa is not None else 0.0)
        root_children.append(rc if rc is not None else 0.0)

    if not ticks:
        return

    cumulative_widen: List[float] = []
    total = 0.0
    for value in widen_added:
        total += value
        cumulative_widen.append(total)

    fig, (ax_top, ax_bottom) = plt.subplots(2, 1, figsize=(10, 7), constrained_layout=True)
    ax_top.plot(ticks, widen_added, color="#2a9d8f", linewidth=1.5, label="widen_added")
    ax_top.plot(ticks, root_children, color="#264653", linewidth=1.3, label="root_children")
    ax_top.set_title("Progressive Widening per Tick")
    ax_top.set_xlabel("tick")
    ax_top.set_ylabel("count")
    ax_top.grid(alpha=0.25)
    ax_top.legend()

    ax_bottom.plot(ticks, cumulative_widen, color="#e76f51", linewidth=1.7)
    ax_bottom.set_title("Cumulative Widen Added")
    ax_bottom.set_xlabel("tick")
    ax_bottom.set_ylabel("cumulative count")
    ax_bottom.grid(alpha=0.25)

    fig.savefig(out_dir / "progressive_widening.png", dpi=150)
    plt.close(fig)


def action_label(action_value: Any) -> str:
    if isinstance(action_value, list):
        numbers = [float(v) for v in action_value]
        if len(numbers) >= 2:
            return f"[{numbers[0]:+.2f}, {numbers[1]:+.2f}]"
        if len(numbers) == 1:
            return f"[{numbers[0]:+.2f}]"
    return str(action_value)


def top_k_entries(planner: JsonMap, k: int) -> List[JsonMap]:
    top_k = planner.get("top_k", [])
    if not isinstance(top_k, list):
        return []
    entries = [entry for entry in top_k if isinstance(entry, dict)]
    entries.sort(key=lambda item: int(item.get("visits", 0)), reverse=True)
    return entries[: max(1, k)]


def plot_root_distribution(rows: List[Tuple[int, JsonMap]], out_dir: Path, every: int, k: int) -> None:
    aggregate_visits: Dict[str, int] = defaultdict(int)
    sampled = 0

    for tick, planner in rows:
        if not bool(planner.get("used", False)):
            continue
        if every > 1 and (tick % every) != 0:
            continue

        entries = top_k_entries(planner, k)
        if not entries:
            continue
        sampled += 1

        labels = [action_label(entry.get("u", entry.get("action"))) for entry in entries]
        visits = [int(entry.get("visits", 0)) for entry in entries]
        q_values = [float(entry.get("q", 0.0)) for entry in entries]

        fig, ax = plt.subplots(figsize=(9, 4.4), constrained_layout=True)
        bars = ax.bar(labels, visits, color="#386641")
        ax.set_title(f"Planner Root Top-{len(entries)} at tick {tick}")
        ax.set_xlabel("action")
        ax.set_ylabel("visits")
        ax.tick_params(axis="x", rotation=30)
        ax.grid(alpha=0.25, axis="y")

        for bar, q in zip(bars, q_values):
            ax.text(bar.get_x() + bar.get_width() * 0.5,
                    bar.get_height(),
                    f"q={q:.2f}",
                    ha="center",
                    va="bottom",
                    fontsize=8)

        fig.savefig(out_dir / f"root_topk_tick_{tick}.png", dpi=150)
        plt.close(fig)

        for label, visit in zip(labels, visits):
            aggregate_visits[label] += visit

    if sampled == 0 or not aggregate_visits:
        return

    top_items = sorted(aggregate_visits.items(), key=lambda item: item[1], reverse=True)[: max(6, k)]
    labels = [item[0] for item in top_items]
    visits = [item[1] for item in top_items]

    fig, ax = plt.subplots(figsize=(10, 4.8), constrained_layout=True)
    ax.bar(labels, visits, color="#2a9d8f")
    ax.set_title("Aggregate Planner Root Action Distribution")
    ax.set_xlabel("action")
    ax.set_ylabel("aggregated visits")
    ax.tick_params(axis="x", rotation=30)
    ax.grid(alpha=0.25, axis="y")
    fig.savefig(out_dir / "root_topk_aggregate.png", dpi=150)
    plt.close(fig)


def plot_series(rows: List[JsonMap], out_dir: Path, key_path: str, title: str, ylabel: str, out_name: str) -> None:
    ticks: List[int] = []
    values: List[float] = []
    for i, row in enumerate(rows):
        value = as_float(nested_get(row, key_path))
        if value is None:
            continue
        ticks.append(tick_of(row, i + 1))
        values.append(value)

    if not ticks:
        return

    fig, ax = plt.subplots(figsize=(10, 3.8), constrained_layout=True)
    ax.plot(ticks, values, linewidth=1.7, color="#005f73")
    ax.set_title(title)
    ax.set_xlabel("tick")
    ax.set_ylabel(ylabel)
    ax.grid(alpha=0.25)
    fig.savefig(out_dir / out_name, dpi=150)
    plt.close(fig)


def plot_wheel_speeds(rows: List[JsonMap], out_dir: Path) -> None:
    ticks: List[int] = []
    left: List[float] = []
    right: List[float] = []

    for i, row in enumerate(rows):
        action = row.get("action")
        if not isinstance(action, dict):
            continue
        u = action.get("u")
        if not isinstance(u, list) or len(u) < 2:
            continue
        left_speed = as_float(u[0])
        right_speed = as_float(u[1])
        if left_speed is None or right_speed is None:
            continue
        ticks.append(tick_of(row, i + 1))
        left.append(left_speed)
        right.append(right_speed)

    if not ticks:
        return

    fig, ax = plt.subplots(figsize=(10, 4.2), constrained_layout=True)
    ax.plot(ticks, left, color="#3a86ff", linewidth=1.6, label="left wheel")
    ax.plot(ticks, right, color="#ff006e", linewidth=1.6, label="right wheel")
    ax.set_title("Wheel Speed Trace")
    ax.set_xlabel("tick")
    ax.set_ylabel("rad/s")
    ax.grid(alpha=0.25)
    ax.legend()
    fig.savefig(out_dir / "wheel_speeds.png", dpi=150)
    plt.close(fig)


def plot_action_scatter(rows: List[JsonMap], out_dir: Path) -> None:
    left: List[float] = []
    right: List[float] = []
    ticks: List[int] = []

    for i, row in enumerate(rows):
        u = nested_get(row, "action.u")
        if not isinstance(u, list) or len(u) < 2:
            continue
        lu = as_float(u[0])
        ru = as_float(u[1])
        if lu is None or ru is None:
            continue
        left.append(lu)
        right.append(ru)
        ticks.append(tick_of(row, i + 1))

    if not ticks:
        return

    fig, ax = plt.subplots(figsize=(6.8, 6.4), constrained_layout=True)
    scatter = ax.scatter(left, right, c=ticks, cmap="viridis", s=12, alpha=0.8)
    ax.set_title("Sampled Action Scatter")
    ax.set_xlabel("left wheel")
    ax.set_ylabel("right wheel")
    ax.grid(alpha=0.25)
    fig.colorbar(scatter, ax=ax, label="tick")
    fig.savefig(out_dir / "action_scatter.png", dpi=150)
    plt.close(fig)


def plot_path_xy(rows: List[JsonMap], out_dir: Path) -> None:
    xs: List[float] = []
    ys: List[float] = []

    goal_xy: Tuple[float, float] | None = None
    base_xy: Tuple[float, float] | None = None

    for row in rows:
        robot_xy = nested_get(row, "obs.robot_xy")
        if isinstance(robot_xy, list) and len(robot_xy) >= 2:
            rx = as_float(robot_xy[0])
            ry = as_float(robot_xy[1])
            if rx is not None and ry is not None:
                xs.append(rx)
                ys.append(ry)

        if goal_xy is None:
            g = nested_get(row, "obs.goal_xy")
            if isinstance(g, list) and len(g) >= 2:
                gx = as_float(g[0])
                gy = as_float(g[1])
                if gx is not None and gy is not None:
                    goal_xy = (gx, gy)

        if base_xy is None:
            b = nested_get(row, "obs.base_xy")
            if isinstance(b, list) and len(b) >= 2:
                bx = as_float(b[0])
                by = as_float(b[1])
                if bx is not None and by is not None:
                    base_xy = (bx, by)

    if len(xs) < 2:
        return

    fig, ax = plt.subplots(figsize=(6.8, 6.2), constrained_layout=True)
    ax.plot(xs, ys, color="#264653", linewidth=1.6, label="robot path")
    ax.scatter([xs[0]], [ys[0]], color="#2a9d8f", s=36, label="start")
    ax.scatter([xs[-1]], [ys[-1]], color="#e76f51", s=36, label="end")

    if goal_xy is not None:
        ax.scatter([goal_xy[0]], [goal_xy[1]], color="#f4a261", marker="*", s=120, label="goal")
    if base_xy is not None:
        ax.scatter([base_xy[0]], [base_xy[1]], color="#457b9d", marker="s", s=52, label="base")

    ax.set_title("2D Robot Path")
    ax.set_xlabel("x")
    ax.set_ylabel("y")
    ax.axis("equal")
    ax.grid(alpha=0.25)
    ax.legend(loc="best")
    fig.savefig(out_dir / "path_xy.png", dpi=150)
    plt.close(fig)


def main() -> int:
    args = parse_args()
    rows = load_jsonl(args.log_jsonl)
    if not rows:
        raise RuntimeError(f"No rows found in {args.log_jsonl}")

    out_dir = ensure_dir(args.out_dir)

    planner = planner_rows(rows)
    plot_budget(rows, out_dir)
    plot_planner_confidence(planner, out_dir)
    plot_progressive_widening(planner, out_dir)
    plot_root_distribution(planner, out_dir, every=max(1, args.every), k=max(1, args.k))

    plot_series(rows, out_dir, "obs.min_obstacle", "Minimum Obstacle Distance Proxy", "min_obstacle", "min_obstacle.png")
    plot_series(rows, out_dir, "obs.line_error", "Line Following Error", "line_error", "line_error.png")
    plot_series(rows, out_dir, "obs.goal_dist", "Goal Distance", "goal_dist", "goal_distance.png")
    plot_series(rows, out_dir, "obs.target_dist", "Foraging Target Distance", "target_dist", "target_distance.png")
    plot_series(rows, out_dir, "obs.evader_dist", "Relative Distance to Evader", "evader_dist", "evader_distance.png")
    plot_series(rows, out_dir, "obs.collected", "Collected Pucks", "count", "collected_over_time.png")
    plot_series(rows, out_dir, "obs.intercepts", "Intercept Count", "count", "intercepts_over_time.png")

    plot_wheel_speeds(rows, out_dir)
    plot_action_scatter(rows, out_dir)
    plot_path_xy(rows, out_dir)

    print(f"Saved plots to: {out_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
