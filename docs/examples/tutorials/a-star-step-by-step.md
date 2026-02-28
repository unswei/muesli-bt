# A* Tutorial (Step By Step)

This tutorial explains how `examples/repl_scripts/a-star-grid.lisp` implements A* in pure muslisp.

## Goal

Find the shortest path on a 2D occupancy grid from start to goal using:

- `g(n)`: cost from start to node `n`
- `h(n)`: heuristic estimate from `n` to goal
- `f(n) = g(n) + h(n)`: node priority

## Step 1: Encode Grid Coordinates As Node IDs

The script uses integer node IDs:

```lisp
(define (xy->id x y)
  (+ x (* y width)))
```

It also precomputes reverse lookup tables (`id->x`, `id->y`) via `init-coords`.

Why this matters:

- maps use compact numeric keys
- path printing can recover `(x y)` from node ID

## Step 2: Build The Map Topology

Obstacles are inserted into `obstacles` map:

```lisp
(define obstacles (map.make))
(define (block! x y)
  (map.set! obstacles (xy->id x y) #t))
```

Blocked nodes are skipped during neighbor expansion.

## Step 3: Define The Heuristic

The heuristic is Manhattan distance:

```lisp
(define (heuristic a b)
  (+ (abs (- ax bx)) (abs (- ay by))))
```

For 4-connected grids with unit cost, Manhattan distance is admissible and keeps A* optimal.

## Step 4: Prepare Search State

`astar-search` initializes:

- `open`: vector of candidate nodes
- `open-set`: map for fast “is in open?”
- `closed`: explored nodes
- `came`: parent links
- `g`: best known cost so far
- `f`: score used for open-set selection

The start node gets:

- `g[start] = 0`
- `f[start] = h(start, goal)`
- `came[start] = start`

## Step 5: Select Current Node

Because this example is intentionally non-PQ, the open set is scanned each loop:

- `pick-current-index` -> finds best `f` entry
- `vec-remove-swap!` -> removes current in O(1) by swapping with last

This keeps code simple while demonstrating baseline A* logic.

## Step 6: Relax Neighbors

`expand-neighbors` visits up to 4 neighbors.

`consider-neighbor` applies the A* relaxation rule:

1. skip if blocked or closed
2. compute tentative cost `g[current] + 1`
3. if better than known `g[neighbor]`, update:
   - `came[neighbor] = current`
   - `g[neighbor] = tentative`
   - `f[neighbor] = tentative + heuristic(neighbor, goal)`
4. push neighbor into `open` if not already there

## Step 7: Termination Conditions

`astar-loop` stops when:

- `open` is empty -> no path
- current node is `goal` -> success

On success it returns:

- `#t`
- reconstructed path vector
- expansion count

## Step 8: Reconstruct The Final Path

`reconstruct-rev` follows parent links from goal back to start, storing reversed path.

`reverse-copy` produces start->goal order for printing.

## Step 9: Print Deterministic Output

The script prints:

- grid and start/goal metadata
- `found` flag
- expansion count
- path length and each `(x y)` step

## Practical Extensions

- replace open-set scan with `pq.*` for larger maps
- add diagonal moves and adjust heuristic
- add weighted terrain (see Dijkstra tutorial)

## See Also

- [A* Example Overview](../a-star-search.md)
- [Dijkstra Tutorial](dijkstra-step-by-step.md)

