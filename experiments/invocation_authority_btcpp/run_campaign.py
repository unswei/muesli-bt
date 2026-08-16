#!/usr/bin/env python3

"""Run the frozen muesli-bt/BehaviorTree.CPP authority comparison."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import platform
import subprocess
import sys
from collections import defaultdict
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[2]
EXPERIMENT = ROOT / "experiments" / "invocation_authority_btcpp"
DEFAULT_PROTOCOL = EXPERIMENT / "configs" / "protocol.v1.json"
PROTOCOL_SCHEMA = (
    ROOT
    / "schemas"
    / "controlled_authority"
    / "v1"
    / "btcpp-comparison-protocol.schema.json"
)
EVENT_SCHEMA = ROOT / "schemas" / "event_log" / "v1" / "mbt.evt.v1.schema.json"
RUN_SCHEMA = (
    ROOT
    / "schemas"
    / "controlled_authority"
    / "v1"
    / "btcpp-comparison-run-manifest.schema.json"
)
CAMPAIGN_SCHEMA = (
    ROOT
    / "schemas"
    / "controlled_authority"
    / "v1"
    / "btcpp-comparison-campaign-manifest.schema.json"
)
DEFAULT_ENGINE = (
    ROOT
    / "build"
    / "bench-release-btcpp"
    / "bench"
    / "muesli_bt_controlled_authority_btcpp_campaign"
)

try:
    import jsonschema
except ImportError:
    jsonschema = None

_SCHEMA_VALIDATORS: dict[Path, Any] = {}


def load_json(path: Path) -> dict[str, Any]:
    with path.open(encoding="utf-8") as handle:
        return json.load(handle)


def write_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def validate(instance: Any, schema_path: Path) -> None:
    if jsonschema is None:
        return
    validator = _SCHEMA_VALIDATORS.get(schema_path)
    if validator is None:
        validator = jsonschema.Draft202012Validator(load_json(schema_path))
        _SCHEMA_VALIDATORS[schema_path] = validator
    validator.validate(instance)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--protocol", type=Path, default=DEFAULT_PROTOCOL)
    parser.add_argument("--engine", type=Path, default=DEFAULT_ENGINE)
    parser.add_argument("--seed-set", choices=("engineering", "paper"), default="engineering")
    parser.add_argument("--seeds", help="comma-separated explicit seeds")
    parser.add_argument("--schedules", help="comma-separated internal schedule keys")
    parser.add_argument("--variants", help="comma-separated manifest labels")
    return parser.parse_args()


def select(value: str | None, available: list[str], name: str) -> list[str]:
    if value is None:
        return available
    requested = [item.strip() for item in value.split(",") if item.strip()]
    unknown = sorted(set(requested) - set(available))
    if unknown:
        raise ValueError(f"unknown {name}: {', '.join(unknown)}")
    return [item for item in available if item in requested]


def resolve_seeds(protocol: dict[str, Any], seed_set: str, explicit: str | None) -> list[int]:
    if explicit:
        seeds = [int(item.strip()) for item in explicit.split(",") if item.strip()]
        if len(seeds) != len(set(seeds)) or any(seed < 0 for seed in seeds):
            raise ValueError("explicit seeds must be unique non-negative integers")
        return seeds
    definition = protocol["semantic_lane"]["seed_sets"][seed_set]
    return list(range(definition["first"], definition["first"] + definition["count"]))


def tsv(value: Any) -> str:
    text = "" if value is None else str(value)
    if any(character in text for character in "\t\r\n"):
        raise ValueError("resolved-plan fields cannot contain tabs or newlines")
    return text


def write_plan(
    path: Path,
    protocol: dict[str, Any],
    c0_protocol: dict[str, Any],
    catalogue: dict[str, Any],
    matrix: dict[str, Any],
    schedules: list[dict[str, Any]],
    variants: list[dict[str, Any]],
    seeds: list[int],
    common_task_path: Path,
) -> int:
    common = c0_protocol["common_task"]
    proposal = common["proposal"]
    rows: list[list[Any]] = [
        ["plan_version", "controlled-authority.btcpp-plan.v1"],
        ["protocol_id", protocol["protocol_id"]],
        ["catalogue_id", catalogue["catalogue_id"]],
        ["matrix_id", matrix["matrix_id"]],
        ["initial_context_id", common["initial_context_id"]],
        ["request_deadline_ms", common["request_deadline_ms"]],
        ["frame_id", proposal["frame_id"]],
        ["minimum", *proposal["bounds"]["min"]],
        ["maximum", *proposal["bounds"]["max"]],
        ["common_task_path", common_task_path],
    ]
    for schedule in schedules:
        rows.append(["schedule", schedule["schedule_id"]])
        for event in schedule["events"]:
            rows.append(
                [
                    "event",
                    event["at"],
                    event["event"],
                    event.get("request", ""),
                    event.get("context_id", ""),
                    event.get("count", ""),
                    "1" if event.get("duplicate") else "",
                    event.get("ordering", ""),
                    "",
                ]
            )
        rows.append(["end_schedule"])
    faults = {row["schedule_id"]: row for row in matrix["rows"]}
    for variant in variants:
        label = variant["manifest_label"]
        source = variant["expected_outcome_source"]
        for seed in seeds:
            for schedule in schedules:
                rows.append(
                    [
                        "run",
                        schedule["schedule_id"],
                        label,
                        seed,
                        faults[schedule["schedule_id"]]["expected_variant_outcomes"][source],
                    ]
                )
    with path.open("w", encoding="utf-8", newline="") as handle:
        for row in rows:
            handle.write("\t".join(tsv(value) for value in row) + "\n")
    return len(variants) * len(seeds) * len(schedules)


def paired_input_digest(schedule: dict[str, Any], seed: int) -> str:
    payload = {
        "generator": "controlled-authority-provider.v1",
        "schedule": schedule,
        "seed": seed,
        "proposal_formula": {
            "x_m": "0.2 + ((seed + request_index * 17) % 31) / 10000",
            "y_m": -0.1,
            "yaw_rad": 0.3,
        },
    }
    return hashlib.sha256(json.dumps(payload, sort_keys=True).encode()).hexdigest()


def validate_stream(path: Path) -> tuple[bool, list[str], int]:
    errors: list[str] = []
    events: list[dict[str, Any]] = []
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip():
            continue
        try:
            event = json.loads(line)
            validate(event, EVENT_SCHEMA)
            events.append(event)
        except Exception as error:  # evidence records schema failures rather than hiding them
            errors.append(f"line {line_number}: {error}")
    if not events:
        errors.append("event stream is empty")
    elif events[0].get("type") != "run_start":
        errors.append("first event is not run_start")
    if [event.get("seq") for event in events] != list(range(1, len(events) + 1)):
        errors.append("event sequence is not contiguous from one")
    return not errors, errors, len(events)


def false_rejection(trial: dict[str, Any]) -> bool:
    return bool(trial["metrics"]["valid_current_result_rejected"])


def write_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if not rows:
        raise ValueError(f"cannot write an empty table: {path}")
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def non_comment_lines(path: Path) -> int:
    count = 0
    for line in path.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if stripped and not stripped.startswith("//") and not stripped.startswith("#"):
            count += 1
    return count


def code_inventory() -> dict[str, Any]:
    groups = {
        "shared_experiment": [
            ROOT / "experiments" / "invocation_authority_controlled" / "src" / "common_task.cpp",
            ROOT / "experiments" / "invocation_authority_controlled" / "src" / "effect_recorder.cpp",
        ],
        "muesli_ordinary_adapter": [
            ROOT / "experiments" / "invocation_authority_controlled" / "src" / "variant.cpp",
        ],
        "muesli_full_adapter": [
            ROOT / "experiments" / "invocation_authority_controlled" / "src" / "runtime_variant.cpp",
        ],
        "btcpp_shared_task_and_lifecycle": [
            EXPERIMENT / "src" / "btcpp_task_runner.cpp",
        ],
        "btcpp_authority_adapters": [
            EXPERIMENT / "src" / "btcpp_variant.cpp",
        ],
    }
    inventory = {
        name: {
            "files": [str(path.relative_to(ROOT)) for path in paths],
            "non_blank_non_comment_lines": sum(non_comment_lines(path) for path in paths),
        }
        for name, paths in groups.items()
    }
    inventory["muesli_ordinary_adapter"]["scope_note"] = (
        "The shared file also contains the blocking and timeout-only C0 adapters; "
        "this whole-file count is an auditable location measure, not an ordinary-profile LOC ratio."
    )
    inventory["btcpp_authority_adapters"]["scope_note"] = (
        "The shared file contains common event/worker support and both BehaviorTree.CPP profiles; "
        "this whole-file count is not divided into a cross-runtime LOC ratio."
    )
    inventory["measurement"] = {
        "method": "non-blank, non-comment whole-file lines",
        "ratios_permitted": False,
    }
    return inventory


def git_revision() -> str:
    result = subprocess.run(
        ["git", "rev-parse", "HEAD"], cwd=ROOT, check=True, text=True, capture_output=True
    )
    return result.stdout.strip()


def main() -> int:
    args = parse_args()
    output = args.output.resolve()
    if output.exists() and any(output.iterdir()):
        raise ValueError(f"output directory must be new or empty: {output}")
    output.mkdir(parents=True, exist_ok=True)

    protocol_path = args.protocol.resolve()
    protocol = load_json(protocol_path)
    validate(protocol, PROTOCOL_SCHEMA)
    source = protocol["source_contract"]
    source_paths = {
        key: (protocol_path.parent / source[key]).resolve()
        for key in ("protocol", "schedule_catalogue", "fault_matrix", "common_task")
    }
    c0_protocol = load_json(source_paths["protocol"])
    catalogue = load_json(source_paths["schedule_catalogue"])
    matrix = load_json(source_paths["fault_matrix"])
    available_schedules = [item["schedule_id"] for item in catalogue["schedules"]]
    available_variants = [item["manifest_label"] for item in protocol["variants"]]
    selected_schedule_ids = select(args.schedules, available_schedules, "schedules")
    selected_variant_labels = select(args.variants, available_variants, "variants")
    if "F16" in selected_schedule_ids and selected_schedule_ids != available_schedules:
        raise ValueError("replay requires every preceding schedule")
    schedules = [
        item for item in catalogue["schedules"] if item["schedule_id"] in selected_schedule_ids
    ]
    variants = [
        item for item in protocol["variants"] if item["manifest_label"] in selected_variant_labels
    ]
    seeds = resolve_seeds(protocol, args.seed_set, args.seeds)

    plan_path = output / "resolved-plan.tsv"
    expected_trials = write_plan(
        plan_path,
        protocol,
        c0_protocol,
        catalogue,
        matrix,
        schedules,
        variants,
        seeds,
        source_paths["common_task"],
    )
    engine = args.engine.resolve()
    if not engine.is_file():
        raise ValueError(f"campaign engine does not exist: {engine}")
    subprocess.run([str(engine), str(plan_path), str(output)], cwd=ROOT, check=True)

    raw_path = output / "raw_trials.jsonl"
    trials = [json.loads(line) for line in raw_path.read_text(encoding="utf-8").splitlines()]
    if len(trials) != expected_trials:
        raise RuntimeError(f"engine wrote {len(trials)} trials; expected {expected_trials}")

    schedules_by_id = {item["schedule_id"]: item for item in schedules}
    variants_by_label = {item["manifest_label"]: item for item in variants}
    manifests: list[dict[str, Any]] = []
    trace_failures = 0
    for raw_index, trial in enumerate(trials, 1):
        streams: list[dict[str, Any]] = []
        for kind, field in (("task", "task_event_streams"), ("variant", "variant_event_streams")):
            for relative_path in trial[field]:
                stream_path = output / relative_path
                valid, errors, event_count = validate_stream(stream_path)
                trace_failures += int(not valid)
                streams.append(
                    {
                        "kind": kind,
                        "path": relative_path,
                        "sha256": sha256(stream_path),
                        "event_count": event_count,
                        "canonical_trace_valid": valid,
                        "validation_errors": errors,
                    }
                )
        schedule = schedules_by_id[trial["schedule_id"]]
        variant = variants_by_label[trial["variant_label"]]
        manifest = {
            "$schema": "https://muesli-bt.invalid/schemas/controlled_authority/v1/btcpp-comparison-run-manifest.schema.json",
            "schema_version": "controlled-authority.btcpp-comparison-run-manifest.v1",
            "protocol_id": protocol["protocol_id"],
            "schedule": {
                "internal_id": trial["schedule_id"],
                "reader_label": schedule["reader_label"],
            },
            "variant": {
                "variant_id": variant["variant_id"],
                "manifest_label": trial["variant_label"],
                "reader_label": variant["reader_label"],
                "runtime_id": variant["runtime_id"],
                "authority_profile": variant["authority_profile"],
                "implementation_id": trial["variant_id"],
            },
            "seed": trial["seed"],
            "raw_trial_index": raw_index,
            "paired_input_digest": paired_input_digest(schedule, trial["seed"]),
            "c0_profile_reference": trial["expected_outcome"],
            "matches_c0_profile_reference": trial["expected_outcome_met"],
            "metrics": {**trial["metrics"], "canonical_trace_valid": all(item["canonical_trace_valid"] for item in streams)},
            "counts": trial["counts"],
            "effects": trial["effects"],
            "canonical_streams": streams,
            "manual_exclusion": False,
        }
        validate(manifest, RUN_SCHEMA)
        manifest_path = (
            output
            / "runs"
            / trial["schedule_id"]
            / trial["variant_label"]
            / str(trial["seed"])
            / "manifest.json"
        )
        write_json(manifest_path, manifest)
        manifest["manifest_path"] = manifest_path.relative_to(output).as_posix()
        manifest["manifest_sha256"] = sha256(manifest_path)
        manifests.append(manifest)

    paired_groups: dict[tuple[str, int], set[str]] = defaultdict(set)
    for manifest in manifests:
        paired_groups[(manifest["schedule"]["internal_id"], manifest["seed"])].add(
            manifest["paired_input_digest"]
        )
    paired_inputs_equal = all(len(digests) == 1 for digests in paired_groups.values())

    trial_rows = [
        {
            "schedule": item["schedule"]["reader_label"],
            "variant": item["variant"]["reader_label"],
            "runtime": item["variant"]["runtime_id"],
            "authority_profile": item["variant"]["authority_profile"],
            "seed": item["seed"],
            "obsolete_effect": item["metrics"]["obsolete_effect"],
            "false_rejection": false_rejection(item),
            "terminal_outcomes": item["metrics"]["terminal_outcome_count"],
            "fallback_activations": item["counts"]["fallback_activations"],
            "safe_stand_activations": item["counts"]["safe_stand_activations"],
            "canonical_trace_valid": item["metrics"]["canonical_trace_valid"],
            "replay_equal": item["metrics"]["task_replay_equal"],
            "matches_c0_profile_reference": item["matches_c0_profile_reference"],
        }
        for item in manifests
    ]
    write_csv(output / "summary" / "trials.csv", trial_rows)

    grouped: dict[tuple[str, str], list[dict[str, Any]]] = defaultdict(list)
    for item in manifests:
        grouped[(item["schedule"]["reader_label"], item["variant"]["manifest_label"])].append(item)
    summary_rows: list[dict[str, Any]] = []
    for (reader_label, variant_label), group in grouped.items():
        summary_rows.append(
            {
                "schedule": reader_label,
                "variant": variants_by_label[variant_label]["reader_label"],
                "runtime": variants_by_label[variant_label]["runtime_id"],
                "authority_profile": variants_by_label[variant_label]["authority_profile"],
                "trials": len(group),
                "obsolete_effect_trials": sum(item["metrics"]["obsolete_effect"] for item in group),
                "false_rejection_trials": sum(false_rejection(item) for item in group),
                "terminal_outcomes": sum(item["metrics"]["terminal_outcome_count"] for item in group),
                "fallback_trials": sum(item["counts"]["fallback_activations"] > 0 for item in group),
                "safe_stand_trials": sum(item["counts"]["safe_stand_activations"] > 0 for item in group),
                "trace_failures": sum(not item["metrics"]["canonical_trace_valid"] for item in group),
                "replay_mismatches": sum(not item["metrics"]["task_replay_equal"] for item in group),
                "c0_reference_deviations": sum(not item["matches_c0_profile_reference"] for item in group),
            }
        )
    write_csv(output / "summary" / "schedule-summary.csv", summary_rows)
    write_json(output / "summary" / "schedule-summary.json", summary_rows)

    variant_rows: list[dict[str, Any]] = []
    for variant in variants:
        group = [item for item in manifests if item["variant"]["manifest_label"] == variant["manifest_label"]]
        variant_rows.append(
            {
                "variant": variant["reader_label"],
                "manifest_label": variant["manifest_label"],
                "runtime": variant["runtime_id"],
                "authority_profile": variant["authority_profile"],
                "trials": len(group),
                "obsolete_effect_trials": sum(item["metrics"]["obsolete_effect"] for item in group),
                "false_rejection_trials": sum(false_rejection(item) for item in group),
                "terminal_outcomes": sum(item["metrics"]["terminal_outcome_count"] for item in group),
                "trace_failures": sum(not item["metrics"]["canonical_trace_valid"] for item in group),
                "replay_mismatches": sum(not item["metrics"]["task_replay_equal"] for item in group),
                "c0_reference_deviations": sum(not item["matches_c0_profile_reference"] for item in group),
            }
        )
    write_csv(output / "summary" / "variant-summary.csv", variant_rows)
    write_json(output / "summary" / "variant-summary.json", variant_rows)
    variant_markdown = [
        "| implementation | trials | obsolete trials | false rejections | trace failures | replay mismatches | reference deviations |",
        "|---|---:|---:|---:|---:|---:|---:|",
    ]
    for row in variant_rows:
        variant_markdown.append(
            f"| {row['variant']} | {row['trials']} | {row['obsolete_effect_trials']} | "
            f"{row['false_rejection_trials']} | {row['trace_failures']} | "
            f"{row['replay_mismatches']} | {row['c0_reference_deviations']} |"
        )
    variant_summary_markdown = output / "paper" / "btcpp-authority-variant-summary.md"
    variant_summary_markdown.parent.mkdir(parents=True, exist_ok=True)
    variant_summary_markdown.write_text("\n".join(variant_markdown) + "\n", encoding="utf-8")
    write_csv(output / "paper" / "btcpp-authority-comparison-table.csv", summary_rows)
    markdown = [
        "| condition | implementation | obsolete trials | false rejections | terminal outcomes | reference deviations |",
        "|---|---|---:|---:|---:|---:|",
    ]
    for row in summary_rows:
        markdown.append(
            f"| {row['schedule']} | {row['variant']} | {row['obsolete_effect_trials']}/{row['trials']} | "
            f"{row['false_rejection_trials']}/{row['trials']} | {row['terminal_outcomes']} | "
            f"{row['c0_reference_deviations']}/{row['trials']} |"
        )
    paper_table = output / "paper" / "btcpp-authority-comparison-table.md"
    paper_table.parent.mkdir(parents=True, exist_ok=True)
    paper_table.write_text("\n".join(markdown) + "\n", encoding="utf-8")

    full = [item for item in manifests if item["variant"]["authority_profile"] == "invocation_scoped"]
    full_safe = all(
        not item["metrics"]["obsolete_effect"]
        and not false_rejection(item)
        and item["metrics"]["canonical_trace_valid"]
        and item["metrics"]["task_replay_equal"]
        and item["matches_c0_profile_reference"]
        for item in full
    )
    exact_paper = (
        selected_schedule_ids == protocol["paper_gate"]["exact_schedule_scope_required"]
        and selected_variant_labels == protocol["paper_gate"]["exact_variants_required"]
        and seeds
        == list(
            range(
                protocol["paper_gate"]["paper_seed_first"],
                protocol["paper_gate"]["paper_seed_first"]
                + protocol["paper_gate"]["paper_seed_count"],
            )
        )
    )
    paper_passed = full_safe and paired_inputs_equal and trace_failures == 0 if exact_paper else None
    paper_gate = {
        "evaluated": exact_paper,
        "passed": paper_passed,
        "reason": None if exact_paper else "partial or engineering selection",
        "full_ports_safe": full_safe,
        "paired_inputs_equal": paired_inputs_equal,
        "trace_failures": trace_failures,
    }

    artefact_paths = [
        "summary/trials.csv",
        "summary/schedule-summary.csv",
        "summary/schedule-summary.json",
        "summary/variant-summary.csv",
        "summary/variant-summary.json",
        "paper/btcpp-authority-comparison-table.csv",
        "paper/btcpp-authority-comparison-table.md",
        "paper/btcpp-authority-variant-summary.md",
    ]
    campaign_material = {
        "protocol": sha256(protocol_path),
        "catalogue": sha256(source_paths["schedule_catalogue"]),
        "matrix": sha256(source_paths["fault_matrix"]),
        "seeds": seeds,
        "schedules": selected_schedule_ids,
        "variants": selected_variant_labels,
    }
    campaign_id = "controlled-authority-btcpp-" + hashlib.sha256(
        json.dumps(campaign_material, sort_keys=True).encode()
    ).hexdigest()[:16]
    campaign = {
        "$schema": "https://muesli-bt.invalid/schemas/controlled_authority/v1/btcpp-comparison-campaign-manifest.schema.json",
        "schema_version": "controlled-authority.btcpp-comparison-campaign-manifest.v1",
        "campaign_id": campaign_id,
        "created_at": datetime.now(timezone.utc).isoformat(),
        "protocol_id": protocol["protocol_id"],
        "seed_set": args.seed_set if not args.seeds else "explicit",
        "seeds": seeds,
        "schedule_internal_ids": selected_schedule_ids,
        "schedule_reader_labels": [item["reader_label"] for item in schedules],
        "variant_labels": selected_variant_labels,
        "trial_count": len(manifests),
        "campaign_valid": full_safe and paired_inputs_equal and trace_failures == 0,
        "paper_gate": paper_gate,
        "frameworks": protocol["frameworks"],
        "code_inventory": code_inventory(),
        "inputs": {
            "protocol": {"path": str(protocol_path), "sha256": sha256(protocol_path)},
            "c0_protocol": {"path": str(source_paths["protocol"]), "sha256": sha256(source_paths["protocol"])},
            "catalogue": {"path": str(source_paths["schedule_catalogue"]), "sha256": sha256(source_paths["schedule_catalogue"])},
            "fault_matrix": {"path": str(source_paths["fault_matrix"]), "sha256": sha256(source_paths["fault_matrix"])},
            "common_task": {"path": str(source_paths["common_task"]), "sha256": sha256(source_paths["common_task"])},
            "resolved_plan": {"path": "resolved-plan.tsv", "sha256": sha256(plan_path)},
            "engine": {"path": str(engine), "sha256": sha256(engine)},
        },
        "environment": {
            "git_revision": git_revision(),
            "python": platform.python_version(),
            "platform": platform.platform(),
        },
        "raw_trials": {"path": "raw_trials.jsonl", "sha256": sha256(raw_path)},
        "run_manifests": [
            {"path": item["manifest_path"], "sha256": item["manifest_sha256"]}
            for item in manifests
        ],
        "derived_artefacts": {
            path: {"path": path, "sha256": sha256(output / path)} for path in artefact_paths
        },
        "manual_run_exclusion_allowed": False,
    }
    validate(campaign, CAMPAIGN_SCHEMA)
    write_json(output / "campaign-manifest.json", campaign)
    print(
        f"campaign {campaign_id}: {len(manifests)} trials, "
        f"full_ports_safe={str(full_safe).lower()}, trace_failures={trace_failures}"
    )
    return 0 if campaign["campaign_valid"] else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"BehaviorTree.CPP comparison campaign failed: {error}", file=sys.stderr)
        raise SystemExit(2) from error
