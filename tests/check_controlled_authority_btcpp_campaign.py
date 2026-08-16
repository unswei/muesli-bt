#!/usr/bin/env python3

"""Smoke-test the frozen BehaviorTree.CPP comparison campaign."""

from __future__ import annotations

import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DRIVER = ROOT / "experiments" / "invocation_authority_btcpp" / "run_campaign.py"
CAMPAIGN_SCHEMA = (
    ROOT
    / "schemas"
    / "controlled_authority"
    / "v1"
    / "btcpp-comparison-campaign-manifest.schema.json"
)
RUN_SCHEMA = (
    ROOT
    / "schemas"
    / "controlled_authority"
    / "v1"
    / "btcpp-comparison-run-manifest.schema.json"
)


def load(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def validate(instance: dict, schema_path: Path) -> None:
    try:
        import jsonschema
    except ImportError:
        return
    schema = load(schema_path)
    jsonschema.Draft202012Validator.check_schema(schema)
    jsonschema.Draft202012Validator(schema).validate(instance)


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: check_controlled_authority_btcpp_campaign.py ENGINE")
    engine = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="mbt-btcpp-campaign-") as temporary:
        output = Path(temporary) / "campaign"
        subprocess.run(
            [
                sys.executable,
                str(DRIVER),
                "--output",
                str(output),
                "--engine",
                str(engine),
                "--seeds",
                "0",
            ],
            cwd=ROOT,
            check=True,
        )
        campaign = load(output / "campaign-manifest.json")
        validate(campaign, CAMPAIGN_SCHEMA)
        assert campaign["trial_count"] == 64
        assert campaign["campaign_valid"] is True
        assert campaign["paper_gate"] == {
            "evaluated": False,
            "full_ports_safe": True,
            "paired_inputs_equal": True,
            "passed": None,
            "reason": "partial or engineering selection",
            "trace_failures": 0,
        }

        manifests = [load(output / item["path"]) for item in campaign["run_manifests"]]
        assert len(manifests) == 64
        for manifest in manifests:
            validate(manifest, RUN_SCHEMA)
            assert manifest["manual_exclusion"] is False
            assert manifest["metrics"]["canonical_trace_valid"] is True

        digests: dict[tuple[str, int], set[str]] = {}
        for manifest in manifests:
            key = (manifest["schedule"]["internal_id"], manifest["seed"])
            digests.setdefault(key, set()).add(manifest["paired_input_digest"])
        assert all(len(values) == 1 for values in digests.values())

        full = [
            manifest
            for manifest in manifests
            if manifest["variant"]["authority_profile"] == "invocation_scoped"
        ]
        assert len(full) == 32
        assert all(not manifest["metrics"]["obsolete_effect"] for manifest in full)
        assert all(not manifest["metrics"]["valid_current_result_rejected"] for manifest in full)
        assert all(manifest["matches_c0_profile_reference"] for manifest in full)
        assert all(manifest["metrics"]["task_replay_equal"] for manifest in full)

        paper = (output / "paper" / "btcpp-authority-comparison-table.md").read_text(
            encoding="utf-8"
        )
        assert not re.search(r"\bF(?:0[1-9]|1[0-6])\b", paper)
        assert "current result before the deadline" in paper
        assert "BehaviorTree.CPP documented ordinary asynchronous lifecycle" in paper

    print("BehaviorTree.CPP controlled-authority campaign valid")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
