#!/usr/bin/env python3

from __future__ import annotations

import argparse
import csv
import math
from pathlib import Path


BENCH_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_RESULTS_ROOT = BENCH_ROOT / "results"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate an SVG tail-latency figure from benchmark aggregate_summary.csv output."
    )
    parser.add_argument(
        "result_dir",
        nargs="?",
        help="benchmark result directory; defaults to the latest directory under bench/results",
    )
    parser.add_argument(
        "--output",
        "-o",
        help="output SVG path; defaults to <result-dir>/tail_latency.svg",
    )
    parser.add_argument(
        "--limit",
        type=int,
        default=12,
        help="maximum number of scenarios to show, ordered by p99.9 latency",
    )
    return parser.parse_args()


def read_csv_rows(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def latest_result_dir(root: Path) -> Path:
    candidates = [
        child
        for child in root.iterdir()
        if child.is_dir() and (child / "aggregate_summary.csv").exists()
    ]
    if not candidates:
        raise FileNotFoundError(f"no benchmark result directories found under {root}")
    return max(candidates, key=lambda path: path.stat().st_mtime)


def select_result_dir(argument: str | None) -> Path:
    result_dir = Path(argument).expanduser().resolve() if argument else latest_result_dir(DEFAULT_RESULTS_ROOT)
    if not result_dir.is_dir():
        raise FileNotFoundError(f"benchmark result directory does not exist: {result_dir}")
    if not (result_dir / "aggregate_summary.csv").exists():
        raise FileNotFoundError(f"missing required benchmark output: {result_dir / 'aggregate_summary.csv'}")
    return result_dir


def maybe_float(value: str | None) -> float | None:
    if value in (None, ""):
        return None
    return float(value)


def xml_escape(value: str) -> str:
    return (
        value.replace("&", "&amp;")
        .replace("<", "&lt;")
        .replace(">", "&gt;")
        .replace('"', "&quot;")
    )


def format_latency_ns(value: float) -> str:
    if value >= 1_000_000.0:
        return f"{value / 1_000_000.0:.2f} ms"
    if value >= 1_000.0:
        return f"{value / 1_000.0:.2f} us"
    return f"{value:.0f} ns"


def short_label(scenario_id: str) -> str:
    label = scenario_id
    for prefix in ("B1-", "B2-", "B5-", "A1-", "A2-"):
        if label.startswith(prefix):
            label = label[len(prefix) :]
    return label.replace("-base-off", "").replace("-off", "")


def log_y(value: float, min_value: float, max_value: float, top: float, height: float) -> float:
    safe_value = max(value, min_value)
    if max_value <= min_value:
        return top + height
    ratio = (math.log10(safe_value) - math.log10(min_value)) / (math.log10(max_value) - math.log10(min_value))
    return top + height - ratio * height


def build_svg(rows: list[dict[str, str]], result_dir: Path, limit: int) -> str:
    candidates: list[dict[str, str]] = []
    for row in rows:
        p999 = maybe_float(row.get("latency_ns_p999_of_runs"))
        median = maybe_float(row.get("latency_ns_median_of_medians"))
        p99 = maybe_float(row.get("latency_ns_p99_of_runs"))
        if p999 is None or median is None or p99 is None:
            continue
        if row.get("scenario_id", "").startswith("B5-"):
            continue
        candidates.append(row)

    candidates.sort(key=lambda row: maybe_float(row.get("latency_ns_p999_of_runs")) or 0.0, reverse=True)
    candidates = candidates[: max(1, limit)]
    candidates.reverse()

    width = 1200
    height = 720
    plot_left = 90
    plot_top = 96
    plot_width = 1030
    plot_height = 420
    baseline_y = plot_top + plot_height

    values = [
        maybe_float(row.get(field)) or 0.0
        for row in candidates
        for field in ("latency_ns_median_of_medians", "latency_ns_p99_of_runs", "latency_ns_p999_of_runs")
    ]
    positive_values = [value for value in values if value > 0.0]
    min_value = max(1.0, min(positive_values) if positive_values else 1.0)
    max_value = max(positive_values) if positive_values else 1.0
    max_value *= 1.25

    colours = {
        "p50": "#2563eb",
        "p99": "#d97706",
        "p999": "#dc2626",
    }
    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        '<rect width="100%" height="100%" fill="white"/>',
        '<style>text{font-family:Arial,sans-serif;fill:#111827} .muted{fill:#6b7280} .axis{stroke:#374151;stroke-width:1} .grid{stroke:#e5e7eb;stroke-width:1}</style>',
        '<text x="60" y="44" font-size="24" font-weight="700">BT tick tail latency</text>',
        f'<text x="60" y="70" font-size="13" class="muted">source: {xml_escape(str(result_dir))}</text>',
    ]

    tick_values = []
    exponent_min = math.floor(math.log10(min_value))
    exponent_max = math.ceil(math.log10(max_value))
    for exponent in range(exponent_min, exponent_max + 1):
        tick = 10.0**exponent
        if min_value <= tick <= max_value:
            tick_values.append(tick)
    if not tick_values:
        tick_values = [min_value, max_value]

    for tick in tick_values:
        y = log_y(tick, min_value, max_value, plot_top, plot_height)
        parts.append(f'<line x1="{plot_left}" x2="{plot_left + plot_width}" y1="{y:.1f}" y2="{y:.1f}" class="grid"/>')
        parts.append(f'<text x="{plot_left - 12}" y="{y + 4:.1f}" text-anchor="end" font-size="11" class="muted">{format_latency_ns(tick)}</text>')

    parts.append(f'<line x1="{plot_left}" x2="{plot_left}" y1="{plot_top}" y2="{baseline_y}" class="axis"/>')
    parts.append(f'<line x1="{plot_left}" x2="{plot_left + plot_width}" y1="{baseline_y}" y2="{baseline_y}" class="axis"/>')

    if candidates:
        slot = plot_width / len(candidates)
        bar_width = min(18.0, slot / 5.0)
        for index, row in enumerate(candidates):
            centre = plot_left + slot * (index + 0.5)
            metrics = (
                ("p50", maybe_float(row.get("latency_ns_median_of_medians")) or 0.0),
                ("p99", maybe_float(row.get("latency_ns_p99_of_runs")) or 0.0),
                ("p99.9", maybe_float(row.get("latency_ns_p999_of_runs")) or 0.0),
            )
            for offset, (name, value) in zip((-bar_width * 1.2, 0.0, bar_width * 1.2), metrics):
                y = log_y(value, min_value, max_value, plot_top, plot_height)
                bar_height = max(1.0, baseline_y - y)
                colour = colours["p999" if name == "p99.9" else name]
                parts.append(
                    f'<rect x="{centre + offset - bar_width / 2:.1f}" y="{y:.1f}" width="{bar_width:.1f}" height="{bar_height:.1f}" fill="{colour}"/>'
                )
            label = xml_escape(short_label(row.get("scenario_id", "")))
            parts.append(
                f'<text transform="translate({centre - 4:.1f},{baseline_y + 16}) rotate(55)" font-size="11" class="muted">{label}</text>'
            )
    else:
        parts.append('<text x="150" y="280" font-size="18" class="muted">No latency rows found.</text>')

    legend_x = 860
    for index, (label, colour) in enumerate((("p50", colours["p50"]), ("p99", colours["p99"]), ("p99.9", colours["p999"]))):
        x = legend_x + index * 88
        parts.append(f'<rect x="{x}" y="36" width="14" height="14" fill="{colour}"/>')
        parts.append(f'<text x="{x + 20}" y="48" font-size="13">{label}</text>')

    parts.append('<text x="60" y="675" font-size="12" class="muted">Y axis is logarithmic. B5 lifecycle rows are excluded because they are operations, not steady-state ticks.</text>')
    parts.append("</svg>")
    return "\n".join(parts) + "\n"


def main() -> int:
    args = parse_args()
    result_dir = select_result_dir(args.result_dir)
    rows = read_csv_rows(result_dir / "aggregate_summary.csv")
    output = Path(args.output).expanduser().resolve() if args.output else result_dir / "tail_latency.svg"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(build_svg(rows, result_dir, args.limit), encoding="utf-8")
    print(f"wrote tail-latency figure to {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
