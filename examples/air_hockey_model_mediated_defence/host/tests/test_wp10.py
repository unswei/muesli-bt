"""Pure checks for the frozen WP10 post-admission authority study."""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

EXAMPLE_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(EXAMPLE_ROOT))

from analysis.evidence import semantic_sha256, write_json
from run_wp10 import (
    DISTURBANCES,
    PROTOCOL_PATH,
    RUN_ROOT_MARKER,
    TREATMENTS,
    GateG10PostAdmissionError,
    _load_protocol,
    _post_loss_motion,
    _sha256,
    seal_campaign,
)


class GateG10PureTest(unittest.TestCase):
    def test_protocol_freezes_the_matched_factorial_campaign(self) -> None:
        protocol, _ = _load_protocol()

        self.assertEqual(protocol["campaign"]["matched_shots"], 24)
        self.assertEqual(protocol["campaign"]["treatment_runs"], 144)
        self.assertEqual(
            protocol["campaign"]["treatment_runs"],
            24 * len(TREATMENTS) * len(DISTURBANCES),
        )
        self.assertEqual(protocol["paired_trial"]["completion_delay_ms"], 50)

    def test_motion_is_measured_only_after_authority_loss(self) -> None:
        initial = [0.0] * 19
        trajectory = [
            {
                "observation_step": 1,
                "mallet_position": [0.1, 0.0],
                "applied_target": [1.0, 0.0],
            },
            {
                "observation_step": 2,
                "mallet_position": [0.2, 0.0],
                "applied_target": [1.0, 0.0],
            },
        ]

        motion = _post_loss_motion(initial, trajectory, [1.0, 0.0], 1)

        self.assertIsNotNone(motion)
        self.assertAlmostEqual(motion["projected_motion_towards_target"], 0.1)
        self.assertAlmostEqual(motion["projected_motion_while_target_applied"], 0.1)
        self.assertEqual(motion["command_steps_towards_target"], 1)

    def test_seal_rejects_a_changed_summary(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            campaign = root / "campaign"
            campaign.mkdir()
            (campaign / RUN_ROOT_MARKER).write_text("", encoding="utf-8")
            write_json(campaign / "post-admission-summary.json", {"runs": 144})
            write_json(
                campaign / "wp10-report.json",
                {
                    "status": "passed",
                    "protocol_sha256": _sha256(PROTOCOL_PATH),
                    "summary_sha256": semantic_sha256({"runs": 143}),
                },
            )

            with self.assertRaises(GateG10PostAdmissionError):
                seal_campaign(
                    campaign, root / "campaign.tar.gz", root / "wp10-seal.json"
                )


if __name__ == "__main__":
    unittest.main()
