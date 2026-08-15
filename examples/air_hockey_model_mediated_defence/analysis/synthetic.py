"""Generate deterministic synthetic WP3 evidence without MuJoCo or Marvin."""

from __future__ import annotations

import subprocess
import tempfile
from pathlib import Path
from typing import Any

from .evidence import (
    DERIVED_ARTEFACTS,
    RUN_MARKER,
    RecordedProviderReplay,
    file_sha256,
    guarded_publish,
    semantic_sha256,
    write_json,
    write_jsonl,
)

PAIR_MOTION_SCALE = (1.0, 0.75, 0.5, 1.0)
BASELINE_OUTCOMES = ("concession", "concession", "save", "concession")
FULL_OUTCOMES = ("save", "save", "save", "save")
OBSOLETE_TARGET = [0.6, 0.0]


def _git_provenance() -> dict[str, Any]:
    repository_root = Path(__file__).resolve().parents[3]
    commit = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=repository_root,
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()
    dirty = bool(
        subprocess.run(
            ["git", "status", "--porcelain", "--untracked-files=no"],
            cwd=repository_root,
            check=True,
            capture_output=True,
            text=True,
        ).stdout.strip()
    )
    return {"present": True, "commit": commit, "dirty": dirty}


def _event(
    run_id: str,
    seq: int,
    event_type: str,
    data: dict[str, Any],
    tick: int | None = None,
    time_offset: int = 0,
) -> dict[str, Any]:
    row: dict[str, Any] = {
        "schema": "mbt.evt.v1",
        "contract_version": "1.0.0",
        "type": event_type,
        "run_id": run_id,
        "unix_ms": 1735689620000 + time_offset + seq,
        "seq": seq,
        "data": data,
    }
    if tick is not None:
        row["tick"] = tick
    return row


def _events(
    run_id: str,
    policy: str,
    provider_action: list[float],
    replay: bool,
) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    replay_offset = 10000 if replay else 0

    def add(event_type: str, data: dict[str, Any], tick: int | None = None) -> None:
        rows.append(
            _event(
                run_id,
                len(rows) + 1,
                event_type,
                data,
                tick,
                replay_offset,
            )
        )

    add(
        "run_start",
        {
            "git_sha": "synthetic",
            "host": {"name": "air-hockey-wp3", "version": "v1", "platform": "local"},
            "contract_version": "1.0.0",
            "contract_id": "runtime-contract-v1.0.0",
            "tick_hz": 50,
            "tree_hash": "fnv1a64:1111111111111111",
            "capabilities": {"reset": True, "air_hockey_action_dispatch": True},
        },
    )
    for tick in range(1, 7):
        observation_step = tick - 1
        context = (
            "episode-000001/track-0001"
            if observation_step < 2
            else "episode-000001/track-0002"
        )
        if tick < 4:
            branch = "model_wait"
        elif policy == "deadline_only":
            branch = "model_execute"
        else:
            branch = "fallback"
        add("tick_begin", {"root": 21}, tick)
        add(
            "bb_write",
            {"key": "air-hockey-context-id", "preview": context},
            tick,
        )
        add("bb_write", {"key": "active-branch", "preview": branch}, tick)
        if tick == 1:
            add(
                "vla_submit",
                {
                    "job_id": "job-1",
                    "generation": 1,
                    "requesting_node_id": 17,
                    "authority_node_id": 17,
                    "job_key": "defence-job",
                    "action_key": "defence-action",
                    "meta_key": "defence-meta",
                    "context_key": "air-hockey-context-id",
                    "captured_context_id": "episode-000001/track-0001",
                    "submitted_at_ns": 100000000000,
                    "deadline_at_ns": 100120000000,
                    "acceptance_policy": policy,
                    "authority_state": "active",
                    "status": "submitted",
                },
                tick,
            )
        elif tick == 2:
            add(
                "vla_poll",
                {
                    "job_id": "job-1",
                    "generation": 1,
                    "requesting_node_id": 17,
                    "authority_node_id": 9,
                    "job_key": "defence-job",
                    "captured_context_id": "episode-000001/track-0001",
                    "acceptance_policy": policy,
                    "authority_state": "active",
                    "status": "running",
                },
                tick,
            )
        elif tick == 4:
            accepted = policy == "deadline_only"
            add(
                "vla_result",
                {
                    "job_id": "job-1",
                    "generation": 1,
                    "requesting_node_id": 17,
                    "authority_node_id": 9,
                    "job_key": "defence-job",
                    "captured_context_id": "episode-000001/track-0001",
                    "current_context_id": "episode-000001/track-0002",
                    "acceptance_policy": policy,
                    "authority_state": "accepted" if accepted else "rejected",
                    "status": "ok",
                    "decision": "accepted" if accepted else "rejected",
                    "reason": "" if accepted else "context_changed",
                    "digest": semantic_sha256(provider_action),
                    "completed_at_ns": 100060000000,
                },
                tick,
            )
            if accepted:
                add(
                    "cap_call_start",
                    {
                        "request_id": "airhockey-job-1",
                        "capability": "cap.vla.action_chunk.v1",
                        "operation": "dispatch",
                        "job_id": "job-1",
                        "generation": 1,
                        "action": provider_action,
                    },
                    tick,
                )
                add(
                    "cap_call_end",
                    {
                        "request_id": "airhockey-job-1",
                        "capability": "cap.vla.action_chunk.v1",
                        "operation": "dispatch",
                        "job_id": "job-1",
                        "generation": 1,
                        "status": "accepted",
                        "host_reached": True,
                        "decision": "accepted",
                        "reason": "",
                        "obsolete": True,
                        "action": provider_action,
                    },
                    tick,
                )
        add(
            "tick_end",
            {"root_status": "running" if tick < 6 else "success", "tick_ms": 0.2},
            tick,
        )
    add("run_end", {"status": "success", "episodes": 1})
    return rows


def _trajectory(
    run_id: str,
    policy: str,
    motion_scale: float,
    outcome: str,
) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for step in range(6):
        context = (
            "episode-000001/track-0001" if step < 2 else "episode-000001/track-0002"
        )
        accepted = policy == "deadline_only" and step >= 3
        progress = (
            (step - 2) * 0.2 * motion_scale
            if policy == "deadline_only" and step >= 3
            else 0.0
        )
        mallet = [min(0.6, progress), 0.0]
        applied = OBSOLETE_TARGET if accepted else mallet
        terminal = step == 5
        rows.append(
            {
                "schema_version": "airhockey.task_trajectory.v1",
                "run_id": run_id,
                "seq": step + 1,
                "observation_step": step,
                "monotonic_ns": 100000000000 + step * 20000000,
                "public": {
                    "episode_id": "episode-000001",
                    "defence_context_id": context,
                    "puck_visible": step != 1,
                    "action_locked": False,
                    "episode_active": not terminal,
                    "terminated": terminal,
                    "truncated": False,
                    "mallet_position": mallet,
                    "visible_puck_position": [0.0, 0.0] if step == 1 else [0.2, -0.1],
                    "requested_target": applied,
                    "authorised_target": OBSOLETE_TARGET if accepted else None,
                    "applied_target": applied,
                },
                "privileged": {
                    "true_puck_position": [0.2 - step * 0.02, -0.1],
                    "true_puck_velocity": [-1.0, 0.0],
                    "target_goal": [-1.0, 0.0],
                    "privileged_intercept_target": OBSOLETE_TARGET,
                    "contact": terminal and outcome == "save",
                    "outcome": outcome if terminal else "pending",
                },
            }
        )
    return rows


def _recorded_provider(run_id: str, request_sha256: str) -> list[dict[str, Any]]:
    action = {
        "type": "continuous",
        "frame": "airhockey.normalised_mallet_target.v1",
        "values": OBSOLETE_TARGET,
        "dt_ms": 20,
    }
    return [
        {
            "schema_version": "airhockey.recorded_provider.v1",
            "run_id": run_id,
            "seq": 1,
            "request_sha256": request_sha256,
            "response_sha256": semantic_sha256(action),
            "job_id": "job-1",
            "generation": 1,
            "captured_context_id": "episode-000001/track-0001",
            "source_observation_step": 0,
            "action": action,
        }
    ]


def _manifest(
    run_dir: Path,
    run_id: str,
    pair_id: str,
    policy: str,
    pair_index: int,
    provider_response_sha256: str,
) -> dict[str, Any]:
    example_root = Path(__file__).resolve().parents[1]
    tree_name = (
        "bt_deadline_only.lisp"
        if policy == "deadline_only"
        else "bt_invocation_scoped.lisp"
    )
    tree_path = example_root / "lisp" / tree_name
    seed = 7000 + pair_index
    delay_schedule = {
        "kind": "fixed",
        "delay_ms": 60,
        "fault": "blackout_reacquisition",
    }
    delay_schedule_sha256 = semantic_sha256(delay_schedule)
    delay_schedule["sha256"] = delay_schedule_sha256
    raw_names = (
        "events.jsonl",
        "task-trajectory.jsonl",
        "recorded-provider.jsonl",
        "replay-events.jsonl",
        "replay-task-trajectory.jsonl",
    )
    return {
        "schema_version": "airhockey.run_manifest.v1",
        "run_id": run_id,
        "pair_id": pair_id,
        "capture_status": "synthetic_ci",
        "paper_eligible": False,
        "acceptance_policy": policy,
        "repositories": {
            "muesli_bt": _git_provenance(),
            "air_hockey": {"present": False, "commit": None, "dirty": None},
        },
        "container": {
            "image": "local/synthetic-wp3",
            "digest": semantic_sha256("local/synthetic-wp3"),
        },
        "simulator": {
            "backend": "synthetic_wp3",
            "version": "v1",
            "control_period_ms": 20,
        },
        "provider": {
            "kind": "deterministic",
            "identity": "airhockey-recorded-synthetic",
            "version": "v1",
            "configuration_sha256": semantic_sha256(
                {"target": OBSOLETE_TARGET, "delay_ms": 60}
            ),
            "checkpoint_sha256": None,
        },
        "shot": {
            "split": "synthetic",
            "manifest_sha256": semantic_sha256("synthetic-campaign-v1"),
            "manifest_entry_sha256": semantic_sha256({"pair_id": pair_id}),
        },
        "behaviour_tree": {
            "path": f"lisp/{tree_name}",
            "sha256": file_sha256(tree_path),
        },
        "context_rule": {
            "version": "air_hockey_muesli_contract.v1",
            "reacquisition_starts_new_context": True,
            "source_age_limit_steps": 6,
        },
        "delay_schedule": delay_schedule,
        "seed": seed,
        "validators": {
            "event": "mbt.evt.v1",
            "trace": "strict_runtime.v1",
            "trajectory": "airhockey.task_trajectory.v1",
            "manifest": "airhockey.run_manifest.v1",
            "analysis": "airhockey.analysis.v1",
        },
        "pairing": {
            "provider_response_sha256": provider_response_sha256,
            "delay_schedule_sha256": delay_schedule_sha256,
            "seed": seed,
        },
        "expected_integrity": {
            "terminal_decision": "accepted"
            if policy == "deadline_only"
            else "rejected",
            "reason": "" if policy == "deadline_only" else "context_changed",
            "obsolete_dispatches": 1 if policy == "deadline_only" else 0,
        },
        "raw_artefacts": {
            name: {"sha256": file_sha256(run_dir / name)} for name in raw_names
        },
        "derived_artefacts": sorted(DERIVED_ARTEFACTS),
    }


def generate_run(
    output_root: Path,
    pair_index: int,
    policy: str,
    force: bool,
) -> Path:
    pair_id = f"synthetic-pair-{pair_index + 1:02d}"
    policy_name = "deadline-only" if policy == "deadline_only" else "invocation-scoped"
    run_id = f"{pair_id}-{policy_name}"
    request = {
        "pair_id": pair_id,
        "captured_context_id": "episode-000001/track-0001",
        "source_observation_step": 0,
        "deadline_ms": 120,
        "public_observation_sha256": semantic_sha256([0.0] * 19),
    }
    request_sha256 = semantic_sha256(request)
    records = _recorded_provider(run_id, request_sha256)
    replay_provider = RecordedProviderReplay(records)
    replay_action = replay_provider.infer(request_sha256)
    provider_values = list(replay_action["values"])
    baseline = policy == "deadline_only"
    outcome = BASELINE_OUTCOMES[pair_index] if baseline else FULL_OUTCOMES[pair_index]

    output_root.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(
        prefix="air-hockey-wp3-stage-", dir=output_root
    ) as temp:
        staged = Path(temp) / run_id
        staged.mkdir()
        (staged / RUN_MARKER).write_text(
            "airhockey.run_manifest.v1\n", encoding="utf-8"
        )
        write_jsonl(
            staged / "events.jsonl", _events(run_id, policy, OBSOLETE_TARGET, False)
        )
        write_jsonl(
            staged / "task-trajectory.jsonl",
            _trajectory(run_id, policy, PAIR_MOTION_SCALE[pair_index], outcome),
        )
        write_jsonl(staged / "recorded-provider.jsonl", records)
        replay_run_id = f"{run_id}-replay"
        write_jsonl(
            staged / "replay-events.jsonl",
            _events(replay_run_id, policy, provider_values, True),
        )
        write_jsonl(
            staged / "replay-task-trajectory.jsonl",
            _trajectory(
                replay_run_id,
                policy,
                PAIR_MOTION_SCALE[pair_index],
                outcome,
            ),
        )
        manifest = _manifest(
            staged,
            run_id,
            pair_id,
            policy,
            pair_index,
            records[0]["response_sha256"],
        )
        write_json(staged / "manifest.json", manifest)
        return guarded_publish(staged, output_root, run_id, force)


def generate_campaign(output_root: Path, force: bool = False) -> list[Path]:
    runs: list[Path] = []
    for pair_index in range(len(PAIR_MOTION_SCALE)):
        for policy in ("deadline_only", "invocation_scoped"):
            runs.append(generate_run(output_root, pair_index, policy, force))
    return runs
