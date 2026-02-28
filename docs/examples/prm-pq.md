# Example: PRM (2D, Simulator-Free) With `pq.*`

This example implements a full probabilistic roadmap planner in pure muslisp:

- continuous 2D point robot
- circular obstacles
- straight-line local planner with sampled edge checks
- shortest-path query over roadmap via Dijkstra + `pq.*`

Path: `examples/repl_scripts/prm-2d-pq.lisp`

Run:

```bash
./build/dev/muslisp examples/repl_scripts/prm-2d-pq.lisp
```

## What It Demonstrates

- deterministic sampling with `(rng.make seed)`
- Lisp-defined `state-valid?` and `edge-valid?`
- naive roadmap construction with k-nearest candidate filtering
- Dijkstra search over roadmap using native priority queue builtins

## Expected Output Shape

The script prints:

- seed
- accepted node count vs target
- roadmap edge count and `k`
- `found` flag and expanded-node count
- if found: path length, path cost, and step-by-step coordinates

With the same seed and unchanged parameters, output remains deterministic.
