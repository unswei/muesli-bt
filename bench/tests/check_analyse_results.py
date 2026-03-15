#!/usr/bin/env python3

from __future__ import annotations

import subprocess
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("usage: check_analyse_results.py <analyse_results.py> <sample-result-dir>")

    script_path = Path(sys.argv[1]).resolve()
    sample_dir = Path(sys.argv[2]).resolve()

    completed = subprocess.run(
        [sys.executable, str(script_path), str(sample_dir)],
        check=True,
        capture_output=True,
        text=True,
    )

    output = completed.stdout
    expected_fragments = [
        "correctness",
        "single-leaf baseline: 84 ns median",
        "sequence traversal: 1.33 us at 31 nodes, 10.33 us at 255 nodes",
        "tick jitter: 10.83 us median, 12.00 us p99, 23.96 us p99.9, ratio 1.07x",
        "worst allocation pressure: B2-reactive-255-flip5-off at 33.00 alloc/tick",
        "single-leaf full trace: 54.07x slowdown",
        "Reactive interruption is the first optimisation target",
    ]
    for fragment in expected_fragments:
        if fragment not in output:
            raise AssertionError(f"missing expected analysis output: {fragment}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
