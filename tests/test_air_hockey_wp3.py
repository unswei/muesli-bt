"""Unit tests for local air-hockey WP3 evidence tooling."""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
EXAMPLE_ROOT = REPOSITORY_ROOT / "examples" / "air_hockey_model_mediated_defence"
sys.path.insert(0, str(EXAMPLE_ROOT))

from analysis.evidence import (
    EvidenceError,
    RecordedProviderReplay,
    campaign_summary,
    clopper_pearson,
    file_sha256,
    read_json,
    read_jsonl,
    semantic_sha256,
    validate_campaign_report,
    validate_raw_bundle,
    write_json,
    write_jsonl,
)
from analysis.synthetic import generate_campaign, generate_run


class AirHockeyWp3Tests(unittest.TestCase):
    def test_synthetic_campaign_regenerates_reports(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "runs"
            run_dirs = generate_campaign(root)
            report = campaign_summary(root)
            validate_campaign_report(report)
            self.assertEqual(len(run_dirs), 8)
            self.assertEqual(report["pair_count"], 4)
            self.assertTrue(report["synthetic_only"])
            self.assertEqual(
                report["integrity_intervals"]["full_obsolete_dispatch"]["successes"],
                0,
            )
            self.assertLess(
                report["paired_intervals"]["obsolete_target_motion"]["estimate"],
                0.0,
            )
            for run_dir in run_dirs:
                self.assertTrue((run_dir / "overlay.svg").is_file())
                self.assertTrue((run_dir / "bundle-validation.json").is_file())

    def test_privileged_scoring_cannot_cross_into_events(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            run_dir = generate_run(
                Path(directory) / "runs", 0, "invocation_scoped", force=False
            )
            event_path = run_dir / "events.jsonl"
            rows = read_jsonl(event_path)
            rows[1]["data"]["outcome"] = "save"
            write_jsonl(event_path, rows)
            manifest_path = run_dir / "manifest.json"
            manifest = read_json(manifest_path)
            manifest["raw_artefacts"]["events.jsonl"]["sha256"] = file_sha256(
                event_path
            )
            write_json(manifest_path, manifest)
            with self.assertRaisesRegex(EvidenceError, "privileged scoring data"):
                validate_raw_bundle(run_dir)

    def test_recorded_provider_requires_exact_request(self) -> None:
        action = {
            "type": "continuous",
            "frame": "airhockey.normalised_mallet_target.v1",
            "values": [0.25, -0.4],
            "dt_ms": 20,
        }
        records = [
            {
                "request_sha256": semantic_sha256("request-a"),
                "response_sha256": semantic_sha256(action),
                "action": action,
            }
        ]
        replay = RecordedProviderReplay(records)
        self.assertEqual(replay.infer(semantic_sha256("request-a")), action)
        with self.assertRaisesRegex(EvidenceError, "no exact request match"):
            replay.infer(semantic_sha256("request-b"))

    def test_exact_binomial_zero_failure_upper_bound(self) -> None:
        zero = clopper_pearson(0, 4)
        all_fail = clopper_pearson(4, 4)
        self.assertAlmostEqual(zero["upper"], 0.6023646356164745)
        self.assertAlmostEqual(all_fail["lower"], 0.3976353643835254)


if __name__ == "__main__":
    unittest.main()
