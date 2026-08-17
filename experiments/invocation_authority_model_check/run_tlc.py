#!/usr/bin/env python3
"""Run the bounded TLC models and check their expected outcomes."""

from __future__ import annotations

import argparse
import pathlib
import subprocess
import sys
import tempfile


MODEL_DIR = pathlib.Path(__file__).resolve().parent
CONFIG_EXPECTATIONS = {
    "full.cfg": ("pass", "Model checking completed. No error has been found."),
    "deadline_only.cfg": ("counterexample", "Invariant Safety is violated."),
    "missing_entry_epoch.cfg": ("counterexample", "Invariant Safety is violated."),
    "missing_generation.cfg": ("counterexample", "Invariant Safety is violated."),
    "missing_context.cfg": ("counterexample", "Invariant Safety is violated."),
    "missing_dispatch_revalidation.cfg": (
        "counterexample",
        "Invariant Safety is violated.",
    ),
    "missing_terminal_latch.cfg": ("counterexample", "Invariant Safety is violated."),
}
TRACE_EXPECTATIONS = {
    "missing_generation.cfg": (
        '/\\ currentGeneration = 1',
        '/\\ capturedGeneration = 0',
        '/\\ requestState = "admitted"',
        '/\\ badAdmission = TRUE',
    ),
}


def run_configuration(jar: pathlib.Path, config: str) -> tuple[bool, str]:
    expected, marker = CONFIG_EXPECTATIONS[config]
    with tempfile.TemporaryDirectory(prefix="muesli-tlc-") as metadir:
        completed = subprocess.run(
            [
                "java",
                "-cp",
                str(jar),
                "tlc2.TLC",
                "-deadlock",
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
    outcome_matches = (
        (expected == "pass" and completed.returncode == 0)
        or (expected == "counterexample" and completed.returncode != 0)
    )
    trace_matches = all(
        trace_marker in output
        for trace_marker in TRACE_EXPECTATIONS.get(config, ())
    )
    return outcome_matches and marker in output and trace_matches, output


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--jar", type=pathlib.Path, required=True)
    args = parser.parse_args()

    if not args.jar.is_file():
        print(f"TLA+ tools jar not found: {args.jar}", file=sys.stderr)
        return 2

    failed = False
    for config in CONFIG_EXPECTATIONS:
        passed, output = run_configuration(args.jar, config)
        label = "PASS" if passed else "FAIL"
        print(f"[{label}] {config}")
        if not passed:
            failed = True
            print(output)
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
