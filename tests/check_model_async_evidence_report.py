#!/usr/bin/env python3
"""Check the standard model-backed async evidence report shape."""

from __future__ import annotations

import copy
import importlib.util
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
RUNNER = REPO_ROOT / "tools/evidence/run_minivla_smoke.py"


def load_runner():
    spec = importlib.util.spec_from_file_location("run_minivla_smoke", RUNNER)
    assert spec is not None
    assert spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def sample_result(*, replay_cache_hit: bool) -> dict[str, object]:
    return {
        "status": ":done",
        "final": {
            "status": ":ok",
            "action": {
                "action_schema": "vla.action_chunk.v1",
                "u": [0.1, -0.1, 0.0, 0.2, -0.2, 0.1, 0.5],
            },
            "model_service": {
                "request_hashes": ["fnv1a64:req-start", "fnv1a64:req-step", "fnv1a64:req-close"],
                "response_hashes": ["fnv1a64:res-start", "fnv1a64:res-step", "fnv1a64:res-close"],
                "frame_refs": ["frame://camera1/1730000000000000000"],
                "replay_cache_hit": replay_cache_hit,
            },
        },
    }


def test_model_async_report_shape() -> None:
    runner = load_runner()
    record = sample_result(replay_cache_hit=False)
    replay = copy.deepcopy(sample_result(replay_cache_hit=True))
    report = runner.build_report([record], [replay])
    assert report["schema"] == "muesli-bt.model_async_evidence_report.v1"
    assert report["profile"] == "model_service.vla_action_chunk_smoke.v1"
    assert report["summary"]["condition_count"] == 1
    assert report["summary"]["all_replay_hits"] is True
    assert report["summary"]["all_record_actions_host_safe"] is True
    assert all(gate["passed"] for gate in report["gates"] if gate["required"])
    condition = report["conditions"][0]
    assert condition["capability"] == "cap.vla.action_chunk.v1"
    assert condition["operation_path"] == ["start", "step", "close"]
    assert condition["record"]["host_reached"] is False
    assert condition["replay"]["replay_cache_hit"] is True
    assert condition["parity"]["request_hashes_match"] is True


def test_release_safe_report_redacts_frame_refs() -> None:
    runner = load_runner()
    record = sample_result(replay_cache_hit=False)
    replay = copy.deepcopy(sample_result(replay_cache_hit=True))
    report = runner.build_report([record], [replay])
    safe = runner.redact_replay_report(report)
    text = str(safe)
    assert safe["schema"] == "muesli-bt.release_safe_model_async_evidence_report.v1"
    assert "frame://camera1/1730000000000000000" not in text
    assert safe["conditions"][0]["artefacts"]["frame_ref_hashes"][0]["scheme"] == "frame"
    assert safe["summary"]["all_actions_match"] is True


if __name__ == "__main__":
    test_model_async_report_shape()
    test_release_safe_report_redacts_frame_refs()
