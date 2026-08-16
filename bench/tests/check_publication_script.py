#!/usr/bin/env python3

from __future__ import annotations

import subprocess
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("usage: check_publication_script.py <run_publication_benchmarks.py> <output-root>")

    script_path = Path(sys.argv[1]).resolve()
    output_root = Path(sys.argv[2]).resolve()

    completed = subprocess.run(
        [
            sys.executable,
            str(script_path),
            "--profile",
            "smoke",
            "--skip-build",
            "--with-btcpp",
            "--dry-run",
            "--output-root",
            str(output_root),
        ],
        check=True,
        capture_output=True,
        text=True,
    )

    output = completed.stdout
    for fragment in (
        "muesli-b8-async-contract",
        "muesli-b9-generated-subtree-contract",
        "run-group B9",
        "--warmup-ms 25 --run-ms 75 --repetitions 1",
    ):
        if fragment not in output:
            raise AssertionError(f"publication dry-run missing expected output: {fragment}")

    if "btcpp" not in output:
        raise AssertionError("publication dry-run should still include the optional btcpp subset")

    comparison = subprocess.run(
        [
            sys.executable,
            str(script_path),
            "--profile",
            "smoke",
            "--skip-build",
            "--with-btcpp",
            "--comparison-only",
            "--dry-run",
            "--output-root",
            str(output_root),
        ],
        check=True,
        capture_output=True,
        text=True,
    ).stdout
    for fragment in ("muesli-a1-baseline", "btcpp-b5-lifecycle", "compare_results.py"):
        if fragment not in comparison:
            raise AssertionError(f"comparison-only dry-run missing expected output: {fragment}")
    for excluded in (
        "muesli-b6-logging",
        "muesli-b7-memory-gc",
        "muesli-b8-async-contract",
        "muesli-b9-generated-subtree-contract",
    ):
        if excluded in comparison:
            raise AssertionError(f"comparison-only dry-run included unsupported run: {excluded}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
