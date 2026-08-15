"""Derive the humanoid video overlay from canonical ``mbt.evt.v1`` evidence."""

from __future__ import annotations

import dataclasses
import json
import math
import pathlib
from typing import Any


class EvidenceError(RuntimeError):
    """The canonical event stream cannot produce a trustworthy overlay."""


@dataclasses.dataclass
class OverlayState:
    active_branch: str = "-"
    job_id: str = "-"
    generation: str = "-"
    ball_context_id: str = "-"
    request_state: str = "idle"
    result_decision: str = "-"
    result_reason: str = "-"
    dispatch_decision: str = "-"
    dispatch_reason: str = "-"
    walking_target_state: str = "none"
    walking_target: str = "-"


def load_events(path: pathlib.Path) -> list[dict[str, Any]]:
    events: list[dict[str, Any]] = []
    for line_number, line in enumerate(
        path.read_text(encoding="utf-8").splitlines(), start=1
    ):
        if not line.strip():
            continue
        try:
            event = json.loads(line)
        except json.JSONDecodeError as exc:
            raise EvidenceError(f"{path}:{line_number}: invalid JSON: {exc}") from exc
        if not isinstance(event, dict) or event.get("schema") != "mbt.evt.v1":
            raise EvidenceError(f"{path}:{line_number}: event is not mbt.evt.v1")
        if not isinstance(event.get("seq"), int) or not isinstance(
            event.get("unix_ms"), int
        ):
            raise EvidenceError(f"{path}:{line_number}: event has no sequence or clock")
        if not isinstance(event.get("data"), dict):
            raise EvidenceError(f"{path}:{line_number}: event data is not an object")
        events.append(event)
    if not events:
        raise EvidenceError(f"empty event stream: {path}")
    sequences = [int(event["seq"]) for event in events]
    if sequences != list(range(sequences[0], sequences[0] + len(sequences))):
        raise EvidenceError("event sequence is not contiguous")
    clocks = [int(event["unix_ms"]) for event in events]
    if clocks != sorted(clocks):
        raise EvidenceError("event clock moves backwards")
    if events[0].get("type") != "run_start" or events[-1].get("type") != "run_end":
        raise EvidenceError(
            "event stream must begin with run_start and end with run_end"
        )
    return events


def _display(value: object) -> str:
    if value is None or value == "":
        return "-"
    if isinstance(value, list):
        return "[" + ", ".join(f"{float(item):.2f}" for item in value) + "]"
    return str(value).lower() if isinstance(value, bool) else str(value)


def apply_event(state: OverlayState, event: dict[str, Any]) -> bool:
    before = dataclasses.astuple(state)
    data = event["data"]
    event_type = event.get("type")
    if event_type == "vla_submit":
        state.job_id = _display(data.get("job_id"))
        state.generation = _display(data.get("generation"))
        state.ball_context_id = _display(data.get("captured_context_id"))
        state.request_state = "running"
        state.result_decision = "-"
        state.result_reason = "-"
        state.dispatch_decision = "-"
        state.dispatch_reason = "-"
    elif event_type == "vla_result" and "decision" in data:
        state.job_id = _display(data.get("job_id"))
        state.generation = _display(data.get("generation"))
        state.result_decision = _display(data.get("decision"))
        state.result_reason = _display(data.get("reason"))
        state.request_state = (
            "done" if data.get("decision") == "accepted" else "rejected"
        )
    elif event_type == "async_authority_revoked":
        state.job_id = _display(data.get("job_id"))
        state.generation = _display(data.get("generation"))
        state.result_decision = "rejected"
        state.result_reason = _display(data.get("reason"))
        state.request_state = "revoked"
        state.walking_target_state = "none"
        state.walking_target = "-"
    elif event_type == "walking_target_dispatch":
        state.dispatch_decision = _display(data.get("decision"))
        state.dispatch_reason = _display(data.get("reason"))

    if event_type == "bb_write":
        key = data.get("key")
        value = data.get("preview")
        if key == "active-branch":
            state.active_branch = _display(value)
        elif key == "ball-context-id":
            state.ball_context_id = _display(value)
        elif key == "request-state":
            state.request_state = _display(value)
        elif key == "result-decision":
            state.result_decision = _display(value)
        elif key == "result-reason":
            state.result_reason = _display(value)
        elif key == "dispatch-reason":
            state.dispatch_reason = _display(value)
        elif key == "walking-target-state":
            state.walking_target_state = _display(value)
            if value == "none":
                state.walking_target = "-"
        elif key in {"current-walking-target", "candidate-walking-target"}:
            state.walking_target = _display(value)
        elif key == "candidate-target-job-id":
            state.job_id = _display(value)
        elif key == "candidate-target-generation":
            state.generation = _display(value)
    return dataclasses.astuple(state) != before


def _ass_escape(value: str) -> str:
    return value.replace("\\", r"\\").replace("{", r"\{").replace("}", r"\}")


def _ass_time(milliseconds: int) -> str:
    centiseconds = max(0, milliseconds) // 10
    hours, remainder = divmod(centiseconds, 360000)
    minutes, remainder = divmod(remainder, 6000)
    seconds, centiseconds = divmod(remainder, 100)
    return f"{hours}:{minutes:02d}:{seconds:02d}.{centiseconds:02d}"


def _render_state(state: OverlayState) -> str:
    lines = [
        f"branch: {_ass_escape(state.active_branch)}",
        (
            f"request: {_ass_escape(state.request_state)}    "
            f"job: {_ass_escape(state.job_id)}    generation: {_ass_escape(state.generation)}"
        ),
        f"ball context: {_ass_escape(state.ball_context_id)}",
        (
            f"result: {_ass_escape(state.result_decision)}    "
            f"reason: {_ass_escape(state.result_reason)}"
        ),
        (
            f"dispatch: {_ass_escape(state.dispatch_decision)}    "
            f"reason: {_ass_escape(state.dispatch_reason)}"
        ),
    ]
    target = (
        f"walking target: {_ass_escape(state.walking_target)} "
        f"({_ass_escape(state.walking_target_state)})"
    )
    if state.walking_target_state == "current":
        target = r"{\c&H0000FF00&}" + target + r"{\c&H00FFFFFF&}"
    elif state.walking_target_state == "obsolete":
        target = r"{\c&H000000FF&}" + target + r"{\c&H00FFFFFF&}"
    lines.append(target)
    return r"\N".join(lines)


def generate_ass(
    events: list[dict[str, Any]], *, request_cue_seconds: float | None = None
) -> str:
    if request_cue_seconds is not None and (
        not math.isfinite(request_cue_seconds) or request_cue_seconds < 0.0
    ):
        raise EvidenceError("request cue time must be finite and non-negative")
    first_clock = int(events[0]["unix_ms"])
    offset_ms = 0
    if request_cue_seconds is not None:
        submissions = [event for event in events if event.get("type") == "vla_submit"]
        if len(submissions) != 1:
            raise EvidenceError(
                "request-cue alignment requires exactly one vla_submit event"
            )
        submission_relative = int(submissions[0]["unix_ms"]) - first_clock
        offset_ms = round(request_cue_seconds * 1000.0) - submission_relative

    state = OverlayState()
    changes: list[tuple[int, str]] = [(max(0, offset_ms), _render_state(state))]
    for event in events:
        if apply_event(state, event):
            timestamp = max(0, int(event["unix_ms"]) - first_clock + offset_ms)
            rendered = _render_state(state)
            if changes and timestamp == changes[-1][0]:
                changes[-1] = (timestamp, rendered)
            elif rendered != changes[-1][1]:
                changes.append((timestamp, rendered))
    final_time = max(
        changes[-1][0] + 1000,
        int(events[-1]["unix_ms"]) - first_clock + offset_ms + 1000,
    )
    header = """[Script Info]
ScriptType: v4.00+
PlayResX: 1920
PlayResY: 1080
WrapStyle: 2

[V4+ Styles]
Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding
Style: Evidence,DejaVu Sans Mono,34,&H00FFFFFF,&H00FFFFFF,&H00000000,&H90000000,0,0,0,0,100,100,0,0,3,1,0,7,40,40,35,1

[Events]
Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text
"""
    dialogue: list[str] = []
    for index, (start, text) in enumerate(changes):
        end = changes[index + 1][0] if index + 1 < len(changes) else final_time
        if end <= start:
            continue
        dialogue.append(
            f"Dialogue: 0,{_ass_time(start)},{_ass_time(end)},Evidence,,0,0,0,,{text}"
        )
    return header + "\n".join(dialogue) + "\n"


def write_overlay(
    event_path: pathlib.Path,
    overlay_path: pathlib.Path,
    *,
    request_cue_seconds: float | None = None,
) -> None:
    events = load_events(event_path)
    overlay_path.write_text(
        generate_ass(events, request_cue_seconds=request_cue_seconds), encoding="utf-8"
    )
