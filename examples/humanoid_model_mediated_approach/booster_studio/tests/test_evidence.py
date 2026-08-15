from __future__ import annotations

import pathlib
import sys
import tempfile
import unittest

PROJECT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT / "src"))

from muesli_booster.evidence import (
    EvidenceError,
    generate_ass,
    load_events,
)


def event(sequence: int, offset_ms: int, event_type: str, data: dict) -> dict:
    return {
        "schema": "mbt.evt.v1",
        "seq": sequence,
        "unix_ms": 1_000_000 + offset_ms,
        "type": event_type,
        "data": data,
    }


class OverlayEvidenceTests(unittest.TestCase):
    def test_overlay_contains_required_fields_and_target_colours(self) -> None:
        events = [
            event(1, 0, "run_start", {}),
            event(
                2,
                100,
                "vla_submit",
                {
                    "job_id": "job-1",
                    "generation": 1,
                    "captured_context_id": "ball-0001",
                },
            ),
            event(
                3, 110, "bb_write", {"key": "active-branch", "preview": "model_wait"}
            ),
            event(
                4,
                200,
                "bb_write",
                {"key": "walking-target-state", "preview": "current"},
            ),
            event(
                5,
                210,
                "bb_write",
                {"key": "current-walking-target", "preview": [-0.45, 0.08, 0.0]},
            ),
            event(
                6,
                300,
                "bb_write",
                {"key": "walking-target-state", "preview": "obsolete"},
            ),
        ]

        overlay = generate_ass(events, request_cue_seconds=2.0)

        self.assertIn("branch: model_wait", overlay)
        self.assertIn("job: job-1", overlay)
        self.assertIn("generation: 1", overlay)
        self.assertIn("ball context: ball-0001", overlay)
        self.assertIn(r"{\c&H0000FF00&}walking target:", overlay)
        self.assertIn(r"{\c&H000000FF&}walking target:", overlay)
        self.assertIn("0:00:02.00", overlay)

    def test_overlay_separates_runtime_acceptance_from_dispatch_rejection(self) -> None:
        events = [
            event(1, 0, "run_start", {}),
            event(
                2,
                100,
                "vla_submit",
                {
                    "job_id": "job-1",
                    "generation": 1,
                    "captured_context_id": "ball-0001",
                },
            ),
            event(
                3,
                2600,
                "vla_result",
                {"job_id": "job-1", "generation": 1, "decision": "accepted"},
            ),
            event(
                4,
                2610,
                "walking_target_dispatch",
                {"decision": "rejected", "reason": "context_changed"},
            ),
            event(5, 2620, "run_end", {}),
        ]

        overlay = generate_ass(events)

        self.assertIn("result: accepted    reason: -", overlay)
        self.assertIn("dispatch: rejected    reason: context_changed", overlay)
        self.assertNotIn("result: rejected    reason: context_changed", overlay)

    def test_loader_rejects_non_contiguous_sequence(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "events.jsonl"
            path.write_text(
                '{"schema":"mbt.evt.v1","seq":1,"unix_ms":1,"data":{}}\n'
                '{"schema":"mbt.evt.v1","seq":3,"unix_ms":2,"data":{}}\n',
                encoding="utf-8",
            )
            with self.assertRaisesRegex(EvidenceError, "not contiguous"):
                load_events(path)


if __name__ == "__main__":
    unittest.main()
