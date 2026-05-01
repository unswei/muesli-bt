# testing and verification

## test layout

Core test binaries:

- `tests/test_main.cpp` (`muslisp_tests`)
- `tests/conformance/test_conformance_main.cpp` (`muesli_bt_conformance_tests`, L0)

Coverage includes:

- reader and parser
- evaluator and closures
- numeric semantics and predicates
- GC and GC safety during evaluation
- BT compile checks
- BT runtime status propagation
- decorators and reset behaviour
- blackboard, canonical events, scheduler stats
- bounded-time planner service (`planner.plan`) and `plan-action` node semantics
- async capability/VLA surface (`cap.*`, `vla.*`, handle metadata, JSON conversion)
- VLA BT nodes (`vla-request`, `vla-wait`, `vla-cancel`) including cancel flow
- [host](terminology.md#host) wrappers and typed robot interface injection
- `env.run-loop` multi-episode semantics for reset-capable and reset-less backends
- generic `env.*` backend contract checks for PyBullet and ROS2 adapters
- runtime-contract L0 conformance checks (tick ordering, budget/deadline hooks, async lifecycle, determinism)

## canonical event fixture suite

Canonical fixtures are stored under `tests/fixtures/mbt.evt.v1/` and validated in CI:

- `minimal_run.jsonl`
- `planner_run.jsonl`
- `scheduler_run.jsonl`
- `scheduler_cancel_run.jsonl` (async cancellation edge case)
- `vla_run.jsonl`
- `vla_cancel_run.jsonl` (async cancellation edge case)
- `vla_late_completion_drop_run.jsonl` (late completion dropped after cancellation)
- `deadline_fallback_run.jsonl` (deadline exceeded with safe fallback)
- `resetless_unsupported_run.jsonl` (multi-episode request on reset-less backend)

Runtime-contract fixture bundles for reproducibility are stored under `fixtures/`:

- `fixtures/budget-warning-case/`
- `fixtures/deadline-cancel-case/`
- `fixtures/late-completion-drop-case/`
- `fixtures/determinism-replay-case/`
- `fixtures/async-cancel-before-start-case/`
- `fixtures/async-cancel-while-running-case/`
- `fixtures/async-cancel-after-timeout-case/`
- `fixtures/async-repeated-cancel-case/`
- `fixtures/async-late-completion-after-cancel-case/`

## run tests

```bash
ctest --preset dev
```

or:

```bash
./build/dev/muslisp_tests
./build/dev/muesli_bt_conformance_tests
```

Validate and verify fixtures:

```bash
python3 tools/validate_log.py --schema schemas/event_log/v1/mbt.evt.v1.schema.json tests/fixtures/mbt.evt.v1/*
python3 tools/validate_trace.py check fixtures/determinism-replay-case
python3 tools/fixtures/verify_fixture.py
```

Verify docs snippet freshness against `examples/**` source files:

```bash
python3 scripts/check_docs_snippet_freshness.py
```

Verify ROS-backed replay artefacts from the canonical event log:

```bash
python3 tools/verify_ros2_l2_artifacts.py \
  --artifact-root build/linux-ros2-l2/ros2_l2_artifacts
python3 tools/validate_log.py build/linux-ros2-l2/ros2_l2_artifacts/ros2_h1_success
```

This tool is mainly for CI, release verification, replay/conformance checks, and regression diagnosis.
It is not part of the normal robot control path.

The same canonical consumer path also applies to simulator-backed and fixture-backed runs:

```bash
python3 tools/validate_log.py fixtures/determinism-replay-case
python3 tools/validate_trace.py check fixtures/determinism-replay-case
python3 tools/validate_trace.py compare \
  fixtures/determinism-replay-case/events.jsonl \
  fixtures/determinism-replay-case/events.jsonl \
  --profile deterministic
python3 tools/validate_log.py build/linux-ros2-l2/ros2_l2_artifacts/ros2_h1_success
```

`tools/validate_log.py` checks per-record schema conformance only.
`tools/validate_trace.py` checks cross-event properties such as `seq` ordering, completed tick delimitation, terminal `node_exit` uniqueness, `deadline_exceeded` evidence for over-budget ticks, async lifecycle ordering, and deterministic replay comparison after configured normalisation.
Replay comparison reports the first divergence with the event index, tick, event type, field path, and any available node id, blackboard key, async job id, planner id, or host capability.

## deterministic BT tests

For deterministic BT tests:

- keep leaf callbacks small and explicit
- isolate one semantic rule per test
- avoid reliance on wall-clock timing where possible
- when async behaviour is needed, bound wait loops tightly

For replay expectations:

- treat `seq` as the strict ordering key
- treat `tick` as the BT execution index
- treat `unix_ms` and ROS observation timestamps as bounded timing metadata, not as the primary correctness oracle
- treat timestamp-only drift differently from event-order or decision-payload drift
- for long multi-episode runs, inspect `episode_end` and `run_end` first before drilling down into every `tick_end`

## benchmark suite

The optional benchmark harness lives under `bench/` and is built with the `bench-release` preset:

```bash
cmake --preset bench-release
cmake --build --preset bench-release -j
```

Build with optional `BehaviorTree.CPP` `4.9.0` comparison support:

```bash
cmake --preset bench-release-btcpp
cmake --build --preset bench-release-btcpp -j
```

List or run scenarios:

```bash
./build/bench-release/bench/bench list
./build/bench-release/bench/bench run A1-single-leaf-off
./build/bench-release/bench/bench run-group B1
./build/bench-release/bench/bench run A2-alt-255-jitter-off
```

Run the comparable subset against `BehaviorTree.CPP`:

```bash
./build/bench-release-btcpp/bench/bench run-all --runtime btcpp
```

`run-all` is the reasonable whole-catalogue runner. Use the benchmark collection script for curated evidence runs with longer durations and stronger repetition counts:

```bash
python3 bench/scripts/run_publication_benchmarks.py
```

Add `--with-btcpp` when the optional comparison preset is available. The script writes one timestamped bundle under `bench/results/`, including per-run summaries, per-benchmark manifests, and generated figure/report artefacts.

Each benchmark result directory writes:

- `run_summary.csv`
- `aggregate_summary.csv`
- `environment_metadata.csv`
- `experiment_manifest.json`
- `jitter_trace.csv` for the `A2` scheduler jitter run

Current harness coverage includes:

- `A1` single-leaf baseline
- `A2` scheduler jitter
- `B1` static tick overhead
- `B2` reactive interruption
- `B5` parse, compile, load, and instantiate cost
- `B6` logging overhead
- `B7` GC and memory evidence smoke runs
- `B8` async cancellation contract edge smoke runs

For `BehaviorTree.CPP`, the harness currently covers:

- `A1` single-leaf baseline
- `A2` scheduler jitter
- `B1` static tick overhead
- `B2` reactive interruption
- `B5` `compile`, `inst1`, `inst100`, and `loaddsl`

`B6`, `B5 parse`, and `B5 loadbin` are intentionally omitted from the cross-runtime run because they are not a fair shared subset.

Run one `B5` phase benchmark:

```bash
./build/bench-release/bench/bench run B5-alt-255-compile-off
```

Run the GC and memory benchmark group:

```bash
./build/bench-release/bench/bench run-group B7
```

`B7` writes per-repetition canonical `events.jsonl` files with `gc_begin` and `gc_end` lifecycle events. Use longer durations for release evidence:

```bash
./build/bench-release/bench/bench run-group B7 --run-ms 30000 --repetitions 5
```

Run the async cancellation contract edge benchmark group:

```bash
./build/bench-release/bench/bench run-group B8
```

`B8` covers cancel before start, cancel while running, cancel after timeout, repeated cancel, and late completion after cancellation. These scenarios mirror the checked-in `fixtures/async-*` bundles and record cancellation latency, deadline miss count/rate, fallback activation count/rate, dropped-completion count/rate, and semantic-error counts in the normal benchmark CSV files. Each repetition also keeps the matching canonical `events.jsonl` under the scenario result directory.

Run the strict precompiled-tick allocation lane:

```bash
ctest --preset bench-release -R muesli_bt_bench_precompiled_tick_allocation_strict --output-on-failure
```

This lane warms and primes precompiled static and reactive BT shapes, enables allocation failure for the steady-state tick loop, and only permits allocations inside explicitly whitelisted logging paths. It covers all `B1` static shapes, a representative `B2` reactive shape, and a logging-on `B6` full-trace shape. Logging-off cases expect zero total allocations and zero whitelist usage.

The current `B6` full-trace benchmark path uses deferred event-log serialisation when no file sink is enabled. The reported `log_bytes_total` still reflects canonical `mbt.evt.v1` line size.

Summarise the latest benchmark result set:

```bash
python3 bench/scripts/analyse_results.py
```

Generate checked-in-script figure outputs from a benchmark result set:

```bash
python3 bench/scripts/figure_tail_latency.py bench/results/my-run
python3 bench/scripts/figure_memory_gc.py bench/results/my-run --event-log build/dev/gc-events.jsonl
python3 bench/scripts/write_evidence_report.py bench/results/my-run --event-log build/dev/gc-events.jsonl
```

The tail-latency script reads `aggregate_summary.csv` and writes `tail_latency.svg`. The memory/GC script reads benchmark allocation/RSS columns and canonical `gc_end` lifecycle events when supplied or found under the result directory. `B7` result directories already contain those GC event logs. `B8` result directories keep the canonical async lifecycle logs. The evidence report records which figures exist and lists missing GC or long-run heap-live evidence explicitly.

Treat benchmark CSV files as summaries. Keep the canonical `events.jsonl` artefacts with any result set used for GC, heap-live, cancellation, timeout, or late-completion claims.

The analysis summary reports `A1`, `A2`, `B1`, `B2`, `B5`, `B6`, `B7`, and `B8` when those rows are present.
That same summary works for the optional `btcpp` result sets; absent groups are reported as absent rather than treated as failures.

Compare two benchmark result sets directly:

```bash
python3 bench/scripts/compare_results.py \
  bench/results/muesli-run \
  bench/results/btcpp-run
```

The comparison script checks the recorded environment metadata first and warns when the two runs were collected under different machine or build settings.

See the repo-root `bench/README.md` for the current catalogue and CLI overrides.

## Integration Checks

Recommended integration checks before merging:

1. compile and tick a small BT from Lisp
2. run at least one action that returns `running` before `success`
3. inspect `events.dump` and `bt.blackboard.dump`
4. run both clang and gcc builds (local or CI matrix)
