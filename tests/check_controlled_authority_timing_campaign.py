#!/usr/bin/env python3

"""Exercise a paired real-clock trial across all authority mechanisms."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DRIVER = (
    ROOT
    / "experiments"
    / "invocation_authority_controlled"
    / "run_timing_campaign.py"
)
RAW_SCHEMA = (
    ROOT / "schemas" / "controlled_authority" / "v1" / "timing-raw-trial.schema.json"
)
MANIFEST_SCHEMA = (
    ROOT
    / "schemas"
    / "controlled_authority"
    / "v1"
    / "timing-campaign-manifest.schema.json"
)


def load(path: Path) -> dict:
    with path.open(encoding="utf-8") as handle:
        return json.load(handle)


def validate_schema(instance: dict, schema_path: Path) -> None:
    try:
        import jsonschema
    except ImportError:
        return
    jsonschema.Draft202012Validator(load(schema_path)).validate(instance)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("engine", type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="muesli-authority-timing-") as directory:
        output = Path(directory) / "campaign"
        subprocess.run(
            [
                "python3",
                str(DRIVER),
                "--engine",
                str(args.engine),
                "--output",
                str(output),
                "--conditions",
                "primary",
                "--repetitions",
                "1",
                "--warmups",
                "0",
            ],
            cwd=ROOT,
            check=True,
        )

        manifest = load(output / "timing-campaign-manifest.json")
        validate_schema(manifest, MANIFEST_SCHEMA)
        assert manifest["lane"] == "timing"
        assert manifest["clock"] == "steady_clock"
        assert manifest["trial_count"] == 4
        assert manifest["warmups_executed"] == 0
        assert manifest["campaign_valid"] is True
        assert manifest["paper_gate"]["evaluated"] is False
        assert manifest["paper_gate"]["passed"] is None
        assert manifest["paper_gate"]["reason"] == "incomplete_timing_campaign"
        assert isinstance(manifest["paper_gate"]["host_load_within_limit"], bool)
        assert manifest["paper_gate"]["validation_failures"] == []
        assert manifest["host_before"]["cpu_model"]
        assert manifest["host_before"]["load_average"]
        assert manifest["host_before"]["background_processes"]

        trials = [
            json.loads(line)
            for line in (output / "raw_timing_trials.jsonl").read_text(
                encoding="utf-8"
            ).splitlines()
        ]
        assert len(trials) == 4
        for trial in trials:
            validate_schema(trial, RAW_SCHEMA)
            assert trial["requested_delays_ms"] == [500]
            assert trial["active_jobs_at_end"] == 0
            assert trial["terminal_decisions"] >= 1
        by_variant = {trial["variant_label"]: trial for trial in trials}
        assert by_variant["B0"]["maximum_tick_ms"] >= 400.0
        assert all(
            by_variant[label]["maximum_tick_ms"] < by_variant["B0"]["maximum_tick_ms"]
            for label in ("B1", "B2", "B3")
        )

        table = (
            output / "paper" / "controlled-authority-timing-table.md"
        ).read_text(encoding="utf-8")
        assert "primary operating point" in table
        assert "invocation-scoped authority" in table
        assert "primary\t" not in table
        for record in manifest["derived_artefacts"].values():
            assert (output / record["path"]).is_file()

        manifest_path = output / "timing-campaign-manifest.json"
        before = hashlib.sha256(manifest_path.read_bytes()).hexdigest()
        refused = subprocess.run(
            [
                "python3",
                str(DRIVER),
                "--engine",
                str(args.engine),
                "--output",
                str(output),
                "--conditions",
                "primary",
                "--repetitions",
                "1",
                "--warmups",
                "0",
            ],
            cwd=ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        after = hashlib.sha256(manifest_path.read_bytes()).hexdigest()
        assert refused.returncode != 0 and before == after

    print("controlled-authority timing campaign valid")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
