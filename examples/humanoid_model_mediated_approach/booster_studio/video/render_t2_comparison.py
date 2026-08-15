#!/usr/bin/env python3
"""Render the event-validated T2a/T2b moved-ball paper-video comparison."""

from __future__ import annotations

import argparse
import dataclasses
import hashlib
import json
import math
import pathlib
import subprocess
from typing import Any


class ComparisonError(RuntimeError):
    """The inputs cannot produce trustworthy T2 comparison evidence."""


@dataclasses.dataclass(frozen=True)
class TrialTimeline:
    run_id: str
    trial_id: str
    acceptance_policy: str
    job_id: str
    generation: int
    captured_context_id: str
    current_context_id: str
    clip_start_seconds: float
    duration_seconds: float
    submit_seconds: float
    move_seconds: float
    decision_seconds: float
    runtime_decision: str
    runtime_reason: str
    dispatch_decision: str | None
    dispatch_reason: str | None
    recording_dispatch_calls: int
    ball_a_field_position_m: tuple[float, float, float]
    ball_b_field_position_m: tuple[float, float, float]
    target_ball_relative: tuple[float, float, float]
    obsolete_target_field_position_m: tuple[float, float, float]


@dataclasses.dataclass(frozen=True)
class RecoveryTimeline:
    run_id: str
    trial_id: str
    acceptance_policy: str
    job_id: str
    generation: int
    context_id: str
    submit_seconds: float
    accept_seconds: float
    dispatch_seconds: float
    recording_dispatch_calls: int
    ball_b_field_position_m: tuple[float, float, float]
    target_ball_relative: tuple[float, float, float]
    current_target_field_position_m: tuple[float, float, float]


def _read_json(path: pathlib.Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except Exception as exc:
        raise ComparisonError(f"failed to read JSON {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise ComparisonError(f"expected a JSON object: {path}")
    return value


def _load_events(path: pathlib.Path) -> list[dict[str, Any]]:
    events: list[dict[str, Any]] = []
    for line_number, line in enumerate(
        path.read_text(encoding="utf-8").splitlines(), start=1
    ):
        if not line.strip():
            continue
        try:
            event = json.loads(line)
        except json.JSONDecodeError as exc:
            raise ComparisonError(f"{path}:{line_number}: invalid JSON: {exc}") from exc
        if (
            not isinstance(event, dict)
            or event.get("schema") != "mbt.evt.v1"
            or not isinstance(event.get("seq"), int)
            or not isinstance(event.get("unix_ms"), int)
            or not isinstance(event.get("data"), dict)
        ):
            raise ComparisonError(
                f"{path}:{line_number}: incomplete canonical event envelope"
            )
        events.append(event)
    if not events:
        raise ComparisonError(f"empty event stream: {path}")
    sequences = [event["seq"] for event in events]
    if sequences != list(range(sequences[0], sequences[0] + len(sequences))):
        raise ComparisonError("canonical event sequence is not contiguous")
    if events[0].get("type") != "run_start" or events[-1].get("type") != "run_end":
        raise ComparisonError("event stream must begin with run_start and end with run_end")
    return events


def _events_of_type(
    events: list[dict[str, Any]], event_type: str
) -> list[dict[str, Any]]:
    return [event for event in events if event.get("type") == event_type]


def _single(events: list[dict[str, Any]], description: str) -> dict[str, Any]:
    if len(events) != 1:
        raise ComparisonError(f"T2 requires exactly one {description}")
    return events[0]


def _triple(value: object, field: str) -> tuple[float, float, float]:
    if (
        not isinstance(value, list)
        or len(value) != 3
        or any(
            isinstance(item, bool)
            or not isinstance(item, (int, float))
            or not math.isfinite(float(item))
            for item in value
        )
    ):
        raise ComparisonError(f"{field} must contain three finite numbers")
    return tuple(float(item) for item in value)  # type: ignore[return-value]


def _positive_number(value: object, field: str) -> float:
    if (
        isinstance(value, bool)
        or not isinstance(value, (int, float))
        or not math.isfinite(float(value))
        or float(value) <= 0.0
    ):
        raise ComparisonError(f"{field} must be a positive finite number")
    return float(value)


def _target_from_dispatch(dispatch: dict[str, Any]) -> tuple[float, float, float]:
    target = dispatch["data"].get("target")
    if not isinstance(target, dict) or target.get("frame_id") != "ball_context":
        raise ComparisonError("walking target must use the ball_context frame")
    return _triple(
        [target.get("x_m"), target.get("y_m"), target.get("yaw_rad")],
        "walking target",
    )


def _candidate_target(events: list[dict[str, Any]]) -> tuple[float, float, float]:
    candidates = [
        event
        for event in _events_of_type(events, "bb_write")
        if event["data"].get("key") == "candidate-walking-target"
    ]
    candidate = _single(candidates, "candidate walking target")
    return _triple(candidate["data"].get("preview"), "candidate walking target")


def build_trial_timeline(
    events: list[dict[str, Any]],
    capture: dict[str, Any],
    shot: dict[str, Any],
    *,
    role: str,
) -> TrialTimeline:
    if role not in {"baseline", "full"}:
        raise ComparisonError(f"unknown T2 comparison role: {role}")
    if capture.get("schema_version") != "humanoid.clean_simulator_capture.v1":
        raise ComparisonError("capture timing is not a clean simulator capture")
    first_frame_epoch = _positive_number(
        capture.get("first_frame_epoch"), "first frame epoch"
    )
    last_frame_epoch = _positive_number(
        capture.get("last_frame_epoch"), "last frame epoch"
    )
    if last_frame_epoch <= first_frame_epoch:
        raise ComparisonError("capture timing ends before it starts")
    duration = _positive_number(shot.get("duration_seconds"), "shot duration")
    request_lead = _positive_number(
        shot.get("request_lead_seconds"), "request lead"
    )

    submission = _single(_events_of_type(events, "vla_submit"), "VLA submission")
    decisions = [
        event
        for event in _events_of_type(events, "vla_result")
        if "decision" in event["data"]
    ]
    decision = _single(decisions, "authoritative VLA decision")
    run_end = _single(_events_of_type(events, "run_end"), "run end")

    submission_data = submission["data"]
    decision_data = decision["data"]
    job_id = str(submission_data.get("job_id", ""))
    generation = submission_data.get("generation")
    captured_context = str(submission_data.get("captured_context_id", ""))
    if not job_id or not isinstance(generation, int) or not captured_context:
        raise ComparisonError("T2 submission identity is incomplete")
    if (
        str(decision_data.get("job_id")) != job_id
        or decision_data.get("generation") != generation
        or decision_data.get("captured_context_id") != captured_context
    ):
        raise ComparisonError("T2 result is not invocation-correlated")
    current_context = str(decision_data.get("current_context_id", ""))
    if not current_context or current_context == captured_context:
        raise ComparisonError("T2 requires a changed current context")

    context_writes = [
        event
        for event in _events_of_type(events, "bb_write")
        if event["data"].get("key") == "ball-context-id"
        and event["seq"] > submission["seq"]
        and str(event["data"].get("preview", "")) != captured_context
    ]
    move = _single(context_writes, "post-submission ball context change")
    if str(move["data"].get("preview", "")) != current_context:
        raise ComparisonError("moved-ball context does not match the result context")

    ball_writes = [
        event
        for event in _events_of_type(events, "bb_write")
        if event["data"].get("key") == "ball-state"
    ]
    if len(ball_writes) < 2:
        raise ComparisonError("T2 requires ball states at A and B")
    ball_a_event = next(
        (event for event in ball_writes if event["seq"] < move["seq"]), None
    )
    ball_b_event = next(
        (event for event in ball_writes if event["seq"] > move["seq"]), None
    )
    if ball_a_event is None or ball_b_event is None:
        raise ComparisonError("T2 ball states do not bracket the context change")
    ball_a = _triple(ball_a_event["data"].get("preview"), "ball A state")
    ball_b = _triple(ball_b_event["data"].get("preview"), "ball B state")
    if math.dist(ball_a[:2], ball_b[:2]) <= 0.15:
        raise ComparisonError("T2 ball movement does not exceed the context threshold")

    policy = str(submission_data.get("acceptance_policy", ""))
    if decision_data.get("acceptance_policy") != policy:
        raise ComparisonError("T2 acceptance policy changed during the invocation")
    dispatches = _events_of_type(events, "walking_target_dispatch")
    if role == "baseline":
        if policy != "deadline_only":
            raise ComparisonError("T2a must use deadline_only acceptance")
        if decision_data.get("decision") != "accepted":
            raise ComparisonError("T2a runtime must accept the stale result")
        dispatch = _single(dispatches, "baseline walking-target decision")
        dispatch_data = dispatch["data"]
        if (
            dispatch_data.get("decision") != "rejected"
            or dispatch_data.get("reason") != "context_changed"
            or str(dispatch_data.get("job_id")) != job_id
            or dispatch_data.get("generation") != generation
            or dispatch_data.get("captured_context_id") != captured_context
            or dispatch_data.get("current_context_id") != current_context
        ):
            raise ComparisonError(
                "T2a host must reject the correlated stale dispatch as context_changed"
            )
        target_relative = _target_from_dispatch(dispatch)
        dispatch_decision: str | None = "rejected"
        dispatch_reason: str | None = "context_changed"
    else:
        if policy != "invocation_scoped":
            raise ComparisonError("T2b must use invocation_scoped acceptance")
        if (
            decision_data.get("decision") != "rejected"
            or decision_data.get("reason") != "context_changed"
        ):
            raise ComparisonError(
                "T2b runtime must reject the stale result as context_changed"
            )
        if dispatches:
            raise ComparisonError("T2b must reject before walking-target dispatch")
        target_relative = _candidate_target(events)
        dispatch_decision = None
        dispatch_reason = None

    trial_id = str(run_end["data"].get("trial_id", ""))
    expected_trial_id = "T2a" if role == "baseline" else "T2b"
    if trial_id != expected_trial_id:
        raise ComparisonError(f"{role} evidence is not trial {expected_trial_id}")
    recording_dispatch_calls = run_end["data"].get("recording_dispatch_calls")
    if recording_dispatch_calls != 0:
        raise ComparisonError("T2 paper evidence must contain zero backend dispatch calls")

    submit_raw = submission["unix_ms"] / 1000.0 - first_frame_epoch
    move_raw = move["unix_ms"] / 1000.0 - first_frame_epoch
    decision_raw = decision["unix_ms"] / 1000.0 - first_frame_epoch
    clip_start = submit_raw - request_lead
    if clip_start < 0.0:
        raise ComparisonError("capture does not contain the configured request lead")
    if clip_start + duration > last_frame_epoch - first_frame_epoch:
        raise ComparisonError("capture does not cover the complete T2 comparison duration")
    if not clip_start < submit_raw < move_raw < decision_raw:
        raise ComparisonError("T2 request, movement and decision timing is not ordered")

    obsolete_target = (
        ball_a[0] + target_relative[0],
        ball_a[1] + target_relative[1],
        target_relative[2],
    )
    return TrialTimeline(
        run_id=str(events[0].get("run_id", "")),
        trial_id=trial_id,
        acceptance_policy=policy,
        job_id=job_id,
        generation=generation,
        captured_context_id=captured_context,
        current_context_id=current_context,
        clip_start_seconds=clip_start,
        duration_seconds=duration,
        submit_seconds=submit_raw - clip_start,
        move_seconds=move_raw - clip_start,
        decision_seconds=decision_raw - clip_start,
        runtime_decision=str(decision_data.get("decision")),
        runtime_reason=str(decision_data.get("reason", "")),
        dispatch_decision=dispatch_decision,
        dispatch_reason=dispatch_reason,
        recording_dispatch_calls=recording_dispatch_calls,
        ball_a_field_position_m=ball_a,
        ball_b_field_position_m=ball_b,
        target_ball_relative=target_relative,
        obsolete_target_field_position_m=obsolete_target,
    )


def build_recovery_timeline(
    events: list[dict[str, Any]],
    capture: dict[str, Any],
    previous: TrialTimeline,
) -> RecoveryTimeline:
    if capture.get("schema_version") != "humanoid.clean_simulator_capture.v1":
        raise ComparisonError("recovery timing is not a clean simulator capture")
    first_frame_epoch = _positive_number(
        capture.get("first_frame_epoch"), "first frame epoch"
    )
    submission = _single(
        _events_of_type(events, "vla_submit"), "recovery VLA submission"
    )
    decisions = [
        event
        for event in _events_of_type(events, "vla_result")
        if "decision" in event["data"]
    ]
    decision = _single(decisions, "recovery authoritative VLA decision")
    dispatch = _single(
        _events_of_type(events, "walking_target_dispatch"),
        "recovery walking-target dispatch",
    )
    run_end = _single(_events_of_type(events, "run_end"), "recovery run end")

    submission_data = submission["data"]
    decision_data = decision["data"]
    dispatch_data = dispatch["data"]
    job_id = str(submission_data.get("job_id", ""))
    generation = submission_data.get("generation")
    context_id = str(submission_data.get("captured_context_id", ""))
    if not job_id or not isinstance(generation, int) or not context_id:
        raise ComparisonError("recovery submission identity is incomplete")
    if context_id != previous.current_context_id:
        raise ComparisonError("recovery request did not capture current ball B context")
    for event_data in (decision_data, dispatch_data):
        if (
            str(event_data.get("job_id")) != job_id
            or event_data.get("generation") != generation
            or event_data.get("captured_context_id") != context_id
            or event_data.get("current_context_id") != context_id
        ):
            raise ComparisonError("recovery result is not current and invocation-correlated")
    if (
        submission_data.get("acceptance_policy") != "invocation_scoped"
        or decision_data.get("acceptance_policy") != "invocation_scoped"
        or decision_data.get("decision") != "accepted"
        or dispatch_data.get("decision") != "accepted"
    ):
        raise ComparisonError("recovery must accept and dispatch one current B result")

    ball_writes = [
        event
        for event in _events_of_type(events, "bb_write")
        if event["data"].get("key") == "ball-state"
    ]
    if not ball_writes:
        raise ComparisonError("recovery event stream has no ball B state")
    ball_b = _triple(ball_writes[0]["data"].get("preview"), "recovery ball B state")
    if any(
        abs(left - right) > 1e-9
        for left, right in zip(
            ball_b, previous.ball_b_field_position_m, strict=True
        )
    ):
        raise ComparisonError("recovery ball state does not match moved ball B")
    target_relative = _target_from_dispatch(dispatch)
    if any(
        abs(left - right) > 1e-9
        for left, right in zip(
            target_relative, previous.target_ball_relative, strict=True
        )
    ):
        raise ComparisonError("recovery changed the model candidate pose")

    if run_end["data"].get("trial_id") != "T1":
        raise ComparisonError("recovery evidence is not a normal-result trial")
    recording_dispatch_calls = run_end["data"].get("recording_dispatch_calls")
    if recording_dispatch_calls != 1:
        raise ComparisonError("recovery must contain exactly one walking backend call")

    submit_seconds = (
        submission["unix_ms"] / 1000.0
        - first_frame_epoch
        - previous.clip_start_seconds
    )
    accept_seconds = (
        decision["unix_ms"] / 1000.0
        - first_frame_epoch
        - previous.clip_start_seconds
    )
    dispatch_seconds = (
        dispatch["unix_ms"] / 1000.0
        - first_frame_epoch
        - previous.clip_start_seconds
    )
    if not (
        previous.decision_seconds
        < submit_seconds
        < accept_seconds
        <= dispatch_seconds
        < previous.duration_seconds
    ):
        raise ComparisonError("recovery timing is not ordered within the T2 capture")
    current_target = (
        ball_b[0] + target_relative[0],
        ball_b[1] + target_relative[1],
        target_relative[2],
    )
    return RecoveryTimeline(
        run_id=str(events[0].get("run_id", "")),
        trial_id="T1",
        acceptance_policy="invocation_scoped",
        job_id=job_id,
        generation=generation,
        context_id=context_id,
        submit_seconds=submit_seconds,
        accept_seconds=accept_seconds,
        dispatch_seconds=dispatch_seconds,
        recording_dispatch_calls=recording_dispatch_calls,
        ball_b_field_position_m=ball_b,
        target_ball_relative=target_relative,
        current_target_field_position_m=current_target,
    )


def validate_matched_trials(
    baseline: TrialTimeline,
    full: TrialTimeline,
    baseline_recovery: RecoveryTimeline | None = None,
    full_recovery: RecoveryTimeline | None = None,
) -> None:
    if baseline.duration_seconds != full.duration_seconds:
        raise ComparisonError("T2 trials have different rendered durations")
    for field in (
        "ball_a_field_position_m",
        "ball_b_field_position_m",
        "target_ball_relative",
    ):
        left = getattr(baseline, field)
        right = getattr(full, field)
        if any(abs(a - b) > 1e-9 for a, b in zip(left, right, strict=True)):
            raise ComparisonError(f"T2 trials are not matched on {field}")
    if (baseline_recovery is None) != (full_recovery is None):
        raise ComparisonError("T2 comparison has only one recovery invocation")
    if baseline_recovery is not None and full_recovery is not None:
        for field in ("ball_b_field_position_m", "target_ball_relative"):
            left = getattr(baseline_recovery, field)
            right = getattr(full_recovery, field)
            if any(abs(a - b) > 1e-9 for a, b in zip(left, right, strict=True)):
                raise ComparisonError(f"T2 recoveries are not matched on {field}")


def _ass_time(seconds: float) -> str:
    centiseconds = max(0, round(seconds * 100.0))
    hours, remainder = divmod(centiseconds, 360000)
    minutes, remainder = divmod(remainder, 6000)
    whole_seconds, centiseconds = divmod(remainder, 100)
    return f"{hours}:{minutes:02d}:{whole_seconds:02d}.{centiseconds:02d}"


def _ass_escape(value: str) -> str:
    return value.replace("\\", r"\\").replace("{", r"\{").replace("}", r"\}")


def _dialogue(start: float, end: float, style: str, text: str, layer: int = 0) -> str:
    return (
        f"Dialogue: {layer},{_ass_time(start)},{_ass_time(end)},{style},,0,0,0,,{text}"
    )


def _panel_point(shot: dict[str, Any], name: str, offset: int) -> tuple[int, int]:
    calibration = shot.get("fixed_camera_panel_calibration")
    value = calibration.get(name) if isinstance(calibration, dict) else None
    if (
        not isinstance(value, list)
        or len(value) != 2
        or any(isinstance(item, bool) or not isinstance(item, int) for item in value)
    ):
        raise ComparisonError(f"missing fixed-camera panel calibration point: {name}")
    return int(value[0]) + offset, int(value[1])


def _panel_rows(
    timeline: TrialTimeline,
    recovery: RecoveryTimeline,
    shot: dict[str, Any],
    *,
    offset: int,
    role: str,
) -> list[str]:
    end = timeline.duration_seconds
    submit = timeline.submit_seconds
    move = timeline.move_seconds
    decision = timeline.decision_seconds
    centre = offset + 480
    left = offset + 34
    ball_a_x, ball_a_y = _panel_point(shot, "ball_a", offset)
    ball_b_x, ball_b_y = _panel_point(shot, "ball_b", offset)
    target_x, target_y = _panel_point(shot, "obsolete_target", offset)
    current_target_x, current_target_y = _panel_point(
        shot, "current_target_b", offset
    )
    captured = _ass_escape(timeline.captured_context_id)
    current = _ass_escape(timeline.current_context_id)
    title = "T2a  ·  TIMEOUT-ONLY BASELINE" if role == "baseline" else (
        "T2b  ·  INVOCATION-SCOPED AUTHORITY"
    )
    rows = [
        _dialogue(
            0.0,
            end,
            "PanelTitle",
            rf"{{\an7\pos({left},86)}}{title}",
            3,
        ),
        _dialogue(
            0.0,
            submit,
            "Status",
            rf"{{\an7\pos({left},147)}}BALL AT A  ·  READY",
            3,
        ),
        _dialogue(
            0.0,
            submit,
            "Detail",
            rf"{{\an7\pos({left},194)}}CURRENT CONTEXT A  ·  {captured}",
            3,
        ),
        _dialogue(
            submit,
            move,
            "StatusAmber",
            rf"{{\an7\pos({left},147)}}MODEL REQUEST PENDING",
            3,
        ),
        _dialogue(
            submit,
            move,
            "Detail",
            (
                rf"{{\an7\pos({left},194)}}JOB {_ass_escape(timeline.job_id)}  ·  "
                rf"GEN {timeline.generation}  ·  CAPTURED A"
            ),
            3,
        ),
        _dialogue(
            move,
            decision,
            "StatusRed",
            rf"{{\an7\pos({left},147)}}BALL MOVED A  →  B",
            3,
        ),
        _dialogue(
            move,
            decision,
            "Detail",
            rf"{{\an7\pos({left},194)}}OLD REQUEST STILL RUNNING  ·  CURRENT {current}",
            3,
        ),
        _dialogue(
            0.0,
            move,
            "BallLabel",
            rf"{{\an2\pos({ball_a_x},{ball_a_y - 42})}}BALL A",
            3,
        ),
        _dialogue(
            move,
            recovery.accept_seconds,
            "GhostRed",
            rf"{{\an5\pos({ball_a_x},{ball_a_y})}}○",
            2,
        ),
        _dialogue(
            move,
            recovery.accept_seconds,
            "RedLabel",
            rf"{{\an2\pos({ball_a_x},{ball_a_y - 45})}}A  ·  OBSOLETE",
            3,
        ),
        _dialogue(
            move,
            end,
            "CurrentGreen",
            rf"{{\an5\pos({ball_b_x},{ball_b_y})}}○",
            2,
        ),
        _dialogue(
            move,
            end,
            "GreenLabel",
            rf"{{\an2\pos({ball_b_x},{ball_b_y - 45})}}B  ·  CURRENT",
            3,
        ),
        _dialogue(
            decision,
            recovery.accept_seconds,
            "VectorRed",
            (
                rf"{{\p1\c&H503B4CFF&}}m {ball_a_x} {ball_a_y - 6} "
                rf"l {target_x} {target_y - 6} l {target_x} {target_y + 6} "
                rf"l {ball_a_x} {ball_a_y + 6}{{\p0}}"
            ),
            1,
        ),
        _dialogue(
            decision,
            recovery.accept_seconds,
            "TargetRed",
            rf"{{\an5\pos({target_x},{target_y})\fad(100,0)}}⊗",
            3,
        ),
        _dialogue(
            decision,
            recovery.accept_seconds,
            "RedLabel",
            rf"{{\an6\pos({target_x - 43},{target_y - 52})}}OLD TARGET FOR A",
            3,
        ),
    ]
    if role == "baseline":
        rows.extend(
            [
                _dialogue(
                    decision,
                    recovery.submit_seconds,
                    "StatusRed",
                    rf"{{\an7\pos({left},147)}}STALE RESULT ACCEPTED BY RUNTIME",
                    3,
                ),
                _dialogue(
                    decision,
                    recovery.submit_seconds,
                    "Detail",
                    rf"{{\an7\pos({left},194)}}HOST BLOCKED DISPATCH  ·  context_changed",
                    3,
                ),
                _dialogue(
                    decision,
                    recovery.submit_seconds,
                    "PipelineRed",
                    rf"{{\an2\pos({centre},1014)}}RUNTIME ACCEPTED  →  HOST BLOCKED",
                    4,
                ),
            ]
        )
    else:
        rows.extend(
            [
                _dialogue(
                    decision,
                    recovery.submit_seconds,
                    "StatusGreen",
                    rf"{{\an7\pos({left},147)}}STALE RESULT REJECTED",
                    3,
                ),
                _dialogue(
                    decision,
                    recovery.submit_seconds,
                    "Detail",
                    rf"{{\an7\pos({left},194)}}INVOCATION GATE  ·  context_changed",
                    3,
                ),
                _dialogue(
                    decision,
                    recovery.submit_seconds,
                    "PipelineGreen",
                    rf"{{\an2\pos({centre},1014)}}RUNTIME REJECTED  →  NO DISPATCH",
                    4,
                ),
            ]
        )
    rows.append(
        _dialogue(
            decision + 0.25,
            recovery.submit_seconds,
            "NoCommand",
            rf"{{\an2\pos({centre},1060)}}0 COMMANDS TO OLD TARGET  ✓",
            4,
        )
    )
    recovery_context = _ass_escape(recovery.context_id)
    rows.extend(
        [
            _dialogue(
                recovery.submit_seconds,
                recovery.accept_seconds,
                "StatusAmber",
                rf"{{\an7\pos({left},147)}}NEW REQUEST FOR CURRENT BALL B",
                3,
            ),
            _dialogue(
                recovery.submit_seconds,
                recovery.accept_seconds,
                "Detail",
                (
                    rf"{{\an7\pos({left},194)}}CAPTURED B = CURRENT B  ·  "
                    rf"{recovery_context}"
                ),
                3,
            ),
            _dialogue(
                recovery.submit_seconds,
                recovery.accept_seconds,
                "PipelineGreen",
                rf"{{\an2\pos({centre},1036)}}FRESH REQUEST CAPTURES B",
                4,
            ),
            _dialogue(
                recovery.accept_seconds,
                end,
                "StatusGreen",
                rf"{{\an7\pos({left},147)}}CURRENT B RESULT ACCEPTED",
                3,
            ),
            _dialogue(
                recovery.accept_seconds,
                end,
                "Detail",
                rf"{{\an7\pos({left},194)}}CURRENT CONTEXT VERIFIED  ·  DISPATCHED ONCE",
                3,
            ),
            _dialogue(
                recovery.accept_seconds,
                end,
                "VectorGreen",
                (
                    rf"{{\p1\c&H506BE62E&}}m {ball_b_x} {ball_b_y - 6} "
                    rf"l {current_target_x} {current_target_y - 6} "
                    rf"l {current_target_x} {current_target_y + 6} "
                    rf"l {ball_b_x} {ball_b_y + 6}{{\p0}}"
                ),
                1,
            ),
            _dialogue(
                recovery.accept_seconds,
                end,
                "TargetGreen",
                (
                    rf"{{\an5\pos({current_target_x},{current_target_y})"
                    r"\fad(100,0)}⊕"
                ),
                3,
            ),
            _dialogue(
                recovery.accept_seconds,
                end,
                "GreenLabel",
                (
                    rf"{{\an6\pos({current_target_x - 43},"
                    rf"{current_target_y - 52})}}CURRENT TARGET FOR B"
                ),
                3,
            ),
            _dialogue(
                recovery.accept_seconds,
                end,
                "PipelineGreen",
                rf"{{\an2\pos({centre},1014)}}CURRENT B ACCEPTED  →  WALKING",
                4,
            ),
            _dialogue(
                recovery.accept_seconds + 0.5,
                end,
                "NoCommand",
                rf"{{\an2\pos({centre},1060)}}ROBOT WALKS TO CURRENT TARGET B  ✓",
                4,
            ),
        ]
    )
    return rows


def generate_ass(
    baseline: TrialTimeline,
    full: TrialTimeline,
    baseline_recovery: RecoveryTimeline,
    full_recovery: RecoveryTimeline,
    shot: dict[str, Any],
) -> str:
    output = shot.get("output")
    if not isinstance(output, dict):
        raise ComparisonError("shot output configuration is missing")
    width = int(_positive_number(output.get("width"), "output width"))
    height = int(_positive_number(output.get("height"), "output height"))
    panel_width = int(_positive_number(output.get("panel_width"), "panel width"))
    if panel_width * 2 != width:
        raise ComparisonError("T2 output must contain two equal-width panels")
    end = baseline.duration_seconds
    header = f"""[Script Info]
ScriptType: v4.00+
PlayResX: {width}
PlayResY: {height}
WrapStyle: 2
ScaledBorderAndShadow: yes

[V4+ Styles]
Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding
Style: Overall,DejaVu Sans,34,&H00FFFFFF,&H00FFFFFF,&H00131A20,&HDA111820,-1,0,0,0,100,100,1,0,3,2,0,8,32,32,24,1
Style: PanelTitle,DejaVu Sans,32,&H00FFFFFF,&H00FFFFFF,&H00131A20,&HC8182028,-1,0,0,0,100,100,0,0,3,2,0,7,0,0,0,1
Style: Status,DejaVu Sans,29,&H00FFFFFF,&H00FFFFFF,&H00131A20,&HC0182028,-1,0,0,0,100,100,0,0,3,1,0,7,0,0,0,1
Style: StatusAmber,DejaVu Sans,29,&H0014A5FF,&H0014A5FF,&H00131A20,&HC0182028,-1,0,0,0,100,100,0,0,3,1,0,7,0,0,0,1
Style: StatusRed,DejaVu Sans,29,&H003B4CFF,&H003B4CFF,&H00131A20,&HC0182028,-1,0,0,0,100,100,0,0,3,1,0,7,0,0,0,1
Style: StatusGreen,DejaVu Sans,29,&H006BE62E,&H006BE62E,&H00131A20,&HC0182028,-1,0,0,0,100,100,0,0,3,1,0,7,0,0,0,1
Style: Detail,DejaVu Sans,21,&H00FFFFFF,&H00FFFFFF,&H00131A20,&HB8182028,0,0,0,0,100,100,0,0,3,1,0,7,0,0,0,1
Style: BallLabel,DejaVu Sans,22,&H00FFFFFF,&H00FFFFFF,&H00131A20,&HB0182028,-1,0,0,0,100,100,0,0,3,1,0,2,0,0,0,1
Style: GhostRed,DejaVu Sans,70,&H603B4CFF,&H603B4CFF,&H30131A20,&H00000000,-1,0,0,0,100,100,0,0,1,4,0,5,0,0,0,1
Style: CurrentGreen,DejaVu Sans,70,&H406BE62E,&H406BE62E,&H30131A20,&H00000000,-1,0,0,0,100,100,0,0,1,4,0,5,0,0,0,1
Style: RedLabel,DejaVu Sans,21,&H003B4CFF,&H003B4CFF,&H00131A20,&HBC182028,-1,0,0,0,100,100,0,0,3,2,0,2,0,0,0,1
Style: GreenLabel,DejaVu Sans,21,&H006BE62E,&H006BE62E,&H00131A20,&HBC182028,-1,0,0,0,100,100,0,0,3,2,0,2,0,0,0,1
Style: TargetRed,DejaVu Sans,78,&H003B4CFF,&H003B4CFF,&H00131A20,&H00000000,-1,0,0,0,100,100,0,0,1,5,0,5,0,0,0,1
Style: TargetGreen,DejaVu Sans,78,&H006BE62E,&H006BE62E,&H00131A20,&H00000000,-1,0,0,0,100,100,0,0,1,5,0,5,0,0,0,1
Style: VectorRed,Arial,20,&H503B4CFF,&H503B4CFF,&H503B4CFF,&H00000000,0,0,0,0,100,100,0,0,1,0,0,7,0,0,0,1
Style: VectorGreen,Arial,20,&H506BE62E,&H506BE62E,&H506BE62E,&H00000000,0,0,0,0,100,100,0,0,1,0,0,7,0,0,0,1
Style: PipelineRed,DejaVu Sans,28,&H003B4CFF,&H003B4CFF,&H00131A20,&HDC182028,-1,0,0,0,100,100,0,0,3,2,0,2,0,0,0,1
Style: PipelineGreen,DejaVu Sans,28,&H006BE62E,&H006BE62E,&H00131A20,&HDC182028,-1,0,0,0,100,100,0,0,3,2,0,2,0,0,0,1
Style: NoCommand,DejaVu Sans,23,&H00FFFFFF,&H00FFFFFF,&H00131A20,&HD0182028,-1,0,0,0,100,100,0,0,3,1,0,2,0,0,0,1
Style: Divider,Arial,20,&H50131A20,&H50131A20,&H50131A20,&H00000000,0,0,0,0,100,100,0,0,1,0,0,7,0,0,0,1

[Events]
Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text
"""
    rows = [
        _dialogue(
            0.0,
            end,
            "Overall",
            r"{\an8\pos(960,22)}T2  ·  BALL MOVED WHILE REQUEST IS RUNNING",
            5,
        ),
        _dialogue(
            0.0,
            end,
            "Divider",
            r"{\p1\c&H50131A20&}m 954 0 l 966 0 l 966 1080 l 954 1080{\p0}",
            5,
        ),
    ]
    rows.extend(
        _panel_rows(
            baseline, baseline_recovery, shot, offset=0, role="baseline"
        )
    )
    rows.extend(
        _panel_rows(
            full, full_recovery, shot, offset=panel_width, role="full"
        )
    )
    return header + "\n".join(rows) + "\n"


def _sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def render(
    baseline_events_path: pathlib.Path,
    baseline_recovery_events_path: pathlib.Path,
    baseline_capture_path: pathlib.Path,
    baseline_video_path: pathlib.Path,
    full_events_path: pathlib.Path,
    full_recovery_events_path: pathlib.Path,
    full_capture_path: pathlib.Path,
    full_video_path: pathlib.Path,
    shot_path: pathlib.Path,
    output_dir: pathlib.Path,
    *,
    force: bool,
) -> pathlib.Path:
    paths = [
        baseline_events_path,
        baseline_recovery_events_path,
        baseline_capture_path,
        baseline_video_path,
        full_events_path,
        full_recovery_events_path,
        full_capture_path,
        full_video_path,
        shot_path,
    ]
    paths = [path.resolve() for path in paths]
    (
        baseline_events_path,
        baseline_recovery_events_path,
        baseline_capture_path,
        baseline_video_path,
        full_events_path,
        full_recovery_events_path,
        full_capture_path,
        full_video_path,
        shot_path,
    ) = paths
    output_dir = output_dir.resolve()
    if output_dir.exists() and (not force or output_dir.is_symlink()):
        raise ComparisonError(f"refusing existing output directory: {output_dir}")
    output_dir.mkdir(parents=True, exist_ok=True)

    shot = _read_json(shot_path)
    if shot.get("schema_version") != "humanoid.paper_video_comparison.v1":
        raise ComparisonError("unsupported T2 comparison configuration")
    baseline = build_trial_timeline(
        _load_events(baseline_events_path),
        _read_json(baseline_capture_path),
        shot,
        role="baseline",
    )
    full = build_trial_timeline(
        _load_events(full_events_path),
        _read_json(full_capture_path),
        shot,
        role="full",
    )
    baseline_recovery = build_recovery_timeline(
        _load_events(baseline_recovery_events_path),
        _read_json(baseline_capture_path),
        baseline,
    )
    full_recovery = build_recovery_timeline(
        _load_events(full_recovery_events_path),
        _read_json(full_capture_path),
        full,
    )
    validate_matched_trials(
        baseline, full, baseline_recovery, full_recovery
    )

    overlay_path = output_dir / "t2-comparison.ass"
    overlay_path.write_text(
        generate_ass(
            baseline, full, baseline_recovery, full_recovery, shot
        ),
        encoding="utf-8",
    )
    crop = shot.get("source_crop")
    output = shot.get("output")
    if not isinstance(crop, dict) or not isinstance(output, dict):
        raise ComparisonError("shot crop or output configuration is missing")
    crop_values = {
        key: int(_positive_number(crop.get(key), f"crop {key}"))
        for key in ("x", "y", "width", "height")
    }
    output_width = int(_positive_number(output.get("width"), "output width"))
    output_height = int(_positive_number(output.get("height"), "output height"))
    panel_width = int(_positive_number(output.get("panel_width"), "panel width"))
    fps = int(_positive_number(output.get("fps"), "output fps"))
    duration = baseline.duration_seconds
    video_path = output_dir / "t2-polished-comparison.mp4"
    crop_filter = (
        f"crop={crop_values['width']}:{crop_values['height']}:"
        f"{crop_values['x']}:{crop_values['y']},"
        f"scale={panel_width}:{output_height}:flags=lanczos"
    )
    filters = (
        f"[0:v]trim=start={baseline.clip_start_seconds:.6f}:duration={duration:.3f},"
        f"setpts=PTS-STARTPTS,{crop_filter}[left];"
        f"[1:v]trim=start={full.clip_start_seconds:.6f}:duration={duration:.3f},"
        f"setpts=PTS-STARTPTS,{crop_filter}[right];"
        f"[left][right]hstack=inputs=2,ass={overlay_path.name},"
        f"fade=t=in:st=0:d=0.25,fade=t=out:st={duration - 0.25:.3f}:d=0.25[out]"
    )
    completed = subprocess.run(
        [
            "ffmpeg",
            "-hide_banner",
            "-loglevel",
            "error",
            "-y",
            "-i",
            str(baseline_video_path),
            "-i",
            str(full_video_path),
            "-filter_complex",
            filters,
            "-map",
            "[out]",
            "-t",
            f"{duration:.3f}",
            "-r",
            str(fps),
            "-c:v",
            "libx264",
            "-crf",
            "18",
            "-preset",
            "medium",
            "-pix_fmt",
            "yuv420p",
            "-movflags",
            "+faststart",
            video_path.name,
        ],
        cwd=output_dir,
        check=False,
    )
    if completed.returncode != 0 or not video_path.is_file():
        raise ComparisonError(f"ffmpeg render failed with code {completed.returncode}")
    if output_width != panel_width * 2:
        raise ComparisonError("rendered panel widths do not match the output width")

    manifest = {
        "schema_version": "humanoid.t2_polished_comparison.v1",
        "shot_id": shot.get("shot_id"),
        "comparison": {
            "baseline": dataclasses.asdict(baseline),
            "baseline_recovery": dataclasses.asdict(baseline_recovery),
            "full": dataclasses.asdict(full),
            "full_recovery": dataclasses.asdict(full_recovery),
        },
        "paper_claim": {
            "ball_moved_during_request": True,
            "baseline_runtime_accepted": True,
            "baseline_host_blocked": True,
            "full_runtime_rejected": True,
            "obsolete_target_backend_dispatch_calls": 0,
            "current_b_backend_dispatch_calls": 2,
        },
        "inputs": {
            "baseline_events_sha256": _sha256(baseline_events_path),
            "baseline_recovery_events_sha256": _sha256(
                baseline_recovery_events_path
            ),
            "baseline_capture_timing_sha256": _sha256(baseline_capture_path),
            "baseline_raw_video_sha256": _sha256(baseline_video_path),
            "full_events_sha256": _sha256(full_events_path),
            "full_recovery_events_sha256": _sha256(
                full_recovery_events_path
            ),
            "full_capture_timing_sha256": _sha256(full_capture_path),
            "full_raw_video_sha256": _sha256(full_video_path),
            "shot_sha256": _sha256(shot_path),
        },
        "outputs": {
            "overlay": overlay_path.name,
            "overlay_sha256": _sha256(overlay_path),
            "video": video_path.name,
            "video_sha256": _sha256(video_path),
            "video_size_bytes": video_path.stat().st_size,
        },
    }
    (output_dir / "render-manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return video_path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline-events", required=True, type=pathlib.Path)
    parser.add_argument(
        "--baseline-recovery-events", required=True, type=pathlib.Path
    )
    parser.add_argument("--baseline-capture-timing", required=True, type=pathlib.Path)
    parser.add_argument("--baseline-raw-video", required=True, type=pathlib.Path)
    parser.add_argument("--full-events", required=True, type=pathlib.Path)
    parser.add_argument("--full-recovery-events", required=True, type=pathlib.Path)
    parser.add_argument("--full-capture-timing", required=True, type=pathlib.Path)
    parser.add_argument("--full-raw-video", required=True, type=pathlib.Path)
    parser.add_argument(
        "--shot",
        type=pathlib.Path,
        default=pathlib.Path(__file__).with_name("t2_comparison.json"),
    )
    parser.add_argument("--output-dir", required=True, type=pathlib.Path)
    parser.add_argument("--force", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        video = render(
            args.baseline_events,
            args.baseline_recovery_events,
            args.baseline_capture_timing,
            args.baseline_raw_video,
            args.full_events,
            args.full_recovery_events,
            args.full_capture_timing,
            args.full_raw_video,
            args.shot,
            args.output_dir,
            force=args.force,
        )
    except ComparisonError as exc:
        print(f"T2 comparison error: {exc}")
        return 2
    print(f"PASS wrote polished T2 comparison: {video}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
