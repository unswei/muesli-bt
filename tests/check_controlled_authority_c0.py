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
MATRIX_PATH = EXPERIMENT / "configs" / "fault-matrix.v1.json"
PROTOCOL_SCHEMA = ROOT / "schemas" / "controlled_authority" / "v1" / "protocol.schema.json"
CATALOGUE_SCHEMA = (
    ROOT
    / "schemas"
    / "controlled_authority"
    / "v1"
    / "schedule-catalogue.schema.json"
)
MATRIX_SCHEMA = (
    ROOT / "schemas" / "controlled_authority" / "v1" / "fault-matrix.schema.json"
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
    matrix = load(MATRIX_PATH)
    validate_schema(protocol, PROTOCOL_SCHEMA)
    validate_schema(catalogue, CATALOGUE_SCHEMA)
    validate_schema(matrix, MATRIX_SCHEMA)

    assert (PROTOCOL_PATH.parent / protocol["schedule_catalogue"]).resolve() == CATALOGUE_PATH
    assert (PROTOCOL_PATH.parent / protocol["fault_matrix"]).resolve() == MATRIX_PATH
    assert matrix["status"] == "frozen"
    assert matrix["protocol_id"] == protocol["protocol_id"]
    assert matrix["catalogue_id"] == catalogue["catalogue_id"]

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
    variant_labels = [variant["short_label"] for variant in variants]
    assert variant_labels == ["B0", "B1", "B2", "B3"]
    assert len({variant["reader_label"] for variant in variants}) == 4

    engineering = contiguous_seed_set(protocol["seed_sets"]["engineering"])
    paper = contiguous_seed_set(protocol["seed_sets"]["paper"])
    assert engineering.isdisjoint(paper), "engineering and paper seeds must be disjoint"
    assert len(engineering) == 32 and len(paper) == 128

    primary_schedule_ids = [
        "F03",
        "F04",
        "F05",
        "F06",
        "F07",
    ]
    rows = matrix["rows"]
    assert [row["schedule_id"] for row in rows] == expected_ids
    assert len({row["fault_id"] for row in rows}) == len(rows)
    assert [
        row["schedule_id"] for row in rows if row["paper_role"] == "primary_integrity"
    ] == primary_schedule_ids
    defined_dimensions = set(matrix["authority_dimension_definitions"])
    allowed_metrics = set(protocol["run_level_metrics"]) | {
        "active_jobs_at_end",
        "blocked_submissions",
        "fallback_activations",
        "result_rejections",
        "safe_stand_activations",
    }
    witnesses: dict[str, str] = {}
    for row in rows:
        assert list(row["expected_variant_outcomes"]) == variant_labels, (
            f"{row['schedule_id']} must freeze one expected outcome for every variant"
        )
        assert set(row["authority_dimensions"]) <= defined_dimensions
        assert set(row["negative_control_for"]) <= set(row["authority_dimensions"])
        assert set(row["primary_metrics"]) <= allowed_metrics
        assert not internal_token.search(row["claim"])
        for dimension in row["negative_control_for"]:
            assert dimension not in witnesses, f"duplicate negative-control witness for {dimension}"
            witnesses[dimension] = row["schedule_id"]
    assert witnesses == {
        "epoch_generation": "F03",
        "context_identity": "F06",
        "logical_revocation": "F04",
        "terminal_claim": "F08",
        "dispatch_revalidation": "F07",
    }

    assert "expected_variant_outcomes" not in protocol
    assert "negative_control_witnesses" not in protocol
    assert "primary_integrity_schedule_ids" not in protocol["semantic_lane"]
    assert all("required_full_outcome" not in schedule for schedule in schedules)
    assert protocol["paper_gate"]["manual_run_exclusion_allowed"] is False
    assert protocol["identifier_policy"]["schedule_ids_are_internal"] is True

    print("controlled-authority C0 protocol valid")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
