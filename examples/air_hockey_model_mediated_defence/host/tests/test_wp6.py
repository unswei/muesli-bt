"""Pure checks for the frozen WP6 campaign protocol and summaries."""

from __future__ import annotations

import json
import sys
import unittest
from pathlib import Path

EXAMPLE_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(EXAMPLE_ROOT))

from run_wp6 import (
    _event_counts,
    _percentile,
    _timing_records,
    _timing_summary,
    load_protocol,
)


class GateG6PureTest(unittest.TestCase):
    def test_frozen_protocol_keeps_engineering_and_paper_splits_distinct(self) -> None:
        protocol = load_protocol()
        self.assertEqual(protocol["engineering_split"]["name"], "engineering")
        self.assertEqual(protocol["engineering_split"]["expected_shots"], 26)
        self.assertEqual(
            protocol["engineering_split"]["manifest_sha256"],
            "41b5a8217f2d5d3b4066d133917c3770909bb0d5033a65963c78b438391e011e",
        )
        self.assertEqual(protocol["paper_split"]["name"], "muesli_test")
        self.assertTrue(protocol["paper_split"]["must_remain_unopened"])
        self.assertEqual(
            protocol["deterministic_matrix"]["scenarios"],
            ["H1", "H2a", "H2b", "H3", "H4", "H5", "H6", "H7", "H8"],
        )

    def test_delay_order_and_percentiles_are_deterministic(self) -> None:
        delays = load_protocol()["delay_calibration"]
        self.assertEqual(
            [
                delays["timely_ms"],
                delays["context_change_ms"],
                delays["stale_unexpired_ms"],
                delays["boundary_ms"],
                delays["timeout_ms"],
            ],
            [20, 40, 60, 119, 121],
        )
        self.assertEqual(_percentile([1.0, 2.0, 3.0, 4.0], 0.5), 2.5)
        summary = _timing_summary([1_000_000, 10_000_000, 21_000_000], 20.0)
        self.assertEqual(summary["budget_misses"], 1)
        self.assertAlmostEqual(summary["budget_miss_rate"], 1 / 3)

    def test_timing_and_event_projections_are_fail_closed(self) -> None:
        timing = {
            "schema_version": "airhockey.wp6.timing.v1",
            "scenario": "H1",
            "tick_duration_ns": [100, 200],
            "provider_wall_duration_ns": [300],
        }
        parsed = _timing_records("TIMING " + json.dumps(timing))
        self.assertEqual(parsed, [timing])
        counts = _event_counts(
            [
                {"type": "vla_result", "data": {"decision": "accepted"}},
                {
                    "type": "vla_result",
                    "data": {"decision": "rejected", "reason": "context_changed"},
                },
                {
                    "type": "cap_call_end",
                    "data": {"status": "accepted", "obsolete": True},
                },
            ]
        )
        self.assertEqual(
            counts,
            {
                "accepted_results": 1,
                "context_changed_rejections": 1,
                "accepted_obsolete_dispatches": 1,
            },
        )


if __name__ == "__main__":
    unittest.main()
