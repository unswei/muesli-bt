# PRM Tutorial (Step By Step)

This tutorial explains how `examples/repl_scripts/prm-2d-pq.lisp` implements a full simulator-free PRM planner in pure muslisp.

## Goal

Plan a collision-free path in continuous 2D by:

1. sampling valid free-space nodes
2. connecting each node to local neighbors with a collision-checked local planner
3. running graph shortest path (`pq.*` Dijkstra) over the roadmap

## Step 1: Set Deterministic Parameters

The script starts with deterministic, reproducible settings:

- `seed`
- `target-nodes`
- `max-sample-attempts`
- `k-neighbors`
- `edge-steps`

Using `(rng.make seed)` ensures repeated runs generate the same roadmap and result.

## Step 2: Define State Space And Obstacles

World bounds:

- `[x-min, x-max]`
- `[y-min, y-max]`

Obstacles are circles stored as `(cx cy r)` tuples inside `obstacles` vector.

## Step 3: Build Collision Predicates

The planner defines:

- `state-valid?`: point is in bounds and outside all circles
- `edge-valid?`: line segment is valid by discretized interpolation

`edge-valid-loop` samples `edge-steps` points along each segment and rejects if any sample collides.

## Step 4: Sample Valid Nodes

`sample-valid-nodes!` repeatedly draws random points and keeps only valid ones:

```lisp
(let ((x (rng.uniform rng x-min x-max))
      (y (rng.uniform rng y-min y-max)))
  ...)
```

The roadmap vector `nodes` is initialized with:

- node `0` = start
- node `1` = goal

then additional sampled nodes are appended.

## Step 5: Pick k Neighbor Candidates

For each node `i`, `collect-candidates!` scans all nodes and keeps the nearest `k-neighbors`.

Because there is no full sort primitive, a fixed-size candidate set is maintained with:

- `idxv`: candidate node IDs
- `distv`: candidate distances
- `worst-index`: replace the current worst candidate when a better one appears

## Step 6: Connect Collision-Free Edges

`connect-candidates!` attempts edges from node `i` to each candidate:

1. avoid duplicate undirected edges via `j > i`
2. run `edge-valid?` for geometric collision checking
3. on success, add symmetric adjacency entries with edge cost = Euclidean distance

`build-roadmap!` repeats this for all nodes and returns `edge-count`.

## Step 7: Run Graph Dijkstra On The Roadmap

`graph-dijkstra` reuses the same duplicate-push + stale-skip strategy as the grid Dijkstra example:

- priority queue key: current best path cost
- relax each adjacency edge
- stop when goal node ID `1` is finalized

## Step 8: Reconstruct Path In Node Space

`reconstruct-path` follows `prev` links from goal node back to start node, then reverses.

`print-path` maps node IDs to coordinates and emits ordered steps.

## Step 9: Interpret Output

The script prints:

- seed
- accepted node count
- edge count and `k`
- `found` + `expanded_nodes`
- if found: `path_len` and `path_cost`
- per-step `(x y)` coordinates

## Practical Extensions

- replace naive all-pairs candidate scan with spatial hashing or k-d trees
- adapt `edge-steps` by edge length
- add post-processing (shortcut smoothing) on final path

## See Also

- [PRM Example Overview](../prm-pq.md)
- [Dijkstra Tutorial](dijkstra-step-by-step.md)

