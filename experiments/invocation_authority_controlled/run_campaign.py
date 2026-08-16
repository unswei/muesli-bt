#!/usr/bin/env python3

"""Run the frozen controlled-authority semantic campaign and write artefacts."""

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
from typing import Any, Iterable


EXPERIMENT = Path(__file__).resolve().parent
ROOT = EXPERIMENT.parents[1]
DEFAULT_PROTOCOL = EXPERIMENT / "configs" / "protocol.v1.json"
EVENT_SCHEMA = ROOT / "schemas" / "event_log" / "v1" / "mbt.evt.v1.schema.json"
RUN_MANIFEST_SCHEMA = (
    ROOT / "schemas" / "controlled_authority" / "v1" / "run-manifest.schema.json"
)
CAMPAIGN_MANIFEST_SCHEMA = (
    ROOT / "schemas" / "controlled_authority" / "v1" / "campaign-manifest.schema.json"
)


def load_json(path: Path) -> dict[str, Any]:
    with path.open(encoding="utf-8") as handle:
        value = json.load(handle)
    if not isinstance(value, dict):
        raise ValueError(f"expected a JSON object in {path}")
    return value


def write_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as handle:
        json.dump(value, handle, indent=2, sort_keys=True)
        handle.write("\n")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def validate_against_schema(instance: Any, schema_path: Path) -> None:
    try:
        import jsonschema
    except ImportError:
        return
    jsonschema.Draft202012Validator(load_json(schema_path)).validate(instance)


def split_selection(value: str | None, available: list[str], name: str) -> list[str]:
    if value is None or value == "all":
        return list(available)
    selected = [part.strip() for part in value.split(",") if part.strip()]
    unknown = sorted(set(selected) - set(available))
    if unknown:
        raise ValueError(f"unknown {name}: {', '.join(unknown)}")
    if not selected or len(selected) != len(set(selected)):
        raise ValueError(f"{name} selection must be non-empty and unique")
    return [item for item in available if item in selected]


def resolve_seeds(protocol: dict[str, Any], seed_set: str, override: str | None) -> list[int]:
    if override:
        seeds = [int(part.strip()) for part in override.split(",") if part.strip()]
        if not seeds or len(seeds) != len(set(seeds)) or min(seeds) < 0:
            raise ValueError("seed override must contain unique non-negative integers")
        return seeds
    definition = protocol["seed_sets"][seed_set]
    return list(range(definition["first"], definition["first"] + definition["count"]))


def tsv_field(value: Any) -> str:
    text = "" if value is None else str(value)
    if "\t" in text or "\n" in text or "\r" in text:
        raise ValueError("campaign plan fields cannot contain tabs or newlines")
    return text


def write_plan(
    path: Path,
    protocol: dict[str, Any],
    catalogue: dict[str, Any],
    schedules: list[dict[str, Any]],
    variants: list[dict[str, Any]],
    seeds: list[int],
) -> int:
    common = protocol["common_task"]
    proposal = common["proposal"]
    rows: list[list[Any]] = [
        ["plan_version", "controlled-authority.plan.v1"],
        ["protocol_id", protocol["protocol_id"]],
        ["catalogue_id", catalogue["catalogue_id"]],
        ["initial_context_id", common["initial_context_id"]],
        ["request_deadline_ms", common["request_deadline_ms"]],
        ["frame_id", proposal["frame_id"]],
        ["minimum", *proposal["bounds"]["min"]],
        ["maximum", *proposal["bounds"]["max"]],
        ["common_task_path", (EXPERIMENT / "lisp" / "common_task.lisp").resolve()],
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

    expected = protocol["expected_variant_outcomes"]
    run_count = 0
    for variant in variants:
        short_label = variant["short_label"]
        for seed in seeds:
            for schedule in schedules:
                schedule_id = schedule["schedule_id"]
                rows.append(
                    ["run", schedule_id, short_label, seed, expected[schedule_id][short_label]]
                )
                run_count += 1

    with path.open("w", encoding="utf-8", newline="") as handle:
        for row in rows:
            handle.write("\t".join(tsv_field(field) for field in row) + "\n")
    return run_count


def validate_event_stream(path: Path) -> tuple[bool, list[str], int]:
    errors: list[str] = []
    events: list[dict[str, Any]] = []
    event_schema = load_json(EVENT_SCHEMA)
    allowed_keys = set(event_schema["properties"])
    required_keys = set(event_schema["required"])
    allowed_types = set(event_schema["properties"]["type"]["enum"])
    with path.open(encoding="utf-8") as handle:
        for line_number, line in enumerate(handle, 1):
            try:
                event = json.loads(line)
                if not isinstance(event, dict):
                    raise ValueError("event is not an object")
                if required_keys - set(event):
                    raise ValueError("event is missing canonical envelope fields")
                if set(event) - allowed_keys:
                    raise ValueError("event has fields outside the canonical envelope")
                if event["schema"] != "mbt.evt.v1" or event["contract_version"] != "1.0.0":
                    raise ValueError("event has the wrong schema or contract version")
                if event["type"] not in allowed_types:
                    raise ValueError("event type is not in the canonical contract")
                if not isinstance(event["run_id"], str) or not event["run_id"]:
                    raise ValueError("event run_id must be a non-empty string")
                if not isinstance(event["unix_ms"], int) or not isinstance(event["seq"], int):
                    raise ValueError("event time and sequence must be integers")
                if event["seq"] < 1 or not isinstance(event["data"], dict):
                    raise ValueError("event sequence or data payload is invalid")
                if "tick" in event and (
                    not isinstance(event["tick"], int) or event["tick"] < 0
                ):
                    raise ValueError("event tick must be a non-negative integer")
                validate_against_schema(event, EVENT_SCHEMA)
                events.append(event)
            except Exception as error:  # schema errors are reported as artefact evidence
                errors.append(f"line {line_number}: {error}")
    if not events:
        errors.append("event stream is empty")
        return False, errors, 0
    run_ids = {event.get("run_id") for event in events}
    if len(run_ids) != 1:
        errors.append("event stream contains more than one run_id")
    expected_sequences = list(range(1, len(events) + 1))
    if [event.get("seq") for event in events] != expected_sequences:
        errors.append("event sequence is not contiguous from one")
    if events[0].get("type") != "run_start":
        errors.append("first event is not run_start")
    return not errors, errors, len(events)


def read_raw_trials(path: Path) -> list[dict[str, Any]]:
    trials: list[dict[str, Any]] = []
    with path.open(encoding="utf-8") as handle:
        for line_number, line in enumerate(handle, 1):
            value = json.loads(line)
            if not isinstance(value, dict):
                raise ValueError(f"raw trial line {line_number} is not an object")
            trials.append(value)
    return trials


def relative(path: Path, root: Path) -> str:
    return path.relative_to(root).as_posix()


def write_trial_manifests(
    output: Path,
    trials: list[dict[str, Any]],
    schedule_by_id: dict[str, dict[str, Any]],
    variant_by_label: dict[str, dict[str, Any]],
    protocol: dict[str, Any],
    catalogue: dict[str, Any],
) -> tuple[list[dict[str, Any]], int]:
    manifests: list[dict[str, Any]] = []
    trace_failures = 0
    for raw_trial_index, trial in enumerate(trials, 1):
        trace_records: list[dict[str, Any]] = []
        all_valid = True
        for stream_kind, key in (
            ("task", "task_event_streams"),
            ("variant", "variant_event_streams"),
        ):
            for stream in trial[key]:
                stream_path = output / stream
                valid, errors, event_count = validate_event_stream(stream_path)
                all_valid = all_valid and valid
                trace_records.append(
                    {
                        "kind": stream_kind,
                        "path": stream,
                        "sha256": sha256(stream_path),
                        "event_count": event_count,
                        "canonical_trace_valid": valid,
                        "validation_errors": errors,
                    }
                )
        if not all_valid:
            trace_failures += 1
        trial["metrics"]["canonical_trace_valid"] = all_valid

        schedule = schedule_by_id[trial["schedule_id"]]
        variant = variant_by_label[trial["variant_label"]]
        manifest_path = (
            output
            / "runs"
            / trial["schedule_id"]
            / trial["variant_label"]
            / str(trial["seed"])
            / "manifest.json"
        )
        manifest = {
            "$schema": (
                "https://muesli-bt.invalid/schemas/controlled_authority/v1/"
                "run-manifest.schema.json"
            ),
            "schema_version": "controlled-authority.run-manifest.v1",
            "protocol_id": protocol["protocol_id"],
            "catalogue_id": catalogue["catalogue_id"],
            "schedule": {
                "internal_id": trial["schedule_id"],
                "reader_label": schedule["reader_label"],
            },
            "variant": {
                "short_label": trial["variant_label"],
                "variant_id": trial["variant_id"],
                "reader_label": variant["reader_label"],
            },
            "seed": trial["seed"],
            "raw_trial_index": raw_trial_index,
            "expected_outcome": trial["expected_outcome"],
            "observed_outcome": trial["observed_outcome"],
            "expected_outcome_met": trial["expected_outcome_met"],
            "metrics": trial["metrics"],
            "counts": trial["counts"],
            "canonical_streams": trace_records,
            "manual_exclusion": False,
        }
        validate_against_schema(manifest, RUN_MANIFEST_SCHEMA)
        write_json(manifest_path, manifest)
        manifests.append(
            {
                **manifest,
                "manifest_path": relative(manifest_path, output),
                "manifest_sha256": sha256(manifest_path),
            }
        )
    return manifests, trace_failures


def rate(numerator: int, denominator: int) -> float:
    return numerator / denominator if denominator else 0.0


def summarise(
    manifests: list[dict[str, Any]], group_fields: tuple[str, ...]
) -> list[dict[str, Any]]:
    groups: dict[tuple[str, ...], list[dict[str, Any]]] = defaultdict(list)
    for manifest in manifests:
        key_parts: list[str] = []
        for field in group_fields:
            container, item = field.split(".", 1)
            key_parts.append(str(manifest[container][item]))
        groups[tuple(key_parts)].append(manifest)

    rows: list[dict[str, Any]] = []
    for group_key, group in groups.items():
        trials = len(group)
        row = {field.replace(".", "_"): value for field, value in zip(group_fields, group_key)}
        obsolete = sum(bool(item["metrics"]["obsolete_effect"]) for item in group)
        false_rejections = sum(
            bool(item["metrics"]["valid_current_result_rejected"]) for item in group
        )
        expected = sum(bool(item["expected_outcome_met"]) for item in group)
        traces = sum(bool(item["metrics"]["canonical_trace_valid"]) for item in group)
        replays = sum(bool(item["metrics"]["task_replay_equal"]) for item in group)
        terminal_outcomes = sum(item["metrics"]["terminal_outcome_count"] for item in group)
        current_once = sum(
            bool(item["metrics"]["current_result_accepted_exactly_once"])
            for item in group
        )
        fallback_trials = sum(item["counts"]["fallback_activations"] > 0 for item in group)
        safe_stand_trials = sum(item["counts"]["safe_stand_activations"] > 0 for item in group)
        row.update(
            {
                "trials": trials,
                "obsolete_effects": obsolete,
                "obsolete_effect_rate": rate(obsolete, trials),
                "valid_current_result_rejections": false_rejections,
                "valid_current_result_rejection_rate": rate(false_rejections, trials),
                "expected_outcomes_met": expected,
                "expected_outcome_rate": rate(expected, trials),
                "canonical_traces_valid": traces,
                "canonical_trace_valid_rate": rate(traces, trials),
                "task_replays_equal": replays,
                "task_replay_equal_rate": rate(replays, trials),
                "terminal_outcomes": terminal_outcomes,
                "mean_terminal_outcomes": rate(terminal_outcomes, trials),
                "current_result_accepted_exactly_once": current_once,
                "current_result_accepted_exactly_once_rate": rate(current_once, trials),
                "fallback_trials": fallback_trials,
                "fallback_rate": rate(fallback_trials, trials),
                "safe_stand_trials": safe_stand_trials,
                "safe_stand_rate": rate(safe_stand_trials, trials),
            }
        )
        rows.append(row)
    return rows


def write_csv(path: Path, rows: list[dict[str, Any]], fields: Iterable[str] | None = None) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fieldnames = list(fields) if fields else list(rows[0])
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)


def write_summaries(
    output: Path,
    manifests: list[dict[str, Any]],
    schedules: list[dict[str, Any]],
    variants: list[dict[str, Any]],
) -> dict[str, str]:
    trial_rows: list[dict[str, Any]] = []
    for item in manifests:
        trial_rows.append(
            {
                "schedule": item["schedule"]["reader_label"],
                "variant": item["variant"]["reader_label"],
                "seed": item["seed"],
                "expected_outcome_met": item["expected_outcome_met"],
                **item["metrics"],
                "manifest": item["manifest_path"],
            }
        )
    write_csv(output / "summary" / "trials.csv", trial_rows)

    schedule_summary = summarise(
        manifests, ("schedule.reader_label", "variant.reader_label")
    )
    variant_summary = summarise(manifests, ("variant.reader_label",))
    write_json(output / "summary" / "schedule-summary.json", schedule_summary)
    write_json(output / "summary" / "variant-summary.json", variant_summary)
    write_csv(output / "summary" / "schedule-summary.csv", schedule_summary)
    write_csv(output / "summary" / "variant-summary.csv", variant_summary)

    summary_lookup = {
        (row["schedule_reader_label"], row["variant_reader_label"]): row
        for row in schedule_summary
    }
    paper_rows: list[dict[str, Any]] = []
    for schedule in schedules:
        for variant in variants:
            paper_rows.append(
                summary_lookup[(schedule["reader_label"], variant["reader_label"])]
            )
    write_csv(output / "paper" / "controlled-authority-table.csv", paper_rows)

    markdown_path = output / "paper" / "controlled-authority-table.md"
    markdown_path.parent.mkdir(parents=True, exist_ok=True)
    with markdown_path.open("w", encoding="utf-8") as handle:
        variant_labels = [variant["reader_label"] for variant in variants]
        handle.write("# controlled invocation-authority results\n\n")
        handle.write(
            "Each cell reports obsolete-effect trials (O), followed by terminal outcomes "
            "(T), over the number of trials. Duplicate terminal effects and blocked "
            "submissions are therefore visible even when no obsolete target is produced. "
            "Internal schedule identifiers are deliberately omitted.\n\n"
        )
        handle.write("| Controlled event schedule | " + " | ".join(variant_labels) + " |\n")
        handle.write("|---|" + "---:|" * len(variant_labels) + "\n")
        for schedule in schedules:
            cells: list[str] = []
            for variant in variants:
                row = summary_lookup[(schedule["reader_label"], variant["reader_label"])]
                cells.append(
                    f"O {row['obsolete_effects']}/{row['trials']}; "
                    f"T {row['terminal_outcomes']}/{row['trials']}"
                )
            handle.write(f"| {schedule['reader_label']} | " + " | ".join(cells) + " |\n")

    variant_markdown = output / "paper" / "variant-summary.md"
    with variant_markdown.open("w", encoding="utf-8") as handle:
        handle.write("# aggregate results by authority mechanism\n\n")
        handle.write(
            "| Authority mechanism | Trials | Obsolete-effect rate | Expected-outcome rate |\n"
        )
        handle.write("|---|---:|---:|---:|\n")
        for row in variant_summary:
            handle.write(
                f"| {row['variant_reader_label']} | {row['trials']} | "
                f"{row['obsolete_effect_rate']:.3f} | {row['expected_outcome_rate']:.3f} |\n"
            )

    return {
        "trials_csv": "summary/trials.csv",
        "schedule_summary_json": "summary/schedule-summary.json",
        "schedule_summary_csv": "summary/schedule-summary.csv",
        "variant_summary_json": "summary/variant-summary.json",
        "variant_summary_csv": "summary/variant-summary.csv",
        "paper_table_csv": "paper/controlled-authority-table.csv",
        "paper_table_markdown": "paper/controlled-authority-table.md",
        "paper_variant_summary": "paper/variant-summary.md",
    }


def git_revision() -> str:
    result = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=ROOT,
        check=False,
        capture_output=True,
        text=True,
    )
    return result.stdout.strip() if result.returncode == 0 else "unknown"


def paper_gate(
    protocol: dict[str, Any],
    manifests: list[dict[str, Any]],
    schedules: list[dict[str, Any]],
    variants: list[dict[str, Any]],
    seeds: list[int],
) -> dict[str, Any]:
    paper_seed = protocol["seed_sets"]["paper"]
    required_seeds = list(range(paper_seed["first"], paper_seed["first"] + paper_seed["count"]))
    complete = (
        [schedule["schedule_id"] for schedule in schedules]
        == list(protocol["expected_variant_outcomes"])
        and [variant["short_label"] for variant in variants] == ["B0", "B1", "B2", "B3"]
        and seeds == required_seeds
    )
    if not complete:
        return {
            "evaluated": False,
            "passed": None,
            "reason": "incomplete_paper_campaign",
        }

    full = [item for item in manifests if item["variant"]["short_label"] == "B3"]
    obsolete = sum(bool(item["metrics"]["obsolete_effect"]) for item in full)
    false_rejections = sum(
        bool(item["metrics"]["valid_current_result_rejected"]) for item in full
    )
    trace_failures = sum(
        not bool(item["metrics"]["canonical_trace_valid"]) for item in manifests
    )
    replay_mismatches = sum(
        not bool(item["metrics"]["task_replay_equal"])
        for item in manifests
        if item["schedule"]["internal_id"] == "F16"
    )
    witness_results: dict[str, bool] = {}
    for name, schedule_id in protocol["negative_control_witnesses"].items():
        witness_results[name] = any(
            item["schedule"]["internal_id"] == schedule_id
            and item["variant"]["short_label"] != "B3"
            and item["expected_outcome_met"]
            and not item["expected_outcome"].startswith("not_applicable")
            for item in manifests
        )
    expected_outcome_failures = sum(not item["expected_outcome_met"] for item in manifests)
    limits = protocol["paper_gate"]
    passed = (
        obsolete <= limits["maximum_full_obsolete_effects"]
        and false_rejections <= limits["maximum_full_false_rejections"]
        and trace_failures <= limits["maximum_full_trace_failures"]
        and replay_mismatches <= limits["maximum_full_replay_mismatches"]
        and (not limits["required_negative_control_exposure"] or all(witness_results.values()))
        and expected_outcome_failures == 0
    )
    return {
        "evaluated": True,
        "passed": passed,
        "full_variant_obsolete_effects": obsolete,
        "full_variant_false_rejections": false_rejections,
        "canonical_trace_failures": trace_failures,
        "replay_mismatches": replay_mismatches,
        "expected_outcome_failures": expected_outcome_failures,
        "negative_control_witnesses": witness_results,
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True, help="new campaign directory")
    parser.add_argument(
        "--engine",
        type=Path,
        default=ROOT / "build" / "muesli_bt_controlled_authority_campaign",
    )
    parser.add_argument("--protocol", type=Path, default=DEFAULT_PROTOCOL)
    parser.add_argument("--seed-set", choices=("engineering", "paper"), default="engineering")
    parser.add_argument("--seeds", help="comma-separated seed override")
    parser.add_argument("--schedules", help="comma-separated internal schedule selection")
    parser.add_argument("--variants", help="comma-separated variant short-label selection")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    output = args.output.resolve()
    if output.exists() and any(output.iterdir()):
        raise ValueError(f"output directory must be new or empty: {output}")
    output.mkdir(parents=True, exist_ok=True)

    protocol_path = args.protocol.resolve()
    protocol = load_json(protocol_path)
    catalogue_path = (protocol_path.parent / protocol["schedule_catalogue"]).resolve()
    catalogue = load_json(catalogue_path)
    available_schedules = [item["schedule_id"] for item in catalogue["schedules"]]
    available_variants = [item["short_label"] for item in protocol["variants"]]
    selected_schedule_ids = split_selection(args.schedules, available_schedules, "schedules")
    selected_variant_labels = split_selection(args.variants, available_variants, "variants")
    if "F16" in selected_schedule_ids and selected_schedule_ids != available_schedules:
        raise ValueError("replay requires all preceding schedules to be selected")
    schedules = [
        item for item in catalogue["schedules"] if item["schedule_id"] in selected_schedule_ids
    ]
    variants = [
        item for item in protocol["variants"] if item["short_label"] in selected_variant_labels
    ]
    seeds = resolve_seeds(protocol, args.seed_set, args.seeds)

    plan_path = output / "resolved-plan.tsv"
    expected_runs = write_plan(plan_path, protocol, catalogue, schedules, variants, seeds)
    engine = args.engine.resolve()
    if not engine.is_file():
        raise ValueError(f"campaign engine does not exist: {engine}")
    subprocess.run([str(engine), str(plan_path), str(output)], cwd=ROOT, check=True)

    raw_path = output / "raw_trials.jsonl"
    trials = read_raw_trials(raw_path)
    if len(trials) != expected_runs:
        raise RuntimeError(f"engine wrote {len(trials)} trials; expected {expected_runs}")
    schedule_by_id = {item["schedule_id"]: item for item in schedules}
    variant_by_label = {item["short_label"]: item for item in variants}
    manifests, trace_failures = write_trial_manifests(
        output, trials, schedule_by_id, variant_by_label, protocol, catalogue
    )
    artefacts = write_summaries(output, manifests, schedules, variants)

    campaign_material = json.dumps(
        {
            "protocol": sha256(protocol_path),
            "catalogue": sha256(catalogue_path),
            "common_task": sha256(EXPERIMENT / "lisp" / "common_task.lisp"),
            "seeds": seeds,
            "schedules": selected_schedule_ids,
            "variants": selected_variant_labels,
        },
        sort_keys=True,
    ).encode()
    campaign_id = "controlled-authority-" + hashlib.sha256(campaign_material).hexdigest()[:16]
    artefact_records = {
        name: {"path": path, "sha256": sha256(output / path)}
        for name, path in artefacts.items()
    }
    campaign_manifest = {
        "$schema": (
            "https://muesli-bt.invalid/schemas/controlled_authority/v1/"
            "campaign-manifest.schema.json"
        ),
        "schema_version": "controlled-authority.campaign-manifest.v1",
        "campaign_id": campaign_id,
        "created_at": datetime.now(timezone.utc).isoformat(),
        "protocol_id": protocol["protocol_id"],
        "catalogue_id": catalogue["catalogue_id"],
        "seed_set": args.seed_set if not args.seeds else "explicit",
        "seeds": seeds,
        "schedule_internal_ids": selected_schedule_ids,
        "schedule_reader_labels": [item["reader_label"] for item in schedules],
        "variant_short_labels": selected_variant_labels,
        "variant_reader_labels": [item["reader_label"] for item in variants],
        "trial_count": len(manifests),
        "expected_outcomes_met": sum(item["expected_outcome_met"] for item in manifests),
        "canonical_trace_failures": trace_failures,
        "campaign_valid": all(item["expected_outcome_met"] for item in manifests)
        and trace_failures == 0,
        "paper_gate": paper_gate(protocol, manifests, schedules, variants, seeds),
        "inputs": {
            "protocol": {"path": str(protocol_path), "sha256": sha256(protocol_path)},
            "catalogue": {"path": str(catalogue_path), "sha256": sha256(catalogue_path)},
            "common_task": {
                "path": str((EXPERIMENT / "lisp" / "common_task.lisp").resolve()),
                "sha256": sha256(EXPERIMENT / "lisp" / "common_task.lisp"),
            },
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
        "derived_artefacts": artefact_records,
        "manual_run_exclusion_allowed": False,
    }
    validate_against_schema(campaign_manifest, CAMPAIGN_MANIFEST_SCHEMA)
    write_json(output / "campaign-manifest.json", campaign_manifest)
    print(
        f"campaign {campaign_id}: {len(manifests)} trials, "
        f"{campaign_manifest['expected_outcomes_met']} expected outcomes met, "
        f"{trace_failures} trace failures"
    )
    return 0 if campaign_manifest["campaign_valid"] else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"controlled-authority campaign failed: {error}", file=sys.stderr)
        raise SystemExit(2) from error
