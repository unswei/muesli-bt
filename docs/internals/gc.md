# Memory Management And GC

## Why This GC Model

muesli-bt uses a simple non-moving mark/sweep collector.

Reasoning:

- stable pointers across Lisp and C++ boundaries
- closure/environment cycle safety
- manageable implementation complexity

## What Is GC-Managed

- Lisp values (including lists, symbols, strings, closures)
- mutable containers (`vec`, `map`)
- runtime handles (`rng` wrapper values)
- environments

## What Is Host-Managed

- non-Lisp runtime objects such as scheduler threads
- host interfaces and external robotics systems
- RNG algorithm state implementation details (through the Lisp `rng` wrapper)

## Container Scanning Rules

`vec` and `map` are mutable, but they are still traced objects in the non-moving mark/sweep heap.

- marking a `vec` walks all live elements and marks each referenced Lisp value
- marking a `map` walks all stored mapped values and marks them
- no write barrier is required because the collector is not generational

## Root Sources

Rooting includes:

- global environment roots
- scoped temporary roots during evaluation
- interned symbols table roots

## Contributor Safety Rules

When adding code that allocates Lisp values:

- root local temporaries before further allocation
- preserve evaluation order correctness around GC calls
- add regression tests if GC can run mid-operation
- for grow/rehash paths (`vec.push!`, `map.set!`), treat the path as allocation-sensitive and keep live references rooted

## Read/Write Round-Trip Constraints

`write` and `write-to-string` are used for read-back-safe serialisation.

Practical implications for contributors:

- readable output must stay parseable by the reader
- non-readable runtime objects (`primitive_fn`, `closure`, `bt_def`, `bt_instance`) must not be silently serialised
- when evaluating file forms repeatedly (`load`), temporary values must be rooted across allocations

## User-Visible GC Stats

Built-ins:

- `(heap-stats)`
- `(gc-stats)`

Both print allocation and threshold counters for quick inspection.
