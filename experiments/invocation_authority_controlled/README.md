# controlled invocation-authority campaign

## status

Gate C0 is frozen as protocol `controlled-authority.c0.v1`. Its authoritative
fault interpretation is frozen as matrix
`controlled-authority.c0.fault-matrix.v1`.

The shared task, independent authority oracle, common effect recorder, all four
authority adapters, shared Lisp task runner, deterministic semantic lane and
real-clock timing lane are implemented. Paper-scale execution remains a
deliberate action because it runs every frozen semantic seed and timing
repetition.

## purpose

This experiment compares blocking, ordinary asynchronous, timeout-only and
invocation-scoped execution under the same task and event schedules.

Identifiers such as `F03` are internal protocol keys. Reports and paper text
must use the corresponding `reader_label`, such as "branch exit and re-entry
before completion". The schedule numbers are not reader-facing terminology.

## layout

- `configs/protocol.v1.json` freezes variants, units, metrics, seed sets and
  acceptance gates.
- `configs/fault-matrix.v1.json` is the single source for fault classes,
  authority dimensions, claims, primary metrics, unsafe effects, expected
  B0--B3 outcomes and negative-control witnesses.
- `schedules/catalogue.v1.json` freezes reader labels and canonical event
  orderings. It does not repeat fault interpretations or expected outcomes.
- `lisp/common_task.lisp` is the task shared by every variant.
- `src/common_task.*` provides common state, request capture, the independent
  obsolescence oracle and a stepped coordinator.
- `src/effect_recorder.*` scores commits and dispatches through that oracle.
- `src/variant.*` defines the provider/variant boundary and the B0 blocking, B1
  ordinary-asynchronous and B2 timeout-only adapters.
- `src/runtime_variant.*` bridges B3 to the production invocation-scoped VLA
  and walking-target gates.
- `src/task_runner.*` binds every adapter to the same Lisp task actions.
- `src/campaign_plan.*` reads the resolved, versioned schedule plan.
- `src/scripted_provider.*` supplies deterministic completion barriers.
- `src/campaign_engine.*`, `src/campaign_outcome.*` and
  `src/campaign_writer.*` execute the plan, evaluate its frozen outcomes and
  serialise raw evidence without combining those responsibilities.
- `run_campaign.py` resolves the frozen inputs, invokes the engine, validates
  evidence and writes manifests, summaries and paper tables.
- `src/timing_plan.*` and `src/timing_engine.*` execute the separate real-clock
  lane across independent task instances.
- `run_timing_campaign.py` constructs the frozen one-factor-at-a-time timing
  design, fingerprints the host and writes timing-only summaries and tables.
- `tests/common_task_tests.cpp` checks the task and oracle foundation.
- `tests/variant_tests.cpp` checks B0-B2 lifecycle, stale-result, timeout and
  shared-validation behaviour.
- `tests/task_runner_tests.cpp` checks common task wiring and B3 production
  admission, dispatch and pre-emption paths.

Runtime execution evidence uses only the canonical `mbt.evt.v1` stream.
Campaign JSON and CSV files are derived analysis artefacts, not alternate
runtime logs. The runner keeps its common-task and B3 production streams
separate so each stream retains a valid envelope and sequence.

## run

Build both lane drivers once:

```sh
cmake -S . -B build -DMUESLI_BT_BUILD_CONTROLLED_AUTHORITY_EXPERIMENT=ON
cmake --build build --target \
  muesli_bt_controlled_authority_campaign \
  muesli_bt_controlled_authority_timing -j
```

Run the 32-seed engineering campaign into a new directory:

```sh
python3 experiments/invocation_authority_controlled/run_campaign.py \
  --output /path/to/authority-engineering \
  --seed-set engineering
```

Use `--seed-set paper` for the frozen 128-seed paper campaign. A fast complete
catalogue check can use `--seeds 0`. The driver refuses to write into a
non-empty directory, so an earlier campaign cannot be silently mixed with a
new one.

The campaign root contains `campaign-manifest.json`, `raw_trials.jsonl`, one
run manifest per experimental unit, canonical streams under `events/`,
machine-readable summaries under `summary/`, and reader-facing CSV and
Markdown tables under `paper/`. The campaign and run manifests identify the
matrix, and the campaign manifest records its SHA-256 digest.

Run the complete timing lane into a different directory:

```sh
python3 experiments/invocation_authority_controlled/run_timing_campaign.py \
  --output /path/to/authority-timing
```

On Linux, `--cpu-set 0-7` pins the engine and its provider workers to a declared
CPU set. The timing manifest captures the actual host, affinity, governor and
load before and after execution, including the busiest background processes.
The paper gate requires the one-minute load average to remain at or below 0.5
per logical CPU. The lane contains 2,480 recorded trials and 300 warm-ups. A
partial timing run can select conditions, override repetitions or override
warm-ups, but cannot pass the paper timing gate.

## c0 invariants

- Every schedule has an internal ID and a plain-language reader label.
- Every schedule maps to exactly one frozen fault-matrix row in catalogue
  order.
- Fault expectations, full-variant requirements and negative-control witnesses
  occur only in the fault matrix.
- All variants receive the same observation, request, proposal and event order.
- The oracle, rather than the tested variant, decides whether an effect is
  obsolete.
- A deadline is valid at the exact deadline and expired only after it.
- Paper seeds are disjoint from engineering seeds.
- A paper campaign cannot pass if the full variant produces an obsolete effect,
  rejects a current valid result, emits an invalid trace or disagrees with
  replay.
- A partial or engineering campaign produces tables but does not evaluate the
  frozen paper gate.
- Replay depends on all preceding schedules. Selecting the replay schedule
  alone is therefore an error.
- The timing lane uses 15 conditions: one primary point and 14
  one-factor-at-a-time variations. It never pools measurements with the
  deterministic semantic lane.
- Concurrent timing jobs are independent common-task instances. A scheduler
  cycle ticks every instance once, and `maximum_tick_ms` is the longest complete
  scheduler cycle in a trial.
- The primary timing statistic is the nearest-rank p99 across 200 per-trial
  maxima. Secondary conditions use 30 paired repetitions.

See [the documentation](../../docs/examples/invocation-authority-controlled-campaign.md)
for the public experiment contract.
