#!/usr/bin/env python3

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
from pathlib import Path


def run_script(script: Path, sample_dir: Path, output: Path, extra_args: list[str] | None = None) -> str:
    command = [sys.executable, str(script), str(sample_dir), "--output", str(output)]
    if extra_args:
        command.extend(extra_args)
    completed = subprocess.run(command, check=True, capture_output=True, text=True)
    return completed.stdout


def write_gc_log(path: Path) -> None:
    records = [
        {
            "v": 1,
            "type": "gc_end",
            "run_id": "figure-test",
            "unix_ms": 1,
            "seq": 1,
            "data": {
                "schema_version": "gc.lifecycle.v1",
                "collection_id": 1,
                "phase": "end",
                "reason": "forced",
                "policy": "between-ticks",
                "forced": True,
                "in_tick": False,
                "heap_live_bytes_before": 4096,
                "live_objects_before": 128,
                "heap_live_bytes_after": 2048,
                "live_objects_after": 64,
                "freed_objects": 64,
                "mark_time_ns": 1200,
                "sweep_time_ns": 1800,
                "pause_time_ns": 3000,
            },
        },
        {
            "v": 1,
            "type": "gc_end",
            "run_id": "figure-test",
            "unix_ms": 2,
            "seq": 2,
            "data": {
                "schema_version": "gc.lifecycle.v1",
                "collection_id": 2,
                "phase": "end",
                "reason": "maybe-collect",
                "policy": "between-ticks",
                "forced": False,
                "in_tick": False,
                "heap_live_bytes_before": 8192,
                "live_objects_before": 256,
                "heap_live_bytes_after": 4096,
                "live_objects_after": 128,
                "freed_objects": 128,
                "mark_time_ns": 2400,
                "sweep_time_ns": 2600,
                "pause_time_ns": 5000,
            },
        },
    ]
    with path.open("w", encoding="utf-8") as handle:
        for record in records:
            handle.write(json.dumps(record, separators=(",", ":")) + "\n")


def main() -> int:
    if len(sys.argv) != 5:
        raise SystemExit(
            "usage: check_figure_scripts.py <figure_tail_latency.py> <figure_memory_gc.py> <write_evidence_report.py> <sample-result-dir>"
        )

    tail_script = Path(sys.argv[1]).resolve()
    memory_script = Path(sys.argv[2]).resolve()
    report_script = Path(sys.argv[3]).resolve()
    sample_dir = Path(sys.argv[4]).resolve()

    with tempfile.TemporaryDirectory(prefix="muesli_bt_figures_") as tmp_raw:
        tmp = Path(tmp_raw)
        tail_svg = tmp / "tail_latency.svg"
        memory_svg = tmp / "memory_gc.svg"
        report_md = tmp / "evidence_report.md"
        gc_log = tmp / "events.jsonl"
        write_gc_log(gc_log)

        tail_stdout = run_script(tail_script, sample_dir, tail_svg)
        memory_stdout = run_script(memory_script, sample_dir, memory_svg, ["--event-log", str(gc_log)])
        report_stdout = run_script(report_script, sample_dir, report_md, ["--event-log", str(gc_log)])

        if "wrote tail-latency figure" not in tail_stdout:
            raise AssertionError("tail-latency script did not report output")
        if "wrote memory/GC figure" not in memory_stdout:
            raise AssertionError("memory/GC script did not report output")
        if "wrote evidence report" not in report_stdout:
            raise AssertionError("evidence report script did not report output")

        tail_text = tail_svg.read_text(encoding="utf-8")
        memory_text = memory_svg.read_text(encoding="utf-8")
        report_text = report_md.read_text(encoding="utf-8")
        for fragment in ("BT tick tail latency", "p99.9", "Y axis is logarithmic"):
            if fragment not in tail_text:
                raise AssertionError(f"tail-latency SVG missing: {fragment}")
        for fragment in ("Memory and GC evidence", "gc_end events: 2", "pause p99:", "Allocation pressure"):
            if fragment not in memory_text:
                raise AssertionError(f"memory/GC SVG missing: {fragment}")
        for fragment in ("benchmark evidence report", "GC end events: `2`", "heap-live delta:"):
            if fragment not in report_text:
                raise AssertionError(f"evidence report missing: {fragment}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
