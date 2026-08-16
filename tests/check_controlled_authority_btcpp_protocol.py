#!/usr/bin/env python3

"""Validate the frozen BehaviorTree.CPP comparison protocol."""

from __future__ import annotations

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
EXPERIMENT = ROOT / "experiments" / "invocation_authority_btcpp"
PROTOCOL_PATH = EXPERIMENT / "configs" / "protocol.v1.json"
SCHEMA_PATH = (
    ROOT
    / "schemas"
    / "controlled_authority"
    / "v1"
    / "btcpp-comparison-protocol.schema.json"
)


def load(path: Path) -> dict:
    with path.open(encoding="utf-8") as handle:
        return json.load(handle)


def main() -> int:
    protocol = load(PROTOCOL_PATH)
    try:
        import jsonschema
    except ImportError:
        jsonschema = None
    if jsonschema is not None:
        schema = load(SCHEMA_PATH)
        jsonschema.Draft202012Validator.check_schema(schema)
        jsonschema.Draft202012Validator(schema).validate(protocol)

    source = protocol["source_contract"]
    resolved = {
        key: (PROTOCOL_PATH.parent / source[key]).resolve()
        for key in ("protocol", "schedule_catalogue", "fault_matrix", "common_task")
    }
    assert all(path.is_file() for path in resolved.values())

    c0_protocol = load(resolved["protocol"])
    catalogue = load(resolved["schedule_catalogue"])
    matrix = load(resolved["fault_matrix"])
    assert c0_protocol["protocol_id"] == source["protocol_id"]
    assert catalogue["catalogue_id"] == source["catalogue_id"]
    assert matrix["matrix_id"] == source["matrix_id"]

    all_schedules = [schedule["schedule_id"] for schedule in catalogue["schedules"]]
    assert protocol["semantic_lane"]["schedule_scope"] == all_schedules
    assert protocol["paper_gate"]["exact_schedule_scope_required"] == all_schedules

    variants = protocol["variants"]
    assert [variant["manifest_label"] for variant in variants] == [
        "MBT-ordinary",
        "BTCPP-ordinary",
        "MBT-full",
        "BTCPP-full",
    ]
    runtime_profiles = {
        (variant["runtime_id"], variant["authority_profile"]): variant
        for variant in variants
    }
    assert len(runtime_profiles) == 4
    assert {
        variant["expected_outcome_source"] for variant in variants
    } == {"B1", "B3"}

    frameworks = {framework["runtime_id"]: framework for framework in protocol["frameworks"]}
    assert frameworks["behaviortree-cpp"]["version"] == "4.9.0"
    assert frameworks["behaviortree-cpp"]["commit"] == (
        "3ff6a32ba0497a08519c77a1436e3b81eff1bcd6"
    )
    assert "StatefulActionNode" in frameworks["behaviortree-cpp"]["framework_lifecycle"]

    paper = protocol["paper_gate"]
    assert paper["ordinary_async_must_be_competent"] is True
    assert paper["manual_run_exclusion_allowed"] is False
    assert paper["paper_seed_first"] == 10000
    assert paper["paper_seed_count"] == 128
    assert protocol["generic_performance_lane"]["separate_from_authority_lane"] is True

    print("controlled-authority BehaviorTree.CPP comparison protocol valid")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
