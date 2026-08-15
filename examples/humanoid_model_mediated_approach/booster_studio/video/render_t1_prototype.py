#!/usr/bin/env python3
"""Render the polished, event-aligned T1 paper-video prototype."""

from __future__ import annotations

import argparse
import dataclasses
import hashlib
import json
import math
import pathlib
import subprocess
from typing import Any


class PrototypeError(RuntimeError):
    """The inputs cannot produce trustworthy T1 video evidence."""


@dataclasses.dataclass(frozen=True)
class Timeline:
    run_id: str
    job_id: str
    generation: int
    context_id: str
    clip_start_seconds: float
    duration_seconds: float
    submit_seconds: float
    accept_seconds: float
    dispatch_seconds: float
    ball_field_position_m: tuple[float, float, float]
    target_ball_relative: tuple[float, float, float]
    target_field_position_m: tuple[float, float, float]


def _read_json(path: pathlib.Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except Exception as exc:
        raise PrototypeError(f"failed to read JSON {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise PrototypeError(f"expected a JSON object: {path}")
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
            raise PrototypeError(f"{path}:{line_number}: invalid JSON: {exc}") from exc
        if (
            not isinstance(event, dict)
            or event.get("schema") != "mbt.evt.v1"
            or not isinstance(event.get("seq"), int)
            or not isinstance(event.get("unix_ms"), int)
            or not isinstance(event.get("data"), dict)
        ):
            raise PrototypeError(
                f"{path}:{line_number}: incomplete canonical event envelope"
            )
        events.append(event)
    if not events:
        raise PrototypeError(f"empty event stream: {path}")
    sequences = [event["seq"] for event in events]
    if sequences != list(range(sequences[0], sequences[0] + len(sequences))):
        raise PrototypeError("canonical event sequence is not contiguous")
    if events[0].get("type") != "run_start" or events[-1].get("type") != "run_end":
        raise PrototypeError("event stream must begin with run_start and end with run_end")
    return events


def _events_of_type(
    events: list[dict[str, Any]], event_type: str
) -> list[dict[str, Any]]:
    return [event for event in events if event.get("type") == event_type]


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
        raise PrototypeError(f"{field} must contain three finite numbers")
    return tuple(float(item) for item in value)  # type: ignore[return-value]


def _positive_number(value: object, field: str) -> float:
    if (
        isinstance(value, bool)
        or not isinstance(value, (int, float))
        or not math.isfinite(float(value))
        or float(value) <= 0.0
    ):
        raise PrototypeError(f"{field} must be a positive finite number")
    return float(value)


def build_timeline(
    events: list[dict[str, Any]],
    capture: dict[str, Any],
    shot: dict[str, Any],
) -> Timeline:
    if capture.get("schema_version") != "humanoid.clean_simulator_capture.v1":
        raise PrototypeError("capture timing is not a clean simulator capture")
    first_frame_epoch = _positive_number(
        capture.get("first_frame_epoch"), "first frame epoch"
    )
    last_frame_epoch = _positive_number(
        capture.get("last_frame_epoch"), "last frame epoch"
    )
    if last_frame_epoch <= first_frame_epoch:
        raise PrototypeError("capture timing ends before it starts")
    duration = _positive_number(shot.get("duration_seconds"), "shot duration")
    request_lead = _positive_number(
        shot.get("request_lead_seconds"), "request lead"
    )

    submissions = _events_of_type(events, "vla_submit")
    decisions = [
        event
        for event in _events_of_type(events, "vla_result")
        if "decision" in event["data"]
    ]
    dispatches = _events_of_type(events, "walking_target_dispatch")
    if len(submissions) != 1 or len(decisions) != 1 or len(dispatches) != 1:
        raise PrototypeError("T1 requires one submission, decision and dispatch")
    submission = submissions[0]
    decision = decisions[0]
    dispatch = dispatches[0]
    if decision["data"].get("decision") != "accepted":
        raise PrototypeError("T1 runtime decision is not accepted")
    if dispatch["data"].get("decision") != "accepted":
        raise PrototypeError("T1 walking target dispatch is not accepted")

    job_id = str(submission["data"].get("job_id", ""))
    generation = submission["data"].get("generation")
    context_id = str(submission["data"].get("captured_context_id", ""))
    if not job_id or not isinstance(generation, int) or not context_id:
        raise PrototypeError("submission identity is incomplete")
    for event in (decision, dispatch):
        data = event["data"]
        if (
            str(data.get("job_id")) != job_id
            or data.get("generation") != generation
            or data.get("captured_context_id") != context_id
            or data.get("current_context_id") != context_id
        ):
            raise PrototypeError("T1 result is not current and invocation-correlated")

    ball_writes = [
        event
        for event in _events_of_type(events, "bb_write")
        if event["data"].get("key") == "ball-state"
    ]
    if not ball_writes:
        raise PrototypeError("event stream has no captured ball state")
    ball = _triple(ball_writes[0]["data"].get("preview"), "ball state")
    target_data = dispatch["data"].get("target")
    if not isinstance(target_data, dict) or target_data.get("frame_id") != "ball_context":
        raise PrototypeError("T1 target must use the ball_context frame")
    target_relative = _triple(
        [target_data.get("x_m"), target_data.get("y_m"), target_data.get("yaw_rad")],
        "walking target",
    )
    target_field = (
        ball[0] + target_relative[0],
        ball[1] + target_relative[1],
        target_relative[2],
    )

    submit_raw = submission["unix_ms"] / 1000.0 - first_frame_epoch
    accept_raw = decision["unix_ms"] / 1000.0 - first_frame_epoch
    dispatch_raw = dispatch["unix_ms"] / 1000.0 - first_frame_epoch
    clip_start = submit_raw - request_lead
    if clip_start < 0.0:
        raise PrototypeError("capture does not contain the configured request lead")
    if clip_start + duration > last_frame_epoch - first_frame_epoch:
        raise PrototypeError("capture does not cover the complete prototype duration")
    if not clip_start < submit_raw < accept_raw <= dispatch_raw:
        raise PrototypeError("T1 event timing is not ordered")

    run_id = str(events[0].get("run_id", ""))
    return Timeline(
        run_id=run_id,
        job_id=job_id,
        generation=generation,
        context_id=context_id,
        clip_start_seconds=clip_start,
        duration_seconds=duration,
        submit_seconds=submit_raw - clip_start,
        accept_seconds=accept_raw - clip_start,
        dispatch_seconds=dispatch_raw - clip_start,
        ball_field_position_m=ball,
        target_ball_relative=target_relative,
        target_field_position_m=target_field,
    )


def _ass_time(seconds: float) -> str:
    centiseconds = max(0, round(seconds * 100.0))
    hours, remainder = divmod(centiseconds, 360000)
    minutes, remainder = divmod(remainder, 6000)
    whole_seconds, centiseconds = divmod(remainder, 100)
    return f"{hours}:{minutes:02d}:{whole_seconds:02d}.{centiseconds:02d}"


def _ass_escape(value: str) -> str:
    return value.replace("\\", r"\\").replace("{", r"\{").replace("}", r"\}")


def _dialogue(start: float, end: float, style: str, text: str) -> str:
    return (
        f"Dialogue: 0,{_ass_time(start)},{_ass_time(end)},{style},,0,0,0,,{text}"
    )


def _calibrated_point(shot: dict[str, Any], name: str) -> tuple[int, int]:
    calibration = shot.get("fixed_camera_pixel_calibration")
    value = calibration.get(name) if isinstance(calibration, dict) else None
    if (
        not isinstance(value, list)
        or len(value) != 2
        or any(isinstance(item, bool) or not isinstance(item, int) for item in value)
    ):
        raise PrototypeError(f"missing fixed-camera calibration point: {name}")
    return int(value[0]), int(value[1])


def generate_ass(timeline: Timeline, shot: dict[str, Any]) -> str:
    output = shot.get("output")
    if not isinstance(output, dict):
        raise PrototypeError("shot output configuration is missing")
    width = int(_positive_number(output.get("width"), "output width"))
    height = int(_positive_number(output.get("height"), "output height"))
    robot_x, robot_y = _calibrated_point(shot, "robot_start")
    target_x, target_y = _calibrated_point(shot, "current_target")
    end = timeline.duration_seconds
    submit = timeline.submit_seconds
    accept = timeline.accept_seconds
    dispatch = timeline.dispatch_seconds
    context = _ass_escape(timeline.context_id)

    header = f"""[Script Info]
ScriptType: v4.00+
PlayResX: {width}
PlayResY: {height}
WrapStyle: 2
ScaledBorderAndShadow: yes

[V4+ Styles]
Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding
Style: Header,DejaVu Sans,52,&H00FFFFFF,&H00FFFFFF,&H00131A20,&HC4182028,-1,0,0,0,100,100,0,0,3,2,0,7,62,62,54,1
Style: Body,DejaVu Sans,30,&H00FFFFFF,&H00FFFFFF,&H00131A20,&HBC182028,0,0,0,0,100,100,0,0,3,1,0,7,66,66,126,1
Style: Badge,DejaVu Sans,25,&H00FFFFFF,&H00FFFFFF,&H00131A20,&HB0182028,-1,0,0,0,100,100,1,0,3,1,0,9,42,42,42,1
Style: Note,DejaVu Sans,27,&H00FFFFFF,&H00FFFFFF,&H00131A20,&HC0182028,-1,0,0,0,100,100,0,0,3,1,0,1,68,68,54,1
Style: Marker,DejaVu Sans,88,&H006BE62E,&H006BE62E,&H00101A12,&H00000000,-1,0,0,0,100,100,0,0,1,5,0,5,0,0,0,1
Style: MarkerLabel,DejaVu Sans,27,&H006BE62E,&H006BE62E,&H00101A12,&HB0101812,-1,0,0,0,100,100,0,0,3,2,0,2,0,0,0,1
Style: Success,DejaVu Sans,34,&H006BE62E,&H006BE62E,&H00131A20,&HC0182028,-1,0,0,0,100,100,0,0,3,2,0,3,64,64,58,1
Style: Vector,Arial,20,&H806BE62E,&H806BE62E,&H806BE62E,&H00000000,0,0,0,0,100,100,0,0,1,0,0,7,0,0,0,1

[Events]
Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text
"""
    rows = [
        _dialogue(
            0.0,
            end,
            "Badge",
            r"{\an9\pos(1860,54)}BOOSTER K1  ·  SIMULATION",
        ),
        _dialogue(
            0.0,
            submit,
            "Header",
            r"{\c&H00FFFFFF&}T1  ·  NORMAL RESULT",
        ),
        _dialogue(
            0.0,
            submit,
            "Body",
            rf"BALL STATIONARY\NCURRENT CONTEXT  ·  {context}",
        ),
        _dialogue(
            submit,
            accept,
            "Header",
            r"{\c&H0014A5FF&}MODEL REQUEST PENDING",
        ),
        _dialogue(
            submit,
            accept,
            "Body",
            (
                f"JOB {_ass_escape(timeline.job_id)}  ·  "
                f"GENERATION {timeline.generation}\\N"
                f"CAPTURED CONTEXT  ·  {context}"
            ),
        ),
        _dialogue(
            submit,
            accept,
            "Note",
            r"{\c&H0014A5FF&}FIXED 2.5 s MODEL DELAY  ·  BT STILL TICKING",
        ),
        _dialogue(
            accept,
            end,
            "Header",
            r"{\c&H006BE62E&}CURRENT RESULT ACCEPTED",
        ),
        _dialogue(
            accept,
            end,
            "Body",
            (
                f"CAPTURED CONTEXT = CURRENT CONTEXT  ·  {context}\\N"
                "WALKING TARGET DISPATCHED EXACTLY ONCE"
            ),
        ),
        _dialogue(
            dispatch,
            end,
            "Vector",
            (
                rf"{{\p1\c&H806BE62E&}}m {robot_x} {robot_y - 7} "
                rf"l {target_x} {target_y - 7} l {target_x} {target_y + 7} "
                rf"l {robot_x} {robot_y + 7}{{\p0}}"
            ),
        ),
        _dialogue(
            dispatch,
            end,
            "Marker",
            rf"{{\an5\pos({target_x},{target_y})\fad(120,0)}}⊕",
        ),
        _dialogue(
            dispatch,
            end,
            "MarkerLabel",
            rf"{{\an6\pos({target_x - 50},{target_y - 55})}}CURRENT TARGET",
        ),
        _dialogue(
            min(end, dispatch + 1.0),
            end,
            "Success",
            r"ROBOT WALKS TO THE ACCEPTED CURRENT TARGET  ✓",
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
    capture_timing_path: pathlib.Path,
    raw_video: pathlib.Path,
    shot_path: pathlib.Path,
    output_dir: pathlib.Path,
    *,
    force: bool,
) -> pathlib.Path:
    events_path = events_path.resolve()
    capture_timing_path = capture_timing_path.resolve()
    raw_video = raw_video.resolve()
    shot_path = shot_path.resolve()
    output_dir = output_dir.resolve()
    if output_dir.exists() and (not force or output_dir.is_symlink()):
        raise PrototypeError(f"refusing existing output directory: {output_dir}")
    output_dir.mkdir(parents=True, exist_ok=True)

    events = _load_events(events_path)
    capture = _read_json(capture_timing_path)
    shot = _read_json(shot_path)
    timeline = build_timeline(events, capture, shot)
    overlay_path = output_dir / "t1-prototype.ass"
    overlay_path.write_text(generate_ass(timeline, shot), encoding="utf-8")

    crop = shot.get("source_crop")
    output = shot.get("output")
    if not isinstance(crop, dict) or not isinstance(output, dict):
        raise PrototypeError("shot crop or output configuration is missing")
    crop_values = {
        key: int(_positive_number(crop.get(key), f"crop {key}"))
        for key in ("x", "y", "width", "height")
    }
    output_width = int(_positive_number(output.get("width"), "output width"))
    output_height = int(_positive_number(output.get("height"), "output height"))
    fps = int(_positive_number(output.get("fps"), "output fps"))
    video_path = output_dir / "t1-polished-prototype.mp4"
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
            str(raw_video),
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
        raise PrototypeError(f"ffmpeg render failed with code {completed.returncode}")

    manifest = {
        "schema_version": "humanoid.t1_polished_prototype.v1",
        "run_id": timeline.run_id,
        "shot_id": shot.get("shot_id"),
        "timeline": dataclasses.asdict(timeline),
        "inputs": {
            "events_sha256": _sha256(events_path),
            "capture_timing_sha256": _sha256(capture_timing_path),
            "raw_video_sha256": _sha256(raw_video),
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
    parser.add_argument("--capture-timing", required=True, type=pathlib.Path)
    parser.add_argument("--raw-video", required=True, type=pathlib.Path)
    parser.add_argument(
        "--shot",
        type=pathlib.Path,
        default=pathlib.Path(__file__).with_name("t1_prototype.json"),
    )
    parser.add_argument("--output-dir", required=True, type=pathlib.Path)
    parser.add_argument("--force", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        video = render(
            args.events,
            args.capture_timing,
            args.raw_video,
            args.shot,
            args.output_dir,
            force=args.force,
        )
    except PrototypeError as exc:
        print(f"T1 prototype error: {exc}")
        return 2
    print(f"PASS wrote polished T1 prototype: {video}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
