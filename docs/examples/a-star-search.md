# Example: A* Search In muslisp

This example implements a full A* search in muslisp over a small 2D occupancy grid.

Path: `examples/repl_scripts/a-star-grid.lisp`

Run:

```bash
./build/dev/muslisp examples/repl_scripts/a-star-grid.lisp
```

## What It Demonstrates

- graph search in pure Lisp without host callbacks
- a deliberate non-PQ baseline open-set implementation (vector + best-node scan)
- mutable container usage:
  - `vec.make`, `vec.push!`, `vec.get`, `vec.set!`, `vec.pop!`
  - `map.make`, `map.get`, `map.set!`, `map.has?`, `map.del!`
- recursive control flow for:
  - open-set best-node selection
  - neighbor expansion
  - path reconstruction
- Manhattan heuristic over a grid

## Core Ideas In The Script

- Node encoding: integer id `id = x + y * width`.
- Open set: vector of node ids plus a membership map.
- Scores:
  - `g`: best known cost-from-start
  - `f = g + h`: A* priority score
- Parent links (`came`) allow goal-to-start backtracking and path reconstruction.

## Expected Output Shape

The script prints:

- grid + start/goal summary
- whether a path was found
- expanded node count
- path length
- each path step as `(x y)`

The exact expansion count may vary if you modify obstacle layout.
