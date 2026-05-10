#!/usr/bin/env python3
"""Check release-safe redaction helpers for the MiniVLA evidence runner."""

from __future__ import annotations

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


def test_prompt_text_is_hashed_not_copied() -> None:
    runner = load_runner()
    prompt = "move forward slowly"
    summary = runner.redact_prompt_summary("test", prompt)
    assert summary["redacted"] is True
    assert summary["char_count"] == len(prompt)
    assert "move forward" not in str(summary)


def test_frame_refs_are_hashed_not_copied() -> None:
    runner = load_runner()
    raw_ref = "frame://camera1/1730000000000000000"
    frame_refs = runner.redact_frame_refs(
        [
            {
                "name": "frame-a",
                "source_file": "fixtures/frame-a.jpg",
                "bundle_file": "frames/frame-a.jpg",
                "fixture_sha256": "abc",
                "ref": raw_ref,
                "latest_ref": "frame://camera1/latest",
                "timestamp_ns": 1730000000000000000,
            }
        ]
    )
    text = str(frame_refs)
    assert raw_ref not in text
    assert "frame://camera1/latest" not in text
    assert frame_refs["frames"][0]["ref"]["scheme"] == "frame"


def test_backend_metadata_uses_public_allowlist() -> None:
    runner = load_runner()
    metadata = runner.redact_metadata(
        {
            "backend": "minivla",
            "adapter": "openvla-mini",
            "capability": "cap.vla.action_chunk.v1",
            "model_path": "Stanford-ILIAD/minivla-vq-bridge-prismatic",
            "device": "cuda",
            "worker_url": "http://127.0.0.1:8766",
        }
    )
    assert metadata["backend"] == "minivla"
    assert metadata["adapter"] == "openvla-mini"
    assert "model_path" not in metadata
    assert "device" not in metadata
    assert "worker_url" not in metadata
    assert metadata["_redacted_key_count"] == 3


def test_cache_summary_redacts_request_and_session_ids() -> None:
    runner = load_runner()
    raw_id = "vla-start-webots-epuck-goal-start"
    raw_session = "sess-000001"
    summary = runner.redact_cache_index(
        [
            {
                "file": "model-service-cache/fnv1a64:abc.json",
                "request_hash": "fnv1a64:abc",
                "sha256": "def",
                "id": raw_id,
                "session_id": raw_session,
                "status": "success",
                "has_output": True,
                "metadata": {"backend": "minivla", "worker_url": "http://127.0.0.1:8766"},
            }
        ]
    )
    text = str(summary)
    assert raw_id not in text
    assert raw_session not in text
    assert "worker_url" not in summary["files"][0]["metadata"]
    assert summary["files"][0]["id"]["redacted"] is True


if __name__ == "__main__":
    test_prompt_text_is_hashed_not_copied()
    test_frame_refs_are_hashed_not_copied()
    test_backend_metadata_uses_public_allowlist()
    test_cache_summary_redacts_request_and_session_ids()
