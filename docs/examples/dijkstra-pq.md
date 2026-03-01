# Example: Dijkstra Search With `pq.*`

This example implements weighted-grid Dijkstra search in muslisp using the native priority queue builtins.

Path: `examples/repl_scripts/dijkstra-grid-pq.lisp`

Run:

```bash
./build/dev/muslisp examples/repl_scripts/dijkstra-grid-pq.lisp
```

Step-by-step tutorial:

- [Dijkstra Tutorial (Step By Step)](tutorials/dijkstra-step-by-step.md)

## What It Demonstrates

- canonical shortest-path pattern with duplicate queue pushes
- stale-distance skipping on pop (no decrease-key primitive required)
- `pq.make`, `pq.push!`, `pq.pop!`, `pq.empty?`
- path reconstruction with a `prev` map

## Notes

- The A* example remains intentionally non-PQ: [A* Search](a-star-search.md).
- Dijkstra here is the reference example that uses `pq.*` directly.

## Source

```lisp
--8<-- "examples/repl_scripts/dijkstra-grid-pq.lisp"
```

## Expected Output Shape

The script prints:

- grid + start/goal summary
- `found` flag
- expanded node count
- total path cost
- path length
- each path step as `(x y)`
