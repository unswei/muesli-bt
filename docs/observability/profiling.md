# Profiling And Performance

Runtime profiling remains available via:

- `(bt.stats inst)`
- `(bt.scheduler.stats)`
- `(bt.set-tick-budget-ms inst ms)`

Observability output (tick/node/blackboard/planner/vla/errors) is unified into the canonical event stream. Use `(events.dump [n])` for recent event inspection.

See [Canonical Event Log](event-log.md).
