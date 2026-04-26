#!/usr/bin/env python3

from __future__ import annotations

import argparse
import csv
import sys
from pathlib import Path
from typing import Iterable


BENCH_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_RESULTS_ROOT = BENCH_ROOT / "results"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Summarise muesli-bt benchmark CSV outputs into a short optimisation-oriented report."
    )
    parser.add_argument(
        "result_dir",
        nargs="?",
        help="benchmark result directory; defaults to the latest directory under bench/results",
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
    if argument:
        result_dir = Path(argument).expanduser().resolve()
    else:
        result_dir = latest_result_dir(DEFAULT_RESULTS_ROOT)

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


def maybe_int(value: str | None) -> int | None:
    if value in (None, ""):
        return None
    return int(value)


def format_latency_ns(value: float | None) -> str:
    if value is None:
        return "n/a"
    if value >= 1_000_000.0:
        return f"{value / 1_000_000.0:.2f} ms"
    if value >= 1_000.0:
        return f"{value / 1_000.0:.2f} us"
    return f"{value:.0f} ns"


def format_ticks_per_second(value: float | None) -> str:
    if value is None:
        return "n/a"
    magnitude = abs(value)
    if magnitude >= 1_000_000_000.0:
        return f"{value / 1_000_000_000.0:.2f} Gticks/s"
    if magnitude >= 1_000_000.0:
        return f"{value / 1_000_000.0:.2f} Mticks/s"
    if magnitude >= 1_000.0:
        return f"{value / 1_000.0:.2f} kticks/s"
    return f"{value:.2f} ticks/s"


def format_ratio(value: float | None) -> str:
    if value is None:
        return "n/a"
    return f"{value:.2f}x"


def format_rate(value: float | None) -> str:
    if value is None:
        return "n/a"
    return f"{value * 100.0:.2f}%"


def short_commit(commit: str) -> str:
    return commit[:10] if commit else "unknown"


def scenario_metric(row: dict[str, str] | None, field: str) -> float | None:
    if row is None:
        return None
    return maybe_float(row.get(field))


def b2_allocations_per_tick(row: dict[str, str]) -> float | None:
    allocations = maybe_float(row.get("alloc_count_total_median"))
    ticks_per_second = maybe_float(row.get("ticks_per_second_median"))
    run_seconds = maybe_float(row.get("run_seconds"))
    if allocations is None or ticks_per_second in (None, 0.0) or run_seconds in (None, 0.0):
        return None
    return allocations / (ticks_per_second * run_seconds)


def b1_slope(rows_by_id: dict[str, dict[str, str]], family: str) -> tuple[float, float, float] | None:
    small = rows_by_id.get(f"B1-{family}-31-base-off")
    large = rows_by_id.get(f"B1-{family}-255-base-off")
    if small is None or large is None:
        return None
    small_latency = scenario_metric(small, "latency_ns_median_of_medians")
    large_latency = scenario_metric(large, "latency_ns_median_of_medians")
    if small_latency is None or large_latency is None:
        return None
    slope = (large_latency - small_latency) / (255.0 - 31.0)
    return small_latency, large_latency, slope


def b5_phase_rows(
    rows_by_id: dict[str, dict[str, str]], variant: str
) -> list[tuple[int, dict[str, str]]]:
    rows: list[tuple[int, dict[str, str]]] = []
    for size in (31, 255, 1023):
        row = rows_by_id.get(f"B5-alt-{size}-{variant}-off")
        if row is not None:
            rows.append((size, row))
    return rows


def available_rows(rows: Iterable[dict[str, str]], prefix: str) -> list[dict[str, str]]:
    return [row for row in rows if row.get("scenario_id", "").startswith(prefix)]


def format_machine_summary(environment: dict[str, str]) -> str:
    return (
        f"{environment.get('cpu_model', 'unknown cpu')} | "
        f"{environment.get('os_name', 'unknown os')} {environment.get('kernel_version', '').strip()} | "
        f"{environment.get('physical_cores', '?')} physical / "
        f"{environment.get('logical_cores', '?')} logical cores"
    )


def print_report(result_dir: Path) -> int:
    aggregate_rows = read_csv_rows(result_dir / "aggregate_summary.csv")
    environment_rows = read_csv_rows(result_dir / "environment_metadata.csv")
    if not aggregate_rows:
        raise ValueError(f"aggregate_summary.csv is empty in {result_dir}")
    if not environment_rows:
        raise ValueError(f"environment_metadata.csv is empty in {result_dir}")

    rows_by_id = {row["scenario_id"]: row for row in aggregate_rows}
    environment = environment_rows[0]

    semantic_error_runs = sum(maybe_int(row.get("semantic_error_runs")) or 0 for row in aggregate_rows)
    semantic_error_scenarios = sum(1 for row in aggregate_rows if (maybe_int(row.get("semantic_error_runs")) or 0) > 0)

    print(f"result set: {result_dir}")
    print(
        "runtime: "
        f"{environment.get('runtime_name', 'unknown')} "
        f"{environment.get('runtime_version', '').strip()} "
        f"({short_commit(environment.get('runtime_commit', ''))})"
    )
    print(f"machine: {format_machine_summary(environment)}")
    print(
        "build: "
        f"{environment.get('compiler_name', 'unknown')} "
        f"{environment.get('compiler_version', '').strip()} | "
        f"{environment.get('build_type', 'unknown')} | "
        f"{environment.get('build_flags', '').strip()}"
    )
    print()

    print("correctness")
    if semantic_error_runs == 0:
        print("- no semantic-error runs")
    else:
        print(
            f"- semantic-error runs: {semantic_error_runs} across "
            f"{semantic_error_scenarios} affected scenario(s)"
        )
    print()

    print("baseline and scaling")
    a1_off = rows_by_id.get("A1-single-leaf-off")
    if a1_off:
        print(
            "- single-leaf baseline: "
            f"{format_latency_ns(scenario_metric(a1_off, 'latency_ns_median_of_medians'))} median, "
            f"{format_ticks_per_second(scenario_metric(a1_off, 'ticks_per_second_median'))}"
        )
    for family, label in (("seq", "sequence"), ("sel", "selector"), ("alt", "alternating")):
        slope_info = b1_slope(rows_by_id, family)
        if slope_info:
            small_latency, large_latency, slope = slope_info
            print(
                f"- {label} traversal: {format_latency_ns(small_latency)} at 31 nodes, "
                f"{format_latency_ns(large_latency)} at 255 nodes, {slope:.2f} ns/node"
            )
    a2 = rows_by_id.get("A2-alt-255-jitter-off")
    if a2:
        print(
            "- tick jitter: "
            f"{format_latency_ns(scenario_metric(a2, 'latency_ns_median_of_medians'))} median, "
            f"{format_latency_ns(scenario_metric(a2, 'latency_ns_p99_of_runs'))} p99, "
            f"{format_latency_ns(scenario_metric(a2, 'latency_ns_p999_of_runs'))} p99.9, "
            f"ratio {format_ratio(scenario_metric(a2, 'jitter_ratio_p99_over_median_of_runs'))}"
        )
    print()

    print("compile and instantiation")
    b5_phase_labels = (
        ("parse", "parse DSL"),
        ("compile", "compile BT"),
        ("inst1", "instantiate 1"),
        ("inst100", "instantiate 100"),
        ("loadbin", "load binary"),
        ("loaddsl", "load DSL"),
    )
    printed_b5 = False
    for variant, label in b5_phase_labels:
        phase_rows = b5_phase_rows(rows_by_id, variant)
        if not phase_rows:
            continue
        printed_b5 = True
        if variant == "inst100":
            total_parts = []
            per_instance_parts = []
            for size, row in phase_rows:
                total_latency = scenario_metric(row, "latency_ns_median_of_medians")
                if total_latency is None:
                    continue
                total_parts.append(f"{size} nodes {format_latency_ns(total_latency)}")
                per_instance_parts.append(f"{size} nodes {format_latency_ns(total_latency / 100.0)}")
            if total_parts:
                print(
                    f"- {label}: total {', '.join(total_parts)}; "
                    f"per instance {', '.join(per_instance_parts)}"
                )
            continue

        parts = []
        for size, row in phase_rows:
            latency = scenario_metric(row, "latency_ns_median_of_medians")
            if latency is None:
                continue
            parts.append(f"{size} nodes {format_latency_ns(latency)}")
        if parts:
            print(f"- {label}: {', '.join(parts)}")
    if not printed_b5:
        print("- no B5 scenarios present in this result set")
    print()

    print("reactive interruption")
    b2_rows = available_rows(aggregate_rows, "B2-")
    b2_rows_with_allocs = [row for row in b2_rows if b2_allocations_per_tick(row) is not None]
    worst_alloc_row = max(b2_rows_with_allocs, key=b2_allocations_per_tick) if b2_rows_with_allocs else None
    b2_rows_with_interrupt = [
        row for row in b2_rows if scenario_metric(row, "interrupt_latency_ns_median_of_medians") is not None
    ]
    worst_interrupt_row = (
        max(
            b2_rows_with_interrupt,
            key=lambda row: scenario_metric(row, "interrupt_latency_ns_median_of_medians") or 0.0,
        )
        if b2_rows_with_interrupt
        else None
    )
    if worst_interrupt_row:
        print(
            "- slowest interruption case: "
            f"{worst_interrupt_row['scenario_id']} at "
            f"{format_latency_ns(scenario_metric(worst_interrupt_row, 'interrupt_latency_ns_median_of_medians'))} "
            "median interruption latency"
        )
    if worst_alloc_row:
        print(
            "- worst allocation pressure: "
            f"{worst_alloc_row['scenario_id']} at {b2_allocations_per_tick(worst_alloc_row):.2f} alloc/tick"
        )
    if not worst_interrupt_row and not worst_alloc_row:
        print("- no B2 scenarios present in this result set")
    print()

    print("logging overhead")
    slowdown_pairs = (
        ("single-leaf full trace", "A1-single-leaf-off", "A1-single-leaf-log"),
        ("static full trace", "B1-alt-31-base-off", "B6-alt-31-base-fulltrace"),
        ("reactive full trace", "B2-reactive-31-flip20-off", "B6-reactive-31-flip20-fulltrace"),
    )
    printed_slowdown = False
    for label, base_id, traced_id in slowdown_pairs:
        base_row = rows_by_id.get(base_id)
        traced_row = rows_by_id.get(traced_id)
        base_latency = scenario_metric(base_row, "latency_ns_median_of_medians")
        traced_latency = scenario_metric(traced_row, "latency_ns_median_of_medians")
        if base_latency in (None, 0.0) or traced_latency is None:
            continue
        printed_slowdown = True
        print(f"- {label}: {format_ratio(traced_latency / base_latency)} slowdown")
    if not printed_slowdown:
        print("- no B6 scenarios present in this result set")
    print()

    print("memory and GC")
    b7_rows = available_rows(aggregate_rows, "B7-")
    if b7_rows:
        for row in b7_rows:
            print(
                f"- {row['scenario_id']}: "
                f"{scenario_metric(row, 'gc_collections_total_median') or 0:.0f} collections, "
                f"GC p99 {format_latency_ns(scenario_metric(row, 'gc_pause_ns_p99_of_runs'))}, "
                f"heap slope {scenario_metric(row, 'heap_live_bytes_slope_per_tick_median') or 0.0:.2f} B/tick, "
                f"event log {scenario_metric(row, 'event_log_bytes_per_tick_median') or 0.0:.2f} B/tick"
            )
    else:
        print("- no B7 scenarios present in this result set")
    print()

    print("async contract edges")
    b8_rows = available_rows(aggregate_rows, "B8-")
    if b8_rows:
        for row in b8_rows:
            print(
                f"- {row['scenario_id']}: "
                f"{format_latency_ns(scenario_metric(row, 'latency_ns_median_of_medians'))} median operation, "
                f"cancel median {format_latency_ns(scenario_metric(row, 'cancel_latency_ns_median_of_medians'))}, "
                f"deadline misses {scenario_metric(row, 'deadline_miss_count_median') or 0:.0f} "
                f"({format_rate(scenario_metric(row, 'deadline_miss_rate_median'))}), "
                f"fallbacks {scenario_metric(row, 'fallback_activation_count_median') or 0:.0f} "
                f"({format_rate(scenario_metric(row, 'fallback_activation_rate_median'))}), "
                f"dropped completions {scenario_metric(row, 'dropped_completion_count_median') or 0:.0f} "
                f"({format_rate(scenario_metric(row, 'dropped_completion_rate_median'))})"
            )
    else:
        print("- no B8 scenarios present in this result set")
    print()

    recommendations: list[str] = []
    if semantic_error_runs > 0:
        recommendations.append("Fix semantic mismatches before using these numbers for optimisation work.")

    jitter_ratio = scenario_metric(a2, "jitter_ratio_p99_over_median_of_runs") if a2 else None
    if jitter_ratio is not None and jitter_ratio >= 5.0:
        recommendations.append(
            f"Tick jitter is unstable at {jitter_ratio:.2f}x p99/median; inspect allocation, logging, and scheduling noise."
        )
    elif jitter_ratio is not None:
        recommendations.append("A1, B1, and A2 look healthy; keep them as regression guards while changing hotter paths.")

    if worst_alloc_row and (b2_allocations_per_tick(worst_alloc_row) or 0.0) > 0.1:
        recommendations.append(
            "Reactive interruption is the first optimisation target: "
            f"{worst_alloc_row['scenario_id']} is allocating {b2_allocations_per_tick(worst_alloc_row):.2f} times per tick."
        )

    slowdowns = []
    for _, base_id, traced_id in slowdown_pairs:
        base_row = rows_by_id.get(base_id)
        traced_row = rows_by_id.get(traced_id)
        base_latency = scenario_metric(base_row, "latency_ns_median_of_medians")
        traced_latency = scenario_metric(traced_row, "latency_ns_median_of_medians")
        if base_latency not in (None, 0.0) and traced_latency is not None:
            slowdowns.append(traced_latency / base_latency)
    if slowdowns and max(slowdowns) >= 10.0:
        recommendations.append(
            "Full-trace logging is expensive in this build; optimise or buffer that path before treating trace-on runs as representative."
        )
    if not b7_rows:
        recommendations.append("Run B7 when memory/GC pause or heap-live evidence is needed.")
    if not b8_rows:
        recommendations.append("Run B8 when async cancellation contract edge evidence is needed.")

    print("recommended next work")
    if recommendations:
        for item in recommendations:
            print(f"- {item}")
    else:
        print("- No obvious optimisation target stood out from this result set.")

    return 0


def main() -> int:
    args = parse_args()
    result_dir = select_result_dir(args.result_dir)
    return print_report(result_dir)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (FileNotFoundError, ValueError) as error:
        print(f"analyse_results.py: {error}", file=sys.stderr)
        raise SystemExit(1)
