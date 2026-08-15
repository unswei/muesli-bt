#!/usr/bin/env python3
"""Run the MuJoCo Gate G5 vertical slice inside the pinned joint image."""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any

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

SCENARIOS = {
    "G5-fixed": {
        "tree": "lisp/bt_invocation_scoped.lisp",
        "predicates": {
            "g5_fixed_shot_completed",
            "g5_fixed_shot_current_dispatch_once",
        },
    },
    "H1": {
        "tree": "lisp/bt_invocation_scoped.lisp",
        "predicates": {
            "h1_current_commit_once",
            "h1_current_dispatch_once",
        },
    },
    "H2a": {
        "tree": "lisp/bt_deadline_only.lisp",
        "predicates": {
            "h2a_baseline_admits_stale_result",
            "h2a_obsolete_dispatch_observed",
        },
    },
    "H2b": {
        "tree": "lisp/bt_invocation_scoped.lisp",
        "predicates": {
            "h2b_changed_context_rejected",
            "h2b_zero_obsolete_dispatch",
        },
    },
}
RUN_MARKER = ".air-hockey-g5-run"


class GateG5Error(RuntimeError):
    """Gate G5 failed without converting partial output into evidence."""


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


def _read_jsonl(path: Path) -> list[dict[str, Any]]:
    rows = [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()]
    if not rows or not all(isinstance(row, dict) for row in rows):
        raise GateG5Error(f"event stream is empty or malformed: {path}")
    return rows


def _prepare_output(output: Path) -> Path:
    output = output.resolve()
    if output == output.parent:
        raise GateG5Error("Gate G5 output cannot be a filesystem root")
    if output.exists() and any(output.iterdir()):
        raise GateG5Error(f"refuse to replace non-empty Gate G5 output: {output}")
    output.mkdir(parents=True, exist_ok=True)
    (output / RUN_MARKER).write_text("airhockey.g5.run.v1\n", encoding="utf-8")
    return output


def _observed_predicates(stdout: str) -> set[str]:
    return {
        line.removeprefix("PREDICATE ").removesuffix(" PASS")
        for line in stdout.splitlines()
        if line.startswith("PREDICATE ") and line.endswith(" PASS")
    }


def _validate_public_boundary(records: list[dict[str, Any]]) -> None:
    if not records:
        raise GateG5Error("MuJoCo scenario produced no control steps")
    for record in records:
        public_state = record.get("public_state")
        if not isinstance(public_state, dict):
            raise GateG5Error("evaluation record omitted its public state")
        leaked = PROHIBITED_CONTROL_KEYS.intersection(_all_keys(public_state))
        if leaked:
            raise GateG5Error(
                f"privileged keys crossed into the host public state: {sorted(leaked)}"
            )
        if "privileged" not in record:
            raise GateG5Error("evaluation record omitted separated privileged scoring")


def _run_scenario(
    executable: Path,
    scenario: str,
    output: Path,
    schemas: SchemaRegistry,
) -> dict[str, Any]:
    definition = SCENARIOS[scenario]
    scenario_output = output / scenario.lower().replace("-", "_")
    scenario_output.mkdir()
    events_path = scenario_output / "events.jsonl"
    backend = MujocoDirectLaunchHostBackend()
    processor = ProtocolProcessor(schemas, backend)
    with tempfile.TemporaryDirectory(prefix=f"muesli-g5-{scenario.lower()}-") as directory:
        socket_path = Path(directory) / "host.sock"
        command = [
            str(executable),
            scenario,
            str(socket_path),
            str(EXAMPLE_ROOT / definition["tree"]),
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
    (scenario_output / "runner.stdout").write_text(
        completed.stdout, encoding="utf-8"
    )
    (scenario_output / "runner.stderr").write_text(
        completed.stderr, encoding="utf-8"
    )
    if completed.returncode != 0:
        backend.shutdown()
        raise GateG5Error(
            f"{scenario} runner exited with {completed.returncode}: "
            f"{completed.stderr.strip()}"
        )
    observed = _observed_predicates(completed.stdout)
    if observed != definition["predicates"]:
        backend.shutdown()
        raise GateG5Error(
            f"{scenario} predicate mismatch: expected {sorted(definition['predicates'])}, "
            f"observed {sorted(observed)}"
        )

    records = backend.evaluation_records()
    _validate_public_boundary(records)
    replay = backend.direct_replay_report()
    backend.shutdown()
    _write_jsonl(scenario_output / "evaluation-records.jsonl", records)
    _write_json(scenario_output / "direct-replay.json", replay)

    events = _read_jsonl(events_path)
    _validate_events(events, events_path)
    return {
        "events": len(events),
        "control_steps": len(records),
        "predicates": sorted(observed),
        "direct_replay": replay,
        "public_privileged_boundary": "passed",
        "runner_exit_code": completed.returncode,
    }


def run_gate(executable: Path, output: Path) -> dict[str, Any]:
    executable = executable.resolve()
    if not executable.is_file() or not os.access(executable, os.X_OK):
        raise GateG5Error(f"scenario runner is not executable: {executable}")
    output = _prepare_output(output)
    schemas = SchemaRegistry(REPOSITORY_ROOT / "schemas" / "air_hockey_host" / "v1")
    results = {
        scenario: _run_scenario(executable, scenario, output, schemas)
        for scenario in SCENARIOS
    }
    report = {
        "schema_version": "airhockey.g5.report.v1",
        "acra_revision": "1b6bbbbf19743b0042f01eabf0628eba5621cacf",
        "control_period_ms": 20,
        "fixed_shot": "DEFAULT_DIRECT_LAUNCH_SHOT",
        "muesli_bt_revision": _source_revision(),
        "paper_split_opened": False,
        "scenarios": results,
        "status": "passed",
    }
    _write_json(output / "g5-report.json", report)
    print(
        "air-hockey Gate G5 MuJoCo vertical slice passed: "
        "fixed shot, H1, H2a, H2b, direct replay and information boundary"
    )
    return report


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


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    arguments = parser.parse_args()
    run_gate(arguments.runner, arguments.out)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except GateG5Error as error:
        raise SystemExit(f"error: {error}") from error
