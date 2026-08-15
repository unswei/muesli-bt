#!/usr/bin/env python3
"""Generate, validate and analyse local air-hockey WP3 evidence bundles."""

from __future__ import annotations

import argparse
import tempfile
from pathlib import Path

from analysis.evidence import (
    DERIVED_ARTEFACTS,
    EvidenceError,
    campaign_summary,
    validate_campaign_report,
    write_json,
)
from analysis.synthetic import generate_campaign, generate_run


def write_campaign_outputs(report: dict, output: Path, force: bool) -> None:
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    summary_path = output / "campaign-summary.json"
    plot_path = output / "campaign-plot-fields.json"
    for path in (summary_path, plot_path):
        if path.exists() and not force:
            raise EvidenceError(
                f"refuse to replace analysis output without --force: {path}"
            )
    write_json(summary_path, report)
    write_json(
        plot_path,
        {
            "schema_version": "airhockey.campaign_plot_fields.v1",
            "raw_provenance_sha256": report["raw_provenance_sha256"],
            "plot_fields": report["plot_fields"],
        },
    )


def run_check() -> None:
    with tempfile.TemporaryDirectory(prefix="muesli-air-hockey-wp3-") as directory:
        root = Path(directory)
        runs = root / "runs"
        generated = generate_campaign(runs)
        if len(generated) != 8:
            raise EvidenceError("synthetic WP3 campaign must contain eight runs")
        report = campaign_summary(runs)
        validate_campaign_report(report)
        analysis = root / "analysis"
        write_campaign_outputs(report, analysis, force=False)
        for name in ("campaign-summary.json", "campaign-plot-fields.json"):
            if not (analysis / name).is_file():
                raise EvidenceError(f"campaign output was not regenerated: {name}")
        if report["pair_count"] != 4 or report["run_count"] != 8:
            raise EvidenceError("synthetic WP3 campaign has the wrong pair cardinality")
        if (
            report["integrity_intervals"]["baseline_obsolete_dispatch"]["successes"]
            != 4
        ):
            raise EvidenceError(
                "synthetic baseline did not expose every obsolete dispatch"
            )
        if report["integrity_intervals"]["full_obsolete_dispatch"]["successes"] != 0:
            raise EvidenceError(
                "synthetic invocation-scoped runs dispatched obsolete actions"
            )
        if report["paired_intervals"]["obsolete_target_motion"]["estimate"] >= 0.0:
            raise EvidenceError(
                "paired obsolete-target motion did not favour full authority"
            )
        if report["paired_intervals"]["save_rate"]["estimate"] != 0.75:
            raise EvidenceError("synthetic paired task outcome is not reproducible")
        for run_dir in generated:
            missing = [
                name for name in DERIVED_ARTEFACTS if not (run_dir / name).is_file()
            ]
            if missing:
                raise EvidenceError(f"derived WP3 artefacts are missing: {missing}")

        try:
            generate_run(runs, 0, "deadline_only", force=False)
        except EvidenceError:
            pass
        else:
            raise EvidenceError(
                "guarded replacement accepted an existing run without force"
            )
        replacement = generate_run(runs, 0, "deadline_only", force=True)
        if not replacement.is_dir():
            raise EvidenceError("guarded marked-directory replacement failed")

        unmarked_root = root / "unmarked-runs"
        unmarked = unmarked_root / "synthetic-pair-01-deadline-only"
        unmarked.mkdir(parents=True)
        (unmarked / "keep.txt").write_text("do not replace\n", encoding="utf-8")
        try:
            generate_run(unmarked_root, 0, "deadline_only", force=True)
        except EvidenceError:
            pass
        else:
            raise EvidenceError("guarded replacement accepted an unmarked directory")
        if not (unmarked / "keep.txt").is_file():
            raise EvidenceError("guarded replacement altered the unmarked directory")
    print("air-hockey Gate G3 synthetic campaign passed: 8 runs, 4 matched pairs")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    subparsers.add_parser("check", help="run the end-to-end synthetic Gate G3 check")
    generate = subparsers.add_parser(
        "generate-synthetic", help="write the deterministic synthetic raw campaign"
    )
    generate.add_argument("--out", required=True, type=Path)
    generate.add_argument("--force", action="store_true")
    analyse = subparsers.add_parser(
        "analyse",
        help="validate marked runs and regenerate all derived analysis fields",
    )
    analyse.add_argument("--runs", required=True, type=Path)
    analyse.add_argument("--out", required=True, type=Path)
    analyse.add_argument("--force", action="store_true")
    arguments = parser.parse_args()

    if arguments.command == "check":
        run_check()
        return 0
    if arguments.command == "generate-synthetic":
        runs = generate_campaign(arguments.out, force=arguments.force)
        print(f"generated {len(runs)} synthetic air-hockey runs under {arguments.out}")
        return 0
    if arguments.command == "analyse":
        report = campaign_summary(arguments.runs)
        validate_campaign_report(report)
        write_campaign_outputs(report, arguments.out, arguments.force)
        print(
            f"analysed {report['run_count']} runs in {report['pair_count']} matched pairs"
        )
        return 0
    raise EvidenceError(f"unsupported command: {arguments.command}")


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except EvidenceError as error:
        raise SystemExit(f"error: {error}") from error
