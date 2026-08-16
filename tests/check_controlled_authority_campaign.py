#!/usr/bin/env python3

"""Exercise one deterministic seed across the complete authority campaign."""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
import re
import runpy
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DRIVER = ROOT / "experiments" / "invocation_authority_controlled" / "run_campaign.py"
RUN_SCHEMA = ROOT / "schemas" / "controlled_authority" / "v1" / "run-manifest.schema.json"
CAMPAIGN_SCHEMA = (
    ROOT / "schemas" / "controlled_authority" / "v1" / "campaign-manifest.schema.json"
)
MATRIX_PATH = (
    ROOT
    / "experiments"
    / "invocation_authority_controlled"
    / "configs"
    / "fault-matrix.v1.json"
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
    with tempfile.TemporaryDirectory(prefix="muesli-authority-campaign-") as directory:
        output = Path(directory) / "campaign"
        subprocess.run(
            [
                "python3",
                str(DRIVER),
                "--engine",
                str(args.engine),
                "--output",
                str(output),
                "--seeds",
                "0",
            ],
            cwd=ROOT,
            check=True,
        )

        campaign = load(output / "campaign-manifest.json")
        validate_schema(campaign, CAMPAIGN_SCHEMA)
        assert campaign["trial_count"] == 64
        assert campaign["expected_outcomes_met"] == 64
        assert campaign["canonical_trace_failures"] == 0
        assert campaign["campaign_valid"] is True
        assert campaign["matrix_id"] == "controlled-authority.c0.fault-matrix.v1"
        assert campaign["inputs"]["fault_matrix"]["path"] == str(MATRIX_PATH)
        assert campaign["paper_gate"] == {
            "evaluated": False,
            "passed": None,
            "reason": "incomplete_paper_campaign",
        }
        assert len(campaign["run_manifests"]) == 64
        raw_trials = [
            json.loads(line)
            for line in (output / "raw_trials.jsonl").read_text(encoding="utf-8").splitlines()
        ]
        assert all(not trial["replay_mismatch_schedule_ids"] for trial in raw_trials)
        assert all(not trial["replay_mismatch_details"] for trial in raw_trials)

        manifests: list[dict] = []
        duplicate_full_manifest: dict | None = None
        for record in campaign["run_manifests"]:
            path = output / record["path"]
            run = load(path)
            manifests.append(run)
            validate_schema(run, RUN_SCHEMA)
            assert run["expected_outcome_met"] is True
            assert run["metrics"]["canonical_trace_valid"] is True
            assert run["manual_exclusion"] is False
            assert run["matrix_id"] == campaign["matrix_id"]
            assert run["fault"]["authority_dimensions"]
            assert run["fault"]["claim"]
            if (
                run["schedule"]["internal_id"] == "F08"
                and run["variant"]["short_label"] == "B3"
            ):
                duplicate_full_manifest = run

        assert duplicate_full_manifest is not None
        duplicate_variant_stream = next(
            stream
            for stream in duplicate_full_manifest["canonical_streams"]
            if stream["kind"] == "variant"
        )
        duplicate_evidence = (output / duplicate_variant_stream["path"]).read_text(
            encoding="utf-8"
        )
        assert '"reason":"duplicate_terminal_result"' in duplicate_evidence

        table = (output / "paper" / "controlled-authority-table.md").read_text(
            encoding="utf-8"
        )
        assert not re.search(r"\bF(?:0[1-9]|1[0-6])\b", table)
        assert "branch exit and re-entry before completion" in table
        assert "epoch and generation identity" in table
        assert "invocation-scoped authority" in table
        assert (output / "summary" / "trials.csv").is_file()
        assert (output / "summary" / "schedule-summary.json").is_file()
        assert (output / "summary" / "variant-summary.csv").is_file()
        assert (output / "raw_trials.jsonl").is_file()
        for record in campaign["derived_artefacts"].values():
            assert (output / record["path"]).is_file()

        driver = runpy.run_path(str(DRIVER))
        protocol = load(
            ROOT
            / "experiments"
            / "invocation_authority_controlled"
            / "configs"
            / "protocol.v1.json"
        )
        catalogue = load(
            ROOT
            / "experiments"
            / "invocation_authority_controlled"
            / "schedules"
            / "catalogue.v1.json"
        )
        matrix = load(MATRIX_PATH)
        reordered_matrix = copy.deepcopy(matrix)
        reordered_matrix["rows"][0], reordered_matrix["rows"][1] = (
            reordered_matrix["rows"][1],
            reordered_matrix["rows"][0],
        )
        try:
            driver["validate_matrix_contract"](protocol, catalogue, reordered_matrix)
        except ValueError:
            pass
        else:
            raise AssertionError("campaign accepted a matrix that drifted from catalogue order")
        paper_seed = protocol["seed_sets"]["paper"]
        paper_seeds = list(
            range(paper_seed["first"], paper_seed["first"] + paper_seed["count"])
        )
        synthetic_paper_runs = manifests * len(paper_seeds)
        gate = driver["paper_gate"](
            protocol,
            matrix,
            synthetic_paper_runs,
            catalogue["schedules"],
            protocol["variants"],
            paper_seeds,
        )
        assert gate["evaluated"] is True and gate["passed"] is True
        assert gate["negative_control_exposure_met"] is True
        assert all(gate["negative_control_witnesses"].values())

        unwitnessed_matrix = copy.deepcopy(matrix)
        for row in unwitnessed_matrix["rows"]:
            row["negative_control_for"] = []
        unwitnessed_gate = driver["paper_gate"](
            protocol,
            unwitnessed_matrix,
            synthetic_paper_runs,
            catalogue["schedules"],
            protocol["variants"],
            paper_seeds,
        )
        assert unwitnessed_gate["negative_control_exposure_met"] is False
        assert unwitnessed_gate["passed"] is False

        failed_runs = list(synthetic_paper_runs)
        unsafe_full = copy.deepcopy(
            next(item for item in manifests if item["variant"]["short_label"] == "B3")
        )
        unsafe_full["metrics"]["obsolete_effect"] = True
        unsafe_index = next(
            index
            for index, item in enumerate(failed_runs)
            if item["variant"]["short_label"] == "B3"
        )
        failed_runs[unsafe_index] = unsafe_full
        failed_gate = driver["paper_gate"](
            protocol,
            matrix,
            failed_runs,
            catalogue["schedules"],
            protocol["variants"],
            paper_seeds,
        )
        assert failed_gate["evaluated"] is True and failed_gate["passed"] is False

        manifest_path = output / "campaign-manifest.json"
        before = hashlib.sha256(manifest_path.read_bytes()).hexdigest()
        refused = subprocess.run(
            [
                "python3",
                str(DRIVER),
                "--engine",
                str(args.engine),
                "--output",
                str(output),
                "--seeds",
                "0",
            ],
            cwd=ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        after = hashlib.sha256(manifest_path.read_bytes()).hexdigest()
        assert refused.returncode != 0 and before == after

    print("controlled-authority campaign driver valid")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
