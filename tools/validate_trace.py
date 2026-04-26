#!/usr/bin/env python3
"""Trace-level validation CLI for canonical mbt.evt.v1 event streams."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from trace_validator import UsageError, batch_validate, compare_traces, load_config, validate_trace


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Validate cross-event properties for mbt.evt.v1 traces.")
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("--config", help="Optional TOML config file.")
    common.add_argument(
        "--profile",
        default="strict_runtime",
        choices=["strict_runtime", "deterministic", "cross_backend", "ci_smoke"],
        help="Validator profile to use (default: strict_runtime).",
    )
    common.add_argument("--report", help="Optional JSON report output path.")

    subparsers = parser.add_subparsers(dest="command", required=True)

    check = subparsers.add_parser("check", help="Validate one trace.", parents=[common])
    check.add_argument("trace", help="JSONL trace or artefact directory containing events.jsonl.")
    check.add_argument(
        "--tolerate-incomplete-tail",
        action="store_true",
        help="Allow the final open tick to remain incomplete and mark the report as truncated.",
    )

    compare = subparsers.add_parser("compare", help="Compare two traces after normalisation.", parents=[common])
    compare.add_argument("trace_a", help="First JSONL trace or artefact directory.")
    compare.add_argument("trace_b", help="Second JSONL trace or artefact directory.")

    batch = subparsers.add_parser("batch", help="Validate multiple traces and emit an aggregate report.", parents=[common])
    batch.add_argument("traces", nargs="+", help="JSONL traces or artefact directories.")
    batch.add_argument(
        "--tolerate-incomplete-tail",
        action="store_true",
        help="Allow the final open tick to remain incomplete for each checked trace.",
    )

    return parser


def write_report(path: str | None, payload: dict) -> None:
    if path is None:
        return
    report_path = Path(path)
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def print_check_summary(report) -> None:
    status = "PASS" if report.passed else "FAIL"
    print(
        f"{status}: {report.input_files[0]} "
        f"(events={report.total_events}, completed_ticks={report.completed_ticks}, violations={len(report.violations)})"
    )
    if report.trace_truncated:
        print("  note: tolerant incomplete-tail handling was used; final open tick was accepted as truncated")
    for violation in report.violations[:5]:
        location = f"{violation.file_name}:{violation.line_no}" if violation.line_no is not None else violation.file_name
        print(f"  {violation.code}: {location}: {violation.message}")


def print_compare_summary(report) -> None:
    status = "MATCH" if report.matched else "MISMATCH"
    print(
        f"{status}: {report.input_files[0]} vs {report.input_files[1]} "
        f"(events_a={report.event_count_a}, events_b={report.event_count_b})"
    )
    if report.first_divergence is not None:
        print(f"  first divergence: index {report.first_divergence['event_index']}")
        details = []
        for key in (
            "tick",
            "tick_a",
            "tick_b",
            "event_type",
            "event_type_a",
            "event_type_b",
            "node_id",
            "node_id_a",
            "node_id_b",
            "blackboard_key",
            "blackboard_key_a",
            "blackboard_key_b",
            "async_job_id",
            "async_job_id_a",
            "async_job_id_b",
            "host_capability",
            "host_capability_a",
            "host_capability_b",
            "planner",
            "planner_a",
            "planner_b",
            "field_path",
        ):
            if key in report.first_divergence:
                details.append(f"{key}={report.first_divergence[key]}")
        if details:
            print(f"  context: {', '.join(details)}")
        if report.mismatch_code is not None:
            print(f"  reason: {report.mismatch_code}")


def print_batch_summary(report_dict: dict) -> None:
    summary = report_dict["summary"]
    status = "PASS" if report_dict["passed"] else "FAIL"
    print(
        f"{status}: {summary['trace_count']} trace(s) "
        f"(passed={summary['passed_count']}, failed={summary['failed_count']}, total_events={summary['total_events']})"
    )
    for code, count in list(report_dict["violation_counts"].items())[:5]:
        print(f"  {code}: {count}")


def main(argv: list[str]) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)

    try:
        config = load_config(args.config, args.profile)
        if getattr(args, "tolerate_incomplete_tail", False):
            config = config.__class__(
                profile=config.profile,
                tolerate_incomplete_tail=True,
                ignore_event_types=config.ignore_event_types,
                drop_fields=config.drop_fields,
            )

        if args.command == "check":
            report = validate_trace(args.trace, config)
            payload = report.to_dict()
            write_report(args.report, payload)
            print_check_summary(report)
            return 0 if report.passed else 1

        if args.command == "compare":
            report = compare_traces(args.trace_a, args.trace_b, config)
            payload = report.to_dict()
            write_report(args.report, payload)
            print_compare_summary(report)
            return 0 if report.matched else 3

        if args.command == "batch":
            payload = batch_validate(args.traces, config)
            write_report(args.report, payload)
            print_batch_summary(payload)
            return 0 if payload["passed"] else 1

        raise UsageError(f"unsupported command: {args.command}")
    except UsageError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
