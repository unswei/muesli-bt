# Example: Continuous-Action MCTS

This example uses the language/runtime primitives for sampling-heavy search:

- `rng.*` for deterministic sampling
- `vec.*` for mutable child storage
- `map.*` for mutable node records
- `sqrt`/`log`/`exp` for UCB and progressive widening
- `time.now-ms` for deadline-based stopping

The runnable script is at:

- `examples/repl_scripts/mcts-continuous.lisp`

Run it with:

```bash
./build/dev/muslisp examples/repl_scripts/mcts-continuous.lisp
```

It prints the selected action from a toy 1D continuous control problem (`x -> x + 0.25*a`, `a in [-1,1]`).

## Source

```lisp
--8<-- "examples/repl_scripts/mcts-continuous.lisp"
```
