# Testing And Verification

## Test Layout

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

## Run Tests

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
python3 tools/validate_log.py --schema schemas/event_log/v1/mbt.evt.v1.schema.json tests/fixtures/mbt.evt.v1/*.jsonl
python3 tools/fixtures/verify_fixture.py
```

Verify docs snippet freshness against `examples/**` source files:

```bash
python3 scripts/check_docs_snippet_freshness.py
```

## Deterministic BT Tests

For deterministic BT tests:

- keep leaf callbacks small and explicit
- isolate one semantic rule per test
- avoid reliance on wall-clock timing where possible
- when async behaviour is needed, bound wait loops tightly

## benchmark suite

The optional benchmark harness lives under `bench/` and is built with the `bench-release` preset:

```bash
cmake --preset bench-release
cmake --build --preset bench-release -j
```

List or run scenarios:

```bash
./build/bench-release/bench/bench list
./build/bench-release/bench/bench run A1-single-leaf-off
./build/bench-release/bench/bench run-group B1
./build/bench-release/bench/bench run A2-alt-255-jitter-off
```

The first milestone writes:

- `run_summary.csv`
- `aggregate_summary.csv`
- `environment_metadata.csv`
- `jitter_trace.csv` for the `A2` scheduler jitter run

Summarise the latest benchmark result set:

```bash
python3 bench/scripts/analyse_results.py
```

See [`bench/README.md`](../bench/README.md) for the current catalogue and CLI overrides.

## Integration Checks

Recommended integration checks before merging:

1. compile and tick a small BT from Lisp
2. run at least one action that returns `running` before `success`
3. inspect `events.dump` and `bt.blackboard.dump`
4. run both clang and gcc builds (local or CI matrix)
