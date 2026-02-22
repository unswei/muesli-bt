# Testing And Verification

## Test Layout

The project currently uses a single C++ test binary:

- `tests/test_main.cpp`

Coverage includes:

- reader and parser
- evaluator and closures
- numeric semantics and predicates
- GC and GC safety during evaluation
- BT compile checks
- BT runtime status propagation
- decorators and reset behaviour
- blackboard, trace, logs, scheduler stats
- phase-6 host wrappers and typed robot interface injection

## Run Tests

```bash
ctest --preset dev
```

or:

```bash
./build/dev/muslisp_tests
```

## Deterministic BT Tests

For deterministic BT tests:

- keep leaf callbacks small and explicit
- isolate one semantic rule per test
- avoid reliance on wall-clock timing where possible
- when async behaviour is needed, bound wait loops tightly

## Integration Checks

Recommended integration checks before merging:

1. compile and tick a small BT from Lisp
2. run at least one action that returns `running` before `success`
3. inspect `bt.trace.snapshot` and `bt.blackboard.dump`
4. run both clang and gcc builds (local or CI matrix)
