# Memory Management And GC

## Why This GC Model

muesli-bt uses a simple non-moving mark/sweep collector.

Reasoning:

- stable pointers across Lisp and C++ boundaries
- closure/environment cycle safety
- manageable implementation complexity for v1

## What Is GC-Managed

- Lisp values (including lists, symbols, strings, closures)
- environments

## What Is Host-Managed

- non-Lisp runtime objects such as scheduler threads
- host interfaces and external robotics systems

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

## User-Visible GC Stats

Built-ins:

- `(heap-stats)`
- `(gc-stats)`

Both print allocation and threshold counters for quick inspection.
