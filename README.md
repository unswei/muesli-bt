# muslisp-bt (Phase 2)

A tiny Lisp core in modern C++20 for the `muslisp-bt` project.

This repository currently implements **Phase 1 + Phase 2** from the design doc:

- values + printer
- reader/parser
- lexical environment
- evaluator special forms
- minimal built-ins
- REPL + script mode
- simple non-moving mark/sweep GC
- heap/gc stats built-ins
- mixed int/float numeric model

## What Works Right Now

### Runtime values

Implemented value types:

- `nil`
- `boolean`
- `integer` (`std::int64_t`)
- `float` (`double`)
- `symbol` (interned)
- `string`
- `cons`
- `primitive_fn`
- `closure`
- `bt_def` (placeholder handle)
- `bt_instance` (placeholder handle)

### Reader/parser

Supported syntax:

- integers (`123`, `-42`)
- float literals (`3.14`, `1e-3`, `2.`)
- symbols
- booleans: `#t`, `#f`
- lists: `( ... )`
- quote sugar: `'x` -> `(quote x)`
- strings with escapes (`\n`, `\t`, `\r`, `\"`, `\\`)
- comments: `; ...` to end of line

Not supported in v1/v2 core:

- rationals (`3/7`)
- complex numbers
- numeric prefixes/suffixes

### Evaluator

Implemented special forms:

- `quote`
- `if`
- `define`
- `lambda`
- `begin`

Implemented function application for:

- primitive functions
- closures with lexical scoping

`define` supports both forms:

- `(define name expr)`
- `(define (fn arg1 arg2) body...)`

### Numeric behavior (int + float)

Implemented promotion rules:

- `int op int -> int` (except `/`, see below)
- `int op float -> float`
- `float op int -> float`
- `float op float -> float`

Division policy:

- `/` always returns float
- examples:
  - `(/ 6 3) -> 2.0`
  - `(/ 5 2) -> 2.5`
  - `(/ 4) -> 0.25`

Arithmetic behavior:

- `(+ ) -> 0`
- `(* ) -> 1`
- `(+ x) -> x`
- `(* x) -> x`
- `(- x) -> unary negation`
- `(/ x) -> reciprocal (float)`

Comparisons:

- `=`, `<`, `>`, `<=`, `>=`
- mixed int/float comparisons supported
- return Lisp booleans (`#t` / `#f`)

Numeric predicates:

- `number?`
- `int?`
- `integer?`
- `float?`
- `zero?`

Integer overflow policy:

- int arithmetic uses checked `int64_t` operations
- overflow raises runtime error

Float behavior:

- IEEE-style `nan`, `inf`, `-inf` are allowed
- printer renders them as `nan`, `inf`, `-inf`

### GC (Phase 2)

Implemented GC model:

- non-moving mark/sweep
- all Lisp values allocated through GC
- environment objects are GC-managed
- root support via global env roots and scoped temporary roots
- threshold-based collection scheduling (`next_gc_threshold`)

### Heap / GC stats built-ins

Implemented built-ins:

- `(heap-stats)`
- `(gc-stats)`

Both print:

- `total allocated objects`
- `live objects after last GC`
- `bytes allocated`
- `next GC threshold`

Behavior:

- `heap-stats` prints current snapshot
- `gc-stats` runs a collection, then prints snapshot

Note: `bytes allocated` is tracked at GC-node object granularity (not deep container heap usage).

### Other built-ins

List/predicate built-ins:

- `cons`, `car`, `cdr`, `null?`, `eq?`, `list`

Utility built-ins:

- `print`

`bt.*` built-ins are present as **phase-2 stubs** and currently raise runtime errors:

- `bt.compile`
- `bt.new-instance`
- `bt.tick`
- `bt.reset`

### REPL

- interactive prompt
- multiline expression support (continues until parse is complete)
- `:q`, `:quit`, `:exit` to leave
- optional script mode by passing a file path

## Build and Test (Linux/macOS)

### Requirements

- C++20 compiler (`clang++` or `g++`)
- CMake 3.20+
- Ninja (recommended)

### With presets

```bash
cmake --preset dev
cmake --build --preset dev -j
ctest --preset dev
```

### Without presets

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## Run

### REPL

```bash
./build/dev/muslisp
```

### Script mode

```bash
./build/dev/muslisp path/to/script.lisp
```

## Verified Status

Automated tests currently cover:

- reader basics (including float literals)
- env shadowing
- special forms + arithmetic semantics
- mixed numeric comparisons and predicates
- float `nan`/`inf` behavior and printing
- integer overflow checks
- lexical closures
- list/predicate built-ins
- GC stress allocations and stats built-ins
- phase-2 BT stub behavior

Validated on:

- AppleClang 17 (macOS)
- GNU g++-15 (Homebrew toolchain on macOS)

## Not Implemented Yet

- BT compiler/runtime semantics (phase 3+)
- decorators and BT instance memory (phase 4)
- tracing/inspectability features (phase 5)
- robotics host integration (phase 6)
