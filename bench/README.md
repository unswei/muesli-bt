# benchmark suite

## what this is

This directory contains the optional runtime benchmark harness for `muesli-bt`.

The current milestone covers:

- `A1` single-leaf baseline
- `A2` scheduler jitter on `alt-255`
- `B1` static tick overhead
- `B2` reactive interruption
- `B6` logging overhead for one static and one reactive scenario

The harness currently runs the native `muesli-bt` runtime through a small adapter layer. The layout leaves room for later `BehaviorTree.CPP` support without mixing benchmark code into `src/` or `tests/`.

## when to use it

Use this harness when you need reproducible runtime numbers rather than example/demo timing.

Typical uses:

- hot-path latency checks before runtime changes
- logging overhead checks
- scheduler jitter diagnosis
- paper or report data collection

## how it works

The harness builds tree fixtures directly against the C++ runtime API, not through the Lisp parser, so the first milestone isolates executor behaviour cleanly.

Each run:

- warms the scenario
- primes the hot path once outside timing
- records per-tick latency with `std::chrono::steady_clock`
- writes `run_summary.csv`, `aggregate_summary.csv`, and `environment_metadata.csv`
- writes `jitter_trace.csv` for `A2`

`schema_version=2` adds two latency interpretation columns:

- `latency_ns_p999`
- `jitter_ratio_p99_over_median`

## api / syntax

Build the optional harness:

```bash
cmake --preset bench-release
cmake --build --preset bench-release -j
```

List scenarios:

```bash
./build/bench-release/bench/bench list
```

The CLI prints one line at the start of the suite and one line at the start of each scenario. These messages are emitted before timing begins.

Run one scenario:

```bash
./build/bench-release/bench/bench run A1-single-leaf-off
```

Run one group:

```bash
./build/bench-release/bench/bench run-group B1
```

Run the jitter benchmark:

```bash
./build/bench-release/bench/bench run A2-alt-255-jitter-off
```

Summarise a completed benchmark session:

```bash
python3 bench/scripts/analyse_results.py bench/results/20260314T234021Z
```

If you omit the path, the script picks the latest result directory under `bench/results/`.

Fast local iteration:

```bash
./build/bench-release/bench/bench run B2-reactive-31-flip20-off --warmup-ms 100 --run-ms 500 --repetitions 3
```

## example

Write results into a fixed output directory:

```bash
./build/bench-release/bench/bench run-group B1 --output-dir bench/results/static-sample
```

The output directory will contain:

- `run_summary.csv`
- `aggregate_summary.csv`
- `environment_metadata.csv`
- `jitter_trace.csv` when the selected scenarios include `A2`

For a quick human-readable summary of those CSV files:

```bash
python3 bench/scripts/analyse_results.py
```

## gotchas

- Use the `bench-release` preset. Debug builds distort the numbers.
- `B1` selector and alternating fixtures force deterministic full traversal with trivial success/failure leaves. This keeps tree size meaningful without changing BT semantics.
- `A2` writes one row per tick. That file can become large on fast machines.

## see also

- [README.md](/Users/z3550628/Code/2026/muesli-bt/README.md)
- [docs/testing.md](/Users/z3550628/Code/2026/muesli-bt/docs/testing.md)
- [docs/internals/code-map.md](/Users/z3550628/Code/2026/muesli-bt/docs/internals/code-map.md)
