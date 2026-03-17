#!/usr/bin/env python3
"""Trace-level validator for canonical mbt.evt.v1 event streams."""

from __future__ import annotations

import json
import pathlib
from collections import Counter
from dataclasses import dataclass, field
from typing import Any, Iterable

try:
    import tomllib
except ModuleNotFoundError:  # pragma: no cover - Python < 3.11 fallback
    tomllib = None  # type: ignore[assignment]


VALIDATOR_VERSION = "0.1.0"
DEFAULT_EVENT_LOG_NAME = "events.jsonl"
DEFAULT_CONTEXT_WINDOW = 2

ROOT_DROP_FIELDS = {"seq", "unix_ms", "run_id"}
TIMING_DROP_FIELDS = {
    "data.tick_ms",
    "data.dur_ms",
    "data.time_used_ms",
    "data.tick_elapsed_ms",
    "data.remaining_ms",
    "data.threshold_ms",
    "data.obs_t_ms",
    "data.budget.tick_time_ms",
    "data.budget.tick_elapsed_ms",
}

PROFILE_PRESETS: dict[str, dict[str, Any]] = {
    "strict_runtime": {
        "tolerate_incomplete_tail": False,
        "ignore_event_types": [],
        "drop_fields": [],
    },
    "deterministic": {
        "tolerate_incomplete_tail": False,
        "ignore_event_types": [],
        "drop_fields": sorted(ROOT_DROP_FIELDS | TIMING_DROP_FIELDS),
    },
    "cross_backend": {
        "tolerate_incomplete_tail": False,
        "ignore_event_types": ["run_start", "run_end", "episode_begin", "episode_end", "bt_def"],
        "drop_fields": sorted(
            ROOT_DROP_FIELDS
            | TIMING_DROP_FIELDS
            | {
                "data.git_sha",
                "data.host",
                "data.capabilities",
            }
        ),
    },
    "ci_smoke": {
        "tolerate_incomplete_tail": False,
        "ignore_event_types": [],
        "drop_fields": sorted(ROOT_DROP_FIELDS | TIMING_DROP_FIELDS),
    },
}

TERMINAL_NODE_STATUSES = {"success", "failure"}
TERMINAL_VLA_STATUSES = {"done", "cancelled", "error", "timeout"}


class UsageError(RuntimeError):
    """Raised when CLI input is invalid."""


@dataclass(slots=True)
class TraceEvent:
    file_name: str
    line_no: int
    raw: dict[str, Any]
    seq: int | None
    event_type: str
    tick: int | None
    node_id: int | None
    status: str | None


@dataclass(slots=True)
class Violation:
    code: str
    severity: str
    file_name: str
    message: str
    line_no: int | None = None
    seq: int | None = None
    tick: int | None = None
    context: dict[str, Any] | None = None

    def to_dict(self) -> dict[str, Any]:
        payload = {
            "code": self.code,
            "severity": self.severity,
            "file_name": self.file_name,
            "message": self.message,
        }
        if self.line_no is not None:
            payload["line_no"] = self.line_no
        if self.seq is not None:
            payload["seq"] = self.seq
        if self.tick is not None:
            payload["tick"] = self.tick
        if self.context is not None:
            payload["context"] = self.context
        return payload


@dataclass(slots=True)
class CheckConfig:
    profile: str = "strict_runtime"
    tolerate_incomplete_tail: bool = False
    ignore_event_types: tuple[str, ...] = ()
    drop_fields: tuple[str, ...] = ()

    def to_dict(self) -> dict[str, Any]:
        return {
            "profile": self.profile,
            "tolerate_incomplete_tail": self.tolerate_incomplete_tail,
            "ignore_event_types": list(self.ignore_event_types),
            "drop_fields": list(self.drop_fields),
        }


@dataclass(slots=True)
class ValidationReport:
    validator_version: str
    mode: str
    input_files: list[str]
    passed: bool
    trace_truncated: bool
    total_events: int
    completed_ticks: int
    distinct_nodes: int
    async_jobs: int
    first_seq: int | None
    last_seq: int | None
    normalisation: dict[str, Any]
    violations: list[Violation] = field(default_factory=list)

    def violation_counts(self) -> dict[str, int]:
        counts: Counter[str] = Counter()
        for violation in self.violations:
            counts[violation.code] += 1
        return dict(sorted(counts.items()))

    def first_violation_by_type(self) -> dict[str, dict[str, Any]]:
        first: dict[str, dict[str, Any]] = {}
        for violation in self.violations:
            if violation.code not in first:
                first[violation.code] = violation.to_dict()
        return first

    def error_count(self) -> int:
        return sum(1 for violation in self.violations if violation.severity == "error")

    def to_dict(self) -> dict[str, Any]:
        return {
            "validator_version": self.validator_version,
            "mode": self.mode,
            "input_files": self.input_files,
            "passed": self.passed,
            "trace_truncated": self.trace_truncated,
            "summary": {
                "total_events": self.total_events,
                "completed_ticks": self.completed_ticks,
                "distinct_nodes": self.distinct_nodes,
                "async_jobs": self.async_jobs,
                "first_seq": self.first_seq,
                "last_seq": self.last_seq,
            },
            "violation_counts": self.violation_counts(),
            "first_violation_by_type": self.first_violation_by_type(),
            "violations": [violation.to_dict() for violation in self.violations],
            "normalisation": self.normalisation,
        }


@dataclass(slots=True)
class ComparisonReport:
    validator_version: str
    mode: str
    input_files: list[str]
    matched: bool
    normalisation: dict[str, Any]
    event_count_a: int
    event_count_b: int
    first_divergence: dict[str, Any] | None = None
    mismatch_code: str | None = None
    violations: list[Violation] = field(default_factory=list)

    def to_dict(self) -> dict[str, Any]:
        counts: Counter[str] = Counter(v.code for v in self.violations)
        first: dict[str, dict[str, Any]] = {}
        for violation in self.violations:
            if violation.code not in first:
                first[violation.code] = violation.to_dict()
        return {
            "validator_version": self.validator_version,
            "mode": self.mode,
            "input_files": self.input_files,
            "matched": self.matched,
            "summary": {
                "event_count_a": self.event_count_a,
                "event_count_b": self.event_count_b,
            },
            "violation_counts": dict(sorted(counts.items())),
            "first_violation_by_type": first,
            "violations": [violation.to_dict() for violation in self.violations],
            "normalisation": self.normalisation,
            "comparison": {
                "mismatch_code": self.mismatch_code,
                "first_divergence": self.first_divergence,
            },
        }


def resolve_log_path(path_arg: str) -> pathlib.Path:
    path = pathlib.Path(path_arg)
    if path.is_dir():
        return path / DEFAULT_EVENT_LOG_NAME
    return path


def load_config(path: str | None, profile: str) -> CheckConfig:
    if profile not in PROFILE_PRESETS:
        raise UsageError(f"unknown profile: {profile}")

    merged = dict(PROFILE_PRESETS[profile])
    merged["profile"] = profile

    if path is not None:
        if tomllib is None:
            raise UsageError("TOML config requires Python 3.11 or newer")
        config_path = pathlib.Path(path)
        if not config_path.is_file():
            raise UsageError(f"config file not found: {config_path}")
        loaded = tomllib.loads(config_path.read_text(encoding="utf-8"))
        validator_cfg = loaded.get("validator", {})
        normalisation_cfg = loaded.get("normalisation", {})
        if not isinstance(validator_cfg, dict) or not isinstance(normalisation_cfg, dict):
            raise UsageError(f"invalid validator config format: {config_path}")

        if "tolerate_incomplete_tail" in validator_cfg:
            merged["tolerate_incomplete_tail"] = bool(validator_cfg["tolerate_incomplete_tail"])
        if "ignore_event_types" in normalisation_cfg:
            merged["ignore_event_types"] = list(normalisation_cfg["ignore_event_types"])
        if "drop_fields" in normalisation_cfg:
            merged["drop_fields"] = list(normalisation_cfg["drop_fields"])

    return CheckConfig(
        profile=str(merged["profile"]),
        tolerate_incomplete_tail=bool(merged.get("tolerate_incomplete_tail", False)),
        ignore_event_types=tuple(str(item) for item in merged.get("ignore_event_types", [])),
        drop_fields=tuple(str(item) for item in merged.get("drop_fields", [])),
    )


def load_trace(path_arg: str) -> list[TraceEvent]:
    log_path = resolve_log_path(path_arg)
    if not log_path.is_file():
        raise UsageError(f"log file not found: {log_path}")

    events: list[TraceEvent] = []
    with log_path.open("r", encoding="utf-8") as handle:
        for line_no, line in enumerate(handle, start=1):
            text = line.strip()
            if not text:
                continue
            try:
                raw = json.loads(text)
            except json.JSONDecodeError as exc:
                raise UsageError(f"{log_path}:{line_no}: invalid JSON: {exc}") from exc
            data = raw.get("data")
            node_id: int | None = None
            status: str | None = None
            if isinstance(data, dict):
                raw_node = data.get("node_id")
                if isinstance(raw_node, int):
                    node_id = raw_node
                raw_status = data.get("status")
                if isinstance(raw_status, str):
                    status = raw_status
                elif isinstance(data.get("root_status"), str):
                    status = str(data["root_status"])
            seq = raw.get("seq")
            tick = raw.get("tick")
            events.append(
                TraceEvent(
                    file_name=str(log_path),
                    line_no=line_no,
                    raw=raw,
                    seq=seq if isinstance(seq, int) else None,
                    event_type=str(raw.get("type", "")),
                    tick=tick if isinstance(tick, int) else None,
                    node_id=node_id,
                    status=status,
                )
            )
    return events


def _event_context(event: TraceEvent) -> dict[str, Any]:
    return {
        "line_no": event.line_no,
        "seq": event.seq,
        "type": event.event_type,
        "tick": event.tick,
        "node_id": event.node_id,
    }


def _event_data(event: TraceEvent) -> dict[str, Any]:
    data = event.raw.get("data")
    if isinstance(data, dict):
        return data
    return {}


def _job_id(event: TraceEvent) -> str | None:
    raw_job = _event_data(event).get("job_id")
    if raw_job is None:
        return None
    return str(raw_job)


def _bool_field(event: TraceEvent, key: str) -> bool | None:
    value = _event_data(event).get(key)
    if isinstance(value, bool):
        return value
    return None


def _number_from_payload(payload: dict[str, Any], *path: str) -> float | None:
    current: Any = payload
    for part in path:
        if not isinstance(current, dict) or part not in current:
            return None
        current = current[part]
    if isinstance(current, (int, float)) and not isinstance(current, bool):
        return float(current)
    return None


def _tick_budget_ms(event: TraceEvent) -> float | None:
    data = _event_data(event)
    direct = _number_from_payload(data, "tick_budget_ms")
    if direct is not None:
        return direct
    return _number_from_payload(data, "budget", "tick_budget_ms")


def _tick_elapsed_ms(event: TraceEvent) -> float | None:
    data = _event_data(event)
    for path in (
        ("tick_ms",),
        ("tick_time_ms",),
        ("tick_elapsed_ms",),
        ("budget", "tick_time_ms"),
        ("budget", "tick_elapsed_ms"),
    ):
        value = _number_from_payload(data, *path)
        if value is not None:
            return value
    return None


def validate_trace(trace_path: str, config: CheckConfig) -> ValidationReport:
    events = load_trace(trace_path)
    violations: list[Violation] = []
    seen_seq: set[int] = set()
    terminal_exit_counts: Counter[tuple[int, int]] = Counter()
    distinct_nodes: set[int] = set()
    async_jobs_seen: set[str] = set()
    current_tick: dict[str, Any] | None = None
    completed_ticks = 0
    completed_tick_ids: set[int] = set()
    trace_truncated = False
    last_seq: int | None = None
    first_seq: int | None = None
    tick_records: dict[int, dict[str, Any]] = {}
    active_jobs: dict[str, dict[str, Any]] = {}
    deadline_snapshots: list[dict[str, Any]] = []
    async_state: dict[str, dict[str, Any]] = {}

    for event in events:
        if event.node_id is not None:
            distinct_nodes.add(event.node_id)
        job_id = _job_id(event)
        if job_id is not None:
            async_jobs_seen.add(job_id)

        if event.seq is None:
            violations.append(
                Violation(
                    code="missing_seq",
                    severity="error",
                    file_name=event.file_name,
                    line_no=event.line_no,
                    tick=event.tick,
                    message="event is missing seq",
                    context=_event_context(event),
                )
            )
        else:
            if first_seq is None:
                first_seq = event.seq
            if event.seq in seen_seq:
                violations.append(
                    Violation(
                        code="duplicate_seq",
                        severity="error",
                        file_name=event.file_name,
                        line_no=event.line_no,
                        seq=event.seq,
                        tick=event.tick,
                        message=f"duplicate seq {event.seq}",
                        context=_event_context(event),
                    )
                )
            else:
                seen_seq.add(event.seq)
            if last_seq is not None and event.seq <= last_seq:
                violations.append(
                    Violation(
                        code="non_monotonic_seq",
                        severity="error",
                        file_name=event.file_name,
                        line_no=event.line_no,
                        seq=event.seq,
                        tick=event.tick,
                        message=f"seq {event.seq} is not greater than previous seq {last_seq}",
                        context={"previous_seq": last_seq, **_event_context(event)},
                    )
                )
            last_seq = event.seq

        if event.event_type == "tick_begin":
            if event.tick is None:
                violations.append(
                    Violation(
                        code="unexpected_missing_tick_id",
                        severity="error",
                        file_name=event.file_name,
                        line_no=event.line_no,
                        seq=event.seq,
                        message="tick_begin is missing tick",
                        context=_event_context(event),
                    )
                )
                continue
            if current_tick is not None:
                code = "duplicate_tick_begin" if current_tick["tick"] == event.tick else "overlapping_ticks"
                violations.append(
                    Violation(
                        code=code,
                        severity="error",
                        file_name=event.file_name,
                        line_no=event.line_no,
                        seq=event.seq,
                        tick=event.tick,
                        message=f"tick_begin for tick {event.tick} arrived before tick {current_tick['tick']} closed",
                        context={"open_tick": current_tick["tick"], **_event_context(event)},
                    )
                )
            if event.tick in completed_tick_ids:
                violations.append(
                    Violation(
                        code="duplicate_tick_id",
                        severity="error",
                        file_name=event.file_name,
                        line_no=event.line_no,
                        seq=event.seq,
                        tick=event.tick,
                        message=f"tick {event.tick} was already completed earlier in the trace",
                        context=_event_context(event),
                    )
                )
            current_tick = {
                "tick": event.tick,
                "begin_line": event.line_no,
                "begin_seq": event.seq,
            }
            tick_records[event.tick] = {
                "tick": event.tick,
                "begin": event,
                "end": None,
                "events": [event],
            }
            continue

        if event.event_type == "tick_end":
            if event.tick is None:
                violations.append(
                    Violation(
                        code="unexpected_missing_tick_id",
                        severity="error",
                        file_name=event.file_name,
                        line_no=event.line_no,
                        seq=event.seq,
                        message="tick_end is missing tick",
                        context=_event_context(event),
                    )
                )
                continue
            if current_tick is None:
                code = "duplicate_tick_end" if event.tick in completed_tick_ids else "missing_tick_begin"
                violations.append(
                    Violation(
                        code=code,
                        severity="error",
                        file_name=event.file_name,
                        line_no=event.line_no,
                        seq=event.seq,
                        tick=event.tick,
                        message=f"tick_end for tick {event.tick} does not have a matching open tick_begin",
                        context=_event_context(event),
                    )
                )
                continue
            if current_tick["tick"] != event.tick:
                violations.append(
                    Violation(
                        code="tick_event_outside_delimiters",
                        severity="error",
                        file_name=event.file_name,
                        line_no=event.line_no,
                        seq=event.seq,
                        tick=event.tick,
                        message=f"tick_end for tick {event.tick} closed while tick {current_tick['tick']} was open",
                        context={"open_tick": current_tick["tick"], **_event_context(event)},
                    )
                )
                continue
            completed_tick_ids.add(event.tick)
            completed_ticks += 1
            if event.tick in tick_records:
                tick_records[event.tick]["events"].append(event)
                tick_records[event.tick]["end"] = event
            current_tick = None
            continue

        if event.tick is not None:
            if current_tick is None:
                violations.append(
                    Violation(
                        code="tick_event_outside_delimiters",
                        severity="error",
                        file_name=event.file_name,
                        line_no=event.line_no,
                        seq=event.seq,
                        tick=event.tick,
                        message=f"event type {event.event_type} for tick {event.tick} occurred outside tick delimiters",
                        context=_event_context(event),
                    )
                )
            elif current_tick["tick"] != event.tick:
                violations.append(
                    Violation(
                        code="tick_event_outside_delimiters",
                        severity="error",
                        file_name=event.file_name,
                        line_no=event.line_no,
                        seq=event.seq,
                        tick=event.tick,
                        message=f"event type {event.event_type} for tick {event.tick} occurred inside tick {current_tick['tick']}",
                        context={"open_tick": current_tick["tick"], **_event_context(event)},
                    )
                )
            elif event.tick in tick_records:
                tick_records[event.tick]["events"].append(event)

        if event.event_type == "node_exit" and event.tick is not None and event.node_id is not None:
            if event.status in TERMINAL_NODE_STATUSES:
                key = (event.tick, event.node_id)
                terminal_exit_counts[key] += 1
                if terminal_exit_counts[key] > 1:
                    violations.append(
                        Violation(
                            code="duplicate_terminal_node_exit",
                            severity="error",
                            file_name=event.file_name,
                            line_no=event.line_no,
                            seq=event.seq,
                            tick=event.tick,
                            message=f"duplicate terminal node_exit for node {event.node_id} in tick {event.tick}",
                            context={"node_id": event.node_id, "status": event.status, **_event_context(event)},
                        )
                    )

        if event.event_type == "vla_submit" and job_id is not None:
            active_jobs[job_id] = {
                "submit_seq": event.seq,
                "submit_line": event.line_no,
                "tick": event.tick,
            }
            async_state[job_id] = {
                "submit_seq": event.seq,
                "submit_line": event.line_no,
                "cancel_request_seq": None,
                "cancel_ack_seq": None,
                "terminal_seq": None,
            }
        elif event.event_type == "deadline_exceeded" and event.tick is not None:
            deadline_snapshots.append(
                {
                    "tick": event.tick,
                    "seq": event.seq,
                    "line_no": event.line_no,
                    "active_jobs": set(active_jobs.keys()),
                }
            )
        elif event.event_type == "async_cancel_requested" and job_id is not None:
            state = async_state.setdefault(
                job_id,
                {
                    "submit_seq": None,
                    "submit_line": None,
                    "cancel_request_seq": None,
                    "cancel_ack_seq": None,
                    "terminal_seq": None,
                },
            )
            state["cancel_request_seq"] = event.seq
        elif event.event_type == "async_cancel_acknowledged" and job_id is not None:
            state = async_state.setdefault(
                job_id,
                {
                    "submit_seq": None,
                    "submit_line": None,
                    "cancel_request_seq": None,
                    "cancel_ack_seq": None,
                    "terminal_seq": None,
                },
            )
            if state["cancel_request_seq"] is None:
                violations.append(
                    Violation(
                        code="async_cancel_ack_without_request",
                        severity="error",
                        file_name=event.file_name,
                        line_no=event.line_no,
                        seq=event.seq,
                        tick=event.tick,
                        message=f"async_cancel_acknowledged for job {job_id} occurred before async_cancel_requested",
                        context={"job_id": job_id, **_event_context(event)},
                    )
                )
            state["cancel_ack_seq"] = event.seq
            accepted = _bool_field(event, "accepted")
            if accepted is True:
                active_jobs.pop(job_id, None)
        elif event.event_type == "vla_poll" and job_id is not None and event.status in TERMINAL_VLA_STATUSES:
            state = async_state.setdefault(
                job_id,
                {
                    "submit_seq": None,
                    "submit_line": None,
                    "cancel_request_seq": None,
                    "cancel_ack_seq": None,
                    "terminal_seq": None,
                },
            )
            if state["submit_seq"] is None:
                violations.append(
                    Violation(
                        code="async_terminal_without_submit",
                        severity="error",
                        file_name=event.file_name,
                        line_no=event.line_no,
                        seq=event.seq,
                        tick=event.tick,
                        message=f"terminal async event {event.event_type} for job {job_id} occurred before vla_submit",
                        context={"job_id": job_id, "status": event.status, **_event_context(event)},
                    )
                )
            state["terminal_seq"] = event.seq
            active_jobs.pop(job_id, None)
        elif event.event_type in {"vla_result", "async_completion_dropped"} and job_id is not None:
            state = async_state.setdefault(
                job_id,
                {
                    "submit_seq": None,
                    "submit_line": None,
                    "cancel_request_seq": None,
                    "cancel_ack_seq": None,
                    "terminal_seq": None,
                },
            )
            if state["submit_seq"] is None:
                violations.append(
                    Violation(
                        code="async_terminal_without_submit",
                        severity="error",
                        file_name=event.file_name,
                        line_no=event.line_no,
                        seq=event.seq,
                        tick=event.tick,
                        message=f"terminal async event {event.event_type} for job {job_id} occurred before vla_submit",
                        context={"job_id": job_id, **_event_context(event)},
                    )
                )
            state["terminal_seq"] = event.seq
            active_jobs.pop(job_id, None)

    if current_tick is not None:
        if config.tolerate_incomplete_tail:
            trace_truncated = True
        else:
            violations.append(
                Violation(
                    code="missing_tick_end",
                    severity="error",
                    file_name=str(resolve_log_path(trace_path)),
                    line_no=current_tick["begin_line"],
                    seq=current_tick["begin_seq"],
                    tick=current_tick["tick"],
                    message=f"tick {current_tick['tick']} started but never emitted tick_end",
                    context=current_tick,
                )
            )

    for tick_id in sorted(completed_tick_ids):
        record = tick_records.get(tick_id)
        if record is None or record.get("end") is None:
            continue
        tick_events: list[TraceEvent] = record["events"]
        tick_end: TraceEvent = record["end"]
        tick_budget_ms = _tick_budget_ms(tick_end)
        tick_elapsed_ms = _tick_elapsed_ms(tick_end)
        has_deadline = any(item.event_type == "deadline_exceeded" for item in tick_events)
        over_budget = (
            tick_budget_ms is not None
            and tick_elapsed_ms is not None
            and tick_elapsed_ms > tick_budget_ms
        )

        if tick_budget_ms is None or tick_elapsed_ms is None:
            if not has_deadline:
                violations.append(
                    Violation(
                        code="budget_check_not_evaluable",
                        severity="info",
                        file_name=tick_end.file_name,
                        line_no=tick_end.line_no,
                        seq=tick_end.seq,
                        tick=tick_id,
                        message=f"tick {tick_id} does not expose enough budget timing fields for overrun validation",
                        context=_event_context(tick_end),
                    )
                )
            continue

        if not over_budget:
            continue

        if not has_deadline:
            violations.append(
                Violation(
                    code="missing_deadline_exceeded",
                    severity="error",
                    file_name=tick_end.file_name,
                    line_no=tick_end.line_no,
                    seq=tick_end.seq,
                    tick=tick_id,
                    message=f"over-budget tick {tick_id} is missing deadline_exceeded",
                    context={
                        "tick_budget_ms": tick_budget_ms,
                        "tick_elapsed_ms": tick_elapsed_ms,
                        **_event_context(tick_end),
                    },
                )
            )

    for snapshot in deadline_snapshots:
        tick_id = snapshot["tick"]
        if tick_id not in completed_tick_ids:
            continue
        record = tick_records.get(tick_id)
        if record is None:
            continue
        tick_end = record.get("end")
        if tick_end is None:
            continue
        tick_budget_ms = _tick_budget_ms(tick_end)
        tick_elapsed_ms = _tick_elapsed_ms(tick_end)
        over_budget = (
            tick_budget_ms is None
            or tick_elapsed_ms is None
            or tick_elapsed_ms > tick_budget_ms
        )
        if not over_budget:
            continue
        active_jobs_at_deadline: set[str] = snapshot["active_jobs"]
        if not active_jobs_at_deadline:
            continue
        cancel_requests_after_deadline = {
            _job_id(event)
            for event in record["events"]
            if event.event_type == "async_cancel_requested"
            and event.seq is not None
            and snapshot["seq"] is not None
            and event.seq > snapshot["seq"]
            and _job_id(event) is not None
        }
        missing_jobs = sorted(active_jobs_at_deadline - cancel_requests_after_deadline)
        if missing_jobs:
            violations.append(
                Violation(
                    code="missing_cancel_request_after_deadline",
                    severity="error",
                    file_name=tick_end.file_name,
                    line_no=snapshot["line_no"],
                    seq=snapshot["seq"],
                    tick=tick_id,
                    message=f"deadline_exceeded in tick {tick_id} is missing async_cancel_requested for active job(s): {', '.join(missing_jobs)}",
                    context={
                        "active_jobs_at_deadline": missing_jobs,
                        "tick_end_seq": tick_end.seq,
                    },
                )
            )

    passed = not any(violation.severity == "error" for violation in violations)
    return ValidationReport(
        validator_version=VALIDATOR_VERSION,
        mode="check",
        input_files=[str(resolve_log_path(trace_path))],
        passed=passed,
        trace_truncated=trace_truncated,
        total_events=len(events),
        completed_ticks=completed_ticks,
        distinct_nodes=len(distinct_nodes),
        async_jobs=len(async_jobs_seen),
        first_seq=first_seq,
        last_seq=last_seq,
        normalisation=config.to_dict(),
        violations=violations,
    )


def _drop_path(payload: Any, path_parts: list[str]) -> None:
    if not path_parts or not isinstance(payload, dict):
        return
    head = path_parts[0]
    if len(path_parts) == 1:
        payload.pop(head, None)
        return
    child = payload.get(head)
    if isinstance(child, dict):
        _drop_path(child, path_parts[1:])
        if not child:
            payload.pop(head, None)


def normalise_event(event: TraceEvent, config: CheckConfig) -> dict[str, Any] | None:
    if event.event_type in set(config.ignore_event_types):
        return None

    payload = json.loads(json.dumps(event.raw))
    for field_path in config.drop_fields:
        _drop_path(payload, field_path.split("."))
    return payload


def normalise_trace(trace_path: str, config: CheckConfig) -> list[dict[str, Any]]:
    normalised: list[dict[str, Any]] = []
    for event in load_trace(trace_path):
        payload = normalise_event(event, config)
        if payload is not None:
            normalised.append(payload)
    return normalised


def _context_window(events: list[dict[str, Any]], index: int) -> dict[str, Any]:
    start = max(0, index - DEFAULT_CONTEXT_WINDOW)
    end = min(len(events), index + DEFAULT_CONTEXT_WINDOW + 1)
    return {
        "start_index": start,
        "end_index": end,
        "events": events[start:end],
    }


def compare_traces(trace_a: str, trace_b: str, config: CheckConfig) -> ComparisonReport:
    normalised_a = normalise_trace(trace_a, config)
    normalised_b = normalise_trace(trace_b, config)
    violations: list[Violation] = []
    first_divergence: dict[str, Any] | None = None
    mismatch_code: str | None = None
    matched = True

    common = min(len(normalised_a), len(normalised_b))
    mismatch_index: int | None = None
    for index in range(common):
        if normalised_a[index] != normalised_b[index]:
            mismatch_index = index
            break

    if mismatch_index is not None:
        matched = False
        mismatch_code = "event_mismatch"
        first_divergence = {
            "event_index": mismatch_index,
            "event_a": normalised_a[mismatch_index],
            "event_b": normalised_b[mismatch_index],
            "context_a": _context_window(normalised_a, mismatch_index),
            "context_b": _context_window(normalised_b, mismatch_index),
        }
        violations.append(
            Violation(
                code="event_mismatch",
                severity="error",
                file_name=str(resolve_log_path(trace_a)),
                line_no=mismatch_index + 1,
                message=f"normalised events diverged at index {mismatch_index}",
                context=first_divergence,
            )
        )
    elif len(normalised_a) != len(normalised_b):
        matched = False
        mismatch_code = "trace_length_mismatch"
        first_divergence = {
            "event_index": common,
            "event_a": normalised_a[common] if common < len(normalised_a) else None,
            "event_b": normalised_b[common] if common < len(normalised_b) else None,
            "context_a": _context_window(normalised_a, min(common, max(len(normalised_a) - 1, 0))),
            "context_b": _context_window(normalised_b, min(common, max(len(normalised_b) - 1, 0))),
        }
        violations.append(
            Violation(
                code="trace_length_mismatch",
                severity="error",
                file_name=str(resolve_log_path(trace_a)),
                line_no=common + 1,
                message=f"normalised traces differ in length: {len(normalised_a)} != {len(normalised_b)}",
                context=first_divergence,
            )
        )

    return ComparisonReport(
        validator_version=VALIDATOR_VERSION,
        mode="compare",
        input_files=[str(resolve_log_path(trace_a)), str(resolve_log_path(trace_b))],
        matched=matched,
        normalisation=config.to_dict(),
        event_count_a=len(normalised_a),
        event_count_b=len(normalised_b),
        first_divergence=first_divergence,
        mismatch_code=mismatch_code,
        violations=violations,
    )


def batch_validate(trace_paths: Iterable[str], config: CheckConfig) -> dict[str, Any]:
    reports = [validate_trace(path, config) for path in trace_paths]
    counts: Counter[str] = Counter()
    for report in reports:
        counts.update(report.violation_counts())
    passed = all(report.passed for report in reports)
    return {
        "validator_version": VALIDATOR_VERSION,
        "mode": "batch",
        "passed": passed,
        "input_files": [report.input_files[0] for report in reports],
        "summary": {
            "trace_count": len(reports),
            "passed_count": sum(1 for report in reports if report.passed),
            "failed_count": sum(1 for report in reports if not report.passed),
            "total_events": sum(report.total_events for report in reports),
            "completed_ticks": sum(report.completed_ticks for report in reports),
        },
        "violation_counts": dict(sorted(counts.items())),
        "normalisation": config.to_dict(),
        "reports": [report.to_dict() for report in reports],
    }
