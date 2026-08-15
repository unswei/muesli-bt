"""Validate and analyse versioned air-hockey evidence bundles."""

from __future__ import annotations

import hashlib
import html
import json
import math
import random
import re
import shutil
from collections import defaultdict
from collections.abc import Iterable
from pathlib import Path
from typing import Any

from jsonschema import Draft202012Validator

EXAMPLE_ROOT = Path(__file__).resolve().parents[1]
REPOSITORY_ROOT = EXAMPLE_ROOT.parents[1]
SCHEMA_ROOT = REPOSITORY_ROOT / "schemas" / "air_hockey_evidence" / "v1"
EVENT_SCHEMA = (
    REPOSITORY_ROOT / "schemas" / "event_log" / "v1" / "mbt.evt.v1.schema.json"
)
RUN_MARKER = ".air-hockey-evidence-run"
SAFE_COMPONENT = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$")
DERIVED_ARTEFACTS = {
    "event-validation.json",
    "trace-validation.json",
    "trial-summary.json",
    "replay-report.json",
    "overlay-timeline.jsonl",
    "overlay.svg",
    "bundle-validation.json",
}
PROHIBITED_CONTROL_KEYS = {
    "alias_family_id",
    "contact",
    "outcome",
    "policy_outcome",
    "privileged",
    "privileged_intercept_target",
    "shot_id",
    "target_goal",
    "target_label",
    "target_region",
    "true_puck_position",
    "true_puck_velocity",
}


class EvidenceError(RuntimeError):
    """An evidence contract or reproducibility invariant failed."""


class RecordedProviderReplay:
    """Fail-closed lookup over immutable recorded provider responses."""

    def __init__(self, records: list[dict[str, Any]]) -> None:
        self._records: dict[str, dict[str, Any]] = {}
        for record in records:
            request_hash = str(record.get("request_sha256", ""))
            if not request_hash or request_hash in self._records:
                raise EvidenceError(
                    "recorded provider request identities must be unique"
                )
            self._records[request_hash] = record

    def infer(self, request_sha256: str) -> dict[str, Any]:
        record = self._records.get(request_sha256)
        if record is None:
            raise EvidenceError("recorded provider has no exact request match")
        if record["response_sha256"] != semantic_sha256(record["action"]):
            raise EvidenceError("recorded provider response digest mismatch")
        return dict(record["action"])


def read_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except Exception as exc:
        raise EvidenceError(f"failed to read JSON {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise EvidenceError(f"expected a JSON object: {path}")
    return value


def read_jsonl(path: Path) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    try:
        for line_number, line in enumerate(
            path.read_text(encoding="utf-8").splitlines(), start=1
        ):
            if not line.strip():
                continue
            value = json.loads(line)
            if not isinstance(value, dict):
                raise EvidenceError(f"{path}:{line_number}: expected a JSON object")
            rows.append(value)
    except EvidenceError:
        raise
    except Exception as exc:
        raise EvidenceError(f"failed to read JSONL {path}: {exc}") from exc
    if not rows:
        raise EvidenceError(f"JSONL artefact is empty: {path}")
    return rows


def write_json(path: Path, value: object) -> None:
    path.write_text(
        json.dumps(value, indent=2, sort_keys=True, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )


def write_jsonl(path: Path, rows: Iterable[dict[str, Any]]) -> None:
    path.write_text(
        "".join(
            json.dumps(row, sort_keys=True, separators=(",", ":"), ensure_ascii=False)
            + "\n"
            for row in rows
        ),
        encoding="utf-8",
    )


def semantic_sha256(value: object) -> str:
    canonical = json.dumps(
        value, sort_keys=True, separators=(",", ":"), ensure_ascii=False
    ).encode("utf-8")
    return f"sha256:{hashlib.sha256(canonical).hexdigest()}"


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return f"sha256:{digest.hexdigest()}"


def _schema(name: str) -> dict[str, Any]:
    schema = read_json(SCHEMA_ROOT / name)
    Draft202012Validator.check_schema(schema)
    return schema


def _validator(name: str) -> Draft202012Validator:
    return Draft202012Validator(_schema(name))


def _all_keys(value: Any) -> set[str]:
    if isinstance(value, dict):
        nested = set(value)
        for item in value.values():
            nested.update(_all_keys(item))
        return nested
    if isinstance(value, list):
        nested: set[str] = set()
        for item in value:
            nested.update(_all_keys(item))
        return nested
    return set()


def _validate_rows(
    rows: list[dict[str, Any]],
    validator: Draft202012Validator,
    path: Path,
) -> None:
    for line_number, row in enumerate(rows, start=1):
        errors = sorted(validator.iter_errors(row), key=lambda error: list(error.path))
        if errors:
            location = "/".join(str(item) for item in errors[0].path)
            raise EvidenceError(
                f"{path}:{line_number}:{location or '<root>'}: {errors[0].message}"
            )


def _require_contiguous(rows: list[dict[str, Any]], field: str, path: Path) -> None:
    for expected, row in enumerate(rows, start=1):
        if row.get(field) != expected:
            raise EvidenceError(f"{path}: {field} must be contiguous from one")


def _validate_trajectory(
    rows: list[dict[str, Any]], path: Path, expected_run_id: str
) -> None:
    _validate_rows(
        rows,
        _validator("airhockey.task_trajectory.v1.schema.json"),
        path,
    )
    _require_contiguous(rows, "seq", path)
    previous_step = -1
    previous_ns = -1
    for index, row in enumerate(rows):
        if row["run_id"] != expected_run_id:
            raise EvidenceError(f"{path}: trajectory mixes run identities")
        step = row["observation_step"]
        monotonic_ns = row["monotonic_ns"]
        if step <= previous_step or monotonic_ns <= previous_ns:
            raise EvidenceError(
                f"{path}: observation steps and monotonic times must increase"
            )
        previous_step = step
        previous_ns = monotonic_ns
        public = row["public"]
        if not public["defence_context_id"].startswith(public["episode_id"] + "/"):
            raise EvidenceError(f"{path}: context does not belong to its episode")
        terminal = public["terminated"] or public["truncated"]
        if public["terminated"] and public["truncated"]:
            raise EvidenceError(f"{path}: terminal and truncated cannot both be true")
        if public["episode_active"] == terminal:
            raise EvidenceError(f"{path}: episode lifecycle flags are inconsistent")
        if index < len(rows) - 1 and row["privileged"]["outcome"] != "pending":
            raise EvidenceError(f"{path}: outcome is visible before the final record")
        if index == len(rows) - 1 and not terminal:
            raise EvidenceError(f"{path}: final trajectory record must be terminal")


def _validate_events(
    rows: list[dict[str, Any]], path: Path, expected_run_id: str | None = None
) -> None:
    event_schema = read_json(EVENT_SCHEMA)
    Draft202012Validator.check_schema(event_schema)
    _validate_rows(rows, Draft202012Validator(event_schema), path)
    _require_contiguous(rows, "seq", path)
    run_ids = {row["run_id"] for row in rows}
    if len(run_ids) != 1:
        raise EvidenceError(f"{path}: event stream mixes run identities")
    if expected_run_id is not None and run_ids != {expected_run_id}:
        raise EvidenceError(f"{path}: event run identity does not match manifest")
    if rows[0]["type"] != "run_start" or rows[-1]["type"] != "run_end":
        raise EvidenceError(
            f"{path}: event stream requires run_start and run_end delimiters"
        )
    if PROHIBITED_CONTROL_KEYS & _all_keys(rows):
        raise EvidenceError(f"{path}: privileged scoring data crossed into events")


def _validate_recorded_provider(
    rows: list[dict[str, Any]], path: Path, expected_run_id: str
) -> None:
    _validate_rows(
        rows,
        _validator("airhockey.recorded_provider.v1.schema.json"),
        path,
    )
    _require_contiguous(rows, "seq", path)
    if PROHIBITED_CONTROL_KEYS & _all_keys(rows):
        raise EvidenceError(
            f"{path}: privileged scoring data crossed into provider records"
        )
    for row in rows:
        if row["run_id"] != expected_run_id:
            raise EvidenceError(f"{path}: provider record mixes run identities")
        if row["response_sha256"] != semantic_sha256(row["action"]):
            raise EvidenceError(f"{path}: provider response digest mismatch")


def _artefact_path(run_dir: Path, name: str) -> Path:
    if Path(name).name != name:
        raise EvidenceError(f"unsafe artefact name in manifest: {name}")
    path = run_dir / name
    if not path.is_file():
        raise EvidenceError(f"missing run artefact: {path}")
    return path


def validate_raw_bundle(run_dir: Path) -> dict[str, Any]:
    run_dir = run_dir.resolve()
    if not (run_dir / RUN_MARKER).is_file():
        raise EvidenceError(f"run directory is not marked: {run_dir}")
    manifest_path = _artefact_path(run_dir, "manifest.json")
    manifest = read_json(manifest_path)
    errors = sorted(
        _validator("airhockey.run_manifest.v1.schema.json").iter_errors(manifest),
        key=lambda error: list(error.path),
    )
    if errors:
        location = "/".join(str(item) for item in errors[0].path)
        raise EvidenceError(
            f"{manifest_path}:{location or '<root>'}: {errors[0].message}"
        )
    if manifest["run_id"] != run_dir.name:
        raise EvidenceError("manifest run_id must equal its directory name")
    if manifest["capture_status"] == "synthetic_ci" and manifest["paper_eligible"]:
        raise EvidenceError("synthetic evidence cannot be paper eligible")
    if set(manifest["derived_artefacts"]) != DERIVED_ARTEFACTS:
        raise EvidenceError("manifest derived artefact contract is incomplete")

    raw_paths: dict[str, Path] = {}
    for name, record in manifest["raw_artefacts"].items():
        path = _artefact_path(run_dir, name)
        if file_sha256(path) != record["sha256"]:
            raise EvidenceError(f"raw artefact hash mismatch: {path}")
        raw_paths[name] = path

    events = read_jsonl(raw_paths["events.jsonl"])
    replay_events = read_jsonl(raw_paths["replay-events.jsonl"])
    trajectory = read_jsonl(raw_paths["task-trajectory.jsonl"])
    replay_trajectory = read_jsonl(raw_paths["replay-task-trajectory.jsonl"])
    provider = read_jsonl(raw_paths["recorded-provider.jsonl"])
    _validate_events(events, raw_paths["events.jsonl"], manifest["run_id"])
    _validate_events(replay_events, raw_paths["replay-events.jsonl"])
    _validate_trajectory(
        trajectory, raw_paths["task-trajectory.jsonl"], manifest["run_id"]
    )
    replay_run_id = replay_trajectory[0].get("run_id")
    if not isinstance(replay_run_id, str) or replay_run_id == manifest["run_id"]:
        raise EvidenceError("replay trajectory requires a distinct run identity")
    _validate_trajectory(
        replay_trajectory,
        raw_paths["replay-task-trajectory.jsonl"],
        replay_run_id,
    )
    _validate_recorded_provider(
        provider, raw_paths["recorded-provider.jsonl"], manifest["run_id"]
    )
    if (
        manifest["pairing"]["provider_response_sha256"]
        != provider[0]["response_sha256"]
    ):
        raise EvidenceError("manifest/provider response pairing hash mismatch")
    if (
        manifest["pairing"]["delay_schedule_sha256"]
        != manifest["delay_schedule"]["sha256"]
    ):
        raise EvidenceError("manifest delay schedule pairing hash mismatch")
    if manifest["pairing"]["seed"] != manifest["seed"]:
        raise EvidenceError("manifest pairing seed mismatch")
    return {
        "manifest": manifest,
        "events": events,
        "replay_events": replay_events,
        "trajectory": trajectory,
        "replay_trajectory": replay_trajectory,
        "provider": provider,
    }


def integrity_summary(
    events: list[dict[str, Any]], expected: dict[str, Any]
) -> dict[str, Any]:
    submissions: dict[str, dict[str, Any]] = {}
    terminal: defaultdict[str, list[dict[str, Any]]] = defaultdict(list)
    revocations: set[str] = set()
    accepted_dispatches: defaultdict[str, list[dict[str, Any]]] = defaultdict(list)
    for row in events:
        data = row["data"]
        if row["type"] == "vla_submit" and "generation" in data:
            submissions[str(data["job_id"])] = data
        elif row["type"] == "vla_result" and "generation" in data:
            terminal[str(data["job_id"])].append(data)
        elif row["type"] == "async_authority_revoked":
            revocations.add(str(data.get("job_id", "")))
        elif row["type"] == "cap_call_end" and data.get("status") == "accepted":
            accepted_dispatches[str(data.get("job_id", ""))].append(data)

    terminal_rows = [row for rows in terminal.values() for row in rows]
    obsolete_results = [
        row
        for row in terminal_rows
        if row.get("decision") == "accepted"
        and row.get("captured_context_id") != row.get("current_context_id")
    ]
    valid_current_rejections = [
        row
        for row in terminal_rows
        if row.get("decision") == "rejected"
        and row.get("captured_context_id") == row.get("current_context_id")
    ]
    obsolete_dispatches = [
        row
        for rows in accepted_dispatches.values()
        for row in rows
        if row.get("obsolete") is True
    ]
    duplicate_commits = sum(max(0, len(rows) - 1) for rows in terminal.values())
    duplicate_dispatches = sum(
        max(0, len(rows) - 1) for rows in accepted_dispatches.values()
    )
    missing_terminal = sorted(
        set(submissions).difference(terminal).difference(revocations)
    )
    decisions = [row for row in terminal_rows if "decision" in row]
    observed_decision = decisions[-1]["decision"] if decisions else ""
    observed_reason = decisions[-1].get("reason", "") if decisions else ""
    reason_agreement = (
        observed_decision == expected["terminal_decision"]
        and observed_reason == expected["reason"]
        and len(obsolete_dispatches) == expected["obsolete_dispatches"]
    )
    return {
        "submitted_invocations": len(submissions),
        "terminal_decisions": len(decisions),
        "obsolete_results_committed": len(obsolete_results),
        "obsolete_action_chunks_dispatched": len(obsolete_dispatches),
        "valid_current_results_rejected": len(valid_current_rejections),
        "duplicate_commits": duplicate_commits,
        "duplicate_dispatches": duplicate_dispatches,
        "invocations_without_terminal_state": len(missing_terminal),
        "missing_terminal_job_ids": missing_terminal,
        "reason_code_agreement": reason_agreement,
        "observed_terminal_decision": observed_decision,
        "observed_reason": observed_reason,
    }


def _distance(lhs: list[float], rhs: list[float]) -> float:
    return math.hypot(lhs[0] - rhs[0], lhs[1] - rhs[1])


def _same_target(lhs: list[float], rhs: list[float]) -> bool:
    return _distance(lhs, rhs) <= 1.0e-12


def obsolete_target_motion(
    trajectory: list[dict[str, Any]], obsolete_target: list[float]
) -> dict[str, Any]:
    initial_context = trajectory[0]["public"]["defence_context_id"]
    change_index = next(
        (
            index
            for index, row in enumerate(trajectory)
            if row["public"]["defence_context_id"] != initial_context
        ),
        None,
    )
    if change_index is None:
        raise EvidenceError(
            "trajectory has no context change for obsolete-target analysis"
        )
    projected_motion = 0.0
    total_motion = 0.0
    motion_steps = 0
    command_steps = 0
    for index in range(change_index + 1, len(trajectory)):
        previous = trajectory[index - 1]["public"]["mallet_position"]
        current = trajectory[index]["public"]["mallet_position"]
        delta = [current[0] - previous[0], current[1] - previous[1]]
        travelled = math.hypot(delta[0], delta[1])
        total_motion += travelled
        direction = [obsolete_target[0] - previous[0], obsolete_target[1] - previous[1]]
        norm = math.hypot(direction[0], direction[1])
        towards = (
            0.0
            if norm <= 1.0e-12
            else max(0.0, (delta[0] * direction[0] + delta[1] * direction[1]) / norm)
        )
        projected_motion += towards
        if towards > 1.0e-12:
            motion_steps += 1
        if _same_target(trajectory[index]["public"]["applied_target"], obsolete_target):
            command_steps += 1
    start = trajectory[change_index]["public"]["mallet_position"]
    final = trajectory[-1]["public"]["mallet_position"]
    return {
        "context_change_observation_step": trajectory[change_index]["observation_step"],
        "obsolete_target": obsolete_target,
        "command_steps_towards_obsolete_target": command_steps,
        "motion_steps_towards_obsolete_target": motion_steps,
        "projected_motion_towards_obsolete_target": projected_motion,
        "net_distance_reduction_to_obsolete_target": max(
            0.0, _distance(start, obsolete_target) - _distance(final, obsolete_target)
        ),
        "total_mallet_motion_after_context_change": total_motion,
        "final_outcome": trajectory[-1]["privileged"]["outcome"],
    }


def timing_summary(events: list[dict[str, Any]]) -> dict[str, Any]:
    tick_ms = [
        float(row["data"]["tick_ms"])
        for row in events
        if row["type"] == "tick_end" and "tick_ms" in row["data"]
    ]
    provider_ms: list[float] = []
    submitted: dict[str, int] = {}
    for row in events:
        data = row["data"]
        if row["type"] == "vla_submit" and "generation" in data:
            submitted[str(data["job_id"])] = int(data["submitted_at_ns"])
        elif row["type"] == "vla_result" and "generation" in data:
            job = str(data["job_id"])
            if job in submitted and "completed_at_ns" in data:
                provider_ms.append(
                    (int(data["completed_at_ns"]) - submitted[job]) / 1e6
                )
    return {
        "tick_ms": _distribution(tick_ms),
        "provider_latency_ms": _distribution(provider_ms),
    }


def _quantile(values: list[float], probability: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    position = probability * (len(ordered) - 1)
    lower = math.floor(position)
    upper = math.ceil(position)
    if lower == upper:
        return ordered[lower]
    weight = position - lower
    return ordered[lower] * (1.0 - weight) + ordered[upper] * weight


def _distribution(values: list[float]) -> dict[str, Any]:
    if not values:
        return {"count": 0, "median": None, "p95": None, "p99": None, "maximum": None}
    return {
        "count": len(values),
        "median": _quantile(values, 0.5),
        "p95": _quantile(values, 0.95),
        "p99": _quantile(values, 0.99),
        "maximum": max(values),
    }


def _binomial_cdf(successes: int, total: int, probability: float) -> float:
    return sum(
        math.comb(total, value)
        * probability**value
        * (1.0 - probability) ** (total - value)
        for value in range(successes + 1)
    )


def _binomial_upper_tail(successes: int, total: int, probability: float) -> float:
    return sum(
        math.comb(total, value)
        * probability**value
        * (1.0 - probability) ** (total - value)
        for value in range(successes, total + 1)
    )


def clopper_pearson(
    successes: int, total: int, confidence: float = 0.95
) -> dict[str, Any]:
    if total <= 0 or successes < 0 or successes > total or not 0.0 < confidence < 1.0:
        raise EvidenceError("invalid binomial interval inputs")
    tail = (1.0 - confidence) / 2.0
    if successes == 0:
        lower = 0.0
    else:
        low, high = 0.0, 1.0
        for _ in range(80):
            middle = (low + high) / 2.0
            if _binomial_upper_tail(successes, total, middle) < tail:
                low = middle
            else:
                high = middle
        lower = (low + high) / 2.0
    if successes == total:
        upper = 1.0
    else:
        low, high = 0.0, 1.0
        for _ in range(80):
            middle = (low + high) / 2.0
            if _binomial_cdf(successes, total, middle) > tail:
                low = middle
            else:
                high = middle
        upper = (low + high) / 2.0
    return {
        "method": "clopper_pearson_exact",
        "confidence": confidence,
        "successes": successes,
        "total": total,
        "estimate": successes / total,
        "lower": lower,
        "upper": upper,
    }


def paired_bootstrap(
    differences: list[float],
    confidence: float = 0.95,
    samples: int = 10000,
    seed: int = 6303,
) -> dict[str, Any]:
    if not differences or samples < 100 or not 0.0 < confidence < 1.0:
        raise EvidenceError("invalid paired bootstrap inputs")
    generator = random.Random(seed)
    size = len(differences)
    means = [
        sum(differences[generator.randrange(size)] for _ in range(size)) / size
        for _ in range(samples)
    ]
    tail = (1.0 - confidence) / 2.0
    return {
        "method": "paired_percentile_bootstrap",
        "confidence": confidence,
        "samples": samples,
        "seed": seed,
        "pairs": size,
        "estimate": sum(differences) / size,
        "lower": _quantile(means, tail),
        "upper": _quantile(means, 1.0 - tail),
    }


def event_projection(events: list[dict[str, Any]]) -> list[dict[str, Any]]:
    projection: list[dict[str, Any]] = []
    for row in events:
        data = row["data"]
        if row["type"] == "bb_write" and data.get("key") in {
            "active-branch",
            "air-hockey-context-id",
        }:
            projection.append(
                {"type": row["type"], "key": data["key"], "preview": data["preview"]}
            )
        elif (
            row["type"] in {"vla_submit", "vla_poll", "vla_result"}
            and "generation" in data
        ):
            projection.append(
                {
                    "type": row["type"],
                    "job_id": str(data["job_id"]),
                    "generation": data["generation"],
                    "status": data.get("status", ""),
                    "decision": data.get("decision", ""),
                    "reason": data.get("reason", ""),
                    "captured_context_id": data.get("captured_context_id", ""),
                    "current_context_id": data.get("current_context_id", ""),
                }
            )
        elif row["type"] == "cap_call_end":
            projection.append(
                {
                    "type": row["type"],
                    "job_id": str(data.get("job_id", "")),
                    "status": data.get("status", ""),
                    "obsolete": data.get("obsolete", False),
                    "action": data.get("action"),
                }
            )
    return projection


def trajectory_projection(rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    return [
        {
            "observation_step": row["observation_step"],
            "context_id": row["public"]["defence_context_id"],
            "mallet_position": row["public"]["mallet_position"],
            "authorised_target": row["public"]["authorised_target"],
            "applied_target": row["public"]["applied_target"],
            "outcome": row["privileged"]["outcome"],
        }
        for row in rows
    ]


def replay_report(bundle: dict[str, Any]) -> dict[str, Any]:
    provider = bundle["provider"]
    request_hashes = {row["request_sha256"] for row in provider}
    if len(request_hashes) != len(provider):
        raise EvidenceError("recorded provider contains duplicate request identities")
    original_events = event_projection(bundle["events"])
    replayed_events = event_projection(bundle["replay_events"])
    original_task = trajectory_projection(bundle["trajectory"])
    replayed_task = trajectory_projection(bundle["replay_trajectory"])
    return {
        "schema_version": "airhockey.replay_report.v1",
        "recorded_provider_responses": len(provider),
        "live_provider_used": False,
        "event_projection_match": original_events == replayed_events,
        "task_projection_match": original_task == replayed_task,
        "matched": original_events == replayed_events
        and original_task == replayed_task,
        "original_event_projection_sha256": semantic_sha256(original_events),
        "replay_event_projection_sha256": semantic_sha256(replayed_events),
        "original_task_projection_sha256": semantic_sha256(original_task),
        "replay_task_projection_sha256": semantic_sha256(replayed_task),
    }


def build_overlay(
    events: list[dict[str, Any]], trajectory: list[dict[str, Any]]
) -> list[dict[str, Any]]:
    by_tick: defaultdict[int, list[dict[str, Any]]] = defaultdict(list)
    for row in events:
        if "tick" in row:
            by_tick[int(row["tick"])].append(row)
    state: dict[str, Any] = {
        "active_branch": "fallback",
        "request_id": None,
        "generation": None,
        "provider_state": "idle",
        "decision": "none",
        "reason": "",
    }
    frames: list[dict[str, Any]] = []
    for trajectory_row in trajectory:
        tick = int(trajectory_row["observation_step"]) + 1
        for event in by_tick[tick]:
            data = event["data"]
            if event["type"] == "bb_write" and data.get("key") == "active-branch":
                state["active_branch"] = str(data.get("preview", "fallback"))
            elif event["type"] == "vla_submit" and "generation" in data:
                state.update(
                    request_id=str(data["job_id"]),
                    generation=int(data["generation"]),
                    provider_state="submitted",
                    decision="none",
                    reason="",
                )
            elif event["type"] == "vla_poll" and "generation" in data:
                status = str(data.get("status", "running"))
                state["provider_state"] = "running" if status == "running" else status
            elif event["type"] == "vla_result" and "decision" in data:
                decision = str(data["decision"])
                state.update(
                    provider_state=decision,
                    decision=decision,
                    reason=str(data.get("reason", "")),
                )
            elif event["type"] == "async_authority_revoked":
                state.update(
                    provider_state="revoked",
                    decision="rejected",
                    reason=str(data.get("reason", "branch_revoked")),
                )
        public = trajectory_row["public"]
        frames.append(
            {
                "schema_version": "airhockey.overlay_frame.v1",
                "run_id": trajectory_row["run_id"],
                "observation_step": trajectory_row["observation_step"],
                "monotonic_ns": trajectory_row["monotonic_ns"],
                "active_branch": state["active_branch"],
                "request_id": state["request_id"],
                "generation": state["generation"],
                "context_id": public["defence_context_id"],
                "provider_state": state["provider_state"],
                "decision": state["decision"],
                "reason": state["reason"],
                "authorised_target": public["authorised_target"],
                "mallet_position": public["mallet_position"],
            }
        )
    validator = _validator("airhockey.overlay_frame.v1.schema.json")
    for frame in frames:
        validator.validate(frame)
    return frames


def render_overlay_svg(frames: list[dict[str, Any]], path: Path) -> None:
    width = 1120
    row_height = 42
    height = 64 + row_height * len(frames)
    rows: list[str] = []
    for index, frame in enumerate(frames):
        y = 54 + index * row_height
        target = frame["authorised_target"]
        target_text = (
            "none" if target is None else f"[{target[0]:.3f}, {target[1]:.3f}]"
        )
        text = (
            f"step {frame['observation_step']:02d}  branch={frame['active_branch']}  "
            f"job={frame['request_id'] or '-'}  gen={frame['generation'] or '-'}  "
            f"context={frame['context_id']}  provider={frame['provider_state']}  "
            f"decision={frame['decision']} {frame['reason']}  target={target_text}"
        )
        colour = (
            "#214d35"
            if frame["decision"] == "accepted"
            else ("#5a2831" if frame["decision"] == "rejected" else "#243247")
        )
        rows.append(
            f'<rect x="16" y="{y - 24}" width="1088" height="34" rx="5" fill="{colour}"/>'
            f'<text x="28" y="{y - 2}" fill="#f1f5f9" font-size="13" '
            f'font-family="ui-monospace, SFMono-Regular, Menlo, monospace">{html.escape(text)}</text>'
        )
    svg = (
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
        f'viewBox="0 0 {width} {height}">'
        '<rect width="100%" height="100%" fill="#101827"/>'
        '<text x="16" y="30" fill="#e2e8f0" font-size="18" '
        'font-family="system-ui, sans-serif">Air-hockey authority overlay timeline</text>'
        + "".join(rows)
        + "</svg>\n"
    )
    path.write_text(svg, encoding="utf-8")


def _trace_report(path: Path) -> dict[str, Any]:
    import sys

    tools_path = str(REPOSITORY_ROOT / "tools")
    if tools_path not in sys.path:
        sys.path.insert(0, tools_path)
    from trace_validator import load_config, validate_trace

    report = validate_trace(path, load_config(None, "strict_runtime"))
    return report.to_dict()


def analyse_run(run_dir: Path) -> dict[str, Any]:
    bundle = validate_raw_bundle(run_dir)
    manifest = bundle["manifest"]
    provider_action = list(bundle["provider"][0]["action"]["values"])
    integrity = integrity_summary(bundle["events"], manifest["expected_integrity"])
    motion = obsolete_target_motion(bundle["trajectory"], provider_action)
    replay = replay_report(bundle)
    if not replay["matched"]:
        raise EvidenceError(f"recorded-provider replay diverged: {run_dir}")
    if not integrity["reason_code_agreement"]:
        raise EvidenceError(f"trial oracle disagrees with runtime evidence: {run_dir}")

    event_validation = {
        "schema_version": "airhockey.event_validation.v1",
        "status": "passed",
        "streams": {
            "events.jsonl": {"records": len(bundle["events"]), "status": "passed"},
            "replay-events.jsonl": {
                "records": len(bundle["replay_events"]),
                "status": "passed",
            },
        },
    }
    original_trace = _trace_report(run_dir / "events.jsonl")
    replay_trace = _trace_report(run_dir / "replay-events.jsonl")
    trace_validation = {
        "schema_version": "airhockey.trace_validation.v1",
        "status": "passed"
        if original_trace["passed"] and replay_trace["passed"]
        else "failed",
        "original": original_trace,
        "replay": replay_trace,
    }
    if trace_validation["status"] != "passed":
        raise EvidenceError(f"cross-event validation failed: {run_dir}")
    summary = {
        "schema_version": "airhockey.trial_summary.v1",
        "run_id": manifest["run_id"],
        "pair_id": manifest["pair_id"],
        "acceptance_policy": manifest["acceptance_policy"],
        "integrity": integrity,
        "timing": timing_summary(bundle["events"]),
        "obsolete_target_motion": motion,
        "task_outcome": motion["final_outcome"],
    }
    overlay = build_overlay(bundle["events"], bundle["trajectory"])
    write_json(run_dir / "event-validation.json", event_validation)
    write_json(run_dir / "trace-validation.json", trace_validation)
    write_json(run_dir / "trial-summary.json", summary)
    write_json(run_dir / "replay-report.json", replay)
    write_jsonl(run_dir / "overlay-timeline.jsonl", overlay)
    render_overlay_svg(overlay, run_dir / "overlay.svg")
    derived_hashes = {
        name: file_sha256(run_dir / name)
        for name in sorted(DERIVED_ARTEFACTS - {"bundle-validation.json"})
    }
    bundle_validation = {
        "schema_version": "airhockey.bundle_validation.v1",
        "status": "passed",
        "run_id": manifest["run_id"],
        "raw_artefacts_verified": sorted(manifest["raw_artefacts"]),
        "derived_artefact_sha256": derived_hashes,
        "public_privileged_boundary": "passed",
        "manifest_validation": "passed",
        "recorded_provider_replay": "passed",
    }
    write_json(run_dir / "bundle-validation.json", bundle_validation)
    return summary


def _pair_compatible(baseline: dict[str, Any], full: dict[str, Any]) -> None:
    if baseline["pair_id"] != full["pair_id"]:
        raise EvidenceError("paired runs have different pair identities")
    if {
        baseline["acceptance_policy"],
        full["acceptance_policy"],
    } != {"deadline_only", "invocation_scoped"}:
        raise EvidenceError(
            "each pair requires baseline and invocation-scoped policies"
        )
    for field in ("pairing", "provider", "shot", "delay_schedule", "seed"):
        if baseline[field] != full[field]:
            raise EvidenceError(f"paired runs differ in frozen field: {field}")


def campaign_summary(run_root: Path) -> dict[str, Any]:
    run_dirs = sorted(
        path.parent
        for path in run_root.glob(f"*/{RUN_MARKER}")
        if (path.parent / "manifest.json").is_file()
    )
    if not run_dirs:
        raise EvidenceError(f"no marked run bundles under {run_root}")
    summaries: dict[str, dict[str, Any]] = {}
    manifests: dict[str, dict[str, Any]] = {}
    grouped: defaultdict[str, list[str]] = defaultdict(list)
    for run_dir in run_dirs:
        summary = analyse_run(run_dir)
        manifest = read_json(run_dir / "manifest.json")
        summaries[manifest["run_id"]] = summary
        manifests[manifest["run_id"]] = manifest
        grouped[manifest["pair_id"]].append(manifest["run_id"])

    pairs: list[dict[str, Any]] = []
    for pair_id, run_ids in sorted(grouped.items()):
        if len(run_ids) != 2:
            raise EvidenceError(f"pair {pair_id} does not contain exactly two runs")
        ordered = sorted(
            run_ids, key=lambda run_id: manifests[run_id]["acceptance_policy"]
        )
        baseline_id = next(
            run_id
            for run_id in ordered
            if manifests[run_id]["acceptance_policy"] == "deadline_only"
        )
        full_id = next(run_id for run_id in ordered if run_id != baseline_id)
        _pair_compatible(manifests[baseline_id], manifests[full_id])
        baseline = summaries[baseline_id]
        full = summaries[full_id]
        baseline_motion = baseline["obsolete_target_motion"][
            "projected_motion_towards_obsolete_target"
        ]
        full_motion = full["obsolete_target_motion"][
            "projected_motion_towards_obsolete_target"
        ]
        pairs.append(
            {
                "pair_id": pair_id,
                "baseline_run_id": baseline_id,
                "full_run_id": full_id,
                "baseline_obsolete_dispatches": baseline["integrity"][
                    "obsolete_action_chunks_dispatched"
                ],
                "full_obsolete_dispatches": full["integrity"][
                    "obsolete_action_chunks_dispatched"
                ],
                "baseline_projected_motion": baseline_motion,
                "full_projected_motion": full_motion,
                "motion_difference_full_minus_baseline": full_motion - baseline_motion,
                "baseline_save": int(baseline["task_outcome"] == "save"),
                "full_save": int(full["task_outcome"] == "save"),
            }
        )

    baseline_failures = sum(pair["baseline_obsolete_dispatches"] > 0 for pair in pairs)
    full_failures = sum(pair["full_obsolete_dispatches"] > 0 for pair in pairs)
    motion_differences = [
        pair["motion_difference_full_minus_baseline"] for pair in pairs
    ]
    save_differences = [pair["full_save"] - pair["baseline_save"] for pair in pairs]
    table_rows = [
        {
            "metric": "obsolete dispatch pair rate",
            "baseline": baseline_failures / len(pairs),
            "full": full_failures / len(pairs),
            "difference": (full_failures - baseline_failures) / len(pairs),
        },
        {
            "metric": "projected motion towards obsolete target",
            "baseline": sum(pair["baseline_projected_motion"] for pair in pairs)
            / len(pairs),
            "full": sum(pair["full_projected_motion"] for pair in pairs) / len(pairs),
            "difference": sum(motion_differences) / len(pairs),
        },
        {
            "metric": "save rate",
            "baseline": sum(pair["baseline_save"] for pair in pairs) / len(pairs),
            "full": sum(pair["full_save"] for pair in pairs) / len(pairs),
            "difference": sum(save_differences) / len(pairs),
        },
    ]
    return {
        "schema_version": "airhockey.campaign_summary.v1",
        "run_count": len(run_dirs),
        "pair_count": len(pairs),
        "synthetic_only": all(
            manifest["capture_status"] == "synthetic_ci"
            for manifest in manifests.values()
        ),
        "pairs": pairs,
        "integrity_intervals": {
            "baseline_obsolete_dispatch": clopper_pearson(
                baseline_failures, len(pairs)
            ),
            "full_obsolete_dispatch": clopper_pearson(full_failures, len(pairs)),
        },
        "paired_intervals": {
            "obsolete_target_motion": paired_bootstrap(motion_differences),
            "save_rate": paired_bootstrap(save_differences),
        },
        "table_rows": table_rows,
        "plot_fields": {
            "pair_id": [pair["pair_id"] for pair in pairs],
            "baseline_projected_motion": [
                pair["baseline_projected_motion"] for pair in pairs
            ],
            "full_projected_motion": [pair["full_projected_motion"] for pair in pairs],
            "baseline_obsolete_dispatches": [
                pair["baseline_obsolete_dispatches"] for pair in pairs
            ],
            "full_obsolete_dispatches": [
                pair["full_obsolete_dispatches"] for pair in pairs
            ],
        },
        "raw_provenance_sha256": {
            run_id: semantic_sha256(manifest["raw_artefacts"])
            for run_id, manifest in sorted(manifests.items())
        },
    }


def validate_campaign_report(report: dict[str, Any]) -> None:
    errors = sorted(
        _validator("airhockey.campaign_summary.v1.schema.json").iter_errors(report),
        key=lambda error: list(error.path),
    )
    if errors:
        location = "/".join(str(item) for item in errors[0].path)
        raise EvidenceError(
            f"campaign report {location or '<root>'}: {errors[0].message}"
        )


def require_safe_component(value: str, field: str) -> str:
    if value in {".", ".."} or not SAFE_COMPONENT.fullmatch(value):
        raise EvidenceError(f"{field} must be a safe single path component")
    return value


def guarded_publish(
    staged_directory: Path, output_root: Path, run_id: str, force: bool
) -> Path:
    safe_run_id = require_safe_component(run_id, "run_id")
    root = output_root.resolve()
    if root == root.parent:
        raise EvidenceError("output root cannot be a filesystem root")
    root.mkdir(parents=True, exist_ok=True)
    destination = (root / safe_run_id).resolve()
    if destination.parent != root:
        raise EvidenceError("run destination escaped its output root")
    if not staged_directory.is_dir() or not (staged_directory / RUN_MARKER).is_file():
        raise EvidenceError("staged run is missing its evidence marker")
    if destination.exists():
        if not force:
            raise EvidenceError(f"run destination already exists: {destination}")
        if not destination.is_dir() or not (destination / RUN_MARKER).is_file():
            raise EvidenceError("refuse to replace an unmarked run directory")
        shutil.rmtree(destination)
    shutil.move(str(staged_directory), str(destination))
    return destination
