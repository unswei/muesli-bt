# runtime contract in practice

This tutorial shows the runtime contract through a small runnable path: build, run a BT, emit a canonical log, validate the log, and inspect the event stream.

## prerequisites

- local `dev` build
- Python environment from `./scripts/setup-python-env.sh`

If you have not built the project yet, start with [first 10 minutes](../getting-started-10min.md).

## run a tiny BT

```bash
./build/dev/muslisp examples/bt/hello_bt.lisp
```

Expected output:

```text
<bt_def:1>
<bt_instance:1>
running
success
("...tick_begin..." "...node_exit..." "...tick_end...")
```

This script demonstrates the tick lifecycle in memory. It does not write a log file.

## enable canonical logging

Use the verified install script to write a real event log:

```bash
make verify-install
```

Expected output:

```text
verify-install passed: /path/to/muesli-bt/logs/verify-install.mbt.evt.v1.jsonl
```

Expected artefact:

- `logs/verify-install.mbt.evt.v1.jsonl`

The script writes the log by calling `events.enable`, `events.set-path`, and then ticking a small BT twice.

## inspect tick events

Show the first few event types:

```bash
python3 - <<'PY'
import json
from pathlib import Path

for line in Path("logs/verify-install.mbt.evt.v1.jsonl").read_text().splitlines()[:8]:
    event = json.loads(line)
    print(event["seq"], event["type"], event.get("tick", ""))
PY
```

You should see a stable sequence of canonical events such as:

```text
1 run_start
2 bt_def
3 tick_begin 1
4 node_enter 1
5 node_enter 1
6 node_exit 1
7 node_status 1
8 node_enter 1
```

Exact sequence numbers can change when extra instrumentation is enabled. The important shape is that ticks have begin/end boundaries and node events are ordered inside those boundaries.

## validate the log

```bash
.venv-py311/bin/python tools/validate_log.py \
  --schema schemas/event_log/v1/mbt.evt.v1.schema.json \
  logs/verify-install.mbt.evt.v1.jsonl
```

Expected output is no error output and exit status `0`.

## inspect a deliberate validation failure

Create a temporary malformed copy and validate it:

```bash
cp logs/verify-install.mbt.evt.v1.jsonl /tmp/muesli-bt-bad-log.jsonl
printf '%s\n' '{"schema":"wrong","type":"broken"}' >> /tmp/muesli-bt-bad-log.jsonl
.venv-py311/bin/python tools/validate_log.py \
  --schema schemas/event_log/v1/mbt.evt.v1.schema.json \
  /tmp/muesli-bt-bad-log.jsonl
```

You should see a schema validation error and a non-zero exit status. That failure is useful: malformed or non-canonical traces should not be mistaken for runtime evidence.

## compare a replay fixture

Trace-level validation checks cross-event structure:

```bash
.venv-py311/bin/python tools/validate_trace.py check fixtures/determinism-replay-case
```

Expected result:

```text
PASS: fixtures/determinism-replay-case/events.jsonl (events=13, completed_ticks=2, violations=0)
```

If a candidate trace diverges from a reference, `tools/validate_trace.py compare` reports the first divergent event and context where available.

## why this matters

The runtime contract makes debugging and evidence review concrete. You can show which tree was ticked, which node returned which status, whether timeouts or fallbacks occurred, and whether a trace still satisfies the canonical event contract.

## see also

- [runtime contract v1](../contracts/runtime-contract-v1.md)
- [canonical event log](../observability/event-log.md)
- [conformance levels](../contracts/conformance.md)
- [observability first](../getting-oriented/observability-first.md)
