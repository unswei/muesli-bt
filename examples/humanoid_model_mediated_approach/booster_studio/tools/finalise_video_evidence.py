#!/usr/bin/env python3
"""Align canonical evidence to a raw recording and burn the video overlay."""

from __future__ import annotations

import argparse
import json
import math
import pathlib
import shutil
import subprocess
import sys
import tempfile

STUDIO_ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(STUDIO_ROOT / "src"))

from muesli_booster.evidence import EvidenceError, write_overlay
from muesli_booster.native_payload import sha256_file
from muesli_booster.trial_runner import LIVE_MANIFEST_SCHEMA


class FinaliseError(RuntimeError):
    """Video evidence cannot be aligned or rendered safely."""


def _read_manifest(path: pathlib.Path) -> dict:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except Exception as exc:
        raise FinaliseError(f"failed to read {path}: {exc}") from exc
    if (
        not isinstance(value, dict)
        or value.get("schema_version") != LIVE_MANIFEST_SCHEMA
    ):
        raise FinaliseError(
            "run directory does not contain a live Booster trial manifest"
        )
    if value.get("status") != "completed":
        raise FinaliseError("only a completed live trial can be finalised")
    return value


def _write_json_atomic(path: pathlib.Path, value: object) -> None:
    with tempfile.NamedTemporaryFile(
        mode="w",
        encoding="utf-8",
        dir=path.parent,
        prefix=f".{path.name}.",
        delete=False,
    ) as handle:
        temporary = pathlib.Path(handle.name)
        json.dump(value, handle, indent=2, sort_keys=True)
        handle.write("\n")
    temporary.replace(path)


def finalise(
    run_dir: pathlib.Path,
    raw_source: pathlib.Path,
    request_cue_seconds: float,
    *,
    force: bool,
    ffmpeg: str,
) -> pathlib.Path:
    run_dir = run_dir.resolve()
    raw_source = raw_source.resolve()
    if not math.isfinite(request_cue_seconds) or request_cue_seconds < 0.0:
        raise FinaliseError("request cue time must be finite and non-negative")
    manifest_path = run_dir / "live-manifest.json"
    manifest = _read_manifest(manifest_path)
    event_path = run_dir / "events.jsonl"
    if not event_path.is_file() or not raw_source.is_file():
        raise FinaliseError("events.jsonl and the raw video must both exist")

    raw_path = run_dir / "raw-video.mp4"
    overlay_path = run_dir / "overlay.ass"
    video_path = run_dir / "overlay-video.mp4"
    for target in (raw_path, video_path):
        if target.exists() and target.resolve() != raw_source and not force:
            raise FinaliseError(
                f"refusing to replace existing artefact without --force: {target}"
            )
    if raw_path.resolve() != raw_source:
        shutil.copy2(raw_source, raw_path)
    try:
        write_overlay(
            event_path,
            overlay_path,
            request_cue_seconds=request_cue_seconds,
        )
    except EvidenceError as exc:
        raise FinaliseError(str(exc)) from exc

    command = [
        ffmpeg,
        "-hide_banner",
        "-loglevel",
        "error",
        "-y" if force else "-n",
        "-i",
        raw_path.name,
        "-vf",
        f"ass={overlay_path.name}",
        "-c:v",
        "libx264",
        "-crf",
        "18",
        "-preset",
        "medium",
        "-c:a",
        "copy",
        video_path.name,
    ]
    try:
        completed = subprocess.run(command, cwd=run_dir, check=False)
    except FileNotFoundError as exc:
        raise FinaliseError(f"ffmpeg executable not found: {ffmpeg}") from exc
    if completed.returncode != 0 or not video_path.is_file():
        raise FinaliseError(
            f"ffmpeg overlay render failed with code {completed.returncode}"
        )

    manifest["video_alignment"] = {
        "reference": "vla_submit_request_cue",
        "request_cue_seconds": request_cue_seconds,
    }
    manifest["raw_video"] = {
        "path": raw_path.name,
        "sha256": sha256_file(raw_path),
        "size_bytes": raw_path.stat().st_size,
    }
    manifest["overlay"] = {
        "path": overlay_path.name,
        "sha256": sha256_file(overlay_path),
        "alignment": "vla_submit_request_cue",
    }
    manifest["overlay_video"] = {
        "path": video_path.name,
        "sha256": sha256_file(video_path),
        "size_bytes": video_path.stat().st_size,
    }
    _write_json_atomic(manifest_path, manifest)
    return video_path


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run-dir", required=True, type=pathlib.Path)
    parser.add_argument("--raw-video", required=True, type=pathlib.Path)
    parser.add_argument("--request-cue-seconds", required=True, type=float)
    parser.add_argument("--ffmpeg", default="ffmpeg")
    parser.add_argument("--force", action="store_true")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    try:
        video = finalise(
            args.run_dir,
            args.raw_video,
            args.request_cue_seconds,
            force=args.force,
            ffmpeg=args.ffmpeg,
        )
        print(f"PASS wrote aligned overlay video: {video}")
        return 0
    except FinaliseError as exc:
        print(f"video evidence error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
