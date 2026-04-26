# runtime performance

## what this is

This page records the current runtime performance guidance for `muesli-bt`.

It has two jobs:

1. define the target shape of a fast and predictable behaviour tree (BT) runtime
2. prioritise the next optimisation steps from actual benchmark evidence rather than instinct

The core principle remains:

> Lisp is the authoring language. The runtime should erase Lisp structures from the hot path as early as possible.

The first benchmark run also shows that some parts of the runtime are already healthy. That changes the immediate work order.

## when to use it

Use this note when you are:

- changing BT runtime internals
- profiling control-loop latency
- deciding whether a structural refactor is worth doing now
- reviewing benchmark results before optimisation work

Use the benchmark harness and the analysis script alongside this page:

```bash
./build/bench-release/bench/bench run-all
python3 bench/scripts/analyse_results.py
```

## how it works

The benchmark-driven view is simple:

- minimal dispatch cost is already good
- static traversal cost scales cleanly
- static jitter is already good
- full-trace logging is still too expensive
- GC and long-run heap-live evidence still need a GC-producing run

That means the runtime should not try to optimise every hot path at once.

### benchmark-driven summary

The current local `bench-release` result set `bench/results/20260425T115357Z` was generated on:

- `muesli-bt` commit `654a1e43cd`
- Apple M3
- Darwin `25.4.0`
- `8` physical / `8` logical cores
- AppleClang `17.0.0.17000604`
- Release build with `-O3 -DNDEBUG`

Headline numbers:

- single-leaf baseline: `125 ns` median, `7.49 Mticks/s`
- sequence traversal: `1.46 us` at `31` nodes, `11.33 us` at `255` nodes, about `44.08 ns/node`
- selector traversal: `1.42 us` at `31` nodes, `11.21 us` at `255` nodes, about `43.71 ns/node`
- alternating traversal: `1.42 us` at `31` nodes, `11.08 us` at `255` nodes, about `43.16 ns/node`
- tick jitter: `11.21 us` median, `13.21 us` p99, `23.54 us` p99.9, ratio `1.17x`
- slowest interruption case: `B2-reactive-255-flip100-off` at `5.50 us` median interruption latency
- worst timed allocation pressure in the analysed run: `0.00 alloc/tick`
- single-leaf full trace slowdown: `9.34x`
- static full trace slowdown: `16.00x`
- reactive full trace slowdown: `16.13x`

The generated evidence report for that run recorded:

- no semantic-error runs
- `6` zero-allocation `B1` aggregate rows
- tail-latency and memory/GC figure scripts were able to render output
- no `gc_begin` or `gc_end` events were attached to the result set
- heap-live slope and GC pause evidence remain pending

The raw result directory is not linked from the top-level README because `bench/results/` is intentionally ignored and this run includes a large `jitter_trace.csv`. Publish a trimmed artefact bundle before treating these exact files as release evidence.

### earlier comparison baseline

The first benchmark run used result set `bench/results/20260314T234021Z` on:

- `muesli-bt` commit `7f2a5dc14a`
- Apple M3
- Darwin `24.6.0`
- `8` physical / `8` logical cores
- AppleClang `17.0.0.17000604`
- Release build with `-O3 -DNDEBUG`

Headline numbers:

- single-leaf baseline: `84 ns` median, `7.41 Mticks/s`
- sequence traversal: `1.33 us` at `31` nodes, `10.33 us` at `255` nodes, about `40.18 ns/node`
- selector traversal: `1.29 us` at `31` nodes, `10.25 us` at `255` nodes, about `39.99 ns/node`
- alternating traversal: `1.33 us` at `31` nodes, `10.83 us` at `255` nodes, about `42.41 ns/node`
- tick jitter: `10.83 us` median, `12.00 us` p99, `23.96 us` p99.9, ratio `1.07x`
- slowest interruption case: `B2-reactive-255-flip5-off` at `6.08 us` median interruption latency
- worst allocation pressure: `B2-reactive-255-flip5-off` at `33.00 alloc/tick`
- single-leaf full trace slowdown: `54.07x`
- static full trace slowdown: `65.31x`
- reactive full trace slowdown: `67.14x`

Correctness was clean in that run, with no semantic errors reported.

### current optimisation order

The immediate order is:

1. keep the strict precompiled-tick zero-allocation lane as a regression guard
2. add a GC-producing long-run benchmark with canonical `gc_begin` and `gc_end` logs
3. reduce logging and replay overhead
4. keep `A1`, `B1`, and `A2` as regression guards
5. only then revisit broader structural tuning where still useful

### design goals

The runtime should aim for:

- zero heap allocation during normal ticks
- predictable branch behaviour
- contiguous memory layouts
- integer node ids instead of pointers or symbols
- fast callback dispatch
- cheap blackboard access
- near-zero logging overhead when disabled
- bounded and predictable logging overhead when enabled

The compiled definition should be immutable and shared across instances.

The runtime instance should contain all mutable execution state.

### priority 1: preserve zero-allocation steady-state ticks

The current benchmark result shows zero timed allocations for the analysed `B1` and `B2` rows. Treat that as a regression guard, not as the final memory story.

Keep running:

```bash
ctest --preset bench-release -R muesli_bt_bench_precompiled_tick_allocation_strict --output-on-failure
```

The strict lane warms and primes precompiled static and reactive trees, then fails on allocations during the measured tick loop except inside explicitly whitelisted logging paths. It includes logging-off `B1` shapes, a representative logging-off `B2` reactive shape, and a logging-on `B6` full-trace shape.

### priority 2: add GC and long-run memory evidence

The current result set does not include canonical GC lifecycle events. That means the memory/GC figure is useful as a scaffold, but it is not yet evidence for GC pause distribution or heap-live slope.

The next benchmark evidence should include:

- canonical `gc_begin` and `gc_end` events
- p50, p95, p99, and p999 GC pause summaries
- heap-live-byte slope over a long run
- resident-set-size slope over a long run
- event-log size per tick

### priority 3: reduce logging and replay overhead

Full-trace logging is expensive enough to deserve explicit work.

Logging should have:

- near-zero overhead when disabled
- bounded overhead when enabled
- no dynamic allocation on the hot path in the default mode
- a fast buffered mode as the normal default
- an optional immediate-serialisation debug mode when required

Recommended implementation direction:

- use compact numeric trace events only
- append into a preallocated ring buffer or chunk buffer
- defer formatting and serialisation by default
- keep the disabled-path branch cheap
- resolve node names later from the definition rather than copying strings into events

Every change here should be checked against `B6` first, then `A1` and `A2`.

### reactive interruption checks

Reactive interruption should remain covered because it exercises cancellation-like control flow.

Profile the `B2` path when changing:

- cancellation bookkeeping
- halt event generation
- temporary child lists or traversal stacks
- async state creation or destruction
- status transition recording
- blackboard updates triggered by interruption
- exception or error wrapping paths

The target remains simple: a normal interruption or cancellation path should allocate nothing during a tick.

### priority 4: preserve current strengths

Treat these scenarios as regression guards:

- `A1` for minimal dispatch health
- `B1` for traversal scaling health
- `A2` for jitter health

### priority 5: structural tuning after the obvious pain points

The target architecture below is still the right direction. It is just no longer the first emergency task unless profiling shows that the logging, GC, or interruption paths start there.

Still recommended medium-term work:

- keep `bt_def` immutable and contiguous
- keep `bt_instance` dense and indexed by node id
- resolve callbacks at compile time
- slot blackboard keys at compile time
- precompute subtree flags

## api / syntax

The recommended compilation pipeline is:

```text
DSL (Lisp)
    ↓
parsed BT AST
    ↓
symbol resolution + validation
    ↓
BT intermediate representation
    ↓
node id assignment
    ↓
lowering to packed runtime layout
    ↓
bt_def
```

After compilation, no symbol lookup should occur during ticks.

### proposed `bt_def` layout

`bt_def` is the immutable compiled representation of the tree. It should use contiguous arrays, with node index as the node id.

```text
struct bt_def {
    uint32_t node_count;

    NodeType *node_type;

    uint32_t *first_child;
    uint16_t *child_count;

    uint32_t *callback_id;

    uint32_t *flags;

    uint32_t *bb_read_slot;
    uint32_t *bb_write_slot;

    uint32_t root_node;
};
```

Useful compile-time flags include:

- `FLAG_CAN_RETURN_RUNNING`
- `FLAG_HAS_ASYNC_CHILD`
- `FLAG_READS_BLACKBOARD`
- `FLAG_WRITES_BLACKBOARD`
- `FLAG_NEEDS_HALT`

Callback dispatch should use table indices rather than symbol lookup:

```text
callback_id[node] -> callback table index
```

### proposed `bt_instance` layout

`bt_instance` should hold dense mutable state indexed by node id:

```text
struct bt_instance {
    bt_def *definition;

    NodeStatus *node_status;
    uint16_t *child_cursor;

    AsyncState *async_state;

    uint32_t tick_index;

    Blackboard bb;

    RuntimeCounters counters;

    TraceBuffer trace_buffer;

    ScratchBuffers scratch;
};
```

Helpful substructures include:

```text
struct AsyncState {
    uint8_t running;
    uint32_t start_tick;
    uint32_t deadline_tick;
}
```

```text
struct ScratchBuffers {
    uint32_t *halt_stack;
    uint32_t halt_stack_capacity;

    uint32_t *traversal_stack;
    uint32_t traversal_stack_capacity;
}
```

```text
struct TraceEvent {
    uint32_t tick;
    uint32_t node;
    uint8_t  event_type;
    uint8_t  old_status;
    uint8_t  new_status;
}
```

### expected hot-path properties

After compilation the hot path should:

- use integer node ids
- perform no symbol lookup
- perform no allocations in normal ticks
- avoid pointer chasing
- operate on contiguous arrays
- append only compact numeric events when tracing is enabled

## example

A practical optimisation loop should look like this:

```bash
./build/bench-release/bench/bench run-group B2
python3 bench/scripts/analyse_results.py
```

Then:

1. identify the top allocation site on the `B2` path
2. remove or preallocate that source
3. re-run `B2`
4. re-run `A1` and `A2` as regression guards
5. repeat until allocation pressure is no longer dominant
6. move to `B6` and repeat the same pattern for logging cost

## gotchas

- Do not optimise everything at once. The first benchmark run already shows that `A1`, `B1`, and `A2` are healthy.
- Do not treat full-trace mode as representative of normal runtime cost. It is currently a debugging-heavy path.
- Do not let logging implementation details leak into interruption mechanics. Those concerns should stay separate.
- Do not reintroduce symbol lookup, dynamic key lookup, or heap allocation into the tick path while chasing other wins.

## see also

- [testing and verification](../testing.md)
- [profiling and performance](../observability/profiling.md)
- [architecture](architecture.md)
- [code map](code-map.md)
- [terminology](../terminology.md)
