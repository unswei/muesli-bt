"""Validate the frozen air-hockey H2b canonical trace and evidence predicates."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

from jsonschema import Draft202012Validator

REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
FIXTURE = (
    REPOSITORY_ROOT
    / "tests"
    / "fixtures"
    / "mbt.evt.v1"
    / "air_hockey_h2b_context_change.jsonl"
)
EVENT_SCHEMA = (
    REPOSITORY_ROOT / "schemas" / "event_log" / "v1" / "mbt.evt.v1.schema.json"
)
PREDICATES = (
    REPOSITORY_ROOT
    / "examples"
    / "air_hockey_model_mediated_defence"
    / "evidence"
    / "g2_predicates.json"
)
PROHIBITED_KEYS = {
    "alias_family_id",
    "outcome",
    "privileged_puck_position",
    "privileged_puck_velocity",
    "shot_id",
    "target_label",
    "target_region",
    "true_puck_position",
    "true_puck_velocity",
}


def fnv1a64(text: str) -> str:
    # Match event_log::hash64_hex, including its repository-stable offset basis.
    value = 1469598103934665603
    for byte in text.encode("utf-8"):
        value ^= byte
        value = (value * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return f"fnv1a64:{value:016x}"


def all_keys(value: Any) -> set[str]:
    if isinstance(value, dict):
        return set(value).union(*(all_keys(item) for item in value.values()))
    if isinstance(value, list):
        return set().union(*(all_keys(item) for item in value))
    return set()


def main() -> int:
    schema = json.loads(EVENT_SCHEMA.read_text(encoding="utf-8"))
    Draft202012Validator.check_schema(schema)
    validator = Draft202012Validator(schema)
    rows = [
        json.loads(line)
        for line in FIXTURE.read_text(encoding="utf-8").splitlines()
        if line.strip()
    ]
    if len(rows) != 5:
        raise RuntimeError(
            "air-hockey H2b fixture must contain exactly five projected events"
        )
    for index, row in enumerate(rows, start=1):
        validator.validate(row)
        if row["seq"] != index:
            raise RuntimeError("air-hockey H2b fixture sequence is not contiguous")
        if row["run_id"] != "fixture-air-hockey-h2b":
            raise RuntimeError("air-hockey H2b fixture mixes run identities")
    if PROHIBITED_KEYS & all_keys(rows):
        raise RuntimeError(
            "air-hockey H2b fixture crosses the privileged-data boundary"
        )

    submissions = [row for row in rows if row["type"] == "vla_submit"]
    decisions = [
        row for row in rows if row["type"] == "vla_result" and "decision" in row["data"]
    ]
    contexts = [
        row
        for row in rows
        if row["type"] == "bb_write"
        and row["data"].get("key") == "air-hockey-context-id"
    ]
    dispatches = [row for row in rows if row["type"] == "cap_call_end"]
    if len(submissions) != 1 or len(decisions) != 1 or len(contexts) != 2:
        raise RuntimeError("air-hockey H2b fixture has an invalid evidence cardinality")
    for context in contexts:
        canonical_value = json.dumps(
            context["data"]["preview"], separators=(",", ":"), ensure_ascii=False
        )
        if context["data"]["value_digest"] != fnv1a64(canonical_value):
            raise RuntimeError("air-hockey H2b context digest is not canonical")
    submission = submissions[0]
    decision = decisions[0]
    if not (
        submission["seq"] < contexts[1]["seq"] < decision["seq"]
        and submission["data"]["captured_context_id"] == contexts[0]["data"]["preview"]
        and decision["data"]["captured_context_id"]
        == submission["data"]["captured_context_id"]
        and decision["data"]["current_context_id"] == contexts[1]["data"]["preview"]
        and decision["data"]["decision"] == "rejected"
        and decision["data"]["reason"] == "context_changed"
        and not dispatches
    ):
        raise RuntimeError("air-hockey H2b context-change predicate failed")

    predicate_document = json.loads(PREDICATES.read_text(encoding="utf-8"))
    names = set(predicate_document["predicates"])
    if not {"h2b_changed_context_rejected", "h2b_zero_obsolete_dispatch"}.issubset(
        names
    ):
        raise RuntimeError("air-hockey H2b named predicates are missing")
    print("air-hockey H2b canonical evidence fixture ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
