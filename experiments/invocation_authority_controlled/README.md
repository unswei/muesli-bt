# controlled invocation-authority campaign

## status

Gate C0 is frozen as protocol `controlled-authority.c0.v1`. Its authoritative
fault interpretation is frozen as matrix
`controlled-authority.c0.fault-matrix.v1`.

The shared task, independent authority oracle, common effect recorder, all four
authority adapters, shared Lisp task runner, semantic schedule driver and
artefact writer are implemented. Paper-scale execution remains a deliberate
separate action because it runs every frozen paper seed.

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

Build the driver once:

```sh
cmake -S . -B build -DMUESLI_BT_BUILD_CONTROLLED_AUTHORITY_EXPERIMENT=ON
cmake --build build --target muesli_bt_controlled_authority_campaign -j
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

See [the documentation](../../docs/examples/invocation-authority-controlled-campaign.md)
for the public experiment contract.
