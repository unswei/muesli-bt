#!/usr/bin/env python3

from __future__ import annotations

import argparse
import csv
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Iterable


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Compare two benchmark result directories on the shared runtime subset."
    )
    parser.add_argument("left_result_dir", help="first benchmark result directory")
    parser.add_argument("right_result_dir", help="second benchmark result directory")
    return parser.parse_args()


def read_csv_rows(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def select_result_dir(argument: str) -> Path:
    result_dir = Path(argument).expanduser().resolve()
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


def short_commit(commit: str) -> str:
    return commit[:10] if commit else "unknown"


def scenario_metric(row: dict[str, str] | None, field: str) -> float | None:
    if row is None:
        return None
    return maybe_float(row.get(field))


def format_machine_summary(environment: dict[str, str]) -> str:
    return (
        f"{environment.get('cpu_model', 'unknown cpu')} | "
        f"{environment.get('os_name', 'unknown os')} {environment.get('kernel_version', '').strip()} | "
        f"{environment.get('physical_cores', '?')} physical / "
        f"{environment.get('logical_cores', '?')} logical cores"
    )


def b2_allocations_per_tick(row: dict[str, str]) -> float | None:
    allocations = maybe_float(row.get("alloc_count_total_median"))
    ticks_per_second = maybe_float(row.get("ticks_per_second_median"))
    run_seconds = maybe_float(row.get("run_seconds"))
    if allocations is None or ticks_per_second in (None, 0.0) or run_seconds in (None, 0.0):
        return None
    return allocations / (ticks_per_second * run_seconds)


def available_rows(rows: Iterable[dict[str, str]], prefix: str) -> list[dict[str, str]]:
    return [row for row in rows if row.get("scenario_id", "").startswith(prefix)]


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


def b5_phase_rows(rows_by_id: dict[str, dict[str, str]], variant: str) -> list[tuple[int, dict[str, str]]]:
    rows: list[tuple[int, dict[str, str]]] = []
    for size in (31, 255, 1023):
        row = rows_by_id.get(f"B5-alt-{size}-{variant}-off")
        if row is not None:
            rows.append((size, row))
    return rows


def worst_interrupt_row(rows: Iterable[dict[str, str]]) -> dict[str, str] | None:
    rows_with_interrupt = [
        row for row in rows if scenario_metric(row, "interrupt_latency_ns_median_of_medians") is not None
    ]
    if not rows_with_interrupt:
        return None
    return max(
        rows_with_interrupt,
        key=lambda row: scenario_metric(row, "interrupt_latency_ns_median_of_medians") or 0.0,
    )


def worst_allocation_row(rows: Iterable[dict[str, str]]) -> dict[str, str] | None:
    rows_with_allocs = [row for row in rows if b2_allocations_per_tick(row) is not None]
    if not rows_with_allocs:
        return None
    return max(rows_with_allocs, key=lambda row: b2_allocations_per_tick(row) or 0.0)


def faster_runtime_label(
    left_label: str,
    left_value: float,
    right_label: str,
    right_value: float,
) -> tuple[str, float]:
    if left_value <= right_value:
        return left_label, right_value / left_value if left_value > 0.0 else float("inf")
    return right_label, left_value / right_value if right_value > 0.0 else float("inf")


def format_lower_better_comparison(
    label: str,
    left_label: str,
    left_value: float | None,
    right_label: str,
    right_value: float | None,
    formatter: Callable[[float | None], str],
    noun: str,
) -> str | None:
    if left_value is None or right_value is None:
        return None
    if left_value == right_value:
        return f"- {label}: tied at {formatter(left_value)}"
    winner_label, ratio = faster_runtime_label(left_label, left_value, right_label, right_value)
    return (
        f"- {label}: {left_label} {formatter(left_value)} vs {right_label} {formatter(right_value)}; "
        f"{winner_label} {format_ratio(ratio)} lower {noun}"
    )


def format_allocation_comparison(
    left_label: str,
    left_value: float | None,
    right_label: str,
    right_value: float | None,
) -> str | None:
    if left_value is None or right_value is None:
        return None
    left_display = round(left_value, 2)
    right_display = round(right_value, 2)
    if left_display == right_display:
        return f"- reactive interruption allocations: both worst cases at {left_display:.2f} alloc/tick"
    if left_display == 0.0:
        return (
            f"- reactive interruption allocations: {left_label} 0.00 alloc/tick worst case vs "
            f"{right_label} {right_display:.2f} alloc/tick worst case; "
            f"{left_label} reached zero timed allocations"
        )
    if right_display == 0.0:
        return (
            f"- reactive interruption allocations: {left_label} {left_display:.2f} alloc/tick worst case vs "
            f"{right_label} 0.00 alloc/tick worst case; "
            f"{right_label} reached zero timed allocations"
        )
    winner_label, ratio = faster_runtime_label(left_label, left_display, right_label, right_display)
    return (
        f"- reactive interruption allocations: {left_label} {left_display:.2f} alloc/tick worst case vs "
        f"{right_label} {right_display:.2f} alloc/tick worst case; "
        f"{winner_label} {format_ratio(ratio)} lower allocation pressure"
    )


@dataclass
class ResultSet:
    result_dir: Path
    aggregate_rows: list[dict[str, str]]
    environment: dict[str, str]
    rows_by_id: dict[str, dict[str, str]]

    @property
    def runtime_label(self) -> str:
        return self.environment.get("runtime_name", "unknown")


def load_result_set(result_dir: Path) -> ResultSet:
    aggregate_rows = read_csv_rows(result_dir / "aggregate_summary.csv")
    environment_rows = read_csv_rows(result_dir / "environment_metadata.csv")
    if not aggregate_rows:
        raise ValueError(f"aggregate_summary.csv is empty in {result_dir}")
    if not environment_rows:
        raise ValueError(f"environment_metadata.csv is empty in {result_dir}")
    return ResultSet(
        result_dir=result_dir,
        aggregate_rows=aggregate_rows,
        environment=environment_rows[0],
        rows_by_id={row["scenario_id"]: row for row in aggregate_rows},
    )


def compare_environment(left: ResultSet, right: ResultSet) -> list[str]:
    fields = (
        ("schema_version", "schema version"),
        ("machine_id", "machine id"),
        ("cpu_model", "CPU model"),
        ("os_name", "OS name"),
        ("kernel_version", "kernel version"),
        ("compiler_name", "compiler"),
        ("compiler_version", "compiler version"),
        ("build_type", "build type"),
        ("build_flags", "build flags"),
        ("clock_source", "clock source"),
    )

    differences = []
    for field, label in fields:
        left_value = left.environment.get(field, "").strip()
        right_value = right.environment.get(field, "").strip()
        if left_value != right_value:
            differences.append(f"{label}: {left_value or 'n/a'} vs {right_value or 'n/a'}")
    return differences


def print_b5_phase_comparison(
    left: ResultSet,
    right: ResultSet,
    variant: str,
    label: str,
    *,
    divisor: float = 1.0,
    suffix: str = "faster",
) -> None:
    left_rows = dict(b5_phase_rows(left.rows_by_id, variant))
    right_rows = dict(b5_phase_rows(right.rows_by_id, variant))
    parts = []
    for size in (31, 255, 1023):
        left_row = left_rows.get(size)
        right_row = right_rows.get(size)
        if left_row is None or right_row is None:
            continue
        left_latency = scenario_metric(left_row, "latency_ns_median_of_medians")
        right_latency = scenario_metric(right_row, "latency_ns_median_of_medians")
        if left_latency is None or right_latency is None:
            continue
        left_value = left_latency / divisor
        right_value = right_latency / divisor
        winner_label, ratio = faster_runtime_label(left.runtime_label, left_value, right.runtime_label, right_value)
        parts.append(
            f"{size} nodes {format_latency_ns(left_value)} vs {format_latency_ns(right_value)} "
            f"({winner_label} {format_ratio(ratio)} {suffix})"
        )
    if parts:
        print(f"- {label}: {'; '.join(parts)}")


def print_report(left: ResultSet, right: ResultSet) -> int:
    print("result sets")
    print(
        f"- {left.runtime_label}: {left.result_dir} "
        f"({left.environment.get('runtime_version', '').strip()} / {short_commit(left.environment.get('runtime_commit', ''))})"
    )
    print(
        f"- {right.runtime_label}: {right.result_dir} "
        f"({right.environment.get('runtime_version', '').strip()} / {short_commit(right.environment.get('runtime_commit', ''))})"
    )
    print()

    print("comparison hygiene")
    differences = compare_environment(left, right)
    if differences:
        print("- warning: execution conditions differ")
        for item in differences:
            print(f"- {item}")
    else:
        print(f"- execution conditions match: {format_machine_summary(left.environment)}")
        print(
            "- build settings match: "
            f"{left.environment.get('compiler_name', 'unknown')} "
            f"{left.environment.get('compiler_version', '').strip()} | "
            f"{left.environment.get('build_type', 'unknown')} | "
            f"{left.environment.get('build_flags', '').strip()}"
        )
    print()

    print("shared runtime comparison")
    left_a1 = left.rows_by_id.get("A1-single-leaf-off")
    right_a1 = right.rows_by_id.get("A1-single-leaf-off")
    left_a1_latency = scenario_metric(left_a1, "latency_ns_median_of_medians")
    right_a1_latency = scenario_metric(right_a1, "latency_ns_median_of_medians")
    left_a1_tps = scenario_metric(left_a1, "ticks_per_second_median")
    right_a1_tps = scenario_metric(right_a1, "ticks_per_second_median")
    if left_a1_latency is not None and right_a1_latency is not None:
        winner_label, ratio = faster_runtime_label(left.runtime_label, left_a1_latency, right.runtime_label, right_a1_latency)
        print(
            f"- single-leaf baseline: {left.runtime_label} {format_latency_ns(left_a1_latency)}, "
            f"{format_ticks_per_second(left_a1_tps)} vs {right.runtime_label} {format_latency_ns(right_a1_latency)}, "
            f"{format_ticks_per_second(right_a1_tps)}; {winner_label} {format_ratio(ratio)} lower latency"
        )

    for family, label in (("seq", "sequence"), ("sel", "selector"), ("alt", "alternating")):
        left_slope = b1_slope(left.rows_by_id, family)
        right_slope = b1_slope(right.rows_by_id, family)
        if left_slope is None or right_slope is None:
            continue
        _, left_large_latency, left_per_node = left_slope
        _, right_large_latency, right_per_node = right_slope
        winner_label, ratio = faster_runtime_label(
            left.runtime_label, left_per_node, right.runtime_label, right_per_node
        )
        print(
            f"- {label} traversal: {left.runtime_label} {format_latency_ns(left_large_latency)} at 255 nodes "
            f"and {left_per_node:.2f} ns/node vs {right.runtime_label} {format_latency_ns(right_large_latency)} "
            f"at 255 nodes and {right_per_node:.2f} ns/node; {winner_label} {format_ratio(ratio)} lower per-node cost"
        )

    left_a2 = left.rows_by_id.get("A2-alt-255-jitter-off")
    right_a2 = right.rows_by_id.get("A2-alt-255-jitter-off")
    left_a2_median = scenario_metric(left_a2, "latency_ns_median_of_medians")
    right_a2_median = scenario_metric(right_a2, "latency_ns_median_of_medians")
    if left_a2_median is not None and right_a2_median is not None:
        winner_label, ratio = faster_runtime_label(left.runtime_label, left_a2_median, right.runtime_label, right_a2_median)
        print(
            f"- tick jitter: {left.runtime_label} {format_latency_ns(left_a2_median)} median, "
            f"{format_latency_ns(scenario_metric(left_a2, 'latency_ns_p99_of_runs'))} p99, "
            f"ratio {format_ratio(scenario_metric(left_a2, 'jitter_ratio_p99_over_median_of_runs'))} vs "
            f"{right.runtime_label} {format_latency_ns(right_a2_median)} median, "
            f"{format_latency_ns(scenario_metric(right_a2, 'latency_ns_p99_of_runs'))} p99, "
            f"ratio {format_ratio(scenario_metric(right_a2, 'jitter_ratio_p99_over_median_of_runs'))}; "
            f"{winner_label} {format_ratio(ratio)} lower absolute tick latency"
        )

    left_b2_worst = worst_interrupt_row(available_rows(left.aggregate_rows, "B2-"))
    right_b2_worst = worst_interrupt_row(available_rows(right.aggregate_rows, "B2-"))
    if left_b2_worst is not None and right_b2_worst is not None:
        left_interrupt = scenario_metric(left_b2_worst, "interrupt_latency_ns_median_of_medians")
        right_interrupt = scenario_metric(right_b2_worst, "interrupt_latency_ns_median_of_medians")
        if left_interrupt is not None and right_interrupt is not None:
            winner_label, ratio = faster_runtime_label(
                left.runtime_label, left_interrupt, right.runtime_label, right_interrupt
            )
            print(
                f"- reactive interruption worst case: {left.runtime_label} {left_b2_worst['scenario_id']} "
                f"{format_latency_ns(left_interrupt)} vs {right.runtime_label} {right_b2_worst['scenario_id']} "
                f"{format_latency_ns(right_interrupt)}; {winner_label} {format_ratio(ratio)} lower interruption latency"
            )

    left_b2_alloc = worst_allocation_row(available_rows(left.aggregate_rows, "B2-"))
    right_b2_alloc = worst_allocation_row(available_rows(right.aggregate_rows, "B2-"))
    if left_b2_alloc is not None and right_b2_alloc is not None:
        line = format_allocation_comparison(
            left.runtime_label,
            b2_allocations_per_tick(left_b2_alloc),
            right.runtime_label,
            b2_allocations_per_tick(right_b2_alloc),
        )
        if line:
            print(line)
    print()

    print("compile and instantiation")
    print_b5_phase_comparison(left, right, "compile", "compile BT")
    print_b5_phase_comparison(left, right, "inst1", "instantiate 1")
    print_b5_phase_comparison(left, right, "inst100", "instantiate 100 total")
    print_b5_phase_comparison(left, right, "inst100", "instantiate 100 per instance", divisor=100.0)
    print_b5_phase_comparison(left, right, "loaddsl", "load DSL")
    print()

    print("publication notes")
    print("- shared subset only: A1, A2, B1, B2, and the comparable B5 phases")
    print("- logging overhead (B6) is intentionally excluded from the cross-runtime comparison")

    left_parse = bool(b5_phase_rows(left.rows_by_id, "parse"))
    right_parse = bool(b5_phase_rows(right.rows_by_id, "parse"))
    left_loadbin = bool(b5_phase_rows(left.rows_by_id, "loadbin"))
    right_loadbin = bool(b5_phase_rows(right.rows_by_id, "loadbin"))
    muesli_only_labels = []
    if left_parse != right_parse:
        muesli_only_labels.append("parse DSL")
    if left_loadbin != right_loadbin:
        muesli_only_labels.append("load binary")
    if muesli_only_labels:
        print(f"- muesli-only phases not compared: {', '.join(muesli_only_labels)}")

    return 0


def main() -> int:
    args = parse_args()
    left = load_result_set(select_result_dir(args.left_result_dir))
    right = load_result_set(select_result_dir(args.right_result_dir))
    return print_report(left, right)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (FileNotFoundError, ValueError) as error:
        print(f"compare_results.py: {error}", file=sys.stderr)
        raise SystemExit(1)
