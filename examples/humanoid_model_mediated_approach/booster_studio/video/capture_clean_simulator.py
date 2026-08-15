#!/usr/bin/env python3
"""Capture a clean Booster Studio simulator viewport through Chromium CDP."""

from __future__ import annotations

import argparse
import base64
import json
import pathlib
import subprocess
import time
import urllib.request
from typing import Any

from websocket import create_connection

STYLE_ID = "muesli-paper-video-clean-capture"
CLEAN_UI_SCRIPT = f"""
(() => {{
  document.getElementById('{STYLE_ID}')?.remove();
  const style = document.createElement('style');
  style.id = '{STYLE_ID}';
  style.textContent = `
    main.booster-wasm-simulator > :not(canvas) {{
      display: none !important;
    }}
  `;
  document.head.appendChild(style);
  return {{
    canvas: Boolean(document.querySelector('main.booster-wasm-simulator canvas')),
    viewport: [window.innerWidth, window.innerHeight]
  }};
}})()
"""
RESTORE_UI_SCRIPT = f"document.getElementById('{STYLE_ID}')?.remove()"


def _send(socket: Any, message_id: int, method: str, params: dict | None = None) -> None:
    message: dict[str, object] = {"id": message_id, "method": method}
    if params is not None:
        message["params"] = params
    socket.send(json.dumps(message))


def _wait_for_response(socket: Any, message_id: int) -> dict:
    while True:
        response = json.loads(socket.recv())
        if response.get("id") == message_id:
            return response


def _evaluate(socket: Any, message_id: int, expression: str) -> dict:
    _send(
        socket,
        message_id,
        "Runtime.evaluate",
        {"expression": expression, "returnByValue": True},
    )
    response = _wait_for_response(socket, message_id)
    result = response.get("result", {}).get("result", {})
    if result.get("subtype") == "error":
        raise RuntimeError(f"simulator UI script failed: {result}")
    value = result.get("value")
    return value if isinstance(value, dict) else {}


def capture(output_dir: pathlib.Path, duration: float, endpoint: str) -> pathlib.Path:
    output_dir = output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=False)
    frames_dir = output_dir / "frames"
    frames_dir.mkdir()

    pages = json.load(urllib.request.urlopen(f"{endpoint}/json/list"))
    page = next(page for page in pages if "Simulator" in page.get("title", ""))
    socket = create_connection(
        page["webSocketDebuggerUrl"], timeout=5, suppress_origin=True
    )
    clean_state = _evaluate(socket, 1, CLEAN_UI_SCRIPT)
    if not clean_state.get("canvas"):
        socket.close()
        raise RuntimeError("Booster simulator canvas was not found")

    _send(
        socket,
        2,
        "Page.startScreencast",
        {
            "format": "jpeg",
            "quality": 94,
            "maxWidth": 1280,
            "maxHeight": 720,
            "everyNthFrame": 1,
        },
    )

    frames: list[dict[str, float | str]] = []
    started = time.monotonic()
    print(f"CAPTURE_READY epoch={time.time():.6f}", flush=True)
    try:
        while time.monotonic() - started < duration:
            message = json.loads(socket.recv())
            if message.get("method") != "Page.screencastFrame":
                continue
            params = message["params"]
            index = len(frames) + 1
            path = frames_dir / f"frame-{index:05d}.jpg"
            path.write_bytes(base64.b64decode(params["data"]))
            frames.append(
                {
                    "path": str(path),
                    "arrival_epoch": time.time(),
                    "session_time": float(params["metadata"].get("timestamp", 0.0)),
                }
            )
            _send(
                socket,
                1000 + index,
                "Page.screencastFrameAck",
                {"sessionId": params["sessionId"]},
            )
    finally:
        _send(socket, 3, "Page.stopScreencast")
        _evaluate(socket, 4, RESTORE_UI_SCRIPT)
        socket.close()

    if len(frames) < 2:
        raise RuntimeError("screencast produced fewer than two frames")

    timing = {
        "schema_version": "humanoid.clean_simulator_capture.v1",
        "clean_ui": True,
        "viewport": clean_state.get("viewport"),
        "frame_count": len(frames),
        "first_frame_epoch": frames[0]["arrival_epoch"],
        "last_frame_epoch": frames[-1]["arrival_epoch"],
        "frames": frames,
    }
    (output_dir / "capture-timing.json").write_text(
        json.dumps(timing, indent=2) + "\n", encoding="utf-8"
    )

    concat_path = output_dir / "frames.txt"
    with concat_path.open("w", encoding="utf-8") as handle:
        for index, frame in enumerate(frames[:-1]):
            frame_duration = float(frames[index + 1]["arrival_epoch"]) - float(
                frame["arrival_epoch"]
            )
            handle.write(f"file '{frame['path']}'\n")
            handle.write(f"duration {max(0.001, frame_duration):.6f}\n")
        handle.write(f"file '{frames[-1]['path']}'\n")
        handle.write(f"file '{frames[-1]['path']}'\n")

    raw_path = output_dir / "capture-full.mp4"
    subprocess.run(
        [
            "ffmpeg",
            "-hide_banner",
            "-loglevel",
            "error",
            "-y",
            "-f",
            "concat",
            "-safe",
            "0",
            "-i",
            str(concat_path),
            "-vf",
            "fps=30",
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
            str(raw_path),
        ],
        check=True,
    )
    for frame in frames_dir.iterdir():
        frame.unlink()
    frames_dir.rmdir()
    concat_path.unlink()
    print(
        json.dumps(
            {
                "raw_video": str(raw_path),
                "frames": len(frames),
                "first_frame_epoch": frames[0]["arrival_epoch"],
                "last_frame_epoch": frames[-1]["arrival_epoch"],
            }
        ),
        flush=True,
    )
    return raw_path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", required=True, type=pathlib.Path)
    parser.add_argument("--duration", type=float, default=15.0)
    parser.add_argument("--endpoint", default="http://127.0.0.1:9223")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if not 1.0 <= args.duration <= 120.0:
        raise SystemExit("capture duration must be between 1 and 120 seconds")
    capture(args.output_dir, args.duration, args.endpoint.rstrip("/"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
