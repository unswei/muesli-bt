#!/usr/bin/env python3
"""Run the humanoid video trial matrix and write evidence bundles."""

from __future__ import annotations

import argparse
import collections
import datetime as dt
import hashlib
import json
import math
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
from typing import Any


EXAMPLE_ROOT = pathlib.Path(__file__).resolve().parent
REPO_ROOT = EXAMPLE_ROOT.parents[1]
CONFIG_ROOT = EXAMPLE_ROOT / "configs"
COMMON_CONFIG = CONFIG_ROOT / "common.json"
DEFAULT_TRIALS = (
    CONFIG_ROOT / "t1_normal_full.json",
    CONFIG_ROOT / "t2a_moved_ball_baseline.json",
    CONFIG_ROOT / "t2b_moved_ball_full.json",
    CONFIG_ROOT / "t3_emergency_full.json",
)
FROZEN_TRIAL_MATRIX: dict[str, dict[str, Any]] = {
    "T1": {
        "name": "normal-full",
        "acceptance_policy": "invocation_scoped",
        "bt": "lisp/bt_invocation_scoped.lisp",
        "intervention": "none",
        "evidence_manifest": "evidence/manifests/t1_normal_full.json",
        "configuration": "configs/t1_normal_full.json",
        "protocol_sha256": "6b04e0158eed73a893ab908d799ff63a1d1ebb6c8da790deb31bd07205c0854b",
        "evidence_checks": [
            "submission_initial_context",
            "accepted_current_result",
            "accepted_dispatch_once",
            "overlay_current_target",
        ],
        "expected": {
            "terminal_decision": "accepted",
            "reason": "",
            "accepted_dispatches": 1,
            "rejected_dispatches": 0,
            "authority_revocations": 0,
            "completion_drops": 0,
            "recording_dispatch_calls": 1,
            "active_branch": "model_execute",
        },
    },
    "T2a": {
        "name": "moved-ball-baseline",
        "acceptance_policy": "deadline_only",
        "bt": "lisp/bt_deadline_only.lisp",
        "intervention": "moved_ball",
        "evidence_manifest": "evidence/manifests/t2a_moved_ball_baseline.json",
        "configuration": "configs/t2a_moved_ball_baseline.json",
        "protocol_sha256": "ee82e350a575b3aef6f6d3247fc87a101ae5a4e04e6c443ceaf32f17dff239a0",
        "evidence_checks": [
            "context_changed_after_submit",
            "intervention_before_backend_completion",
            "baseline_accepts_stale_result",
            "host_rejects_stale_dispatch",
            "overlay_obsolete_candidate",
        ],
        "expected": {
            "terminal_decision": "accepted",
            "reason": "",
            "accepted_dispatches": 0,
            "rejected_dispatches": 1,
            "dispatch_reason": "context_changed",
            "authority_revocations": 0,
            "completion_drops": 0,
            "recording_dispatch_calls": 0,
            "active_branch": "fallback",
        },
    },
    "T2b": {
        "name": "moved-ball-full",
        "acceptance_policy": "invocation_scoped",
        "bt": "lisp/bt_invocation_scoped.lisp",
        "intervention": "moved_ball",
        "evidence_manifest": "evidence/manifests/t2b_moved_ball_full.json",
        "configuration": "configs/t2b_moved_ball_full.json",
        "protocol_sha256": "6217576897df6546a98beb91e81b68e455e701a30c8f36377700648f2e0a3bdd",
        "evidence_checks": [
            "context_changed_after_submit",
            "intervention_before_backend_completion",
            "full_rejects_changed_context",
            "correlated_rejected_candidate",
            "rejected_safe_wait",
            "overlay_obsolete_candidate",
            "no_walking_dispatch",
        ],
        "expected": {
            "terminal_decision": "rejected",
            "reason": "context_changed",
            "accepted_dispatches": 0,
            "rejected_dispatches": 0,
            "authority_revocations": 0,
            "completion_drops": 0,
            "recording_dispatch_calls": 0,
            "active_branch": "fallback",
        },
    },
    "T3": {
        "name": "emergency-full",
        "acceptance_policy": "invocation_scoped",
        "bt": "lisp/bt_invocation_scoped.lisp",
        "intervention": "emergency",
        "evidence_manifest": "evidence/manifests/t3_emergency_full.json",
        "configuration": "configs/t3_emergency_full.json",
        "protocol_sha256": "5ef8ef521d941afa1b92853c9cf9fa3cf0affe97529076d9283399bf01488c0a",
        "evidence_checks": [
            "safe_stand_on_intervention_tick",
            "intervention_before_backend_completion",
            "branch_authority_revoked",
            "late_completion_dropped",
            "overlay_revoked_no_target",
            "no_walking_dispatch",
        ],
        "expected": {
            "terminal_decision": "rejected",
            "reason": "branch_revoked",
            "accepted_dispatches": 0,
            "rejected_dispatches": 0,
            "authority_revocations": 1,
            "completion_drops": 1,
            "recording_dispatch_calls": 0,
            "active_branch": "safe_stand",
        },
    },
}
TRIAL_DOCUMENT_FIELDS = (
    "name",
    "acceptance_policy",
    "bt",
    "intervention",
    "evidence_manifest",
    "expected",
)
FROZEN_COMMON_CONTRACT_SHA256 = (
    "7c5b030a0577b98c5e00b8e11bef361d20751c737f4184b5f962e6bf48e0c8c8"
)
SAFE_COMPONENT = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$")
RUN_DIRECTORY_MARKER = ".humanoid-video-experiment-run"


class ExperimentError(RuntimeError):
    """A configuration, runner or evidence invariant failed."""


def read_json(path: pathlib.Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except Exception as exc:
        raise ExperimentError(f"failed to read JSON {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise ExperimentError(f"expected a JSON object: {path}")
    return value


def write_json(path: pathlib.Path, value: object) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def semantic_json_sha256(value: object) -> str:
    canonical = json.dumps(
        value, sort_keys=True, separators=(",", ":"), ensure_ascii=False
    ).encode("utf-8")
    return hashlib.sha256(canonical).hexdigest()


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return f"sha256:{digest.hexdigest()}"


def relative_path(path: pathlib.Path) -> str:
    try:
        return path.resolve().relative_to(REPO_ROOT).as_posix()
    except ValueError:
        return str(path.resolve())


def source_record(path: pathlib.Path) -> dict[str, str]:
    return {"path": relative_path(path), "sha256": sha256_file(path)}


def require_safe_component(value: object, field: str) -> str:
    if (
        not isinstance(value, str)
        or value in {".", ".."}
        or not SAFE_COMPONENT.fullmatch(value)
    ):
        raise ExperimentError(
            f"{field} must be a single basename using letters, digits, '.', '_' or '-'"
        )
    return value


def resolve_run_destination(
    out_root: pathlib.Path, run_id: str, force: bool
) -> tuple[pathlib.Path, pathlib.Path]:
    root = out_root.resolve()
    if root == root.parent:
        raise ExperimentError("the output root cannot be a filesystem root")
    try:
        root.mkdir(parents=True, exist_ok=True)
    except OSError as exc:
        raise ExperimentError(f"cannot prepare output root {root}: {exc}") from exc
    if not root.is_dir():
        raise ExperimentError(f"output root is not a directory: {root}")

    lexical_run_dir = root / require_safe_component(run_id, "run ID")
    if lexical_run_dir.is_symlink():
        raise ExperimentError(f"refusing a symlink run path: {lexical_run_dir}")
    run_dir = lexical_run_dir.resolve()
    if run_dir.parent != root or run_dir == root:
        raise ExperimentError(f"run directory must be a strict child of {root}")

    marker = run_dir / RUN_DIRECTORY_MARKER
    if run_dir.exists():
        if not force:
            raise ExperimentError(f"run directory already exists: {run_dir}")
        if run_dir.is_symlink() or not run_dir.is_dir():
            raise ExperimentError(f"refusing to replace a non-directory run path: {run_dir}")
        if not marker.is_file() or marker.read_text(encoding="utf-8") != "humanoid.video.run.v1\n":
            raise ExperimentError(
                f"refusing to replace an unmarked directory with --force: {run_dir}"
            )
    return root, run_dir


def create_run_directory(out_root: pathlib.Path, run_id: str) -> pathlib.Path:
    root, run_dir = resolve_run_destination(out_root, run_id, False)
    run_dir.mkdir()
    (run_dir / RUN_DIRECTORY_MARKER).write_text(
        "humanoid.video.run.v1\n", encoding="utf-8"
    )
    return run_dir


def publish_run_directory(staged: pathlib.Path, destination: pathlib.Path, force: bool) -> None:
    _, checked_destination = resolve_run_destination(
        destination.parent, destination.name, force
    )
    if checked_destination != destination:
        raise ExperimentError("run destination changed during staged capture")
    if not destination.exists():
        staged.replace(destination)
        return

    backup = pathlib.Path(
        tempfile.mkdtemp(prefix=f".{destination.name}.previous-", dir=destination.parent)
    )
    backup.rmdir()
    destination.replace(backup)
    try:
        staged.replace(destination)
    except Exception:
        backup.replace(destination)
        raise
    try:
        shutil.rmtree(backup)
    except OSError as exc:
        print(
            f"warning: published {destination}; previous bundle retained at {backup}: {exc}",
            file=sys.stderr,
        )


def check_run_directory_guards() -> None:
    for unsafe in ("", ".", "..", "../escape", "/tmp/escape", "name/child"):
        try:
            require_safe_component(unsafe, "self-test component")
        except ExperimentError:
            continue
        raise ExperimentError(f"run-directory guard accepted unsafe component: {unsafe!r}")

    with tempfile.TemporaryDirectory(prefix="muesli-humanoid-path-guard-") as tmp:
        root = pathlib.Path(tmp)
        unmarked = root / "unmarked"
        unmarked.mkdir()
        try:
            resolve_run_destination(root, "unmarked", True)
        except ExperimentError:
            pass
        else:
            raise ExperimentError("--force guard accepted an unmarked directory")

        marked = create_run_directory(root, "marked")
        sentinel = marked / "previous-evidence.txt"
        sentinel.write_text("preserve until publish\n", encoding="utf-8")
        _, checked = resolve_run_destination(root, "marked", True)
        if checked != marked or sentinel.read_text(encoding="utf-8") != "preserve until publish\n":
            raise ExperimentError("--force preflight modified the existing evidence bundle")

        try:
            publish_run_directory(root / "missing-staged-run", marked, True)
        except OSError:
            pass
        else:
            raise ExperimentError("staged publication guard did not exercise rollback")
        if sentinel.read_text(encoding="utf-8") != "preserve until publish\n":
            raise ExperimentError("failed staged publication did not restore previous evidence")

        staging_root = root / "staging"
        staging_root.mkdir()
        staged = create_run_directory(staging_root, "marked")
        (staged / "replacement-evidence.txt").write_text("validated\n", encoding="utf-8")
        publish_run_directory(staged, marked, True)
        if not (marked / "replacement-evidence.txt").is_file() or sentinel.exists():
            raise ExperimentError("successful staged publication did not replace previous evidence")


def check_native_runner_guards(runner_path: pathlib.Path) -> None:
    """Ensure a direct native invocation cannot skip a required intervention."""
    with tempfile.TemporaryDirectory(prefix="muesli-humanoid-native-guard-") as tmp:
        event_path = pathlib.Path(tmp) / "events.jsonl"
        completed = subprocess.run(
            [
                str(runner_path),
                "--tree",
                str(EXAMPLE_ROOT / "lisp" / "bt_invocation_scoped.lisp"),
                "--events",
                str(event_path),
                "--run-id",
                "native-timing-guard",
                "--trial-id",
                "T2b",
                "--acceptance-policy",
                "invocation_scoped",
                "--intervention",
                "moved_ball",
                "--delay-ms",
                "50",
                "--intervention-ms",
                "50",
            ],
            cwd=REPO_ROOT,
            check=False,
            capture_output=True,
            text=True,
        )
        if completed.returncode == 0 or event_path.exists():
            raise ExperimentError(
                "native timing guard accepted an intervention at or after completion"
            )
        if "intervention time must precede delayed completion" not in completed.stderr:
            raise ExperimentError(
                "native timing guard did not report the expected configuration error"
            )

        garbage_event_path = pathlib.Path(tmp) / "garbage-number-events.jsonl"
        garbage_number = subprocess.run(
            [
                str(runner_path),
                "--tree",
                str(EXAMPLE_ROOT / "lisp" / "bt_invocation_scoped.lisp"),
                "--events",
                str(garbage_event_path),
                "--run-id",
                "native-number-guard",
                "--trial-id",
                "T1",
                "--acceptance-policy",
                "invocation_scoped",
                "--intervention",
                "none",
                "--delay-ms",
                "50garbage",
            ],
            cwd=REPO_ROOT,
            check=False,
            capture_output=True,
            text=True,
        )
        if garbage_number.returncode == 0 or garbage_event_path.exists():
            raise ExperimentError("native numeric parser accepted a value suffix")
        if "expects a complete signed integer" not in garbage_number.stderr:
            raise ExperimentError("native numeric parser guard did not report the expected error")

        original_tree = (EXAMPLE_ROOT / "lisp" / "bt_invocation_scoped.lisp").read_text(
            encoding="utf-8"
        )
        drifted_tree = pathlib.Path(tmp) / "drifted-tree.lisp"
        drifted_tree.write_text(original_tree.replace(":dims 3", ":dims 4"), encoding="utf-8")
        drifted_event_path = pathlib.Path(tmp) / "drifted-events.jsonl"
        drifted = subprocess.run(
            [
                str(runner_path),
                "--tree",
                str(drifted_tree),
                "--events",
                str(drifted_event_path),
                "--run-id",
                "native-request-contract-guard",
                "--trial-id",
                "T1",
                "--acceptance-policy",
                "invocation_scoped",
                "--intervention",
                "none",
                "--delay-ms",
                "50",
            ],
            cwd=REPO_ROOT,
            check=False,
            capture_output=True,
            text=True,
        )
        if drifted.returncode == 0 or drifted_event_path.exists():
            raise ExperimentError("native request-contract guard accepted a changed action dimension")
        if "action-space parameters do not match configuration" not in drifted.stderr:
            raise ExperimentError("native request-contract guard did not report the expected error")

        extra_option_tree = pathlib.Path(tmp) / "extra-option-tree.lisp"
        extra_option_tree.write_text(
            original_tree.replace(":max_delta 10.0", ":max_abs 0.5\n          :max_delta 10.0"),
            encoding="utf-8",
        )
        extra_option_event_path = pathlib.Path(tmp) / "extra-option-events.jsonl"
        extra_option = subprocess.run(
            [
                str(runner_path),
                "--tree",
                str(extra_option_tree),
                "--events",
                str(extra_option_event_path),
                "--run-id",
                "native-extra-option-guard",
                "--trial-id",
                "T1",
                "--acceptance-policy",
                "invocation_scoped",
                "--intervention",
                "none",
                "--delay-ms",
                "50",
            ],
            cwd=REPO_ROOT,
            check=False,
            capture_output=True,
            text=True,
        )
        if extra_option.returncode == 0 or extra_option_event_path.exists():
            raise ExperimentError("native request-contract guard accepted an extra VLA option")
        if "option set does not match" not in extra_option.stderr:
            raise ExperimentError("native extra-option guard did not report the expected error")


def check_frozen_configuration_guards() -> None:
    common = read_json(COMMON_CONFIG)
    trial = read_json(DEFAULT_TRIALS[1])
    drifted_trial = json.loads(json.dumps(trial))
    drifted_trial["acceptance_policy"] = "invocation_scoped"
    try:
        validate_configuration(common, drifted_trial)
    except ExperimentError:
        pass
    else:
        raise ExperimentError("frozen trial-matrix guard accepted policy drift")

    typed_trial = read_json(DEFAULT_TRIALS[0])
    typed_trial["expected"]["accepted_dispatches"] = True
    try:
        validate_configuration(common, typed_trial)
    except ExperimentError:
        pass
    else:
        raise ExperimentError("frozen trial-matrix guard accepted Boolean/integer drift")

    drifted_common = json.loads(json.dumps(common))
    drifted_common["perception"]["initial_context_id"] = "ball-renamed"
    try:
        validate_configuration(drifted_common, trial)
    except ExperimentError:
        pass
    else:
        raise ExperimentError("frozen context guard accepted evidence-identity drift")

    frozen = FROZEN_TRIAL_MATRIX["T1"]
    protocol = read_json(EXAMPLE_ROOT / frozen["evidence_manifest"])
    protocol["required_video_action"] = "Perform an unrelated action."
    try:
        validate_frozen_protocol_identity(protocol, frozen, "T1")
    except ExperimentError:
        pass
    else:
        raise ExperimentError("frozen protocol guard accepted video-action drift")


def run_git(*args: str) -> str:
    completed = subprocess.run(
        ["git", *args],
        cwd=REPO_ROOT,
        check=False,
        capture_output=True,
        text=True,
    )
    return completed.stdout.strip() if completed.returncode == 0 else "unknown"


def git_provenance() -> dict[str, object]:
    status = run_git("status", "--porcelain", "--untracked-files=normal")
    return {
        "commit": run_git("rev-parse", "HEAD"),
        "dirty": bool(status and status != "unknown"),
        "status_digest": hashlib.sha256(status.encode("utf-8")).hexdigest() if status else "",
    }


def load_events(path: pathlib.Path) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for line_no, line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        if not line.strip():
            continue
        try:
            event = json.loads(line)
        except json.JSONDecodeError as exc:
            raise ExperimentError(f"{path}:{line_no}: invalid JSON: {exc}") from exc
        if event.get("schema") != "mbt.evt.v1":
            raise ExperimentError(f"{path}:{line_no}: event is not mbt.evt.v1")
        if not isinstance(event.get("seq"), int) or not isinstance(event.get("data"), dict):
            raise ExperimentError(f"{path}:{line_no}: incomplete canonical event envelope")
        rows.append(event)
    if not rows:
        raise ExperimentError(f"empty event stream: {path}")
    sequences = [event["seq"] for event in rows]
    if sequences != list(range(sequences[0], sequences[0] + len(sequences))):
        raise ExperimentError(f"non-contiguous event sequence in {path}")
    if rows[0].get("type") != "run_start" or rows[-1].get("type") != "run_end":
        raise ExperimentError(f"event stream must begin with run_start and end with run_end: {path}")
    return rows


def events_of_type(events: list[dict[str, Any]], event_type: str) -> list[dict[str, Any]]:
    return [event for event in events if event.get("type") == event_type]


def blackboard_events(
    events: list[dict[str, Any]], key: str, preview: object | None = None
) -> list[dict[str, Any]]:
    matches = [
        event
        for event in events_of_type(events, "bb_write")
        if event["data"].get("key") == key
    ]
    if preview is not None:
        matches = [event for event in matches if event["data"].get("preview") == preview]
    return matches


def final_branch(events: list[dict[str, Any]]) -> str:
    branches = blackboard_events(events, "active-branch")
    return str(branches[-1]["data"].get("preview", "")) if branches else ""


def pose_matches(observed: object, expected: dict[str, Any]) -> bool:
    if not isinstance(observed, list) or len(observed) != 3:
        return False
    wanted = [expected["x_m"], expected["y_m"], expected["yaw_rad"]]
    return all(
        isinstance(value, (int, float))
        and math.isclose(float(value), float(target), rel_tol=0.0, abs_tol=1e-9)
        for value, target in zip(observed, wanted, strict=True)
    )


def evidence_check_passes(
    check_id: str,
    common: dict[str, Any],
    events: list[dict[str, Any]],
) -> bool:
    submissions = events_of_type(events, "vla_submit")
    decisions = [
        event
        for event in events_of_type(events, "vla_result")
        if "decision" in event["data"]
    ]
    backend_results = [
        event
        for event in events_of_type(events, "vla_result")
        if "decision" not in event["data"] and isinstance(event["data"].get("record"), dict)
    ]
    dispatches = events_of_type(events, "walking_target_dispatch")
    revocations = [
        event
        for event in events_of_type(events, "async_authority_revoked")
        if event["data"].get("reason") == "branch_revoked"
    ]
    completion_drops = [
        event
        for event in events_of_type(events, "async_completion_dropped")
        if event["data"].get("reason") == "completion_after_cancel"
    ]
    submission = submissions[0] if len(submissions) == 1 else None
    decision = decisions[0] if len(decisions) == 1 else None
    initial_context = common["perception"]["initial_context_id"]
    moved_context = common["perception"]["moved_context_id"]
    expected_pose = common["proposer"]["approach_pose"]

    if check_id == "submission_initial_context":
        return bool(
            submission
            and submission["data"].get("generation") == 1
            and submission["data"].get("captured_context_id") == initial_context
        )
    if check_id == "accepted_current_result":
        return bool(
            submission
            and decision
            and decision["data"].get("decision") == "accepted"
            and decision["data"].get("captured_context_id") == initial_context
            and decision["data"].get("current_context_id") == initial_context
            and decision["data"].get("job_id") == submission["data"].get("job_id")
            and decision["data"].get("generation") == submission["data"].get("generation")
        )
    if check_id == "accepted_dispatch_once":
        return bool(
            submission
            and len(dispatches) == 1
            and dispatches[0]["data"].get("decision") == "accepted"
            and dispatches[0]["data"].get("job_id") == submission["data"].get("job_id")
            and dispatches[0]["data"].get("generation")
            == submission["data"].get("generation")
        )
    if check_id == "overlay_current_target":
        targets = blackboard_events(events, "current-walking-target")
        states = blackboard_events(events, "walking-target-state", "current")
        return bool(
            len(dispatches) == 1
            and targets
            and states
            and targets[-1]["seq"] > dispatches[0]["seq"]
            and states[-1]["seq"] > dispatches[0]["seq"]
            and pose_matches(targets[-1]["data"].get("preview"), expected_pose)
        )
    if check_id == "context_changed_after_submit":
        moved = blackboard_events(events, "ball-context-id", moved_context)
        return bool(
            submission
            and decision
            and moved
            and submission["seq"] < moved[0]["seq"] < decision["seq"]
        )
    if check_id == "intervention_before_backend_completion":
        moved = blackboard_events(events, "ball-context-id", moved_context)
        emergencies = blackboard_events(events, "emergency", True)
        interventions = moved if moved else emergencies
        return bool(
            submission
            and len(interventions) == 1
            and len(backend_results) == 1
            and submission["seq"] < interventions[0]["seq"] < backend_results[0]["seq"]
            and int(submission["unix_ms"])
            < int(interventions[0]["unix_ms"])
            < int(backend_results[0]["unix_ms"])
        )
    if check_id == "baseline_accepts_stale_result":
        return bool(
            decision
            and decision["data"].get("decision") == "accepted"
            and decision["data"].get("captured_context_id") == initial_context
            and decision["data"].get("current_context_id") == moved_context
        )
    if check_id == "host_rejects_stale_dispatch":
        return bool(
            len(dispatches) == 1
            and dispatches[0]["data"].get("decision") == "rejected"
            and dispatches[0]["data"].get("reason") == "context_changed"
            and dispatches[0]["data"].get("captured_context_id") == initial_context
            and dispatches[0]["data"].get("current_context_id") == moved_context
        )
    if check_id == "overlay_obsolete_candidate":
        candidates = blackboard_events(events, "candidate-walking-target")
        states = blackboard_events(events, "walking-target-state", "obsolete")
        return bool(
            candidates
            and states
            and pose_matches(candidates[-1]["data"].get("preview"), expected_pose)
            and not blackboard_events(events, "current-walking-target")
        )
    if check_id == "full_rejects_changed_context":
        return bool(
            decision
            and decision["data"].get("decision") == "rejected"
            and decision["data"].get("reason") == "context_changed"
            and decision["data"].get("captured_context_id") == initial_context
            and decision["data"].get("current_context_id") == moved_context
        )
    if check_id == "correlated_rejected_candidate":
        candidate_jobs = blackboard_events(events, "candidate-target-job-id")
        candidate_generations = blackboard_events(events, "candidate-target-generation")
        candidates = blackboard_events(events, "candidate-walking-target")
        return bool(
            submission
            and candidates
            and candidate_jobs
            and candidate_generations
            and str(candidate_jobs[-1]["data"].get("preview"))
            == str(submission["data"].get("job_id"))
            and candidate_generations[-1]["data"].get("preview")
            == submission["data"].get("generation")
            and pose_matches(candidates[-1]["data"].get("preview"), expected_pose)
        )
    if check_id == "rejected_safe_wait":
        return bool(blackboard_events(events, "request-state", "rejected_safe_wait"))
    if check_id == "no_walking_dispatch":
        return not dispatches
    if check_id == "safe_stand_on_intervention_tick":
        emergencies = blackboard_events(events, "emergency", True)
        safe_stands = blackboard_events(events, "active-branch", "safe_stand")
        return bool(
            emergencies
            and safe_stands
            and safe_stands[0]["seq"] > emergencies[0]["seq"]
            and safe_stands[0].get("tick") == emergencies[0].get("tick")
        )
    if check_id == "branch_authority_revoked":
        emergencies = blackboard_events(events, "emergency", True)
        return bool(
            len(revocations) == 1
            and emergencies
            and revocations[0]["seq"] > emergencies[0]["seq"]
            and revocations[0].get("tick") == emergencies[0].get("tick")
        )
    if check_id == "late_completion_dropped":
        return bool(
            len(revocations) == 1
            and len(completion_drops) == 1
            and completion_drops[0]["seq"] > revocations[0]["seq"]
        )
    if check_id == "overlay_revoked_no_target":
        emergencies = blackboard_events(events, "emergency", True)
        if not emergencies:
            return False
        emergency_seq = emergencies[0]["seq"]
        revoked = [
            event
            for event in blackboard_events(events, "request-state", "revoked")
            if event["seq"] > emergency_seq
        ]
        no_target = [
            event
            for event in blackboard_events(events, "walking-target-state", "none")
            if event["seq"] > emergency_seq
        ]
        return bool(revoked and no_target and not blackboard_events(events, "current-walking-target"))
    raise ExperimentError(f"unsupported structured evidence check: {check_id}")


def evaluate_required_event_evidence(
    common: dict[str, Any],
    protocol: dict[str, Any],
    events: list[dict[str, Any]],
) -> list[dict[str, Any]]:
    return [
        {
            "check": requirement["check"],
            "description": requirement["description"],
            "pass": evidence_check_passes(requirement["check"], common, events),
        }
        for requirement in protocol["required_event_evidence"]
    ]


def summarise_trial(
    common: dict[str, Any], trial: dict[str, Any], events: list[dict[str, Any]]
) -> dict[str, Any]:
    submissions = events_of_type(events, "vla_submit")
    decisions = [
        event
        for event in events_of_type(events, "vla_result")
        if "decision" in event["data"]
    ]
    backend_results = [
        event
        for event in events_of_type(events, "vla_result")
        if "decision" not in event["data"] and isinstance(event["data"].get("record"), dict)
    ]
    dispatches = events_of_type(events, "walking_target_dispatch")
    revocations = [
        event
        for event in events_of_type(events, "async_authority_revoked")
        if event["data"].get("reason") == "branch_revoked"
    ]
    completion_drops = [
        event
        for event in events_of_type(events, "async_completion_dropped")
        if event["data"].get("reason") == "completion_after_cancel"
    ]
    run_ends = events_of_type(events, "run_end")
    run_starts = events_of_type(events, "run_start")
    definitions = events_of_type(events, "bt_def")
    if len(submissions) != 1 or len(run_ends) != 1:
        raise ExperimentError("each finite trial must have one submission and one run_end")

    if trial["trial_id"] == "T3" and revocations:
        terminal = revocations[-1]
        terminal_decision = "rejected"
        reason = str(terminal["data"].get("reason", ""))
    elif decisions:
        terminal = decisions[-1]
        terminal_decision = str(terminal["data"].get("decision", ""))
        reason = str(terminal["data"].get("reason", ""))
    else:
        raise ExperimentError("trial has no commit decision or authority revocation")

    accepted_dispatches = [event for event in dispatches if event["data"].get("decision") == "accepted"]
    rejected_dispatches = [event for event in dispatches if event["data"].get("decision") == "rejected"]
    obsolete_dispatches = [
        event
        for event in accepted_dispatches
        if event["data"].get("captured_context_id") != event["data"].get("current_context_id")
    ]
    blocked_obsolete_dispatches = [
        event
        for event in rejected_dispatches
        if event["data"].get("captured_context_id") != event["data"].get("current_context_id")
    ]

    submission = submissions[0]
    latency_event = terminal
    if completion_drops and trial["intervention"] == "emergency":
        latency_event = completion_drops[-1]
    request_latency_ms = int(latency_event["unix_ms"]) - int(submission["unix_ms"])

    intervention_event: dict[str, Any] | None = None
    if trial["intervention"] == "moved_ball":
        moved = blackboard_events(
            events, "ball-context-id", common["perception"]["moved_context_id"]
        )
        intervention_event = moved[0] if moved else None
    elif trial["intervention"] == "emergency":
        emergency = blackboard_events(events, "emergency", True)
        intervention_event = emergency[0] if emergency else None

    safe_branch_event: dict[str, Any] | None = None
    if intervention_event is not None:
        safe_candidates = [
            event
            for event in blackboard_events(events, "active-branch", "safe_stand")
            if event["seq"] >= intervention_event["seq"]
        ]
        safe_branch_event = safe_candidates[0] if safe_candidates else None

    candidate_pose_events = blackboard_events(events, "candidate-walking-target")
    candidate_job_events = blackboard_events(events, "candidate-target-job-id")
    candidate_generation_events = blackboard_events(events, "candidate-target-generation")
    candidate_pose = (
        candidate_pose_events[-1]["data"].get("preview") if candidate_pose_events else None
    )
    observed_intervention_after_submit_ms = (
        int(intervention_event["unix_ms"]) - int(submission["unix_ms"])
        if intervention_event is not None
        else None
    )

    summary: dict[str, Any] = {
        "schema_version": "humanoid.video_trial_summary.v1",
        "experiment": common["experiment"],
        "trial_id": trial["trial_id"],
        "trial_name": trial["name"],
        "acceptance_policy": trial["acceptance_policy"],
        "terminal_decision": terminal_decision,
        "rejection_reason": reason,
        "request_id": submission["data"].get("job_id"),
        "generation": submission["data"].get("generation"),
        "captured_context_id": submission["data"].get("captured_context_id"),
        "terminal_current_context_id": terminal["data"].get("current_context_id", ""),
        "request_latency_ms": request_latency_ms,
        "walking_target_dispatches": len(dispatches),
        "accepted_dispatches": len(accepted_dispatches),
        "rejected_dispatches": len(rejected_dispatches),
        "obsolete_dispatches": len(obsolete_dispatches),
        "blocked_obsolete_dispatches": len(blocked_obsolete_dispatches),
        "authority_revocations": len(revocations),
        "completion_drops": len(completion_drops),
        "backend_results": len(backend_results),
        "recording_dispatch_calls": int(run_ends[0]["data"].get("recording_dispatch_calls", -1)),
        "active_branch": final_branch(events),
        "intervention": {
            "kind": trial["intervention"],
            "tick": intervention_event.get("tick") if intervention_event else None,
            "unix_ms": intervention_event.get("unix_ms") if intervention_event else None,
        },
        "observed_intervention_after_submit_ms": observed_intervention_after_submit_ms,
        "intervention_to_safe_branch_ms": (
            int(safe_branch_event["unix_ms"]) - int(intervention_event["unix_ms"])
            if safe_branch_event is not None and intervention_event is not None
            else None
        ),
        "candidate_walking_target": candidate_pose,
        "event_count": len(events),
        "event_type_counts": dict(collections.Counter(event["type"] for event in events)),
    }

    expected = trial["expected"]
    observed = {
        "terminal_decision": terminal_decision,
        "reason": reason,
        "accepted_dispatches": len(accepted_dispatches),
        "rejected_dispatches": len(rejected_dispatches),
        "authority_revocations": len(revocations),
        "completion_drops": len(completion_drops),
        "recording_dispatch_calls": summary["recording_dispatch_calls"],
        "active_branch": summary["active_branch"],
    }
    if "dispatch_reason" in expected:
        observed["dispatch_reason"] = (
            str(rejected_dispatches[-1]["data"].get("reason", ""))
            if rejected_dispatches
            else ""
        )
    mismatches = {
        key: {"expected": value, "observed": observed.get(key)}
        for key, value in expected.items()
        if observed.get(key) != value
    }

    def mismatch(name: str, expected_value: object, observed_value: object) -> None:
        mismatches[name] = {"expected": expected_value, "observed": observed_value}

    expected_decision_count = 0 if trial["trial_id"] == "T3" else 1
    if len(decisions) != expected_decision_count:
        mismatch("exactly_once_terminal_decision", expected_decision_count, len(decisions))
    if len(backend_results) != 1:
        mismatch("exactly_once_backend_result", 1, len(backend_results))
    if len(run_starts) != 1 or len(definitions) != 1:
        mismatch(
            "run_definition_cardinality",
            {"run_start": 1, "bt_def": 1},
            {"run_start": len(run_starts), "bt_def": len(definitions)},
        )
    elif (
        not run_starts[0]["data"].get("tree_hash")
        or run_starts[0]["data"].get("tree_hash")
        != definitions[0]["data"].get("tree_hash")
        or definitions[0]["data"].get("canonical_dsl_hash")
        != definitions[0]["data"].get("tree_hash")
        or not definitions[0]["data"].get("source_hash")
    ):
        mismatch(
            "tree_identity",
            "matching non-empty run/tree/canonical hashes plus source hash",
            {
                "run_tree_hash": run_starts[0]["data"].get("tree_hash"),
                "definition_tree_hash": definitions[0]["data"].get("tree_hash"),
                "canonical_dsl_hash": definitions[0]["data"].get("canonical_dsl_hash"),
                "source_hash": definitions[0]["data"].get("source_hash"),
            },
        )

    for field in ("job_id", "generation", "requesting_node_id", "captured_context_id"):
        if terminal["data"].get(field) != submission["data"].get(field):
            mismatch(
                f"terminal_correlation_{field}",
                submission["data"].get(field),
                terminal["data"].get(field),
            )

    if trial["intervention"] != "none" and intervention_event is None:
        mismatch("intervention_evidence", True, False)
    if intervention_event is not None:
        if intervention_event["seq"] <= submission["seq"]:
            mismatch(
                "intervention_after_submit",
                f"> seq {submission['seq']}",
                intervention_event["seq"],
            )
        if latency_event["seq"] <= intervention_event["seq"]:
            mismatch(
                "completion_after_intervention",
                f"> seq {intervention_event['seq']}",
                latency_event["seq"],
            )
        if len(backend_results) == 1 and (
            intervention_event["seq"] >= backend_results[0]["seq"]
            or int(intervention_event["unix_ms"]) >= int(backend_results[0]["unix_ms"])
        ):
            mismatch(
                "intervention_before_backend_completion",
                {
                    "seq": f"< {backend_results[0]['seq']}",
                    "unix_ms": f"< {backend_results[0]['unix_ms']}",
                },
                {
                    "seq": intervention_event["seq"],
                    "unix_ms": intervention_event["unix_ms"],
                },
            )
    if trial["trial_id"] in {"T2a", "T2b"}:
        terminal_data = terminal["data"]
        if terminal_data.get("captured_context_id") == terminal_data.get("current_context_id"):
            mismatch("context_changed_before_result", True, False)
    if trial["trial_id"] in {"T2b", "T3"} and dispatches:
        mismatch("no_dispatch_attempt", 0, len(dispatches))
    if obsolete_dispatches:
        mismatch("obsolete_dispatches", 0, len(obsolete_dispatches))

    expected_pose = common["proposer"]["approach_pose"]
    if len(backend_results) == 1:
        backend_result = backend_results[0]
        record = backend_result["data"]["record"]
        response = record.get("response", {})
        action = response.get("action", {}) if isinstance(response, dict) else {}
        if (
            record.get("node_name") != "approach-pose"
            or record.get("tick_index") != submission.get("tick")
            or f"frame_id={common['frames']['ball_position_frame']}" not in str(record.get("observation", ""))
            or action.get("frame_id") != expected_pose["frame_id"]
            or not pose_matches(action.get("u"), expected_pose)
        ):
            mismatch(
                "backend_result_contract",
                {
                    "node_name": "approach-pose",
                    "tick_index": submission.get("tick"),
                    "observation_frame": common["frames"]["ball_position_frame"],
                    "action": expected_pose,
                },
                {
                    "node_name": record.get("node_name"),
                    "observation": record.get("observation"),
                    "action": action,
                },
            )
        if backend_result["seq"] >= latency_event["seq"]:
            mismatch(
                "backend_result_before_terminal_evidence",
                f"< seq {latency_event['seq']}",
                backend_result["seq"],
            )
        if trial["trial_id"] == "T3" and record.get("completion_dropped") is not True:
            mismatch("backend_completion_marked_dropped", True, record.get("completion_dropped"))

    if trial["trial_id"] in {"T1", "T2a"} and dispatches:
        dispatch = dispatches[-1]
        if dispatch["seq"] <= terminal["seq"] or dispatch.get("tick") != terminal.get("tick"):
            mismatch(
                "dispatch_after_commit_same_tick",
                {"after_seq": terminal["seq"], "tick": terminal.get("tick")},
                {"seq": dispatch["seq"], "tick": dispatch.get("tick")},
            )
        for field in ("job_id", "generation", "captured_context_id"):
            if dispatch["data"].get(field) != terminal["data"].get(field):
                mismatch(
                    f"dispatch_correlation_{field}",
                    terminal["data"].get(field),
                    dispatch["data"].get(field),
                )
        target = dispatch["data"].get("target")
        target_pose = (
            [target.get("x_m"), target.get("y_m"), target.get("yaw_rad")]
            if isinstance(target, dict)
            else None
        )
        if (
            not pose_matches(target_pose, expected_pose)
            or not isinstance(target, dict)
            or target.get("frame_id") != expected_pose["frame_id"]
        ):
            mismatch("dispatch_target", expected_pose, target)

    if trial["trial_id"] == "T2a" and len(blocked_obsolete_dispatches) != 1:
        mismatch("blocked_obsolete_dispatch", 1, len(blocked_obsolete_dispatches))
    if trial["trial_id"] == "T2a":
        if not pose_matches(candidate_pose, expected_pose):
            mismatch("blocked_candidate_pose", expected_pose, candidate_pose)
        if blackboard_events(events, "current-walking-target"):
            mismatch("no_live_obsolete_target", 0, 1)

    if trial["trial_id"] == "T2b":
        candidate_job = (
            candidate_job_events[-1]["data"].get("preview") if candidate_job_events else None
        )
        candidate_generation = (
            candidate_generation_events[-1]["data"].get("preview")
            if candidate_generation_events
            else None
        )
        if not pose_matches(candidate_pose, expected_pose):
            mismatch("obsolete_candidate_pose", expected_pose, candidate_pose)
        elif (
            candidate_pose_events[-1]["seq"] <= terminal["seq"]
            or candidate_pose_events[-1].get("tick") != terminal.get("tick")
        ):
            mismatch(
                "candidate_after_rejection_same_tick",
                {"after_seq": terminal["seq"], "tick": terminal.get("tick")},
                {
                    "seq": candidate_pose_events[-1]["seq"],
                    "tick": candidate_pose_events[-1].get("tick"),
                },
            )
        if str(candidate_job) != str(submission["data"].get("job_id")):
            mismatch("candidate_job_correlation", submission["data"].get("job_id"), candidate_job)
        if candidate_generation != submission["data"].get("generation"):
            mismatch(
                "candidate_generation_correlation",
                submission["data"].get("generation"),
                candidate_generation,
            )
        if not blackboard_events(events, "request-state", "rejected_safe_wait"):
            mismatch("safe_recovery_state", "rejected_safe_wait", None)

    if trial["trial_id"] == "T3" and intervention_event is not None:
        if safe_branch_event is None or safe_branch_event.get("tick") != intervention_event.get("tick"):
            mismatch(
                "safe_branch_first_tick",
                intervention_event.get("tick"),
                safe_branch_event.get("tick") if safe_branch_event else None,
            )
        if len(revocations) != 1 or revocations[0].get("tick") != intervention_event.get("tick"):
            mismatch(
                "revocation_first_tick",
                {"count": 1, "tick": intervention_event.get("tick")},
                {
                    "count": len(revocations),
                    "tick": revocations[0].get("tick") if revocations else None,
                },
            )
        if len(completion_drops) == 1 and len(revocations) == 1:
            if completion_drops[0]["seq"] <= revocations[0]["seq"]:
                mismatch(
                    "completion_after_revocation",
                    f"> seq {revocations[0]['seq']}",
                    completion_drops[0]["seq"],
                )
            if len(backend_results) == 1 and completion_drops[0]["data"].get(
                "job_id"
            ) != backend_results[0]["data"].get("job_id"):
                mismatch(
                    "dropped_completion_backend_correlation",
                    backend_results[0]["data"].get("job_id"),
                    completion_drops[0]["data"].get("job_id"),
                )

    summary["pass"] = not mismatches
    summary["mismatches"] = mismatches
    return summary


def validate_tree_pair() -> None:
    baseline_path = EXAMPLE_ROOT / "lisp" / "bt_deadline_only.lisp"
    full_path = EXAMPLE_ROOT / "lisp" / "bt_invocation_scoped.lisp"
    baseline = baseline_path.read_text(encoding="utf-8")
    full = full_path.read_text(encoding="utf-8")
    normalised_baseline = baseline.replace(
        "humanoid-model-mediated-approach-deadline-only", "<tree-name>"
    ).replace("deadline_only", "<acceptance-policy>")
    normalised_full = full.replace(
        "humanoid-model-mediated-approach-invocation-scoped", "<tree-name>"
    ).replace("invocation_scoped", "<acceptance-policy>")
    if normalised_baseline != normalised_full:
        raise ExperimentError("baseline and full BTs differ by more than name and acceptance policy")


def validate_frozen_protocol_identity(
    protocol: dict[str, Any], frozen: dict[str, Any], trial_id: str
) -> None:
    if semantic_json_sha256(protocol) != frozen["protocol_sha256"]:
        raise ExperimentError(f"evidence protocol drifted from the frozen contract for {trial_id}")


def validate_configuration(common: dict[str, Any], trial: dict[str, Any]) -> tuple[pathlib.Path, pathlib.Path]:
    if common.get("schema_version") != "humanoid.video_experiment_common.v1":
        raise ExperimentError("unsupported common experiment configuration")
    if semantic_json_sha256(common) != FROZEN_COMMON_CONTRACT_SHA256:
        raise ExperimentError("common configuration drifted from the frozen experiment contract")
    if trial.get("schema_version") != "humanoid.video_trial.v1":
        raise ExperimentError("unsupported trial configuration")
    trial_id = str(trial.get("trial_id", ""))
    frozen = FROZEN_TRIAL_MATRIX.get(trial_id)
    if frozen is None:
        raise ExperimentError(f"trial is not in the frozen matrix: {trial_id or '<missing>'}")
    expected_trial = {
        "schema_version": "humanoid.video_trial.v1",
        "trial_id": trial_id,
        **{field: frozen[field] for field in TRIAL_DOCUMENT_FIELDS},
    }
    if semantic_json_sha256(trial) != semantic_json_sha256(expected_trial):
        raise ExperimentError(f"trial configuration drifted from the frozen matrix: {trial_id}")
    require_safe_component(trial.get("trial_id"), "trial ID")
    require_safe_component(trial.get("name"), "trial name")
    if common["timing"]["deadline_ms"] != 3500 or common["proposer"]["seed"] != 424242:
        raise ExperimentError("the checked-in BT contract requires deadline 3500 ms and seed 424242")
    if (
        not 2000 <= common["proposer"]["artificial_delay_ms"] <= 3000
        or common["timing"]["tick_hz"] != 20.0
        or common["proposer"].get("completes_after_cancel") is not True
    ):
        raise ExperimentError("the frozen service requires a 2-3 second delay, 20 Hz and late completion")
    if common["timing"]["deadline_ms"] <= common["proposer"]["artificial_delay_ms"]:
        raise ExperimentError("request deadline must exceed the artificial service delay")
    if not 0 <= common["timing"]["intervention_after_submit_ms"] < common["proposer"][
        "artificial_delay_ms"
    ]:
        raise ExperimentError("intervention must occur after submission and before completion")
    if common["host"]["physical_motion_enabled"]:
        raise ExperimentError("the SDK-independent runner cannot enable physical motion")
    expected_host = {
        "identity": "booster-k1-sdk-independent-host-stub",
        "platform": "sdk-independent",
        "walking_adapter": "recording-stub",
        "stability_source": "software-flag",
        "physical_motion_enabled": False,
    }
    if common["host"] != expected_host:
        raise ExperimentError("unsupported SDK-independent host configuration")
    if (
        common["proposer"].get("kind") != "deterministic"
        or common["proposer"].get("backend") != "humanoid-delayed-fake"
        or common["proposer"].get("model") != "deterministic-v1"
        or common["proposer"].get("instruction")
        != "choose a bounded approach pose relative to the observed ball"
        or common["frames"].get("approach_pose_frame") != "ball_context"
        or common["frames"].get("ball_position_frame") != "field"
        or common["proposer"].get("approach_pose", {}).get("frame_id") != "ball_context"
    ):
        raise ExperimentError("the checked-in service and BT require deterministic-v1 in ball_context")
    if common["proposer"].get("request_action_space") != {
        "dims": 3,
        "bound_lo": -1.0,
        "bound_hi": 1.0,
        "max_delta": 10.0,
    }:
        raise ExperimentError("the checked-in BTs require the frozen three-dimensional action space")
    if common["perception"].get("context_change_policy") != "euclidean_distance_gt_threshold":
        raise ExperimentError("unsupported ball context-change policy")
    if (
        common["perception"].get("observation_age_policy")
        != "not_simulated_by_sdk_independent_stub"
        or common["perception"].get("ball_observation_max_age_ms") is not None
    ):
        raise ExperimentError(
            "the SDK-independent stub must record observation-age policy as not simulated"
        )

    initial_position = common["perception"].get("initial_ball_position_m", {})
    moved_position = common["perception"].get("moved_ball_position_m", {})
    try:
        displacement = math.sqrt(
            sum(
                (float(moved_position[key]) - float(initial_position[key])) ** 2
                for key in ("x_m", "y_m", "z_m")
            )
        )
        threshold = float(common["perception"]["ball_context_change_threshold_m"])
    except (KeyError, TypeError, ValueError) as exc:
        raise ExperimentError("invalid configured ball positions or context threshold") from exc
    if not math.isfinite(displacement) or not math.isfinite(threshold) or displacement <= threshold:
        raise ExperimentError("moved ball must cross the configured context-change threshold")
    if (
        common["perception"].get("initial_context_id") != "ball-0001"
        or common["perception"].get("moved_context_id") != "ball-0002"
    ):
        raise ExperimentError("the frozen evidence protocol requires ball-0001 and ball-0002")

    tree_path = (EXAMPLE_ROOT / str(trial["bt"])).resolve()
    protocol_path = (EXAMPLE_ROOT / str(trial["evidence_manifest"])).resolve()
    if EXAMPLE_ROOT not in tree_path.parents or EXAMPLE_ROOT not in protocol_path.parents:
        raise ExperimentError("BT and evidence protocol must remain inside the experiment directory")
    if not tree_path.is_file() or not protocol_path.is_file():
        raise ExperimentError(f"missing BT or evidence protocol for {trial.get('trial_id')}")
    protocol = read_json(protocol_path)
    validate_frozen_protocol_identity(protocol, frozen, trial_id)
    if (
        protocol.get("schema_version") != "humanoid.video_evidence_protocol.v1"
        or protocol.get("trial_id") != trial.get("trial_id")
        or protocol.get("configuration") != frozen["configuration"]
        or protocol.get("capture_status") != "pending"
    ):
        raise ExperimentError(f"evidence protocol mismatch for {trial.get('trial_id')}")
    required_artefacts = {
        "manifest.json",
        "events.jsonl",
        "event-validation.json",
        "trial-summary.json",
        "raw-video.mp4",
        "overlay-video.mp4",
        "replay-report.json",
    }
    if set(protocol.get("required_artefacts", [])) != required_artefacts:
        raise ExperimentError(f"evidence artefact contract mismatch for {trial.get('trial_id')}")
    requirements = protocol.get("required_event_evidence")
    if not isinstance(requirements, list) or any(
        not isinstance(requirement, dict)
        or set(requirement) != {"check", "description"}
        or not isinstance(requirement.get("description"), str)
        or not requirement["description"]
        for requirement in requirements
    ):
        raise ExperimentError(f"invalid structured evidence requirements for {trial.get('trial_id')}")
    observed_checks = [requirement["check"] for requirement in requirements]
    if observed_checks != frozen["evidence_checks"]:
        raise ExperimentError(f"evidence checks drifted from the frozen matrix for {trial.get('trial_id')}")
    if set(protocol) != {
        "schema_version",
        "trial_id",
        "capture_status",
        "configuration",
        "required_event_evidence",
        "required_video_action",
        "required_artefacts",
    } or not isinstance(protocol.get("required_video_action"), str) or not protocol[
        "required_video_action"
    ]:
        raise ExperimentError(f"incomplete evidence protocol for {trial.get('trial_id')}")
    return tree_path, protocol_path


def run_event_validation(event_path: pathlib.Path, run_dir: pathlib.Path) -> dict[str, Any]:
    report_path = run_dir / "event-validation.json"
    completed = subprocess.run(
        [
            sys.executable,
            str(REPO_ROOT / "tools" / "validate_log.py"),
            str(event_path),
        ],
        cwd=REPO_ROOT,
        check=False,
        capture_output=True,
        text=True,
    )
    schema_status = "passed" if completed.returncode == 0 else "failed"
    report = {
        "schema_version": "humanoid.video_event_validation.v1",
        "status": "passed" if schema_status == "passed" else "failed",
        "canonical_envelope": {
            "status": "passed",
            "checks": ["valid JSON", "mbt.evt.v1 schema name", "contiguous seq", "run delimiters"],
        },
        "json_schema": {
            "status": schema_status,
            "return_code": completed.returncode,
            "stdout": completed.stdout.strip(),
            "stderr": completed.stderr.strip(),
        },
    }
    write_json(report_path, report)
    return report


def build_manifest(
    common: dict[str, Any],
    trial: dict[str, Any],
    protocol: dict[str, Any],
    run_id: str,
    run_dir: pathlib.Path,
    common_path: pathlib.Path,
    trial_path: pathlib.Path,
    protocol_path: pathlib.Path,
    tree_path: pathlib.Path,
    runner_path: pathlib.Path,
    command: list[str],
    time_scale: float,
    effective_delay_ms: int,
    effective_intervention_ms: int,
    check_mode: bool,
    summary: dict[str, Any],
) -> dict[str, Any]:
    artefacts: dict[str, object] = {}
    for name in ("events.jsonl", "trial-summary.json", "replay-report.json", "event-validation.json"):
        path = run_dir / name
        artefacts[name] = {
            "status": "captured",
            "sha256": sha256_file(path),
        }
    artefacts["raw-video.mp4"] = {"status": "pending"}
    artefacts["overlay-video.mp4"] = {"status": "pending"}

    return {
        "schema_version": "humanoid.video_run_manifest.v1",
        "experiment": common["experiment"],
        "run_id": run_id,
        "captured_at_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "capture_status": "ci_smoke" if check_mode else "runtime_complete_video_pending",
        "paper_eligible": False,
        "repository": git_provenance(),
        "trial": {
            "id": trial["trial_id"],
            "name": trial["name"],
            "acceptance_policy": trial["acceptance_policy"],
            "intervention": trial["intervention"],
        },
        "sources": {
            "common_configuration": source_record(common_path),
            "trial_configuration": source_record(trial_path),
            "evidence_protocol": source_record(protocol_path),
            "behaviour_tree": source_record(tree_path),
            "orchestrator": source_record(pathlib.Path(__file__)),
            "native_runner_source": source_record(
                EXAMPLE_ROOT / "src" / "experiment_runner.cpp"
            ),
            "delayed_fake_service_source": source_record(
                EXAMPLE_ROOT / "src" / "delayed_fake_service.cpp"
            ),
            "native_runner_binary": source_record(runner_path),
        },
        "host": common["host"],
        "proposer": common["proposer"],
        "timing": {
            **common["timing"],
            "execution_time_scale": time_scale,
            "effective_artificial_delay_ms": effective_delay_ms,
            "effective_intervention_after_submit_ms": effective_intervention_ms,
            "observed_intervention_after_submit_ms": summary[
                "observed_intervention_after_submit_ms"
            ],
        },
        "frames": common["frames"],
        "pose_bounds": common["pose_bounds"],
        "perception": common["perception"],
        "clock_alignment": common["video"],
        "observed_intervention": summary["intervention"],
        "required_event_evidence": protocol["required_event_evidence"],
        "event_evidence_results": summary["required_event_evidence"],
        "required_video_action": protocol["required_video_action"],
        "event_log_schema": "mbt.evt.v1",
        "event_type_counts": summary["event_type_counts"],
        "runner_command": command,
        "artefacts": artefacts,
    }


def capture_one_staged(
    runner_path: pathlib.Path,
    common_path: pathlib.Path,
    trial_path: pathlib.Path,
    out_root: pathlib.Path,
    run_id_prefix: str,
    time_scale: float,
    check_mode: bool,
    published_run_dir: pathlib.Path,
) -> pathlib.Path:
    common = read_json(common_path)
    trial = read_json(trial_path)
    tree_path, protocol_path = validate_configuration(common, trial)
    protocol = read_json(protocol_path)

    run_id = f"{require_safe_component(run_id_prefix, 'run ID prefix')}-{trial['name']}"
    run_dir = create_run_directory(out_root, run_id)

    configured_delay_ms = int(common["proposer"]["artificial_delay_ms"])
    configured_intervention_ms = int(common["timing"]["intervention_after_submit_ms"])
    effective_delay_ms = max(50, round(configured_delay_ms * time_scale))
    effective_intervention_ms = max(10, round(configured_intervention_ms * time_scale))
    if trial["intervention"] != "none" and effective_intervention_ms >= effective_delay_ms:
        raise ExperimentError("scaled intervention must occur before delayed completion")

    pose = common["proposer"]["approach_pose"]
    request_action_space = common["proposer"]["request_action_space"]
    bounds = common["pose_bounds"]
    perception = common["perception"]
    initial_position = perception["initial_ball_position_m"]
    moved_position = perception["moved_ball_position_m"]
    event_path = run_dir / "events.jsonl"
    git_sha = str(git_provenance()["commit"])
    command = [
        str(runner_path),
        "--tree",
        str(tree_path),
        "--events",
        str(event_path),
        "--run-id",
        run_id,
        "--trial-id",
        str(trial["trial_id"]),
        "--acceptance-policy",
        str(trial["acceptance_policy"]),
        "--intervention",
        str(trial["intervention"]),
        "--delay-ms",
        str(effective_delay_ms),
        "--deadline-ms",
        str(common["timing"]["deadline_ms"]),
        "--intervention-ms",
        str(effective_intervention_ms),
        "--tick-hz",
        str(common["timing"]["tick_hz"]),
        "--seed",
        str(common["proposer"]["seed"]),
        "--pose-x-m",
        str(pose["x_m"]),
        "--pose-y-m",
        str(pose["y_m"]),
        "--pose-yaw-rad",
        str(pose["yaw_rad"]),
        "--action-frame",
        str(common["frames"]["approach_pose_frame"]),
        "--observation-frame",
        str(common["frames"]["ball_position_frame"]),
        "--min-x-m",
        str(bounds["min_x_m"]),
        "--max-x-m",
        str(bounds["max_x_m"]),
        "--min-y-m",
        str(bounds["min_y_m"]),
        "--max-y-m",
        str(bounds["max_y_m"]),
        "--min-yaw-rad",
        str(bounds["min_yaw_rad"]),
        "--max-yaw-rad",
        str(bounds["max_yaw_rad"]),
        "--initial-context-id",
        str(perception["initial_context_id"]),
        "--moved-context-id",
        str(perception["moved_context_id"]),
        "--initial-ball-x-m",
        str(initial_position["x_m"]),
        "--initial-ball-y-m",
        str(initial_position["y_m"]),
        "--initial-ball-z-m",
        str(initial_position["z_m"]),
        "--moved-ball-x-m",
        str(moved_position["x_m"]),
        "--moved-ball-y-m",
        str(moved_position["y_m"]),
        "--moved-ball-z-m",
        str(moved_position["z_m"]),
        "--context-change-threshold-m",
        str(perception["ball_context_change_threshold_m"]),
        "--git-sha",
        git_sha,
        "--platform",
        str(common["host"]["platform"]),
        "--physical-motion-enabled",
        str(common["host"]["physical_motion_enabled"]).lower(),
        "--backend-name",
        str(common["proposer"]["backend"]),
        "--model-version",
        str(common["proposer"]["model"]),
        "--instruction",
        str(common["proposer"]["instruction"]),
        "--request-dims",
        str(request_action_space["dims"]),
        "--request-bound-lo",
        str(request_action_space["bound_lo"]),
        "--request-bound-hi",
        str(request_action_space["bound_hi"]),
        "--request-max-delta",
        str(request_action_space["max_delta"]),
    ]
    completed = subprocess.run(
        command,
        cwd=REPO_ROOT,
        check=False,
        stderr=subprocess.PIPE,
        text=True,
    )
    if completed.returncode != 0:
        raise ExperimentError(
            f"native runner failed for {trial['trial_id']} ({completed.returncode}): "
            f"{completed.stderr.strip()}"
        )

    manifest_command = list(command)
    manifest_command[manifest_command.index("--events") + 1] = str(
        published_run_dir / "events.jsonl"
    )

    events = load_events(event_path)
    summary = summarise_trial(common, trial, events)
    evidence_results = evaluate_required_event_evidence(common, protocol, events)
    summary["required_event_evidence"] = evidence_results
    for result in evidence_results:
        if not result["pass"]:
            summary["mismatches"][f"evidence:{result['check']}"] = {
                "expected": True,
                "observed": False,
            }
    timing_slack_ms = math.ceil(2000.0 / float(common["timing"]["tick_hz"])) + 20
    request_latency_ms = int(summary["request_latency_ms"])
    if not (
        effective_delay_ms - 5
        <= request_latency_ms
        <= effective_delay_ms + timing_slack_ms
    ):
        summary["mismatches"]["delayed_completion_timing"] = {
            "expected": {
                "min_ms": effective_delay_ms - 5,
                "max_ms": effective_delay_ms + timing_slack_ms,
            },
            "observed": request_latency_ms,
        }
    if trial["intervention"] != "none":
        observed_intervention_ms = summary["observed_intervention_after_submit_ms"]
        if not isinstance(observed_intervention_ms, int) or not (
            effective_intervention_ms - 5
            <= observed_intervention_ms
            <= effective_intervention_ms + timing_slack_ms
        ):
            summary["mismatches"]["intervention_timing"] = {
                "expected": {
                    "min_ms": effective_intervention_ms - 5,
                    "max_ms": effective_intervention_ms + timing_slack_ms,
                },
                "observed": observed_intervention_ms,
            }
    summary["pass"] = not summary["mismatches"]
    write_json(run_dir / "trial-summary.json", summary)

    event_validation = run_event_validation(event_path, run_dir)
    replay_report = {
        "schema_version": "humanoid.video_replay_report.v1",
        "event_validation": event_validation,
        "cross_event_validation": {
            "status": "pending",
            "reason": "cross-event replay validation is required before paper evidence is frozen",
        },
        "replay_comparison": {
            "status": "pending",
            "reason": "recorded-result replay comparison is required before paper evidence is frozen",
        },
    }
    write_json(run_dir / "replay-report.json", replay_report)

    manifest = build_manifest(
        common,
        trial,
        protocol,
        run_id,
        run_dir,
        common_path,
        trial_path,
        protocol_path,
        tree_path,
        runner_path,
        manifest_command,
        time_scale,
        effective_delay_ms,
        effective_intervention_ms,
        check_mode,
        summary,
    )
    write_json(run_dir / "manifest.json", manifest)

    if not summary["pass"]:
        raise ExperimentError(
            f"trial {trial['trial_id']} evidence mismatch: {json.dumps(summary['mismatches'], sort_keys=True)}"
        )
    if event_validation["status"] != "passed":
        raise ExperimentError(
            f"trial {trial['trial_id']} canonical event validation failed: "
            f"{event_validation['json_schema']['stdout']} "
            f"{event_validation['json_schema']['stderr']}"
        )
    return run_dir


def run_matrix(
    runner_path: pathlib.Path,
    common_path: pathlib.Path,
    trial_paths: list[pathlib.Path],
    out_root: pathlib.Path,
    run_id_prefix: str,
    time_scale: float,
    check_mode: bool,
    force: bool,
) -> list[pathlib.Path]:
    safe_prefix = require_safe_component(run_id_prefix, "run ID prefix")
    planned: list[tuple[pathlib.Path, dict[str, Any], pathlib.Path]] = []
    root: pathlib.Path | None = None
    destinations: set[pathlib.Path] = set()
    for trial_path in trial_paths:
        trial = read_json(trial_path)
        run_id = f"{safe_prefix}-{require_safe_component(trial.get('name'), 'trial name')}"
        resolved_root, destination = resolve_run_destination(out_root, run_id, force)
        if root is None:
            root = resolved_root
        elif root != resolved_root:
            raise ExperimentError("all matrix destinations must share one output root")
        if destination in destinations:
            raise ExperimentError(f"duplicate trial destination in matrix: {destination}")
        destinations.add(destination)
        planned.append((trial_path, trial, destination))

    if root is None:
        raise ExperimentError("the trial matrix must contain at least one trial")
    staged_runs: list[tuple[pathlib.Path, dict[str, Any], pathlib.Path]] = []
    with tempfile.TemporaryDirectory(
        prefix=f".{safe_prefix}.matrix-staging-", dir=root
    ) as staging_root:
        for trial_path, trial, destination in planned:
            staged = capture_one_staged(
                runner_path,
                common_path,
                trial_path,
                pathlib.Path(staging_root),
                safe_prefix,
                time_scale,
                check_mode,
                destination,
            )
            staged_runs.append((staged, trial, destination))

        # Publish only after every selected trial has passed native, schema and
        # evidence validation. This avoids a mixed old/new matrix on a later
        # trial validation failure.
        for staged, _, destination in staged_runs:
            publish_run_directory(staged, destination, force)

    for _, trial, destination in staged_runs:
        print(f"PASS {trial['trial_id']}: {destination}")
    return [destination for _, _, destination in staged_runs]


def resolve_trial_config(value: str) -> pathlib.Path:
    candidate = pathlib.Path(value)
    if candidate.is_file():
        return candidate.resolve()
    lookup: dict[str, pathlib.Path] = {}
    for path in DEFAULT_TRIALS:
        trial = read_json(path)
        lookup[path.stem.lower()] = path
        lookup[str(trial["trial_id"]).lower()] = path
        lookup[str(trial["name"]).lower()] = path
    resolved = lookup.get(value.lower())
    if resolved is None:
        raise ExperimentError(f"unknown trial configuration: {value}")
    return resolved


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--runner",
        default=str(REPO_ROOT / "build" / "dev" / "humanoid_model_mediated_trial"),
        help="Path to the native humanoid_model_mediated_trial executable.",
    )
    parser.add_argument(
        "--config",
        action="append",
        help="Trial ID, name, stem or JSON path. Repeat to select multiple trials.",
    )
    parser.add_argument(
        "--out-root",
        default=str(EXAMPLE_ROOT / "runs"),
        help="Directory that receives one evidence bundle per trial.",
    )
    parser.add_argument("--run-id-prefix", help="Prefix shared by the selected trial runs.")
    parser.add_argument(
        "--time-scale",
        type=float,
        default=None,
        help="Scale fake-service and intervention delays. Use 1.0 for video capture.",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="Run the complete matrix as a shortened CI smoke test in a temporary directory.",
    )
    parser.add_argument("--force", action="store_true", help="Replace an existing run directory.")
    parser.add_argument("--list", action="store_true", help="List trial IDs and exit.")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    if args.list:
        for path in DEFAULT_TRIALS:
            trial = read_json(path)
            print(f"{trial['trial_id']}: {trial['name']} ({path.name})")
        return 0

    try:
        validate_tree_pair()
        runner_path = pathlib.Path(args.runner).resolve()
        if not runner_path.is_file():
            raise ExperimentError(f"native runner not found: {runner_path}")
        if not os.access(runner_path, os.X_OK):
            raise ExperimentError(f"native runner is not executable: {runner_path}")
        trial_paths = (
            [resolve_trial_config(value) for value in args.config]
            if args.config
            else list(DEFAULT_TRIALS)
        )
        time_scale = args.time_scale if args.time_scale is not None else (0.08 if args.check else 1.0)
        if not math.isfinite(time_scale) or time_scale <= 0.0:
            raise ExperimentError("--time-scale must be finite and greater than zero")
        if not args.check and time_scale != 1.0:
            raise ExperimentError("--time-scale must be 1.0 outside --check protocol smoke runs")
        prefix = args.run_id_prefix or dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ")

        if args.check:
            check_run_directory_guards()
            check_native_runner_guards(runner_path)
            check_frozen_configuration_guards()
            with tempfile.TemporaryDirectory(prefix="muesli-humanoid-video-check-") as tmp:
                run_matrix(
                    runner_path,
                    COMMON_CONFIG,
                    trial_paths,
                    pathlib.Path(tmp),
                    prefix,
                    time_scale,
                    True,
                    False,
                )
        else:
            run_matrix(
                runner_path,
                COMMON_CONFIG,
                trial_paths,
                pathlib.Path(args.out_root),
                prefix,
                time_scale,
                False,
                args.force,
            )
        print(f"humanoid video experiment matrix passed ({len(trial_paths)} trial(s))")
        return 0
    except ExperimentError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    except OSError as exc:
        print(f"error: operating-system failure: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
