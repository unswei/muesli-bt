# Dijkstra Tutorial (Step By Step)

This tutorial explains how `examples/repl_scripts/dijkstra-grid-pq.lisp` implements weighted-grid Dijkstra using native `pq.*` builtins.

## Goal

Compute the minimum-cost path on a weighted 2D grid where:

- each move cost is `cell-cost(neighbor)`
- obstacles are blocked
- all costs are non-negative

## Step 1: Encode Nodes And Coordinates

Like the A* example, node IDs are `id = x + y * width`.

The script precomputes:

- `id->x`
- `id->y`

for fast decode during output.

## Step 2: Define Obstacles And Terrain Costs

Two map layers are used:

- `obstacles`: blocked cells
- `terrain`: optional per-cell weights

`cell-cost` reads `terrain` with fallback `1.0`.

## Step 3: Initialize Dijkstra Structures

`dijkstra-search` creates:

- `open` as `(pq.make)` storing `(priority, node)`
- `dist` map (best known distance)
- `prev` map (parent links)
- `closed` map (finalized nodes)

Start state:

- `dist[start] = 0.0`
- `prev[start] = start`
- `(pq.push! open 0.0 start)`

## Step 4: Pop Best Candidate

`dijkstra-loop` repeatedly pops:

```lisp
(let ((entry (pq.pop! open)))
  (let ((current-dist (car entry))
        (current (car (cdr entry))))
```

The popped priority is the candidate shortest distance for that node.

## Step 5: Handle Duplicate Pushes (No Decrease-Key)

This implementation intentionally uses duplicate inserts:

- whenever a better route is found, push a new `(new-dist, node)` pair
- old queue entries become stale

Two guards skip stale work:

1. if node already in `closed`, skip
2. if popped `current-dist` is greater than `dist[current] + epsilon`, skip

This is a standard Dijkstra pattern when decrease-key is unavailable.

## Step 6: Relax Outgoing Neighbors

`expand-neighbors` checks up to four grid moves.

`relax-neighbor` performs:

1. skip blocked cell
2. `new-dist = current-dist + cell-cost(neighbor)`
3. if `new-dist < dist[neighbor]`:
   - update `dist[neighbor]`
   - set `prev[neighbor] = current`
   - push `(new-dist, neighbor)` into queue

## Step 7: Stop Conditions

Loop terminates when:

- queue is empty -> no path
- goal is popped as a valid current node -> success

The result tuple contains:

- `found`
- `dist` map
- `prev` map
- expansion count

## Step 8: Reconstruct Path

`reconstruct-path` follows `prev` from goal to start, then reverses.

Total cost is read directly from:

```lisp
(map.get dist goal inf)
```

## Step 9: Output

Printed fields include:

- `found`
- `expanded_nodes`
- `total_cost`
- `path_len`
- per-step `(x y)` coordinates

## Practical Extensions

- use 8-connected moves (diagonals) with corresponding move costs
- feed roadmap graphs instead of grids (see PRM tutorial)
- switch to A* by adding a heuristic term to queue priorities

## See Also

- [Dijkstra Example Overview](../dijkstra-pq.md)
- [PRM Tutorial](prm-step-by-step.md)

