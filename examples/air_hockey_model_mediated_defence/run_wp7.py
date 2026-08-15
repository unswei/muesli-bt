#!/usr/bin/env python3
"""Run, validate and seal the frozen air-hockey Gate G7 paper campaign."""

from __future__ import annotations

import argparse
import csv
import hashlib
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile
import time
from collections import Counter
from pathlib import Path
from typing import Any

from jsonschema import Draft202012Validator

EXAMPLE_ROOT = Path(__file__).resolve().parent
REPOSITORY_ROOT = EXAMPLE_ROOT.parents[1]
HOST_ROOT = EXAMPLE_ROOT / "host"
sys.path.insert(0, str(HOST_ROOT / "src"))

from analysis.evidence import (
    DERIVED_ARTEFACTS,
    RUN_MARKER,
    EvidenceError,
    RecordedProviderReplay,
    analyse_run,
    campaign_summary,
    file_sha256,
    guarded_publish,
    read_json,
    read_jsonl,
    semantic_sha256,
    validate_campaign_report,
    write_json,
    write_jsonl,
)
from muesli_air_hockey_host import (
    FakeDirectLaunchBackend,
    MujocoDirectLaunchHostBackend,
    ProtocolProcessor,
    SchemaRegistry,
    UnixHostServer,
)
from provider.adapters import AcraExportProvider
from run_wp6 import (
    _observed_predicates,
    _timing_records,
    _timing_summary,
    _validate_public_boundary,
)

PROTOCOL_PATH = EXAMPLE_ROOT / "configs" / "wp7_protocol.json"
PROTOCOL_SCHEMA = (
    REPOSITORY_ROOT
    / "schemas"
    / "air_hockey_integration"
    / "v1"
    / "airhockey.wp7.protocol.v1.schema.json"
)
RUN_ROOT_MARKER = ".air-hockey-g7-run"
SAVE_OUTCOMES = frozenset({"returned", "arrested", "safe_deflection"})
RAW_NAMES = (
    "events.jsonl",
    "task-trajectory.jsonl",
    "recorded-provider.jsonl",
    "replay-events.jsonl",
    "replay-task-trajectory.jsonl",
    "direct-replay.json",
    "replay-direct-replay.json",
    "runner.stdout",
    "replay-runner.stdout",
)


class GateG7Error(EvidenceError):
    """A paper campaign invariant failed before evidence promotion."""


def _sha256(path: Path) -> str:
    return file_sha256(path).removeprefix("sha256:")


def load_protocol() -> dict[str, Any]:
    protocol = read_json(PROTOCOL_PATH)
    schema = read_json(PROTOCOL_SCHEMA)
    Draft202012Validator.check_schema(schema)
    errors = sorted(
        Draft202012Validator(schema).iter_errors(protocol),
        key=lambda error: list(error.path),
    )
    if errors:
        where = "/".join(str(value) for value in errors[0].path) or "<root>"
        raise GateG7Error(f"WP7 protocol {where}: {errors[0].message}")

    if _sha256(EXAMPLE_ROOT / "configs" / "wp6_protocol.json") != protocol[
        "parent_wp6_protocol_sha256"
    ]:
        raise GateG7Error("the frozen WP6 parent protocol changed")
    split = protocol["paper_split"]
    if _sha256(EXAMPLE_ROOT / split["distribution_config"]) != split[
        "distribution_config_sha256"
    ]:
        raise GateG7Error("the WP7 distribution configuration changed")
    for definition in protocol["behaviour_trees"].values():
        if _sha256(EXAMPLE_ROOT / definition["path"]) != definition["sha256"]:
            raise GateG7Error("a frozen WP7 Behaviour Tree changed")

    trial = protocol["paired_trial"]
    for schedule in protocol["deterministic_provider"]["delay_schedule"]:
        if not trial["context_change_step"] * protocol["control_period_ms"] <= schedule[
            "delay_ms"
        ] < protocol["deadline_ms"]:
            raise GateG7Error("a deterministic completion is not stale and unexpired")
    learned = protocol["learned_provider"]
    if not trial["context_change_step"] * protocol["control_period_ms"] <= learned[
        "delay_ms"
    ] < protocol["deadline_ms"]:
        raise GateG7Error("the learned completion is not stale and unexpired")

    shots = split["expected_shots"]
    deterministic_pairs = shots * len(
        protocol["deterministic_provider"]["delay_schedule"]
    )
    learned_pairs = learned["subset_shots"]
    if (
        deterministic_pairs != protocol["campaign"]["deterministic_pairs"]
        or learned_pairs != protocol["campaign"]["learned_pairs"]
        or deterministic_pairs + learned_pairs
        != protocol["campaign"]["total_pairs"]
        or 2 * (deterministic_pairs + learned_pairs)
        != protocol["campaign"]["total_runs"]
    ):
        raise GateG7Error("WP7 campaign cardinalities do not reconcile")
    return protocol


def _source_revision() -> str:
    marker = REPOSITORY_ROOT / ".muesli-bt-source-revision"
    if marker.is_file():
        return marker.read_text(encoding="utf-8").strip()
    return subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=REPOSITORY_ROOT,
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()


def _prepare_output(output: Path) -> Path:
    output = output.resolve()
    if output == output.parent:
        raise GateG7Error("Gate G7 output cannot be a filesystem root")
    if output.exists() and any(output.iterdir()):
        raise GateG7Error(f"refuse to replace non-empty Gate G7 output: {output}")
    output.mkdir(parents=True, exist_ok=True)
    (output / RUN_ROOT_MARKER).write_text("airhockey.g7.run.v1\n", encoding="utf-8")
    (output / "runs").mkdir()
    (output / "analysis").mkdir()
    return output


def _paper_shots(protocol: dict[str, Any]) -> tuple[Any, ...]:
    try:
        from airhockey_distill.envs import (
            load_direct_launch_distribution,
            manifest_sha256,
        )
    except Exception as error:
        raise GateG7Error("pinned airhockey_distill package is unavailable") from error

    split = protocol["paper_split"]
    distribution = load_direct_launch_distribution(
        EXAMPLE_ROOT / split["distribution_config"]
    )
    shots = distribution.generate(split["name"])
    if len(shots) != split["expected_shots"]:
        raise GateG7Error("paper shot count changed")
    if manifest_sha256(shots) != split["manifest_sha256"]:
        raise GateG7Error("paper shot manifest changed")
    if any(generated.split != "muesli_test" for generated in shots):
        raise GateG7Error("a non-paper shot crossed into Gate G7")
    return shots


def _shot_key(generated: Any) -> str:
    return hashlib.sha256(generated.shot.shot_id.encode("utf-8")).hexdigest()


def select_learned_subset(shots: tuple[Any, ...], count: int) -> tuple[Any, ...]:
    if count <= 0 or count > len(shots):
        raise GateG7Error("invalid learned subset size")
    return tuple(sorted(shots, key=lambda value: (_shot_key(value), value.shot.shot_id))[:count])


def _initial_observation(generated: Any, action_lock_steps: int) -> list[float]:
    try:
        from airhockey_distill.envs import (
            BlackoutSchedule,
            DefendShotTrackingLoss,
            MujocoDirectLaunchBackend,
        )
    except Exception as error:
        raise GateG7Error("MuJoCo paper environment is unavailable") from error

    environment = DefendShotTrackingLoss(
        MujocoDirectLaunchBackend(),
        blackout=BlackoutSchedule(start_observation_step=1, length_steps=1),
        timeout_steps=125,
        action_lock_steps=action_lock_steps,
    )
    try:
        observation, _ = environment.reset(shot=generated.shot, seed=6303)
        return [float(value) for value in observation]
    finally:
        environment.close()


def _deterministic_action(observation: list[float]) -> list[float]:
    if len(observation) != 19:
        raise GateG7Error("deterministic provider received the wrong observation size")
    return [max(-1.0, min(1.0, float(observation[index]))) for index in (16, 17)]


def _provider_action(
    provider: AcraExportProvider,
    observation: list[float],
) -> tuple[list[float], int]:
    provider.reset()
    started_ns = time.perf_counter_ns()
    action = provider.infer(observation)
    elapsed_ns = time.perf_counter_ns() - started_ns
    return action, elapsed_ns


def _expected_predicates(policy: str) -> set[str]:
    return {
        "p7_deadline_only_obsolete_dispatch"
        if policy == "deadline_only"
        else "p7_invocation_scoped_context_rejection",
        "p7_provider_mode_matches",
        "p7_task_episode_completed",
    }


def _run_native_half(
    *,
    executable: Path,
    tree: Path,
    scenario: str,
    run_id: str,
    action: list[float],
    paper: dict[str, Any],
    delay_ms: int,
    action_lock_steps: int,
    generated: Any,
    events_path: Path,
    replay: bool,
) -> dict[str, Any]:
    backend = MujocoDirectLaunchHostBackend(
        shot_factory=lambda shot=generated.shot: shot
    )
    schemas = SchemaRegistry(REPOSITORY_ROOT / "schemas" / "air_hockey_host" / "v1")
    processor = ProtocolProcessor(schemas, backend)
    with tempfile.TemporaryDirectory(prefix="m7-") as directory:
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
            with UnixHostServer(socket_path, processor):
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
        raise GateG7Error(
            f"{scenario} exited with {completed.returncode}: {completed.stderr.strip()}"
        )
    observed = _observed_predicates(completed.stdout)
    policy = "deadline_only" if "deadline-only" in scenario else "invocation_scoped"
    if observed != _expected_predicates(policy):
        backend.shutdown()
        raise GateG7Error(
            f"{scenario} predicate mismatch: {sorted(observed)}"
        )
    records = backend.evaluation_records()
    _validate_public_boundary(records)
    direct_replay = backend.direct_replay_report()
    backend.shutdown()
    if not direct_replay["passed"] or direct_replay["steps"] != len(records):
        raise GateG7Error("direct MuJoCo replay did not cover the complete episode")
    events = read_jsonl(events_path)
    from analysis.evidence import _validate_events

    _validate_events(events, events_path, run_id)
    timing = _timing_records(completed.stdout)
    return {
        "stdout": completed.stdout,
        "stderr": completed.stderr,
        "records": records,
        "direct_replay": direct_replay,
        "timing": timing,
    }


def check_native(executable: Path) -> None:
    """Exercise both WP7 policies and provider modes without opening muesli_test."""
    protocol = load_protocol()
    executable = executable.resolve()
    if not executable.is_file() or not os.access(executable, os.X_OK):
        raise GateG7Error(f"WP7 scenario runner is not executable: {executable}")
    schemas = SchemaRegistry(REPOSITORY_ROOT / "schemas" / "air_hockey_host" / "v1")
    with tempfile.TemporaryDirectory(prefix="muesli-g7-check-") as directory:
        root = Path(directory)
        for policy in protocol["paired_trial"]["policies"]:
            scenario = (
                "P7-deadline-only"
                if policy == "deadline_only"
                else "P7-invocation-scoped"
            )
            for replay in (False, True):
                backend = FakeDirectLaunchBackend()
                processor = ProtocolProcessor(schemas, backend)
                run_id = f"wp7-check-{policy}-{'replay' if replay else 'live'}"
                events = root / f"{run_id}.jsonl"
                socket_path = root / "host.sock"
                command = [
                    str(executable),
                    scenario,
                    str(socket_path),
                    str(EXAMPLE_ROOT / protocol["behaviour_trees"][policy]["path"]),
                    str(events),
                    run_id,
                    "0.25",
                    "-0.4",
                    str(protocol["paired_trial"]["blackout_start_step"]),
                    str(protocol["paired_trial"]["blackout_length_steps"]),
                    "8",
                    "0",
                    "60",
                    "replay" if replay else "live",
                ]
                with UnixHostServer(socket_path, processor):
                    completed = subprocess.run(
                        command,
                        check=False,
                        capture_output=True,
                        text=True,
                        timeout=30,
                    )
                if completed.returncode != 0:
                    raise GateG7Error(
                        f"{run_id} exited with {completed.returncode}: "
                        f"{completed.stderr.strip()}"
                    )
                if _observed_predicates(completed.stdout) != _expected_predicates(
                    policy
                ):
                    raise GateG7Error(f"{run_id} predicate mismatch")
                from analysis.evidence import _validate_events

                _validate_events(read_jsonl(events), events, run_id)
    print(
        "air-hockey WP7 native smoke passed without opening muesli_test: "
        "2 policies x live/exact-replay",
        flush=True,
    )


def check_mujoco(executable: Path) -> None:
    """Exercise WP7 against one engineering shot without opening muesli_test."""
    from run_wp6 import _engineering_shots
    from run_wp6 import load_protocol as load_wp6_protocol

    protocol = load_protocol()
    generated = _engineering_shots(load_wp6_protocol())[0]
    observation = _initial_observation(
        generated, protocol["deterministic_provider"]["action_lock_steps"]
    )
    action = _deterministic_action(observation)
    executable = executable.resolve()
    with tempfile.TemporaryDirectory(prefix="m7e-") as directory:
        root = Path(directory)
        for policy in protocol["paired_trial"]["policies"]:
            scenario = (
                "P7-deadline-only"
                if policy == "deadline_only"
                else "P7-invocation-scoped"
            )
            for replay in (False, True):
                mode = "replay" if replay else "live"
                run_id = f"wp7-engineering-{policy}-{mode}"
                result = _run_native_half(
                    executable=executable,
                    tree=EXAMPLE_ROOT
                    / protocol["behaviour_trees"][policy]["path"],
                    scenario=scenario,
                    run_id=run_id,
                    action=action,
                    paper=protocol["paired_trial"],
                    delay_ms=60,
                    action_lock_steps=protocol["deterministic_provider"][
                        "action_lock_steps"
                    ],
                    generated=generated,
                    events_path=root / f"{run_id}.jsonl",
                    replay=replay,
                )
                _trajectory(
                    run_id,
                    result["records"],
                    generated,
                    action,
                    policy,
                    protocol["paired_trial"]["context_change_step"],
                )
    print(
        "air-hockey WP7 MuJoCo smoke passed on one engineering shot without "
        "opening muesli_test: 2 policies x live/exact-replay",
        flush=True,
    )


def _paper_outcome(value: str) -> str:
    if value in SAVE_OUTCOMES:
        return "save"
    if value == "goal_conceded":
        return "concession"
    if value in {"timeout", "timeout_without_contact", "rollout_limit"}:
        return "timeout"
    return "other"


def _trajectory(
    run_id: str,
    records: list[dict[str, Any]],
    generated: Any,
    action: list[float],
    policy: str,
    context_change_step: int,
) -> list[dict[str, Any]]:
    if not records or not (
        records[-1]["public_state"]["terminated"]
        or records[-1]["public_state"]["truncated"]
    ):
        raise GateG7Error("paper trajectory did not reach a terminal record")
    rows: list[dict[str, Any]] = []
    for index, record in enumerate(records, start=1):
        state = record["public_state"]
        observation = state["observation"]
        final = index == len(records)
        outcome = (
            _paper_outcome(str(record["privileged"]["outcome"]))
            if final
            else "pending"
        )
        authorised = (
            list(action)
            if policy == "deadline_only"
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
                    "outcome": outcome,
                },
            }
        )
    return rows


def _recorded_provider(
    run_id: str,
    pair_id: str,
    observation: list[float],
    action: list[float],
    delay_ms: int,
) -> list[dict[str, Any]]:
    request = {
        "pair_id": pair_id,
        "captured_context_id": "episode-000001/track-0001",
        "source_observation_step": 0,
        "deadline_ms": 120,
        "delay_ms": delay_ms,
        "public_observation": observation,
    }
    action_record = {
        "type": "continuous",
        "frame": "airhockey.normalised_mallet_target.v1",
        "values": list(action),
        "dt_ms": 20,
    }
    return [
        {
            "schema_version": "airhockey.recorded_provider.v1",
            "run_id": run_id,
            "seq": 1,
            "request_sha256": semantic_sha256(request),
            "response_sha256": semantic_sha256(action_record),
            "job_id": "job-1",
            "generation": 1,
            "captured_context_id": "episode-000001/track-0001",
            "source_observation_step": 0,
            "action": action_record,
        }
    ]


def _manifest(
    *,
    staged: Path,
    run_id: str,
    pair_id: str,
    policy: str,
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
    delay_hash = semantic_sha256(delay)
    delay["sha256"] = delay_hash
    tree = protocol["behaviour_trees"][policy]
    expected = protocol["paired_trial"]["expected"][policy]
    return {
        "schema_version": "airhockey.run_manifest.v1",
        "run_id": run_id,
        "pair_id": pair_id,
        "capture_status": "paper_complete",
        "paper_eligible": True,
        "acceptance_policy": policy,
        "repositories": {
            "muesli_bt": {
                "present": True,
                "commit": _source_revision(),
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
            "delay_schedule_sha256": delay_hash,
            "seed": seed,
        },
        "expected_integrity": expected,
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
    policy: str,
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
    policy_slug = "deadline-only" if policy == "deadline_only" else "invocation-scoped"
    run_id = f"{pair_id}-{policy_slug}"
    replay_run_id = f"{run_id}-replay"
    scenario = (
        "P7-deadline-only"
        if policy == "deadline_only"
        else "P7-invocation-scoped"
    )
    tree = EXAMPLE_ROOT / protocol["behaviour_trees"][policy]["path"]
    output_root.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="air-hockey-wp7-stage-", dir=output_root) as temp:
        staged = Path(temp) / run_id
        staged.mkdir()
        (staged / RUN_MARKER).write_text(
            "airhockey.run_manifest.v1\n", encoding="utf-8"
        )
        live = _run_native_half(
            executable=executable,
            tree=tree,
            scenario=scenario,
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
            scenario=scenario,
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
                policy,
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
                policy,
                protocol["paired_trial"]["context_change_step"],
            ),
        )
        records = _recorded_provider(
            run_id, pair_id, observation, action, delay_ms
        )
        recorded = RecordedProviderReplay(records)
        if recorded.infer(records[0]["request_sha256"])["values"] != action:
            raise GateG7Error("recorded provider did not reproduce its exact action")
        write_jsonl(staged / "recorded-provider.jsonl", records)
        write_json(staged / "direct-replay.json", live["direct_replay"])
        write_json(staged / "replay-direct-replay.json", replay["direct_replay"])
        (staged / "runner.stdout").write_text(live["stdout"], encoding="utf-8")
        (staged / "replay-runner.stdout").write_text(
            replay["stdout"], encoding="utf-8"
        )
        manifest = _manifest(
            staged=staged,
            run_id=run_id,
            pair_id=pair_id,
            policy=policy,
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
        )
        write_json(staged / "manifest.json", manifest)
        published = guarded_publish(staged, output_root, run_id, force=False)
    analyse_run(published)
    return published


def _campaign_measurements(
    runs: Path,
    learned_inference_ns: list[int],
) -> dict[str, Any]:
    manifests: list[dict[str, Any]] = []
    summaries: list[dict[str, Any]] = []
    tick_ns: list[int] = []
    replay_mismatches = 0
    trace_failures = 0
    direct_replay_failures = 0
    for run_dir in sorted(path.parent for path in runs.glob(f"*/{RUN_MARKER}")):
        manifest = read_json(run_dir / "manifest.json")
        summary = read_json(run_dir / "trial-summary.json")
        manifests.append(manifest)
        summaries.append(summary)
        if not read_json(run_dir / "replay-report.json")["matched"]:
            replay_mismatches += 1
        if read_json(run_dir / "trace-validation.json")["status"] != "passed":
            trace_failures += 1
        if not read_json(run_dir / "direct-replay.json")["passed"]:
            direct_replay_failures += 1
        for timing in _timing_records((run_dir / "runner.stdout").read_text(encoding="utf-8")):
            tick_ns.extend(timing["tick_duration_ns"])

    missing_terminal = sum(
        summary["integrity"]["invocations_without_terminal_state"]
        for summary in summaries
    )
    reason_failures = sum(
        not summary["integrity"]["reason_code_agreement"] for summary in summaries
    )
    outcomes: dict[str, Counter[str]] = {
        "deterministic_deadline_only": Counter(),
        "deterministic_invocation_scoped": Counter(),
        "learned_deadline_only": Counter(),
        "learned_invocation_scoped": Counter(),
    }
    for manifest, summary in zip(manifests, summaries, strict=True):
        provider = "learned" if manifest["provider"]["kind"] == "acra_frozen" else "deterministic"
        key = f"{provider}_{manifest['acceptance_policy']}"
        outcomes[key][summary["task_outcome"]] += 1
    return {
        "runs": len(manifests),
        "missing_terminal_invocations": missing_terminal,
        "reason_code_failures": reason_failures,
        "replay_mismatches": replay_mismatches,
        "trace_failures": trace_failures,
        "direct_replay_failures": direct_replay_failures,
        "tick_timing": _timing_summary(tick_ns, 20.0),
        "learned_inference_timing": _timing_summary(learned_inference_ns),
        "outcome_counts": {
            key: dict(sorted(value.items())) for key, value in outcomes.items()
        },
    }


def _render_result_svg(report: dict[str, Any], path: Path) -> None:
    rows = report["table_rows"]
    width, height = 980, 330
    colours = {"baseline": "#b45309", "full": "#166534"}
    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        '<rect width="100%" height="100%" fill="#f8fafc"/>',
        '<text x="30" y="38" font-family="system-ui" font-size="22" fill="#0f172a">Air-hockey WP7 paired paper results</text>',
    ]
    for index, row in enumerate(rows):
        y = 90 + index * 78
        parts.append(
            f'<text x="30" y="{y}" font-family="system-ui" font-size="15" fill="#334155">{row["metric"]}</text>'
        )
        scale = 480 if row["metric"] != "projected motion towards obsolete target" else 3600
        for offset, key in ((0, "baseline"), (26, "full")):
            value = float(row[key])
            bar = max(1.0, min(400.0, abs(value) * scale))
            parts.append(
                f'<rect x="500" y="{y - 18 + offset}" width="{bar:.2f}" height="18" fill="{colours[key]}"/>'
                f'<text x="910" y="{y - 4 + offset}" text-anchor="end" font-family="ui-monospace" font-size="13" fill="#0f172a">{key}: {value:.4f}</text>'
            )
    parts.append("</svg>\n")
    path.write_text("".join(parts), encoding="utf-8")


def _render_trace_figure(
    baseline: list[dict[str, Any]],
    full: list[dict[str, Any]],
    pair_id: str,
    path: Path,
) -> None:
    def projection(rows: list[dict[str, Any]]) -> tuple[int, int, int]:
        change = next(
            row["observation_step"]
            for row in rows
            if row["public"]["defence_context_id"]
            != rows[0]["public"]["defence_context_id"]
        )
        authorised = sum(row["public"]["authorised_target"] is not None for row in rows)
        applied = sum(
            row["public"]["applied_target"]
            == row["privileged"]["privileged_intercept_target"]
            for row in rows
        )
        return change, authorised, applied

    b = projection(baseline)
    f = projection(full)
    text = [
        '<svg xmlns="http://www.w3.org/2000/svg" width="1100" height="330" viewBox="0 0 1100 330">',
        '<rect width="100%" height="100%" fill="#0f172a"/>',
        '<text x="28" y="38" fill="#e2e8f0" font-family="system-ui" font-size="21">Representative authority trace</text>',
        f'<text x="28" y="65" fill="#94a3b8" font-family="ui-monospace" font-size="13">{pair_id}</text>',
    ]
    for index, (label, values, colour) in enumerate(
        (("deadline_only", b, "#f59e0b"), ("invocation_scoped", f, "#22c55e"))
    ):
        y = 115 + index * 105
        text.extend(
            [
                f'<text x="28" y="{y}" fill="{colour}" font-family="system-ui" font-size="17">{label}</text>',
                f'<line x1="220" y1="{y - 6}" x2="1010" y2="{y - 6}" stroke="#475569" stroke-width="4"/>',
                f'<circle cx="390" cy="{y - 6}" r="10" fill="#38bdf8"/><text x="390" y="{y + 22}" text-anchor="middle" fill="#cbd5e1" font-family="ui-monospace" font-size="12">context change step {values[0]}</text>',
                f'<circle cx="650" cy="{y - 6}" r="10" fill="{colour}"/><text x="650" y="{y + 22}" text-anchor="middle" fill="#cbd5e1" font-family="ui-monospace" font-size="12">authorised steps {values[1]}</text>',
                f'<circle cx="900" cy="{y - 6}" r="10" fill="{colour}"/><text x="900" y="{y + 22}" text-anchor="middle" fill="#cbd5e1" font-family="ui-monospace" font-size="12">matching command steps {values[2]}</text>',
            ]
        )
    text.append("</svg>\n")
    path.write_text("".join(text), encoding="utf-8")


def _render_video(
    baseline: list[dict[str, Any]],
    full: list[dict[str, Any]],
    output: Path,
    fps: int,
    overlay: bool,
) -> None:
    try:
        from PIL import Image, ImageDraw
    except Exception as error:
        raise GateG7Error("Pillow is required for the representative video") from error

    frames = max(len(baseline), len(full))
    with tempfile.TemporaryDirectory(prefix="muesli-g7-video-") as directory:
        frame_root = Path(directory)
        for index in range(frames):
            image = Image.new("RGB", (1280, 480), (15, 23, 42))
            draw = ImageDraw.Draw(image)
            for panel, (label, rows, x0) in enumerate(
                (("deadline_only", baseline, 20), ("invocation_scoped", full, 650))
            ):
                del panel
                row = rows[min(index, len(rows) - 1)]
                public = row["public"]
                draw.rounded_rectangle((x0, 50, x0 + 610, 445), radius=12, fill=(30, 41, 59))
                draw.text((x0 + 20, 68), label, fill=(241, 245, 249))
                draw.rectangle((x0 + 55, 120, x0 + 555, 390), outline=(100, 116, 139), width=3)

                def point(
                    values: list[float], x_origin: int = x0
                ) -> tuple[int, int]:
                    return (
                        int(
                            x_origin
                            + 305
                            + max(-1.0, min(1.0, values[0])) * 235
                        ),
                        int(255 - max(-1.0, min(1.0, values[1])) * 120),
                    )

                puck = point(public["visible_puck_position"])
                mallet = point(public["mallet_position"])
                draw.ellipse((puck[0] - 9, puck[1] - 9, puck[0] + 9, puck[1] + 9), fill=(56, 189, 248))
                draw.ellipse((mallet[0] - 14, mallet[1] - 14, mallet[0] + 14, mallet[1] + 14), fill=(248, 250, 252))
                target = public["authorised_target"]
                if target is not None:
                    target_point = point(target)
                    draw.line((mallet, target_point), fill=(245, 158, 11), width=4)
                    draw.ellipse((target_point[0] - 6, target_point[1] - 6, target_point[0] + 6, target_point[1] + 6), fill=(245, 158, 11))
                if overlay:
                    draw.text((x0 + 20, 405), f"step={row['observation_step']} context={public['defence_context_id']}", fill=(203, 213, 225))
                    draw.text((x0 + 20, 425), f"outcome={row['privileged']['outcome']} authorised={target is not None}", fill=(203, 213, 225))
            image.save(frame_root / f"frame-{index:04d}.png")
        subprocess.run(
            [
                "ffmpeg",
                "-hide_banner",
                "-loglevel",
                "error",
                "-y",
                "-framerate",
                str(fps),
                "-i",
                str(frame_root / "frame-%04d.png"),
                "-c:v",
                "libx264",
                "-pix_fmt",
                "yuv420p",
                str(output),
            ],
            check=True,
        )


def _write_analysis_outputs(
    output: Path,
    report: dict[str, Any],
    measurements: dict[str, Any],
    representative_pair: str,
    runs: Path,
    protocol: dict[str, Any],
) -> None:
    analysis = output / "analysis"
    write_json(analysis / "campaign-summary.json", report)
    write_json(
        analysis / "campaign-plot-fields.json",
        {
            "schema_version": "airhockey.campaign_plot_fields.v1",
            "raw_provenance_sha256": report["raw_provenance_sha256"],
            "plot_fields": report["plot_fields"],
        },
    )
    write_json(analysis / "measurements.json", measurements)
    with (analysis / "result-table.csv").open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=("metric", "baseline", "full", "difference"))
        writer.writeheader()
        writer.writerows(report["table_rows"])
    latex_rows = [
        "Metric & Deadline-only & Invocation-scoped & Difference \\\\",
        "\\midrule",
    ]
    for row in report["table_rows"]:
        latex_rows.append(
            f"{row['metric']} & {row['baseline']:.4f} & {row['full']:.4f} & {row['difference']:.4f} \\\\")
    (analysis / "result-table.tex").write_text("\n".join(latex_rows) + "\n", encoding="utf-8")
    _render_result_svg(report, analysis / "paper-results.svg")

    baseline_dir = runs / f"{representative_pair}-deadline-only"
    full_dir = runs / f"{representative_pair}-invocation-scoped"
    baseline = read_jsonl(baseline_dir / "task-trajectory.jsonl")
    full = read_jsonl(full_dir / "task-trajectory.jsonl")
    write_json(
        analysis / "representative-pair.json",
        {
            "selection": protocol["campaign"]["representative_pair_selection"],
            "pair_id": representative_pair,
            "baseline_run_id": baseline_dir.name,
            "full_run_id": full_dir.name,
        },
    )
    _render_trace_figure(baseline, full, representative_pair, analysis / "trace-figure.svg")
    _render_video(
        baseline,
        full,
        analysis / "representative-raw.mp4",
        protocol["campaign"]["video_fps"],
        overlay=False,
    )
    _render_video(
        baseline,
        full,
        analysis / "representative-overlay.mp4",
        protocol["campaign"]["video_fps"],
        overlay=True,
    )


def run_campaign(
    executable: Path,
    checkpoint: Path,
    output: Path,
    image: str,
    image_digest: str,
) -> dict[str, Any]:
    protocol = load_protocol()
    executable = executable.resolve()
    checkpoint = checkpoint.resolve()
    if not executable.is_file() or not os.access(executable, os.X_OK):
        raise GateG7Error(f"paper scenario runner is not executable: {executable}")
    if not checkpoint.is_file() or _sha256(checkpoint) != protocol["learned_provider"]["checkpoint_sha256"]:
        raise GateG7Error("the selected learned checkpoint is missing or changed")
    output = _prepare_output(output)
    shutil.copy2(PROTOCOL_PATH, output / "wp7-protocol.json")
    shots = _paper_shots(protocol)
    write_json(
        output / "paper-manifest.json",
        {
            "schema_version": "airhockey.g7.paper_manifest.v1",
            "split": "muesli_test",
            "manifest_sha256": protocol["paper_split"]["manifest_sha256"],
            "shots": [generated.as_dict() for generated in shots],
        },
    )

    learned_definition = protocol["learned_provider"]
    learned_provider = AcraExportProvider(
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
        raise GateG7Error("learned checkpoint metadata changed")

    runs = output / "runs"
    pair_count = 0
    deterministic_pairs: list[str] = []
    for shot_index, generated in enumerate(shots):
        observation = _initial_observation(
            generated, protocol["deterministic_provider"]["action_lock_steps"]
        )
        action = _deterministic_action(observation)
        shot_slug = f"q{shot_index:03d}-{_shot_key(generated)[:8]}"
        for schedule in protocol["deterministic_provider"]["delay_schedule"]:
            pair_id = f"p7-det-s{schedule['seed']}-{shot_slug}"
            deterministic_pairs.append(pair_id)
            provider_configuration = {
                **protocol["deterministic_provider"],
                "selected_seed": schedule["seed"],
                "selected_delay_ms": schedule["delay_ms"],
            }
            for policy in protocol["paired_trial"]["policies"]:
                _capture_run(
                    executable=executable,
                    output_root=runs,
                    generated=generated,
                    pair_id=pair_id,
                    policy=policy,
                    observation=observation,
                    action=action,
                    provider_kind="deterministic",
                    provider_identity=protocol["deterministic_provider"]["identity"],
                    provider_configuration=provider_configuration,
                    checkpoint_sha256=None,
                    seed=schedule["seed"],
                    delay_ms=schedule["delay_ms"],
                    action_lock_steps=protocol["deterministic_provider"]["action_lock_steps"],
                    protocol=protocol,
                    image=image,
                    image_digest=image_digest,
                )
            pair_count += 1
            print(
                f"WP7 deterministic pair {pair_count}/{protocol['campaign']['total_pairs']} passed: {pair_id}",
                flush=True,
            )

    learned_inference_ns: list[int] = []
    for generated in select_learned_subset(shots, learned_definition["subset_shots"]):
        observation = _initial_observation(generated, learned_definition["action_lock_steps"])
        action, elapsed_ns = _provider_action(learned_provider, observation)
        learned_inference_ns.append(elapsed_ns)
        pair_id = f"p7-learned-s{learned_definition['delay_seed']}-{_shot_key(generated)[:12]}"
        for policy in protocol["paired_trial"]["policies"]:
            _capture_run(
                executable=executable,
                output_root=runs,
                generated=generated,
                pair_id=pair_id,
                policy=policy,
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
            f"WP7 learned pair {pair_count}/{protocol['campaign']['total_pairs']} passed: {pair_id}",
            flush=True,
        )

    report = campaign_summary(runs)
    validate_campaign_report(report)
    measurements = _campaign_measurements(runs, learned_inference_ns)
    baseline_failures = report["integrity_intervals"]["baseline_obsolete_dispatch"]["successes"]
    full_failures = report["integrity_intervals"]["full_obsolete_dispatch"]["successes"]
    gate = protocol["gate"]
    measured = {
        "schema_version": "airhockey.g7.report.v1",
        "status": "measured",
        "muesli_bt_revision": _source_revision(),
        "acra_revision": protocol["repositories"]["acra_revision"],
        "protocol_sha256": _sha256(PROTOCOL_PATH),
        "paper_manifest_sha256": protocol["paper_split"]["manifest_sha256"],
        "paper_split_opened": True,
        "paper_split": "muesli_test",
        "pairs": report["pair_count"],
        "runs": report["run_count"],
        "baseline_obsolete_dispatch_pairs": baseline_failures,
        "full_obsolete_dispatch_pairs": full_failures,
        "measurements": measurements,
        "campaign_summary_sha256": semantic_sha256(report),
    }
    write_json(output / "g7-measured.json", measured)
    failures = []
    if report["pair_count"] != protocol["campaign"]["total_pairs"] or report["run_count"] != protocol["campaign"]["total_runs"]:
        failures.append("campaign cardinality")
    if baseline_failures != gate["required_baseline_obsolete_dispatch_pairs"]:
        failures.append("deadline-only obsolete dispatch exposure")
    if full_failures > gate["maximum_full_obsolete_dispatch_pairs"]:
        failures.append("invocation-scoped obsolete dispatch")
    if measurements["missing_terminal_invocations"] > gate["maximum_missing_terminal_invocations"]:
        failures.append("missing terminal invocation")
    if measurements["reason_code_failures"]:
        failures.append("reason-code agreement")
    if measurements["replay_mismatches"] > gate["maximum_replay_mismatches"] or measurements["direct_replay_failures"]:
        failures.append("replay")
    if measurements["trace_failures"] > gate["maximum_trace_failures"]:
        failures.append("trace validation")
    if measurements["tick_timing"]["p99_ms"] > gate["maximum_tick_p99_ms"]:
        failures.append("BT tick p99")
    if measurements["learned_inference_timing"]["p95_ms"] > gate["maximum_learned_p95_inference_ms"]:
        failures.append("learned inference p95")
    if failures:
        raise GateG7Error(f"Gate G7 failed: {', '.join(failures)}")

    representative_pair = min(deterministic_pairs)
    _write_analysis_outputs(
        output, report, measurements, representative_pair, runs, protocol
    )
    result = {
        **measured,
        "status": "passed",
        "representative_pair_id": representative_pair,
        "protocol_frozen": True,
        "raw_bundles_read_only": False,
        "backup_verified": False,
    }
    write_json(output / "g7-report.json", result)
    print(
        "air-hockey Gate G7 passed: 228 matched paper pairs, 456 validated runs, "
        "frozen protocol and exact replay",
        flush=True,
    )
    return result


def _checksum_manifest(root: Path, output: Path) -> None:
    rows = []
    for path in sorted(value for value in root.rglob("*") if value.is_file() and value != output):
        rows.append(f"{_sha256(path)}  {path.relative_to(root).as_posix()}\n")
    output.write_text("".join(rows), encoding="utf-8")


def seal_campaign(campaign: Path, backup: Path, seal_report: Path) -> dict[str, Any]:
    campaign = campaign.resolve()
    backup = backup.resolve()
    seal_report = seal_report.resolve()
    if not (campaign / RUN_ROOT_MARKER).is_file():
        raise GateG7Error("refuse to seal an unmarked campaign")
    report = read_json(campaign / "g7-report.json")
    if report.get("status") != "passed":
        raise GateG7Error("refuse to seal a campaign that did not pass Gate G7")
    if backup.exists() or seal_report.exists():
        raise GateG7Error("refuse to replace an existing WP7 backup or seal report")
    backup.parent.mkdir(parents=True, exist_ok=True)
    seal_report.parent.mkdir(parents=True, exist_ok=True)
    checksums = campaign / "checksums.sha256"
    _checksum_manifest(campaign, checksums)
    with tarfile.open(backup, "w:gz", compresslevel=6) as archive:
        archive.add(campaign, arcname=campaign.name, recursive=True)
    backup_sha256 = _sha256(backup)
    with tarfile.open(backup, "r:gz") as archive:
        members = archive.getmembers()
        if not members or not any(member.name.endswith("/g7-report.json") for member in members):
            raise GateG7Error("WP7 backup verification failed")
    for path in sorted(campaign.rglob("*"), reverse=True):
        path.chmod(0o555 if path.is_dir() else 0o444)
    campaign.chmod(0o555)
    seal = {
        "schema_version": "airhockey.g7.seal.v1",
        "status": "sealed",
        "campaign": str(campaign),
        "campaign_checksum_manifest_sha256": _sha256(checksums),
        "backup": str(backup),
        "backup_sha256": backup_sha256,
        "backup_verified": True,
        "file_mode": "0444",
        "directory_mode": "0555",
    }
    write_json(seal_report, seal)
    seal_report.chmod(0o444)
    backup.chmod(0o444)
    return seal


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    subparsers.add_parser("check-protocol", help="validate WP7 without opening the paper split")
    native = subparsers.add_parser(
        "check-native", help="smoke-test WP7 policies without opening the paper split"
    )
    native.add_argument("--runner", required=True, type=Path)
    mujoco = subparsers.add_parser(
        "check-mujoco",
        help="smoke-test WP7 on an engineering MuJoCo shot without opening the paper split",
    )
    mujoco.add_argument("--runner", required=True, type=Path)
    run = subparsers.add_parser("run", help="open and run the frozen paper campaign")
    run.add_argument("--runner", required=True, type=Path)
    run.add_argument("--checkpoint", required=True, type=Path)
    run.add_argument("--out", required=True, type=Path)
    run.add_argument("--image", required=True)
    run.add_argument("--image-digest", required=True)
    seal = subparsers.add_parser("seal", help="checksum, back up and make a passed campaign read-only")
    seal.add_argument("--campaign", required=True, type=Path)
    seal.add_argument("--backup", required=True, type=Path)
    seal.add_argument("--seal-report", required=True, type=Path)
    arguments = parser.parse_args()
    if arguments.command == "check-protocol":
        protocol = load_protocol()
        print(
            "air-hockey WP7 protocol passed without opening muesli_test: "
            f"{protocol['campaign']['total_pairs']} pairs, "
            f"{protocol['campaign']['total_runs']} runs"
        )
        return 0
    if arguments.command == "check-native":
        check_native(arguments.runner)
        return 0
    if arguments.command == "check-mujoco":
        check_mujoco(arguments.runner)
        return 0
    if arguments.command == "run":
        run_campaign(
            arguments.runner,
            arguments.checkpoint,
            arguments.out,
            arguments.image,
            arguments.image_digest,
        )
        return 0
    if arguments.command == "seal":
        result = seal_campaign(arguments.campaign, arguments.backup, arguments.seal_report)
        print(f"air-hockey Gate G7 sealed: {result['backup_sha256']}")
        return 0
    raise GateG7Error(f"unsupported command: {arguments.command}")


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except EvidenceError as error:
        raise SystemExit(f"error: {error}") from error
