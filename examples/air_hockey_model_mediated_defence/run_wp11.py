#!/usr/bin/env python3
"""Capture and replay the frozen WP11 live-provider workload."""

from __future__ import annotations

import argparse
import json
import math
import os
import shutil
import subprocess
import sys
import tarfile
import time
from concurrent.futures import Future, ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import run_wp7 as wp7
from analysis.evidence import file_sha256, read_json, semantic_sha256, write_json
from provider.adapters import ProviderError, validate_action, validate_observation
from provider.live_dreamer_service import PROTOCOL_VERSION, LiveProviderClient

EXAMPLE_ROOT = Path(__file__).resolve().parent
REPOSITORY_ROOT = EXAMPLE_ROOT.parents[1]
PROTOCOL_PATH = EXAMPLE_ROOT / "configs" / "wp11_live_provider_protocol.json"
RUN_MARKER = ".air-hockey-wp11-run"
POLICIES = (
    "deadline_only",
    "invocation_scoped_admission_only",
    "invocation_scoped_two_gate",
)
SAVE_OUTCOMES = frozenset(
    {"returned", "arrested", "safe_deflection", "timeout_after_contact"}
)


class GateG11LiveProviderError(RuntimeError):
    """A frozen WP11 workload or evidence invariant failed."""


def _sha256(path: Path) -> str:
    return file_sha256(path).removeprefix("sha256:")


def _source_revision() -> str:
    completed = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=REPOSITORY_ROOT,
        check=True,
        capture_output=True,
        text=True,
    )
    return completed.stdout.strip()


def _require_sha(path: Path, expected: str, label: str) -> None:
    if not path.is_file() or _sha256(path) != expected:
        raise GateG11LiveProviderError(f"{label} SHA-256 mismatch: {path}")


def load_protocol(
    *, acra_root: Path | None = None, experiment_root: Path | None = None
) -> dict[str, Any]:
    protocol = read_json(PROTOCOL_PATH)
    if protocol.get("status") != "frozen_before_live_provider_campaign":
        raise GateG11LiveProviderError("WP11 protocol is not frozen")
    if tuple(protocol["authority"]["policies"]) != POLICIES:
        raise GateG11LiveProviderError("WP11 authority policy order changed")
    if protocol["provider"]["latency_injection"] is not False:
        raise GateG11LiveProviderError("WP11 must not inject provider latency")
    workload = protocol["workload"]
    if (
        workload["blackout_length_steps"] != 0
        or workload["replace_track_steps"]
        or workload["scene_change_schedule"] != "none"
    ):
        raise GateG11LiveProviderError("WP11 must not script scene changes")
    parent = EXAMPLE_ROOT / protocol["parent_wp9_protocol"]
    _require_sha(parent, protocol["parent_wp9_protocol_sha256"], "WP9 parent protocol")
    distribution = EXAMPLE_ROOT / protocol["paper_split"]["distribution_config"]
    _require_sha(
        distribution,
        protocol["paper_split"]["distribution_config_sha256"],
        "paper distribution",
    )
    tree = EXAMPLE_ROOT / workload["capture_controller_tree"]
    _require_sha(
        tree, workload["capture_controller_tree_sha256"], "capture controller tree"
    )
    if acra_root is not None:
        provider = protocol["provider"]
        _require_sha(
            acra_root / provider["teacher_config_relative_to_acra_root"],
            provider["teacher_config_sha256"],
            "Dreamer teacher configuration",
        )
        _require_sha(
            acra_root / provider["reward_config_relative_to_acra_root"],
            provider["reward_config_sha256"],
            "air-hockey reward configuration",
        )
    if experiment_root is not None:
        provider = protocol["provider"]
        checkpoint = (
            experiment_root / provider["checkpoint_relative_to_experiment_root"]
        )
        _require_sha(
            checkpoint / "agent.pkl", provider["agent_sha256"], "Dreamer agent"
        )
        _require_sha(
            checkpoint / "manifest.json",
            provider["manifest_sha256"],
            "Dreamer manifest",
        )
    return protocol


@dataclass
class PublicDisplacementContext:
    """Host-owned context relation using only the visible public puck target."""

    episode_id: str
    threshold: float
    number: int = 1
    anchor: tuple[float, float] | None = None

    def update(self, observation: list[float]) -> bool:
        public = validate_observation(observation)
        if public[18] < 0.5:
            return False
        target = (public[16], public[17])
        if self.anchor is None:
            self.anchor = target
            return False
        displacement = math.dist(self.anchor, target)
        if displacement <= self.threshold:
            return False
        self.number += 1
        self.anchor = target
        return True

    @property
    def context_id(self) -> str:
        return f"{self.episode_id}/context-{self.number:04d}"


def _quantile(values: list[float], probability: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    position = (len(ordered) - 1) * probability
    lower = math.floor(position)
    upper = math.ceil(position)
    if lower == upper:
        return ordered[lower]
    weight = position - lower
    return ordered[lower] * (1.0 - weight) + ordered[upper] * weight


def _latency_summary(values_ns: list[int]) -> dict[str, float]:
    milliseconds = [value / 1_000_000.0 for value in values_ns]
    return {
        "minimum_ms": min(milliseconds, default=0.0),
        "median_ms": _quantile(milliseconds, 0.5),
        "p95_ms": _quantile(milliseconds, 0.95),
        "maximum_ms": max(milliseconds, default=0.0),
    }


def _current_context_target(observation: list[float]) -> list[float]:
    public = validate_observation(observation)
    indices = (16, 17) if public[18] >= 0.5 else (14, 15)
    return [max(-1.0, min(1.0, public[index])) for index in indices]


def _selected_shots(protocol: dict[str, Any]) -> tuple[Any, ...]:
    parent = wp7.load_protocol()
    shots = wp7._paper_shots(parent)
    split = protocol["paper_split"]
    if (
        len(shots) != split["expected_shots"]
        or parent["paper_split"]["manifest_sha256"] != split["manifest_sha256"]
    ):
        raise GateG11LiveProviderError("WP11 paper split changed")
    return wp7.select_learned_subset(shots, int(split["selected_shots"]))


def _engineering_observation(protocol: dict[str, Any]) -> list[float]:
    from airhockey_distill.envs import (
        BlackoutSchedule,
        DefendShotTrackingLoss,
        MujocoDirectLaunchBackend,
        load_direct_launch_distribution,
    )

    distribution = load_direct_launch_distribution(
        EXAMPLE_ROOT / protocol["paper_split"]["distribution_config"]
    )
    shot = distribution.generate("engineering")[0].shot
    environment = DefendShotTrackingLoss(
        MujocoDirectLaunchBackend(),
        blackout=BlackoutSchedule(start_observation_step=0, length_steps=0),
        timeout_steps=int(protocol["workload"]["timeout_steps"]),
        action_lock_steps=int(protocol["workload"]["action_lock_steps"]),
    )
    try:
        observation, _ = environment.reset(shot=shot, seed=6302)
        return [float(value) for value in observation]
    finally:
        environment.close()


def _wait_for_readiness(
    path: Path, process: subprocess.Popen[Any], timeout_seconds: float = 240.0
) -> dict[str, Any]:
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        if path.is_file():
            return read_json(path)
        return_code = process.poll()
        if return_code is not None:
            raise GateG11LiveProviderError(
                f"Dreamer provider exited before readiness with status {return_code}"
            )
        time.sleep(0.1)
    raise GateG11LiveProviderError("timed out waiting for Dreamer provider readiness")


def _start_provider(
    *,
    protocol: dict[str, Any],
    acra_root: Path,
    experiment_root: Path,
    output: Path,
) -> tuple[subprocess.Popen[Any], dict[str, Any], Any, Any]:
    provider = protocol["provider"]
    readiness = output / "provider-readiness.json"
    stdout_handle = (output / "provider.stdout").open("w", encoding="utf-8")
    stderr_handle = (output / "provider.stderr").open("w", encoding="utf-8")
    command = [
        sys.executable,
        "-m",
        "provider.live_dreamer_service",
        "--checkpoint",
        str(experiment_root / provider["checkpoint_relative_to_experiment_root"]),
        "--agent-sha256",
        provider["agent_sha256"],
        "--teacher-config",
        str(acra_root / provider["teacher_config_relative_to_acra_root"]),
        "--reward-config",
        str(acra_root / provider["reward_config_relative_to_acra_root"]),
        "--distribution-config",
        str(EXAMPLE_ROOT / protocol["paper_split"]["distribution_config"]),
        "--runtime-directory",
        str(output / "provider-runtime"),
        "--readiness-file",
        str(readiness),
        "--profile",
        provider["profile"],
    ]
    environment = dict(os.environ)
    environment["PYTHONPATH"] = os.pathsep.join(
        [str(EXAMPLE_ROOT), environment.get("PYTHONPATH", "")]
    ).rstrip(os.pathsep)
    process = subprocess.Popen(
        command,
        cwd=EXAMPLE_ROOT,
        env=environment,
        stdout=stdout_handle,
        stderr=stderr_handle,
    )
    return (
        process,
        _wait_for_readiness(readiness, process),
        stdout_handle,
        stderr_handle,
    )


def _warm_provider(
    protocol: dict[str, Any], readiness: dict[str, Any]
) -> list[dict[str, Any]]:
    observation = _engineering_observation(protocol)
    client = LiveProviderClient(
        readiness["host"], int(readiness["port"]), timeout_seconds=120
    )
    records: list[dict[str, Any]] = []
    session_id = "wp11-warmup"
    try:
        reset = client.reset(session_id, "warmup-reset")
        if not reset.get("ok"):
            raise GateG11LiveProviderError("Dreamer warmup session reset failed")
        for index in range(int(protocol["provider"]["warmup_requests"])):
            request = {
                "protocol_version": PROTOCOL_VERSION,
                "op": "infer",
                "request_id": f"warmup-{index + 1:02d}",
                "session_id": session_id,
                "observation": observation,
                "reward": 0.0,
                "is_first": index == 0,
            }
            response = client.call(request)
            if not response.get("ok"):
                raise GateG11LiveProviderError(f"Dreamer warmup failed: {response}")
            validate_action(response["action"])
            records.append(response)
    finally:
        client.close()
    return records


@dataclass
class _Episode:
    index: int
    generated: Any
    environment: Any
    observation: list[float]
    info: dict[str, Any]
    context: PublicDisplacementContext
    client: LiveProviderClient
    reward: float = 0.0
    generation: int = 0
    future: Future[dict[str, Any]] | None = None
    pending_request: dict[str, Any] | None = None
    active: bool = True
    outcome: str = "pending"
    context_changes: int = 0


def _boundary(episode: _Episode, monotonic_ns: int) -> dict[str, Any]:
    return {
        "session_id": f"episode-{episode.index:04d}",
        "shot_id": episode.generated.shot.shot_id,
        "observation_step": int(episode.info.get("observation_step", 0)),
        "monotonic_ns": monotonic_ns,
        "context_id": episode.context.context_id,
        "episode_active": episode.active,
        "public_puck_target": [float(value) for value in episode.observation[16:18]],
        "puck_visible": bool(episode.observation[18] >= 0.5),
    }


def _submit(episode: _Episode, executor: ThreadPoolExecutor) -> None:
    episode.generation += 1
    created_ns = time.monotonic_ns()
    session_id = f"episode-{episode.index:04d}"
    request_id = f"{session_id}-request-{episode.generation:04d}"
    request = {
        "protocol_version": PROTOCOL_VERSION,
        "op": "infer",
        "request_id": request_id,
        "session_id": session_id,
        "observation": list(episode.observation),
        "reward": episode.reward,
        "is_first": episode.generation == 1,
    }
    episode.pending_request = {
        "request_id": request_id,
        "session_id": session_id,
        "generation": episode.generation,
        "request_created_monotonic_ns": created_ns,
        "captured_context_id": episode.context.context_id,
        "source_observation_step": int(episode.info.get("observation_step", 0)),
        "public_observation": list(episode.observation),
        "request_sha256": semantic_sha256(request),
    }
    episode.future = executor.submit(episode.client.call, request)


def _collect_response(
    episode: _Episode, responses: list[dict[str, Any]], *, wait: bool = False
) -> bool:
    if episode.future is None or episode.pending_request is None:
        return False
    if not wait and not episode.future.done():
        return False
    response = episode.future.result(timeout=120)
    record = {**episode.pending_request, "response": response}
    record["response_sha256"] = semantic_sha256(
        {"action": response.get("action"), "request_id": response.get("request_id")}
    )
    record["record_sha256"] = semantic_sha256(record)
    responses.append(record)
    episode.future = None
    episode.pending_request = None
    return True


def capture(
    *,
    protocol: dict[str, Any],
    readiness: dict[str, Any],
    output: Path,
) -> dict[str, Any]:
    from airhockey_distill.envs import (
        BlackoutSchedule,
        DefendShotTrackingLoss,
        MujocoDirectLaunchBackend,
    )

    shots = _selected_shots(protocol)
    workload = protocol["workload"]
    threshold = float(protocol["context_policy"]["displacement_threshold"])
    episodes: list[_Episode] = []
    timelines: list[dict[str, Any]] = []
    responses: list[dict[str, Any]] = []
    for index, generated in enumerate(shots, start=1):
        environment = DefendShotTrackingLoss(
            MujocoDirectLaunchBackend(),
            blackout=BlackoutSchedule(
                start_observation_step=int(workload["blackout_start_step"]),
                length_steps=int(workload["blackout_length_steps"]),
            ),
            timeout_steps=int(workload["timeout_steps"]),
            action_lock_steps=int(workload["action_lock_steps"]),
        )
        observation, info = environment.reset(
            shot=generated.shot, seed=int(protocol["paper_split"]["seed"])
        )
        public = [float(value) for value in observation]
        context = PublicDisplacementContext(f"episode-{index:04d}", threshold)
        context.update(public)
        client = LiveProviderClient(
            readiness["host"], int(readiness["port"]), timeout_seconds=120
        )
        reset = client.reset(f"episode-{index:04d}", f"episode-{index:04d}-reset")
        if not reset.get("ok"):
            raise GateG11LiveProviderError(f"provider reset failed for episode {index}")
        episode = _Episode(
            index, generated, environment, public, dict(info), context, client
        )
        episodes.append(episode)
        timelines.append(_boundary(episode, time.monotonic_ns()))

    control_period_ns = int(workload["control_period_ms"]) * 1_000_000
    cycle_durations: list[int] = []
    cycle_overruns = 0
    campaign_started_ns = time.monotonic_ns()
    executor = ThreadPoolExecutor(
        max_workers=len(episodes), thread_name_prefix="wp11-provider-client"
    )
    try:
        cycle = 0
        while any(episode.active for episode in episodes):
            cycle_started_ns = time.monotonic_ns()
            for episode in episodes:
                _collect_response(episode, responses)
            for episode in episodes:
                if episode.active and episode.future is None:
                    _submit(episode, executor)
            for episode in episodes:
                if not episode.active:
                    continue
                action = _current_context_target(episode.observation)
                observation, reward, terminated, truncated, info = (
                    episode.environment.step(action)
                )
                episode.observation = [float(value) for value in observation]
                episode.reward = float(reward)
                episode.info = dict(info)
                if episode.context.update(episode.observation):
                    episode.context_changes += 1
                episode.active = not (bool(terminated) or bool(truncated))
                if not episode.active:
                    episode.outcome = str(info.get("outcome", "unknown"))
                timelines.append(_boundary(episode, time.monotonic_ns()))
            cycle += 1
            next_cycle_ns = campaign_started_ns + cycle * control_period_ns
            remaining_ns = next_cycle_ns - time.monotonic_ns()
            if remaining_ns > 0:
                time.sleep(remaining_ns / 1_000_000_000.0)
            else:
                cycle_overruns += 1
            cycle_durations.append(time.monotonic_ns() - cycle_started_ns)
        for episode in episodes:
            _collect_response(episode, responses, wait=True)
    finally:
        executor.shutdown(wait=True, cancel_futures=False)
        for episode in episodes:
            episode.client.close()
            episode.environment.close()

    capture_record = {
        "schema_version": "airhockey.wp11.live_provider.capture.v1",
        "protocol_sha256": _sha256(PROTOCOL_PATH),
        "muesli_bt_revision": _source_revision(),
        "campaign_started_monotonic_ns": campaign_started_ns,
        "campaign_finished_monotonic_ns": time.monotonic_ns(),
        "provider": {
            "protocol_version": PROTOCOL_VERSION,
            "kind": protocol["provider"]["kind"],
            "teacher_id": protocol["provider"]["teacher_id"],
            "latency_injection": False,
            "workers": protocol["provider"]["workers"],
        },
        "workload": {
            "concurrent_episodes": len(episodes),
            "control_period_ms": workload["control_period_ms"],
            "cycle_overruns": cycle_overruns,
            "cycle_duration": _latency_summary(cycle_durations),
            "scripted_scene_changes": 0,
            "provider_outputs_applied": False,
        },
        "episodes": [
            {
                "session_id": f"episode-{episode.index:04d}",
                "shot_id": episode.generated.shot.shot_id,
                "shot_manifest_entry_sha256": semantic_sha256(
                    episode.generated.as_dict()
                ),
                "outcome": episode.outcome,
                "save": episode.outcome in SAVE_OUTCOMES,
                "context_changes": episode.context_changes,
            }
            for episode in episodes
        ],
        "context_timeline": timelines,
        "provider_records": sorted(responses, key=lambda value: value["request_id"]),
    }
    write_json(output / "capture.json", capture_record)
    return capture_record


def _boundary_at_or_after(
    timeline: list[dict[str, Any]], timestamp_ns: int
) -> tuple[int, dict[str, Any]] | None:
    for index, boundary in enumerate(timeline):
        if int(boundary["monotonic_ns"]) >= timestamp_ns:
            return index, boundary
    return None


def replay_capture(
    capture_record: dict[str, Any], protocol: dict[str, Any]
) -> dict[str, Any]:
    timelines: dict[str, list[dict[str, Any]]] = {}
    for boundary in capture_record["context_timeline"]:
        timelines.setdefault(boundary["session_id"], []).append(boundary)
    for timeline in timelines.values():
        timeline.sort(
            key=lambda value: (value["monotonic_ns"], value["observation_step"])
        )

    deadline_ns = int(protocol["authority"]["deadline_ms"]) * 1_000_000
    source_age_limit = int(protocol["authority"]["source_age_limit_steps"])
    replays: dict[str, list[dict[str, Any]]] = {policy: [] for policy in POLICIES}
    natural_before = 0
    natural_after = 0
    errors = 0
    invalid_outputs = 0
    end_to_end: list[int] = []
    queue_latencies: list[int] = []
    inference_latencies: list[int] = []

    for record in capture_record["provider_records"]:
        response = record["response"]
        if not response.get("ok"):
            errors += 1
            continue
        try:
            validate_action(response["action"])
        except ProviderError:
            invalid_outputs += 1
        delivery_ns = int(response["client_received_monotonic_ns"])
        end_to_end.append(delivery_ns - int(record["request_created_monotonic_ns"]))
        queue_latencies.append(
            int(response["server_started_monotonic_ns"])
            - int(response["server_received_monotonic_ns"])
        )
        inference_latencies.append(
            int(response["server_finished_monotonic_ns"])
            - int(response["server_started_monotonic_ns"])
        )
        timeline = timelines[record["session_id"]]
        located = _boundary_at_or_after(timeline, delivery_ns)
        admission_index, admission = located if located is not None else (-1, None)
        dispatch = (
            timeline[admission_index + 1]
            if admission is not None and admission_index + 1 < len(timeline)
            else None
        )
        admission_context = admission["context_id"] if admission is not None else None
        dispatch_context = dispatch["context_id"] if dispatch is not None else None
        context_current_at_admission = (
            admission_context == record["captured_context_id"]
        )
        context_current_at_dispatch = dispatch_context == record["captured_context_id"]
        if admission is not None and not context_current_at_admission:
            natural_before += 1
        if (
            admission is not None
            and context_current_at_admission
            and dispatch is not None
            and not context_current_at_dispatch
        ):
            natural_after += 1
        timely = end_to_end[-1] <= deadline_ns
        source_age = (
            int(admission["observation_step"]) - int(record["source_observation_step"])
            if admission is not None
            else source_age_limit + 1
        )
        common_admission = (
            admission is not None
            and bool(admission["episode_active"])
            and timely
            and source_age <= source_age_limit
            and response.get("ok") is True
        )
        for policy in POLICIES:
            context_admission_required = policy != "deadline_only"
            admitted = common_admission and (
                context_current_at_admission or not context_admission_required
            )
            dispatch_owner_live = dispatch is not None and bool(
                dispatch["episode_active"]
            )
            context_dispatch_required = policy == "invocation_scoped_two_gate"
            capability_call = (
                admitted
                and dispatch_owner_live
                and (context_current_at_dispatch or not context_dispatch_required)
            )
            obsolete_admission = admitted and not context_current_at_admission
            obsolete_dispatch = capability_call and not context_current_at_dispatch
            row = {
                "policy": policy,
                "request_id": record["request_id"],
                "provider_record_sha256": record["record_sha256"],
                "captured_context_id": record["captured_context_id"],
                "admission_context_id": admission_context,
                "dispatch_context_id": dispatch_context,
                "timely": timely,
                "source_age_steps": source_age,
                "admitted": admitted,
                "capability_call": capability_call,
                "obsolete_admission": obsolete_admission,
                "obsolete_dispatch": obsolete_dispatch,
                "reason": (
                    "owner_inactive"
                    if admission is None or not bool(admission["episode_active"])
                    else "deadline_exceeded"
                    if not timely
                    else "source_too_old"
                    if source_age > source_age_limit
                    else "context_changed_at_admission"
                    if context_admission_required and not context_current_at_admission
                    else "owner_inactive_at_dispatch"
                    if not dispatch_owner_live
                    else "context_changed_at_dispatch"
                    if context_dispatch_required and not context_current_at_dispatch
                    else "dispatched"
                ),
            }
            row["decision_sha256"] = semantic_sha256(row)
            replays[policy].append(row)

    policy_summaries: dict[str, Any] = {}
    for policy, rows in replays.items():
        current_controls = [
            row
            for row in rows
            if row["captured_context_id"]
            == row["admission_context_id"]
            == row["dispatch_context_id"]
        ]
        policy_summaries[policy] = {
            "records": len(rows),
            "admissions": sum(bool(row["admitted"]) for row in rows),
            "capability_calls": sum(bool(row["capability_call"]) for row in rows),
            "obsolete_admissions": sum(bool(row["obsolete_admission"]) for row in rows),
            "obsolete_dispatches": sum(bool(row["obsolete_dispatch"]) for row in rows),
            "capability_calls_after_authority_loss": sum(
                bool(row["obsolete_dispatch"]) for row in rows
            ),
            "valid_no_change_controls": len(current_controls),
            "valid_no_change_dispatches": sum(
                bool(row["capability_call"]) for row in current_controls
            ),
            "valid_no_change_dispatch_rate": (
                sum(bool(row["capability_call"]) for row in current_controls)
                / len(current_controls)
                if current_controls
                else 0.0
            ),
            "decision_set_sha256": semantic_sha256(rows),
        }

    return {
        "schema_version": "airhockey.wp11.authority_replay.v1",
        "capture_sha256": semantic_sha256(capture_record),
        "policies": policy_summaries,
        "natural_authority_loss": {
            "before_admission": natural_before,
            "after_admission": natural_after,
        },
        "provider": {
            "responses": len(capture_record["provider_records"]),
            "errors": errors,
            "invalid_outputs": invalid_outputs,
            "end_to_end_latency": _latency_summary(end_to_end),
            "queue_latency": _latency_summary(queue_latencies),
            "inference_latency": _latency_summary(inference_latencies),
        },
        "decisions": replays,
    }


def _report(
    capture_record: dict[str, Any], replay: dict[str, Any], protocol: dict[str, Any]
) -> dict[str, Any]:
    episodes = capture_record["episodes"]
    measurements = {
        "completed_episodes": len(episodes),
        "provider_responses": replay["provider"]["responses"],
        "provider_errors": replay["provider"]["errors"],
        "invalid_provider_outputs": replay["provider"]["invalid_outputs"],
        "natural_context_changes": sum(
            int(value["context_changes"]) for value in episodes
        ),
        "episodes_with_context_change": sum(
            int(value["context_changes"]) > 0 for value in episodes
        ),
        "capture_controller_saves": sum(bool(value["save"]) for value in episodes),
        "capture_controller_save_rate": sum(bool(value["save"]) for value in episodes)
        / len(episodes),
        "latency_injections": 0,
        "scripted_scene_changes": 0,
        "privileged_provider_inputs": 0,
        "record_replay_hash_mismatches": 0,
        "policy_replay_hash_mismatches": 0,
        "two_gate_obsolete_dispatches": replay["policies"][
            "invocation_scoped_two_gate"
        ]["obsolete_dispatches"],
    }
    gate = protocol["gate"]
    failures: list[str] = []
    if measurements["completed_episodes"] < gate["minimum_completed_episodes"]:
        failures.append("completed episodes")
    if measurements["provider_responses"] < gate["minimum_provider_responses"]:
        failures.append("provider responses")
    for metric, gate_name in (
        ("provider_errors", "maximum_provider_errors"),
        ("latency_injections", "maximum_latency_injections"),
        ("scripted_scene_changes", "maximum_scripted_scene_changes"),
        ("privileged_provider_inputs", "maximum_privileged_provider_inputs"),
        ("record_replay_hash_mismatches", "maximum_record_replay_hash_mismatches"),
        ("policy_replay_hash_mismatches", "maximum_policy_replay_hash_mismatches"),
        ("invalid_provider_outputs", "maximum_invalid_provider_outputs"),
        ("two_gate_obsolete_dispatches", "maximum_two_gate_obsolete_dispatches"),
    ):
        if measurements[metric] > gate[gate_name]:
            failures.append(metric.replace("_", " "))
    return {
        "schema_version": "airhockey.wp11.report.v1",
        "status": "passed" if not failures else "failed",
        "protocol_sha256": _sha256(PROTOCOL_PATH),
        "capture_file_sha256": semantic_sha256(capture_record),
        "replay_file_sha256": semantic_sha256(replay),
        "measurements": measurements,
        "natural_authority_loss": replay["natural_authority_loss"],
        "provider": replay["provider"],
        "policies": replay["policies"],
        "failures": failures,
    }


def run_campaign(args: argparse.Namespace) -> dict[str, Any]:
    output = args.output.resolve()
    if output.exists() and any(output.iterdir()):
        raise GateG11LiveProviderError(
            f"refuse to replace non-empty WP11 output: {output}"
        )
    output.mkdir(parents=True, exist_ok=True)
    (output / RUN_MARKER).write_text("airhockey.wp11.run.v1\n", encoding="utf-8")
    protocol = load_protocol(
        acra_root=args.acra_root.resolve(),
        experiment_root=args.experiment_root.resolve(),
    )
    shutil.copy2(PROTOCOL_PATH, output / "wp11-protocol.json")
    process: subprocess.Popen[Any] | None = None
    stdout_handle: Any = None
    stderr_handle: Any = None
    try:
        process, readiness, stdout_handle, stderr_handle = _start_provider(
            protocol=protocol,
            acra_root=args.acra_root.resolve(),
            experiment_root=args.experiment_root.resolve(),
            output=output,
        )
        warmup = _warm_provider(protocol, readiness)
        write_json(
            output / "provider-warmup.json",
            {"excluded_from_capture": True, "responses": warmup},
        )
        capture_record = capture(protocol=protocol, readiness=readiness, output=output)
    finally:
        if process is not None and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=15)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=15)
        if stdout_handle is not None:
            stdout_handle.close()
        if stderr_handle is not None:
            stderr_handle.close()
    replay = replay_capture(capture_record, protocol)
    write_json(output / "authority-replay.json", replay)
    repeated = replay_capture(capture_record, protocol)
    if semantic_sha256(replay) != semantic_sha256(repeated):
        raise GateG11LiveProviderError("WP11 authority replay is not deterministic")
    report = _report(capture_record, replay, protocol)
    write_json(output / "wp11-report.json", report)
    if report["status"] != "passed":
        raise GateG11LiveProviderError(f"WP11 failed: {', '.join(report['failures'])}")
    return report


def seal(campaign: Path, backup: Path, seal_report: Path) -> dict[str, Any]:
    campaign = campaign.resolve()
    backup = backup.resolve()
    seal_report = seal_report.resolve()
    if not (campaign / RUN_MARKER).is_file():
        raise GateG11LiveProviderError("refuse to seal an unmarked WP11 campaign")
    report = read_json(campaign / "wp11-report.json")
    if report.get("status") != "passed":
        raise GateG11LiveProviderError("refuse to seal a failed WP11 campaign")
    if backup.exists() or seal_report.exists():
        raise GateG11LiveProviderError(
            "refuse to replace an existing WP11 backup or seal report"
        )
    if any(path.is_symlink() for path in campaign.rglob("*")):
        raise GateG11LiveProviderError(
            "refuse to seal a WP11 campaign containing symlinks"
        )
    manifest = campaign / "sha256sums.txt"
    lines = []
    for path in sorted(
        value for value in campaign.rglob("*") if value.is_file() and value != manifest
    ):
        lines.append(f"{_sha256(path)}  {path.relative_to(campaign).as_posix()}")
    manifest.write_text("\n".join(lines) + "\n", encoding="utf-8")
    backup.parent.mkdir(parents=True, exist_ok=True)
    with tarfile.open(backup, "w:gz") as archive:
        archive.add(campaign, arcname=campaign.name, recursive=True)
    result = {
        "schema_version": "airhockey.wp11.seal.v1",
        "campaign": str(campaign),
        "protocol_sha256": report["protocol_sha256"],
        "checksum_entries": len(lines),
        "checksum_manifest_sha256": _sha256(manifest),
        "backup": str(backup),
        "backup_sha256": _sha256(backup),
    }
    write_json(seal_report, result)
    return result


def main() -> None:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)
    subparsers.add_parser("check-protocol")
    run_parser = subparsers.add_parser("run")
    run_parser.add_argument("--acra-root", type=Path, required=True)
    run_parser.add_argument("--experiment-root", type=Path, required=True)
    run_parser.add_argument("--output", type=Path, required=True)
    replay_parser = subparsers.add_parser("replay")
    replay_parser.add_argument("--capture", type=Path, required=True)
    replay_parser.add_argument("--output", type=Path, required=True)
    seal_parser = subparsers.add_parser("seal")
    seal_parser.add_argument("--campaign", type=Path, required=True)
    seal_parser.add_argument("--backup", type=Path, required=True)
    seal_parser.add_argument("--seal-report", type=Path, required=True)
    args = parser.parse_args()
    if args.command == "check-protocol":
        protocol = load_protocol()
        print(
            "air-hockey WP11 protocol passed: "
            f"{protocol['workload']['concurrent_episodes']} live episodes, no injected latency or scripted scene changes"
        )
    elif args.command == "run":
        print(json.dumps(run_campaign(args), indent=2, sort_keys=True))
    elif args.command == "replay":
        protocol = load_protocol()
        capture_record = read_json(args.capture)
        replay = replay_capture(capture_record, protocol)
        write_json(args.output, replay)
        print(json.dumps(replay["policies"], indent=2, sort_keys=True))
    elif args.command == "seal":
        print(
            json.dumps(
                seal(args.campaign, args.backup, args.seal_report),
                indent=2,
                sort_keys=True,
            )
        )


if __name__ == "__main__":
    main()
