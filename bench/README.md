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
- `B7` GC and memory evidence smoke runs
- `B8` async cancellation contract edge smoke runs

The harness currently supports:

- native `muesli-bt`
- optional `BehaviorTree.CPP` comparison runs, pinned to `4.9.0`

The comparison runtime is limited to the shared subset for `A1`, `A2`, `B1`, `B2`, and the comparable `B5` phases (`compile`, `inst1`, `inst100`, `loaddsl`). `B6`, `B7`, and `B8` remain `muesli-bt` only.

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
- writes `run_summary.csv`, `aggregate_summary.csv`, `environment_metadata.csv`, and `experiment_manifest.json`
- writes `jitter_trace.csv` for `A2`

For `B6`, the current harness records full-trace capture with deferred JSONL serialisation when no file or ring sink is enabled. `log_bytes_total` still reports the canonical `mbt.evt.v1` line size that would be emitted.

`schema_version=5` adds paper-facing async/fallback rate columns: fallback activation rate, dropped-completion rate, and aggregate deadline miss counts. `schema_version=4` added first-class async outcome columns for deadline miss rate, fallback activation count, and dropped-completion count. `schema_version=3` added GC and memory evidence columns for `B7`, including GC pause quantiles, collection count, heap-live slope, RSS slope, and event-log bytes per tick.

`schema_version=2` added two latency interpretation columns:

- `latency_ns_p999`
- `jitter_ratio_p99_over_median`

## api / syntax

Build the optional harness:

```bash
cmake --preset bench-release
cmake --build --preset bench-release -j
```

Build with the optional `BehaviorTree.CPP` adapter:

```bash
cmake --preset bench-release-btcpp
cmake --build --preset bench-release-btcpp -j
```

List scenarios:

```bash
./build/bench-release/bench/bench list
```

Run the comparison subset against `BehaviorTree.CPP`:

```bash
./build/bench-release-btcpp/bench/bench run-all --runtime btcpp
```

Unsupported scenarios are skipped automatically for the selected runtime. For `btcpp`, that means `B6`, `B5` `parse`, and `B5` `loadbin` are omitted.

`run-all` is the reasonable whole-catalogue runner. It keeps the default smoke-quality `B7` and `B8` settings and is useful for regression sweeps. Use the publication script below when collecting paper-facing evidence.

The CLI prints one line at the start of the suite and one line at the start of each scenario. These messages are emitted before timing begins.

Run the strict allocation CTest lane for precompiled BT ticks:

```bash
ctest --preset bench-release -R muesli_bt_bench_precompiled_tick_allocation_strict --output-on-failure
```

The strict lane warms and primes precompiled static and reactive BT shapes before enabling allocation failure for the measured tick loop. It covers the `B1` `seq`, `sel`, and `alt` shapes, a representative `B2` reactive shape, and a logging-on `B6` full-trace shape. Allocations are only allowed inside explicit benchmark allocation whitelist scopes reserved for canonical logging paths. Logging-off cases expect zero total allocations and zero whitelist usage.

Run the curated publication suite:

```bash
python3 bench/scripts/run_publication_benchmarks.py
```

This writes one timestamped bundle under `bench/results/` with separate result directories for the baseline, static tick, reactive interruption, logging, tail-latency, lifecycle, memory/GC, and async contract groups. It also writes per-directory `analysis.txt` summaries and generates the checked-in figure/report outputs where applicable.

Run the same suite with the optional `BehaviorTree.CPP` comparison subset:

```bash
python3 bench/scripts/run_publication_benchmarks.py --with-btcpp
```

`--with-btcpp` configures and builds the `bench-release-btcpp` preset unless `--skip-build` is also passed. That preset uses CMake `FetchContent` to fetch the pinned `BehaviorTree.CPP` `4.9.0` source into the build tree, usually under `build/bench-release-btcpp/_deps/`. No system-wide `BehaviorTree.CPP` install or fixed external checkout path is required.

If `--skip-build --with-btcpp` is used, the script expects the comparison binary to already exist at `build/bench-release-btcpp/bench/bench`. Override that path with `--btcpp-bench-bin /path/to/bench` when using a prebuilt binary.

Check the script path without collecting publication-length data:

```bash
python3 bench/scripts/run_publication_benchmarks.py --profile smoke --skip-build
```

Run one scenario:

```bash
./build/bench-release/bench/bench run A1-single-leaf-off
```

Run one group:

```bash
./build/bench-release/bench/bench run-group B1
```

Run the GC and memory evidence smoke group:

```bash
./build/bench-release/bench/bench run-group B7
```

`B7` writes canonical `events.jsonl` files under each scenario/repetition directory. Use a longer run duration when collecting paper-facing heap and RSS slope evidence:

```bash
./build/bench-release/bench/bench run-group B7 --run-ms 30000 --repetitions 5
```

Run the async cancellation contract edge smoke group:

```bash
./build/bench-release/bench/bench run-group B8
```

`B8` covers the five checked-in async fixture edges: cancel before start, cancel while running, cancel after timeout, repeated cancel, and late completion after cancellation. The benchmark records operation latency, cancellation latency, deadline miss count/rate, fallback activation count/rate, dropped-completion count/rate, and semantic-error counts. Each repetition also keeps the matching canonical `events.jsonl` under the scenario result directory, so async lifecycle claims can be inspected from the event stream rather than only from CSV summaries.

Run one group against `BehaviorTree.CPP`:

```bash
./build/bench-release-btcpp/bench/bench run-group B1 --runtime btcpp
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
python3 bench/scripts/analyse_results.py bench/results/my-run
```

Generate the current tail-latency figure:

```bash
python3 bench/scripts/figure_tail_latency.py \
  bench/results/my-run \
  --output bench/results/my-run/tail_latency.svg
```

Generate the current memory/GC figure:

```bash
python3 bench/scripts/figure_memory_gc.py \
  bench/results/my-run \
  --event-log build/dev/gc-events.jsonl \
  --output bench/results/my-run/memory_gc.svg
```

`figure_memory_gc.py` also searches for `events.jsonl` files under the benchmark result directory. If no `gc_end` events are available yet, it still renders the allocation/RSS panel and marks the GC evidence as pending.

Write a compact evidence report for the same result directory:

```bash
python3 bench/scripts/write_evidence_report.py \
  bench/results/my-run \
  --event-log build/dev/gc-events.jsonl
```

If you omit the path, the script picks the latest result directory under `bench/results/`.
The summary covers `A1`, `A2`, `B1`, `B2`, `B5`, `B6`, `B7`, and `B8` when those rows are present.
The same script also summarises `BehaviorTree.CPP` result sets; unsupported groups simply report as absent.

Compare two completed result sets directly:

```bash
python3 bench/scripts/compare_results.py \
  bench/results/muesli-run \
  bench/results/btcpp-run
```

Fast local iteration:

```bash
./build/bench-release/bench/bench run B2-reactive-31-flip20-off --warmup-ms 100 --run-ms 500 --repetitions 3
```

## example

Write results into a fixed output directory:

```bash
./build/bench-release/bench/bench run-group B1 --output-dir bench/results/static-sample
```

Write a comparison run into a dedicated directory:

```bash
./build/bench-release-btcpp/bench/bench run-all --runtime btcpp --output-dir bench/results/btcpp-run
```

The output directory will contain:

- `run_summary.csv`
- `aggregate_summary.csv`
- `environment_metadata.csv`
- `experiment_manifest.json`
- `jitter_trace.csv` when the selected scenarios include `A2`
- `B7-*/rep-*/events.jsonl` when the selected scenarios include `B7`
- `B8-*/rep-*/events.jsonl` when the selected scenarios include `B8`
- `tail_latency.svg` when generated with `bench/scripts/figure_tail_latency.py`
- `memory_gc.svg` when generated with `bench/scripts/figure_memory_gc.py`
- `evidence_report.md` when generated with `bench/scripts/write_evidence_report.py`

`bench/results/` is intentionally ignored by Git. Local result directories can be large because `A2` writes one latency row per tick. Summarise or trim a result set before linking it from release notes, papers, or the top-level README.

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

For a direct cross-runtime ratio summary:

```bash
python3 bench/scripts/compare_results.py \
  bench/results/muesli-run \
  bench/results/btcpp-run
```

## gotchas

- Use the `bench-release` preset. Debug builds distort the numbers.
- Use `bench-release-btcpp` when you want the optional `BehaviorTree.CPP` adapter. The default `bench-release` preset does not include that runtime.
- `B1` selector and alternating fixtures force deterministic full traversal with trivial success/failure leaves. This keeps tree size meaningful without changing BT semantics.
- `A2` writes one row per tick. That file can become large on fast machines.
- `B5` writes per-phase latency into the same CSV schema as the tick benchmarks. Read those rows as lifecycle operations, not executor ticks.
- `B6` full-trace rows currently measure capture overhead with deferred serialisation, not forced inline file output.
- The strict allocation CTest lane is a guardrail for precompiled steady-state ticks. Warm-up, compilation, instantiation, and ordinary benchmark CSV writing happen outside the guarded section.
- `B7` default scenarios are smoke runs. Use longer `--run-ms` and more repetitions before treating heap-live or RSS slope as paper evidence.
- `B8` default scenarios are smoke runs. Use longer `--run-ms` and more repetitions before treating async cancellation latency as paper evidence.
- The CSV files are summaries. Keep the canonical `events.jsonl` artefacts with result bundles whenever making GC pause, heap-live, cancellation, timeout, or late-completion claims.
- `BehaviorTree.CPP` comparison runs are pinned to release `4.9.0` and the common semantic subset. Do not treat skipped groups as missing data bugs.
- `compare_results.py` assumes both result sets were collected under meaningfully similar machine and build settings. It prints a warning when the recorded environment metadata differ.

## see also

- [README.md](/Users/z3550628/Code/2026/muesli-bt/README.md)
- [docs/testing.md](/Users/z3550628/Code/2026/muesli-bt/docs/testing.md)
- [docs/internals/code-map.md](/Users/z3550628/Code/2026/muesli-bt/docs/internals/code-map.md)
