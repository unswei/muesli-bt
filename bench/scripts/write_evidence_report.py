#!/usr/bin/env python3

from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path


BENCH_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_RESULTS_ROOT = BENCH_ROOT / "results"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Write a Markdown evidence report for a benchmark result directory."
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
        help="output Markdown path; defaults to <result-dir>/evidence_report.md",
    )
    return parser.parse_args()


def read_csv_rows(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def latest_result_dir(root: Path) -> Path:
    candidates = [
        child
        for child in root.iterdir()
        if child.is_dir()
        and (child / "aggregate_summary.csv").exists()
        and (child / "environment_metadata.csv").exists()
    ]
    if not candidates:
        raise FileNotFoundError(f"no benchmark result directories found under {root}")
    return max(candidates, key=lambda path: path.stat().st_mtime)


def select_result_dir(argument: str | None) -> Path:
    result_dir = Path(argument).expanduser().resolve() if argument else latest_result_dir(DEFAULT_RESULTS_ROOT)
    if not result_dir.is_dir():
        raise FileNotFoundError(f"benchmark result directory does not exist: {result_dir}")
    for required_name in ("aggregate_summary.csv", "environment_metadata.csv"):
        required_path = result_dir / required_name
        if not required_path.exists():
            raise FileNotFoundError(f"missing required benchmark output: {required_path}")
    return result_dir


def maybe_float(value: str | None) -> float | None:
    if value in (None, ""):
        return None
    return float(value)


def format_latency_ns(value: float | None) -> str:
    if value is None:
        return "n/a"
    if value >= 1_000_000.0:
        return f"{value / 1_000_000.0:.2f} ms"
    if value >= 1_000.0:
        return f"{value / 1_000.0:.2f} us"
    return f"{value:.0f} ns"


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


def format_rate(value: float | None) -> str:
    if value is None:
        return "n/a"
    return f"{value * 100.0:.2f}%"


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


def count_gc_events(paths: list[Path]) -> tuple[int, int, list[float], list[float]]:
    gc_begin = 0
    gc_end = 0
    pauses: list[float] = []
    heap_live_after: list[float] = []
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
                if record.get("type") == "gc_begin":
                    gc_begin += 1
                elif record.get("type") == "gc_end":
                    gc_end += 1
                    data = record.get("data")
                    if isinstance(data, dict):
                        if data.get("pause_time_ns") is not None:
                            pauses.append(float(data["pause_time_ns"]))
                        if data.get("heap_live_bytes_after") is not None:
                            heap_live_after.append(float(data["heap_live_bytes_after"]))
    return gc_begin, gc_end, pauses, heap_live_after


def percentile(values: list[float], q: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    index = min(len(ordered) - 1, max(0, int(round((len(ordered) - 1) * q))))
    return ordered[index]


def write_report(result_dir: Path, event_log_paths: list[Path], output: Path) -> None:
    aggregate_rows = read_csv_rows(result_dir / "aggregate_summary.csv")
    environment_rows = read_csv_rows(result_dir / "environment_metadata.csv")
    if not aggregate_rows:
        raise ValueError(f"aggregate_summary.csv is empty in {result_dir}")
    if not environment_rows:
        raise ValueError(f"environment_metadata.csv is empty in {result_dir}")

    environment = environment_rows[0]
    rows_by_id = {row.get("scenario_id", ""): row for row in aggregate_rows}
    has_tick_latency_rows = any(
        not row.get("scenario_id", "").startswith("B7-") and row.get("latency_ns_p999_of_runs")
        for row in aggregate_rows
    )
    a2 = rows_by_id.get("A2-alt-255-jitter-off")
    strict_like_rows = [
        row
        for row in aggregate_rows
        if row.get("scenario_id", "").startswith("B1-")
        and maybe_float(row.get("alloc_count_total_median")) == 0.0
        and maybe_float(row.get("alloc_bytes_total_median")) == 0.0
    ]
    max_alloc_row = max(
        aggregate_rows,
        key=lambda row: maybe_float(row.get("alloc_bytes_total_median")) or 0.0,
    )
    gc_begin, gc_end, pauses, heap_live_after = count_gc_events(event_log_paths)

    tail_figure = result_dir / "tail_latency.svg"
    memory_figure = result_dir / "memory_gc.svg"
    semantic_error_runs = sum(int(row.get("semantic_error_runs") or 0) for row in aggregate_rows)

    lines = [
        "# benchmark evidence report",
        "",
        "## what this is",
        "",
        "This report summarises one benchmark result directory and records which v0.7 evidence items are present.",
        "",
        "## source",
        "",
        f"- result directory: `{result_dir}`",
        f"- runtime: `{environment.get('runtime_name', 'unknown')} {environment.get('runtime_version', '').strip()}`",
        f"- commit: `{environment.get('runtime_commit', 'unknown')}`",
        f"- build type: `{environment.get('build_type', 'unknown')}`",
        f"- compiler: `{environment.get('compiler_name', 'unknown')} {environment.get('compiler_version', '').strip()}`",
        f"- event logs scanned: `{len(event_log_paths)}`",
        "",
        "## figures",
        "",
        f"- tail latency: `{'present' if tail_figure.exists() else ('missing' if has_tick_latency_rows else 'not applicable')}` at `{tail_figure.name}`",
        f"- memory/GC: `{'present' if memory_figure.exists() else 'missing'}` at `{memory_figure.name}`",
        "",
        "## checks",
        "",
        f"- semantic-error runs: `{semantic_error_runs}`",
        f"- zero-allocation B1 aggregate rows: `{len(strict_like_rows)}`",
        f"- highest allocation row: `{max_alloc_row.get('scenario_id', 'unknown')}` at `{format_bytes(maybe_float(max_alloc_row.get('alloc_bytes_total_median')))}`",
    ]
    if a2 is not None:
        lines.append(
            f"- A2 p99.9 latency: `{format_latency_ns(maybe_float(a2.get('latency_ns_p999_of_runs')))}`"
        )
    else:
        lines.append("- A2 p99.9 latency: `missing`")
    lines.extend(
        [
            f"- GC begin events: `{gc_begin}`",
            f"- GC end events: `{gc_end}`",
            f"- GC pause p99: `{format_latency_ns(percentile(pauses, 0.99))}`",
        ]
    )
    if len(heap_live_after) >= 2:
        lines.append(f"- heap-live delta: `{format_bytes(heap_live_after[-1] - heap_live_after[0])}`")
    else:
        lines.append("- heap-live delta: `pending longer GC-producing run`")

    b8_rows = [row for row in aggregate_rows if row.get("scenario_id", "").startswith("B8-")]
    lines.extend(
        [
            "",
            "## async and fallback metrics",
            "",
        ]
    )
    if b8_rows:
        lines.append("| scenario | deadline misses | deadline rate | fallback count | fallback rate | dropped completions | dropped rate |")
        lines.append("| --- | ---: | ---: | ---: | ---: | ---: | ---: |")
        for row in b8_rows:
            lines.append(
                "| "
                f"`{row.get('scenario_id', 'unknown')}` | "
                f"{maybe_float(row.get('deadline_miss_count_median')) or 0:.0f} | "
                f"{format_rate(maybe_float(row.get('deadline_miss_rate_median')))} | "
                f"{maybe_float(row.get('fallback_activation_count_median')) or 0:.0f} | "
                f"{format_rate(maybe_float(row.get('fallback_activation_rate_median')))} | "
                f"{maybe_float(row.get('dropped_completion_count_median')) or 0:.0f} | "
                f"{format_rate(maybe_float(row.get('dropped_completion_rate_median')))} |"
            )
    else:
        lines.append("- B8 async contract rows: `missing`")

    lines.extend(
        [
            "",
            "## gaps",
            "",
        ]
    )
    gaps = []
    if has_tick_latency_rows and not tail_figure.exists():
        gaps.append("Generate `tail_latency.svg` with `bench/scripts/figure_tail_latency.py`.")
    if not memory_figure.exists():
        gaps.append("Generate `memory_gc.svg` with `bench/scripts/figure_memory_gc.py`.")
    if gc_end == 0:
        gaps.append("Run or attach a canonical event log with `gc_end` events for GC pause evidence.")
    if len(heap_live_after) < 2:
        gaps.append("Run a longer GC-producing benchmark to estimate heap-live slope.")
    if not gaps:
        gaps.append("No immediate evidence gaps detected by this report.")
    lines.extend(f"- {gap}" for gap in gaps)
    lines.append("")

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    args = parse_args()
    result_dir = select_result_dir(args.result_dir)
    output = Path(args.output).expanduser().resolve() if args.output else result_dir / "evidence_report.md"
    event_log_paths = resolve_event_log_paths(args.event_log, result_dir)
    write_report(result_dir, event_log_paths, output)
    print(f"wrote evidence report to {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
