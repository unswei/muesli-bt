"""Pure checks for WP8 integrity aggregation."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

EXAMPLE_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(EXAMPLE_ROOT))

from analysis.evidence import integrity_summary
from run_wp8 import _timing_gate_summary


def _event(event_type: str, **data: object) -> dict[str, object]:
    return {"type": event_type, "data": data}


class GateG8PureTest(unittest.TestCase):
    def test_reaggregation_gates_only_new_release_recovery_timing(self) -> None:
        measurements = {
            "tick_timing": {"p99_ms": 72.0},
            "tick_timing_by_treatment": {
                "invocation_scoped_current_context_recovery": {"p99_ms": 8.0}
            },
        }

        scope, timing = _timing_gate_summary(measurements, True)

        self.assertEqual(
            scope, "invocation_scoped_current_context_recovery"
        )
        self.assertEqual(timing["p99_ms"], 8.0)

    def test_fresh_campaign_gates_all_treatment_timing(self) -> None:
        measurements = {
            "tick_timing": {"p99_ms": 9.0},
            "tick_timing_by_treatment": {},
        }

        scope, timing = _timing_gate_summary(measurements, False)

        self.assertEqual(scope, "all_treatments")
        self.assertEqual(timing["p99_ms"], 9.0)

    def test_duplicate_terminal_decision_fails_reason_agreement(self) -> None:
        events = [
            _event("vla_submit", job_id="job-1", generation=1),
            _event(
                "vla_result",
                job_id="job-1",
                generation=1,
                decision="rejected",
                reason="context_changed",
                captured_context_id="context-a",
                current_context_id="context-b",
            ),
            _event(
                "vla_result",
                job_id="job-1",
                generation=1,
                decision="rejected",
                reason="context_changed",
                captured_context_id="context-a",
                current_context_id="context-b",
            ),
        ]
        expected = {
            "terminal_decision": "rejected",
            "reason": "context_changed",
            "obsolete_dispatches": 0,
        }

        summary = integrity_summary(events, expected)

        self.assertEqual(summary["duplicate_commits"], 1)
        self.assertFalse(summary["reason_code_agreement"])

    def test_single_terminal_decision_preserves_reason_agreement(self) -> None:
        events = [
            _event("vla_submit", job_id="job-1", generation=1),
            _event(
                "vla_result",
                job_id="job-1",
                generation=1,
                decision="rejected",
                reason="context_changed",
                captured_context_id="context-a",
                current_context_id="context-b",
            ),
        ]
        expected = {
            "terminal_decision": "rejected",
            "reason": "context_changed",
            "obsolete_dispatches": 0,
        }

        summary = integrity_summary(events, expected)

        self.assertEqual(summary["duplicate_commits"], 0)
        self.assertTrue(summary["reason_code_agreement"])


if __name__ == "__main__":
    unittest.main()
