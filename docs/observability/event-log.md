# Canonical Event Log (`mbt.evt.v1`)

`muesli-bt` emits one canonical event stream. Each line is JSON (`.jsonl`) with envelope:

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

## Envelope fields

- `schema`: always `"mbt.evt.v1"`
- `contract_version`: runtime contract version (`"1.0.0"` in runtime-contract v1)
- `type`: event type
- `run_id`: run identifier
- `unix_ms`: UTC milliseconds
- `seq`: strictly increasing per run
- `tick`: optional tick index
- `data`: type-specific payload

## Event types

- `run_start`
- `run_end`
- `episode_begin`
- `episode_end`
- `bt_def`
- `tick_begin`
- `tick_end`
- `tick_audit` (opt-in `v0.7.0` audit-mode event)
- `gc_begin`, `gc_end`
- `node_enter`
- `node_exit`
- `node_status`
- `budget_warning`
- `deadline_exceeded`
- `bb_write`
- `bb_delete`
- `bb_snapshot`
- `sched_submit`, `sched_start`, `sched_finish`, `sched_cancel`
- `planner_call_start`, `planner_call_end`
- `planner_v1`
- `vla_submit`, `vla_poll`, `vla_cancel`, `vla_result`
- `async_cancel_requested`, `async_cancel_acknowledged`, `async_completion_dropped`
- `error`

## Lisp controls

- `(events.enable #t/#f)`
- `(events.enable-tick-audit #t/#f)`
- `(events.set-path "logs/run.jsonl")`
- `(events.set-flush-each-message #t/#f)`
- `(events.set-ring-size n)`
- `(events.dump [n])` -> list of JSON strings
- `(events.snapshot-bb [#t])` -> request snapshot at next tick boundary

## c++ integration hooks

- `bt::event_log::set_line_listener(...)`: consume canonical pre-serialised JSON lines for streaming transports.
- `bt::event_log::serialise_event_line(...)`: canonical serialiser for `mbt.evt.v1` envelopes.
- `bt::event_log::set_deterministic_time(...)`: fixed timestamp progression for deterministic fixture/test runs.
- `bt::runtime_host::enable_deterministic_test_mode(...)`: one-call deterministic mode (fixed planner seed + deterministic event timestamps).

## Notes

- Blackboard `bb_write.preview` is size-limited (4KB JSON).
- `seq` is the authoritative ordering key for replay/monitoring.
- Existing planner/vla metadata is wrapped in canonical events (for example `planner_v1`).
- `gc_begin` and `gc_end` are emitted when the Lisp heap collector runs through the default runtime host. Payloads use `schema_version: "gc.lifecycle.v1"`.
- The opt-in `tick_audit` event is defined in [tick audit record](tick-audit.md). The runtime emits it after `tick_end` when tick audit mode is enabled.
- File-backed event output is buffered by default. Enable `(events.set-flush-each-message #t)` when durability after each emitted event matters more than throughput.

## validation

Use two validators together:

- `tools/validate_log.py`: per-record JSON and schema validation
- `tools/validate_trace.py`: cross-event trace validation and replay comparison

Current trace-level checks include:

- `seq` ordering and duplicate detection
- completed tick delimitation with `tick_begin` / `tick_end`
- duplicate terminal `node_exit` detection
- over-budget tick evidence via `deadline_exceeded`
- async cancellation evidence after deadline overrun
- async lifecycle ordering checks such as cancel acknowledgement before request or terminal async events before submit

Examples:

```bash
python3 tools/validate_log.py fixtures/determinism-replay-case
python3 tools/validate_trace.py check fixtures/determinism-replay-case
python3 tools/validate_trace.py compare \
  fixtures/determinism-replay-case/events.jsonl \
  fixtures/determinism-replay-case/events.jsonl \
  --profile deterministic
```

Example normalisation configs live under:

- `tools/trace_validator/deterministic.toml`
- `tools/trace_validator/cross_backend.toml`

## determinism and bounded behaviour

Tooling consumers should separate three classes of guarantee:

- deterministic with fixed inputs:

  - event envelope shape
  - event ordering by `seq`
  - deterministic fixture runs created through deterministic mode
  - planner outcomes when the planner backend and seed are both deterministic

- bounded but not strictly deterministic:

  - `unix_ms`
  - wall-clock run duration
  - ROS observation timing when transport delivery jitter exists
  - best-effort tick budget and deadline behaviour

- environment-dependent:

  - live ROS transport arrival timing
  - host scheduling jitter
  - external simulator pacing
  - any callback that depends on non-deterministic external state

For replay and inspection, treat `seq` as the only strict ordering key.
Treat `tick` as the BT tick index within a run.
Treat `unix_ms` and observation `t_ms` as descriptive timing fields, not as the primary replay ordering mechanism.

## shared artefact path

Simulator-backed runs and ROS-backed runs should be exposed to tooling through the same canonical artefact shape:

- one artefact directory per run or scenario
- canonical event log at `events.jsonl`

Examples:

- `fixtures/determinism-replay-case/events.jsonl`
- `build/linux-ros2-l2/ros2_l2_artifacts/ros2_h1_success/events.jsonl`

This lets a consumer use the same validation and replay entry point for both classes of run:

```bash
python3 tools/validate_log.py fixtures/determinism-replay-case
python3 tools/validate_log.py build/linux-ros2-l2/ros2_l2_artifacts/ros2_h1_success
```

## interpreting replay failures

When replay or validation fails, classify the failure before assuming a runtime bug:

- schema failure:

  - the event line is malformed or does not satisfy `mbt.evt.v1`
  - usually indicates logger or serialisation drift

- ordering failure:

  - `seq` is missing, repeated, or out of order
  - indicates a broken event stream contract

- invariant failure:

  - expected event counts, status transitions, or policy fields do not match
  - usually indicates behavioural drift in the runtime or backend integration

- timing-only difference:

  - `unix_ms` or observation times differ while `seq`, event types, and decision payloads still match
  - usually indicates bounded-but-not-deterministic timing variance, not semantic drift

- transport-context difference:

  - ROS runs may differ in arrival timing or pacing while still remaining conformant
  - if the canonical decisions and event ordering remain valid, treat this as transport variance first

For ROS-backed runs, inspect `run_start.data.capabilities` before drawing conclusions:

- `time_source`
- `use_sim_time`
- `obs_timestamp_source`

Those fields explain whether timestamp differences came from ROS wall time, ROS simulation time, or message-header timestamps.

For long-running multi-episode experiments, prefer the summary anchors in the canonical stream:

- `episode_begin`
- `episode_end`
- `run_end`

These events let consumers compute per-episode and per-run summaries from `events.jsonl` without depending on a separate export format.
