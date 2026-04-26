#!/usr/bin/env python3

from __future__ import annotations

import argparse
import datetime as dt
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


def quality_specs(profile: str, with_btcpp: bool) -> list[RunSpec]:
    if profile == "smoke":
        core = dict(warmup_ms=25, run_ms=75, repetitions=1)
        jitter = dict(warmup_ms=25, run_ms=75, repetitions=1)
        b5 = dict(repetitions=1)
        b7 = dict(warmup_ms=25, run_ms=75, repetitions=1)
        b8 = dict(warmup_ms=25, run_ms=75, repetitions=1)
    else:
        core = dict(warmup_ms=1000, run_ms=5000, repetitions=10)
        jitter = dict(warmup_ms=3000, run_ms=60000, repetitions=5)
        b5 = dict(repetitions=200)
        b7 = dict(warmup_ms=1000, run_ms=30000, repetitions=5)
        b8 = dict(warmup_ms=500, run_ms=5000, repetitions=10)

    specs = [
        RunSpec("muesli-a1-baseline", "run-group", "A1", **core),
        RunSpec("muesli-b1-static-tick", "run-group", "B1", **core),
        RunSpec("muesli-b2-reactive-interrupt", "run-group", "B2", **core),
        RunSpec("muesli-b6-logging", "run-group", "B6", **core),
        RunSpec("muesli-a2-tail-latency", "run", "A2-alt-255-jitter-off", **jitter),
        RunSpec("muesli-b5-lifecycle", "run-group", "B5", **b5),
        RunSpec(
            "muesli-b7-memory-gc",
            "run-group",
            "B7",
            make_memory_figure=True,
            make_evidence_report=True,
            **b7,
        ),
        RunSpec("muesli-b8-async-contract", "run-group", "B8", **b8),
    ]

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
        if spec.selector in {"B7", "B8"}:
            handle.write("  - evidence: `<scenario>/rep-*/events.jsonl`\n")


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


def main() -> int:
    args = parse_args()
    output_root = Path(args.output_root).expanduser()
    if not output_root.is_absolute():
        output_root = (REPO_ROOT / output_root).resolve()
    bundle_dir = output_root / f"{args.label}-{args.profile}-{timestamp_slug()}"
    specs = quality_specs(args.profile, args.with_btcpp)
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

    print(f"wrote publication benchmark bundle to {bundle_dir}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
