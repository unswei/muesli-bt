#!/usr/bin/env python3
"""Check the canonical humanoid VLA evidence fixture invariants."""

from __future__ import annotations

import json
import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]
FIXTURE = ROOT / "tests" / "fixtures" / "mbt.evt.v1" / "humanoid_vla_evidence_run.jsonl"


def fnv1a64(text: str) -> str:
    value = 0xCBF29CE484222325
    for byte in text.encode("utf-8"):
        value ^= byte
        value = (value * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return f"fnv1a64:{value:016x}"


def main() -> int:
    rows = [json.loads(line) for line in FIXTURE.read_text(encoding="utf-8").splitlines() if line.strip()]
    events = [row for row in rows if row["type"] != "run_start"]
    submissions = {row["data"]["job_id"]: row["data"] for row in events if row["type"] == "vla_submit"}
    results = {row["data"]["job_id"]: row["data"] for row in events if row["type"] == "vla_result"}
    dispatches = [row["data"] for row in events if row["type"] == "walking_target_dispatch"]
    revocations = {row["data"]["job_id"]: row["data"] for row in events if row["type"] == "async_authority_revoked"}

    assert [submissions[job]["generation"] for job in ("job-10", "job-11", "job-12")] == [1, 2, 3]
    assert results["job-10"]["decision"] == "accepted"
    assert results["job-10"]["captured_context_id"] == results["job-10"]["current_context_id"]
    assert results["job-11"]["decision"] == "rejected"
    assert results["job-11"]["reason"] == "context_changed"
    assert results["job-11"]["captured_context_id"] != results["job-11"]["current_context_id"]
    assert revocations["job-12"]["authority_state"] == "revoked"
    assert revocations["job-12"]["reason"] == "branch_revoked"

    accepted_dispatches = [row for row in dispatches if row["decision"] == "accepted"]
    assert len(accepted_dispatches) == 1
    dispatch = accepted_dispatches[0]
    assert dispatch["job_id"] == "job-10"
    assert dispatch["generation"] == results["job-10"]["generation"]
    assert dispatch["captured_context_id"] == results["job-10"]["captured_context_id"]
    assert dispatch["current_context_id"] == results["job-10"]["current_context_id"]
    assert dispatch["target"]["frame_id"] == "ball_context"
    canonical_target = json.dumps(dispatch["target"], separators=(",", ":"), ensure_ascii=False)
    assert dispatch["target_digest"] == fnv1a64(canonical_target)
    assert not any(row["job_id"] in {"job-11", "job-12"} for row in dispatches)

    print("humanoid VLA canonical evidence fixture ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
