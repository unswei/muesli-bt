#!/usr/bin/env python3

from __future__ import annotations

import argparse
import csv
import datetime as dt
import hashlib
import json
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
BENCH_ROOT = REPO_ROOT / "bench"
DEFAULT_OUTPUT_ROOT = BENCH_ROOT / "results"


@dataclass(frozen=True)
class RunSpec:
    name: str
    command: str
    selector: str
    runtime: str = "muesli"
    warmup_ms: int | None = None
    run_ms: int | None = None
    repetitions: int | None = None
    make_tail_figure: bool = True
    make_memory_figure: bool = False
    make_evidence_report: bool = False


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Run the publication-quality benchmark suite into one timestamped result bundle. "
            "This is intentionally separate from bench run-all, which stays a reasonable catalogue smoke run."
        )
    )
    parser.add_argument(
        "--profile",
        choices=("publication", "smoke"),
        default="publication",
        help="quality profile; smoke is for checking the script path only",
    )
    parser.add_argument(
        "--output-root",
        default=str(DEFAULT_OUTPUT_ROOT),
        help="directory under which the timestamped publication result bundle is created",
    )
    parser.add_argument("--label", default="publication", help="prefix for the timestamped result bundle")
    parser.add_argument("--skip-build", action="store_true", help="do not configure or build benchmark presets first")
    parser.add_argument("--with-btcpp", action="store_true", help="also run the optional BehaviorTree.CPP comparison subset")
    parser.add_argument(
        "--comparison-only",
        action="store_true",
        help="run only the shared A1, A2, B1, B2 and B5 subset; requires --with-btcpp",
    )
    parser.add_argument("--dry-run", action="store_true", help="print the commands and manifest path without running them")
    parser.add_argument(
        "--bench-bin",
        default=str(REPO_ROOT / "build" / "bench-release" / "bench" / "bench"),
        help="muesli-bt benchmark binary",
    )
    parser.add_argument(
        "--btcpp-bench-bin",
        default=str(REPO_ROOT / "build" / "bench-release-btcpp" / "bench" / "bench"),
        help="BehaviorTree.CPP-enabled benchmark binary",
    )
    return parser.parse_args()


def quality_specs(profile: str, with_btcpp: bool, comparison_only: bool = False) -> list[RunSpec]:
    if profile == "smoke":
        core = dict(warmup_ms=25, run_ms=75, repetitions=1)
        jitter = dict(warmup_ms=25, run_ms=75, repetitions=1)
        b5 = dict(repetitions=1)
        b7 = dict(warmup_ms=25, run_ms=75, repetitions=1)
        b8 = dict(warmup_ms=25, run_ms=75, repetitions=1)
        b9 = dict(warmup_ms=25, run_ms=75, repetitions=1)
    else:
        core = dict(warmup_ms=1000, run_ms=5000, repetitions=10)
        jitter = dict(warmup_ms=3000, run_ms=60000, repetitions=5)
        b5 = dict(repetitions=200)
        b7 = dict(warmup_ms=1000, run_ms=30000, repetitions=5)
        b8 = dict(warmup_ms=500, run_ms=5000, repetitions=10)
        b9 = dict(warmup_ms=500, run_ms=5000, repetitions=10)

    specs = [
        RunSpec("muesli-a1-baseline", "run-group", "A1", **core),
        RunSpec("muesli-b1-static-tick", "run-group", "B1", **core),
        RunSpec("muesli-b2-reactive-interrupt", "run-group", "B2", **core),
        RunSpec("muesli-a2-tail-latency", "run", "A2-alt-255-jitter-off", **jitter),
        RunSpec("muesli-b5-lifecycle", "run-group", "B5", **b5),
    ]
    if not comparison_only:
        specs.extend(
            [
                RunSpec("muesli-b6-logging", "run-group", "B6", **core),
                RunSpec(
                    "muesli-b7-memory-gc",
                    "run-group",
                    "B7",
                    make_memory_figure=True,
                    make_evidence_report=True,
                    **b7,
                ),
                RunSpec("muesli-b8-async-contract", "run-group", "B8", **b8),
                RunSpec(
                    "muesli-b9-generated-subtree-contract",
                    "run-group",
                    "B9",
                    make_evidence_report=True,
                    **b9,
                ),
            ]
        )

    if with_btcpp:
        specs.extend(
            [
                RunSpec("btcpp-a1-baseline", "run-group", "A1", runtime="btcpp", **core),
                RunSpec("btcpp-b1-static-tick", "run-group", "B1", runtime="btcpp", **core),
                RunSpec("btcpp-b2-reactive-interrupt", "run-group", "B2", runtime="btcpp", **core),
                RunSpec("btcpp-a2-tail-latency", "run", "A2-alt-255-jitter-off", runtime="btcpp", **jitter),
                RunSpec("btcpp-b5-lifecycle", "run-group", "B5", runtime="btcpp", **b5),
            ]
        )

    return specs


def timestamp_slug() -> str:
    return dt.datetime.now(dt.UTC).strftime("%Y%m%dT%H%M%SZ")


def build_command_for_spec(spec: RunSpec, output_dir: Path, bench_bin: Path, btcpp_bench_bin: Path) -> list[str]:
    binary = btcpp_bench_bin if spec.runtime == "btcpp" else bench_bin
    cmd = [str(binary), spec.command, spec.selector, "--output-dir", str(output_dir)]
    if spec.runtime != "muesli":
        cmd.extend(["--runtime", spec.runtime])
    if spec.warmup_ms is not None:
        cmd.extend(["--warmup-ms", str(spec.warmup_ms)])
    if spec.run_ms is not None:
        cmd.extend(["--run-ms", str(spec.run_ms)])
    if spec.repetitions is not None:
        cmd.extend(["--repetitions", str(spec.repetitions)])
    return cmd


def run_command(cmd: list[str], *, stdout_path: Path | None = None, dry_run: bool = False) -> None:
    printable = " ".join(cmd)
    print(f"+ {printable}", flush=True)
    if dry_run:
        return
    if stdout_path is None:
        subprocess.run(cmd, cwd=REPO_ROOT, check=True)
        return
    with stdout_path.open("w", encoding="utf-8") as handle:
        subprocess.run(cmd, cwd=REPO_ROOT, check=True, stdout=handle)


def maybe_build(args: argparse.Namespace) -> None:
    if args.skip_build:
        return
    run_command(["cmake", "--preset", "bench-release"], dry_run=args.dry_run)
    run_command(["cmake", "--build", "--preset", "bench-release", "-j"], dry_run=args.dry_run)
    if args.with_btcpp:
        run_command(["cmake", "--preset", "bench-release-btcpp"], dry_run=args.dry_run)
        run_command(["cmake", "--build", "--preset", "bench-release-btcpp", "-j"], dry_run=args.dry_run)


def write_manifest_header(path: Path, args: argparse.Namespace, specs: list[RunSpec]) -> None:
    lines = [
        "# publication benchmark bundle",
        "",
        f"- created_utc: `{dt.datetime.now(dt.UTC).isoformat(timespec='seconds')}`",
        f"- profile: `{args.profile}`",
        f"- with_btcpp: `{str(args.with_btcpp).lower()}`",
        f"- comparison_only: `{str(args.comparison_only).lower()}`",
        f"- result_count: `{len(specs)}`",
        "",
        "## runs",
        "",
    ]
    path.write_text("\n".join(lines), encoding="utf-8")


def append_manifest_run(path: Path, spec: RunSpec, output_dir: Path, cmd: list[str]) -> None:
    try:
        display_dir = output_dir.relative_to(REPO_ROOT)
    except ValueError:
        display_dir = output_dir
    with path.open("a", encoding="utf-8") as handle:
        handle.write(f"- `{spec.name}`: `{display_dir}`\n")
        handle.write(f"  - command: `{' '.join(cmd)}`\n")
        handle.write("  - outputs: `run_summary.csv`, `aggregate_summary.csv`, `environment_metadata.csv`, `experiment_manifest.json`\n")
        if spec.make_tail_figure:
            handle.write("  - figure: `tail_latency.svg`\n")
        if spec.make_memory_figure:
            handle.write("  - figure: `memory_gc.svg`\n")
        if spec.make_evidence_report:
            handle.write("  - report: `evidence_report.md`\n")
        if spec.selector in {"B7", "B8", "B9"}:
            handle.write("  - evidence: `<scenario>/rep-*/events.jsonl`\n")
        if spec.selector == "B9":
            handle.write("  - sidecars: `<scenario>/rep-*/generated_subtree_report.json`\n")


def post_process(spec: RunSpec, output_dir: Path, dry_run: bool) -> None:
    analyse = BENCH_ROOT / "scripts" / "analyse_results.py"
    tail = BENCH_ROOT / "scripts" / "figure_tail_latency.py"
    memory = BENCH_ROOT / "scripts" / "figure_memory_gc.py"
    report = BENCH_ROOT / "scripts" / "write_evidence_report.py"

    run_command([sys.executable, str(analyse), str(output_dir)], stdout_path=output_dir / "analysis.txt", dry_run=dry_run)
    if spec.make_tail_figure:
        run_command(
            [sys.executable, str(tail), str(output_dir), "--output", str(output_dir / "tail_latency.svg")],
            dry_run=dry_run,
        )
    if spec.make_memory_figure:
        run_command(
            [sys.executable, str(memory), str(output_dir), "--output", str(output_dir / "memory_gc.svg")],
            dry_run=dry_run,
        )
    if spec.make_evidence_report:
        run_command([sys.executable, str(report), str(output_dir)], dry_run=dry_run)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def supports_cross_runtime_scenario(scenario_id: str) -> bool:
    if scenario_id == "A1-single-leaf-off" or scenario_id == "A2-alt-255-jitter-off":
        return True
    if scenario_id.startswith("B1-") or scenario_id.startswith("B2-"):
        return True
    return scenario_id.startswith("B5-") and any(
        f"-{phase}-off" in scenario_id
        for phase in ("compile", "inst1", "inst100", "loaddsl")
    )


def combine_runtime_results(bundle_dir: Path, specs: list[RunSpec], runtime: str) -> Path:
    shared_selectors = {"A1", "A2-alt-255-jitter-off", "B1", "B2", "B5"}
    selected = [
        spec for spec in specs if spec.runtime == runtime and spec.selector in shared_selectors
    ]
    if not selected:
        raise ValueError(f"publication bundle has no {runtime} comparison runs")
    output = bundle_dir / "cross-runtime" / runtime
    output.mkdir(parents=True, exist_ok=False)

    aggregate_header: list[str] | None = None
    aggregate_rows: list[dict[str, str]] = []
    environment_header: list[str] | None = None
    environment_row: dict[str, str] | None = None
    sources: list[dict[str, str]] = []
    for spec in selected:
        source = bundle_dir / spec.name
        aggregate_path = source / "aggregate_summary.csv"
        environment_path = source / "environment_metadata.csv"
        manifest_path = source / "experiment_manifest.json"
        with aggregate_path.open(newline="", encoding="utf-8") as handle:
            reader = csv.DictReader(handle)
            current_header = list(reader.fieldnames or [])
            if aggregate_header is None:
                aggregate_header = current_header
            elif current_header != aggregate_header:
                raise ValueError("comparison aggregate CSV headers differ")
            aggregate_rows.extend(
                row for row in reader if supports_cross_runtime_scenario(row["scenario_id"])
            )
        with environment_path.open(newline="", encoding="utf-8") as handle:
            reader = csv.DictReader(handle)
            rows = list(reader)
            if len(rows) != 1:
                raise ValueError(f"expected one environment row in {environment_path}")
            current_header = list(reader.fieldnames or [])
            if environment_header is None:
                environment_header = current_header
                environment_row = rows[0]
            else:
                if current_header != environment_header:
                    raise ValueError("comparison environment CSV headers differ")
                stable_fields = set(current_header) - {"timestamp_utc"}
                if any(rows[0][field] != environment_row[field] for field in stable_fields):
                    raise ValueError(f"execution environment changed within {runtime} runs")
        sources.append(
            {
                "run": spec.name,
                "aggregate_summary_sha256": sha256(aggregate_path),
                "environment_metadata_sha256": sha256(environment_path),
                "experiment_manifest_sha256": sha256(manifest_path),
            }
        )

    if aggregate_header is None:
        raise ValueError(f"publication bundle has no aggregate rows for {runtime}")
    scenario_ids = [row["scenario_id"] for row in aggregate_rows]
    if len(scenario_ids) != len(set(scenario_ids)):
        raise ValueError(f"comparison bundle has duplicate {runtime} scenarios")
    aggregate_output = output / "aggregate_summary.csv"
    with aggregate_output.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=aggregate_header)
        writer.writeheader()
        writer.writerows(aggregate_rows)
    shutil.copyfile(
        bundle_dir / selected[0].name / "environment_metadata.csv",
        output / "environment_metadata.csv",
    )
    (output / "sources.json").write_text(
        json.dumps({"runtime": runtime, "sources": sources}, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return output


def write_cross_runtime_report(bundle_dir: Path, specs: list[RunSpec], dry_run: bool) -> None:
    comparison_root = bundle_dir / "cross-runtime"
    left = comparison_root / "muesli"
    right = comparison_root / "btcpp"
    report = comparison_root / "comparison.md"
    compare = BENCH_ROOT / "scripts" / "compare_results.py"
    if dry_run:
        run_command([sys.executable, str(compare), str(left), str(right)], dry_run=True)
        return
    left = combine_runtime_results(bundle_dir, specs, "muesli")
    right = combine_runtime_results(bundle_dir, specs, "btcpp")
    with (left / "aggregate_summary.csv").open(newline="", encoding="utf-8") as handle:
        left_scenarios = {row["scenario_id"] for row in csv.DictReader(handle)}
    with (right / "aggregate_summary.csv").open(newline="", encoding="utf-8") as handle:
        right_scenarios = {row["scenario_id"] for row in csv.DictReader(handle)}
    if left_scenarios != right_scenarios:
        raise ValueError("cross-runtime scenario sets differ after filtering")
    run_command([sys.executable, str(compare), str(left), str(right)], stdout_path=report)
    manifest = {
        "schema_version": "muesli-bt.cross-runtime-publication.v1",
        "shared_scenarios": ["A1", "A2-alt-255-jitter-off", "B1", "B2", "B5"],
        "matched_scenario_count": len(left_scenarios),
        "matched_scenario_ids": sorted(left_scenarios),
        "unsupported_scenarios_excluded": True,
        "muesli_sources_sha256": sha256(left / "sources.json"),
        "btcpp_sources_sha256": sha256(right / "sources.json"),
        "comparison_report_sha256": sha256(report),
    }
    (comparison_root / "manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )


def main() -> int:
    args = parse_args()
    if args.comparison_only and not args.with_btcpp:
        raise ValueError("--comparison-only requires --with-btcpp")
    output_root = Path(args.output_root).expanduser()
    if not output_root.is_absolute():
        output_root = (REPO_ROOT / output_root).resolve()
    bundle_dir = output_root / f"{args.label}-{args.profile}-{timestamp_slug()}"
    specs = quality_specs(args.profile, args.with_btcpp, args.comparison_only)
    bench_bin = Path(args.bench_bin).expanduser().resolve()
    btcpp_bench_bin = Path(args.btcpp_bench_bin).expanduser().resolve()

    maybe_build(args)
    print(f"bundle: {bundle_dir}", flush=True)
    if not args.dry_run:
        bundle_dir.mkdir(parents=True, exist_ok=False)
    manifest_path = bundle_dir / "publication_manifest.md"
    if not args.dry_run:
        write_manifest_header(manifest_path, args, specs)

    for spec in specs:
        output_dir = bundle_dir / spec.name
        cmd = build_command_for_spec(spec, output_dir, bench_bin, btcpp_bench_bin)
        run_command(cmd, dry_run=args.dry_run)
        if not args.dry_run:
            post_process(spec, output_dir, args.dry_run)
            append_manifest_run(manifest_path, spec, output_dir, cmd)

    if args.with_btcpp:
        write_cross_runtime_report(bundle_dir, specs, args.dry_run)
        if not args.dry_run:
            with manifest_path.open("a", encoding="utf-8") as handle:
                handle.write("\n## cross-runtime report\n\n")
                handle.write("- `cross-runtime/comparison.md`\n")
                handle.write("- `cross-runtime/manifest.json`\n")

    print(f"wrote publication benchmark bundle to {bundle_dir}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
