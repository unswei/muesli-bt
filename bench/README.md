# benchmark suite

## what this is

This directory contains the optional runtime benchmark harness for `muesli-bt`.

The current milestone covers:

- `A1` single-leaf baseline
- `A2` scheduler jitter on `alt-255`
- `B1` static tick overhead
- `B2` reactive interruption
- `B5` parse, compile, load, and instantiate cost
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

The harness uses two execution paths:

- `A1`, `A2`, `B1`, `B2`, and `B6` build tree fixtures directly against the C++ runtime API so executor cost is isolated cleanly
- `B5` generates DSL source from the same fixtures so parse, compile, load, and instantiation phases can be measured separately

Each run:

- warms the scenario
- primes the hot path once outside timing
- records per-tick latency with `std::chrono::steady_clock`
- writes `run_summary.csv`, `aggregate_summary.csv`, and `environment_metadata.csv`
- writes `jitter_trace.csv` for `A2`

For `B6`, the current harness records full-trace capture with deferred JSONL serialisation when no file or ring sink is enabled. `log_bytes_total` still reports the canonical `mbt.evt.v1` line size that would be emitted.

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

Run one compile/load scenario:

```bash
./build/bench-release/bench/bench run B5-alt-255-compile-off
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

For `B5`, the latency columns record one phase execution per repetition:

- `parse`: DSL text to parsed Lisp form
- `compile`: parsed Lisp form to `bt::definition`
- `inst1`: one `bt::instance`
- `inst100`: one hundred `bt::instance` objects from one compiled definition
- `loadbin`: load a pre-saved binary definition
- `loaddsl`: read DSL text from file and compile it

For `B5`, `ticks_total` and `ticks_per_second` should be read as operations and operations per second rather than BT ticks.

For a quick human-readable summary of those CSV files:

```bash
python3 bench/scripts/analyse_results.py
```

## gotchas

- Use the `bench-release` preset. Debug builds distort the numbers.
- `B1` selector and alternating fixtures force deterministic full traversal with trivial success/failure leaves. This keeps tree size meaningful without changing BT semantics.
- `A2` writes one row per tick. That file can become large on fast machines.
- `B5` writes per-phase latency into the same CSV schema as the tick benchmarks. Read those rows as lifecycle operations, not executor ticks.
- `B6` full-trace rows currently measure capture overhead with deferred serialisation, not forced inline file output.

## see also

- [README.md](/Users/z3550628/Code/2026/muesli-bt/README.md)
- [docs/testing.md](/Users/z3550628/Code/2026/muesli-bt/docs/testing.md)
- [docs/internals/code-map.md](/Users/z3550628/Code/2026/muesli-bt/docs/internals/code-map.md)
