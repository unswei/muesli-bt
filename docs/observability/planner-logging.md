# Planner Logging Schema

Planner calls emit structured records to:

- in-memory planner ring (via `planner.logs.dump`)
- JSON lines file sink (`planner-records.jsonl` by default)

Use:

- `(planner.set-log-path "path/to/planner.jsonl")`
- `(planner.set-log-enabled #t)` or `(planner.set-log-enabled #f)`
- `(planner.logs.dump [n])`

## Record Fields

Each record contains:

- `schema_version` (currently `planner.v1`)
- `ts_ms`
- `run_id`
- `tick_index`
- `node_name`
- `budget_ms`
- `time_used_ms`
- `iters`
- `root_visits`
- `root_children`
- `widen_added`
- `action`
- `confidence`
- `value_est`
- `status`
- `depth_max`
- `depth_mean`
- `seed`
- optional `state_key`
- optional `top_k` list of `{action, visits, q}`

## Example JSON Line

```json
{"schema_version":"planner.v1","ts_ms":4955202,"run_id":"inst-1","tick_index":1,"node_name":"toy-plan","budget_ms":5,"time_used_ms":3.258,"iters":200,"root_visits":200,"root_children":29,"widen_added":200,"action":[0.973466],"confidence":0.18,"value_est":-3.2084,"status":"ok","depth_max":25,"depth_mean":18.99,"seed":42,"state_key":"state"}
```

## Quick Plotting Example

```python
import json
from pathlib import Path
import matplotlib.pyplot as plt

rows = [json.loads(line) for line in Path("planner-records.jsonl").read_text().splitlines() if line.strip()]
rows = [r for r in rows if r.get("node_name") == "toy-plan"]

ticks = [r["tick_index"] for r in rows]
times = [r["time_used_ms"] for r in rows]
iters = [r["iters"] for r in rows]

fig, (ax1, ax2) = plt.subplots(2, 1, sharex=True)
ax1.plot(ticks, times)
ax1.set_ylabel("time_used_ms")
ax2.plot(ticks, iters)
ax2.set_ylabel("iters")
ax2.set_xlabel("tick_index")
plt.show()
```

## See Also

- [Logging](logging.md)
- [PlanAction Node Reference](../bt/plan-action-node.md)
- [VLA Logging Schema](vla-logging.md)
