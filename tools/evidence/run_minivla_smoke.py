#!/usr/bin/env python3
"""Run a curated MiniVLA model-service smoke evidence bundle."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import shutil
import subprocess
import sys
import urllib.error
import urllib.request
from datetime import datetime, timezone
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_FIXTURE = REPO_ROOT / "fixtures/model-service/minivla-smoke"
DEFAULT_MUSLISP = REPO_ROOT / "build/model-service-bridge-test/muslisp"
MODEL_ASYNC_REPORT_SCHEMA = "muesli-bt.model_async_evidence_report.v1"
RELEASE_SAFE_MODEL_ASYNC_REPORT_SCHEMA = "muesli-bt.release_safe_model_async_evidence_report.v1"
MODEL_ASYNC_REPORT_PROFILE = "model_service.vla_action_chunk_smoke.v1"
MINIVLA_SMOKE_INSTRUCTION = "Given the camera image, propose one safe low-speed robot action chunk."
RELEASE_METADATA_ALLOWLIST = {
    "adapter",
    "backend",
    "capability",
    "requires_gpu",
    "service",
    "service_version",
}


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def sha256_text(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def read_json(path: Path) -> object:
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def write_jsonl(path: Path, rows: list[object]) -> None:
    with path.open("w", encoding="utf-8") as f:
        for row in rows:
            f.write(json.dumps(row, sort_keys=True) + "\n")


def parse_lisp_json_stdout(path: Path) -> list[object]:
    rows: list[object] = []
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line == "nil":
            continue
        decoded = json.loads(line)
        rows.append(json.loads(decoded))
    return rows


def http_get_json(url: str, timeout_s: float) -> object:
    with urllib.request.urlopen(url, timeout=timeout_s) as response:
        return json.loads(response.read().decode("utf-8"))


def upload_frame(http_base: str, frame_path: Path, timeout_s: float) -> dict[str, object]:
    url = http_base.rstrip("/") + "/v1/frames/camera1"
    request = urllib.request.Request(
        url,
        data=frame_path.read_bytes(),
        headers={"Content-Type": "image/jpeg"},
        method="PUT",
    )
    with urllib.request.urlopen(request, timeout=timeout_s) as response:
        return json.loads(response.read().decode("utf-8"))


def shell_string(text: str) -> str:
    return json.dumps(text)


def release_redaction_policy() -> dict[str, object]:
    return {
        "schema": "muesli-bt.release_redaction_policy.v1",
        "profile": "release_safe",
        "purpose": "publishable evidence summaries without raw prompts, raw frame refs, backend placement details, or raw model-service envelopes",
        "raw_only": [
            "run-record.lisp",
            "run-replay.lisp",
            "record.stdout",
            "record.stderr",
            "replay.stdout",
            "replay.stderr",
            "model-service-cache/*.json",
            "model-service-describe.json",
            "frame_refs.json",
            "request_response_cache_index.json",
        ],
        "release_safe": [
            "redaction_policy.json",
            "release_safe_prompt_summary.json",
            "release_safe_frame_refs.json",
            "release_safe_model_service_describe.json",
            "release_safe_cache_summary.json",
            "release_safe_replay_report.json",
            "mock_host_dispatch_report.json",
        ],
        "rules": {
            "prompts": "replace prompt text with SHA-256 and character count",
            "frame_refs": "replace raw frame:// refs with SHA-256; keep fixture file names and fixture hashes",
            "backend_metadata": "allow only stable public fields; redact model paths, device names, worker URLs, and placement details",
            "cache_summaries": "keep request/response hashes and response file hashes; redact request ids, session ids, raw outputs, and raw metadata",
        },
    }


def redact_identifier(value: object) -> object:
    if isinstance(value, str) and value:
        return {"sha256": sha256_text(value), "redacted": True}
    return None


def redact_prompt_summary(name: str, prompt: str) -> dict[str, object]:
    return {
        "name": name,
        "sha256": sha256_text(prompt),
        "char_count": len(prompt),
        "redacted": True,
    }


def redact_ref(value: object) -> object:
    if not isinstance(value, str) or not value:
        return None
    scheme = value.split(":", 1)[0] if ":" in value else None
    return {
        "scheme": scheme,
        "sha256": sha256_text(value),
        "redacted": True,
    }


def redact_metadata(value: object) -> dict[str, object]:
    if not isinstance(value, dict):
        return {}
    out: dict[str, object] = {}
    redacted_key_count = 0
    for key, raw in sorted(value.items()):
        if key in RELEASE_METADATA_ALLOWLIST and (isinstance(raw, (str, int, float, bool)) or raw is None):
            out[key] = raw
        else:
            redacted_key_count += 1
    if redacted_key_count:
        out["_redacted_key_count"] = redacted_key_count
    return out


def redact_frame_refs(frames: list[dict[str, object]]) -> dict[str, object]:
    redacted_frames: list[dict[str, object]] = []
    for frame in frames:
        redacted_frames.append(
            {
                "name": frame.get("name"),
                "source_file": frame.get("source_file"),
                "bundle_file": frame.get("bundle_file"),
                "fixture_sha256": frame.get("fixture_sha256"),
                "content_sha256": frame.get("sha256") or frame.get("fixture_sha256"),
                "ref": redact_ref(frame.get("ref")),
                "latest_ref": redact_ref(frame.get("latest_ref")),
                "timestamp_ns": frame.get("timestamp_ns"),
            }
        )
    return {
        "schema": "muesli-bt.release_safe_frame_refs.v1",
        "redaction": "raw frame:// refs are private to the raw bundle; release artefacts keep hashes",
        "frames": redacted_frames,
    }


def redact_describe(describe: object) -> dict[str, object]:
    output = describe.get("output") if isinstance(describe, dict) else None
    capabilities = output.get("capabilities") if isinstance(output, dict) else None
    redacted_capabilities: list[dict[str, object]] = []
    if isinstance(capabilities, list):
        for capability in capabilities:
            if not isinstance(capability, dict):
                continue
            redacted_capabilities.append(
                {
                    "id": capability.get("id"),
                    "kind": capability.get("kind"),
                    "mode": capability.get("mode"),
                    "input_schema": capability.get("input_schema"),
                    "output_schema": capability.get("output_schema"),
                    "supports_cancel": capability.get("supports_cancel"),
                    "supports_deadline": capability.get("supports_deadline"),
                    "freshness": capability.get("freshness") if isinstance(capability.get("freshness"), dict) else {},
                    "replay": capability.get("replay") if isinstance(capability.get("replay"), dict) else {},
                    "metadata": redact_metadata(capability.get("metadata")),
                }
            )
    metadata = describe.get("metadata") if isinstance(describe, dict) else None
    return {
        "schema": "muesli-bt.release_safe_model_service_describe.v1",
        "version": describe.get("version") if isinstance(describe, dict) else None,
        "status": describe.get("status") if isinstance(describe, dict) else None,
        "metadata": redact_metadata(metadata),
        "capabilities": redacted_capabilities,
    }


def redact_cache_index(entries: list[dict[str, object]]) -> dict[str, object]:
    redacted_files: list[dict[str, object]] = []
    for entry in entries:
        redacted_files.append(
            {
                "file": entry.get("file"),
                "request_hash": entry.get("request_hash"),
                "sha256": entry.get("sha256"),
                "id": redact_identifier(entry.get("id")),
                "session_id": redact_identifier(entry.get("session_id")),
                "status": entry.get("status"),
                "has_output": entry.get("has_output"),
                "metadata": redact_metadata(entry.get("metadata")),
            }
        )
    return {
        "schema": "muesli-bt.release_safe_cache_summary.v1",
        "redaction": "raw model-service envelopes stay under model-service-cache; this file contains only hashes, statuses, and allowlisted metadata",
        "file_count": len(redacted_files),
        "files": redacted_files,
    }


def redact_replay_report(report: dict[str, object]) -> dict[str, object]:
    conditions = report.get("conditions", [])
    redacted_conditions: list[dict[str, object]] = []
    if isinstance(conditions, list):
        for condition in conditions:
            if not isinstance(condition, dict):
                continue
            artefacts = condition.get("artefacts", {})
            frame_refs = artefacts.get("frame_refs", []) if isinstance(artefacts, dict) else []
            redacted_artefacts = {
                "request_hashes": artefacts.get("request_hashes", []) if isinstance(artefacts, dict) else [],
                "response_hashes": artefacts.get("response_hashes", []) if isinstance(artefacts, dict) else [],
                "frame_ref_hashes": [redact_ref(ref) for ref in frame_refs] if isinstance(frame_refs, list) else [],
            }
            redacted_conditions.append(
                {
                    "condition_id": condition.get("condition_id"),
                    "capability": condition.get("capability"),
                    "operation_path": condition.get("operation_path", []),
                    "record": condition.get("record", {}),
                    "replay": condition.get("replay", {}),
                    "parity": condition.get("parity", {}),
                    "artefacts": redacted_artefacts,
                    "host_dispatch": condition.get("host_dispatch"),
                }
            )
    summary = report.get("summary", {}) if isinstance(report.get("summary"), dict) else {}
    return {
        "schema": RELEASE_SAFE_MODEL_ASYNC_REPORT_SCHEMA,
        "profile": report.get("profile"),
        "capability": report.get("capability"),
        "service_protocol": report.get("service_protocol"),
        "transport": report.get("transport"),
        "redaction": "raw frame refs and raw model-service envelopes are private to the raw evidence bundle",
        "summary": summary,
        "gates": report.get("gates", []),
        "conditions": redacted_conditions,
        "record_result_count": summary.get("record_result_count"),
        "replay_result_count": summary.get("replay_result_count"),
        "all_actions_match": summary.get("all_actions_match"),
        "all_replay_hits": summary.get("all_replay_hits"),
        "all_request_hashes_match": summary.get("all_request_hashes_match"),
        "all_response_hashes_match": summary.get("all_response_hashes_match"),
        "all_record_actions_host_safe": summary.get("all_record_actions_host_safe"),
        "all_replay_actions_host_safe": summary.get("all_replay_actions_host_safe"),
    }


def make_lisp_script(
    *,
    run_dir: Path,
    run_id: str,
    ws_endpoint: str,
    replay_mode: str,
    event_log_name: str,
    frames: list[dict[str, object]],
    poll_sleep_ms: int,
    poll_count: int,
) -> str:
    lines = [
        "(begin",
        f"  (events.set-path {shell_string(str(run_dir / event_log_name))})",
        "  (events.set-flush-each-message #t)",
        "  (events.set-ring-size 256)",
        "",
        "  (define cfg (map.make))",
        f"  (map.set! cfg 'endpoint {shell_string(ws_endpoint)})",
        "  (map.set! cfg 'connect_timeout_ms 5000)",
        "  (map.set! cfg 'request_timeout_ms 70000)",
        f"  (map.set! cfg 'replay_mode {shell_string(replay_mode)})",
        f"  (map.set! cfg 'replay_cache_path {shell_string(str(run_dir / 'model-service-cache'))})",
        "  (map.set! cfg 'check #t)",
        "  (model-service.configure cfg)",
        "  (print (json.encode (model-service.check)))",
        "  (print (json.encode (model-service.info)))",
        "",
        "  (define model (map.make))",
        "  (map.set! model 'name \"model-service\")",
        "  (map.set! model 'version \"0.2\")",
        "",
        "  (define (make-request task-id frame-ref tick ts-ms)",
        "    (let ((req (map.make)))",
        "      (map.set! req 'capability \"cap.vla.action_chunk.v1\")",
        "      (map.set! req 'task_id task-id)",
        f"      (map.set! req 'run_id {shell_string(run_id)})",
        "      (map.set! req 'tick_index tick)",
        "      (map.set! req 'node_name \"mini-vla-action-chunk\")",
        f"      (map.set! req 'instruction {shell_string(MINIVLA_SMOKE_INSTRUCTION)})",
        "      (map.set! req 'deadline_ms 60000)",
        "      (map.set! req 'seed task-id)",
        "      (map.set! req 'model model)",
        "      (let ((obs (map.make)))",
        "        (map.set! obs 'state (list 0.0 0.0 0.0 0.0 0.0 0.0 0.0))",
        "        (map.set! obs 'timestamp_ms ts-ms)",
        "        (map.set! obs 'frame_id frame-ref)",
        "        (map.set! req 'observation obs))",
        "      (let ((space (map.make)))",
        "        (map.set! space 'type \"continuous\")",
        "        (map.set! space 'dims 7)",
        "        (map.set! space 'bounds (list (list -2.0 2.0) (list -2.0 2.0) (list -2.0 2.0) (list -2.0 2.0) (list -2.0 2.0) (list -2.0 2.0) (list 0.0 1.0)))",
        "        (map.set! req 'action_space space))",
        "      (let ((constraints (map.make)))",
        "        (map.set! constraints 'max_abs_value 2.0)",
        "        (map.set! constraints 'max_delta 2.0)",
        "        (map.set! req 'constraints constraints))",
        "      req))",
        "",
        "  (define (terminal? p)",
        "    (or (eq? (map.get p 'status ':none) ':done)",
        "        (eq? (map.get p 'status ':none) ':error)",
        "        (eq? (map.get p 'status ':none) ':timeout)",
        "        (eq? (map.get p 'status ':none) ':cancelled)))",
        "",
        "  (define (wait-final id remaining)",
        "    (if (= remaining 0)",
        "        (vla.poll id)",
        "        (let ((p (vla.poll id)))",
        "          (if (terminal? p)",
        "              p",
        "              (begin",
        f"                (time.sleep-ms {poll_sleep_ms})",
        "                (wait-final id (- remaining 1)))))))",
        "",
        "  (define (run-one label frame-ref tick ts-ms)",
        "    (let ((job (vla.submit (make-request label frame-ref tick ts-ms))))",
        "      (let ((result (wait-final job " + str(poll_count) + ")))",
        "        (print (json.encode result))",
        "        result)))",
        "",
    ]

    for index, frame in enumerate(frames, start=1):
        label = str(frame["name"])
        ref = str(frame["ref"])
        timestamp_ms = int(int(frame["timestamp_ns"]) / 1_000_000)
        lines.append(
            f"  (run-one {shell_string(label)} {shell_string(ref)} {index} {timestamp_ms})"
        )

    lines.extend(
        [
            f"  (save {shell_string(str(run_dir / (replay_mode + '-events.snapshot.lisp')))} (events.dump 256))",
            "  (model-service.clear))",
            "",
        ]
    )
    return "\n".join(lines)


def run_muslisp(muslisp: Path, script: Path, stdout: Path, stderr: Path) -> None:
    with stdout.open("w", encoding="utf-8") as out, stderr.open("w", encoding="utf-8") as err:
        subprocess.run([str(muslisp), str(script)], cwd=REPO_ROOT, stdout=out, stderr=err, check=True)


def validate_action(row: dict[str, object]) -> dict[str, object]:
    final = row.get("final")
    if not isinstance(final, dict):
        return {"validation_status": "rejected", "reason": "missing_final", "host_reached": False}
    action = final.get("action")
    if not isinstance(action, dict):
        return {"validation_status": "rejected", "reason": "missing_action", "host_reached": False}
    values = action.get("u")
    if not isinstance(values, list) or len(values) != 7:
        return {"validation_status": "rejected", "reason": "wrong_action_dimension", "host_reached": False}
    finite = all(isinstance(v, (int, float)) and math.isfinite(float(v)) for v in values)
    bounds_ok = finite and all(-2.0 <= float(v) <= 2.0 for v in values[:6]) and 0.0 <= float(values[6]) <= 1.0
    status_ok = row.get("status") == ":done" and final.get("status") == ":ok"
    if status_ok and bounds_ok:
        return {"validation_status": "accepted", "reason": "host_safe_action_proposal", "host_reached": False}
    return {"validation_status": "rejected", "reason": "invalid_or_unsafe_action_proposal", "host_reached": False}


def mock_host_dispatch(row: dict[str, object], condition_id: str) -> dict[str, object]:
    validation = validate_action(row)
    final = row.get("final") if isinstance(row.get("final"), dict) else {}
    action = final.get("action") if isinstance(final, dict) and isinstance(final.get("action"), dict) else {}
    values = action.get("u") if isinstance(action, dict) else None
    if validation["validation_status"] != "accepted" or not isinstance(values, list):
        return {
            "condition_id": condition_id,
            "host": "mock-host.v1",
            "dispatch_status": "rejected",
            "validation": validation,
            "host_reached": False,
            "reason": validation.get("reason"),
        }

    host_action = {
        "schema": "mock_host.action_handoff.v1",
        "source": "validated_vla_proposal",
        "condition_id": condition_id,
        "u": values,
    }
    action_hash = sha256_text(json.dumps(host_action, sort_keys=True, separators=(",", ":")))
    return {
        "condition_id": condition_id,
        "host": "mock-host.v1",
        "dispatch_status": "accepted",
        "validation": validation,
        "host_reached": True,
        "action_schema": host_action["schema"],
        "action_hash": action_hash,
        "action_dims": len(values),
    }


def build_mock_host_dispatch_report(record_rows: list[dict[str, object]], condition_ids: list[str]) -> dict[str, object]:
    dispatches = [
        mock_host_dispatch(row, condition_ids[index] if index < len(condition_ids) else f"condition-{index + 1:03d}")
        for index, row in enumerate(record_rows)
    ]
    return {
        "schema": "muesli-bt.mock_host_dispatch_report.v1",
        "host": "mock-host.v1",
        "source_boundary": "validated_vla_proposal",
        "dispatch_count": len(dispatches),
        "accepted_count": sum(1 for row in dispatches if row["dispatch_status"] == "accepted"),
        "all_dispatches_host_reached": all(row["host_reached"] is True for row in dispatches),
        "all_dispatches_validated": all(
            row["validation"]["validation_status"] == "accepted" for row in dispatches
        ),
        "dispatches": dispatches,
    }


def index_cache(run_dir: Path) -> list[dict[str, object]]:
    entries: list[dict[str, object]] = []
    for path in sorted((run_dir / "model-service-cache").glob("*.json")):
        response = read_json(path)
        assert isinstance(response, dict)
        entries.append(
            {
                "file": str(path.relative_to(run_dir)),
                "request_hash": path.stem,
                "sha256": sha256_file(path),
                "id": response.get("id"),
                "session_id": response.get("session_id"),
                "status": response.get("status"),
                "has_output": response.get("output") is not None,
                "metadata": response.get("metadata"),
            }
        )
    return entries


def build_report(record_rows: list[dict[str, object]],
                 replay_rows: list[dict[str, object]],
                 condition_ids: list[str] | None = None,
                 host_dispatch_report: dict[str, object] | None = None) -> dict[str, object]:
    conditions = []
    dispatches_by_id = {}
    if host_dispatch_report is not None and isinstance(host_dispatch_report.get("dispatches"), list):
        dispatches_by_id = {
            str(row.get("condition_id")): row
            for row in host_dispatch_report["dispatches"]
            if isinstance(row, dict) and row.get("condition_id") is not None
        }
    for index, (record, replay) in enumerate(zip(record_rows, replay_rows, strict=False), start=1):
        record_final = record.get("final", {}) if isinstance(record.get("final"), dict) else {}
        replay_final = replay.get("final", {}) if isinstance(replay.get("final"), dict) else {}
        record_ms = record_final.get("model_service", {}) if isinstance(record_final.get("model_service"), dict) else {}
        replay_ms = replay_final.get("model_service", {}) if isinstance(replay_final.get("model_service"), dict) else {}
        record_validation = validate_action(record)
        replay_validation = validate_action(replay)
        condition_id = (
            condition_ids[index - 1]
            if condition_ids is not None and index - 1 < len(condition_ids)
            else str(record.get("task_id") or record.get("id") or f"condition-{index:03d}")
        )
        request_hashes_match = record_ms.get("request_hashes") == replay_ms.get("request_hashes")
        response_hashes_match = record_ms.get("response_hashes") == replay_ms.get("response_hashes")
        frame_refs_match = record_ms.get("frame_refs") == replay_ms.get("frame_refs")
        actions_match = record_final.get("action") == replay_final.get("action")
        conditions.append(
            {
                "condition_id": condition_id,
                "capability": "cap.vla.action_chunk.v1",
                "operation_path": ["start", "step", "close"],
                "record": {
                    "status": record.get("status"),
                    "final_status": record_final.get("status"),
                    "validation": record_validation,
                    "replay_cache_hit": record_ms.get("replay_cache_hit"),
                    "host_reached": record_validation.get("host_reached"),
                },
                "replay": {
                    "status": replay.get("status"),
                    "final_status": replay_final.get("status"),
                    "validation": replay_validation,
                    "replay_cache_hit": replay_ms.get("replay_cache_hit"),
                    "host_reached": replay_validation.get("host_reached"),
                },
                "parity": {
                    "actions_match": actions_match,
                    "frame_refs_match": frame_refs_match,
                    "request_hashes_match": request_hashes_match,
                    "response_hashes_match": response_hashes_match,
                },
                "artefacts": {
                    "request_hashes": record_ms.get("request_hashes", []),
                    "response_hashes": record_ms.get("response_hashes", []),
                    "frame_refs": record_ms.get("frame_refs", []),
                },
                "host_dispatch": dispatches_by_id.get(condition_id),
            }
        )
    dispatch_count = int(host_dispatch_report.get("dispatch_count", 0)) if host_dispatch_report else 0
    accepted_dispatch_count = int(host_dispatch_report.get("accepted_count", 0)) if host_dispatch_report else 0
    all_dispatches_host_reached = (
        bool(host_dispatch_report.get("all_dispatches_host_reached")) if host_dispatch_report else False
    )
    all_dispatches_validated = (
        bool(host_dispatch_report.get("all_dispatches_validated")) if host_dispatch_report else False
    )
    summary = {
        "record_result_count": len(record_rows),
        "replay_result_count": len(replay_rows),
        "condition_count": len(conditions),
        "mock_host_dispatch_count": dispatch_count,
        "mock_host_dispatch_accepted_count": accepted_dispatch_count,
        "all_mock_host_dispatches_host_reached": all_dispatches_host_reached,
        "all_mock_host_dispatches_validated": all_dispatches_validated,
        "all_actions_match": all(c["parity"]["actions_match"] for c in conditions),
        "all_replay_hits": all(c["replay"]["replay_cache_hit"] is True for c in conditions),
        "all_request_hashes_match": all(c["parity"]["request_hashes_match"] for c in conditions),
        "all_response_hashes_match": all(c["parity"]["response_hashes_match"] for c in conditions),
        "all_frame_refs_match": all(c["parity"]["frame_refs_match"] for c in conditions),
        "all_record_actions_host_safe": all(
            c["record"]["validation"]["validation_status"] == "accepted" and c["record"]["host_reached"] is False
            for c in conditions
        ),
        "all_replay_actions_host_safe": all(
            c["replay"]["validation"]["validation_status"] == "accepted" and c["replay"]["host_reached"] is False
            for c in conditions
        ),
    }
    gates = [
        {
            "name": "record_completed",
            "required": True,
            "passed": summary["record_result_count"] == summary["condition_count"],
        },
        {
            "name": "replay_completed",
            "required": True,
            "passed": summary["replay_result_count"] == summary["condition_count"],
        },
        {"name": "replay_cache_hit", "required": True, "passed": summary["all_replay_hits"]},
        {"name": "action_parity", "required": True, "passed": summary["all_actions_match"]},
        {"name": "request_hash_parity", "required": True, "passed": summary["all_request_hashes_match"]},
        {"name": "response_hash_parity", "required": True, "passed": summary["all_response_hashes_match"]},
        {"name": "frame_ref_parity", "required": True, "passed": summary["all_frame_refs_match"]},
        {"name": "record_host_reached_zero", "required": True, "passed": summary["all_record_actions_host_safe"]},
        {"name": "replay_host_reached_zero", "required": True, "passed": summary["all_replay_actions_host_safe"]},
        {
            "name": "mock_host_dispatch_completed",
            "required": True,
            "passed": summary["mock_host_dispatch_count"] == summary["condition_count"],
        },
        {
            "name": "mock_host_dispatch_validated",
            "required": True,
            "passed": summary["all_mock_host_dispatches_validated"],
        },
        {
            "name": "mock_host_dispatch_reached",
            "required": True,
            "passed": summary["all_mock_host_dispatches_host_reached"],
        },
    ]
    report = {
        "schema": MODEL_ASYNC_REPORT_SCHEMA,
        "profile": MODEL_ASYNC_REPORT_PROFILE,
        "capability": "cap.vla.action_chunk.v1",
        "service_protocol": "MMSP v0.2",
        "transport": "model-service websocket plus HTTP frame ingest",
        "evidence_kind": "model_backed_async_replay",
        "host_dispatch_boundary": "validated mock host action handoff",
        "summary": summary,
        "gates": gates,
        "conditions": conditions,
        "legacy_schema": "muesli-bt.minivla.replay_report.v1",
        "record_result_count": summary["record_result_count"],
        "replay_result_count": summary["replay_result_count"],
        "all_actions_match": summary["all_actions_match"],
        "all_replay_hits": summary["all_replay_hits"],
        "all_request_hashes_match": summary["all_request_hashes_match"],
        "all_response_hashes_match": summary["all_response_hashes_match"],
        "all_record_actions_host_safe": summary["all_record_actions_host_safe"],
        "all_replay_actions_host_safe": summary["all_replay_actions_host_safe"],
    }
    return report


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixture-dir", type=Path, default=DEFAULT_FIXTURE)
    parser.add_argument("--out-dir", type=Path)
    parser.add_argument("--http-endpoint", default="http://127.0.0.1:8765")
    parser.add_argument("--ws-endpoint", default="ws://127.0.0.1:8765/v1/ws")
    parser.add_argument("--muslisp", type=Path, default=DEFAULT_MUSLISP)
    parser.add_argument("--poll-sleep-ms", type=int, default=50)
    parser.add_argument("--poll-count", type=int, default=1400)
    parser.add_argument("--service-release-tag", default="")
    parser.add_argument("--service-git-commit", default="")
    parser.add_argument("--no-replay", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    fixture_dir = args.fixture_dir.resolve()
    muslisp = args.muslisp.resolve()
    if not muslisp.is_file():
        print(f"missing muslisp executable: {muslisp}", file=sys.stderr)
        return 2
    fixture_manifest = read_json(fixture_dir / "manifest.json")
    if not isinstance(fixture_manifest, dict):
        print("fixture manifest must be a JSON object", file=sys.stderr)
        return 2

    run_id = "minivla-smoke-" + datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    run_dir = (args.out_dir or (REPO_ROOT / "build/evidence" / run_id)).resolve()
    run_dir.mkdir(parents=True, exist_ok=True)
    (run_dir / "frames").mkdir(exist_ok=True)
    (run_dir / "model-service-cache").mkdir(exist_ok=True)

    health = http_get_json(args.http_endpoint.rstrip("/") + "/health", 5.0)
    describe = http_get_json(args.http_endpoint.rstrip("/") + "/v1/describe", 5.0)

    uploaded_frames: list[dict[str, object]] = []
    fixture_frames = fixture_manifest.get("frames", [])
    if not isinstance(fixture_frames, list) or not fixture_frames:
        print("fixture manifest has no frames", file=sys.stderr)
        return 2
    for frame in fixture_frames:
        if not isinstance(frame, dict):
            print("fixture frame entry must be an object", file=sys.stderr)
            return 2
        source = fixture_dir / str(frame["file"])
        expected_sha = str(frame["sha256"])
        actual_sha = sha256_file(source)
        if actual_sha != expected_sha:
            print(f"fixture hash mismatch for {source}: {actual_sha} != {expected_sha}", file=sys.stderr)
            return 2
        target = run_dir / "frames" / source.name
        shutil.copy2(source, target)
        uploaded = upload_frame(args.http_endpoint, source, 30.0)
        if not isinstance(uploaded, dict):
            print(f"frame ingest did not return an object for {source}", file=sys.stderr)
            return 3
        frame_record = {
            "name": source.stem,
            "source_file": str(source.relative_to(REPO_ROOT)),
            "bundle_file": str(target.relative_to(run_dir)),
            "fixture_sha256": actual_sha,
            **uploaded,
        }
        uploaded_frames.append(frame_record)
        write_json(run_dir / "frames" / f"{source.stem}.frame.json", frame_record)

    write_json(run_dir / "model-service-health.json", health)
    write_json(run_dir / "model-service-describe.json", describe)
    write_json(run_dir / "frame_refs.json", {"frames": uploaded_frames})

    record_script = run_dir / "run-record.lisp"
    record_script.write_text(
        make_lisp_script(
            run_dir=run_dir,
            run_id=run_id,
            ws_endpoint=args.ws_endpoint,
            replay_mode="record",
            event_log_name="events.jsonl",
            frames=uploaded_frames,
            poll_sleep_ms=args.poll_sleep_ms,
            poll_count=args.poll_count,
        ),
        encoding="utf-8",
    )
    run_muslisp(muslisp, record_script, run_dir / "record.stdout", run_dir / "record.stderr")
    record_stdout = parse_lisp_json_stdout(run_dir / "record.stdout")
    record_results = [row for row in record_stdout[2:] if isinstance(row, dict)]
    write_jsonl(run_dir / "record-results.jsonl", record_results)

    replay_results: list[dict[str, object]] = []
    if not args.no_replay:
        replay_script = run_dir / "run-replay.lisp"
        replay_script.write_text(
            make_lisp_script(
                run_dir=run_dir,
                run_id=run_id,
                ws_endpoint=args.ws_endpoint,
                replay_mode="replay",
                event_log_name="replay-events.jsonl",
                frames=uploaded_frames,
                poll_sleep_ms=args.poll_sleep_ms,
                poll_count=args.poll_count,
            ),
            encoding="utf-8",
        )
        run_muslisp(muslisp, replay_script, run_dir / "replay.stdout", run_dir / "replay.stderr")
        replay_stdout = parse_lisp_json_stdout(run_dir / "replay.stdout")
        replay_results = [row for row in replay_stdout[2:] if isinstance(row, dict)]
        write_jsonl(run_dir / "replay-results.jsonl", replay_results)

    cache_index = index_cache(run_dir)
    write_json(run_dir / "request_response_cache_index.json", {"files": cache_index})
    condition_ids = [str(frame["name"]) for frame in uploaded_frames]
    mock_host_dispatch_report = build_mock_host_dispatch_report(record_results, condition_ids)
    write_json(run_dir / "mock_host_dispatch_report.json", mock_host_dispatch_report)
    replay_report = build_report(record_results, replay_results, condition_ids, mock_host_dispatch_report) if replay_results else {}
    write_json(run_dir / "replay_report.json", replay_report)
    redaction_policy = release_redaction_policy()
    release_safe_prompt_summary = {
        "schema": "muesli-bt.release_safe_prompt_summary.v1",
        "redaction": "prompt text is private to the raw bundle; release artefacts keep hashes and sizes",
        "prompts": [redact_prompt_summary("minivla_smoke_instruction", MINIVLA_SMOKE_INSTRUCTION)],
    }
    release_safe_frame_refs = redact_frame_refs(uploaded_frames)
    release_safe_describe = redact_describe(describe)
    release_safe_cache_summary = redact_cache_index(cache_index)
    release_safe_replay_report = redact_replay_report(replay_report) if replay_report else {}
    write_json(run_dir / "redaction_policy.json", redaction_policy)
    write_json(run_dir / "release_safe_prompt_summary.json", release_safe_prompt_summary)
    write_json(run_dir / "release_safe_frame_refs.json", release_safe_frame_refs)
    write_json(run_dir / "release_safe_model_service_describe.json", release_safe_describe)
    write_json(run_dir / "release_safe_cache_summary.json", release_safe_cache_summary)
    write_json(run_dir / "release_safe_replay_report.json", release_safe_replay_report)

    git_sha = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=REPO_ROOT, text=True).strip()
    git_describe = subprocess.check_output(["git", "describe", "--tags", "--always", "--dirty"], cwd=REPO_ROOT, text=True).strip()
    dirty = subprocess.run(["git", "diff", "--quiet"], cwd=REPO_ROOT).returncode != 0
    manifest = {
        "schema": "muesli-bt.evidence.minivla_smoke.v1",
        "bundle_name": run_dir.name,
        "generated_at_utc": datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z"),
        "git_sha": git_sha,
        "git_describe": git_describe,
        "dirty_worktree": dirty,
        "fixture_manifest": str((fixture_dir / "manifest.json").relative_to(REPO_ROOT)),
        "model_service": {
            "http_endpoint": args.http_endpoint,
            "ws_endpoint": args.ws_endpoint,
            "health": health,
            "release_tag": args.service_release_tag or None,
            "git_commit": args.service_git_commit or None,
            "capability": "cap.vla.action_chunk.v1",
            "backend": "minivla",
        },
        "artefacts": {
            "events": "events.jsonl",
            "replay_events": "replay-events.jsonl",
            "manifest": "manifest.json",
            "replay_report": "replay_report.json",
            "request_response_cache": "model-service-cache/",
            "request_response_cache_index": "request_response_cache_index.json",
            "frame_refs": "frame_refs.json",
            "record_results": "record-results.jsonl",
            "replay_results": "replay-results.jsonl",
            "redaction_policy": "redaction_policy.json",
            "release_safe_prompt_summary": "release_safe_prompt_summary.json",
            "release_safe_frame_refs": "release_safe_frame_refs.json",
            "release_safe_model_service_describe": "release_safe_model_service_describe.json",
            "release_safe_cache_summary": "release_safe_cache_summary.json",
            "release_safe_replay_report": "release_safe_replay_report.json",
            "mock_host_dispatch_report": "mock_host_dispatch_report.json",
        },
        "release_safety": {
            "profile": "release_safe",
            "publishable_artefacts": redaction_policy["release_safe"],
            "raw_only_artefacts": redaction_policy["raw_only"],
        },
        "hashes": {
            "events_jsonl_sha256": sha256_file(run_dir / "events.jsonl"),
            "record_results_jsonl_sha256": sha256_file(run_dir / "record-results.jsonl"),
            "replay_events_jsonl_sha256": sha256_file(run_dir / "replay-events.jsonl")
            if (run_dir / "replay-events.jsonl").exists()
            else None,
            "replay_results_jsonl_sha256": sha256_file(run_dir / "replay-results.jsonl")
            if (run_dir / "replay-results.jsonl").exists()
            else None,
            "replay_report_json_sha256": sha256_file(run_dir / "replay_report.json"),
            "release_safe_replay_report_json_sha256": sha256_file(run_dir / "release_safe_replay_report.json"),
            "release_safe_cache_summary_json_sha256": sha256_file(run_dir / "release_safe_cache_summary.json"),
            "mock_host_dispatch_report_json_sha256": sha256_file(run_dir / "mock_host_dispatch_report.json"),
        },
        "summary": {
            "record_runs": len(record_results),
            "replay_runs": len(replay_results),
            "record_successes": sum(
                1 for row in record_results if row.get("status") == ":done" and row.get("final", {}).get("status") == ":ok"
            ),
            "replay_successes": sum(
                1 for row in replay_results if row.get("status") == ":done" and row.get("final", {}).get("status") == ":ok"
            ),
            "cache_files": len(cache_index),
            "all_replay_hits": replay_report.get("all_replay_hits") if replay_report else None,
            "all_actions_match": replay_report.get("all_actions_match") if replay_report else None,
            "all_record_actions_host_safe": replay_report.get("all_record_actions_host_safe") if replay_report else None,
            "mock_host_dispatches": mock_host_dispatch_report["dispatch_count"],
            "all_mock_host_dispatches_host_reached": mock_host_dispatch_report["all_dispatches_host_reached"],
        },
    }
    write_json(run_dir / "manifest.json", manifest)

    failures = []
    if manifest["summary"]["record_successes"] != manifest["summary"]["record_runs"]:
        failures.append("record successes do not match record run count")
    if replay_results and manifest["summary"]["replay_successes"] != manifest["summary"]["replay_runs"]:
        failures.append("replay successes do not match replay run count")
    if replay_results and not replay_report.get("all_replay_hits"):
        failures.append("not all replay calls hit the replay cache")
    if replay_results and not replay_report.get("all_actions_match"):
        failures.append("replayed actions do not match recorded actions")
    if replay_results and not replay_report.get("all_record_actions_host_safe"):
        failures.append("recorded actions were not all accepted host-safe proposals")
    if not mock_host_dispatch_report.get("all_dispatches_host_reached"):
        failures.append("validated actions did not all reach the mock host")

    print(json.dumps({"run_dir": str(run_dir), "summary": manifest["summary"], "failures": failures}, indent=2))
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
