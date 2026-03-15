#!/usr/bin/env python3

from __future__ import annotations

import subprocess
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 4:
        raise SystemExit(
            "usage: check_compare_results.py <compare_results.py> <left-sample-result-dir> <right-sample-result-dir>"
        )

    script_path = Path(sys.argv[1]).resolve()
    left_sample_dir = Path(sys.argv[2]).resolve()
    right_sample_dir = Path(sys.argv[3]).resolve()

    completed = subprocess.run(
        [sys.executable, str(script_path), str(left_sample_dir), str(right_sample_dir)],
        check=True,
        capture_output=True,
        text=True,
    )

    output = completed.stdout
    expected_fragments = [
        "result sets",
        "comparison hygiene",
        "execution conditions match: Apple M3 | Darwin 24.6.0 | 8 physical / 8 logical cores",
        "single-leaf baseline: muesli-bt 84 ns, 7.41 Mticks/s vs BehaviorTree.CPP 125 ns, 6.26 Mticks/s; muesli-bt 1.49x lower latency",
        "sequence traversal: muesli-bt 10.33 us at 255 nodes and 40.18 ns/node vs BehaviorTree.CPP 28.08 us at 255 nodes and 110.09 ns/node; muesli-bt 2.74x lower per-node cost",
        "tick jitter: muesli-bt 10.83 us median, 12.00 us p99, ratio 1.07x vs BehaviorTree.CPP 28.04 us median, 31.29 us p99, ratio 1.12x; muesli-bt 2.59x lower absolute tick latency",
        "reactive interruption worst case: muesli-bt B2-reactive-255-flip5-off 6.08 us vs BehaviorTree.CPP B2-reactive-255-flip100-off 13.83 us; muesli-bt 2.27x lower interruption latency",
        "reactive interruption allocations: muesli-bt 33.00 alloc/tick worst case vs BehaviorTree.CPP 0.00 alloc/tick worst case; BehaviorTree.CPP reached zero timed allocations",
        "compile BT: 31 nodes 13.88 us vs 48.08 us (muesli-bt 3.47x faster); 255 nodes 72.25 us vs 120.42 us (muesli-bt 1.67x faster); 1023 nodes 333.00 us vs 488.71 us (muesli-bt 1.47x faster)",
        "instantiate 100 per instance: 31 nodes 625 ns vs 20.82 us (muesli-bt 33.31x faster); 255 nodes 3.58 us vs 139.10 us (muesli-bt 38.82x faster); 1023 nodes 17.12 us vs 573.63 us (muesli-bt 33.50x faster)",
        "muesli-only phases not compared: parse DSL, load binary",
    ]
    for fragment in expected_fragments:
        if fragment not in output:
            raise AssertionError(f"missing expected comparison output: {fragment}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
