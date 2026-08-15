#!/usr/bin/env python3
"""Run the frozen Gate G6 engineering campaign inside the joint image."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import subprocess
import sys
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

from analysis.evidence import PROHIBITED_CONTROL_KEYS, _all_keys, _validate_events
from muesli_air_hockey_host import (
    MujocoDirectLaunchHostBackend,
    ProtocolProcessor,
    SchemaRegistry,
    UnixHostServer,
)
from provider.adapters import AcraExportProvider, provider_response
from run_g2 import load_contracts

PROTOCOL_PATH = EXAMPLE_ROOT / "configs" / "wp6_protocol.json"
PROTOCOL_SCHEMA = (
    REPOSITORY_ROOT
    / "schemas"
    / "air_hockey_integration"
    / "v1"
    / "airhockey.wp6.protocol.v1.schema.json"
)
PROVIDER_RESPONSE_SCHEMA = (
    REPOSITORY_ROOT
    / "schemas"
    / "air_hockey_integration"
    / "v1"
    / "airhockey.provider.response.v1.schema.json"
)
RUN_MARKER = ".air-hockey-g6-run"
CALIBRATION_SCENARIOS = {
    "G6-delay-timely": {"g6_timely_completion_accepted"},
    "G6-delay-boundary": {"g6_boundary_completion_accepted"},
    "G6-delay-stale": {"g6_stale_unexpired_completion_rejected"},
}


class GateG6Error(RuntimeError):
    """Gate G6 failed without promoting partial output to frozen evidence."""


def _read_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except Exception as error:
        raise GateG6Error(f"failed to read JSON {path}: {error}") from error
    if not isinstance(value, dict):
        raise GateG6Error(f"expected a JSON object: {path}")
    return value


def _write_json(path: Path, value: object) -> None:
    path.write_text(
        json.dumps(value, indent=2, sort_keys=True, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )


def _write_jsonl(path: Path, rows: list[dict[str, Any]]) -> None:
    path.write_text(
        "".join(
            json.dumps(row, sort_keys=True, separators=(",", ":")) + "\n"
            for row in rows
        ),
        encoding="utf-8",
    )


def _file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _semantic_sha256(value: object) -> str:
    serialised = json.dumps(
        value, sort_keys=True, separators=(",", ":"), ensure_ascii=False
    ).encode("utf-8")
    return hashlib.sha256(serialised).hexdigest()


def load_protocol() -> dict[str, Any]:
    protocol = _read_json(PROTOCOL_PATH)
    schema = _read_json(PROTOCOL_SCHEMA)
    Draft202012Validator.check_schema(schema)
    errors = sorted(
        Draft202012Validator(schema).iter_errors(protocol),
        key=lambda error: list(error.path),
    )
    if errors:
        where = "/".join(str(value) for value in errors[0].path) or "<root>"
        raise GateG6Error(f"WP6 protocol {where}: {errors[0].message}")

    split = protocol["engineering_split"]
    distribution_path = EXAMPLE_ROOT / split["distribution_config"]
    if _file_sha256(distribution_path) != split["distribution_config_sha256"]:
        raise GateG6Error("WP6 distribution configuration hash changed")
    delays = protocol["delay_calibration"]
    if not (
        delays["timely_ms"] < delays["context_change_ms"]
        < delays["stale_unexpired_ms"] < protocol["deadline_ms"]
        < delays["timeout_ms"]
    ):
        raise GateG6Error("WP6 delay calibration does not order timely/stale/timeout")
    if delays["boundary_ms"] != protocol["deadline_ms"] - 1:
        raise GateG6Error("WP6 boundary completion must be one millisecond before expiry")
    if protocol["paper_split"]["must_remain_unopened"] is not True:
        raise GateG6Error("WP6 cannot authorise opening the paper split")
    return protocol


def _prepare_output(output: Path) -> Path:
    output = output.resolve()
    if output == output.parent:
        raise GateG6Error("Gate G6 output cannot be a filesystem root")
    if output.exists() and any(output.iterdir()):
        raise GateG6Error(f"refuse to replace non-empty Gate G6 output: {output}")
    output.mkdir(parents=True, exist_ok=True)
    (output / RUN_MARKER).write_text("airhockey.g6.run.v1\n", encoding="utf-8")
    return output


def _engineering_shots(protocol: dict[str, Any]) -> tuple[Any, ...]:
    try:
        from airhockey_distill.envs import (
            load_direct_launch_distribution,
            manifest_sha256,
        )
    except Exception as error:
        raise GateG6Error("pinned airhockey_distill package is unavailable") from error

    split = protocol["engineering_split"]
    distribution = load_direct_launch_distribution(
        EXAMPLE_ROOT / split["distribution_config"]
    )
    shots = distribution.generate(split["name"])
    if len(shots) != split["expected_shots"]:
        raise GateG6Error("engineering shot count changed")
    if manifest_sha256(shots) != split["manifest_sha256"]:
        raise GateG6Error("engineering shot manifest hash changed")
    if any(generated.split != "engineering" for generated in shots):
        raise GateG6Error("non-engineering shot crossed into Gate G6")
    return shots


def _observed_predicates(stdout: str) -> set[str]:
    return {
        line.removeprefix("PREDICATE ").removesuffix(" PASS")
        for line in stdout.splitlines()
        if line.startswith("PREDICATE ") and line.endswith(" PASS")
    }


def _timing_records(stdout: str) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    for line in stdout.splitlines():
        if not line.startswith("TIMING "):
            continue
        try:
            record = json.loads(line.removeprefix("TIMING "))
        except json.JSONDecodeError as error:
            raise GateG6Error(f"invalid C++ timing record: {error}") from error
        if (
            not isinstance(record, dict)
            or record.get("schema_version") != "airhockey.wp6.timing.v1"
        ):
            raise GateG6Error("C++ runner emitted an unsupported timing record")
        for name in ("tick_duration_ns", "provider_wall_duration_ns"):
            values = record.get(name)
            if (
                not isinstance(values, list)
                or not values
                or any(not isinstance(value, int) or value < 0 for value in values)
            ):
                raise GateG6Error(f"C++ timing record has invalid {name}")
        records.append(record)
    if not records:
        raise GateG6Error("C++ runner emitted no timing records")
    return records


def _read_jsonl(path: Path) -> list[dict[str, Any]]:
    try:
        rows = [
            json.loads(line)
            for line in path.read_text(encoding="utf-8").splitlines()
            if line.strip()
        ]
    except Exception as error:
        raise GateG6Error(f"failed to read JSONL {path}: {error}") from error
    if not rows or not all(isinstance(row, dict) for row in rows):
        raise GateG6Error(f"event stream is empty or malformed: {path}")
    return rows


def _normalise_event_streams(
    rows: list[dict[str, Any]], path: Path
) -> list[list[dict[str, Any]]]:
    """Split multi-rig scenarios into independently canonical event streams."""

    streams: list[list[dict[str, Any]]] = []
    current: list[dict[str, Any]] = []
    for row in rows:
        if row.get("type") == "run_start":
            if current:
                if current[-1].get("type") != "run_end":
                    raise GateG6Error("a new runtime run started before the prior run ended")
                streams.append(current)
            current = [row]
        else:
            if not current:
                raise GateG6Error("event evidence appeared before run_start")
            current.append(row)
    if not current or current[-1].get("type") != "run_end":
        raise GateG6Error("event evidence ended without run_end")
    streams.append(current)

    for stream in streams:
        try:
            _validate_events(stream, path)
        except Exception as error:
            raise GateG6Error(f"invalid canonical event stream {path}: {error}") from error
    if len(streams) > 1:
        _write_jsonl(path, streams[0])
        for index, stream in enumerate(streams[1:], start=2):
            _write_jsonl(path.with_name(f"events-{index}.jsonl"), stream)
    return streams


def _validate_public_boundary(records: list[dict[str, Any]]) -> None:
    if not records:
        raise GateG6Error("MuJoCo scenario produced no control steps")
    for record in records:
        public_state = record.get("public_state")
        if not isinstance(public_state, dict):
            raise GateG6Error("evaluation record omitted its public state")
        leaked = PROHIBITED_CONTROL_KEYS.intersection(_all_keys(public_state))
        if leaked:
            raise GateG6Error(
                f"privileged keys crossed into public state: {sorted(leaked)}"
            )
        if "privileged" not in record:
            raise GateG6Error("evaluation record omitted separated privileged scoring")


def _event_counts(events: list[dict[str, Any]]) -> dict[str, int]:
    def count(event_type: str, **fields: Any) -> int:
        return sum(
            row.get("type") == event_type
            and isinstance(row.get("data"), dict)
            and all(row["data"].get(key) == value for key, value in fields.items())
            for row in events
        )

    return {
        "accepted_results": count("vla_result", decision="accepted"),
        "context_changed_rejections": count(
            "vla_result", decision="rejected", reason="context_changed"
        ),
        "accepted_obsolete_dispatches": count(
            "cap_call_end", status="accepted", obsolete=True
        ),
    }


def _run_scenario(
    executable: Path,
    scenario: str,
    tree: Path,
    expected_predicates: set[str],
    generated: Any,
    output: Path,
    schemas: SchemaRegistry,
) -> dict[str, Any]:
    output.mkdir(parents=True)
    events_path = output / "events.jsonl"
    backend = MujocoDirectLaunchHostBackend(
        shot_factory=lambda shot=generated.shot: shot
    )
    processor = ProtocolProcessor(schemas, backend)
    with tempfile.TemporaryDirectory(prefix=f"muesli-g6-{scenario.lower()}-") as directory:
        socket_path = Path(directory) / "host.sock"
        command = [
            str(executable),
            scenario,
            str(socket_path),
            str(tree),
            str(events_path),
        ]
        try:
            with UnixHostServer(socket_path, processor):
                completed = subprocess.run(
                    command,
                    check=False,
                    capture_output=True,
                    text=True,
                    timeout=60,
                )
        except Exception:
            backend.shutdown()
            raise
    (output / "runner.stdout").write_text(completed.stdout, encoding="utf-8")
    (output / "runner.stderr").write_text(completed.stderr, encoding="utf-8")
    if completed.returncode != 0:
        backend.shutdown()
        raise GateG6Error(
            f"{scenario} runner exited with {completed.returncode}: "
            f"{completed.stderr.strip()}"
        )
    observed = _observed_predicates(completed.stdout)
    if observed != expected_predicates:
        backend.shutdown()
        raise GateG6Error(
            f"{scenario} predicate mismatch: expected {sorted(expected_predicates)}, "
            f"observed {sorted(observed)}"
        )

    records = backend.evaluation_records()
    _validate_public_boundary(records)
    replay = backend.direct_replay_report()
    backend.shutdown()
    _write_jsonl(output / "evaluation-records.jsonl", records)
    _write_json(output / "direct-replay.json", replay)
    event_streams = _normalise_event_streams(_read_jsonl(events_path), events_path)
    events = [row for stream in event_streams for row in stream]
    result = {
        "scenario": scenario,
        "shot_id": generated.shot.shot_id,
        "predicates": sorted(observed),
        "events": len(events),
        "event_streams": [
            "events.jsonl",
            *(f"events-{index}.jsonl" for index in range(2, len(event_streams) + 1)),
        ],
        "control_steps": len(records),
        "event_counts": _event_counts(events),
        "timing": _timing_records(completed.stdout),
        "direct_replay": replay,
        "public_privileged_boundary": "passed",
    }
    _write_json(output / "scenario-report.json", result)
    return result


def _percentile(values: list[float], probability: float) -> float:
    if not values:
        raise GateG6Error("cannot summarise an empty timing series")
    ordered = sorted(float(value) for value in values)
    position = (len(ordered) - 1) * probability
    lower = math.floor(position)
    upper = math.ceil(position)
    if lower == upper:
        return ordered[lower]
    return ordered[lower] + (position - lower) * (ordered[upper] - ordered[lower])


def _timing_summary(nanoseconds: list[int], budget_ms: float | None = None) -> dict[str, Any]:
    milliseconds = [value / 1_000_000.0 for value in nanoseconds]
    summary: dict[str, Any] = {
        "samples": len(milliseconds),
        "median_ms": _percentile(milliseconds, 0.5),
        "p95_ms": _percentile(milliseconds, 0.95),
        "p99_ms": _percentile(milliseconds, 0.99),
        "maximum_ms": max(milliseconds),
    }
    if budget_ms is not None:
        misses = sum(value > budget_ms for value in milliseconds)
        summary["budget_ms"] = budget_ms
        summary["budget_misses"] = misses
        summary["budget_miss_rate"] = misses / len(milliseconds)
    return summary


def _run_deterministic_campaign(
    executable: Path,
    shots: tuple[Any, ...],
    protocol: dict[str, Any],
    output: Path,
) -> dict[str, Any]:
    configurations, _ = load_contracts()
    schemas = SchemaRegistry(REPOSITORY_ROOT / "schemas" / "air_hockey_host" / "v1")
    tick_ns: list[int] = []
    provider_ns: list[int] = []
    scenario_counts: Counter[str] = Counter()
    accepted_obsolete = 0
    invocation_obsolete = 0
    accepted_current = 0
    context_rejections = 0
    maximum_replay_error = 0.0

    first_shot = shots[0]
    calibration_results = []
    for scenario, predicates in CALIBRATION_SCENARIOS.items():
        result = _run_scenario(
            executable,
            scenario,
            EXAMPLE_ROOT / "lisp" / "bt_invocation_scoped.lisp",
            predicates,
            first_shot,
            output / "calibration" / scenario.lower(),
            schemas,
        )
        calibration_results.append(result)
        for timing in result["timing"]:
            tick_ns.extend(timing["tick_duration_ns"])
            provider_ns.extend(timing["provider_wall_duration_ns"])
        maximum_replay_error = max(
            maximum_replay_error,
            result["direct_replay"]["maximum_public_observation_error"],
        )

    matrix_output = output / "deterministic-matrix"
    for shot_index, generated in enumerate(shots):
        shot_output = matrix_output / f"shot-{shot_index:02d}"
        for scenario in protocol["deterministic_matrix"]["scenarios"]:
            configuration = configurations[scenario]
            result = _run_scenario(
                executable,
                scenario,
                EXAMPLE_ROOT / configuration["bt"],
                set(configuration["expected_predicates"]),
                generated,
                shot_output / scenario.lower(),
                schemas,
            )
            scenario_counts[scenario] += 1
            counts = result["event_counts"]
            if scenario == "H1":
                accepted_current += int(counts["accepted_results"] > 0)
            if scenario == "H2a":
                accepted_obsolete += counts["accepted_obsolete_dispatches"]
            else:
                invocation_obsolete += counts["accepted_obsolete_dispatches"]
            context_rejections += counts["context_changed_rejections"]
            for timing in result["timing"]:
                tick_ns.extend(timing["tick_duration_ns"])
                provider_ns.extend(timing["provider_wall_duration_ns"])
            maximum_replay_error = max(
                maximum_replay_error,
                result["direct_replay"]["maximum_public_observation_error"],
            )

    matrix = protocol["deterministic_matrix"]
    current_progress_rate = accepted_current / len(shots)
    if current_progress_rate < matrix["minimum_current_progress_rate"]:
        raise GateG6Error("current-result progress fell below its frozen floor")
    if invocation_obsolete > matrix["maximum_invocation_scoped_obsolete_dispatches"]:
        raise GateG6Error("invocation-scoped matrix dispatched an obsolete action")
    if accepted_obsolete != matrix["required_deadline_only_obsolete_dispatches"]:
        raise GateG6Error("deadline-only matrix did not expose one stale dispatch per shot")
    if context_rejections < len(shots):
        raise GateG6Error("context rule did not produce the required rejected results")

    tick = _timing_summary(tick_ns, float(protocol["timing"]["tick_budget_ms"]))
    if tick["p99_ms"] > protocol["timing"]["maximum_tick_p99_ms"]:
        raise GateG6Error("BT tick p99 exceeded the frozen limit")
    if tick["budget_miss_rate"] > protocol["timing"]["maximum_tick_budget_miss_rate"]:
        raise GateG6Error("BT tick budget miss rate exceeded the frozen limit")
    return {
        "calibration_scenarios": [value["scenario"] for value in calibration_results],
        "scenario_counts": dict(sorted(scenario_counts.items())),
        "runs": sum(scenario_counts.values()),
        "current_results_accepted": accepted_current,
        "current_progress_rate": current_progress_rate,
        "context_changed_rejections": context_rejections,
        "deadline_only_obsolete_dispatches": accepted_obsolete,
        "invocation_scoped_obsolete_dispatches": invocation_obsolete,
        "fallback_checks": scenario_counts["H6"],
        "maximum_public_observation_replay_error": maximum_replay_error,
        "tick_timing": tick,
        "deterministic_provider_wall_timing": _timing_summary(provider_ns),
        "status": "passed",
    }


def _run_learned_pilot(
    shots: tuple[Any, ...],
    checkpoint: Path,
    protocol: dict[str, Any],
    output: Path,
) -> dict[str, Any]:
    try:
        from airhockey_distill.envs import (
            BlackoutSchedule,
            DefendShotTrackingLoss,
            MujocoDirectLaunchBackend,
        )
    except Exception as error:
        raise GateG6Error("MuJoCo learned-provider environment is unavailable") from error

    definition = protocol["learned_provider"]
    expected_digest = f"sha256:{definition['checkpoint_sha256']}"
    provider = AcraExportProvider(
        definition["family_id"], checkpoint, expected_digest
    )
    if int(provider.metadata.get("training_seed", -1)) != definition["training_seed"]:
        raise GateG6Error("learned checkpoint training seed changed")
    if provider.metadata.get("training_stage") != definition["training_stage"]:
        raise GateG6Error("learned checkpoint is not a frozen final-stage export")

    response_schema = _read_json(PROVIDER_RESPONSE_SCHEMA)
    Draft202012Validator.check_schema(response_schema)
    response_validator = Draft202012Validator(response_schema)
    environment = DefendShotTrackingLoss(
        MujocoDirectLaunchBackend(),
        blackout=BlackoutSchedule(
            start_observation_step=definition["blackout_start_step"],
            length_steps=definition["blackout_length_steps"],
        ),
        timeout_steps=definition["timeout_steps"],
    )
    inference_ns: list[int] = []
    episodes: list[dict[str, Any]] = []
    deadline_misses = 0
    fallback_steps = 0
    try:
        for episode_index, generated in enumerate(shots, start=1):
            observation, info = environment.reset(
                shot=generated.shot,
                seed=protocol["engineering_split"]["seed"] + episode_index,
            )
            provider.reset()
            previous_visible = bool(info["puck_visible"])
            track_number = 1
            steps = 0
            outcome = "rollout_limit"
            while steps < definition["timeout_steps"]:
                request = {
                    "schema_version": "airhockey.provider.request.v1",
                    "request_id": f"g6-{episode_index:02d}-{steps:03d}",
                    "captured_context_id": (
                        f"episode-{episode_index:06d}/track-{track_number:04d}"
                    ),
                    "source_observation_step": steps,
                    "deadline_ms": protocol["deadline_ms"],
                    "action_frame": "airhockey.normalised_mallet_target.v1",
                    "observation": [float(value) for value in observation],
                }
                started_ns = time.perf_counter_ns()
                action = provider.infer(request["observation"])
                elapsed_ns = time.perf_counter_ns() - started_ns
                inference_ns.append(elapsed_ns)
                response = provider_response(request, action)
                errors = list(response_validator.iter_errors(response))
                if errors:
                    raise GateG6Error(
                        f"learned provider response failed schema validation: {errors[0].message}"
                    )
                if elapsed_ns > protocol["deadline_ms"] * 1_000_000:
                    deadline_misses += 1
                    fallback_steps += 1
                    applied = [float(observation[14]), float(observation[15])]
                else:
                    applied = response["actions"][0]["values"]
                observation, _, terminated, truncated, info = environment.step(applied)
                steps += 1
                visible = bool(info["puck_visible"])
                if not previous_visible and visible:
                    track_number += 1
                previous_visible = visible
                if terminated or truncated:
                    outcome = str(info["outcome"])
                    break
            episodes.append(
                {
                    "episode_index": episode_index,
                    "shot_id": generated.shot.shot_id,
                    "alias_family_id": generated.alias_family_id,
                    "steps": steps,
                    "outcome": outcome,
                }
            )
    finally:
        environment.close()

    outcomes = Counter(episode["outcome"] for episode in episodes)
    save_count = sum(outcomes[value] for value in definition["save_outcomes"])
    save_rate = save_count / len(episodes)
    timing = _timing_summary(inference_ns)
    if save_rate < definition["minimum_save_rate"]:
        raise GateG6Error("learned-provider save rate fell below the engineering floor")
    if timing["p95_ms"] > definition["maximum_p95_inference_ms"]:
        raise GateG6Error("learned-provider p95 inference latency exceeded its limit")
    if timing["maximum_ms"] > definition["maximum_inference_ms"]:
        raise GateG6Error("learned-provider maximum inference latency exceeded its limit")
    if deadline_misses != 0 or fallback_steps != 0:
        raise GateG6Error("learned-provider pilot required an unexpected deadline fallback")

    result = {
        "family_id": definition["family_id"],
        "training_seed": definition["training_seed"],
        "checkpoint_sha256": definition["checkpoint_sha256"],
        "episodes": len(episodes),
        "blackout_start_step": definition["blackout_start_step"],
        "blackout_length_steps": definition["blackout_length_steps"],
        "save_count": save_count,
        "save_rate": save_rate,
        "outcome_counts": dict(sorted(outcomes.items())),
        "inference_timing": timing,
        "deadline_misses": deadline_misses,
        "fallback_steps": fallback_steps,
        "status": "passed",
    }
    _write_json(output / "learned-episodes.json", {"episodes": episodes})
    _write_json(output / "learned-provider-report.json", result)
    return result


def _source_revision() -> str:
    marker = REPOSITORY_ROOT / ".muesli-bt-source-revision"
    if marker.is_file():
        return marker.read_text(encoding="utf-8").strip()
    completed = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=REPOSITORY_ROOT,
        check=True,
        capture_output=True,
        text=True,
    )
    return completed.stdout.strip()


def run_gate(executable: Path, checkpoint: Path, output: Path) -> dict[str, Any]:
    protocol = load_protocol()
    executable = executable.resolve()
    checkpoint = checkpoint.resolve()
    if not executable.is_file() or not os.access(executable, os.X_OK):
        raise GateG6Error(f"scenario runner is not executable: {executable}")
    if not checkpoint.is_file():
        raise GateG6Error(f"learned checkpoint is missing: {checkpoint}")
    output = _prepare_output(output)
    shots = _engineering_shots(protocol)
    _write_json(
        output / "engineering-manifest.json",
        {
            "schema_version": "airhockey.g6.engineering_manifest.v1",
            "split": "engineering",
            "manifest_sha256": protocol["engineering_split"]["manifest_sha256"],
            "shots": [generated.as_dict() for generated in shots],
        },
    )
    deterministic = _run_deterministic_campaign(
        executable, shots, protocol, output
    )
    learned = _run_learned_pilot(shots, checkpoint, protocol, output)
    report = {
        "schema_version": "airhockey.g6.report.v1",
        "status": "passed",
        "muesli_bt_revision": _source_revision(),
        "acra_revision": "1b6bbbbf19743b0042f01eabf0628eba5621cacf",
        "protocol_sha256": _file_sha256(PROTOCOL_PATH),
        "protocol_semantic_sha256": _semantic_sha256(protocol),
        "engineering_manifest_sha256": protocol["engineering_split"][
            "manifest_sha256"
        ],
        "engineering_shots": len(shots),
        "deterministic": deterministic,
        "learned_provider": learned,
        "protocol_frozen": True,
        "paper_split_opened": False,
    }
    _write_json(output / "g6-report.json", report)
    print(
        "air-hockey Gate G6 passed: 26-shot H1-H8 matrix, calibrated delays, "
        "frozen learned provider, timing, fallback and unopened paper split"
    )
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    subparsers.add_parser("check-protocol", help="validate the frozen WP6 protocol")
    run = subparsers.add_parser("run", help="run the complete Gate G6 campaign")
    run.add_argument("--runner", required=True, type=Path)
    run.add_argument("--checkpoint", required=True, type=Path)
    run.add_argument("--out", required=True, type=Path)
    arguments = parser.parse_args()
    if arguments.command == "check-protocol":
        protocol = load_protocol()
        print(
            "air-hockey WP6 protocol passed: "
            f"{protocol['engineering_split']['expected_shots']} engineering shots, "
            "paper split closed"
        )
        return 0
    if arguments.command == "run":
        run_gate(arguments.runner, arguments.checkpoint, arguments.out)
        return 0
    raise GateG6Error(f"unsupported command: {arguments.command}")


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except GateG6Error as error:
        raise SystemExit(f"error: {error}") from error
