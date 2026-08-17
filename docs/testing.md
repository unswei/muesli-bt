# testing and verification

## test layout

Core test binaries:

- `tests/test_main.cpp` (`muslisp_tests`)
- `tests/conformance/test_conformance_main.cpp` (`muesli_bt_conformance_tests`, L0)
- `tests/test_humanoid_vla_scenarios.cpp`
  (`muesli_bt_humanoid_vla_scenario_tests`)
- `examples/air_hockey_model_mediated_defence/tests/scenario_tests.cpp`
  (`muesli_bt_air_hockey_scenario_tests`)

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

## deterministic humanoid VLA scenarios

The humanoid scenario binary uses a gate-controlled VLA backend and fixed
canonical event timestamps. A gate decides exactly when each backend invocation
starts and completes. Wall-clock sleeps only bound a failed test; they do not
decide the scenario ordering.

CTest exposes one test for each required authority transition:

- `normal_acceptance`: accept and dispatch the current generation;
- `moved_ball`: reject context A after context B becomes current;
- `supersession`: revoke generation one and accept generation two;
- `late_completion`: drop successful output that arrives after cancellation;
- `duplicate_completion`: reject a second terminal poll without another write;
- `branch_halt`: revoke authority and clear invocation-owned keys;
- `re_entry`: create a fresh generation and capture the new context;
- `emergency_interruption`: switch to the safety branch, then reject the old
  target without calling the walking controller.

Run the whole matrix or one named scenario:

```bash
./build/dev/muesli_bt_humanoid_vla_scenario_tests
./build/dev/muesli_bt_humanoid_vla_scenario_tests moved_ball
```

## humanoid video experiment smoke test

The executable video experiment has a shortened CTest matrix. It builds the
deadline-only and invocation-scoped BTs, runs the delayed fake service, applies
the configured moved-ball and emergency interventions, and checks the generated
canonical evidence and run manifests. It also rejects a drifted action-space
contract and evaluates the structured overlay and evidence predicates.

```bash
ctest --test-dir build/dev --output-on-failure \
  -R muesli_bt_humanoid_video_experiment
```

The smoke test scales the 2.5-second delay and writes to a temporary directory.
It also exercises the guarded run-directory replacement policy. JSON Schema
validation is fail-closed and requires the Python `jsonschema` package. The
smoke test is not paper evidence. Run the example without `--check` for the
frozen real-time protocol.

The Booster Studio host policy has a separate SDK-independent test. It covers
ball context movement and reacquisition, observation age, synchronous dispatch,
exactly-once admission, emergency rejection, velocity bounds, target
revocation, the Unix-socket boundary, native-payload tamper checks, process
supervision, public manifest schemas and canonical-event overlay generation:

```bash
ctest --test-dir build/dev --output-on-failure \
  -R muesli_bt_booster_studio_adapter
```

The pinned Linux payload has a source-only check that does not require Docker:

```bash
python3 examples/humanoid_model_mediated_approach/booster_studio/tools/build_native_payload.py \
  --source-check
```

A release payload build refuses a dirty relevant source tree. The generated
runner must be ELF64 x86-64, and verification recomputes the digest and size of
the executable and every frozen experiment asset.

CMake registers this CTest only when its selected Python interpreter can import
`jsonschema`. The Linux GCC and Clang CI jobs install that dependency and run
the matrix. The standalone runner always fails closed when validation cannot
run.

## deterministic air-hockey authority scenarios

The air-hockey WP2 harness starts a fresh pure fake host for every row and runs
the C++ socket `env_backend` with a gate-controlled provider. H1, H2a/H2b and
H3--H8 have fixed intervention order and named evidence predicates. H2a alone
demonstrates the bounded deadline-only defect; all invocation-scoped rows have
zero accepted obsolete dispatches. H6 runs under both policies, H7 exercises
duplicate terminal polling and dispatch, and H8 reuses a recorded response
without live inference.

```bash
ctest --test-dir build/dev --output-on-failure \
  -R '^muesli_bt_air_hockey_(h1|h2a|h2b|h3|h4|h5|h6|h7|h8|evidence)$'
```

These tests require `jsonschema` for the Python host boundary. They do not
import MuJoCo, load a checkpoint, use a GPU or contact Marvin.

## air-hockey evidence-bundle analysis

WP3 has a standalone local gate because its Python analysis does not require a
native build. The command creates eight temporary synthetic bundles, exercises
guarded replacement and regenerates all derived table, plot, replay and overlay
fields from raw artefacts:

```bash
uv run --with 'jsonschema>=4.20,<5' \
  python examples/air_hockey_model_mediated_defence/run_wp3.py check
```

The focused unit suite also covers exact recorded-provider lookup, privileged
field rejection and exact binomial interval endpoints:

```bash
uv run --with 'jsonschema>=4.20,<5' \
  python -m unittest tests/test_air_hockey_wp3.py -v
```

Neither command contacts Marvin or imports the ACRA simulator.

## air-hockey integration packaging

WP4 validates the digest-bound container definition, provider schemas, fixed
provider lifecycle and the real committed ACRA export loader. It obtains the
ACRA source using `git archive` at the pinned revision, so unrelated changes in
the local ACRA working tree cannot enter the check:

```bash
uv run \
  --with 'numpy>=1.26,<2' \
  --with 'gymnasium>=1.0,<2' \
  --with 'pyyaml>=6,<7' \
  --with 'jsonschema>=4.20,<5' \
  python examples/air_hockey_model_mediated_defence/run_wp4.py check
```

The focused pure unit suite requires only `jsonschema`:

```bash
uv run --with 'jsonschema>=4.20,<5' \
  python -m unittest tests/test_air_hockey_wp4.py -v
```

These commands do not resolve the Marvin-local base image, start Docker,
import MuJoCo or select a final learned checkpoint.

The remote WP5 engineering gate is deliberately separate from the local suite.
After ACRA's unchanged pinned integration tests pass, run `run_wp5.py` inside
the commit-tagged joint image. The gate requires 2 CPUs and 8 GB RAM, does not
require GPU access, and refuses to replace a non-empty output directory. It
uses only the default engineering shot; the `muesli_test` paper split remains
closed until the later protocol-freeze gate.

WP6 adds a pure protocol check to the local suite and a separate Marvin
campaign. `run_wp6.py check-protocol` validates the engineering distribution
and manifest hashes, calibrated delay ordering, H1--H8 matrix, learned-provider
identity, source-protocol action lock, timing thresholds and the closed paper
split. The focused host suite exercises its percentile, timing-record and event
stream projections without MuJoCo.

The remote `run_wp6.py run` command requires an empty output directory, the
hash-matched checkpoint mounted read-only, and the commit-tagged joint image.
It runs 26 engineering shots across all nine H1--H8 rows, then runs the frozen
learned provider on the same shots. Gate G6 fails closed on integrity,
current-result progress, obsolete dispatches, context rejection, replay,
fallback, BT tick timing, learned inference timing or save rate. The passing
campaign used image `local/muesli-air-hockey:8555ffb-1b6bbbb`, 2 CPUs, 8 GB RAM
and no GPU; it recorded 234 passing deterministic runs and 26/26 learned saves.
The paper split remained unopened.

WP7 adds the final split-safe `run_wp7.py check-protocol` check, native and
MuJoCo engineering preflights, and the separately authorised paper campaign.
The passing archive-built image
`local/muesli-air-hockey:38c8a19-1b6bbbb` ran 228 matched pairs and produced
456 validated bundles, including exact replay. All 228 deadline-only pairs
dispatched the authored obsolete proposal; none of the invocation-scoped pairs
did. Missing terminal invocations, reason-code failures, replay mismatches,
trace failures and direct-replay failures were all zero. Operational tick p99
was 9.145 ms, although 9/1,824 samples exceeded the 20 ms budget and the
maximum was 51.976 ms.

`run_wp7.py seal` is a separate, fail-closed step. It writes a complete SHA-256
manifest, verifies a compressed backup and then changes campaign files to
mode `0444` and directories to `0555`. The passing campaign's checksum-manifest
SHA-256 is
`80af405a8c05cf035525af27c27b532b29f82d0e33cd90e4cfe6b6262f0031f1`;
its verified backup SHA-256 is
`99f70a25e6736a24ddf3f1422d70336d59054919be12a3b60c5d2c7a7c3f903b`.
Because the full condition's current fallback only holds position, its lower
save rate is a documented fallback limitation, not a Gate G7 integrity failure.

WP8 adds the current-context recovery arm. Its Marvin campaign requires a
Release joint image and sets `OMP_NUM_THREADS`, `OPENBLAS_NUM_THREADS`,
`MKL_NUM_THREADS` and `NUMEXPR_NUM_THREADS` to `1`. The runner checks these
limits before creating evidence. This prevents numerical worker pools from
introducing periodic scheduler stalls under the two-CPU container quota.
The passing campaign revalidated 456 preserved baseline bundles and captured
228 fresh recovery bundles. Recovery had zero obsolete dispatches and zero
duplicate terminal decisions, saved 150/228 shots, and recorded 5.879 ms BT
tick p99. Learned inference p95 was 0.249 ms.

`run_wp8.py seal` is a separate, fail-closed publication step. It verifies the
WP8 marker, passing report, frozen protocol hash and campaign-summary hash;
writes a complete SHA-256 manifest; creates and verifies a compressed backup;
and makes the campaign read-only. The external seal report is authoritative
for backup verification and file modes because `wp8-report.json` necessarily
records the pre-seal state.

The passing Marvin campaign is sealed. Its 12,318-entry checksum manifest has
SHA-256
`149c0b71906c6675217e2a97e50b3d54059a68d4bc812ad6364c3179b1a0aa73`;
all entries revalidated after sealing. The verified compressed backup has
SHA-256
`3a81bc515f74b39c9ba819ae51e1448d897a40f33b510bb7ed29667d1c7486cb`.
All campaign files are mode `0444` and directories are mode `0555`.

WP9 freezes the context-token sensitivity protocol before its campaign. It
uses only the public puck target and visibility flag, fixes the downstream
usefulness tolerance at 0.10 normalised units, and compares reacquisition
identity with 0.10 and 0.20 displacement equivalence thresholds. The selected
24-shot subset is determined by SHA-256 shot-identifier order, independent of
outcome, and crossed with the three existing service delays.

## canonical event fixture suite

Canonical fixtures are stored under `tests/fixtures/mbt.evt.v1/` and validated in CI:

- `minimal_run.jsonl`
- `planner_run.jsonl`
- `scheduler_run.jsonl`
- `scheduler_cancel_run.jsonl` (async cancellation edge case)
- `vla_run.jsonl`
- `vla_cancel_run.jsonl` (async cancellation edge case)
- `vla_late_completion_drop_run.jsonl` (late completion dropped after cancellation)
- `vla_authority_revoked_run.jsonl` (invocation-scoped pre-emption)
- `humanoid_vla_evidence_run.jsonl` (accept, dispatch, moved-context reject and pre-emption)
- `air_hockey_h2b_context_change.jsonl` (public context change followed by invocation-scoped rejection)
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
./build/dev/muesli_bt_humanoid_vla_scenario_tests
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

Verify BT node option schema coverage and user-facing prose hygiene:

```bash
python3 scripts/check-bt-node-option-docs.py
python3 scripts/check-docs-user-prose.py
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

Add `--comparison-only` with `--with-btcpp` to run only the shared `A1`, `A2`,
`B1`, `B2` and supported `B5` comparison surface. The resulting bundle contains
the generated `cross-runtime/comparison.md` report and a hash manifest for its
source summaries. Runtime-specific `B6`--`B9` evidence remains outside these
ratios.

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
- `B9` generated-subtree contract evidence smoke runs

For `BehaviorTree.CPP`, the harness currently covers:

- `A1` single-leaf baseline
- `A2` scheduler jitter
- `B1` static tick overhead
- `B2` reactive interruption
- `B5` `compile`, `inst1`, `inst100`, and `loaddsl`

`B6`, `B7`, `B8`, `B9`, `B5 parse`, and `B5 loadbin` are intentionally omitted from the cross-runtime run because they are not a fair shared subset.

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

Run the generated-subtree contract evidence benchmark group:

```bash
./build/bench-release/bench/bench run-group B9
```

`B9` is `muesli-bt` only. It covers accepted generated recovery fragments at small, medium, and large sizes, rejected policy proposals, install plus rollback, replay parity, and first-divergence detection. Each repetition writes canonical `mbt.evt.v1` lifecycle events and a `generated_subtree_report.json` sidecar with phase timings, hashes, validation status, install or rejection status, rollback handles, replay parity, divergence status, allocation counts, and `host_reached`.

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

The tail-latency script reads `aggregate_summary.csv` and writes `tail_latency.svg`. The memory/GC script reads benchmark allocation/RSS columns and canonical `gc_end` lifecycle events when supplied or found under the result directory. `B7` result directories already contain those GC event logs. `B8` result directories keep the canonical async lifecycle logs. `B9` result directories keep generated-subtree lifecycle logs and JSON sidecars. The evidence report records which figures exist and lists missing GC, long-run heap-live, or generated-subtree sidecar evidence explicitly.

Treat benchmark CSV files as summaries. Keep the canonical `events.jsonl` artefacts with any result set used for GC, heap-live, cancellation, timeout, late-completion, generated-subtree install, generated-subtree rejection, rollback, replay, or divergence claims.

The analysis summary reports `A1`, `A2`, `B1`, `B2`, `B5`, `B6`, `B7`, `B8`, and `B9` when those rows are present.
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
