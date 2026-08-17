#!/usr/bin/env python3
"""Run and validate the frozen three-treatment WP8 paper campaign."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
from collections import defaultdict
from pathlib import Path
from typing import Any

import run_wp7 as wp7
from analysis.evidence import (
    DERIVED_ARTEFACTS,
    EvidenceError,
    RUN_MARKER,
    RecordedProviderReplay,
    analyse_run,
    file_sha256,
    guarded_publish,
    paired_bootstrap,
    read_json,
    read_jsonl,
    semantic_sha256,
    write_json,
    write_jsonl,
)
from run_wp6 import _timing_records, _timing_summary, _validate_public_boundary

EXAMPLE_ROOT = Path(__file__).resolve().parent
REPOSITORY_ROOT = EXAMPLE_ROOT.parents[1]
PROTOCOL_PATH = EXAMPLE_ROOT / "configs" / "wp8_recovery_protocol.json"
RUN_ROOT_MARKER = ".air-hockey-wp8-run"
RAW_NAMES = wp7.RAW_NAMES
TREATMENTS = (
    "deadline_only",
    "invocation_scoped_hold",
    "invocation_scoped_current_context_recovery",
)
SCENARIOS = {
    "deadline_only": "P7-deadline-only",
    "invocation_scoped_hold": "P7-invocation-scoped",
    "invocation_scoped_current_context_recovery": "P8-current-context-recovery",
}
SLUGS = {
    "deadline_only": "deadline-only",
    "invocation_scoped_hold": "invocation-scoped-hold",
    "invocation_scoped_current_context_recovery": (
        "invocation-scoped-current-context-recovery"
    ),
}


class GateG8RecoveryError(EvidenceError):
    """A WP8 campaign invariant failed before evidence promotion."""


def _sha256(path: Path) -> str:
    return file_sha256(path).removeprefix("sha256:")


def _load_wp8() -> dict[str, Any]:
    protocol = read_json(PROTOCOL_PATH)
    parent_path = EXAMPLE_ROOT / protocol["parent_wp7_protocol"]
    parent = read_json(parent_path)
    if _sha256(parent_path) != protocol["parent_wp7_protocol_sha256"]:
        raise GateG8RecoveryError("the frozen WP7 parent protocol changed")
    for treatment in protocol["treatments"].values():
        tree = EXAMPLE_ROOT / treatment["tree"]
        if _sha256(tree) != treatment["tree_sha256"]:
            raise GateG8RecoveryError(f"a frozen WP8 Behaviour Tree changed: {tree}")
    if protocol["paper_split"] != parent["paper_split"]:
        raise GateG8RecoveryError("WP8 changed the frozen paper split")

    merged = dict(parent)
    merged["schema_version"] = protocol["schema_version"]
    merged["status"] = protocol["status"]
    merged["control_period_ms"] = protocol["control_period_ms"]
    merged["deadline_ms"] = protocol["deadline_ms"]
    merged["paired_trial"] = {
        **parent["paired_trial"],
        **protocol["paired_trial"],
        "policies": list(TREATMENTS),
        "expected": {
            "deadline_only": {
                "terminal_decision": "accepted",
                "reason": "",
                "obsolete_dispatches": 1,
            },
            "invocation_scoped_hold": {
                "terminal_decision": "rejected",
                "reason": "context_changed",
                "obsolete_dispatches": 0,
            },
            "invocation_scoped_current_context_recovery": {
                "terminal_decision": "rejected",
                "reason": "context_changed",
                "obsolete_dispatches": 0,
            },
        },
    }
    merged["behaviour_trees"] = {
        treatment: {
            "path": definition["tree"],
            "sha256": definition["tree_sha256"],
        }
        for treatment, definition in protocol["treatments"].items()
    }
    merged["campaign"] = {
        **parent["campaign"],
        **protocol["campaign"],
        "total_runs": protocol["campaign"]["treatment_runs"],
    }
    merged["gate"] = protocol["gate"]
    merged["wp8_protocol"] = protocol
    return merged


def _prepare_output(output: Path) -> Path:
    output = output.resolve()
    if output == output.parent:
        raise GateG8RecoveryError("WP8 output cannot be a filesystem root")
    if output.exists() and any(output.iterdir()):
        raise GateG8RecoveryError(f"refuse to replace non-empty WP8 output: {output}")
    output.mkdir(parents=True, exist_ok=True)
    (output / RUN_ROOT_MARKER).write_text("airhockey.wp8.run.v1\n", encoding="utf-8")
    (output / "runs").mkdir()
    return output


def _marked_run_dirs(runs: Path) -> list[Path]:
    return sorted(
        path.parent
        for path in runs.glob(f"*/{RUN_MARKER}")
        if (path.parent / "manifest.json").is_file()
    )


def _reuse_baseline_runs(
    source: Path, output_runs: Path, protocol: dict[str, Any]
) -> dict[str, Any]:
    source = source.resolve()
    if not (source / RUN_ROOT_MARKER).is_file():
        raise GateG8RecoveryError(f"baseline source is not a marked WP8 run: {source}")
    source_protocol = source / "wp8-protocol.json"
    if not source_protocol.is_file() or _sha256(source_protocol) != _sha256(
        PROTOCOL_PATH
    ):
        raise GateG8RecoveryError("baseline source used a different WP8 protocol")

    copied: defaultdict[str, int] = defaultdict(int)
    source_revisions: defaultdict[str, set[str]] = defaultdict(set)
    for source_run in _marked_run_dirs(source / "runs"):
        manifest = read_json(source_run / "manifest.json")
        treatment = _treatment_from_run_id(manifest["run_id"])
        if treatment == "invocation_scoped_current_context_recovery":
            continue
        if manifest["expected_integrity"] != protocol["paired_trial"][
            "expected"
        ][treatment]:
            raise GateG8RecoveryError(
                f"baseline source oracle changed: {manifest['run_id']}"
            )
        destination = output_runs / source_run.name
        if destination.exists():
            raise GateG8RecoveryError(f"duplicate reused run: {destination.name}")
        destination.mkdir()
        shutil.copy2(source_run / RUN_MARKER, destination / RUN_MARKER)
        shutil.copy2(source_run / "manifest.json", destination / "manifest.json")
        for name in RAW_NAMES:
            shutil.copy2(source_run / name, destination / name)
        analyse_run(destination)
        copied[treatment] += 1
        source_revisions[treatment].add(
            manifest["repositories"]["muesli_bt"]["commit"]
        )

    expected = protocol["campaign"]["total_pairs"]
    for treatment in TREATMENTS[:2]:
        if copied[treatment] != expected:
            raise GateG8RecoveryError(
                f"baseline source contains {copied[treatment]} {treatment} runs; "
                f"expected {expected}"
            )
    measured_path = source / "wp8-measured.json"
    return {
        "mode": "recovery_only_with_reused_baselines",
        "source_root": str(source),
        "source_protocol_sha256": _sha256(source_protocol),
        "source_measured_sha256": (
            _sha256(measured_path) if measured_path.is_file() else None
        ),
        "copied_runs": dict(sorted(copied.items())),
        "source_revisions": {
            treatment: sorted(revisions)
            for treatment, revisions in sorted(source_revisions.items())
        },
    }


def _expected_predicates(treatment: str) -> set[str]:
    if treatment == "deadline_only":
        return {
            "p7_deadline_only_obsolete_dispatch",
            "p7_provider_mode_matches",
            "p7_task_episode_completed",
        }
    if treatment == "invocation_scoped_hold":
        return {
            "p7_invocation_scoped_context_rejection",
            "p7_provider_mode_matches",
            "p7_task_episode_completed",
        }
    return {
        "p8_current_context_recovery_target",
        "p8_zero_obsolete_dispatch",
        "p8_recovery_episode_completed",
    }


def _run_native_half(
    *,
    executable: Path,
    tree: Path,
    scenario: str,
    treatment: str,
    run_id: str,
    action: list[float],
    paper: dict[str, Any],
    delay_ms: int,
    action_lock_steps: int,
    generated: Any,
    events_path: Path,
    replay: bool,
) -> dict[str, Any]:
    backend = wp7.MujocoDirectLaunchHostBackend(
        shot_factory=lambda shot=generated.shot: shot
    )
    schemas = wp7.SchemaRegistry(REPOSITORY_ROOT / "schemas" / "air_hockey_host" / "v1")
    with tempfile.TemporaryDirectory(prefix="m8-") as directory:
        socket_path = Path(directory) / "host.sock"
        command = [
            str(executable),
            scenario,
            str(socket_path),
            str(tree),
            str(events_path),
            run_id,
            repr(action[0]),
            repr(action[1]),
            str(paper["blackout_start_step"]),
            str(paper["blackout_length_steps"]),
            str(paper["timeout_steps"]),
            str(action_lock_steps),
            str(delay_ms),
            "replay" if replay else "live",
        ]
        try:
            with wp7.UnixHostServer(socket_path, wp7.ProtocolProcessor(schemas, backend)):
                completed = subprocess.run(
                    command,
                    check=False,
                    capture_output=True,
                    text=True,
                    timeout=90,
                )
        except Exception:
            backend.shutdown()
            raise
    if completed.returncode != 0:
        backend.shutdown()
        raise GateG8RecoveryError(
            f"{run_id} exited with {completed.returncode}: "
            f"{completed.stderr.strip()}"
        )
    observed = wp7._observed_predicates(completed.stdout)
    if observed != _expected_predicates(treatment):
        backend.shutdown()
        raise GateG8RecoveryError(
            f"{run_id} predicate mismatch: {sorted(observed)}"
        )
    records = backend.evaluation_records()
    _validate_public_boundary(records)
    direct_replay = backend.direct_replay_report()
    backend.shutdown()
    if not direct_replay["passed"] or direct_replay["steps"] != len(records):
        raise GateG8RecoveryError(f"direct MuJoCo replay did not cover {run_id}")
    from analysis.evidence import _validate_events

    _validate_events(read_jsonl(events_path), events_path, run_id)
    return {
        "stdout": completed.stdout,
        "stderr": completed.stderr,
        "records": records,
        "direct_replay": direct_replay,
        "timing": _timing_records(completed.stdout),
    }


def _trajectory(
    run_id: str,
    records: list[dict[str, Any]],
    generated: Any,
    action: list[float],
    treatment: str,
    context_change_step: int,
) -> list[dict[str, Any]]:
    if not records or not (
        records[-1]["public_state"]["terminated"]
        or records[-1]["public_state"]["truncated"]
    ):
        raise GateG8RecoveryError(f"{run_id} did not reach a terminal record")
    rows: list[dict[str, Any]] = []
    for index, record in enumerate(records, start=1):
        state = record["public_state"]
        observation = state["observation"]
        final = index == len(records)
        authorised = (
            list(action)
            if treatment == "deadline_only"
            and int(record["observation_step"]) > context_change_step
            else None
        )
        rows.append(
            {
                "schema_version": "airhockey.task_trajectory.v1",
                "run_id": run_id,
                "seq": index,
                "observation_step": int(record["observation_step"]),
                "monotonic_ns": int(record["finished_monotonic_ns"]),
                "public": {
                    "episode_id": state["episode_id"],
                    "defence_context_id": state["defence_context_id"],
                    "puck_visible": bool(state["puck_visible"]),
                    "action_locked": bool(state["action_locked"]),
                    "episode_active": bool(state["episode_active"]),
                    "terminated": bool(state["terminated"]),
                    "truncated": bool(state["truncated"]),
                    "mallet_position": [float(value) for value in observation[14:16]],
                    "visible_puck_position": [
                        float(value) for value in observation[16:18]
                    ],
                    "requested_target": [
                        float(value) for value in record["requested_action"]
                    ],
                    "authorised_target": authorised,
                    "applied_target": [
                        float(value) for value in record["applied_action"]
                    ],
                },
                "privileged": {
                    "true_puck_position": [
                        float(value)
                        for value in record["privileged"]["puck_position_table_xy"]
                    ],
                    "true_puck_velocity": [
                        float(value)
                        for value in record["privileged"]["puck_velocity_table_xy"]
                    ],
                    "target_goal": [-0.974, float(generated.target_goal_y)],
                    "privileged_intercept_target": list(action),
                    "contact": bool(record["privileged"]["contact"]),
                    "outcome": (
                        wp7._paper_outcome(str(record["privileged"]["outcome"]))
                        if final
                        else "pending"
                    ),
                },
            }
        )
    return rows


def _manifest(
    *,
    staged: Path,
    run_id: str,
    pair_id: str,
    treatment: str,
    provider_kind: str,
    provider_identity: str,
    provider_configuration: dict[str, Any],
    checkpoint_sha256: str | None,
    generated: Any,
    protocol: dict[str, Any],
    seed: int,
    delay_ms: int,
    provider_response_sha256: str,
    image: str,
    image_digest: str,
) -> dict[str, Any]:
    delay = {
        "kind": "seeded",
        "delay_ms": delay_ms,
        "fault": protocol["paired_trial"]["fault"],
    }
    delay["sha256"] = semantic_sha256(delay)
    tree = protocol["behaviour_trees"][treatment]
    acceptance_policy = (
        "deadline_only" if treatment == "deadline_only" else "invocation_scoped"
    )
    return {
        "schema_version": "airhockey.run_manifest.v1",
        "run_id": run_id,
        "pair_id": pair_id,
        "capture_status": "paper_complete",
        "paper_eligible": True,
        "acceptance_policy": acceptance_policy,
        "repositories": {
            "muesli_bt": {
                "present": True,
                "commit": wp7._source_revision(),
                "dirty": False,
            },
            "air_hockey": {
                "present": True,
                "commit": protocol["repositories"]["acra_revision"],
                "dirty": False,
            },
        },
        "container": {
            "image": image,
            "digest": f"sha256:{image_digest.removeprefix('sha256:')}",
        },
        "simulator": {
            "backend": "acra_direct_launch",
            "version": "airhockey_distill-0.1.0",
            "control_period_ms": protocol["control_period_ms"],
        },
        "provider": {
            "kind": provider_kind,
            "identity": provider_identity,
            "version": "v1",
            "configuration_sha256": semantic_sha256(provider_configuration),
            "checkpoint_sha256": (
                None if checkpoint_sha256 is None else f"sha256:{checkpoint_sha256}"
            ),
        },
        "shot": {
            "split": protocol["paper_split"]["name"],
            "manifest_sha256": f"sha256:{protocol['paper_split']['manifest_sha256']}",
            "manifest_entry_sha256": semantic_sha256(generated.as_dict()),
        },
        "behaviour_tree": {
            "path": tree["path"],
            "sha256": f"sha256:{tree['sha256']}",
        },
        "context_rule": {
            "version": "air_hockey_muesli_contract.v1",
            "reacquisition_starts_new_context": True,
            "source_age_limit_steps": protocol["paired_trial"][
                "source_age_limit_steps"
            ],
        },
        "delay_schedule": delay,
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
            "delay_schedule_sha256": delay["sha256"],
            "seed": seed,
        },
        "expected_integrity": protocol["paired_trial"]["expected"][treatment],
        "raw_artefacts": {
            name: {"sha256": file_sha256(staged / name)} for name in RAW_NAMES
        },
        "derived_artefacts": sorted(DERIVED_ARTEFACTS),
    }


def _capture_run(
    *,
    executable: Path,
    output_root: Path,
    generated: Any,
    pair_id: str,
    treatment: str,
    observation: list[float],
    action: list[float],
    provider_kind: str,
    provider_identity: str,
    provider_configuration: dict[str, Any],
    checkpoint_sha256: str | None,
    seed: int,
    delay_ms: int,
    action_lock_steps: int,
    protocol: dict[str, Any],
    image: str,
    image_digest: str,
) -> Path:
    run_id = f"{pair_id}-{SLUGS[treatment]}"
    replay_run_id = f"{run_id}-replay"
    tree = EXAMPLE_ROOT / protocol["behaviour_trees"][treatment]["path"]
    output_root.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(
        prefix="air-hockey-wp8-stage-", dir=output_root
    ) as temp:
        staged = Path(temp) / run_id
        staged.mkdir()
        (staged / RUN_MARKER).write_text(
            "airhockey.run_manifest.v1\n", encoding="utf-8"
        )
        live = _run_native_half(
            executable=executable,
            tree=tree,
            scenario=SCENARIOS[treatment],
            treatment=treatment,
            run_id=run_id,
            action=action,
            paper=protocol["paired_trial"],
            delay_ms=delay_ms,
            action_lock_steps=action_lock_steps,
            generated=generated,
            events_path=staged / "events.jsonl",
            replay=False,
        )
        replay = _run_native_half(
            executable=executable,
            tree=tree,
            scenario=SCENARIOS[treatment],
            treatment=treatment,
            run_id=replay_run_id,
            action=action,
            paper=protocol["paired_trial"],
            delay_ms=delay_ms,
            action_lock_steps=action_lock_steps,
            generated=generated,
            events_path=staged / "replay-events.jsonl",
            replay=True,
        )
        write_jsonl(
            staged / "task-trajectory.jsonl",
            _trajectory(
                run_id,
                live["records"],
                generated,
                action,
                treatment,
                protocol["paired_trial"]["context_change_step"],
            ),
        )
        write_jsonl(
            staged / "replay-task-trajectory.jsonl",
            _trajectory(
                replay_run_id,
                replay["records"],
                generated,
                action,
                treatment,
                protocol["paired_trial"]["context_change_step"],
            ),
        )
        records = wp7._recorded_provider(
            run_id, pair_id, observation, action, delay_ms
        )
        recorded = RecordedProviderReplay(records)
        if recorded.infer(records[0]["request_sha256"])["values"] != action:
            raise GateG8RecoveryError("recorded provider did not reproduce its action")
        write_jsonl(staged / "recorded-provider.jsonl", records)
        write_json(staged / "direct-replay.json", live["direct_replay"])
        write_json(staged / "replay-direct-replay.json", replay["direct_replay"])
        (staged / "runner.stdout").write_text(live["stdout"], encoding="utf-8")
        (staged / "replay-runner.stdout").write_text(
            replay["stdout"], encoding="utf-8"
        )
        write_json(
            staged / "manifest.json",
            _manifest(
                staged=staged,
                run_id=run_id,
                pair_id=pair_id,
                treatment=treatment,
                provider_kind=provider_kind,
                provider_identity=provider_identity,
                provider_configuration=provider_configuration,
                checkpoint_sha256=checkpoint_sha256,
                generated=generated,
                protocol=protocol,
                seed=seed,
                delay_ms=delay_ms,
                provider_response_sha256=records[0]["response_sha256"],
                image=image,
                image_digest=image_digest,
            ),
        )
        published = guarded_publish(staged, output_root, run_id, force=False)
    analyse_run(published)
    return published


def _treatment_from_run_id(run_id: str) -> str:
    for treatment, slug in SLUGS.items():
        if run_id.endswith(f"-{slug}"):
            return treatment
    raise GateG8RecoveryError(f"unknown WP8 treatment in run id: {run_id}")


def _campaign_summary(runs: Path) -> dict[str, Any]:
    groups: defaultdict[str, dict[str, dict[str, Any]]] = defaultdict(dict)
    run_dirs = _marked_run_dirs(runs)
    if not run_dirs:
        raise GateG8RecoveryError("no marked WP8 run bundles")
    for run_dir in run_dirs:
        manifest = read_json(run_dir / "manifest.json")
        summary = read_json(run_dir / "trial-summary.json")
        groups[manifest["pair_id"]][_treatment_from_run_id(manifest["run_id"])] = {
            "manifest": manifest,
            "summary": summary,
            "run_dir": run_dir,
        }
    if any(set(group) != set(TREATMENTS) for group in groups.values()):
        raise GateG8RecoveryError("each WP8 pair must contain exactly three treatments")

    treatment_rows: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for pair_id, group in sorted(groups.items()):
        for treatment in TREATMENTS:
            summary = group[treatment]["summary"]
            treatment_rows[treatment].append(
                {
                    "pair_id": pair_id,
                    "obsolete_dispatches": summary["integrity"][
                        "obsolete_action_chunks_dispatched"
                    ],
                    "projected_motion": summary["obsolete_target_motion"][
                        "projected_motion_towards_obsolete_target"
                    ],
                    "save": int(summary["task_outcome"] == "save"),
                    "outcome": summary["task_outcome"],
                }
            )

    treatments: dict[str, Any] = {}
    baseline_rows = treatment_rows["deadline_only"]
    for treatment in TREATMENTS:
        rows = treatment_rows[treatment]
        motions = [row["projected_motion"] for row in rows]
        baseline_motions = [row["projected_motion"] for row in baseline_rows]
        motion_differences = [
            row["projected_motion"] - baseline_motions[index]
            for index, row in enumerate(rows)
        ]
        treatments[treatment] = {
            "pairs": len(rows),
            "obsolete_dispatch_pairs": sum(
                row["obsolete_dispatches"] > 0 for row in rows
            ),
            "obsolete_dispatches": sum(row["obsolete_dispatches"] for row in rows),
            "save_count": sum(row["save"] for row in rows),
            "save_rate": sum(row["save"] for row in rows) / len(rows),
            "projected_motion_mean": sum(motions) / len(motions),
            "paired_motion_vs_deadline_only": paired_bootstrap(
                motion_differences
            ),
            "outcome_counts": {
                outcome: sum(row["outcome"] == outcome for row in rows)
                for outcome in sorted({row["outcome"] for row in rows})
            },
        }
    return {
        "schema_version": "airhockey.wp8.campaign_summary.v1",
        "pair_count": len(groups),
        "run_count": len(run_dirs),
        "treatments": treatments,
        "pairs": [
            {
                "pair_id": pair_id,
                "treatments": {
                    treatment: group[treatment]["summary"]
                    for treatment in TREATMENTS
                },
            }
            for pair_id, group in sorted(groups.items())
        ],
    }


def _measurements(
    runs: Path,
    summary: dict[str, Any],
    learned_inference_ns: list[int],
) -> dict[str, Any]:
    run_dirs = _marked_run_dirs(runs)
    tick_ns: list[int] = []
    tick_ns_by_treatment: defaultdict[str, list[int]] = defaultdict(list)
    replay_mismatches = 0
    trace_failures = 0
    direct_replay_failures = 0
    missing_terminal = 0
    reason_failures = 0
    duplicate_terminal_decisions = 0
    duplicate_dispatches = 0
    recovery_predicates = 0
    treatment_revisions: defaultdict[str, set[str]] = defaultdict(set)
    treatment_images: defaultdict[str, set[str]] = defaultdict(set)
    for run_dir in run_dirs:
        manifest = read_json(run_dir / "manifest.json")
        trial = read_json(run_dir / "trial-summary.json")
        treatment = _treatment_from_run_id(manifest["run_id"])
        treatment_revisions[treatment].add(
            manifest["repositories"]["muesli_bt"]["commit"]
        )
        treatment_images[treatment].add(manifest["container"]["digest"])
        for timing in _timing_records(
            (run_dir / "runner.stdout").read_text(encoding="utf-8")
        ):
            tick_ns.extend(timing["tick_duration_ns"])
            tick_ns_by_treatment[treatment].extend(timing["tick_duration_ns"])
        if not read_json(run_dir / "replay-report.json")["matched"]:
            replay_mismatches += 1
        if read_json(run_dir / "trace-validation.json")["status"] != "passed":
            trace_failures += 1
        if not read_json(run_dir / "direct-replay.json")["passed"]:
            direct_replay_failures += 1
        missing_terminal += trial["integrity"]["invocations_without_terminal_state"]
        reason_failures += int(not trial["integrity"]["reason_code_agreement"])
        duplicate_terminal_decisions += trial["integrity"]["duplicate_commits"]
        duplicate_dispatches += trial["integrity"]["duplicate_dispatches"]
        if treatment == "invocation_scoped_current_context_recovery":
            recovery_predicates += int(
                "p8_recovery_episode_completed PASS"
                in (run_dir / "runner.stdout").read_text(encoding="utf-8")
            )
    return {
        "runs": len(run_dirs),
        "missing_terminal_invocations": missing_terminal,
        "reason_code_failures": reason_failures,
        "duplicate_terminal_decisions": duplicate_terminal_decisions,
        "duplicate_dispatches": duplicate_dispatches,
        "replay_mismatches": replay_mismatches,
        "trace_failures": trace_failures,
        "direct_replay_failures": direct_replay_failures,
        "recovery_episode_predicates": recovery_predicates,
        "tick_timing": _timing_summary(tick_ns, 20.0),
        "tick_timing_by_treatment": {
            treatment: _timing_summary(tick_ns_by_treatment[treatment], 20.0)
            for treatment in TREATMENTS
        },
        "learned_inference_timing": _timing_summary(learned_inference_ns),
        "treatment_provenance": {
            treatment: {
                "muesli_bt_revisions": sorted(treatment_revisions[treatment]),
                "container_digests": sorted(treatment_images[treatment]),
            }
            for treatment in TREATMENTS
        },
        "treatments": {
            treatment: {
                "obsolete_dispatch_pairs": summary["treatments"][treatment][
                    "obsolete_dispatch_pairs"
                ],
                "save_count": summary["treatments"][treatment]["save_count"],
            }
            for treatment in TREATMENTS
        },
    }


def _timing_gate_summary(
    measurements: dict[str, Any], reaggregated: bool
) -> tuple[str, dict[str, Any]]:
    scope = (
        "invocation_scoped_current_context_recovery"
        if reaggregated
        else "all_treatments"
    )
    timing = (
        measurements["tick_timing_by_treatment"][scope]
        if reaggregated
        else measurements["tick_timing"]
    )
    return scope, timing


def run_campaign(
    executable: Path,
    checkpoint: Path,
    output: Path,
    image: str,
    image_digest: str,
    reuse_baselines_from: Path | None = None,
) -> dict[str, Any]:
    protocol = _load_wp8()
    executable = executable.resolve()
    checkpoint = checkpoint.resolve()
    if not executable.is_file() or not os.access(executable, os.X_OK):
        raise GateG8RecoveryError(f"WP8 runner is not executable: {executable}")
    learned_definition = protocol["learned_provider"]
    if not checkpoint.is_file() or _sha256(checkpoint) != learned_definition[
        "checkpoint_sha256"
    ]:
        raise GateG8RecoveryError("the selected learned checkpoint is missing or changed")
    output = _prepare_output(output)
    shutil.copy2(PROTOCOL_PATH, output / "wp8-protocol.json")
    shots = wp7._paper_shots(protocol)
    write_json(
        output / "paper-manifest.json",
        {
            "schema_version": "airhockey.wp8.paper_manifest.v1",
            "split": "muesli_test",
            "manifest_sha256": protocol["paper_split"]["manifest_sha256"],
            "shots": [generated.as_dict() for generated in shots],
        },
    )
    learned_provider = wp7.AcraExportProvider(
        learned_definition["family_id"],
        checkpoint,
        f"sha256:{learned_definition['checkpoint_sha256']}",
    )
    if (
        int(learned_provider.metadata.get("training_seed", -1))
        != learned_definition["training_seed"]
        or learned_provider.metadata.get("training_stage")
        != learned_definition["training_stage"]
        or learned_provider.metadata.get("protocol_sha256")
        != learned_definition["source_protocol_sha256"]
    ):
        raise GateG8RecoveryError("learned checkpoint metadata changed")

    runs = output / "runs"
    reaggregation = None
    treatments = TREATMENTS
    if reuse_baselines_from is not None:
        reaggregation = _reuse_baseline_runs(
            reuse_baselines_from, runs, protocol
        )
        treatments = ("invocation_scoped_current_context_recovery",)
    pair_count = 0
    for shot_index, generated in enumerate(shots):
        observation = wp7._initial_observation(
            generated, protocol["deterministic_provider"]["action_lock_steps"]
        )
        action = wp7._deterministic_action(observation)
        shot_slug = f"q{shot_index:03d}-{wp7._shot_key(generated)[:8]}"
        for schedule in protocol["deterministic_provider"]["delay_schedule"]:
            pair_id = f"p8-det-s{schedule['seed']}-{shot_slug}"
            provider_configuration = {
                **protocol["deterministic_provider"],
                "selected_seed": schedule["seed"],
                "selected_delay_ms": schedule["delay_ms"],
            }
            for treatment in treatments:
                _capture_run(
                    executable=executable,
                    output_root=runs,
                    generated=generated,
                    pair_id=pair_id,
                    treatment=treatment,
                    observation=observation,
                    action=action,
                    provider_kind="deterministic",
                    provider_identity=protocol["deterministic_provider"]["identity"],
                    provider_configuration=provider_configuration,
                    checkpoint_sha256=None,
                    seed=schedule["seed"],
                    delay_ms=schedule["delay_ms"],
                    action_lock_steps=protocol["deterministic_provider"][
                        "action_lock_steps"
                    ],
                    protocol=protocol,
                    image=image,
                    image_digest=image_digest,
                )
            pair_count += 1
            print(
                f"WP8 deterministic pair {pair_count}/"
                f"{protocol['campaign']['total_pairs']} passed: {pair_id}",
                flush=True,
            )

    learned_inference_ns: list[int] = []
    for generated in wp7.select_learned_subset(
        shots, learned_definition["subset_shots"]
    ):
        observation = wp7._initial_observation(
            generated, learned_definition["action_lock_steps"]
        )
        action, elapsed_ns = wp7._provider_action(learned_provider, observation)
        learned_inference_ns.append(elapsed_ns)
        pair_id = f"p8-learned-s{learned_definition['delay_seed']}-{wp7._shot_key(generated)[:12]}"
        for treatment in treatments:
            _capture_run(
                executable=executable,
                output_root=runs,
                generated=generated,
                pair_id=pair_id,
                treatment=treatment,
                observation=observation,
                action=action,
                provider_kind="acra_frozen",
                provider_identity=learned_definition["family_id"],
                provider_configuration=learned_definition,
                checkpoint_sha256=learned_definition["checkpoint_sha256"],
                seed=learned_definition["delay_seed"],
                delay_ms=learned_definition["delay_ms"],
                action_lock_steps=learned_definition["action_lock_steps"],
                protocol=protocol,
                image=image,
                image_digest=image_digest,
            )
        pair_count += 1
        print(
            f"WP8 learned pair {pair_count}/{protocol['campaign']['total_pairs']} "
            f"passed: {pair_id}",
            flush=True,
        )

    summary = _campaign_summary(runs)
    measurements = _measurements(runs, summary, learned_inference_ns)
    write_json(output / "campaign-summary.json", summary)
    measured = {
        "schema_version": "airhockey.wp8.report.v1",
        "status": "measured",
        "muesli_bt_revision": wp7._source_revision(),
        "acra_revision": protocol["repositories"]["acra_revision"],
        "protocol_sha256": _sha256(PROTOCOL_PATH),
        "paper_manifest_sha256": protocol["paper_split"]["manifest_sha256"],
        "paper_split_opened": True,
        "pairs": summary["pair_count"],
        "runs": summary["run_count"],
        "measurements": measurements,
        "campaign_summary_sha256": semantic_sha256(summary),
        "reaggregation": reaggregation,
    }
    write_json(output / "wp8-measured.json", measured)
    gate = protocol["gate"]
    failures: list[str] = []
    if summary["pair_count"] != protocol["campaign"]["total_pairs"]:
        failures.append("campaign cardinality")
    if summary["run_count"] != protocol["campaign"]["treatment_runs"]:
        failures.append("treatment run cardinality")
    expected_dispatches = {
        "deadline_only": gate["maximum_deadline_only_obsolete_dispatch_pairs"],
        "invocation_scoped_hold": gate[
            "maximum_invocation_scoped_hold_obsolete_dispatch_pairs"
        ],
        "invocation_scoped_current_context_recovery": gate[
            "maximum_recovery_obsolete_dispatch_pairs"
        ],
    }
    for treatment, expected in expected_dispatches.items():
        if summary["treatments"][treatment]["obsolete_dispatch_pairs"] != (
            expected
            if treatment == "deadline_only"
            else 0
        ):
            failures.append(f"{treatment} obsolete dispatch gate")
    if (
        measurements["recovery_episode_predicates"]
        < gate["minimum_recovery_policy_actions"]
        * protocol["campaign"]["total_pairs"]
    ):
        failures.append("recovery predicate coverage")
    if measurements["missing_terminal_invocations"] > gate[
        "maximum_missing_terminal_invocations"
    ]:
        failures.append("missing terminal invocation")
    if measurements["reason_code_failures"]:
        failures.append("reason-code agreement")
    if measurements["duplicate_terminal_decisions"]:
        failures.append("duplicate terminal decision")
    if measurements["duplicate_dispatches"]:
        failures.append("duplicate dispatch")
    if measurements["replay_mismatches"] > gate["maximum_replay_mismatches"]:
        failures.append("replay")
    if measurements["trace_failures"] > gate["maximum_trace_failures"]:
        failures.append("trace validation")
    if measurements["direct_replay_failures"]:
        failures.append("direct replay")
    timing_gate_scope, timing_gate = _timing_gate_summary(
        measurements, reaggregation is not None
    )
    measured["timing_gate_scope"] = timing_gate_scope
    measured["timing_gate"] = timing_gate
    write_json(output / "wp8-measured.json", measured)
    if timing_gate["p99_ms"] > gate["maximum_tick_p99_ms"]:
        failures.append("BT tick p99")
    if measurements["learned_inference_timing"]["p95_ms"] > gate[
        "maximum_learned_p95_inference_ms"
    ]:
        failures.append("learned inference p95")
    if failures:
        raise GateG8RecoveryError(f"Gate G8 recovery failed: {', '.join(failures)}")
    result = {
        **measured,
        "status": "passed",
        "protocol_frozen": True,
        "raw_bundles_read_only": False,
        "backup_verified": False,
    }
    write_json(output / "wp8-report.json", result)
    print(
        "air-hockey WP8 recovery campaign passed: 228 matched paper pairs, "
        "684 validated treatment runs",
        flush=True,
    )
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("check-protocol", "run"))
    parser.add_argument("--runner", type=Path)
    parser.add_argument("--checkpoint", type=Path)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--image")
    parser.add_argument("--image-digest")
    parser.add_argument(
        "--reuse-baselines-from",
        type=Path,
        help="reuse and revalidate deadline-only and hold bundles from a marked WP8 campaign",
    )
    arguments = parser.parse_args()
    protocol = _load_wp8()
    if arguments.command == "check-protocol":
        print(
            "air-hockey WP8 protocol passed: "
            f"{protocol['campaign']['total_pairs']} pairs, "
            f"{protocol['campaign']['treatment_runs']} treatment runs"
        )
        return 0
    required = (
        arguments.runner,
        arguments.checkpoint,
        arguments.out,
        arguments.image,
        arguments.image_digest,
    )
    if any(value is None for value in required):
        parser.error("run requires --runner, --checkpoint, --out, --image and --image-digest")
    run_campaign(
        arguments.runner,
        arguments.checkpoint,
        arguments.out,
        arguments.image,
        arguments.image_digest,
        arguments.reuse_baselines_from,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
