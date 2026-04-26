# runtime contract v1

## what this is

This page defines the `muesli-bt` runtime contract for:

- tick semantics
- budget and deadline semantics
- async node lifecycle
- deterministic event log semantics

Contract identifier:

- contract name: `runtime-contract`
- contract version: `1.0.0`
- contract id: `runtime-contract-v1.0.0`
- event schema: `mbt.evt.v1`

This contract is versioned and testable. Logs that omit contract version metadata are non-conformant.

## when to use it

Use this contract when you:

- embed `muesli-bt` into a host loop
- consume `mbt.evt.v1` logs in tooling
- add or review runtime behaviours that affect tick timing, async jobs, or replay guarantees
- update conformance fixtures or CI gates

## how it works

### host model

Host responsibilities:

- own the authoritative time source integration
- ingest sensor data and write blackboard inputs
- execute actuator commands
- enforce safety fallback independently of BT runtime outcomes
- choose timeout policy at system level

Runtime responsibilities:

- evaluate BT nodes and tree status per tick
- supervise async jobs through scheduler/VLA services
- enforce best-effort tick budget at decision points
- emit canonical `mbt.evt.v1` events

Code anchors:

- tick entry and loop: `src/bt/runtime.cpp` (`bt::tick`)
- host services wiring: `src/bt/runtime_host.cpp` (`runtime_host::tick_instance`)
- event envelope serialisation: `src/bt/event_log.cpp` (`event_log::serialise_event_line`)

### tick semantics

Tick entry conditions:

- instance has a compiled definition
- host has provided required callbacks/services for referenced nodes

A single tick is:

1. increment `instance.tick_index`
2. emit `tick_begin`
3. evaluate root subtree
4. emit `tick_end` with root status and timing

Allowed work inside a tick:

- deterministic tree traversal and callback invocation
- bounded planner calls and async decision-point checks
- blackboard reads/writes and event emission

Disallowed by contract:

- unbounded blocking inside runtime-managed decision points
- direct actuator side effects outside host callbacks/actions

Single source of truth:

- tick index: `instance.tick_index`
- tick timing origin: `tick_context.tick_started_at`

Code anchors:

- tick scope and end emission: `src/bt/runtime.cpp` (`tick_scope`)
- node enter/exit events: `src/bt/runtime.cpp` (`node_scope`)

### budgets and deadlines

Definitions:

- `tick_budget_ms`: per-tick budget configured via `bt::set_tick_budget_ms(...)`
- `tick_deadline`: absolute monotonic deadline derived from `tick_started_at + tick_budget_ms`

Contract behaviour:

- enforcement is best-effort
- decision-point checks occur before:

    - planner calls (`planner_call_start`)
    - async start (`vla_submit`)
    - async poll (`vla_poll`)

- if remaining budget is below threshold, runtime emits `budget_warning` and blocks that decision-point action

Overrun behaviour:

- runtime may overrun due already-started work
- on overrun runtime emits:

    - `budget_warning`
    - `deadline_exceeded`

- runtime requests async cancellation for active tracked async jobs
- host must still enforce actuator safety independently

Code anchors:

- decision-point budget guard: `src/bt/runtime.cpp` (`budget_allows_decision_point`)
- overrun handling and deadline events: `src/bt/runtime.cpp` (`tick_scope::~tick_scope`)

### async node lifecycle

Contract lifecycle states:

- `idle`
- `running`
- `succeeded`
- `failed`
- `cancel_requested`
- `cancelled`
- `deadline_exceeded`

Required transitions:

- `start`: creates a job with stable `job_id`
- `poll`: returns running/terminal status and optional progress
- `cancel`: idempotent; repeated cancel calls are accepted
- `deadline`: expiry triggers cancellation request path

Safety rules:

- one active job per async node instance unless explicitly extended
- late completion after cancellation does not commit actions
- late completion drop is observable

Canonical async events:

- `vla_submit`, `vla_poll`, `vla_cancel`, `vla_result`
- `async_cancel_requested`
- `async_cancel_acknowledged`
- `async_completion_dropped`

Code anchors:

- VLA request/wait/cancel nodes: `src/bt/runtime.cpp` (`execute_vla_request`, `execute_vla_wait`, `execute_vla_cancel`)
- VLA service lifecycle and cancel idempotence: `src/bt/vla.cpp` (`vla_service::submit`, `vla_service::poll`, `vla_service::cancel`)
- active async tracking: `include/bt/instance.hpp` (`instance::active_vla_jobs`)

### deterministic logging and replay

Envelope guarantees:

- canonical schema: `mbt.evt.v1`
- contract version attached to every event line (`contract_version`)
- per-run strict event ordering via `seq`

Minimum required runtime events:

- run lifecycle: `run_start`, `run_end`
- multi-episode loop lifecycle when applicable: `episode_begin`, `episode_end`
- tick: `tick_begin`, `tick_end`
- node: `node_enter`, `node_exit` (`node_status` retained for compatibility)
- async: submit/poll/cancel/result plus cancel/drop events
- planner: `planner_call_start`, `planner_call_end`, `planner_v1`
- timing: `budget_warning`, `deadline_exceeded`
- heap lifecycle: `gc_begin`, `gc_end` when the Lisp heap collector runs through the default runtime host

GC policy:

- `default`: automatic collection uses the normal runtime policy
- `between-ticks`: automatic collection requested during a BT tick is deferred until the tick exits
- `manual`: automatic `maybe_collect` checks do not collect; explicit collection can still run
- `fail-on-tick-gc`: collection during a BT tick is a contract violation and raises an error

Determinism scope:

- with deterministic mode enabled and identical inputs:

    - node status trace matches
    - event ordering and deterministic timestamps match
    - planner outcomes match when backend/seed are deterministic

Bounded-but-not-strictly-deterministic scope:

- `unix_ms`
- live ROS observation timing
- wall-clock duration of long experiments
- best-effort budget and overrun timing

Code anchors:

- deterministic event timestamps: `src/bt/event_log.cpp` (`set_deterministic_time`)
- deterministic host mode: `src/bt/runtime_host.cpp` (`enable_deterministic_test_mode`)

## api / syntax

Public contract headers:

- `include/muesli_bt/contract/version.hpp`
- `include/muesli_bt/contract/time.hpp`
- `include/muesli_bt/contract/ids.hpp`
- `include/muesli_bt/contract/outcome.hpp`
- `include/muesli_bt/contract/events.hpp`

Schema and validator:

- `schemas/event_log/v1/mbt.evt.v1.schema.json`
- `tools/validate_log.py`

## example

Minimal envelope example:

```json
{
  "schema": "mbt.evt.v1",
  "contract_version": "1.0.0",
  "type": "tick_begin",
  "run_id": "run-1735689600000",
  "unix_ms": 1735689600123,
  "seq": 42,
  "tick": 7,
  "data": {}
}
```

## gotchas

- `tick_budget_ms` is best-effort, not hard real-time preemption.
- Host safety policy is outside the runtime and remains mandatory.
- `node_status` exists for backward compatibility; use `node_enter`/`node_exit` for strict pairing.
- Async cancellation completion may arrive later; dropped completions are expected and logged.

## see also

- [contracts index](README.md)
- [compatibility policy](compatibility.md)
- [conformance levels](conformance.md)
- [muesli-studio integration contract](muesli-studio-integration.md)
