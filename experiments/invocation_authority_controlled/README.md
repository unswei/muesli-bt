# controlled invocation-authority campaign

## status

Gate C0 is frozen as protocol `controlled-authority.c0.v1`.

The shared task, independent authority oracle, common effect recorder, all four
authority adapters and the shared Lisp task runner are implemented. The
schedule driver, artefact writer and paper-scale campaign execution are later
gates.

## purpose

This experiment compares blocking, ordinary asynchronous, timeout-only and
invocation-scoped execution under the same task and event schedules.

Identifiers such as `F03` are internal protocol keys. Reports and paper text
must use the corresponding `reader_label`, such as "branch exit and re-entry
before completion". The schedule numbers are not reader-facing terminology.

## layout

- `configs/protocol.v1.json` freezes variants, units, metrics, seed sets and
  acceptance gates.
- `schedules/catalogue.v1.json` freezes the schedule meanings and canonical
  event orderings.
- `lisp/common_task.lisp` is the task shared by every variant.
- `src/common_task.*` provides common state, request capture, the independent
  obsolescence oracle and a stepped coordinator.
- `src/effect_recorder.*` scores commits and dispatches through that oracle.
- `src/variant.*` defines the provider/variant boundary and the B0 blocking, B1
  ordinary-asynchronous and B2 timeout-only adapters.
- `src/runtime_variant.*` bridges B3 to the production invocation-scoped VLA
  and walking-target gates.
- `src/task_runner.*` binds every adapter to the same Lisp task actions.
- `tests/common_task_tests.cpp` checks the task and oracle foundation.
- `tests/variant_tests.cpp` checks B0-B2 lifecycle, stale-result, timeout and
  shared-validation behaviour.
- `tests/task_runner_tests.cpp` checks common task wiring and B3 production
  admission, dispatch and pre-emption paths.

Runtime execution evidence will use only the canonical `mbt.evt.v1` stream.
Campaign JSON and CSV files will be derived analysis artefacts, not alternate
runtime logs. The runner keeps its common-task and B3 production streams
separate so each stream retains a valid envelope and sequence.

## c0 invariants

- Every schedule has an internal ID and a plain-language reader label.
- All variants receive the same observation, request, proposal and event order.
- The oracle, rather than the tested variant, decides whether an effect is
  obsolete.
- A deadline is valid at the exact deadline and expired only after it.
- Paper seeds are disjoint from engineering seeds.
- A paper campaign cannot pass if the full variant produces an obsolete effect,
  rejects a current valid result, emits an invalid trace or disagrees with
  replay.

See [the documentation](../../docs/examples/invocation-authority-controlled-campaign.md)
for the public experiment contract.
