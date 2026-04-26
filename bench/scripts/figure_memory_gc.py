#!/usr/bin/env python3

from __future__ import annotations

import argparse
import csv
import json
import statistics
from pathlib import Path


BENCH_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_RESULTS_ROOT = BENCH_ROOT / "results"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate an SVG memory/GC figure from benchmark CSV output and optional mbt.evt.v1 logs."
    )
    parser.add_argument(
        "result_dir",
        nargs="?",
        help="benchmark result directory; defaults to the latest directory under bench/results",
    )
    parser.add_argument(
        "--event-log",
        action="append",
        default=[],
        help="canonical JSONL event log or directory containing events.jsonl; may be repeated",
    )
    parser.add_argument(
        "--output",
        "-o",
        help="output SVG path; defaults to <result-dir>/memory_gc.svg",
    )
    parser.add_argument(
        "--limit",
        type=int,
        default=10,
        help="maximum number of allocation-pressure scenarios to show",
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


def format_bytes(value: float | None) -> str:
    if value is None:
        return "n/a"
    units = ("B", "KiB", "MiB", "GiB")
    scaled = float(value)
    for unit in units:
        if abs(scaled) < 1024.0 or unit == units[-1]:
            return f"{scaled:.2f} {unit}" if unit != "B" else f"{scaled:.0f} {unit}"
        scaled /= 1024.0
    return f"{value:.0f} B"


def format_latency_ns(value: float | None) -> str:
    if value is None:
        return "n/a"
    if value >= 1_000_000.0:
        return f"{value / 1_000_000.0:.2f} ms"
    if value >= 1_000.0:
        return f"{value / 1_000.0:.2f} us"
    return f"{value:.0f} ns"


def short_label(scenario_id: str) -> str:
    label = scenario_id
    for prefix in ("B1-", "B2-", "B5-", "B6-", "A1-", "A2-"):
        if label.startswith(prefix):
            label = label[len(prefix) :]
    return label.replace("-base-off", "").replace("-off", "")


def resolve_event_log_paths(raw_paths: list[str], result_dir: Path) -> list[Path]:
    paths: list[Path] = []
    for raw in raw_paths:
        path = Path(raw).expanduser().resolve()
        if path.is_dir():
            candidate = path / "events.jsonl"
            if candidate.exists():
                paths.append(candidate)
        else:
            paths.append(path)
    for candidate in sorted(result_dir.glob("**/events.jsonl")):
        if candidate not in paths:
            paths.append(candidate)
    return paths


def gc_end_payloads(paths: list[Path]) -> list[dict]:
    payloads: list[dict] = []
    for path in paths:
        if not path.exists():
            raise FileNotFoundError(f"event log does not exist: {path}")
        with path.open(encoding="utf-8") as handle:
            for line_number, line in enumerate(handle, start=1):
                line = line.strip()
                if not line:
                    continue
                try:
                    record = json.loads(line)
                except json.JSONDecodeError as exc:
                    raise ValueError(f"{path}:{line_number}: invalid JSONL: {exc}") from exc
                if record.get("type") != "gc_end":
                    continue
                data = record.get("data")
                if isinstance(data, dict):
                    payloads.append(data)
    return payloads


def percentile(values: list[float], q: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    index = min(len(ordered) - 1, max(0, int(round((len(ordered) - 1) * q))))
    return ordered[index]


def build_svg(rows: list[dict[str, str]], gc_payloads: list[dict], event_log_count: int, result_dir: Path, limit: int) -> str:
    pressure_rows = []
    for row in rows:
        alloc_bytes = maybe_float(row.get("alloc_bytes_total_median")) or 0.0
        alloc_count = maybe_float(row.get("alloc_count_total_median")) or 0.0
        rss = maybe_float(row.get("rss_bytes_peak_max")) or 0.0
        if alloc_bytes > 0.0 or alloc_count > 0.0 or rss > 0.0:
            pressure_rows.append((alloc_bytes, alloc_count, rss, row))
    pressure_rows.sort(key=lambda item: item[0], reverse=True)
    pressure_rows = pressure_rows[: max(1, limit)]
    pressure_rows.reverse()

    pauses = [float(payload.get("pause_time_ns", 0.0)) for payload in gc_payloads if payload.get("pause_time_ns") is not None]
    freed_total = sum(int(payload.get("freed_objects", 0) or 0) for payload in gc_payloads)
    in_tick_count = sum(1 for payload in gc_payloads if bool(payload.get("in_tick")))
    heap_after = [
        float(payload["heap_live_bytes_after"])
        for payload in gc_payloads
        if payload.get("heap_live_bytes_after") is not None
    ]

    width = 1200
    height = 740
    left = 90
    top = 110
    plot_width = 620
    plot_height = 390
    baseline_y = top + plot_height
    max_alloc = max((item[0] for item in pressure_rows), default=1.0)
    max_alloc = max(max_alloc, 1.0)

    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        '<rect width="100%" height="100%" fill="white"/>',
        '<style>text{font-family:Arial,sans-serif;fill:#111827} .muted{fill:#6b7280} .axis{stroke:#374151;stroke-width:1} .grid{stroke:#e5e7eb;stroke-width:1}</style>',
        '<text x="60" y="44" font-size="24" font-weight="700">Memory and GC evidence</text>',
        f'<text x="60" y="70" font-size="13" class="muted">source: {xml_escape(str(result_dir))}</text>',
        '<text x="90" y="96" font-size="15" font-weight="700">Allocation pressure by scenario</text>',
    ]

    for fraction in (0.0, 0.25, 0.5, 0.75, 1.0):
        y = baseline_y - fraction * plot_height
        value = max_alloc * fraction
        parts.append(f'<line x1="{left}" x2="{left + plot_width}" y1="{y:.1f}" y2="{y:.1f}" class="grid"/>')
        parts.append(f'<text x="{left - 12}" y="{y + 4:.1f}" text-anchor="end" font-size="11" class="muted">{format_bytes(value)}</text>')

    parts.append(f'<line x1="{left}" x2="{left}" y1="{top}" y2="{baseline_y}" class="axis"/>')
    parts.append(f'<line x1="{left}" x2="{left + plot_width}" y1="{baseline_y}" y2="{baseline_y}" class="axis"/>')

    if pressure_rows:
        slot = plot_width / len(pressure_rows)
        bar_width = min(34.0, slot * 0.58)
        for index, (alloc_bytes, alloc_count, _rss, row) in enumerate(pressure_rows):
            centre = left + slot * (index + 0.5)
            height_value = (alloc_bytes / max_alloc) * plot_height if max_alloc > 0 else 0.0
            y = baseline_y - height_value
            parts.append(f'<rect x="{centre - bar_width / 2:.1f}" y="{y:.1f}" width="{bar_width:.1f}" height="{max(1.0, height_value):.1f}" fill="#059669"/>')
            parts.append(f'<text x="{centre:.1f}" y="{y - 6:.1f}" text-anchor="middle" font-size="10" class="muted">{alloc_count:.0f} alloc</text>')
            label = xml_escape(short_label(row.get("scenario_id", "")))
            parts.append(f'<text transform="translate({centre - 4:.1f},{baseline_y + 16}) rotate(55)" font-size="11" class="muted">{label}</text>')
    else:
        parts.append('<text x="150" y="280" font-size="18" class="muted">No allocation rows found.</text>')

    panel_x = 760
    parts.append('<text x="760" y="96" font-size="15" font-weight="700">GC lifecycle summary</text>')
    parts.append(f'<rect x="{panel_x}" y="{top}" width="360" height="390" rx="4" fill="#f9fafb" stroke="#d1d5db"/>')

    summary_lines = [
        f"event logs scanned: {event_log_count}",
        f"gc_end events: {len(gc_payloads)}",
        f"in-tick GC events: {in_tick_count}",
        f"freed objects: {freed_total}",
        f"pause p50: {format_latency_ns(percentile(pauses, 0.50))}",
        f"pause p95: {format_latency_ns(percentile(pauses, 0.95))}",
        f"pause p99: {format_latency_ns(percentile(pauses, 0.99))}",
    ]
    if heap_after:
        summary_lines.append(f"heap live after last GC: {format_bytes(heap_after[-1])}")
        if len(heap_after) > 1:
            summary_lines.append(f"heap live delta: {format_bytes(heap_after[-1] - heap_after[0])}")
    else:
        summary_lines.append("heap live series: no gc_end data")
    if not gc_payloads:
        summary_lines.append("status: GC figure scaffold ready; run logs still needed")
    elif len(heap_after) < 2:
        summary_lines.append("status: heap-live slope needs longer runs")
    else:
        slope = statistics.fmean([heap_after[i + 1] - heap_after[i] for i in range(len(heap_after) - 1)])
        summary_lines.append(f"mean heap-live delta/event: {format_bytes(slope)}")

    for index, line in enumerate(summary_lines):
        css = "muted" if line.startswith("status:") else ""
        parts.append(f'<text x="{panel_x + 24}" y="{top + 36 + index * 28}" font-size="14" class="{css}">{xml_escape(line)}</text>')

    parts.append('<text x="60" y="692" font-size="12" class="muted">Allocation pressure uses aggregate_summary.csv. GC pause and heap-live fields come from canonical gc_end events when event logs are supplied or found under the result directory.</text>')
    parts.append("</svg>")
    return "\n".join(parts) + "\n"


def main() -> int:
    args = parse_args()
    result_dir = select_result_dir(args.result_dir)
    rows = read_csv_rows(result_dir / "aggregate_summary.csv")
    event_log_paths = resolve_event_log_paths(args.event_log, result_dir)
    payloads = gc_end_payloads(event_log_paths)
    output = Path(args.output).expanduser().resolve() if args.output else result_dir / "memory_gc.svg"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(build_svg(rows, payloads, len(event_log_paths), result_dir, args.limit), encoding="utf-8")
    print(f"wrote memory/GC figure to {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
