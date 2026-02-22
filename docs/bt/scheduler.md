# Scheduler And Execution Model

This page explains ticking and async execution assumptions.

## Who Calls `bt.tick`

`bt.tick` is called by host code:

- REPL/manual tests
- script loops
- application supervision loops
- robot control/supervisory threads

The runtime defines per-tick semantics. The host controls frequency.

## Tick Frequency Guidance

Choose a fixed cadence that matches your application budget.

Typical approach:

- run BT tick at steady rate
- keep leaf poll work cheap
- move heavy work off tick thread when possible

## Synchronous vs Asynchronous Leaves

- synchronous leaves: quick checks/writes, return immediately
- asynchronous pattern: submit work, return `running`, poll later

The project includes `async-sleep-ms` as a scheduler-backed action example.

## Time Handling

Tick context includes:

- `tick_index`
- `now`

`services` can carry a typed `clock_interface`, so host code can provide custom timing sources.

## Reset Behaviour

`bt.reset` clears node memory and blackboard.

Any action that persists progress in node memory should tolerate reset cleanly.

## Cancellation / Halt

Explicit `halt` semantics are not implemented in v1.

Current policy:

- reset and status transitions drive interruption indirectly
- scheduler cancel is best-effort where used

## Blocking Caveat

Avoid blocking operations inside `act` callbacks on the tick thread.

If blocking is unavoidable, isolate it behind scheduler/external async APIs and return `running` while pending.
