#!/usr/bin/env python3

"""Run the real-clock controlled-authority timing lane and write artefacts."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import os
import platform
import random
import shutil
import statistics
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable


EXPERIMENT = Path(__file__).resolve().parent
ROOT = EXPERIMENT.parents[1]
DEFAULT_PROTOCOL = EXPERIMENT / "configs" / "protocol.v1.json"
RAW_SCHEMA = ROOT / "schemas" / "controlled_authority" / "v1" / "timing-raw-trial.schema.json"
MANIFEST_SCHEMA = (
    ROOT
    / "schemas"
    / "controlled_authority"
    / "v1"
    / "timing-campaign-manifest.schema.json"
)


def load_json(path: Path) -> dict[str, Any]:
    with path.open(encoding="utf-8") as handle:
        value = json.load(handle)
    if not isinstance(value, dict):
        raise ValueError(f"expected a JSON object in {path}")
    return value


def write_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as handle:
        json.dump(value, handle, indent=2, sort_keys=True)
        handle.write("\n")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def validate_against_schema(instance: Any, schema_path: Path) -> None:
    try:
        import jsonschema
    except ImportError:
        return
    jsonschema.Draft202012Validator(load_json(schema_path)).validate(instance)


def tsv_field(value: Any) -> str:
    text = str(value)
    if any(character in text for character in "\t\r\n"):
        raise ValueError("timing plan fields cannot contain tabs or newlines")
    return text


def write_csv(path: Path, rows: list[dict[str, Any]], fields: Iterable[str] | None = None) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fieldnames = list(fields) if fields else list(rows[0])
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)


def condition_catalogue(timing: dict[str, Any]) -> list[dict[str, Any]]:
    primary = timing["primary"]
    conditions = [
        {
            "condition_id": "primary",
            "axis": "primary",
            "reader_label": "primary operating point",
            "service_delay_ms": primary["service_delay_ms"],
            "tick_rate_hz": primary["tick_rate_hz"],
            "concurrent_jobs": primary["concurrent_jobs"],
            "distribution": "fixed",
            "repetitions": primary["repetitions"],
        }
    ]
    for delay in timing["service_delay_ms"]:
        if delay == primary["service_delay_ms"]:
            continue
        conditions.append(
            {
                "condition_id": f"service-delay-{delay:04d}ms",
                "axis": "service_delay",
                "reader_label": f"service delay: {delay} ms",
                "service_delay_ms": delay,
                "tick_rate_hz": primary["tick_rate_hz"],
                "concurrent_jobs": primary["concurrent_jobs"],
                "distribution": "fixed",
                "repetitions": timing["secondary_repetitions"],
            }
        )
    for tick_rate in timing["tick_rate_hz"]:
        if tick_rate == primary["tick_rate_hz"]:
            continue
        conditions.append(
            {
                "condition_id": f"tick-rate-{tick_rate:03d}hz",
                "axis": "tick_rate",
                "reader_label": f"tick rate: {tick_rate} Hz",
                "service_delay_ms": primary["service_delay_ms"],
                "tick_rate_hz": tick_rate,
                "concurrent_jobs": primary["concurrent_jobs"],
                "distribution": "fixed",
                "repetitions": timing["secondary_repetitions"],
            }
        )
    for jobs in timing["concurrent_jobs"]:
        if jobs == primary["concurrent_jobs"]:
            continue
        conditions.append(
            {
                "condition_id": f"concurrent-jobs-{jobs:02d}",
                "axis": "concurrency",
                "reader_label": f"concurrent task instances: {jobs}",
                "service_delay_ms": primary["service_delay_ms"],
                "tick_rate_hz": primary["tick_rate_hz"],
                "concurrent_jobs": jobs,
                "distribution": "fixed",
                "repetitions": timing["secondary_repetitions"],
            }
        )
    for distribution in timing["delay_distributions"]:
        if distribution == "fixed":
            continue
        conditions.append(
            {
                "condition_id": f"distribution-{distribution}",
                "axis": "distribution",
                "reader_label": f"delay distribution: {distribution.replace('_', ' ')}",
                "service_delay_ms": primary["service_delay_ms"],
                "tick_rate_hz": primary["tick_rate_hz"],
                "concurrent_jobs": primary["concurrent_jobs"],
                "distribution": distribution,
                "repetitions": timing["secondary_repetitions"],
            }
        )
    ids = [condition["condition_id"] for condition in conditions]
    if len(ids) != len(set(ids)):
        raise ValueError("timing condition catalogue contains duplicate IDs")
    return conditions


def split_selection(value: str | None, available: list[str], name: str) -> list[str]:
    if value is None or value == "all":
        return list(available)
    selected = [item.strip() for item in value.split(",") if item.strip()]
    unknown = sorted(set(selected) - set(available))
    if unknown:
        raise ValueError(f"unknown {name}: {', '.join(unknown)}")
    if not selected or len(selected) != len(set(selected)):
        raise ValueError(f"{name} selection must be non-empty and unique")
    return [item for item in available if item in selected]


def write_plan(
    path: Path,
    protocol: dict[str, Any],
    conditions: list[dict[str, Any]],
    variants: list[dict[str, Any]],
    repetitions_override: int | None,
    warmups: int,
) -> tuple[int, int]:
    timing = protocol["timing_lane"]
    common = protocol["common_task"]
    proposal = common["proposal"]
    rows: list[list[Any]] = [
        ["plan_version", "controlled-authority.timing-plan.v1"],
        ["protocol_id", protocol["protocol_id"]],
        ["timing_contract_id", timing["contract_id"]],
        ["initial_context_id", common["initial_context_id"]],
        ["request_deadline_ms", common["request_deadline_ms"]],
        ["frame_id", proposal["frame_id"]],
        ["minimum", *proposal["bounds"]["min"]],
        ["maximum", *proposal["bounds"]["max"]],
        ["common_task_path", (EXPERIMENT / "lisp" / "common_task.lisp").resolve()],
    ]
    variant_labels = [variant["short_label"] for variant in variants]
    seed_first = timing["seed_first"]
    for condition_index, condition in enumerate(conditions):
        for repetition in range(warmups):
            seed = seed_first - 10000 + condition_index * 100 + repetition
            for variant_label in variant_labels:
                rows.append(
                    [
                        "run",
                        condition["condition_id"],
                        condition["axis"],
                        condition["reader_label"],
                        variant_label,
                        seed,
                        repetition,
                        condition["service_delay_ms"],
                        condition["tick_rate_hz"],
                        condition["concurrent_jobs"],
                        condition["distribution"],
                        0,
                    ]
                )

    blocks: list[tuple[int, dict[str, Any], int]] = []
    for condition_index, condition in enumerate(conditions):
        repetitions = repetitions_override or condition["repetitions"]
        for repetition in range(repetitions):
            blocks.append((condition_index, condition, repetition))
    random.Random(seed_first).shuffle(blocks)
    for condition_index, condition, repetition in blocks:
        seed = seed_first + condition_index * 10000 + repetition
        ordered_variants = list(variant_labels)
        random.Random(seed).shuffle(ordered_variants)
        for variant_label in ordered_variants:
            rows.append(
                [
                    "run",
                    condition["condition_id"],
                    condition["axis"],
                    condition["reader_label"],
                    variant_label,
                    seed,
                    repetition,
                    condition["service_delay_ms"],
                    condition["tick_rate_hz"],
                    condition["concurrent_jobs"],
                    condition["distribution"],
                    1,
                ]
            )

    with path.open("w", encoding="utf-8", newline="") as handle:
        for row in rows:
            handle.write("\t".join(tsv_field(field) for field in row) + "\n")
    return len(blocks) * len(variants), len(conditions) * warmups * len(variants)


def read_text(path: Path) -> str | None:
    try:
        return path.read_text(encoding="utf-8").strip()
    except OSError:
        return None


def cpu_model() -> str:
    if Path("/proc/cpuinfo").is_file():
        for line in Path("/proc/cpuinfo").read_text(encoding="utf-8").splitlines():
            if line.lower().startswith("model name"):
                return line.split(":", 1)[1].strip()
    return platform.processor() or "unknown"


def memory_bytes() -> int:
    try:
        return os.sysconf("SC_PHYS_PAGES") * os.sysconf("SC_PAGE_SIZE")
    except (ValueError, OSError):
        return 0


def background_processes() -> list[str]:
    result = subprocess.run(
        ["ps", "-eo", "comm=,%cpu=,%mem="],
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        return []
    rows: list[tuple[float, str]] = []
    for line in result.stdout.splitlines():
        fields = line.split()
        if len(fields) < 3:
            continue
        try:
            cpu = float(fields[-2])
        except ValueError:
            continue
        rows.append((cpu, " ".join(fields)))
    rows.sort(reverse=True)
    return [text for _, text in rows[:20]]


def host_fingerprint(cpu_set: str | None) -> dict[str, Any]:
    affinity: str
    if cpu_set:
        affinity = cpu_set
    elif hasattr(os, "sched_getaffinity"):
        affinity = ",".join(str(cpu) for cpu in sorted(os.sched_getaffinity(0)))
    else:
        affinity = "all"
    load = list(os.getloadavg()) if hasattr(os, "getloadavg") else []
    return {
        "hostname": platform.node(),
        "operating_system": platform.system(),
        "operating_system_release": platform.release(),
        "kernel": platform.version(),
        "architecture": platform.machine(),
        "cpu_model": cpu_model(),
        "logical_cpu_count": os.cpu_count() or 0,
        "memory_bytes": memory_bytes(),
        "cpu_affinity": affinity,
        "load_average": load,
        "background_processes": background_processes(),
        "cpu_scaling_governor": read_text(
            Path("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor")
        ),
        "turbo_disabled": read_text(Path("/sys/devices/system/cpu/intel_pstate/no_turbo")),
    }


def read_trials(path: Path) -> list[dict[str, Any]]:
    trials: list[dict[str, Any]] = []
    with path.open(encoding="utf-8") as handle:
        for line_number, line in enumerate(handle, 1):
            value = json.loads(line)
            if not isinstance(value, dict):
                raise ValueError(f"raw timing trial {line_number} is not an object")
            validate_against_schema(value, RAW_SCHEMA)
            trials.append(value)
    return trials


def nearest_rank(values: list[float], quantile: float) -> float:
    if not values:
        raise ValueError("cannot calculate a quantile of an empty sample")
    ordered = sorted(values)
    return ordered[max(0, math.ceil(quantile * len(ordered)) - 1)]


def validate_trials(trials: list[dict[str, Any]], expected_count: int) -> list[str]:
    failures: list[str] = []
    if len(trials) != expected_count:
        failures.append(f"engine wrote {len(trials)} trials; expected {expected_count}")
    paired: dict[tuple[str, int], list[list[int]]] = {}
    for trial in trials:
        jobs = trial["concurrent_jobs"]
        if len(trial["requested_delays_ms"]) != jobs:
            failures.append("requested delay count does not match concurrent task count")
        if len(trial["actual_service_ms"]) != jobs:
            failures.append("actual service count does not match concurrent task count")
        if trial["terminal_decisions"] < jobs:
            failures.append("not every timing task reached a terminal decision")
        if trial["active_jobs_at_end"] != 0:
            failures.append("timing trial ended with an active job")
        if trial["maximum_tick_ms"] < trial["maximum_task_tick_ms"]:
            failures.append("batch tick maximum is smaller than an individual task tick")
        key = (trial["condition_id"], trial["repetition"])
        paired.setdefault(key, []).append(trial["requested_delays_ms"])
    for key, delays in paired.items():
        if any(value != delays[0] for value in delays[1:]):
            failures.append(f"variants did not receive paired delays for {key}")
    return sorted(set(failures))


def summarise(trials: list[dict[str, Any]]) -> list[dict[str, Any]]:
    grouped: dict[tuple[str, str], list[dict[str, Any]]] = {}
    for trial in trials:
        grouped.setdefault((trial["condition_id"], trial["variant_label"]), []).append(trial)
    rows: list[dict[str, Any]] = []
    for _, group in grouped.items():
        tick_values = [float(item["maximum_tick_ms"]) for item in group]
        task_values = [float(item["maximum_task_tick_ms"]) for item in group]
        wall_values = [float(item["wall_duration_ms"]) for item in group]
        first = group[0]
        rows.append(
            {
                "condition_id": first["condition_id"],
                "axis": first["axis"],
                "condition": first["reader_label"],
                "variant_label": first["variant_label"],
                "trials": len(group),
                "service_delay_ms": first["service_delay_ms"],
                "tick_rate_hz": first["tick_rate_hz"],
                "concurrent_jobs": first["concurrent_jobs"],
                "distribution": first["distribution"],
                "maximum_tick_ms_mean": statistics.fmean(tick_values),
                "maximum_tick_ms_p50": nearest_rank(tick_values, 0.50),
                "maximum_tick_ms_p95": nearest_rank(tick_values, 0.95),
                "maximum_tick_ms_p99": nearest_rank(tick_values, 0.99),
                "maximum_tick_ms_max": max(tick_values),
                "maximum_task_tick_ms_p99": nearest_rank(task_values, 0.99),
                "wall_duration_ms_p50": nearest_rank(wall_values, 0.50),
            }
        )
    return sorted(rows, key=lambda row: (row["axis"], row["condition_id"], row["variant_label"]))


def write_artefacts(
    output: Path,
    trials: list[dict[str, Any]],
    variants: list[dict[str, Any]],
    conditions: list[dict[str, Any]],
) -> dict[str, str]:
    summary = summarise(trials)
    variant_names = {variant["short_label"]: variant["reader_label"] for variant in variants}
    for row in summary:
        row["authority_mechanism"] = variant_names[row["variant_label"]]
    write_json(output / "summary" / "timing-summary.json", summary)
    write_csv(output / "summary" / "timing-summary.csv", summary)
    write_csv(output / "summary" / "timing-trials.csv", trials)

    paper_rows = [
        {
            "controlled_condition": row["condition"],
            "authority_mechanism": row["authority_mechanism"],
            "trials": row["trials"],
            "p50_maximum_tick_ms": row["maximum_tick_ms_p50"],
            "p99_maximum_tick_ms": row["maximum_tick_ms_p99"],
            "maximum_tick_ms": row["maximum_tick_ms_max"],
        }
        for row in summary
    ]
    write_csv(output / "paper" / "controlled-authority-timing-table.csv", paper_rows)

    lookup = {
        (row["condition_id"], row["variant_label"]): row
        for row in summary
    }
    markdown = output / "paper" / "controlled-authority-timing-table.md"
    markdown.parent.mkdir(parents=True, exist_ok=True)
    with markdown.open("w", encoding="utf-8") as handle:
        handle.write("# controlled invocation-authority timing results\n\n")
        handle.write(
            "Each cell is the nearest-rank p99 of the maximum real-clock scheduler-cycle "
            "duration per trial, in milliseconds. Timing and semantic results are not pooled.\n\n"
        )
        labels = [variant["reader_label"] for variant in variants]
        handle.write("| Controlled condition | " + " | ".join(labels) + " |\n")
        handle.write("|---|" + "---:|" * len(labels) + "\n")
        for condition in conditions:
            condition_id = condition["condition_id"]
            reader_label = condition["reader_label"]
            cells = [
                f"{lookup[(condition_id, variant['short_label'])]['maximum_tick_ms_p99']:.3f}"
                for variant in variants
            ]
            handle.write(f"| {reader_label} | " + " | ".join(cells) + " |\n")
    return {
        "timing_trials_csv": "summary/timing-trials.csv",
        "timing_summary_json": "summary/timing-summary.json",
        "timing_summary_csv": "summary/timing-summary.csv",
        "paper_table_csv": "paper/controlled-authority-timing-table.csv",
        "paper_table_markdown": "paper/controlled-authority-timing-table.md",
    }


def git_revision() -> str:
    result = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=ROOT,
        check=False,
        capture_output=True,
        text=True,
    )
    return result.stdout.strip() if result.returncode == 0 else "unknown"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True, help="new timing campaign directory")
    parser.add_argument(
        "--engine",
        type=Path,
        default=ROOT / "build" / "muesli_bt_controlled_authority_timing",
    )
    parser.add_argument("--protocol", type=Path, default=DEFAULT_PROTOCOL)
    parser.add_argument("--conditions", help="comma-separated internal condition selection")
    parser.add_argument("--variants", help="comma-separated B0--B3 selection")
    parser.add_argument("--repetitions", type=int, help="override repetitions for every condition")
    parser.add_argument("--warmups", type=int, help="override warm-ups per condition and variant")
    parser.add_argument("--cpu-set", help="Linux taskset CPU list, for example 0-7")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.repetitions is not None and args.repetitions <= 0:
        raise ValueError("repetition override must be positive")
    if args.warmups is not None and args.warmups < 0:
        raise ValueError("warm-up override must be non-negative")
    output = args.output.resolve()
    if output.exists() and any(output.iterdir()):
        raise ValueError(f"output directory must be new or empty: {output}")
    output.mkdir(parents=True, exist_ok=True)

    protocol_path = args.protocol.resolve()
    protocol = load_json(protocol_path)
    timing = protocol["timing_lane"]
    all_conditions = condition_catalogue(timing)
    condition_ids = split_selection(
        args.conditions, [item["condition_id"] for item in all_conditions], "conditions"
    )
    variant_labels = split_selection(
        args.variants, [item["short_label"] for item in protocol["variants"]], "variants"
    )
    conditions = [item for item in all_conditions if item["condition_id"] in condition_ids]
    variants = [
        item for item in protocol["variants"] if item["short_label"] in variant_labels
    ]
    warmups = (
        args.warmups
        if args.warmups is not None
        else timing["warmup_repetitions_per_condition_and_variant"]
    )

    plan_path = output / "resolved-timing-plan.tsv"
    expected_trials, expected_warmups = write_plan(
        plan_path, protocol, conditions, variants, args.repetitions, warmups
    )
    engine = args.engine.resolve()
    if not engine.is_file():
        raise ValueError(f"timing engine does not exist: {engine}")
    command = [str(engine), str(plan_path), str(output)]
    if args.cpu_set:
        taskset = shutil.which("taskset")
        if not taskset:
            raise ValueError("--cpu-set requires the Linux taskset command")
        command = [taskset, "-c", args.cpu_set, *command]

    host_before = host_fingerprint(args.cpu_set)
    subprocess.run(command, cwd=ROOT, check=True)
    host_after = host_fingerprint(args.cpu_set)
    raw_path = output / "raw_timing_trials.jsonl"
    trials = read_trials(raw_path)
    validation_failures = validate_trials(trials, expected_trials)
    artefacts = write_artefacts(output, trials, variants, conditions)

    full_repetitions = all(
        sum(
            1
            for trial in trials
            if trial["condition_id"] == condition["condition_id"]
            and trial["variant_label"] == "B0"
        )
        == condition["repetitions"]
        for condition in all_conditions
    )
    paper_complete = (
        condition_ids == [item["condition_id"] for item in all_conditions]
        and variant_labels == ["B0", "B1", "B2", "B3"]
        and args.repetitions is None
        and warmups == timing["warmup_repetitions_per_condition_and_variant"]
        and full_repetitions
    )
    maximum_load = timing["host_policy"]["maximum_normalised_load_average"]
    load_within_limit = all(
        not host["load_average"]
        or host["load_average"][0] / host["logical_cpu_count"] <= maximum_load
        for host in (host_before, host_after)
    )
    if not paper_complete:
        paper_reason = "incomplete_timing_campaign"
    elif validation_failures:
        paper_reason = "timing_validation_failure"
    elif not load_within_limit:
        paper_reason = "host_load_exceeded"
    else:
        paper_reason = None
    paper_gate = {
        "evaluated": paper_complete,
        "passed": not validation_failures and load_within_limit if paper_complete else None,
        "reason": paper_reason,
        "host_load_within_limit": load_within_limit,
        "validation_failures": validation_failures,
    }

    material = json.dumps(
        {
            "protocol": sha256(protocol_path),
            "plan": sha256(plan_path),
            "engine": sha256(engine),
            "host": {
                key: host_before[key]
                for key in (
                    "hostname",
                    "operating_system",
                    "architecture",
                    "cpu_model",
                    "logical_cpu_count",
                    "cpu_affinity",
                )
            },
        },
        sort_keys=True,
    ).encode()
    campaign_id = "controlled-authority-timing-" + hashlib.sha256(material).hexdigest()[:16]
    manifest = {
        "$schema": (
            "https://muesli-bt.invalid/schemas/controlled_authority/v1/"
            "timing-campaign-manifest.schema.json"
        ),
        "schema_version": "controlled-authority.timing-campaign-manifest.v1",
        "campaign_id": campaign_id,
        "created_at": datetime.now(timezone.utc).isoformat(),
        "protocol_id": protocol["protocol_id"],
        "timing_contract_id": timing["contract_id"],
        "lane": "timing",
        "clock": timing["clock"],
        "condition_design": timing["condition_design"],
        "condition_ids": condition_ids,
        "condition_reader_labels": [item["reader_label"] for item in conditions],
        "variant_short_labels": variant_labels,
        "variant_reader_labels": [item["reader_label"] for item in variants],
        "warmups_executed": expected_warmups,
        "trial_count": len(trials),
        "host_before": host_before,
        "host_after": host_after,
        "paper_gate": paper_gate,
        "campaign_valid": not validation_failures,
        "inputs": {
            "protocol": {"path": str(protocol_path), "sha256": sha256(protocol_path)},
            "common_task": {
                "path": str((EXPERIMENT / "lisp" / "common_task.lisp").resolve()),
                "sha256": sha256(EXPERIMENT / "lisp" / "common_task.lisp"),
            },
            "resolved_plan": {"path": "resolved-timing-plan.tsv", "sha256": sha256(plan_path)},
            "engine": {"path": str(engine), "sha256": sha256(engine)},
        },
        "environment": {
            "git_revision": git_revision(),
            "python": platform.python_version(),
            "command": command,
        },
        "raw_trials": {"path": "raw_timing_trials.jsonl", "sha256": sha256(raw_path)},
        "derived_artefacts": {
            name: {"path": path, "sha256": sha256(output / path)}
            for name, path in artefacts.items()
        },
        "manual_run_exclusion_allowed": False,
    }
    validate_against_schema(manifest, MANIFEST_SCHEMA)
    write_json(output / "timing-campaign-manifest.json", manifest)
    print(
        f"timing campaign {campaign_id}: {len(trials)} trials, "
        f"{expected_warmups} warm-ups, {len(validation_failures)} validation failures"
    )
    return 0 if manifest["campaign_valid"] else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"controlled-authority timing campaign failed: {error}", file=sys.stderr)
        raise SystemExit(2) from error
