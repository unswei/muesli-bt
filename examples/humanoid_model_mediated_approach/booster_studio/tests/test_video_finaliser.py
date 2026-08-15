from __future__ import annotations

import json
import pathlib
import sys
import tempfile
import unittest

PROJECT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT / "tools"))

import finalise_video_evidence as finaliser


class VideoFinaliserTests(unittest.TestCase):
    def test_finaliser_aligns_overlay_and_hashes_video_artefacts(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            run_dir = root / "run"
            run_dir.mkdir()
            manifest = {
                "schema_version": "humanoid.booster_live_trial.v1",
                "status": "completed",
                "trial_id": "T1",
                "run_id": "test-t1",
                "source_git_commit": "test",
                "source_git_dirty": False,
                "payload_manifest_sha256": "sha256:" + "0" * 64,
                "motion_enabled": True,
                "runner_command": ["runner"],
                "event_log": {"path": "events.jsonl", "schema": "mbt.evt.v1"},
            }
            (run_dir / "live-manifest.json").write_text(
                json.dumps(manifest), encoding="utf-8"
            )
            events = [
                {
                    "schema": "mbt.evt.v1",
                    "seq": 1,
                    "unix_ms": 1000,
                    "type": "run_start",
                    "data": {},
                },
                {
                    "schema": "mbt.evt.v1",
                    "seq": 2,
                    "unix_ms": 1100,
                    "type": "vla_submit",
                    "data": {
                        "job_id": "job-1",
                        "generation": 1,
                        "captured_context_id": "ball-0001",
                    },
                },
                {
                    "schema": "mbt.evt.v1",
                    "seq": 3,
                    "unix_ms": 1200,
                    "type": "run_end",
                    "data": {},
                },
            ]
            (run_dir / "events.jsonl").write_text(
                "".join(json.dumps(event) + "\n" for event in events), encoding="utf-8"
            )
            raw_video = root / "capture.mp4"
            raw_video.write_bytes(b"raw-video-test")
            fake_ffmpeg = root / "ffmpeg"
            fake_ffmpeg.write_text(
                "#!/usr/bin/env python3\n"
                "import pathlib, sys\n"
                "pathlib.Path(sys.argv[-1]).write_bytes(b'overlay-video-test')\n",
                encoding="utf-8",
            )
            fake_ffmpeg.chmod(0o755)

            video = finaliser.finalise(
                run_dir,
                raw_video,
                2.0,
                force=False,
                ffmpeg=str(fake_ffmpeg),
            )

            self.assertEqual(video, (run_dir / "overlay-video.mp4").resolve())
            updated = json.loads(
                (run_dir / "live-manifest.json").read_text(encoding="utf-8")
            )
            self.assertEqual(updated["video_alignment"]["request_cue_seconds"], 2.0)
            self.assertIn("sha256", updated["raw_video"])
            self.assertIn("sha256", updated["overlay_video"])
            overlay = (run_dir / "overlay.ass").read_text(encoding="utf-8")
            self.assertIn("0:00:02.00", overlay)


if __name__ == "__main__":
    unittest.main()
