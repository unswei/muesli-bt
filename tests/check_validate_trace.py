#!/usr/bin/env python3

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
SCRIPT = REPO_ROOT / "tools" / "validate_trace.py"
FIXTURE = REPO_ROOT / "fixtures" / "determinism-replay-case" / "events.jsonl"
DEADLINE_FIXTURE = REPO_ROOT / "fixtures" / "deadline-cancel-case" / "events.jsonl"


def load_events(path: Path) -> list[dict]:
    events: list[dict] = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.strip():
            events.append(json.loads(line))
    return events


def write_events(path: Path, events: list[dict]) -> None:
    payload = "\n".join(json.dumps(event, separators=(",", ":")) for event in events) + "\n"
    path.write_text(payload, encoding="utf-8")


def make_run_start() -> dict:
    return {
        "schema": "mbt.evt.v1",
        "contract_version": "1.0.0",
        "type": "run_start",
        "run_id": "trace-validator-smoke",
        "unix_ms": 1735689600000,
        "seq": 1,
        "data": {
            "git_sha": "fixture",
            "host": {"name": "muesli-bt", "version": "0.1.0", "platform": "linux"},
            "contract_version": "1.0.0",
            "contract_id": "runtime-contract-v1.0.0",
            "tick_hz": 50.0,
            "tree_hash": "fnv1a64:abababababababab",
            "capabilities": {"reset": True},
        },
    }


def resequence(events: list[dict], start_unix_ms: int = 1735689600000) -> None:
    for index, event in enumerate(events, start=1):
        event["seq"] = index
        event["unix_ms"] = start_unix_ms + index - 1


def run_cli(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(SCRIPT), *args],
        cwd=REPO_ROOT,
        check=False,
        capture_output=True,
        text=True,
    )


def assert_ok(completed: subprocess.CompletedProcess[str], context: str) -> None:
    if completed.returncode != 0:
        raise AssertionError(f"{context} failed unexpectedly:\nstdout={completed.stdout}\nstderr={completed.stderr}")


def assert_exit(completed: subprocess.CompletedProcess[str], code: int, context: str) -> None:
    if completed.returncode != code:
        raise AssertionError(
            f"{context} returned {completed.returncode}, expected {code}:\nstdout={completed.stdout}\nstderr={completed.stderr}"
        )


def main() -> int:
    with tempfile.TemporaryDirectory() as tmp_dir:
        tmp = Path(tmp_dir)

        completed = run_cli("check", str(FIXTURE))
        assert_ok(completed, "baseline check")
        if "PASS:" not in completed.stdout:
            raise AssertionError("baseline check summary should report PASS")

        completed = run_cli("check", str(DEADLINE_FIXTURE))
        assert_ok(completed, "deadline fixture check")
        if "PASS:" not in completed.stdout:
            raise AssertionError("deadline fixture check summary should report PASS")

        duplicate_seq_report = tmp / "duplicate_seq_report.json"
        duplicate_seq_trace = tmp / "duplicate_seq.jsonl"
        duplicate_seq_events = load_events(FIXTURE)
        duplicate_seq_events[1]["seq"] = duplicate_seq_events[0]["seq"]
        write_events(duplicate_seq_trace, duplicate_seq_events)
        completed = run_cli("check", str(duplicate_seq_trace), "--report", str(duplicate_seq_report))
        assert_exit(completed, 1, "duplicate seq check")
        duplicate_seq_payload = json.loads(duplicate_seq_report.read_text(encoding="utf-8"))
        if duplicate_seq_payload["violation_counts"].get("duplicate_seq") != 1:
            raise AssertionError("duplicate seq report should count duplicate_seq")

        missing_tick_end_trace = tmp / "missing_tick_end.jsonl"
        missing_tick_end_events = load_events(FIXTURE)[:-1]
        write_events(missing_tick_end_trace, missing_tick_end_events)
        completed = run_cli("check", str(missing_tick_end_trace))
        assert_exit(completed, 1, "strict missing tick_end check")
        if "missing_tick_end" not in completed.stdout:
            raise AssertionError("strict missing tick_end check should mention missing_tick_end")

        completed = run_cli("check", str(missing_tick_end_trace), "--tolerate-incomplete-tail")
        assert_ok(completed, "tolerant missing tick_end check")
        if "truncated" not in completed.stdout:
            raise AssertionError("tolerant mode should mention truncated handling")

        duplicate_terminal_trace = tmp / "duplicate_terminal.jsonl"
        duplicate_terminal_events = load_events(FIXTURE)
        extra_exit = json.loads(json.dumps(duplicate_terminal_events[11]))
        extra_exit["seq"] = 100
        duplicate_terminal_events.insert(12, extra_exit)
        for index, event in enumerate(duplicate_terminal_events, start=1):
            event["seq"] = index
        write_events(duplicate_terminal_trace, duplicate_terminal_events)
        completed = run_cli("check", str(duplicate_terminal_trace))
        assert_exit(completed, 1, "duplicate terminal node_exit check")
        if "duplicate_terminal_node_exit" not in completed.stdout:
            raise AssertionError("duplicate terminal node_exit check should mention duplicate_terminal_node_exit")

        missing_deadline_trace = tmp / "missing_deadline.jsonl"
        missing_deadline_events = [event for event in load_events(DEADLINE_FIXTURE) if event["type"] != "deadline_exceeded"]
        resequence(missing_deadline_events)
        write_events(missing_deadline_trace, missing_deadline_events)
        completed = run_cli("check", str(missing_deadline_trace))
        assert_exit(completed, 1, "missing deadline_exceeded check")
        if "missing_deadline_exceeded" not in completed.stdout:
            raise AssertionError("missing deadline_exceeded check should mention missing_deadline_exceeded")

        missing_cancel_trace = tmp / "missing_cancel_request.jsonl"
        missing_cancel_events = [
            event
            for event in load_events(DEADLINE_FIXTURE)
            if event["type"] not in {"async_cancel_requested", "async_cancel_acknowledged"}
        ]
        resequence(missing_cancel_events)
        write_events(missing_cancel_trace, missing_cancel_events)
        completed = run_cli("check", str(missing_cancel_trace))
        assert_exit(completed, 1, "missing cancel request after deadline check")
        if "missing_cancel_request_after_deadline" not in completed.stdout:
            raise AssertionError(
                "missing cancel request after deadline check should mention missing_cancel_request_after_deadline"
            )

        ack_without_request_trace = tmp / "ack_without_request.jsonl"
        ack_without_request_events = [
            make_run_start(),
            {
                "schema": "mbt.evt.v1",
                "contract_version": "1.0.0",
                "type": "tick_begin",
                "run_id": "trace-validator-smoke",
                "unix_ms": 1735689600001,
                "seq": 2,
                "tick": 1,
                "data": {"tick_budget_ms": 20},
            },
            {
                "schema": "mbt.evt.v1",
                "contract_version": "1.0.0",
                "type": "vla_submit",
                "run_id": "trace-validator-smoke",
                "unix_ms": 1735689600002,
                "seq": 3,
                "tick": 1,
                "data": {"job_id": "job-1", "node_id": 4, "status": "submitted"},
            },
            {
                "schema": "mbt.evt.v1",
                "contract_version": "1.0.0",
                "type": "async_cancel_acknowledged",
                "run_id": "trace-validator-smoke",
                "unix_ms": 1735689600003,
                "seq": 4,
                "tick": 1,
                "data": {"job_id": "job-1", "node_id": 4, "accepted": True, "reason": "manual_cancel"},
            },
            {
                "schema": "mbt.evt.v1",
                "contract_version": "1.0.0",
                "type": "tick_end",
                "run_id": "trace-validator-smoke",
                "unix_ms": 1735689600004,
                "seq": 5,
                "tick": 1,
                "data": {"root_status": "failure", "tick_ms": 1.0, "tick_budget_ms": 20.0},
            },
        ]
        write_events(ack_without_request_trace, ack_without_request_events)
        completed = run_cli("check", str(ack_without_request_trace))
        assert_exit(completed, 1, "async cancel ack without request check")
        if "async_cancel_ack_without_request" not in completed.stdout:
            raise AssertionError("async cancel ack without request check should mention async_cancel_ack_without_request")

        terminal_without_submit_trace = tmp / "terminal_without_submit.jsonl"
        terminal_without_submit_events = [
            make_run_start(),
            {
                "schema": "mbt.evt.v1",
                "contract_version": "1.0.0",
                "type": "tick_begin",
                "run_id": "trace-validator-smoke",
                "unix_ms": 1735689600001,
                "seq": 2,
                "tick": 1,
                "data": {"tick_budget_ms": 20},
            },
            {
                "schema": "mbt.evt.v1",
                "contract_version": "1.0.0",
                "type": "vla_poll",
                "run_id": "trace-validator-smoke",
                "unix_ms": 1735689600002,
                "seq": 3,
                "tick": 1,
                "data": {"job_id": "job-2", "node_id": 7, "status": "cancelled"},
            },
            {
                "schema": "mbt.evt.v1",
                "contract_version": "1.0.0",
                "type": "tick_end",
                "run_id": "trace-validator-smoke",
                "unix_ms": 1735689600003,
                "seq": 4,
                "tick": 1,
                "data": {"root_status": "failure", "tick_ms": 1.0, "tick_budget_ms": 20.0},
            },
        ]
        write_events(terminal_without_submit_trace, terminal_without_submit_events)
        completed = run_cli("check", str(terminal_without_submit_trace))
        assert_exit(completed, 1, "async terminal without submit check")
        if "async_terminal_without_submit" not in completed.stdout:
            raise AssertionError("async terminal without submit check should mention async_terminal_without_submit")

        timing_only_trace = tmp / "timing_only.jsonl"
        timing_only_events = load_events(FIXTURE)
        for index, event in enumerate(timing_only_events, start=1):
            event["unix_ms"] = 2000000000000 + index
            if event["type"] == "node_exit":
                event["data"]["dur_ms"] = 99.9
            if event["type"] == "tick_end":
                event["data"]["tick_ms"] = 77.7
        write_events(timing_only_trace, timing_only_events)
        completed = run_cli("compare", str(FIXTURE), str(timing_only_trace), "--profile", "deterministic")
        assert_ok(completed, "deterministic comparison")
        if "MATCH:" not in completed.stdout:
            raise AssertionError("deterministic comparison should report MATCH")

        mismatch_trace = tmp / "mismatch.jsonl"
        mismatch_events = load_events(FIXTURE)
        mismatch_events[4]["data"]["status"] = "failed"
        write_events(mismatch_trace, mismatch_events)
        completed = run_cli("compare", str(FIXTURE), str(mismatch_trace), "--profile", "deterministic")
        assert_exit(completed, 3, "comparison mismatch")
        if "event_mismatch" not in completed.stdout:
            raise AssertionError("comparison mismatch should report event_mismatch")

        batch_report = tmp / "batch_report.json"
        completed = run_cli("batch", str(FIXTURE), str(duplicate_seq_trace), "--report", str(batch_report))
        assert_exit(completed, 1, "batch mode")
        batch_payload = json.loads(batch_report.read_text(encoding="utf-8"))
        if batch_payload["summary"]["trace_count"] != 2:
            raise AssertionError("batch report should include two traces")
        if batch_payload["summary"]["failed_count"] != 1:
            raise AssertionError("batch report should include one failing trace")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
