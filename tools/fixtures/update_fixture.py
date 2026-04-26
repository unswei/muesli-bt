#!/usr/bin/env python3
"""Update deterministic runtime-contract fixture bundles with provenance."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import subprocess
from datetime import datetime, timezone


CONTRACT_VERSION = "1.0.0"
CONTRACT_ID = "runtime-contract-v1.0.0"
SCHEMA_NAME = "mbt.evt.v1"
SCHEMA_PATH = "schemas/event_log/v1/mbt.evt.v1.schema.json"
FIXTURE_ROOT = pathlib.Path("fixtures")


def event(
    *,
    event_type: str,
    run_id: str,
    unix_ms: int,
    seq: int,
    data: dict,
    tick: int | None = None,
) -> dict:
    out = {
        "schema": SCHEMA_NAME,
        "contract_version": CONTRACT_VERSION,
        "type": event_type,
        "run_id": run_id,
        "unix_ms": unix_ms,
        "seq": seq,
        "data": data,
    }
    if tick is not None:
        out["tick"] = tick
    return out


def git_sha() -> str:
    try:
        return (
            subprocess.check_output(["git", "rev-parse", "HEAD"], text=True, stderr=subprocess.DEVNULL).strip()
        )
    except Exception:
        return "unknown"


def git_commit_time_utc_iso8601() -> str:
    try:
        epoch = int(
            subprocess.check_output(
                ["git", "show", "-s", "--format=%ct", "HEAD"],
                text=True,
                stderr=subprocess.DEVNULL,
            ).strip()
        )
        return datetime.fromtimestamp(epoch, timezone.utc).isoformat().replace("+00:00", "Z")
    except Exception:
        return "unknown"


def fixture_definitions() -> dict[str, dict]:
    return {
        "budget-warning-case": {
            "config": {
                "scenario": "planner_decision_blocked_on_budget",
                "tick_budget_ms": 2,
                "deterministic_mode": True,
            },
            "seed": {"planner_seed": 424242, "vla_seed": 0, "run_seed": 11},
            "events": [
                event(
                    event_type="run_start",
                    run_id="fixture-budget-warning",
                    unix_ms=1735689605000,
                    seq=1,
                    data={
                        "git_sha": "fixture",
                        "host": {"name": "muesli-bt", "version": "0.1.0", "platform": "linux"},
                        "contract_version": CONTRACT_VERSION,
                        "contract_id": CONTRACT_ID,
                        "tick_hz": 50.0,
                        "tree_hash": "fnv1a64:0101010101010101",
                        "capabilities": {"reset": True},
                    },
                ),
                event(
                    event_type="tick_begin",
                    run_id="fixture-budget-warning",
                    unix_ms=1735689605001,
                    seq=2,
                    tick=1,
                    data={"tick_budget_ms": 2},
                ),
                event(
                    event_type="budget_warning",
                    run_id="fixture-budget-warning",
                    unix_ms=1735689605002,
                    seq=3,
                    tick=1,
                    data={
                        "decision_point": "planner_call_start",
                        "remaining_ms": -3.0,
                        "threshold_ms": 0.25,
                        "reason": "insufficient_budget",
                        "node_id": 2,
                    },
                ),
                event(
                    event_type="tick_end",
                    run_id="fixture-budget-warning",
                    unix_ms=1735689605003,
                    seq=4,
                    tick=1,
                    data={"root_status": "failure", "tick_ms": 5.0, "tick_budget_ms": 2.0},
                ),
            ],
            "expected_metrics": {
                "event_count": 4,
                "required_types": ["run_start", "tick_begin", "budget_warning", "tick_end"],
                "absent_types": ["planner_call_start"],
            },
        },
        "deadline-cancel-case": {
            "config": {
                "scenario": "deadline_overrun_cancels_active_async_jobs",
                "tick_budget_ms": 2,
                "deterministic_mode": True,
            },
            "seed": {"planner_seed": 424242, "vla_seed": 99, "run_seed": 12},
            "events": [
                event(
                    event_type="run_start",
                    run_id="fixture-deadline-cancel",
                    unix_ms=1735689605100,
                    seq=1,
                    data={
                        "git_sha": "fixture",
                        "host": {"name": "muesli-bt", "version": "0.1.0", "platform": "linux"},
                        "contract_version": CONTRACT_VERSION,
                        "contract_id": CONTRACT_ID,
                        "tick_hz": 50.0,
                        "tree_hash": "fnv1a64:0202020202020202",
                        "capabilities": {"reset": True},
                    },
                ),
                event(
                    event_type="tick_begin",
                    run_id="fixture-deadline-cancel",
                    unix_ms=1735689605101,
                    seq=2,
                    tick=2,
                    data={"tick_budget_ms": 2},
                ),
                event(
                    event_type="vla_submit",
                    run_id="fixture-deadline-cancel",
                    unix_ms=1735689605102,
                    seq=3,
                    tick=2,
                    data={"job_id": "17", "node_id": 4, "status": "submitted"},
                ),
                event(
                    event_type="deadline_exceeded",
                    run_id="fixture-deadline-cancel",
                    unix_ms=1735689605103,
                    seq=4,
                    tick=2,
                    data={"source": "tick_end", "tick_budget_ms": 2.0, "tick_elapsed_ms": 8.0},
                ),
                event(
                    event_type="async_cancel_requested",
                    run_id="fixture-deadline-cancel",
                    unix_ms=1735689605104,
                    seq=5,
                    tick=2,
                    data={"job_id": "17", "node_id": 4, "reason": "tick_deadline_exceeded"},
                ),
                event(
                    event_type="async_cancel_acknowledged",
                    run_id="fixture-deadline-cancel",
                    unix_ms=1735689605105,
                    seq=6,
                    tick=2,
                    data={
                        "job_id": "17",
                        "node_id": 4,
                        "accepted": True,
                        "reason": "tick_deadline_exceeded",
                    },
                ),
                event(
                    event_type="tick_end",
                    run_id="fixture-deadline-cancel",
                    unix_ms=1735689605106,
                    seq=7,
                    tick=2,
                    data={"root_status": "failure", "tick_ms": 8.0, "tick_budget_ms": 2.0},
                ),
            ],
            "expected_metrics": {
                "event_count": 7,
                "required_types": [
                    "run_start",
                    "tick_begin",
                    "vla_submit",
                    "deadline_exceeded",
                    "async_cancel_requested",
                    "async_cancel_acknowledged",
                    "tick_end",
                ],
            },
        },
        "late-completion-drop-case": {
            "config": {
                "scenario": "cancelled_job_late_completion_is_dropped",
                "tick_budget_ms": 20,
                "deterministic_mode": True,
            },
            "seed": {"planner_seed": 424242, "vla_seed": 123, "run_seed": 14},
            "events": [
                event(
                    event_type="run_start",
                    run_id="fixture-late-completion-drop",
                    unix_ms=1735689605150,
                    seq=1,
                    data={
                        "git_sha": "fixture",
                        "host": {"name": "muesli-bt", "version": "0.1.0", "platform": "linux"},
                        "contract_version": CONTRACT_VERSION,
                        "contract_id": CONTRACT_ID,
                        "tick_hz": 50.0,
                        "tree_hash": "fnv1a64:0202020202020203",
                        "capabilities": {"reset": True},
                    },
                ),
                event(
                    event_type="tick_begin",
                    run_id="fixture-late-completion-drop",
                    unix_ms=1735689605151,
                    seq=2,
                    tick=3,
                    data={"tick_budget_ms": 20},
                ),
                event(
                    event_type="vla_submit",
                    run_id="fixture-late-completion-drop",
                    unix_ms=1735689605152,
                    seq=3,
                    tick=3,
                    data={"job_id": "22", "node_id": 5, "status": "submitted"},
                ),
                event(
                    event_type="async_cancel_requested",
                    run_id="fixture-late-completion-drop",
                    unix_ms=1735689605153,
                    seq=4,
                    tick=4,
                    data={"job_id": "22", "node_id": 5, "reason": "manual_cancel"},
                ),
                event(
                    event_type="async_cancel_acknowledged",
                    run_id="fixture-late-completion-drop",
                    unix_ms=1735689605154,
                    seq=5,
                    tick=4,
                    data={
                        "job_id": "22",
                        "node_id": 5,
                        "accepted": True,
                        "reason": "manual_cancel",
                    },
                ),
                event(
                    event_type="vla_poll",
                    run_id="fixture-late-completion-drop",
                    unix_ms=1735689605155,
                    seq=6,
                    tick=5,
                    data={"job_id": "22", "node_id": 5, "status": "cancelled"},
                ),
                event(
                    event_type="async_completion_dropped",
                    run_id="fixture-late-completion-drop",
                    unix_ms=1735689605156,
                    seq=7,
                    tick=6,
                    data={
                        "job_id": "22",
                        "node_id": 5,
                        "reason": "cancelled_before_completion",
                        "status": "done",
                    },
                ),
                event(
                    event_type="tick_end",
                    run_id="fixture-late-completion-drop",
                    unix_ms=1735689605157,
                    seq=8,
                    tick=6,
                    data={"root_status": "failure", "tick_ms": 3.0, "tick_budget_ms": 20.0},
                ),
            ],
            "expected_metrics": {
                "event_count": 8,
                "required_types": [
                    "run_start",
                    "tick_begin",
                    "vla_submit",
                    "async_cancel_requested",
                    "async_cancel_acknowledged",
                    "vla_poll",
                    "async_completion_dropped",
                    "tick_end",
                ],
                "absent_types": ["vla_result"],
            },
        },
        "async-cancel-before-start-case": {
            "config": {
                "scenario": "async_cancel_before_start_is_rejected_without_submit",
                "tick_budget_ms": 20,
                "deterministic_mode": True,
            },
            "seed": {"planner_seed": 424242, "vla_seed": 140, "run_seed": 40},
            "events": [
                event(
                    event_type="run_start",
                    run_id="fixture-async-cancel-before-start",
                    unix_ms=1735689605160,
                    seq=1,
                    data={
                        "git_sha": "fixture",
                        "host": {"name": "muesli-bt", "version": "0.7.0-dev", "platform": "linux"},
                        "contract_version": CONTRACT_VERSION,
                        "contract_id": CONTRACT_ID,
                        "tick_hz": 50.0,
                        "tree_hash": "fnv1a64:0808080808080801",
                        "capabilities": {"reset": True},
                    },
                ),
                event(
                    event_type="tick_begin",
                    run_id="fixture-async-cancel-before-start",
                    unix_ms=1735689605161,
                    seq=2,
                    tick=1,
                    data={"tick_budget_ms": 20},
                ),
                event(
                    event_type="async_cancel_requested",
                    run_id="fixture-async-cancel-before-start",
                    unix_ms=1735689605162,
                    seq=3,
                    tick=1,
                    data={"job_id": "not-started", "node_id": 7, "reason": "cancel_before_start"},
                ),
                event(
                    event_type="async_cancel_acknowledged",
                    run_id="fixture-async-cancel-before-start",
                    unix_ms=1735689605163,
                    seq=4,
                    tick=1,
                    data={
                        "job_id": "not-started",
                        "node_id": 7,
                        "accepted": False,
                        "reason": "no_active_job",
                    },
                ),
                event(
                    event_type="tick_end",
                    run_id="fixture-async-cancel-before-start",
                    unix_ms=1735689605164,
                    seq=5,
                    tick=1,
                    data={"root_status": "failure", "tick_ms": 0.5, "tick_budget_ms": 20.0},
                ),
            ],
            "expected_metrics": {
                "event_count": 5,
                "required_types": [
                    "run_start",
                    "tick_begin",
                    "async_cancel_requested",
                    "async_cancel_acknowledged",
                    "tick_end",
                ],
                "absent_types": ["vla_submit", "vla_result", "async_completion_dropped"],
                "type_counts": {
                    "async_cancel_requested": 1,
                    "async_cancel_acknowledged": 1,
                },
            },
        },
        "async-cancel-while-running-case": {
            "config": {
                "scenario": "async_cancel_while_running_reaches_cancelled_terminal_state",
                "tick_budget_ms": 20,
                "deterministic_mode": True,
            },
            "seed": {"planner_seed": 424242, "vla_seed": 141, "run_seed": 41},
            "events": [
                event(
                    event_type="run_start",
                    run_id="fixture-async-cancel-while-running",
                    unix_ms=1735689605170,
                    seq=1,
                    data={
                        "git_sha": "fixture",
                        "host": {"name": "muesli-bt", "version": "0.7.0-dev", "platform": "linux"},
                        "contract_version": CONTRACT_VERSION,
                        "contract_id": CONTRACT_ID,
                        "tick_hz": 50.0,
                        "tree_hash": "fnv1a64:0808080808080802",
                        "capabilities": {"reset": True},
                    },
                ),
                event(
                    event_type="tick_begin",
                    run_id="fixture-async-cancel-while-running",
                    unix_ms=1735689605171,
                    seq=2,
                    tick=1,
                    data={"tick_budget_ms": 20},
                ),
                event(
                    event_type="vla_submit",
                    run_id="fixture-async-cancel-while-running",
                    unix_ms=1735689605172,
                    seq=3,
                    tick=1,
                    data={"job_id": "31", "node_id": 7, "status": "submitted"},
                ),
                event(
                    event_type="vla_poll",
                    run_id="fixture-async-cancel-while-running",
                    unix_ms=1735689605173,
                    seq=4,
                    tick=1,
                    data={"job_id": "31", "node_id": 7, "status": "running"},
                ),
                event(
                    event_type="tick_end",
                    run_id="fixture-async-cancel-while-running",
                    unix_ms=1735689605174,
                    seq=5,
                    tick=1,
                    data={"root_status": "running", "tick_ms": 1.0, "tick_budget_ms": 20.0},
                ),
                event(
                    event_type="tick_begin",
                    run_id="fixture-async-cancel-while-running",
                    unix_ms=1735689605175,
                    seq=6,
                    tick=2,
                    data={"tick_budget_ms": 20},
                ),
                event(
                    event_type="async_cancel_requested",
                    run_id="fixture-async-cancel-while-running",
                    unix_ms=1735689605176,
                    seq=7,
                    tick=2,
                    data={"job_id": "31", "node_id": 7, "reason": "manual_cancel"},
                ),
                event(
                    event_type="async_cancel_acknowledged",
                    run_id="fixture-async-cancel-while-running",
                    unix_ms=1735689605177,
                    seq=8,
                    tick=2,
                    data={"job_id": "31", "node_id": 7, "accepted": True, "reason": "manual_cancel"},
                ),
                event(
                    event_type="vla_poll",
                    run_id="fixture-async-cancel-while-running",
                    unix_ms=1735689605178,
                    seq=9,
                    tick=2,
                    data={"job_id": "31", "node_id": 7, "status": "cancelled"},
                ),
                event(
                    event_type="tick_end",
                    run_id="fixture-async-cancel-while-running",
                    unix_ms=1735689605179,
                    seq=10,
                    tick=2,
                    data={"root_status": "failure", "tick_ms": 0.9, "tick_budget_ms": 20.0},
                ),
            ],
            "expected_metrics": {
                "event_count": 10,
                "required_types": [
                    "run_start",
                    "tick_begin",
                    "vla_submit",
                    "vla_poll",
                    "async_cancel_requested",
                    "async_cancel_acknowledged",
                    "tick_end",
                ],
                "absent_types": ["vla_result", "async_completion_dropped"],
                "type_counts": {
                    "vla_submit": 1,
                    "vla_poll": 2,
                    "async_cancel_requested": 1,
                    "async_cancel_acknowledged": 1,
                },
            },
        },
        "async-cancel-after-timeout-case": {
            "config": {
                "scenario": "async_cancel_after_timeout_is_idempotent_rejection",
                "tick_budget_ms": 20,
                "deterministic_mode": True,
            },
            "seed": {"planner_seed": 424242, "vla_seed": 142, "run_seed": 42},
            "events": [
                event(
                    event_type="run_start",
                    run_id="fixture-async-cancel-after-timeout",
                    unix_ms=1735689605180,
                    seq=1,
                    data={
                        "git_sha": "fixture",
                        "host": {"name": "muesli-bt", "version": "0.7.0-dev", "platform": "linux"},
                        "contract_version": CONTRACT_VERSION,
                        "contract_id": CONTRACT_ID,
                        "tick_hz": 50.0,
                        "tree_hash": "fnv1a64:0808080808080803",
                        "capabilities": {"reset": True},
                    },
                ),
                event(
                    event_type="tick_begin",
                    run_id="fixture-async-cancel-after-timeout",
                    unix_ms=1735689605181,
                    seq=2,
                    tick=1,
                    data={"tick_budget_ms": 20},
                ),
                event(
                    event_type="vla_submit",
                    run_id="fixture-async-cancel-after-timeout",
                    unix_ms=1735689605182,
                    seq=3,
                    tick=1,
                    data={"job_id": "32", "node_id": 8, "status": "submitted"},
                ),
                event(
                    event_type="vla_poll",
                    run_id="fixture-async-cancel-after-timeout",
                    unix_ms=1735689605183,
                    seq=4,
                    tick=1,
                    data={"job_id": "32", "node_id": 8, "status": "timeout"},
                ),
                event(
                    event_type="deadline_exceeded",
                    run_id="fixture-async-cancel-after-timeout",
                    unix_ms=1735689605184,
                    seq=5,
                    tick=1,
                    data={"source": "vla_poll", "tick_budget_ms": 20.0, "tick_elapsed_ms": 20.2},
                ),
                event(
                    event_type="tick_end",
                    run_id="fixture-async-cancel-after-timeout",
                    unix_ms=1735689605185,
                    seq=6,
                    tick=1,
                    data={"root_status": "failure", "tick_ms": 20.2, "tick_budget_ms": 20.0},
                ),
                event(
                    event_type="tick_begin",
                    run_id="fixture-async-cancel-after-timeout",
                    unix_ms=1735689605186,
                    seq=7,
                    tick=2,
                    data={"tick_budget_ms": 20},
                ),
                event(
                    event_type="async_cancel_requested",
                    run_id="fixture-async-cancel-after-timeout",
                    unix_ms=1735689605187,
                    seq=8,
                    tick=2,
                    data={"job_id": "32", "node_id": 8, "reason": "manual_cancel_after_timeout"},
                ),
                event(
                    event_type="async_cancel_acknowledged",
                    run_id="fixture-async-cancel-after-timeout",
                    unix_ms=1735689605188,
                    seq=9,
                    tick=2,
                    data={
                        "job_id": "32",
                        "node_id": 8,
                        "accepted": False,
                        "reason": "already_timeout",
                    },
                ),
                event(
                    event_type="tick_end",
                    run_id="fixture-async-cancel-after-timeout",
                    unix_ms=1735689605189,
                    seq=10,
                    tick=2,
                    data={"root_status": "failure", "tick_ms": 0.4, "tick_budget_ms": 20.0},
                ),
            ],
            "expected_metrics": {
                "event_count": 10,
                "required_types": [
                    "run_start",
                    "tick_begin",
                    "vla_submit",
                    "vla_poll",
                    "deadline_exceeded",
                    "async_cancel_requested",
                    "async_cancel_acknowledged",
                    "tick_end",
                ],
                "absent_types": ["vla_result", "async_completion_dropped"],
                "type_counts": {
                    "vla_submit": 1,
                    "vla_poll": 1,
                    "deadline_exceeded": 1,
                    "async_cancel_requested": 1,
                    "async_cancel_acknowledged": 1,
                },
            },
        },
        "async-repeated-cancel-case": {
            "config": {
                "scenario": "async_repeated_cancel_is_idempotent",
                "tick_budget_ms": 20,
                "deterministic_mode": True,
            },
            "seed": {"planner_seed": 424242, "vla_seed": 143, "run_seed": 43},
            "events": [
                event(
                    event_type="run_start",
                    run_id="fixture-async-repeated-cancel",
                    unix_ms=1735689605190,
                    seq=1,
                    data={
                        "git_sha": "fixture",
                        "host": {"name": "muesli-bt", "version": "0.7.0-dev", "platform": "linux"},
                        "contract_version": CONTRACT_VERSION,
                        "contract_id": CONTRACT_ID,
                        "tick_hz": 50.0,
                        "tree_hash": "fnv1a64:0808080808080804",
                        "capabilities": {"reset": True},
                    },
                ),
                event(
                    event_type="tick_begin",
                    run_id="fixture-async-repeated-cancel",
                    unix_ms=1735689605191,
                    seq=2,
                    tick=1,
                    data={"tick_budget_ms": 20},
                ),
                event(
                    event_type="vla_submit",
                    run_id="fixture-async-repeated-cancel",
                    unix_ms=1735689605192,
                    seq=3,
                    tick=1,
                    data={"job_id": "33", "node_id": 9, "status": "submitted"},
                ),
                event(
                    event_type="async_cancel_requested",
                    run_id="fixture-async-repeated-cancel",
                    unix_ms=1735689605193,
                    seq=4,
                    tick=1,
                    data={"job_id": "33", "node_id": 9, "reason": "manual_cancel"},
                ),
                event(
                    event_type="async_cancel_acknowledged",
                    run_id="fixture-async-repeated-cancel",
                    unix_ms=1735689605194,
                    seq=5,
                    tick=1,
                    data={"job_id": "33", "node_id": 9, "accepted": True, "reason": "manual_cancel"},
                ),
                event(
                    event_type="async_cancel_requested",
                    run_id="fixture-async-repeated-cancel",
                    unix_ms=1735689605195,
                    seq=6,
                    tick=1,
                    data={"job_id": "33", "node_id": 9, "reason": "manual_cancel_repeated"},
                ),
                event(
                    event_type="async_cancel_acknowledged",
                    run_id="fixture-async-repeated-cancel",
                    unix_ms=1735689605196,
                    seq=7,
                    tick=1,
                    data={"job_id": "33", "node_id": 9, "accepted": False, "reason": "already_cancelled"},
                ),
                event(
                    event_type="vla_poll",
                    run_id="fixture-async-repeated-cancel",
                    unix_ms=1735689605197,
                    seq=8,
                    tick=1,
                    data={"job_id": "33", "node_id": 9, "status": "cancelled"},
                ),
                event(
                    event_type="tick_end",
                    run_id="fixture-async-repeated-cancel",
                    unix_ms=1735689605198,
                    seq=9,
                    tick=1,
                    data={"root_status": "failure", "tick_ms": 0.9, "tick_budget_ms": 20.0},
                ),
            ],
            "expected_metrics": {
                "event_count": 9,
                "required_types": [
                    "run_start",
                    "tick_begin",
                    "vla_submit",
                    "async_cancel_requested",
                    "async_cancel_acknowledged",
                    "vla_poll",
                    "tick_end",
                ],
                "absent_types": ["vla_result", "async_completion_dropped"],
                "type_counts": {
                    "vla_submit": 1,
                    "vla_poll": 1,
                    "async_cancel_requested": 2,
                    "async_cancel_acknowledged": 2,
                },
            },
        },
        "async-late-completion-after-cancel-case": {
            "config": {
                "scenario": "async_late_completion_after_cancel_is_dropped",
                "tick_budget_ms": 20,
                "deterministic_mode": True,
            },
            "seed": {"planner_seed": 424242, "vla_seed": 144, "run_seed": 44},
            "events": [
                event(
                    event_type="run_start",
                    run_id="fixture-async-late-completion-after-cancel",
                    unix_ms=1735689605220,
                    seq=1,
                    data={
                        "git_sha": "fixture",
                        "host": {"name": "muesli-bt", "version": "0.7.0-dev", "platform": "linux"},
                        "contract_version": CONTRACT_VERSION,
                        "contract_id": CONTRACT_ID,
                        "tick_hz": 50.0,
                        "tree_hash": "fnv1a64:0808080808080805",
                        "capabilities": {"reset": True},
                    },
                ),
                event(
                    event_type="tick_begin",
                    run_id="fixture-async-late-completion-after-cancel",
                    unix_ms=1735689605221,
                    seq=2,
                    tick=1,
                    data={"tick_budget_ms": 20},
                ),
                event(
                    event_type="vla_submit",
                    run_id="fixture-async-late-completion-after-cancel",
                    unix_ms=1735689605222,
                    seq=3,
                    tick=1,
                    data={"job_id": "34", "node_id": 10, "status": "submitted"},
                ),
                event(
                    event_type="async_cancel_requested",
                    run_id="fixture-async-late-completion-after-cancel",
                    unix_ms=1735689605223,
                    seq=4,
                    tick=1,
                    data={"job_id": "34", "node_id": 10, "reason": "manual_cancel"},
                ),
                event(
                    event_type="async_cancel_acknowledged",
                    run_id="fixture-async-late-completion-after-cancel",
                    unix_ms=1735689605224,
                    seq=5,
                    tick=1,
                    data={"job_id": "34", "node_id": 10, "accepted": True, "reason": "manual_cancel"},
                ),
                event(
                    event_type="tick_end",
                    run_id="fixture-async-late-completion-after-cancel",
                    unix_ms=1735689605225,
                    seq=6,
                    tick=1,
                    data={"root_status": "failure", "tick_ms": 0.8, "tick_budget_ms": 20.0},
                ),
                event(
                    event_type="tick_begin",
                    run_id="fixture-async-late-completion-after-cancel",
                    unix_ms=1735689605226,
                    seq=7,
                    tick=2,
                    data={"tick_budget_ms": 20},
                ),
                event(
                    event_type="vla_poll",
                    run_id="fixture-async-late-completion-after-cancel",
                    unix_ms=1735689605227,
                    seq=8,
                    tick=2,
                    data={"job_id": "34", "node_id": 10, "status": "cancelled"},
                ),
                event(
                    event_type="async_completion_dropped",
                    run_id="fixture-async-late-completion-after-cancel",
                    unix_ms=1735689605228,
                    seq=9,
                    tick=2,
                    data={"job_id": "34", "node_id": 10, "reason": "cancelled_before_completion", "status": "done"},
                ),
                event(
                    event_type="tick_end",
                    run_id="fixture-async-late-completion-after-cancel",
                    unix_ms=1735689605229,
                    seq=10,
                    tick=2,
                    data={"root_status": "failure", "tick_ms": 1.1, "tick_budget_ms": 20.0},
                ),
            ],
            "expected_metrics": {
                "event_count": 10,
                "required_types": [
                    "run_start",
                    "tick_begin",
                    "vla_submit",
                    "async_cancel_requested",
                    "async_cancel_acknowledged",
                    "vla_poll",
                    "async_completion_dropped",
                    "tick_end",
                ],
                "absent_types": ["vla_result"],
                "type_counts": {
                    "vla_submit": 1,
                    "vla_poll": 1,
                    "async_cancel_requested": 1,
                    "async_cancel_acknowledged": 1,
                    "async_completion_dropped": 1,
                },
            },
        },
        "determinism-replay-case": {
            "config": {
                "scenario": "deterministic_trace_replay",
                "tick_budget_ms": 20,
                "deterministic_mode": True,
            },
            "seed": {"planner_seed": 424242, "vla_seed": 0, "run_seed": 13},
            "events": [
                event(
                    event_type="run_start",
                    run_id="fixture-determinism-replay",
                    unix_ms=1735689605200,
                    seq=1,
                    data={
                        "git_sha": "fixture",
                        "host": {"name": "muesli-bt", "version": "0.1.0", "platform": "linux"},
                        "contract_version": CONTRACT_VERSION,
                        "contract_id": CONTRACT_ID,
                        "tick_hz": 20.0,
                        "tree_hash": "fnv1a64:0303030303030303",
                        "capabilities": {"reset": True},
                    },
                ),
                event(
                    event_type="tick_begin",
                    run_id="fixture-determinism-replay",
                    unix_ms=1735689605201,
                    seq=2,
                    tick=1,
                    data={"tick_budget_ms": 20},
                ),
                event(
                    event_type="node_enter",
                    run_id="fixture-determinism-replay",
                    unix_ms=1735689605202,
                    seq=3,
                    tick=1,
                    data={"node_id": 1},
                ),
                event(
                    event_type="planner_call_start",
                    run_id="fixture-determinism-replay",
                    unix_ms=1735689605203,
                    seq=4,
                    tick=1,
                    data={"node_id": 2, "planner": "mcts", "budget_ms": 12},
                ),
                event(
                    event_type="planner_call_end",
                    run_id="fixture-determinism-replay",
                    unix_ms=1735689605204,
                    seq=5,
                    tick=1,
                    data={"node_id": 2, "planner": "mcts", "status": "ok", "time_used_ms": 2.0, "work_done": 64},
                ),
                event(
                    event_type="node_exit",
                    run_id="fixture-determinism-replay",
                    unix_ms=1735689605205,
                    seq=6,
                    tick=1,
                    data={"node_id": 1, "status": "running", "dur_ms": 2.2},
                ),
                event(
                    event_type="tick_end",
                    run_id="fixture-determinism-replay",
                    unix_ms=1735689605206,
                    seq=7,
                    tick=1,
                    data={"root_status": "running", "tick_ms": 2.3, "tick_budget_ms": 20.0},
                ),
                event(
                    event_type="tick_begin",
                    run_id="fixture-determinism-replay",
                    unix_ms=1735689605207,
                    seq=8,
                    tick=2,
                    data={"tick_budget_ms": 20},
                ),
                event(
                    event_type="node_enter",
                    run_id="fixture-determinism-replay",
                    unix_ms=1735689605208,
                    seq=9,
                    tick=2,
                    data={"node_id": 1},
                ),
                event(
                    event_type="planner_call_start",
                    run_id="fixture-determinism-replay",
                    unix_ms=1735689605209,
                    seq=10,
                    tick=2,
                    data={"node_id": 2, "planner": "mcts", "budget_ms": 12},
                ),
                event(
                    event_type="planner_call_end",
                    run_id="fixture-determinism-replay",
                    unix_ms=1735689605210,
                    seq=11,
                    tick=2,
                    data={"node_id": 2, "planner": "mcts", "status": "ok", "time_used_ms": 2.0, "work_done": 64},
                ),
                event(
                    event_type="node_exit",
                    run_id="fixture-determinism-replay",
                    unix_ms=1735689605211,
                    seq=12,
                    tick=2,
                    data={"node_id": 1, "status": "success", "dur_ms": 2.1},
                ),
                event(
                    event_type="tick_end",
                    run_id="fixture-determinism-replay",
                    unix_ms=1735689605212,
                    seq=13,
                    tick=2,
                    data={"root_status": "success", "tick_ms": 2.2, "tick_budget_ms": 20.0},
                ),
            ],
            "expected_metrics": {
                "event_count": 13,
                "required_types": [
                    "run_start",
                    "planner_call_start",
                    "planner_call_end",
                    "node_enter",
                    "node_exit",
                    "tick_begin",
                    "tick_end",
                ],
                "node_status_sequence": ["1:running", "1:success"],
            },
        },
        "ros2-observe-act-step-case": {
            "config": {
                "scenario": "ros2_latest_sample_observe_act_step",
                "backend": "ros2",
                "control_hz": 50,
                "obs_source": "odom",
                "action_sink": "cmd_vel",
                "reset_mode": "stub",
                "deterministic_mode": True,
            },
            "seed": {"planner_seed": 0, "vla_seed": 0, "run_seed": 31},
            "events": [
                event(
                    event_type="run_start",
                    run_id="fixture-ros2-observe-act-step",
                    unix_ms=1735689605300,
                    seq=1,
                    data={
                        "git_sha": "fixture",
                        "host": {"name": "muesli-bt", "version": "0.2.0", "platform": "linux"},
                        "contract_version": CONTRACT_VERSION,
                        "contract_id": CONTRACT_ID,
                        "tick_hz": 50.0,
                        "tree_hash": "fnv1a64:0404040404040404",
                        "capabilities": {"reset": True, "backend": "ros2"},
                    },
                ),
                event(
                    event_type="tick_begin",
                    run_id="fixture-ros2-observe-act-step",
                    unix_ms=1735689605301,
                    seq=2,
                    tick=1,
                    data={"tick_budget_ms": 20},
                ),
                event(
                    event_type="bb_write",
                    run_id="fixture-ros2-observe-act-step",
                    unix_ms=1735689605302,
                    seq=3,
                    tick=1,
                    data={
                        "key": "obs",
                        "preview": {
                            "obs_schema": "ros2.obs.v1",
                            "state_schema": "ros2.state.v1",
                            "source": "odom",
                        },
                    },
                ),
                event(
                    event_type="node_enter",
                    run_id="fixture-ros2-observe-act-step",
                    unix_ms=1735689605303,
                    seq=4,
                    tick=1,
                    data={"node_id": 1},
                ),
                event(
                    event_type="bb_write",
                    run_id="fixture-ros2-observe-act-step",
                    unix_ms=1735689605304,
                    seq=5,
                    tick=1,
                    data={
                        "key": "action",
                        "preview": {
                            "action_schema": "ros2.action.v1",
                            "u": {"linear_x": 0.1, "linear_y": 0.0, "angular_z": 0.2},
                        },
                    },
                ),
                event(
                    event_type="node_exit",
                    run_id="fixture-ros2-observe-act-step",
                    unix_ms=1735689605305,
                    seq=6,
                    tick=1,
                    data={"node_id": 1, "status": "success", "dur_ms": 1.4},
                ),
                event(
                    event_type="tick_end",
                    run_id="fixture-ros2-observe-act-step",
                    unix_ms=1735689605306,
                    seq=7,
                    tick=1,
                    data={"root_status": "success", "tick_ms": 1.8, "tick_budget_ms": 20.0},
                ),
            ],
            "expected_metrics": {
                "event_count": 7,
                "required_types": ["run_start", "tick_begin", "bb_write", "node_enter", "node_exit", "tick_end"],
                "absent_types": ["error"],
                "node_status_sequence": ["1:success"],
            },
        },
        "ros2-invalid-action-fallback-case": {
            "config": {
                "scenario": "ros2_invalid_action_uses_safe_fallback",
                "backend": "ros2",
                "action_clamp": "reject",
                "reset_mode": "stub",
                "deterministic_mode": True,
            },
            "seed": {"planner_seed": 0, "vla_seed": 0, "run_seed": 32},
            "events": [
                event(
                    event_type="run_start",
                    run_id="fixture-ros2-invalid-action-fallback",
                    unix_ms=1735689605310,
                    seq=1,
                    data={
                        "git_sha": "fixture",
                        "host": {"name": "muesli-bt", "version": "0.2.0", "platform": "linux"},
                        "contract_version": CONTRACT_VERSION,
                        "contract_id": CONTRACT_ID,
                        "tick_hz": 50.0,
                        "tree_hash": "fnv1a64:0505050505050505",
                        "capabilities": {"reset": True, "backend": "ros2"},
                    },
                ),
                event(
                    event_type="tick_begin",
                    run_id="fixture-ros2-invalid-action-fallback",
                    unix_ms=1735689605311,
                    seq=2,
                    tick=1,
                    data={"tick_budget_ms": 20},
                ),
                event(
                    event_type="error",
                    run_id="fixture-ros2-invalid-action-fallback",
                    unix_ms=1735689605312,
                    seq=3,
                    tick=1,
                    data={
                        "stage": "env.act",
                        "code": "invalid_action",
                        "message": "env.act: missing action key 'u'",
                    },
                ),
                event(
                    event_type="bb_write",
                    run_id="fixture-ros2-invalid-action-fallback",
                    unix_ms=1735689605313,
                    seq=4,
                    tick=1,
                    data={
                        "key": "action",
                        "preview": {
                            "action_schema": "ros2.action.v1",
                            "source": "safe_action",
                            "u": {"linear_x": 0.0, "linear_y": 0.0, "angular_z": 0.0},
                        },
                    },
                ),
                event(
                    event_type="tick_end",
                    run_id="fixture-ros2-invalid-action-fallback",
                    unix_ms=1735689605314,
                    seq=5,
                    tick=1,
                    data={"root_status": "failure", "tick_ms": 1.2, "tick_budget_ms": 20.0},
                ),
            ],
            "expected_metrics": {
                "event_count": 5,
                "required_types": ["run_start", "tick_begin", "error", "bb_write", "tick_end"],
                "absent_types": ["planner_call_start"],
            },
        },
        "ros2-reset-unsupported-case": {
            "config": {
                "scenario": "ros2_multi_episode_requires_reset",
                "backend": "ros2",
                "episode_max": 2,
                "reset_mode": "unsupported",
                "deterministic_mode": True,
            },
            "seed": {"planner_seed": 0, "vla_seed": 0, "run_seed": 33},
            "events": [
                event(
                    event_type="run_start",
                    run_id="fixture-ros2-reset-unsupported",
                    unix_ms=1735689605320,
                    seq=1,
                    data={
                        "git_sha": "fixture",
                        "host": {"name": "muesli-bt", "version": "0.2.0", "platform": "linux"},
                        "contract_version": CONTRACT_VERSION,
                        "contract_id": CONTRACT_ID,
                        "tick_hz": 50.0,
                        "tree_hash": "fnv1a64:0606060606060606",
                        "capabilities": {"reset": False, "backend": "ros2"},
                    },
                ),
                event(
                    event_type="error",
                    run_id="fixture-ros2-reset-unsupported",
                    unix_ms=1735689605321,
                    seq=2,
                    data={
                        "stage": "env.run-loop",
                        "code": "unsupported",
                        "message": "episode_max>1 requires env.reset capability",
                    },
                ),
            ],
            "expected_metrics": {
                "event_count": 2,
                "required_types": ["run_start", "error"],
                "absent_types": ["tick_begin"],
            },
        },
        "ros2-deadline-fallback-case": {
            "config": {
                "scenario": "ros2_deadline_overrun_uses_safe_fallback",
                "backend": "ros2",
                "tick_budget_ms": 20,
                "reset_mode": "stub",
                "deterministic_mode": True,
            },
            "seed": {"planner_seed": 0, "vla_seed": 0, "run_seed": 34},
            "events": [
                event(
                    event_type="run_start",
                    run_id="fixture-ros2-deadline-fallback",
                    unix_ms=1735689605330,
                    seq=1,
                    data={
                        "git_sha": "fixture",
                        "host": {"name": "muesli-bt", "version": "0.2.0", "platform": "linux"},
                        "contract_version": CONTRACT_VERSION,
                        "contract_id": CONTRACT_ID,
                        "tick_hz": 50.0,
                        "tree_hash": "fnv1a64:0707070707070707",
                        "capabilities": {"reset": True, "backend": "ros2"},
                    },
                ),
                event(
                    event_type="tick_begin",
                    run_id="fixture-ros2-deadline-fallback",
                    unix_ms=1735689605331,
                    seq=2,
                    tick=1,
                    data={"tick_budget_ms": 20},
                ),
                event(
                    event_type="deadline_exceeded",
                    run_id="fixture-ros2-deadline-fallback",
                    unix_ms=1735689605332,
                    seq=3,
                    tick=1,
                    data={"source": "tick_end", "tick_budget_ms": 20.0, "tick_elapsed_ms": 35.0},
                ),
                event(
                    event_type="bb_write",
                    run_id="fixture-ros2-deadline-fallback",
                    unix_ms=1735689605333,
                    seq=4,
                    tick=1,
                    data={
                        "key": "action",
                        "preview": {
                            "action_schema": "ros2.action.v1",
                            "source": "safe_action",
                            "u": {"linear_x": 0.0, "linear_y": 0.0, "angular_z": 0.0},
                        },
                    },
                ),
                event(
                    event_type="tick_end",
                    run_id="fixture-ros2-deadline-fallback",
                    unix_ms=1735689605334,
                    seq=5,
                    tick=1,
                    data={"root_status": "failure", "tick_ms": 35.0, "tick_budget_ms": 20.0},
                ),
            ],
            "expected_metrics": {
                "event_count": 5,
                "required_types": ["run_start", "tick_begin", "deadline_exceeded", "bb_write", "tick_end"],
                "absent_types": ["planner_call_start"],
            },
        },
    }


def write_json(path: pathlib.Path, payload: dict) -> None:
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def write_events(path: pathlib.Path, events: list[dict]) -> str:
    lines = [json.dumps(ev, separators=(",", ":"), sort_keys=False) for ev in events]
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    digest = hashlib.sha256()
    digest.update(("\n".join(lines) + "\n").encode("utf-8"))
    return digest.hexdigest()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Update deterministic fixture bundles.")
    parser.add_argument(
        "--fixture",
        action="append",
        default=[],
        help="Optional fixture name filter. May be passed multiple times.",
    )
    parser.add_argument(
        "--refresh-provenance",
        action="store_true",
        help="Refresh manifest provenance fields (git_sha and generated_from_commit_time_utc) from current HEAD.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    definitions = fixture_definitions()
    selected = set(args.fixture)
    if selected:
        unknown = selected - set(definitions)
        if unknown:
            raise SystemExit(f"unknown fixture(s): {', '.join(sorted(unknown))}")

    FIXTURE_ROOT.mkdir(parents=True, exist_ok=True)
    sha = git_sha()
    commit_time = git_commit_time_utc_iso8601()

    for name, definition in definitions.items():
        if selected and name not in selected:
            continue

        fixture_dir = FIXTURE_ROOT / name
        fixture_dir.mkdir(parents=True, exist_ok=True)

        write_json(fixture_dir / "config.json", definition["config"])
        write_json(fixture_dir / "seed.json", definition["seed"])
        digest = write_events(fixture_dir / "events.jsonl", definition["events"])

        expected = dict(definition["expected_metrics"])
        expected["events_sha256"] = digest
        write_json(fixture_dir / "expected_metrics.json", expected)

        manifest_path = fixture_dir / "manifest.json"
        existing_manifest: dict[str, object] = {}
        if manifest_path.exists():
            try:
                loaded = json.loads(manifest_path.read_text(encoding="utf-8"))
                if isinstance(loaded, dict):
                    existing_manifest = loaded
            except Exception:
                existing_manifest = {}

        if args.refresh_provenance:
            manifest_git_sha = sha
            manifest_commit_time = commit_time
        else:
            manifest_git_sha = str(existing_manifest.get("git_sha", sha))
            manifest_commit_time = str(existing_manifest.get("generated_from_commit_time_utc", commit_time))

        manifest = {
            "fixture_name": name,
            "schema": SCHEMA_NAME,
            "schema_path": SCHEMA_PATH,
            "contract_version": CONTRACT_VERSION,
            "contract_id": CONTRACT_ID,
            "generator": "tools/fixtures/update_fixture.py",
            "generated_from_commit_time_utc": manifest_commit_time,
            "git_sha": manifest_git_sha,
            "provenance_model": "deterministic-from-git",
        }
        write_json(manifest_path, manifest)

        print(f"updated {fixture_dir}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
