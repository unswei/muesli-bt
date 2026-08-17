#!/usr/bin/env python3
"""Run, validate and seal the frozen post-admission authority campaign."""

from __future__ import annotations

import argparse
import csv
import json
import math
import os
import shutil
import subprocess
import tarfile
import tempfile
from collections import defaultdict
from pathlib import Path
from typing import Any

import run_wp7 as wp7
import run_wp8 as wp8
import run_wp9 as wp9
from analysis.evidence import (
    EvidenceError,
    _trace_report,
    clopper_pearson,
    event_projection,
    file_sha256,
    paired_bootstrap,
    read_json,
    read_jsonl,
    semantic_sha256,
    write_json,
    write_jsonl,
)
from run_wp6 import _timing_summary

EXAMPLE_ROOT = Path(__file__).resolve().parent
PROTOCOL_PATH = EXAMPLE_ROOT / "configs" / "wp10_post_admission_protocol.json"
RUN_ROOT_MARKER = ".air-hockey-wp10-run"
TREATMENTS = ("admission_only", "two_gate")
DISTURBANCES = ("context_change", "owner_revocation", "no_change")
SCENARIOS = {
    ("context_change", "admission_only"): "P10-context-admission-only",
    ("context_change", "two_gate"): "P10-context-two-gate",
    ("owner_revocation", "admission_only"): "P10-owner-admission-only",
    ("owner_revocation", "two_gate"): "P10-owner-two-gate",
    ("no_change", "admission_only"): "P10-control-admission-only",
    ("no_change", "two_gate"): "P10-control-two-gate",
}
RAW_NAMES = (
    "events.jsonl",
    "replay-events.jsonl",
    "public-trajectory.jsonl",
    "replay-public-trajectory.jsonl",
    "runner.stdout",
    "replay-runner.stdout",
    "direct-replay.json",
    "replay-direct-replay.json",
    "trace-validation.json",
    "cell-result.json",
)


class GateG10PostAdmissionError(EvidenceError):
    """A frozen WP10 post-admission invariant failed."""


def _sha256(path: Path) -> str:
    return file_sha256(path).removeprefix("sha256:")


def _load_protocol() -> tuple[dict[str, Any], dict[str, Any]]:
    protocol = read_json(PROTOCOL_PATH)
    if protocol.get("status") != "frozen_before_post_admission_campaign":
        raise GateG10PostAdmissionError("WP10 protocol is not frozen")
    parent_path = EXAMPLE_ROOT / protocol["parent_wp9_protocol"]
    if _sha256(parent_path) != protocol["parent_wp9_protocol_sha256"]:
        raise GateG10PostAdmissionError("the frozen WP9 parent protocol changed")
    parent, wp8_parent = wp9._load_protocol()
    tree = EXAMPLE_ROOT / protocol["paired_trial"]["behaviour_tree"]
    if _sha256(tree) != protocol["paired_trial"]["behaviour_tree_sha256"]:
        raise GateG10PostAdmissionError("the frozen WP10 Behaviour Tree changed")
    if protocol["paper_split"] != parent["paper_split"]:
        raise GateG10PostAdmissionError("WP10 changed the frozen paper split")
    if [row["id"] for row in protocol["treatments"]] != list(TREATMENTS):
        raise GateG10PostAdmissionError("WP10 treatment order changed")
    if [row["id"] for row in protocol["disturbances"]] != list(DISTURBANCES):
        raise GateG10PostAdmissionError("WP10 disturbance order changed")
    campaign = protocol["campaign"]
    expected = (
        campaign["matched_shots"] * len(TREATMENTS) * len(DISTURBANCES)
    )
    if (
        campaign["matched_shots"] != protocol["paper_split"]["selected_shots"]
        or campaign["cells_per_shot"] != len(TREATMENTS) * len(DISTURBANCES)
        or campaign["treatment_runs"] != expected
    ):
        raise GateG10PostAdmissionError("WP10 campaign cardinalities do not reconcile")
    return protocol, wp8_parent


def _prepare_output(output: Path) -> Path:
    output = output.resolve()
    if output == Path(output.anchor):
        raise GateG10PostAdmissionError("WP10 output cannot be a filesystem root")
    if output.exists() and any(output.iterdir()):
        raise GateG10PostAdmissionError(
            f"refuse to replace non-empty WP10 output: {output}"
        )
    output.mkdir(parents=True, exist_ok=True)
    (output / RUN_ROOT_MARKER).write_text("airhockey.wp10.run.v1\n", encoding="utf-8")
    (output / "runs").mkdir()
    return output


def _expected_predicates(disturbance: str) -> set[str]:
    expected = {
        "p10_admission_succeeds_before_intervention",
        "p10_recovery_episode_completed",
    }
    expected.add(
        "p10_no_change_control_accepts_valid_handle"
        if disturbance == "no_change"
        else "p10_dispatch_outcome_matches_treatment"
    )
    return expected


def _post_admission_marker(stdout: str) -> dict[str, Any]:
    rows = [
        json.loads(line.removeprefix("POST_ADMISSION "))
        for line in stdout.splitlines()
        if line.startswith("POST_ADMISSION ")
    ]
    if len(rows) != 1:
        raise GateG10PostAdmissionError(
            "WP10 runner did not emit exactly one post-admission marker"
        )
    return rows[0]


def _public_trajectory(records: list[dict[str, Any]]) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for record in records:
        state = record["public_state"]
        observation = state["observation"]
        rows.append(
            {
                "schema_version": "airhockey.wp10.public_trajectory.v1",
                "observation_step": int(state["observation_step"]),
                "context_id": state["defence_context_id"],
                "puck_visible": bool(state["puck_visible"]),
                "mallet_position": [float(value) for value in observation[14:16]],
                "visible_puck_position": [
                    float(value) for value in observation[16:18]
                ],
                "requested_target": [
                    float(value) for value in record["requested_action"]
                ],
                "applied_target": [
                    float(value) for value in record["applied_action"]
                ],
                "episode_active": bool(state["episode_active"]),
                "terminated": bool(state["terminated"]),
                "truncated": bool(state["truncated"]),
                "outcome": wp7._paper_outcome(
                    str(record["privileged"]["outcome"])
                ),
            }
        )
    return rows


def _same_target(lhs: list[float], rhs: list[float]) -> bool:
    return math.dist(lhs, rhs) <= 1.0e-12


def _post_loss_motion(
    initial_observation: list[float],
    trajectory: list[dict[str, Any]],
    target: list[float],
    loss_step: int | None,
) -> dict[str, Any] | None:
    if loss_step is None:
        return None
    positions: dict[int, list[float]] = {
        0: [float(initial_observation[14]), float(initial_observation[15])]
    }
    for row in trajectory:
        positions[int(row["observation_step"])] = list(row["mallet_position"])
    if loss_step not in positions:
        raise GateG10PostAdmissionError(
            f"authority-loss step {loss_step} is absent from the public trajectory"
        )
    projected = 0.0
    commanded_projected = 0.0
    total = 0.0
    motion_steps = 0
    command_steps = 0
    ordered = sorted((step, position) for step, position in positions.items())
    for (previous_step, previous), (step, current) in zip(ordered, ordered[1:]):
        if step <= loss_step:
            continue
        delta = [current[0] - previous[0], current[1] - previous[1]]
        travelled = math.hypot(delta[0], delta[1])
        total += travelled
        direction = [target[0] - previous[0], target[1] - previous[1]]
        norm = math.hypot(direction[0], direction[1])
        towards = (
            0.0
            if norm <= 1.0e-12
            else max(0.0, (delta[0] * direction[0] + delta[1] * direction[1]) / norm)
        )
        projected += towards
        motion_steps += int(towards > 1.0e-12)
        row = next(
            value for value in trajectory if int(value["observation_step"]) == step
        )
        target_applied = _same_target(row["applied_target"], target)
        command_steps += int(target_applied)
        if target_applied:
            commanded_projected += towards
    start = positions[loss_step]
    final = ordered[-1][1]
    return {
        "authority_loss_observation_step": loss_step,
        "no_longer_authorised_target": target,
        "command_steps_towards_target": command_steps,
        "motion_steps_towards_target": motion_steps,
        "projected_motion_towards_target": projected,
        "projected_motion_while_target_applied": commanded_projected,
        "net_distance_reduction_to_target": max(
            0.0, math.dist(start, target) - math.dist(final, target)
        ),
        "total_mallet_motion_after_authority_loss": total,
    }


def _terminal_integrity(events: list[dict[str, Any]]) -> dict[str, int]:
    submitted: defaultdict[tuple[str, int], int] = defaultdict(int)
    terminal: defaultdict[tuple[str, int], int] = defaultdict(int)
    dispatches: defaultdict[str, int] = defaultdict(int)
    for row in events:
        data = row["data"]
        if row["type"] == "vla_submit" and "generation" in data:
            submitted[(str(data["job_id"]), int(data["generation"]))] += 1
        elif row["type"] == "vla_result" and "generation" in data:
            terminal[(str(data["job_id"]), int(data["generation"]))] += 1
        elif (
            row["type"] == "cap_call_start"
            and data.get("capability") == "cap.vla.action_chunk.v1"
        ):
            dispatches[str(data.get("job_id", ""))] += 1
    return {
        "missing_terminal_invocations": sum(
            max(0, count - terminal[key]) for key, count in submitted.items()
        ),
        "duplicate_terminal_decisions": sum(
            max(0, count - submitted.get(key, 0)) for key, count in terminal.items()
        ),
        "duplicate_dispatches": sum(max(0, count - 1) for count in dispatches.values()),
    }


def _paired_trajectory_effect(
    admission_only: dict[str, Any], two_gate: dict[str, Any]
) -> dict[str, Any]:
    admission_motion = admission_only[
        "motion_towards_no_longer_authorised_target"
    ]
    two_gate_motion = two_gate["motion_towards_no_longer_authorised_target"]
    if admission_motion is None or two_gate_motion is None:
        raise GateG10PostAdmissionError(
            "paired physical effect requires a disturbed authority schedule"
        )
    if (
        admission_motion["authority_loss_observation_step"]
        != two_gate_motion["authority_loss_observation_step"]
        or admission_motion["no_longer_authorised_target"]
        != two_gate_motion["no_longer_authorised_target"]
    ):
        raise GateG10PostAdmissionError("paired physical-effect oracles changed")
    loss_step = int(admission_motion["authority_loss_observation_step"])
    target = admission_motion["no_longer_authorised_target"]
    admission_rows = {
        int(row["observation_step"]): row
        for row in admission_only["_public_trajectory"]
    }
    two_gate_rows = {
        int(row["observation_step"]): row for row in two_gate["_public_trajectory"]
    }
    if loss_step not in admission_rows or loss_step not in two_gate_rows:
        raise GateG10PostAdmissionError("authority-loss state is absent from a pair")
    admission_origin = admission_rows[loss_step]["mallet_position"]
    two_gate_origin = two_gate_rows[loss_step]["mallet_position"]
    if math.dist(admission_origin, two_gate_origin) > 1.0e-12:
        raise GateG10PostAdmissionError("paired trajectories differ before authority loss")
    direction = [
        target[0] - admission_origin[0],
        target[1] - admission_origin[1],
    ]
    norm = math.hypot(direction[0], direction[1])
    if norm <= 1.0e-12:
        raise GateG10PostAdmissionError("obsolete target equals the loss-state mallet pose")
    unit = [direction[0] / norm, direction[1] / norm]
    matched_steps = sorted(set(admission_rows) & set(two_gate_rows))
    projected_separation: list[tuple[int, float]] = []
    euclidean_separation: list[tuple[int, float]] = []
    for step in matched_steps:
        if step <= loss_step:
            continue
        admission_position = admission_rows[step]["mallet_position"]
        two_gate_position = two_gate_rows[step]["mallet_position"]
        delta = [
            admission_position[0] - two_gate_position[0],
            admission_position[1] - two_gate_position[1],
        ]
        projected_separation.append((step, delta[0] * unit[0] + delta[1] * unit[1]))
        euclidean_separation.append((step, math.hypot(delta[0], delta[1])))
    if not projected_separation:
        raise GateG10PostAdmissionError("paired trajectories have no post-loss overlap")
    maximum_step, maximum_projection = max(
        projected_separation, key=lambda value: value[1]
    )
    maximum_distance_step, maximum_distance = max(
        euclidean_separation, key=lambda value: value[1]
    )
    return {
        "authority_loss_observation_step": loss_step,
        "matched_post_loss_steps": len(projected_separation),
        "maximum_projected_admission_only_separation_towards_obsolete_target": max(
            0.0, maximum_projection
        ),
        "maximum_projected_separation_observation_step": maximum_step,
        "maximum_euclidean_mallet_separation": maximum_distance,
        "maximum_euclidean_separation_observation_step": maximum_distance_step,
    }


def _capture_cell(
    *,
    executable: Path,
    output_root: Path,
    generated: Any,
    initial_observation: list[float],
    action: list[float],
    shot_key: str,
    treatment: str,
    disturbance: str,
    protocol: dict[str, Any],
    image: str,
    image_digest: str,
) -> dict[str, Any]:
    run_id = f"p10-{shot_key}-{disturbance.replace('_', '-')}-{treatment.replace('_', '-')}"
    replay_run_id = f"{run_id}-replay"
    tree = EXAMPLE_ROOT / protocol["paired_trial"]["behaviour_tree"]
    with tempfile.TemporaryDirectory(prefix="wp10-stage-", dir=output_root) as directory:
        staged = Path(directory) / run_id
        staged.mkdir()
        common = {
            "executable": executable,
            "tree": tree,
            "scenario": SCENARIOS[(disturbance, treatment)],
            "treatment": "invocation_scoped_current_context_recovery",
            "action": action,
            "paper": protocol["paired_trial"],
            "delay_ms": protocol["paired_trial"]["completion_delay_ms"],
            "action_lock_steps": 0,
            "generated": generated,
            "expected_predicates": _expected_predicates(disturbance),
        }
        live = wp8._run_native_half(
            **common,
            run_id=run_id,
            events_path=staged / "events.jsonl",
            replay=False,
        )
        replay = wp8._run_native_half(
            **common,
            run_id=replay_run_id,
            events_path=staged / "replay-events.jsonl",
            replay=True,
        )
        marker = _post_admission_marker(live["stdout"])
        replay_marker = _post_admission_marker(replay["stdout"])
        if marker != replay_marker:
            raise GateG10PostAdmissionError(f"marker replay mismatch: {run_id}")
        events = read_jsonl(staged / "events.jsonl")
        replay_events = read_jsonl(staged / "replay-events.jsonl")
        if event_projection(events) != event_projection(replay_events):
            raise GateG10PostAdmissionError(f"event replay mismatch: {run_id}")
        trajectory = _public_trajectory(live["records"])
        replay_trajectory = _public_trajectory(replay["records"])
        if trajectory != replay_trajectory:
            raise GateG10PostAdmissionError(f"trajectory replay mismatch: {run_id}")
        if not trajectory or not (
            trajectory[-1]["terminated"] or trajectory[-1]["truncated"]
        ):
            raise GateG10PostAdmissionError(f"episode did not terminate: {run_id}")
        original_trace = _trace_report(staged / "events.jsonl")
        replay_trace = _trace_report(staged / "replay-events.jsonl")
        trace_validation = {
            "schema_version": "airhockey.wp10.trace_validation.v1",
            "status": (
                "passed"
                if original_trace["passed"] and replay_trace["passed"]
                else "failed"
            ),
            "original": original_trace,
            "replay": replay_trace,
        }
        if trace_validation["status"] != "passed":
            raise GateG10PostAdmissionError(f"trace validation failed: {run_id}")
        expected_obsolete = disturbance != "no_change" and treatment == "admission_only"
        if (
            bool(marker["dispatch_obsolete"]) != (disturbance != "no_change")
            or bool(marker["dispatch_accepted"]) != (
                treatment == "admission_only" or disturbance == "no_change"
            )
            or int(marker["obsolete_dispatches"]) != int(expected_obsolete)
        ):
            raise GateG10PostAdmissionError(f"dispatch oracle mismatch: {run_id}")
        integrity = _terminal_integrity(events)
        motion = _post_loss_motion(
            initial_observation,
            trajectory,
            action,
            marker["authority_loss_observation_step"],
        )
        outcome = trajectory[-1]["outcome"]
        result: dict[str, Any] = {
            "schema_version": "airhockey.wp10.cell_result.v1",
            "run_id": run_id,
            "shot_key": shot_key,
            "treatment": treatment,
            "disturbance": disturbance,
            "dispatch_accepted": bool(marker["dispatch_accepted"]),
            "dispatch_obsolete": bool(marker["dispatch_obsolete"]),
            "dispatch_reason": marker["dispatch_reason"],
            "obsolete_dispatches": int(marker["obsolete_dispatches"]),
            "capability_calls_after_authority_loss": (
                int(marker["accepted_dispatches"])
                if disturbance != "no_change"
                else 0
            ),
            "motion_towards_no_longer_authorised_target": motion,
            "task_outcome": outcome,
            "saved": outcome == "save",
            "valid_handle_rejected": (
                disturbance == "no_change" and not marker["dispatch_accepted"]
            ),
            "replay_matched": True,
            "trace_passed": True,
            "privileged_policy_inputs": 0,
            **integrity,
        }
        write_jsonl(staged / "public-trajectory.jsonl", trajectory)
        write_jsonl(staged / "replay-public-trajectory.jsonl", replay_trajectory)
        (staged / "runner.stdout").write_text(live["stdout"], encoding="utf-8")
        (staged / "replay-runner.stdout").write_text(
            replay["stdout"], encoding="utf-8"
        )
        write_json(staged / "direct-replay.json", live["direct_replay"])
        write_json(staged / "replay-direct-replay.json", replay["direct_replay"])
        write_json(staged / "trace-validation.json", trace_validation)
        write_json(staged / "cell-result.json", result)
        write_json(
            staged / "manifest.json",
            {
                "schema_version": "airhockey.wp10.run_manifest.v1",
                "run_id": run_id,
                "shot_key": shot_key,
                "treatment": treatment,
                "disturbance": disturbance,
                "protocol_sha256": _sha256(PROTOCOL_PATH),
                "muesli_bt_revision": wp7._source_revision(),
                "acra_revision": protocol["acra_revision"],
                "container": {
                    "image": image,
                    "digest": f"sha256:{image_digest.removeprefix('sha256:')}",
                },
                "public_policy_inputs": list(range(19)),
                "privileged_policy_inputs": [],
                "shot_manifest_entry_sha256": semantic_sha256(generated.as_dict()),
                "raw_artefacts": {
                    name: {"sha256": file_sha256(staged / name)}
                    for name in RAW_NAMES
                },
            },
        )
        destination = output_root / run_id
        if destination.exists():
            raise GateG10PostAdmissionError(f"refuse to replace WP10 run: {run_id}")
        os.replace(staged, destination)
    result["_public_trajectory"] = trajectory
    result["tick_duration_ns"] = [
        value for timing in live["timing"] for value in timing["tick_duration_ns"]
    ]
    return result


def _summarise(results: list[dict[str, Any]], protocol: dict[str, Any]) -> dict[str, Any]:
    grouped: defaultdict[tuple[str, str], list[dict[str, Any]]] = defaultdict(list)
    for row in results:
        grouped[(row["disturbance"], row["treatment"])].append(row)
    cells: dict[str, Any] = {}
    expected = protocol["campaign"]["matched_shots"]
    for disturbance in DISTURBANCES:
        for treatment in TREATMENTS:
            rows = grouped[(disturbance, treatment)]
            if len(rows) != expected:
                raise GateG10PostAdmissionError(
                    f"WP10 cell cardinality changed: {disturbance}/{treatment}"
                )
            obsolete = sum(row["obsolete_dispatches"] for row in rows)
            calls = sum(row["capability_calls_after_authority_loss"] for row in rows)
            saves = sum(bool(row["saved"]) for row in rows)
            valid_rejections = sum(bool(row["valid_handle_rejected"]) for row in rows)
            motions = [
                row["motion_towards_no_longer_authorised_target"][
                    "projected_motion_while_target_applied"
                ]
                for row in rows
                if row["motion_towards_no_longer_authorised_target"] is not None
            ]
            cells[f"{disturbance}/{treatment}"] = {
                "runs": len(rows),
                "obsolete_dispatches": clopper_pearson(obsolete, len(rows)),
                "capability_calls_after_authority_loss": calls,
                "mean_projected_motion_while_no_longer_authorised_target_applied": (
                    sum(motions) / len(motions) if motions else None
                ),
                "saves": clopper_pearson(saves, len(rows)),
                "valid_handle_rejections": valid_rejections,
            }
    paired_effects: dict[str, Any] = {}
    for disturbance in DISTURBANCES[:2]:
        by_treatment: dict[str, dict[str, dict[str, Any]]] = {
            treatment: {
                row["shot_key"]: row for row in grouped[(disturbance, treatment)]
            }
            for treatment in TREATMENTS
        }
        keys = sorted(by_treatment["admission_only"])
        effects = [
            _paired_trajectory_effect(
                by_treatment["admission_only"][key],
                by_treatment["two_gate"][key],
            )
            for key in keys
        ]
        maximum_projected = [
            row[
                "maximum_projected_admission_only_separation_towards_obsolete_target"
            ]
            for row in effects
        ]
        paired_effects[disturbance] = {
            "pairs": len(effects),
            "pairs_with_positive_projected_separation": sum(
                value > 1.0e-12 for value in maximum_projected
            ),
            "mean_maximum_projected_admission_only_separation_towards_obsolete_target": sum(
                maximum_projected
            )
            / len(maximum_projected),
            "mean_maximum_euclidean_mallet_separation": sum(
                row["maximum_euclidean_mallet_separation"] for row in effects
            )
            / len(effects),
            "maximum_projected_separation_bootstrap": paired_bootstrap(
                maximum_projected,
                samples=protocol["campaign"].get("bootstrap_samples", 10000),
                seed=protocol["campaign"].get("bootstrap_seed", 6311),
            ),
        }
    tick_ns = [value for row in results for value in row.pop("tick_duration_ns")]
    for row in results:
        row.pop("_public_trajectory")
    return {
        "schema_version": "airhockey.wp10.post_admission.summary.v1",
        "matched_shots": expected,
        "treatment_runs": len(results),
        "cells": cells,
        "paired_physical_effect": paired_effects,
        "tick_timing": _timing_summary(tick_ns, protocol["control_period_ms"]),
    }


def _validate_gate(
    results: list[dict[str, Any]], summary: dict[str, Any], protocol: dict[str, Any]
) -> None:
    gate = protocol["gate"]
    failures: list[str] = []
    if len(results) != protocol["campaign"]["treatment_runs"]:
        failures.append("campaign cardinality")
    for disturbance in DISTURBANCES[:2]:
        admission = summary["cells"][f"{disturbance}/admission_only"]
        two_gate = summary["cells"][f"{disturbance}/two_gate"]
        if admission["obsolete_dispatches"]["successes"] != gate[
            "required_admission_only_obsolete_dispatches_per_disturbance"
        ]:
            failures.append(f"{disturbance} admission-only obsolete dispatch")
        if two_gate["obsolete_dispatches"]["successes"] > gate[
            "maximum_two_gate_obsolete_dispatches"
        ]:
            failures.append(f"{disturbance} two-gate obsolete dispatch")
    for treatment in TREATMENTS:
        control = summary["cells"][f"no_change/{treatment}"]
        accepted = control["runs"] - control["valid_handle_rejections"]
        if accepted != gate["required_valid_control_dispatches_per_treatment"]:
            failures.append(f"{treatment} valid control dispatch")
        if control["valid_handle_rejections"] > gate["maximum_valid_control_rejections"]:
            failures.append(f"{treatment} valid control rejection")
    if summary["paired_physical_effect"]["context_change"][
        "pairs_with_positive_projected_separation"
    ] != gate["required_context_pairs_with_positive_projected_separation"]:
        failures.append("context-change physical separation")
    for name in (
        "missing_terminal_invocations",
        "duplicate_terminal_decisions",
        "duplicate_dispatches",
        "privileged_policy_inputs",
    ):
        if sum(int(row[name]) for row in results) > gate[f"maximum_{name}"]:
            failures.append(name.replace("_", " "))
    if sum(not row["replay_matched"] for row in results) > gate[
        "maximum_replay_mismatches"
    ]:
        failures.append("replay")
    if sum(not row["trace_passed"] for row in results) > gate[
        "maximum_trace_failures"
    ]:
        failures.append("trace")
    if summary["tick_timing"]["p99_ms"] > gate["maximum_tick_p99_ms"]:
        failures.append("BT tick p99")
    if failures:
        raise GateG10PostAdmissionError(f"WP10 failed: {', '.join(failures)}")


def run_campaign(
    executable: Path,
    output: Path,
    image: str,
    image_digest: str,
) -> dict[str, Any]:
    wp8._validate_thread_limits(dict(os.environ))
    protocol, parent = _load_protocol()
    protocol["acra_revision"] = parent["repositories"]["acra_revision"]
    executable = executable.resolve()
    if not executable.is_file() or not os.access(executable, os.X_OK):
        raise GateG10PostAdmissionError(f"WP10 runner is not executable: {executable}")
    output = _prepare_output(output)
    shutil.copy2(PROTOCOL_PATH, output / "wp10-protocol.json")
    shots = wp7._paper_shots(parent)
    selected = wp7.select_learned_subset(shots, protocol["campaign"]["matched_shots"])
    results: list[dict[str, Any]] = []
    completed = 0
    for generated in selected:
        observation = wp7._initial_observation(generated, 0)
        action = wp7._deterministic_action(observation)
        shot_key = wp7._shot_key(generated)[:12]
        for disturbance in DISTURBANCES:
            for treatment in TREATMENTS:
                results.append(
                    _capture_cell(
                        executable=executable,
                        output_root=output / "runs",
                        generated=generated,
                        initial_observation=observation,
                        action=action,
                        shot_key=shot_key,
                        treatment=treatment,
                        disturbance=disturbance,
                        protocol=protocol,
                        image=image,
                        image_digest=image_digest,
                    )
                )
                completed += 1
                print(
                    f"WP10 run {completed}/{protocol['campaign']['treatment_runs']} "
                    f"passed: {shot_key} {disturbance} {treatment}",
                    flush=True,
                )
    summary = _summarise(results, protocol)
    _validate_gate(results, summary, protocol)
    write_jsonl(output / "cell-results.jsonl", results)
    write_json(output / "post-admission-summary.json", summary)
    with (output / "result-table.csv").open("w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(
            (
                "disturbance",
                "treatment",
                "runs",
                "obsolete_dispatch_rate",
                "capability_calls_after_authority_loss",
                "mean_projected_motion",
                "save_rate",
                "valid_handle_rejections",
            )
        )
        for disturbance in DISTURBANCES:
            for treatment in TREATMENTS:
                row = summary["cells"][f"{disturbance}/{treatment}"]
                writer.writerow(
                    (
                        disturbance,
                        treatment,
                        row["runs"],
                        row["obsolete_dispatches"]["estimate"],
                        row["capability_calls_after_authority_loss"],
                        row["mean_projected_motion_while_no_longer_authorised_target_applied"],
                        row["saves"]["estimate"],
                        row["valid_handle_rejections"],
                    )
                )
    report = {
        "schema_version": "airhockey.wp10.report.v1",
        "status": "passed",
        "protocol_sha256": _sha256(PROTOCOL_PATH),
        "muesli_bt_revision": wp7._source_revision(),
        "acra_revision": protocol["acra_revision"],
        "matched_shots": summary["matched_shots"],
        "treatment_runs": summary["treatment_runs"],
        "summary_sha256": semantic_sha256(summary),
        "tick_timing": summary["tick_timing"],
        "raw_bundles_read_only": False,
        "backup_verified": False,
    }
    write_json(output / "wp10-report.json", report)
    print("air-hockey WP10 post-admission campaign passed", flush=True)
    return report


def check_native(executable: Path) -> None:
    protocol, _ = _load_protocol()
    tree = EXAMPLE_ROOT / protocol["paired_trial"]["behaviour_tree"]
    schemas = wp7.SchemaRegistry(
        wp7.REPOSITORY_ROOT / "schemas" / "air_hockey_host" / "v1"
    )
    for disturbance in DISTURBANCES:
        for treatment in TREATMENTS:
            backend = wp7.FakeDirectLaunchBackend()
            with tempfile.TemporaryDirectory(prefix="wp10-native-") as directory:
                root = Path(directory)
                socket_path = root / "host.sock"
                events_path = root / "events.jsonl"
                paper = protocol["paired_trial"]
                command = [
                    str(executable),
                    SCENARIOS[(disturbance, treatment)],
                    str(socket_path),
                    str(tree),
                    str(events_path),
                    f"wp10-native-{disturbance}-{treatment}",
                    "0.8",
                    "-0.4",
                    str(paper["blackout_start_step"]),
                    str(paper["blackout_length_steps"]),
                    str(paper["timeout_steps"]),
                    "0",
                    str(paper["completion_delay_ms"]),
                    "live",
                ]
                with wp7.UnixHostServer(
                    socket_path, wp7.ProtocolProcessor(schemas, backend)
                ):
                    completed = subprocess.run(
                        command,
                        check=False,
                        capture_output=True,
                        text=True,
                        timeout=20,
                    )
            if completed.returncode != 0:
                raise GateG10PostAdmissionError(
                    f"WP10 native check failed for {disturbance}/{treatment}: "
                    f"{completed.stderr.strip()}"
                )
            observed = wp7._observed_predicates(completed.stdout)
            if observed != _expected_predicates(disturbance):
                raise GateG10PostAdmissionError(
                    f"WP10 native predicate mismatch: {disturbance}/{treatment}"
                )
            marker = _post_admission_marker(completed.stdout)
            expected_obsolete = (
                disturbance != "no_change" and treatment == "admission_only"
            )
            if int(marker["obsolete_dispatches"]) != int(expected_obsolete):
                raise GateG10PostAdmissionError(
                    f"WP10 native dispatch mismatch: {disturbance}/{treatment}"
                )
    print("air-hockey WP10 native post-admission branches passed", flush=True)


def _checksum_manifest(root: Path, output: Path) -> None:
    rows = []
    for path in sorted(value for value in root.rglob("*") if value.is_file()):
        if path == output:
            continue
        if path.is_symlink():
            raise GateG10PostAdmissionError("refuse to seal a campaign with symlinks")
        rows.append(f"{_sha256(path)}  {path.relative_to(root).as_posix()}\n")
    output.write_text("".join(rows), encoding="utf-8")


def seal_campaign(campaign: Path, backup: Path, seal_report: Path) -> dict[str, Any]:
    campaign = campaign.resolve()
    backup = backup.resolve()
    seal_report = seal_report.resolve()
    if not (campaign / RUN_ROOT_MARKER).is_file():
        raise GateG10PostAdmissionError("refuse to seal an unmarked WP10 campaign")
    if any(path.is_symlink() for path in campaign.rglob("*")):
        raise GateG10PostAdmissionError("refuse to seal a campaign with symlinks")
    for destination in (backup, seal_report):
        try:
            destination.relative_to(campaign)
        except ValueError:
            pass
        else:
            raise GateG10PostAdmissionError(
                "WP10 backup and seal report must be outside the campaign"
            )
    if backup == seal_report or backup.exists() or seal_report.exists():
        raise GateG10PostAdmissionError("refuse to replace a WP10 seal artefact")
    report_path = campaign / "wp10-report.json"
    summary_path = campaign / "post-admission-summary.json"
    report = read_json(report_path)
    summary = read_json(summary_path)
    if report.get("status") != "passed":
        raise GateG10PostAdmissionError("refuse to seal a failed WP10 campaign")
    if report.get("protocol_sha256") != _sha256(PROTOCOL_PATH):
        raise GateG10PostAdmissionError("WP10 campaign used a different protocol")
    if semantic_sha256(summary) != report.get("summary_sha256"):
        raise GateG10PostAdmissionError("WP10 summary changed after validation")
    checksums = campaign / "checksums.sha256"
    if checksums.exists():
        raise GateG10PostAdmissionError("refuse to replace WP10 checksums")
    backup.parent.mkdir(parents=True, exist_ok=True)
    seal_report.parent.mkdir(parents=True, exist_ok=True)
    _checksum_manifest(campaign, checksums)
    with tarfile.open(backup, "w:gz", compresslevel=6) as archive:
        archive.add(campaign, arcname=campaign.name, recursive=True)
    with tarfile.open(backup, "r:gz") as archive:
        names = {member.name for member in archive.getmembers()}
        if not all(
            any(name.endswith(suffix) for name in names)
            for suffix in (
                "/wp10-report.json",
                "/post-admission-summary.json",
                "/checksums.sha256",
            )
        ):
            raise GateG10PostAdmissionError("WP10 backup verification failed")
    for path in sorted(campaign.rglob("*"), reverse=True):
        path.chmod(0o555 if path.is_dir() else 0o444)
    campaign.chmod(0o555)
    seal = {
        "schema_version": "airhockey.wp10.seal.v1",
        "status": "sealed",
        "campaign": str(campaign),
        "campaign_report_sha256": _sha256(report_path),
        "campaign_checksum_manifest_sha256": _sha256(checksums),
        "backup": str(backup),
        "backup_sha256": _sha256(backup),
        "backup_verified": True,
        "raw_bundles_read_only": True,
        "file_mode": "0444",
        "directory_mode": "0555",
    }
    write_json(seal_report, seal)
    seal_report.chmod(0o444)
    backup.chmod(0o444)
    return seal


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "command", choices=("check-protocol", "check-native", "run", "seal")
    )
    parser.add_argument("--runner", type=Path)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--image")
    parser.add_argument("--image-digest")
    parser.add_argument("--campaign", type=Path)
    parser.add_argument("--backup", type=Path)
    parser.add_argument("--seal-report", type=Path)
    arguments = parser.parse_args()
    protocol, _ = _load_protocol()
    if arguments.command == "check-protocol":
        print(
            "air-hockey WP10 protocol passed: "
            f"{protocol['campaign']['matched_shots']} matched shots, "
            f"{protocol['campaign']['treatment_runs']} treatment runs"
        )
        return 0
    if arguments.command == "check-native":
        if arguments.runner is None:
            parser.error("check-native requires --runner")
        check_native(arguments.runner.resolve())
        return 0
    if arguments.command == "seal":
        if any(
            value is None
            for value in (arguments.campaign, arguments.backup, arguments.seal_report)
        ):
            parser.error("seal requires --campaign, --backup and --seal-report")
        result = seal_campaign(
            arguments.campaign, arguments.backup, arguments.seal_report
        )
        print(f"air-hockey WP10 sealed: {result['backup_sha256']}")
        return 0
    if any(
        value is None
        for value in (
            arguments.runner,
            arguments.out,
            arguments.image,
            arguments.image_digest,
        )
    ):
        parser.error("run requires --runner, --out, --image and --image-digest")
    run_campaign(
        arguments.runner,
        arguments.out,
        arguments.image,
        arguments.image_digest,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
