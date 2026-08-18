#!/usr/bin/env python3
"""Run the finite TLC models and check their expected outcomes."""

from __future__ import annotations

import argparse
import dataclasses
import pathlib
import re
import subprocess
import sys
import tempfile


MODEL_DIR = pathlib.Path(__file__).resolve().parent


@dataclasses.dataclass(frozen=True)
class Expectation:
    outcome: str
    trace: tuple[str, ...] = ()
    trace_markers: tuple[str, ...] = ()


CONFIG_EXPECTATIONS = {
    "full.cfg": Expectation("pass"),
    "deadline_only.cfg": Expectation(
        "counterexample", ("Start", "BranchExit", "Complete", "AdmitAccepted")
    ),
    "missing_entry_epoch.cfg": Expectation(
        "counterexample",
        ("Start", "BranchExit", "Reenter", "Complete", "AdmitAccepted"),
    ),
    "missing_generation.cfg": Expectation(
        "counterexample",
        ("Start", "Supersede", "Complete", "AdmitAccepted"),
        (
            '/\\ currentGeneration = 1',
            '/\\ capturedGeneration = 0',
            '/\\ requestState = "admitted"',
            '/\\ badAdmission = TRUE',
        ),
    ),
    "missing_context.cfg": Expectation(
        "counterexample", ("Start", "ContextChange", "Complete", "AdmitAccepted")
    ),
    "missing_dispatch_revalidation.cfg": Expectation(
        "counterexample",
        ("Start", "Complete", "AdmitAccepted", "BranchExit", "Dispatch"),
    ),
    "missing_terminal_latch.cfg": Expectation(
        "counterexample", ("Start", "Complete", "DuplicateComplete")
    ),
}

FULL_GENERATED_STATES = 436
FULL_REACHABLE_STATES = 186
FULL_GRAPH_DEPTH = 11

TRACE_ACTION_RE = re.compile(r"State \d+: <([A-Za-z][A-Za-z0-9_]*) line")
STATS_RE = re.compile(
    r"(\d+) states generated, (\d+) distinct states found, "
    r"(\d+) states left on queue\."
)
DEPTH_RE = re.compile(r"The depth of the complete state graph search is (\d+)\.")


def run_configuration(jar: pathlib.Path, config: str) -> tuple[bool, str, str]:
    expected = CONFIG_EXPECTATIONS[config]
    with tempfile.TemporaryDirectory(prefix="muesli-tlc-") as metadir:
        completed = subprocess.run(
            [
                "java",
                "-cp",
                str(jar),
                "tlc2.TLC",
                "-deadlock",
                "-nowarning",
                "-workers",
                "1",
                "-metadir",
                metadir,
                "-config",
                config,
                "InvocationAuthority",
            ],
            cwd=MODEL_DIR,
            check=False,
            text=True,
            capture_output=True,
        )
    output = completed.stdout + completed.stderr
    trace = tuple(TRACE_ACTION_RE.findall(output))
    stats_match = STATS_RE.search(output)
    depth_match = DEPTH_RE.search(output)
    stats = (
        tuple(int(value) for value in stats_match.groups())
        if stats_match is not None
        else None
    )
    depth = int(depth_match.group(1)) if depth_match is not None else None
    outcome_matches = (
        (
            expected.outcome == "pass"
            and completed.returncode == 0
            and "Model checking completed. No error has been found." in output
        )
        or (
            expected.outcome == "counterexample"
            and completed.returncode != 0
            and "Invariant Safety is violated." in output
        )
    )
    trace_matches = all(
        trace_marker in output for trace_marker in expected.trace_markers
    )
    if expected.outcome == "pass":
        fixed_point_matches = stats == (
            FULL_GENERATED_STATES,
            FULL_REACHABLE_STATES,
            0,
        ) and depth == FULL_GRAPH_DEPTH
        detail = (
            f"fixed point; {stats[1]} reachable ({stats[0]} generated), "
            f"depth {depth}"
            if stats is not None
            else "missing state-space statistics"
        )
    else:
        fixed_point_matches = trace == expected.trace
        detail = (
            f"shortest counterexample {len(trace)} transitions "
            f"({' -> '.join(trace)})"
        )
    passed = outcome_matches and trace_matches and fixed_point_matches
    return passed, output, detail


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--jar", type=pathlib.Path, required=True)
    args = parser.parse_args()

    if not args.jar.is_file():
        print(f"TLA+ tools jar not found: {args.jar}", file=sys.stderr)
        return 2

    failed = False
    for config in CONFIG_EXPECTATIONS:
        passed, output, detail = run_configuration(args.jar, config)
        label = "PASS" if passed else "FAIL"
        print(f"[{label}] {config}: {detail}")
        if not passed:
            failed = True
            print(output)
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
