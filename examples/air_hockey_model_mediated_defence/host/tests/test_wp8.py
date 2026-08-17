"""Pure checks for WP8 integrity aggregation."""

from __future__ import annotations

import sys
import tarfile
import tempfile
import unittest
from pathlib import Path

EXAMPLE_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(EXAMPLE_ROOT))

from analysis.evidence import integrity_summary, semantic_sha256, write_json
from run_wp8 import (
    GateG8RecoveryError,
    PROTOCOL_PATH,
    RUN_ROOT_MARKER,
    THREAD_LIMIT_ENVIRONMENT,
    _sha256,
    _timing_gate_summary,
    _validate_thread_limits,
    seal_campaign,
)


def _event(event_type: str, **data: object) -> dict[str, object]:
    return {"type": event_type, "data": data}


class GateG8PureTest(unittest.TestCase):
    def test_seal_checksums_backs_up_and_makes_campaign_read_only(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            campaign = root / "campaign"
            run = campaign / "runs" / "run-a"
            run.mkdir(parents=True)
            (campaign / RUN_ROOT_MARKER).write_text("", encoding="utf-8")
            (run / "events.jsonl").write_text("{}\n", encoding="utf-8")
            summary = {"schema_version": "airhockey.campaign_summary.v1", "pairs": []}
            write_json(campaign / "campaign-summary.json", summary)
            write_json(
                campaign / "wp8-report.json",
                {
                    "status": "passed",
                    "protocol_frozen": True,
                    "protocol_sha256": _sha256(PROTOCOL_PATH),
                    "campaign_summary_sha256": semantic_sha256(summary),
                },
            )
            backup = root / "campaign.tar.gz"
            seal_report = root / "wp8-seal.json"

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
                self.assertTrue(any(name.endswith("/wp8-report.json") for name in names))
            finally:
                if backup.exists():
                    backup.chmod(0o644)
                if seal_report.exists():
                    seal_report.chmod(0o644)
                if campaign.exists():
                    campaign.chmod(0o755)
                    for path in campaign.rglob("*"):
                        path.chmod(0o755 if path.is_dir() else 0o644)

    def test_seal_rejects_an_unmarked_campaign(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            campaign = root / "campaign"
            campaign.mkdir()
            with self.assertRaises(GateG8RecoveryError):
                seal_campaign(
                    campaign, root / "campaign.tar.gz", root / "wp8-seal.json"
                )

    def test_campaign_requires_single_threaded_numerical_libraries(self) -> None:
        environment = {name: "1" for name in THREAD_LIMIT_ENVIRONMENT}

        _validate_thread_limits(environment)

        environment["OPENBLAS_NUM_THREADS"] = "32"
        with self.assertRaises(GateG8RecoveryError):
            _validate_thread_limits(environment)

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
