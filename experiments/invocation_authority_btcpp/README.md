# BehaviorTree.CPP invocation-authority comparison

## status

External-comparison gate E1 is frozen as protocol
`controlled-authority.btcpp-comparison.e1.v1`. The protocol was frozen before
the BehaviorTree.CPP authority adapters were implemented.

The documented ordinary asynchronous adapter, full invocation-scoped port,
equivalent BehaviorTree.CPP task runner and deterministic adapter tests are now
implemented. The campaign driver and paper-scale artefact writer are the next
work package.

## purpose

This experiment tests whether ordinary asynchronous lifecycle behaviour and
the full invocation-scoped authority contract produce the same task effects in
muesli-bt and BehaviorTree.CPP. It also freezes a separate generic performance
lane. Performance results must not be presented as authority results.

The comparison reuses the C0 task, schedules, scripted proposal boundary, host
validation and independent oracle. Internal schedule identifiers remain
manifest keys only. Reader-facing tables use the schedule descriptions.

See [the public experiment contract](../../docs/examples/invocation-authority-btcpp-comparison.md).

## implementation

- `src/btcpp_task_runner.*` defines the equivalent reactive task and owns the
  `StatefulActionNode` lifecycle.
- `src/btcpp_variant.*` defines the ordinary asynchronous provider boundary and
  the explicit full-contract port.
- `tests/btcpp_task_runner_tests.cpp` covers positive work, stale contexts,
  deadlines, halt/re-entry, emergency pre-emption, supersession, stale
  dispatch, duplicate and racing completion, host validation and reset.
- `tests/check_controlled_authority_btcpp_events.py` validates the emitted
  canonical evidence.
