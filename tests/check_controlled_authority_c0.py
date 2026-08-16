#!/usr/bin/env python3

"""Validate the frozen controlled-authority C0 protocol."""

from __future__ import annotations

import json
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
EXPERIMENT = ROOT / "experiments" / "invocation_authority_controlled"
PROTOCOL_PATH = EXPERIMENT / "configs" / "protocol.v1.json"
CATALOGUE_PATH = EXPERIMENT / "schedules" / "catalogue.v1.json"
PROTOCOL_SCHEMA = ROOT / "schemas" / "controlled_authority" / "v1" / "protocol.schema.json"
CATALOGUE_SCHEMA = (
    ROOT
    / "schemas"
    / "controlled_authority"
    / "v1"
    / "schedule-catalogue.schema.json"
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


def contiguous_seed_set(seed_set: dict) -> set[int]:
    return set(range(seed_set["first"], seed_set["first"] + seed_set["count"]))


def main() -> int:
    protocol = load(PROTOCOL_PATH)
    catalogue = load(CATALOGUE_PATH)
    validate_schema(protocol, PROTOCOL_SCHEMA)
    validate_schema(catalogue, CATALOGUE_SCHEMA)

    schedules = catalogue["schedules"]
    schedule_ids = [schedule["schedule_id"] for schedule in schedules]
    expected_ids = [f"F{index:02d}" for index in range(1, 17)]
    assert schedule_ids == expected_ids, "schedule catalogue must contain ordered F01--F16 keys"
    assert len(set(schedule_ids)) == len(schedule_ids), "schedule IDs must be unique"

    internal_token = re.compile(r"\bF(?:0[1-9]|1[0-6])\b")
    for schedule in schedules:
        assert not internal_token.search(schedule["reader_label"]), (
            "reader labels must explain schedules without internal identifiers"
        )
        times = [event["at"] for event in schedule["events"]]
        assert times == sorted(times), f"{schedule['schedule_id']} events must be time ordered"

    variants = protocol["variants"]
    assert [variant["short_label"] for variant in variants] == ["B0", "B1", "B2", "B3"]
    assert len({variant["reader_label"] for variant in variants}) == 4

    engineering = contiguous_seed_set(protocol["seed_sets"]["engineering"])
    paper = contiguous_seed_set(protocol["seed_sets"]["paper"])
    assert engineering.isdisjoint(paper), "engineering and paper seeds must be disjoint"
    assert len(engineering) == 32 and len(paper) == 128

    assert protocol["semantic_lane"]["primary_integrity_schedule_ids"] == [
        "F03",
        "F04",
        "F05",
        "F06",
        "F07",
    ]
    expected_outcomes = protocol["expected_variant_outcomes"]
    assert list(expected_outcomes) == expected_ids
    for schedule_id, outcomes in expected_outcomes.items():
        assert list(outcomes) == ["B0", "B1", "B2", "B3"], (
            f"{schedule_id} must freeze one expected outcome for every variant"
        )
    assert protocol["negative_control_witnesses"] == {
        "without_epoch_or_generation": "F03",
        "without_context_identity": "F06",
        "without_local_revocation": "F04",
        "without_terminal_claim": "F08",
        "without_dispatch_revalidation": "F07",
    }
    assert protocol["paper_gate"]["manual_run_exclusion_allowed"] is False
    assert protocol["identifier_policy"]["schedule_ids_are_internal"] is True

    print("controlled-authority C0 protocol valid")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
