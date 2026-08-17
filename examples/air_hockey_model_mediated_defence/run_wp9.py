#!/usr/bin/env python3
"""Run the frozen public-observation context-token sensitivity campaign."""

from __future__ import annotations

import argparse
import copy
import csv
import hashlib
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
from analysis.evidence import (
    EvidenceError,
    clopper_pearson,
    event_projection,
    file_sha256,
    read_json,
    read_jsonl,
    semantic_sha256,
    write_json,
    write_jsonl,
)
from run_wp6 import _timing_summary

EXAMPLE_ROOT = Path(__file__).resolve().parent
REPOSITORY_ROOT = EXAMPLE_ROOT.parents[1]
PROTOCOL_PATH = EXAMPLE_ROOT / "configs" / "wp9_context_sensitivity_protocol.json"
RUN_ROOT_MARKER = ".air-hockey-wp9-run"
EXPECTED_PREDICATES = {
    "p9_context_policy_effect_matches_token",
    "p9_context_policy_terminal_once",
    "p9_context_policy_episode_completed",
}


class GateG9SensitivityError(EvidenceError):
    """A frozen WP9 sensitivity invariant failed."""


def _sha256(path: Path) -> str:
    return file_sha256(path).removeprefix("sha256:")


def _load_protocol() -> tuple[dict[str, Any], dict[str, Any]]:
    protocol = read_json(PROTOCOL_PATH)
    if protocol.get("status") != "frozen_before_context_sensitivity_campaign":
        raise GateG9SensitivityError("WP9 context sensitivity protocol is not frozen")
    parent_path = EXAMPLE_ROOT / protocol["parent_wp8_protocol"]
    if _sha256(parent_path) != protocol["parent_wp8_protocol_sha256"]:
        raise GateG9SensitivityError("the frozen WP8 parent protocol changed")
    tree = EXAMPLE_ROOT / protocol["paired_trial"]["behaviour_tree"]
    if _sha256(tree) != protocol["paired_trial"]["behaviour_tree_sha256"]:
        raise GateG9SensitivityError("the frozen WP9 Behaviour Tree changed")
    if protocol["context_observation"].get("privileged_state"):
        raise GateG9SensitivityError("WP9 context policy must not use privileged state")
    policies = protocol.get("policies", [])
    if [row.get("id") for row in policies] != [
        "reacquisition_identity",
        "public_displacement_0_10",
        "public_displacement_0_20",
    ]:
        raise GateG9SensitivityError("WP9 context policy order changed")
    parent = wp8._load_wp8()
    if protocol["paper_split"]["manifest_sha256"] != parent["paper_split"][
        "manifest_sha256"
    ]:
        raise GateG9SensitivityError("WP9 changed the frozen paper split")
    return protocol, parent


def context_invalidates(policy: dict[str, Any], displacement: float) -> bool:
    if policy["kind"] == "always_new_context_on_reacquisition":
        return True
    if policy["kind"] != "new_context_above_public_displacement":
        raise GateG9SensitivityError(f"unknown WP9 policy kind: {policy['kind']}")
    threshold = float(policy["displacement_threshold"])
    return displacement > threshold


class SensitivityBackend(wp7.MujocoDirectLaunchHostBackend):
    """Apply one frozen public-only equivalence rule at reacquisition."""

    def __init__(self, generated: Any, policy: dict[str, Any]) -> None:
        super().__init__(shot_factory=lambda shot=generated.shot: shot)
        self._context_policy = dict(policy)
        self._submission_target: tuple[float, float] | None = None
        self.reacquisition_displacement: float | None = None
        self.context_invalidations = 0

    def _reset(self, payload: dict[str, Any]) -> dict[str, Any]:
        result = super()._reset(payload)
        observation = result["state"]["observation"]
        self._submission_target = (float(observation[16]), float(observation[17]))
        self.reacquisition_displacement = None
        self.context_invalidations = 0
        return result

    def _advance(self) -> dict[str, Any]:
        previous_visible = self._previous_visible
        previous_track = self._track_number
        result = super()._advance()
        state = result["state"]
        reacquired = not previous_visible and bool(state["puck_visible"])
        if not reacquired:
            return result
        if self._submission_target is None:
            raise GateG9SensitivityError("WP9 backend omitted the submission target")
        observation = state["observation"]
        current = (float(observation[16]), float(observation[17]))
        displacement = math.dist(self._submission_target, current)
        self.reacquisition_displacement = displacement
        invalidates = context_invalidates(self._context_policy, displacement)
        if invalidates:
            self.context_invalidations += 1
        else:
            self._track_number = previous_track
        corrected = self._public_state()
        result["state"] = corrected
        self._evaluation[-1]["public_state"] = copy.deepcopy(corrected)
        return result


class _NativeCheckBackend(wp7.FakeDirectLaunchBackend):
    """Exercise both terminal branches without MuJoCo."""

    def __init__(self, invalidate_reacquisition: bool) -> None:
        super().__init__()
        self._invalidate_reacquisition = invalidate_reacquisition
        self.trace: list[dict[str, Any]] = []
        self.advance_error: str | None = None

    def handle(self, request: dict[str, Any]) -> dict[str, Any]:
        response = super().handle(request)
        self.trace.append({"request": request, "response": response})
        return response

    def _advance(self) -> dict[str, Any]:
        try:
            previous_visible = self._previous_visible
            previous_track = self._track_number
            result = super()._advance()
            if (
                not previous_visible
                and bool(result["state"]["puck_visible"])
                and not self._invalidate_reacquisition
            ):
                self._track_number = previous_track
                result["state"] = self._public_state()
            return result
        except Exception as error:
            self.advance_error = repr(error)
            raise


def _prepare_output(output: Path) -> Path:
    output = output.resolve()
    if output == Path(output.anchor):
        raise GateG9SensitivityError("WP9 output cannot be a filesystem root")
    if output.exists() and any(output.iterdir()):
        raise GateG9SensitivityError(f"refuse to replace non-empty WP9 output: {output}")
    output.mkdir(parents=True, exist_ok=True)
    (output / RUN_ROOT_MARKER).write_text("airhockey.wp9.run.v1\n", encoding="utf-8")
    return output


def _checksum_manifest(root: Path, output: Path) -> None:
    rows = []
    for path in sorted(
        value for value in root.rglob("*") if value.is_file() and value != output
    ):
        if path.is_symlink():
            raise GateG9SensitivityError(
                "refuse to seal a WP9 campaign containing symlinks"
            )
        rows.append(f"{_sha256(path)}  {path.relative_to(root).as_posix()}\n")
    output.write_text("".join(rows), encoding="utf-8")


def seal_campaign(campaign: Path, backup: Path, seal_report: Path) -> dict[str, Any]:
    campaign = campaign.resolve()
    backup = backup.resolve()
    seal_report = seal_report.resolve()
    if campaign == Path(campaign.anchor):
        raise GateG9SensitivityError("refuse to seal a filesystem root")
    if not (campaign / RUN_ROOT_MARKER).is_file():
        raise GateG9SensitivityError("refuse to seal an unmarked WP9 campaign")
    if any(path.is_symlink() for path in campaign.rglob("*")):
        raise GateG9SensitivityError(
            "refuse to seal a WP9 campaign containing symlinks"
        )
    for destination in (backup, seal_report):
        try:
            destination.relative_to(campaign)
        except ValueError:
            pass
        else:
            raise GateG9SensitivityError(
                "WP9 backup and seal report must be outside the campaign"
            )
    if backup == seal_report:
        raise GateG9SensitivityError(
            "WP9 backup and seal report must be different files"
        )

    report_path = campaign / "wp9-report.json"
    summary_path = campaign / "context-sensitivity-summary.json"
    report = read_json(report_path)
    if report.get("status") != "passed":
        raise GateG9SensitivityError(
            "refuse to seal a WP9 campaign that did not pass"
        )
    if report.get("protocol_sha256") != _sha256(PROTOCOL_PATH):
        raise GateG9SensitivityError(
            "WP9 campaign protocol does not match the frozen protocol"
        )
    summary = read_json(summary_path)
    if semantic_sha256(summary) != report.get("summary_sha256"):
        raise GateG9SensitivityError(
            "WP9 campaign summary changed after validation"
        )
    if report.get("policy_runs") != summary.get("policy_runs"):
        raise GateG9SensitivityError("WP9 report and summary cardinalities differ")
    if report.get("privileged_policy_inputs") != 0:
        raise GateG9SensitivityError(
            "refuse to seal a WP9 campaign with privileged policy inputs"
        )
    if backup.exists() or seal_report.exists():
        raise GateG9SensitivityError(
            "refuse to replace an existing WP9 backup or seal report"
        )

    checksums = campaign / "checksums.sha256"
    if checksums.exists():
        raise GateG9SensitivityError(
            "refuse to replace an existing WP9 checksum manifest"
        )
    backup.parent.mkdir(parents=True, exist_ok=True)
    seal_report.parent.mkdir(parents=True, exist_ok=True)
    _checksum_manifest(campaign, checksums)
    with tarfile.open(backup, "w:gz", compresslevel=6) as archive:
        archive.add(campaign, arcname=campaign.name, recursive=True)
    backup_sha256 = _sha256(backup)
    with tarfile.open(backup, "r:gz") as archive:
        member_names = {member.name for member in archive.getmembers()}
        required_suffixes = (
            "/wp9-report.json",
            "/context-sensitivity-summary.json",
            "/checksums.sha256",
        )
        if not all(
            any(name.endswith(suffix) for name in member_names)
            for suffix in required_suffixes
        ):
            raise GateG9SensitivityError("WP9 backup verification failed")

    for path in sorted(campaign.rglob("*"), reverse=True):
        path.chmod(0o555 if path.is_dir() else 0o444)
    campaign.chmod(0o555)
    seal = {
        "schema_version": "airhockey.wp9.seal.v1",
        "status": "sealed",
        "campaign": str(campaign),
        "campaign_report_sha256": _sha256(report_path),
        "campaign_checksum_manifest_sha256": _sha256(checksums),
        "backup": str(backup),
        "backup_sha256": backup_sha256,
        "backup_verified": True,
        "raw_bundles_read_only": True,
        "file_mode": "0444",
        "directory_mode": "0555",
    }
    write_json(seal_report, seal)
    seal_report.chmod(0o444)
    backup.chmod(0o444)
    return seal


def check_native(executable: Path) -> None:
    protocol, _ = _load_protocol()
    tree = EXAMPLE_ROOT / protocol["paired_trial"]["behaviour_tree"]
    schemas = wp7.SchemaRegistry(REPOSITORY_ROOT / "schemas" / "air_hockey_host" / "v1")
    for invalidate in (True, False):
        backend = _NativeCheckBackend(invalidate)
        with tempfile.TemporaryDirectory(prefix="wp9-native-") as directory:
            root = Path(directory)
            socket_path = root / "host.sock"
            events_path = root / "events.jsonl"
            processor = wp7.ProtocolProcessor(schemas, backend)
            with wp7.UnixHostServer(socket_path, processor):
                completed = subprocess.run(
                    [
                        str(executable),
                        "P9-context-sensitivity",
                        str(socket_path),
                        str(tree),
                        str(events_path),
                        f"wp9-native-{'changed' if invalidate else 'equivalent'}",
                        "0.8",
                        "-0.4",
                        "1",
                        "1",
                        "125",
                        "0",
                        "50",
                        "live",
                    ],
                    check=False,
                    capture_output=True,
                    text=True,
                    timeout=20,
                )
        if completed.returncode != 0:
            raise GateG9SensitivityError(
                f"WP9 native check failed: {completed.stderr.strip()}; "
                f"advance error: {backend.advance_error}; "
                f"last host exchanges: {backend.trace[-4:]}"
            )
        marker = _sensitivity_marker(completed.stdout)
        if bool(marker["context_changed"]) != invalidate:
            raise GateG9SensitivityError("WP9 native context branch mismatch")
        if set(
            line.removeprefix("PREDICATE ").removesuffix(" PASS")
            for line in completed.stdout.splitlines()
            if line.startswith("PREDICATE ") and line.endswith(" PASS")
        ) != EXPECTED_PREDICATES:
            raise GateG9SensitivityError("WP9 native predicate mismatch")
    print("air-hockey WP9 native context branches passed")


def _verify_source(
    campaign: Path, seal_report: Path, protocol: dict[str, Any]
) -> dict[str, Any]:
    campaign = campaign.resolve()
    seal_report = seal_report.resolve()
    report_path = campaign / "wp8-report.json"
    checksums_path = campaign / "checksums.sha256"
    if _sha256(report_path) != protocol["source_wp8_report_sha256"]:
        raise GateG9SensitivityError("the sealed WP8 report changed")
    if _sha256(checksums_path) != protocol["source_wp8_seal"][
        "checksum_manifest_sha256"
    ]:
        raise GateG9SensitivityError("the sealed WP8 checksum manifest changed")
    seal = read_json(seal_report)
    if seal.get("status") != "sealed" or not seal.get("backup_verified"):
        raise GateG9SensitivityError("WP8 source campaign is not sealed and verified")
    if seal.get("backup_sha256") != protocol["source_wp8_seal"]["backup_sha256"]:
        raise GateG9SensitivityError("the WP8 backup identity changed")
    failures = 0
    checks = 0
    for line in checksums_path.read_text(encoding="utf-8").splitlines():
        digest, relative = line.split("  ", 1)
        path = (campaign / relative).resolve()
        if campaign not in path.parents or len(digest) != 64:
            raise GateG9SensitivityError("unsafe WP8 checksum-manifest entry")
        checks += 1
        failures += int(_sha256(path) != digest)
    if failures:
        raise GateG9SensitivityError("sealed WP8 checksum verification failed")
    report = read_json(report_path)
    measurements = report["measurements"]
    source_failures = sum(
        int(measurements[name])
        for name in (
            "missing_terminal_invocations",
            "reason_code_failures",
            "duplicate_terminal_decisions",
            "duplicate_dispatches",
            "replay_mismatches",
            "trace_failures",
            "direct_replay_failures",
        )
    )
    if source_failures:
        raise GateG9SensitivityError("sealed WP8 source contains integrity failures")
    return {
        "campaign": str(campaign),
        "report_sha256": _sha256(report_path),
        "checksum_manifest_sha256": _sha256(checksums_path),
        "checksum_entries": checks,
        "checksum_failures": failures,
        "seal_report_sha256": _sha256(seal_report),
        "backup_sha256": seal["backup_sha256"],
    }


def _sensitivity_marker(stdout: str) -> dict[str, Any]:
    rows = [
        json.loads(line.removeprefix("CONTEXT_SENSITIVITY "))
        for line in stdout.splitlines()
        if line.startswith("CONTEXT_SENSITIVITY ")
    ]
    if len(rows) != 1:
        raise GateG9SensitivityError("WP9 runner did not emit one sensitivity marker")
    return rows[0]


def _public_trajectory(run_id: str, records: list[dict[str, Any]]) -> list[dict[str, Any]]:
    rows = []
    for record in records:
        state = record["public_state"]
        observation = state["observation"]
        rows.append(
            {
                "schema_version": "airhockey.wp9.public_trajectory.v1",
                "run_id": run_id,
                "observation_step": int(state["observation_step"]),
                "context_id": state["defence_context_id"],
                "puck_visible": bool(state["puck_visible"]),
                "public_mallet_target": [
                    float(observation[14]),
                    float(observation[15]),
                ],
                "public_puck_target": [
                    float(observation[16]),
                    float(observation[17]),
                ],
                "requested_target": [float(value) for value in record["requested_action"]],
                "applied_target": [float(value) for value in record["applied_action"]],
                "episode_active": bool(state["episode_active"]),
            }
        )
    return rows


def _capture_case(
    *,
    executable: Path,
    output_root: Path,
    generated: Any,
    case_id: str,
    policy: dict[str, Any],
    action: list[float],
    delay_ms: int,
    seed: int,
    protocol: dict[str, Any],
    image: str,
    image_digest: str,
) -> dict[str, Any]:
    policy_id = policy["id"]
    run_id = f"{case_id}-{policy_id.replace('_', '-')}"
    replay_run_id = f"{run_id}-replay"
    tree = EXAMPLE_ROOT / protocol["paired_trial"]["behaviour_tree"]
    output_root.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="wp9-stage-", dir=output_root) as directory:
        staged = Path(directory) / run_id
        staged.mkdir()
        live_backend = SensitivityBackend(generated, policy)
        live = wp8._run_native_half(
            executable=executable,
            tree=tree,
            scenario="P9-context-sensitivity",
            treatment="invocation_scoped_current_context_recovery",
            run_id=run_id,
            action=action,
            paper=protocol["paired_trial"],
            delay_ms=delay_ms,
            action_lock_steps=0,
            generated=generated,
            events_path=staged / "events.jsonl",
            replay=False,
            backend=live_backend,
            expected_predicates=EXPECTED_PREDICATES,
        )
        replay_backend = SensitivityBackend(generated, policy)
        replay = wp8._run_native_half(
            executable=executable,
            tree=tree,
            scenario="P9-context-sensitivity",
            treatment="invocation_scoped_current_context_recovery",
            run_id=replay_run_id,
            action=action,
            paper=protocol["paired_trial"],
            delay_ms=delay_ms,
            action_lock_steps=0,
            generated=generated,
            events_path=staged / "replay-events.jsonl",
            replay=True,
            backend=replay_backend,
            expected_predicates=EXPECTED_PREDICATES,
        )
        live_marker = _sensitivity_marker(live["stdout"])
        replay_marker = _sensitivity_marker(replay["stdout"])
        if live_marker != replay_marker:
            raise GateG9SensitivityError(f"WP9 marker replay mismatch: {run_id}")
        if event_projection(read_jsonl(staged / "events.jsonl")) != event_projection(
            read_jsonl(staged / "replay-events.jsonl")
        ):
            raise GateG9SensitivityError(f"WP9 event replay mismatch: {run_id}")
        displacement = live_backend.reacquisition_displacement
        if displacement is None or replay_backend.reacquisition_displacement != displacement:
            raise GateG9SensitivityError(f"WP9 displacement replay mismatch: {run_id}")
        expected_invalidation = context_invalidates(policy, displacement)
        if bool(live_marker["context_changed"]) != expected_invalidation:
            raise GateG9SensitivityError(f"WP9 context oracle mismatch: {run_id}")
        reference = float(
            protocol["context_observation"]["reference_usefulness_max_displacement"]
        )
        useful = displacement <= reference
        outcome = wp7._paper_outcome(
            str(live["records"][-1]["privileged"]["outcome"])
        )
        replay_outcome = wp7._paper_outcome(
            str(replay["records"][-1]["privileged"]["outcome"])
        )
        if outcome != replay_outcome:
            raise GateG9SensitivityError(f"WP9 outcome replay mismatch: {run_id}")
        write_jsonl(
            staged / "public-trajectory.jsonl",
            _public_trajectory(run_id, live["records"]),
        )
        write_jsonl(
            staged / "replay-public-trajectory.jsonl",
            _public_trajectory(replay_run_id, replay["records"]),
        )
        (staged / "runner.stdout").write_text(live["stdout"], encoding="utf-8")
        (staged / "replay-runner.stdout").write_text(
            replay["stdout"], encoding="utf-8"
        )
        write_json(staged / "direct-replay.json", live["direct_replay"])
        write_json(staged / "replay-direct-replay.json", replay["direct_replay"])
        result = {
            "schema_version": "airhockey.wp9.case_result.v1",
            "case_id": case_id,
            "run_id": run_id,
            "policy_id": policy_id,
            "seed": seed,
            "delay_ms": delay_ms,
            "public_reacquisition_displacement": displacement,
            "context_invalidations": live_backend.context_invalidations,
            "terminal_decision": live_marker["terminal"],
            "reason": live_marker["reason"],
            "reference_useful": useful,
            "obsolete_dispatch": not expected_invalidation and not useful,
            "reference_useful_rejection": expected_invalidation and useful,
            "task_outcome": outcome,
            "saved": outcome == "save",
            "replay_matched": True,
            "privileged_policy_inputs": 0,
        }
        write_json(staged / "case-result.json", result)
        raw_names = (
            "events.jsonl",
            "replay-events.jsonl",
            "public-trajectory.jsonl",
            "replay-public-trajectory.jsonl",
            "runner.stdout",
            "replay-runner.stdout",
            "direct-replay.json",
            "replay-direct-replay.json",
            "case-result.json",
        )
        write_json(
            staged / "manifest.json",
            {
                "schema_version": "airhockey.wp9.run_manifest.v1",
                "run_id": run_id,
                "case_id": case_id,
                "policy": policy,
                "protocol_sha256": _sha256(PROTOCOL_PATH),
                "muesli_bt_revision": wp7._source_revision(),
                "acra_revision": protocol["acra_revision"],
                "container": {
                    "image": image,
                    "digest": f"sha256:{image_digest.removeprefix('sha256:')}",
                },
                "public_policy_inputs": [16, 17, 18],
                "privileged_policy_inputs": [],
                "shot_manifest_entry_sha256": semantic_sha256(generated.as_dict()),
                "raw_artefacts": {
                    name: {"sha256": file_sha256(staged / name)} for name in raw_names
                },
            },
        )
        destination = output_root / run_id
        if destination.exists():
            raise GateG9SensitivityError(f"refuse to replace WP9 run: {run_id}")
        os.replace(staged, destination)
    result["tick_duration_ns"] = [
        value
        for timing in live["timing"]
        for value in timing["tick_duration_ns"]
    ]
    return result


def _summarise(results: list[dict[str, Any]], protocol: dict[str, Any]) -> dict[str, Any]:
    grouped: defaultdict[str, list[dict[str, Any]]] = defaultdict(list)
    for row in results:
        grouped[row["policy_id"]].append(row)
    policy_rows: dict[str, Any] = {}
    for policy in protocol["policies"]:
        rows = grouped[policy["id"]]
        total = len(rows)
        if total != protocol["campaign"]["matched_cases"]:
            raise GateG9SensitivityError(f"WP9 policy cardinality changed: {policy['id']}")
        invalidations = sum(row["context_invalidations"] for row in rows)
        obsolete = sum(bool(row["obsolete_dispatch"]) for row in rows)
        useful_rejections = sum(bool(row["reference_useful_rejection"]) for row in rows)
        saves = sum(bool(row["saved"]) for row in rows)
        policy_rows[policy["id"]] = {
            "cases": total,
            "context_invalidations": clopper_pearson(invalidations, total),
            "obsolete_dispatches": clopper_pearson(obsolete, total),
            "reference_useful_rejections": clopper_pearson(
                useful_rejections, total
            ),
            "saves": clopper_pearson(saves, total),
            "mean_public_reacquisition_displacement": sum(
                row["public_reacquisition_displacement"] for row in rows
            )
            / total,
        }
    tick_ns = [value for row in results for value in row.pop("tick_duration_ns")]
    return {
        "schema_version": "airhockey.wp9.context_sensitivity.summary.v1",
        "matched_cases": protocol["campaign"]["matched_cases"],
        "policy_runs": len(results),
        "reference_usefulness_max_displacement": protocol["context_observation"][
            "reference_usefulness_max_displacement"
        ],
        "policies": policy_rows,
        "tick_timing": _timing_summary(tick_ns, protocol["control_period_ms"]),
    }


def run_campaign(
    executable: Path,
    output: Path,
    source_campaign: Path,
    source_seal: Path,
    image: str,
    image_digest: str,
) -> dict[str, Any]:
    wp8._validate_thread_limits(dict(os.environ))
    protocol, parent = _load_protocol()
    protocol["acra_revision"] = parent["repositories"]["acra_revision"]
    source = _verify_source(source_campaign, source_seal, protocol)
    executable = executable.resolve()
    if not executable.is_file() or not os.access(executable, os.X_OK):
        raise GateG9SensitivityError(f"WP9 runner is not executable: {executable}")
    output = _prepare_output(output)
    shutil.copy2(PROTOCOL_PATH, output / "wp9-protocol.json")
    shots = wp7._paper_shots(parent)
    selected = wp7.select_learned_subset(
        shots, protocol["paper_split"]["selected_shots"]
    )
    schedules = parent["deterministic_provider"]["delay_schedule"]
    results: list[dict[str, Any]] = []
    completed = 0
    for generated in selected:
        observation = wp7._initial_observation(generated, 0)
        action = wp7._deterministic_action(observation)
        shot_key = wp7._shot_key(generated)[:12]
        for schedule in schedules:
            case_id = f"p9-s{schedule['seed']}-{shot_key}"
            for policy in protocol["policies"]:
                results.append(
                    _capture_case(
                        executable=executable,
                        output_root=output / "runs",
                        generated=generated,
                        case_id=case_id,
                        policy=policy,
                        action=action,
                        delay_ms=int(schedule["delay_ms"]),
                        seed=int(schedule["seed"]),
                        protocol=protocol,
                        image=image,
                        image_digest=image_digest,
                    )
                )
                completed += 1
                print(
                    f"WP9 policy run {completed}/{protocol['campaign']['policy_runs']} "
                    f"passed: {case_id} {policy['id']}",
                    flush=True,
                )
    summary = _summarise(results, protocol)
    failures = []
    if len(results) != protocol["campaign"]["policy_runs"]:
        failures.append("campaign cardinality")
    if any(not row["replay_matched"] for row in results):
        failures.append("replay")
    if sum(row["privileged_policy_inputs"] for row in results):
        failures.append("privileged policy input")
    if summary["tick_timing"]["p99_ms"] > protocol["gate"]["maximum_tick_p99_ms"]:
        failures.append("BT tick p99")
    if failures:
        raise GateG9SensitivityError(f"WP9 failed: {', '.join(failures)}")
    write_jsonl(output / "case-results.jsonl", results)
    write_json(output / "context-sensitivity-summary.json", summary)
    with (output / "result-table.csv").open("w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(
            (
                "policy",
                "cases",
                "invalidation_rate",
                "obsolete_dispatch_rate",
                "reference_useful_rejection_rate",
                "save_rate",
            )
        )
        for policy in protocol["policies"]:
            row = summary["policies"][policy["id"]]
            writer.writerow(
                (
                    policy["id"],
                    row["cases"],
                    row["context_invalidations"]["estimate"],
                    row["obsolete_dispatches"]["estimate"],
                    row["reference_useful_rejections"]["estimate"],
                    row["saves"]["estimate"],
                )
            )
    report = {
        "schema_version": "airhockey.wp9.report.v1",
        "status": "passed",
        "protocol_sha256": _sha256(PROTOCOL_PATH),
        "muesli_bt_revision": wp7._source_revision(),
        "acra_revision": parent["repositories"]["acra_revision"],
        "source_wp8": source,
        "matched_cases": summary["matched_cases"],
        "policy_runs": summary["policy_runs"],
        "public_policy_inputs": [16, 17, 18],
        "privileged_policy_inputs": 0,
        "summary_sha256": semantic_sha256(summary),
        "tick_timing": summary["tick_timing"],
        "raw_bundles_read_only": False,
        "backup_verified": False,
    }
    write_json(output / "wp9-report.json", report)
    print("air-hockey WP9 context sensitivity campaign passed", flush=True)
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "command", choices=("check-protocol", "check-native", "run", "seal")
    )
    parser.add_argument("--runner", type=Path)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--source-wp8-campaign", type=Path)
    parser.add_argument("--source-wp8-seal", type=Path)
    parser.add_argument("--image")
    parser.add_argument("--image-digest")
    parser.add_argument("--campaign", type=Path)
    parser.add_argument("--backup", type=Path)
    parser.add_argument("--seal-report", type=Path)
    arguments = parser.parse_args()
    protocol, _ = _load_protocol()
    if arguments.command == "check-protocol":
        print(
            "air-hockey WP9 protocol passed: "
            f"{protocol['campaign']['matched_cases']} matched cases, "
            f"{protocol['campaign']['policy_runs']} policy runs"
        )
        return 0
    if arguments.command == "check-native":
        if arguments.runner is None:
            parser.error("check-native requires --runner")
        check_native(arguments.runner.resolve())
        return 0
    if arguments.command == "seal":
        required = (arguments.campaign, arguments.backup, arguments.seal_report)
        if any(value is None for value in required):
            parser.error("seal requires --campaign, --backup and --seal-report")
        result = seal_campaign(
            arguments.campaign, arguments.backup, arguments.seal_report
        )
        print(f"air-hockey WP9 sealed: {result['backup_sha256']}")
        return 0
    required = (
        arguments.runner,
        arguments.out,
        arguments.source_wp8_campaign,
        arguments.source_wp8_seal,
        arguments.image,
        arguments.image_digest,
    )
    if any(value is None for value in required):
        parser.error(
            "run requires --runner, --out, --source-wp8-campaign, "
            "--source-wp8-seal, --image and --image-digest"
        )
    run_campaign(
        arguments.runner,
        arguments.out,
        arguments.source_wp8_campaign,
        arguments.source_wp8_seal,
        arguments.image,
        arguments.image_digest,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
