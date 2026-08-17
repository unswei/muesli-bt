"""Pure checks for the frozen WP9 context-token sensitivity study."""

from __future__ import annotations

import sys
import tarfile
import tempfile
import unittest
from pathlib import Path

EXAMPLE_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(EXAMPLE_ROOT))

from analysis.evidence import semantic_sha256, write_json
from run_wp9 import (
    GateG9SensitivityError,
    PROTOCOL_PATH,
    RUN_ROOT_MARKER,
    _load_protocol,
    _sha256,
    context_invalidates,
    seal_campaign,
)


class GateG9PureTest(unittest.TestCase):
    def test_seal_checksums_backs_up_and_makes_campaign_read_only(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            campaign = root / "campaign"
            run = campaign / "runs" / "run-a"
            run.mkdir(parents=True)
            (campaign / RUN_ROOT_MARKER).write_text("", encoding="utf-8")
            (run / "events.jsonl").write_text("{}\n", encoding="utf-8")
            summary = {
                "schema_version": "airhockey.wp9.context_sensitivity.summary.v1",
                "policy_runs": 3,
            }
            write_json(campaign / "context-sensitivity-summary.json", summary)
            write_json(
                campaign / "wp9-report.json",
                {
                    "status": "passed",
                    "protocol_sha256": _sha256(PROTOCOL_PATH),
                    "summary_sha256": semantic_sha256(summary),
                    "policy_runs": 3,
                    "privileged_policy_inputs": 0,
                },
            )
            backup = root / "campaign.tar.gz"
            seal_report = root / "wp9-seal.json"

            try:
                seal = seal_campaign(campaign, backup, seal_report)

                self.assertTrue(seal["backup_verified"])
                self.assertTrue(seal["raw_bundles_read_only"])
                self.assertEqual(_sha256(backup), seal["backup_sha256"])
                self.assertEqual(campaign.stat().st_mode & 0o777, 0o555)
                self.assertEqual(
                    (run / "events.jsonl").stat().st_mode & 0o777, 0o444
                )
                with tarfile.open(backup, "r:gz") as archive:
                    names = {member.name for member in archive.getmembers()}
                self.assertTrue(
                    any(name.endswith("/checksums.sha256") for name in names)
                )
                self.assertTrue(any(name.endswith("/wp9-report.json") for name in names))
            finally:
                if backup.exists():
                    backup.chmod(0o644)
                if seal_report.exists():
                    seal_report.chmod(0o644)
                if campaign.exists():
                    campaign.chmod(0o755)
                    for path in campaign.rglob("*"):
                        path.chmod(0o755 if path.is_dir() else 0o644)

    def test_seal_rejects_a_changed_summary(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            campaign = root / "campaign"
            campaign.mkdir()
            (campaign / RUN_ROOT_MARKER).write_text("", encoding="utf-8")
            write_json(
                campaign / "context-sensitivity-summary.json",
                {"policy_runs": 3},
            )
            write_json(
                campaign / "wp9-report.json",
                {
                    "status": "passed",
                    "protocol_sha256": _sha256(PROTOCOL_PATH),
                    "summary_sha256": "sha256:changed",
                    "policy_runs": 3,
                    "privileged_policy_inputs": 0,
                },
            )

            with self.assertRaises(GateG9SensitivityError):
                seal_campaign(
                    campaign, root / "campaign.tar.gz", root / "wp9-seal.json"
                )

    def test_protocol_freezes_public_only_policy_inputs_and_cardinality(self) -> None:
        protocol, _ = _load_protocol()

        self.assertEqual(protocol["context_observation"]["target_indices"], [16, 17])
        self.assertEqual(protocol["context_observation"]["visible_flag_index"], 18)
        self.assertFalse(protocol["context_observation"]["privileged_state"])
        self.assertEqual(protocol["paper_split"]["selected_shots"], 24)
        self.assertEqual(protocol["campaign"]["matched_cases"], 72)
        self.assertEqual(protocol["campaign"]["policy_runs"], 216)

    def test_reacquisition_identity_always_invalidates(self) -> None:
        policy = {
            "kind": "always_new_context_on_reacquisition",
            "displacement_threshold": None,
        }

        self.assertTrue(context_invalidates(policy, 0.0))

    def test_displacement_policy_keeps_the_declared_boundary_equivalent(self) -> None:
        policy = {
            "kind": "new_context_above_public_displacement",
            "displacement_threshold": 0.1,
        }

        self.assertFalse(context_invalidates(policy, 0.1))
        self.assertTrue(context_invalidates(policy, 0.1000001))


if __name__ == "__main__":
    unittest.main()
