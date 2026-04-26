# Profiling And Performance

Runtime profiling remains available via:

- `(bt.stats inst)`
- `(bt.scheduler.stats)`
- `(bt.set-tick-budget-ms inst ms)`

Observability output (tick/node/blackboard/planner/vla/errors) is unified into the canonical event stream. Use `(events.dump [n])` for recent event inspection.

The opt-in `v0.7.0` per-tick audit payload is defined in [tick audit record](tick-audit.md).
Enable it with `(events.enable-tick-audit #t)` before calling `bt.tick`.

See [Canonical Event Log](event-log.md).

For the current benchmark-driven runtime tuning order, see [runtime performance](../internals/runtime-performance.md).
