#!/usr/bin/env python3
"""Render the evidence-gated T3 higher-priority interruption video."""

from __future__ import annotations

import argparse
import dataclasses
import hashlib
import json
import math
import pathlib
import subprocess
from typing import Any


class EmergencyVideoError(RuntimeError):
    """The inputs cannot support the T3 paper-video claim."""


@dataclasses.dataclass(frozen=True)
class EmergencyTimeline:
    run_id: str
    trial_id: str
    acceptance_policy: str
    job_id: str
    generation: int
    captured_context_id: str
    clip_start_seconds: float
    duration_seconds: float
    submit_seconds: float
    emergency_seconds: float
    revoke_seconds: float
    completion_seconds: float
    emergency_tick: int
    safe_stand_tick: int
    safe_stand_tick_delta: int
    authority_reason: str
    completion_reason: str
    recording_dispatch_calls: int
    ball_field_position_m: tuple[float, float, float]
    target_ball_relative: tuple[float, float, float]
    revoked_target_field_position_m: tuple[float, float, float]


def _read_json(path: pathlib.Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except Exception as exc:
        raise EmergencyVideoError(f"failed to read JSON {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise EmergencyVideoError(f"expected a JSON object: {path}")
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
            raise EmergencyVideoError(
                f"{path}:{line_number}: invalid JSON: {exc}"
            ) from exc
        if (
            not isinstance(event, dict)
            or event.get("schema") != "mbt.evt.v1"
            or not isinstance(event.get("seq"), int)
            or not isinstance(event.get("tick"), int)
            or not isinstance(event.get("unix_ms"), int)
            or not isinstance(event.get("data"), dict)
        ):
            raise EmergencyVideoError(
                f"{path}:{line_number}: incomplete canonical event envelope"
            )
        events.append(event)
    if not events:
        raise EmergencyVideoError(f"empty event stream: {path}")
    sequences = [event["seq"] for event in events]
    if sequences != list(range(sequences[0], sequences[0] + len(sequences))):
        raise EmergencyVideoError("canonical event sequence is not contiguous")
    if events[0].get("type") != "run_start" or events[-1].get("type") != "run_end":
        raise EmergencyVideoError(
            "event stream must begin with run_start and end with run_end"
        )
    return events


def _events_of_type(
    events: list[dict[str, Any]], event_type: str
) -> list[dict[str, Any]]:
    return [event for event in events if event.get("type") == event_type]


def _single(events: list[dict[str, Any]], description: str) -> dict[str, Any]:
    if len(events) != 1:
        raise EmergencyVideoError(
            f"expected one {description}, found {len(events)}"
        )
    return events[0]


def _blackboard_writes(
    events: list[dict[str, Any]], key: str, value: object
) -> list[dict[str, Any]]:
    return [
        event
        for event in _events_of_type(events, "bb_write")
        if event["data"].get("key") == key
        and event["data"].get("preview") == value
    ]


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
        raise EmergencyVideoError(f"{field} must contain three finite numbers")
    return tuple(float(item) for item in value)  # type: ignore[return-value]


def _positive_number(value: object, field: str) -> float:
    if (
        isinstance(value, bool)
        or not isinstance(value, (int, float))
        or not math.isfinite(float(value))
        or float(value) <= 0.0
    ):
        raise EmergencyVideoError(f"{field} must be a positive finite number")
    return float(value)


def _cancelled_target(result: dict[str, Any]) -> tuple[float, float, float]:
    data = result["data"]
    record = data.get("record")
    if (
        data.get("status") != "cancelled"
        or not isinstance(record, dict)
        or record.get("status") != "cancelled"
        or record.get("completion_dropped") is not True
    ):
        raise EmergencyVideoError(
            "late model result must be recorded as a dropped cancellation"
        )
    response = record.get("response")
    action = response.get("action") if isinstance(response, dict) else None
    if not isinstance(action, dict) or action.get("frame_id") != "ball_context":
        raise EmergencyVideoError(
            "cancelled model candidate must use the ball_context frame"
        )
    return _triple(action.get("u"), "cancelled model candidate")


def build_timeline(
    events: list[dict[str, Any]],
    capture: dict[str, Any],
    shot: dict[str, Any],
) -> EmergencyTimeline:
    if capture.get("schema_version") != "humanoid.clean_simulator_capture.v1":
        raise EmergencyVideoError("capture timing is not a clean simulator capture")
    first_frame_epoch = _positive_number(
        capture.get("first_frame_epoch"), "first frame epoch"
    )
    last_frame_epoch = _positive_number(
        capture.get("last_frame_epoch"), "last frame epoch"
    )
    if last_frame_epoch <= first_frame_epoch:
        raise EmergencyVideoError("capture timing ends before it starts")
    duration = _positive_number(shot.get("duration_seconds"), "shot duration")
    request_lead = _positive_number(
        shot.get("request_lead_seconds"), "request lead"
    )

    submission = _single(_events_of_type(events, "vla_submit"), "VLA submission")
    submission_data = submission["data"]
    job_id = str(submission_data.get("job_id", ""))
    generation = submission_data.get("generation")
    context_id = str(submission_data.get("captured_context_id", ""))
    if not job_id or not isinstance(generation, int) or not context_id:
        raise EmergencyVideoError("T3 submission identity is incomplete")
    if submission_data.get("acceptance_policy") != "invocation_scoped":
        raise EmergencyVideoError("T3 must use invocation_scoped acceptance")

    emergency = _single(
        [
            event
            for event in _blackboard_writes(events, "emergency", True)
            if event["seq"] > submission["seq"]
        ],
        "post-submission software emergency",
    )
    unstable = _single(
        [
            event
            for event in _blackboard_writes(events, "robot-stable", False)
            if event["seq"] >= emergency["seq"]
        ],
        "emergency instability state",
    )
    safe_stand = _single(
        [
            event
            for event in _blackboard_writes(events, "active-branch", "safe_stand")
            if event["seq"] >= emergency["seq"]
        ],
        "safe-stand branch transition",
    )
    request_revoked = _single(
        [
            event
            for event in _blackboard_writes(events, "request-state", "revoked")
            if event["seq"] >= emergency["seq"]
        ],
        "revoked request state",
    )
    target_none = _single(
        [
            event
            for event in _blackboard_writes(events, "walking-target-state", "none")
            if event["seq"] >= emergency["seq"]
        ],
        "cleared walking-target state",
    )

    emergency_tick = emergency["tick"]
    safe_tick = safe_stand["tick"]
    if safe_tick < emergency_tick or safe_tick > emergency_tick + 1:
        raise EmergencyVideoError(
            "safe_stand did not become active within one BT tick of the emergency"
        )
    for event, description in (
        (unstable, "instability"),
        (request_revoked, "request revocation"),
        (target_none, "walking-target clear"),
    ):
        if event["tick"] < emergency_tick or event["tick"] > emergency_tick + 1:
            raise EmergencyVideoError(
                f"{description} did not occur within one BT tick of the emergency"
            )

    revocation = _single(
        _events_of_type(events, "async_authority_revoked"),
        "authority revocation",
    )
    revocation_data = revocation["data"]
    if (
        str(revocation_data.get("job_id")) != job_id
        or revocation_data.get("generation") != generation
        or revocation_data.get("captured_context_id") != context_id
        or revocation_data.get("acceptance_policy") != "invocation_scoped"
        or revocation_data.get("authority_state") != "revoked"
        or revocation_data.get("reason") != "branch_revoked"
    ):
        raise EmergencyVideoError(
            "T3 authority revocation is not correlated as branch_revoked"
        )
    if (
        revocation["seq"] < emergency["seq"]
        or revocation["tick"] > emergency_tick + 1
    ):
        raise EmergencyVideoError(
            "authority was not revoked within one BT tick of the emergency"
        )

    cancelled_result = _single(
        [
            event
            for event in _events_of_type(events, "vla_result")
            if event["data"].get("status") == "cancelled"
        ],
        "cancelled late model result",
    )
    completion_drop = _single(
        _events_of_type(events, "async_completion_dropped"),
        "dropped late completion",
    )
    if (
        completion_drop["data"].get("reason") != "completion_after_cancel"
        or not (
            revocation["seq"]
            < cancelled_result["seq"]
            < completion_drop["seq"]
        )
    ):
        raise EmergencyVideoError(
            "late completion was not dropped after authority revocation"
        )
    target_relative = _cancelled_target(cancelled_result)

    dispatches = _events_of_type(events, "walking_target_dispatch")
    if dispatches:
        raise EmergencyVideoError("T3 must contain zero walking-target dispatches")
    run_end = _single(_events_of_type(events, "run_end"), "run end")
    if (
        run_end["data"].get("trial_id") != "T3"
        or run_end["data"].get("status") != "complete"
        or run_end["data"].get("recording_dispatch_calls") != 0
    ):
        raise EmergencyVideoError(
            "T3 run must complete with zero walking backend calls"
        )

    ball_writes = [
        event
        for event in _events_of_type(events, "bb_write")
        if event["data"].get("key") == "ball-state"
        and event["seq"] < emergency["seq"]
    ]
    if not ball_writes:
        raise EmergencyVideoError("T3 event stream has no captured ball state")
    ball = _triple(ball_writes[0]["data"].get("preview"), "captured ball state")
    target_field = (
        ball[0] + target_relative[0],
        ball[1] + target_relative[1],
        target_relative[2],
    )

    submit_raw = submission["unix_ms"] / 1000.0 - first_frame_epoch
    emergency_raw = emergency["unix_ms"] / 1000.0 - first_frame_epoch
    revoke_raw = revocation["unix_ms"] / 1000.0 - first_frame_epoch
    completion_raw = cancelled_result["unix_ms"] / 1000.0 - first_frame_epoch
    clip_start = submit_raw - request_lead
    if clip_start < 0.0:
        raise EmergencyVideoError("capture does not contain the configured request lead")
    if clip_start + duration > last_frame_epoch - first_frame_epoch:
        raise EmergencyVideoError("capture does not cover the complete T3 shot")
    if not submit_raw < emergency_raw <= revoke_raw < completion_raw:
        raise EmergencyVideoError(
            "T3 request, emergency, revocation and completion timing is not ordered"
        )

    return EmergencyTimeline(
        run_id=str(events[0].get("run_id", "")),
        trial_id="T3",
        acceptance_policy="invocation_scoped",
        job_id=job_id,
        generation=generation,
        captured_context_id=context_id,
        clip_start_seconds=clip_start,
        duration_seconds=duration,
        submit_seconds=submit_raw - clip_start,
        emergency_seconds=emergency_raw - clip_start,
        revoke_seconds=revoke_raw - clip_start,
        completion_seconds=completion_raw - clip_start,
        emergency_tick=emergency_tick,
        safe_stand_tick=safe_tick,
        safe_stand_tick_delta=safe_tick - emergency_tick,
        authority_reason="branch_revoked",
        completion_reason="completion_after_cancel",
        recording_dispatch_calls=0,
        ball_field_position_m=ball,
        target_ball_relative=target_relative,
        revoked_target_field_position_m=target_field,
    )


def validate_live_manifest(
    manifest: dict[str, Any], timeline: EmergencyTimeline, events_sha256: str
) -> None:
    if manifest.get("schema_version") != "humanoid.booster_live_trial.v1":
        raise EmergencyVideoError("live manifest has an unsupported schema")
    if manifest.get("status") != "completed" or manifest.get("return_code") != 0:
        raise EmergencyVideoError("live manifest is not a completed successful run")
    if manifest.get("trial_id") != "T3" or manifest.get("run_id") != timeline.run_id:
        raise EmergencyVideoError("live manifest does not identify the T3 event stream")
    if manifest.get("safety_profile") != "full_host_envelope":
        raise EmergencyVideoError("T3 did not retain the full host safety envelope")
    event_log = manifest.get("event_log")
    if (
        not isinstance(event_log, dict)
        or event_log.get("sha256") != f"sha256:{events_sha256}"
    ):
        raise EmergencyVideoError("live manifest is not bound to the event stream")


def _ass_time(seconds: float) -> str:
    centiseconds = max(0, round(seconds * 100.0))
    hours, remainder = divmod(centiseconds, 360000)
    minutes, remainder = divmod(remainder, 6000)
    whole_seconds, centiseconds = divmod(remainder, 100)
    return f"{hours}:{minutes:02d}:{whole_seconds:02d}.{centiseconds:02d}"


def _ass_escape(value: str) -> str:
    return value.replace("\\", r"\\").replace("{", r"\{").replace("}", r"\}")


def _dialogue(
    start: float, end: float, style: str, text: str, layer: int = 0
) -> str:
    return (
        f"Dialogue: {layer},{_ass_time(start)},{_ass_time(end)},"
        f"{style},,0,0,0,,{text}"
    )


def _calibrated_point(shot: dict[str, Any], name: str) -> tuple[int, int]:
    calibration = shot.get("fixed_camera_pixel_calibration")
    value = calibration.get(name) if isinstance(calibration, dict) else None
    if (
        not isinstance(value, list)
        or len(value) != 2
        or any(isinstance(item, bool) or not isinstance(item, int) for item in value)
    ):
        raise EmergencyVideoError(f"missing fixed-camera calibration point: {name}")
    return int(value[0]), int(value[1])


def generate_ass(timeline: EmergencyTimeline, shot: dict[str, Any]) -> str:
    output = shot.get("output")
    if not isinstance(output, dict):
        raise EmergencyVideoError("shot output configuration is missing")
    width = int(_positive_number(output.get("width"), "output width"))
    height = int(_positive_number(output.get("height"), "output height"))
    robot_x, robot_y = _calibrated_point(shot, "robot_start")
    target_x, target_y = _calibrated_point(shot, "revoked_target")
    end = timeline.duration_seconds
    submit = timeline.submit_seconds
    emergency = timeline.emergency_seconds
    completion = timeline.completion_seconds
    context = _ass_escape(timeline.captured_context_id)
    job = _ass_escape(timeline.job_id)

    header = f"""[Script Info]
ScriptType: v4.00+
PlayResX: {width}
PlayResY: {height}
WrapStyle: 2
ScaledBorderAndShadow: yes

[V4+ Styles]
Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding
Style: Overall,DejaVu Sans,35,&H00FFFFFF,&H00FFFFFF,&H00131A20,&HDA111820,-1,0,0,0,100,100,1,0,3,2,0,8,32,32,24,1
Style: Header,DejaVu Sans,48,&H00FFFFFF,&H00FFFFFF,&H00131A20,&HC8182028,-1,0,0,0,100,100,0,0,3,2,0,7,62,62,84,1
Style: HeaderAmber,DejaVu Sans,48,&H0014A5FF,&H0014A5FF,&H00131A20,&HC8182028,-1,0,0,0,100,100,0,0,3,2,0,7,62,62,84,1
Style: HeaderRed,DejaVu Sans,48,&H003B4CFF,&H003B4CFF,&H00131A20,&HC8182028,-1,0,0,0,100,100,0,0,3,2,0,7,62,62,84,1
Style: HeaderGreen,DejaVu Sans,48,&H006BE62E,&H006BE62E,&H00131A20,&HC8182028,-1,0,0,0,100,100,0,0,3,2,0,7,62,62,84,1
Style: Body,DejaVu Sans,29,&H00FFFFFF,&H00FFFFFF,&H00131A20,&HBC182028,0,0,0,0,100,100,0,0,3,1,0,7,66,66,150,1
Style: Badge,DejaVu Sans,25,&H00FFFFFF,&H00FFFFFF,&H00131A20,&HB0182028,-1,0,0,0,100,100,1,0,3,1,0,9,42,42,42,1
Style: Alert,DejaVu Sans,94,&H003B4CFF,&H003B4CFF,&H00131A20,&H98182028,-1,0,0,0,100,100,0,0,3,3,0,5,0,0,0,1
Style: SafeMarker,DejaVu Sans,40,&H006BE62E,&H006BE62E,&H00131A20,&HC0182028,-1,0,0,0,100,100,0,0,3,2,0,2,0,0,0,1
Style: TargetRed,DejaVu Sans,92,&H003B4CFF,&H003B4CFF,&H00131A20,&H00000000,-1,0,0,0,100,100,0,0,1,5,0,5,0,0,0,1
Style: RedLabel,DejaVu Sans,25,&H003B4CFF,&H003B4CFF,&H00131A20,&HBC182028,-1,0,0,0,100,100,0,0,3,2,0,2,0,0,0,1
Style: PipelineRed,DejaVu Sans,33,&H003B4CFF,&H003B4CFF,&H00131A20,&HDC182028,-1,0,0,0,100,100,0,0,3,2,0,2,0,0,0,1
Style: PipelineGreen,DejaVu Sans,33,&H006BE62E,&H006BE62E,&H00131A20,&HDC182028,-1,0,0,0,100,100,0,0,3,2,0,2,0,0,0,1
Style: NoCommand,DejaVu Sans,25,&H00FFFFFF,&H00FFFFFF,&H00131A20,&HD0182028,-1,0,0,0,100,100,0,0,3,1,0,2,0,0,0,1
Style: Vector,Arial,20,&H003B4CFF,&H003B4CFF,&H003B4CFF,&H00000000,0,0,0,0,100,100,0,0,1,0,0,7,0,0,0,1

[Events]
Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text
"""
    border = (
        r"{\p1\c&H183B4C&\1a&H25&}"
        r"m 0 0 l 1920 0 l 1920 18 l 0 18 "
        r"m 0 1062 l 1920 1062 l 1920 1080 l 0 1080 "
        r"m 0 18 l 18 18 l 18 1062 l 0 1062 "
        r"m 1902 18 l 1920 18 l 1920 1062 l 1902 1062{\p0}"
    )
    flash = (
        r"{\p1\c&H183B4C&\1a&HC5&\fad(40,180)}"
        r"m 0 0 l 1920 0 l 1920 1080 l 0 1080{\p0}"
    )
    rows = [
        _dialogue(
            0.0,
            end,
            "Overall",
            r"{\an8\pos(960,22)}T3  ·  HIGHER-PRIORITY SAFETY INTERRUPTION",
            5,
        ),
        _dialogue(
            0.0,
            end,
            "Badge",
            r"{\an9\pos(1860,54)}BOOSTER K1  ·  SIMULATION",
            5,
        ),
        _dialogue(
            0.0,
            submit,
            "Header",
            r"ROBOT STABLE  ·  SAFETY BRANCH READY",
            3,
        ),
        _dialogue(
            0.0,
            submit,
            "Body",
            rf"NO WALKING TARGET\NCURRENT CONTEXT  ·  {context}",
            3,
        ),
        _dialogue(
            submit,
            emergency,
            "HeaderAmber",
            r"MODEL REQUEST PENDING",
            3,
        ),
        _dialogue(
            submit,
            emergency,
            "Body",
            (
                rf"JOB {job}  ·  GENERATION {timeline.generation}\N"
                rf"ACTIVE BRANCH  ·  model_wait  ·  CAPTURED {context}"
            ),
            3,
        ),
        _dialogue(
            emergency,
            min(end, emergency + 0.45),
            "Vector",
            flash,
            6,
        ),
        _dialogue(emergency, end, "Vector", border, 6),
        _dialogue(
            emergency,
            completion,
            "HeaderRed",
            r"SOFTWARE EMERGENCY ASSERTED",
            7,
        ),
        _dialogue(
            emergency,
            completion,
            "Body",
            (
                r"ACTIVE BRANCH  ·  safe_stand  ·  REQUEST STATE  ·  revoked\N"
                r"AUTHORITY REVOKED  ·  branch_revoked  ·  TARGET STATE  ·  none"
            ),
            7,
        ),
        _dialogue(
            emergency,
            min(end, emergency + 1.1),
            "Alert",
            r"{\an5\pos(1580,540)\fad(70,180)}!",
            7,
        ),
        _dialogue(
            emergency,
            end,
            "SafeMarker",
            rf"{{\an2\pos({robot_x},{robot_y - 82})}}SAFE STAND ACTIVE",
            7,
        ),
        _dialogue(
            emergency,
            completion,
            "PipelineRed",
            (
                r"{\an2\pos(960,1014)}AUTHORITY REVOKED IMMEDIATELY"
                r"  ·  WALKING INHIBITED"
            ),
            7,
        ),
        _dialogue(
            completion,
            end,
            "HeaderGreen",
            r"LATE MODEL COMPLETION DROPPED",
            7,
        ),
        _dialogue(
            completion,
            end,
            "Body",
            (
                r"ACTIVE BRANCH  ·  safe_stand  ·  completion_after_cancel\N"
                r"REVOKED GENERATION CANNOT REGAIN AUTHORITY"
            ),
            7,
        ),
        _dialogue(
            completion,
            end,
            "TargetRed",
            rf"{{\an5\pos({target_x},{target_y})\fad(100,0)}}⊗",
            7,
        ),
        _dialogue(
            completion,
            end,
            "RedLabel",
            rf"{{\an6\pos({target_x - 50},{target_y - 58})}}REVOKED TARGET  ·  NEVER DISPATCHED",
            7,
        ),
        _dialogue(
            completion,
            end,
            "PipelineGreen",
            (
                r"{\an2\pos(960,1014)}LATE COMPLETION DROPPED"
                r"  ·  ZERO WALKING DISPATCHES"
            ),
            8,
        ),
        _dialogue(
            min(end, completion + 0.8),
            end,
            "NoCommand",
            r"{\an2\pos(960,1060)}SAFE STAND HELD  ·  ROBOT DOES NOT WALK",
            8,
        ),
    ]
    return header + "\n".join(rows) + "\n"


def _sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def render(
    events_path: pathlib.Path,
    live_manifest_path: pathlib.Path,
    capture_timing_path: pathlib.Path,
    raw_video_path: pathlib.Path,
    shot_path: pathlib.Path,
    output_dir: pathlib.Path,
    *,
    force: bool,
) -> pathlib.Path:
    events_path = events_path.resolve()
    live_manifest_path = live_manifest_path.resolve()
    capture_timing_path = capture_timing_path.resolve()
    raw_video_path = raw_video_path.resolve()
    shot_path = shot_path.resolve()
    output_dir = output_dir.resolve()
    if output_dir.exists() and (not force or output_dir.is_symlink()):
        raise EmergencyVideoError(f"refusing existing output directory: {output_dir}")
    output_dir.mkdir(parents=True, exist_ok=True)

    shot = _read_json(shot_path)
    if shot.get("schema_version") != "humanoid.paper_video_emergency.v1":
        raise EmergencyVideoError("unsupported T3 video configuration")
    timeline = build_timeline(
        _load_events(events_path), _read_json(capture_timing_path), shot
    )
    validate_live_manifest(
        _read_json(live_manifest_path), timeline, _sha256(events_path)
    )
    overlay_path = output_dir / "t3-emergency.ass"
    overlay_path.write_text(generate_ass(timeline, shot), encoding="utf-8")

    crop = shot.get("source_crop")
    output = shot.get("output")
    if not isinstance(crop, dict) or not isinstance(output, dict):
        raise EmergencyVideoError("shot crop or output configuration is missing")
    crop_values = {
        key: int(_positive_number(crop.get(key), f"crop {key}"))
        for key in ("x", "y", "width", "height")
    }
    output_width = int(_positive_number(output.get("width"), "output width"))
    output_height = int(_positive_number(output.get("height"), "output height"))
    fps = int(_positive_number(output.get("fps"), "output fps"))
    video_path = output_dir / "t3-polished-emergency.mp4"
    filters = (
        f"crop={crop_values['width']}:{crop_values['height']}:"
        f"{crop_values['x']}:{crop_values['y']},"
        f"scale={output_width}:{output_height}:flags=lanczos,"
        f"ass={overlay_path.name},"
        f"fade=t=in:st=0:d=0.25,"
        f"fade=t=out:st={timeline.duration_seconds - 0.25:.3f}:d=0.25"
    )
    completed = subprocess.run(
        [
            "ffmpeg",
            "-hide_banner",
            "-loglevel",
            "error",
            "-y",
            "-ss",
            f"{timeline.clip_start_seconds:.6f}",
            "-i",
            str(raw_video_path),
            "-t",
            f"{timeline.duration_seconds:.3f}",
            "-vf",
            filters,
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
        raise EmergencyVideoError(
            f"ffmpeg render failed with code {completed.returncode}"
        )

    manifest = {
        "schema_version": "humanoid.t3_polished_emergency.v1",
        "shot_id": shot.get("shot_id"),
        "timeline": dataclasses.asdict(timeline),
        "paper_claim": {
            "software_emergency_during_request": True,
            "safe_stand_within_one_bt_tick": True,
            "authority_revoked": True,
            "late_completion_dropped": True,
            "walking_target_dispatch_calls": 0,
            "full_host_safety_envelope": True,
        },
        "inputs": {
            "events_sha256": _sha256(events_path),
            "live_manifest_sha256": _sha256(live_manifest_path),
            "capture_timing_sha256": _sha256(capture_timing_path),
            "raw_video_sha256": _sha256(raw_video_path),
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
    parser.add_argument("--events", required=True, type=pathlib.Path)
    parser.add_argument("--live-manifest", required=True, type=pathlib.Path)
    parser.add_argument("--capture-timing", required=True, type=pathlib.Path)
    parser.add_argument("--raw-video", required=True, type=pathlib.Path)
    parser.add_argument(
        "--shot",
        type=pathlib.Path,
        default=pathlib.Path(__file__).with_name("t3_emergency.json"),
    )
    parser.add_argument("--output-dir", required=True, type=pathlib.Path)
    parser.add_argument("--force", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        video = render(
            args.events,
            args.live_manifest,
            args.capture_timing,
            args.raw_video,
            args.shot,
            args.output_dir,
            force=args.force,
        )
    except EmergencyVideoError as exc:
        print(f"T3 emergency video error: {exc}")
        return 2
    print(f"PASS wrote polished T3 emergency video: {video}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
