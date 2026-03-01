# Example: Reactive Guard Demo

This script demonstrates guard re-check + pre-emption using `reactive-seq` and `async-seq`.

Script path:

- `examples/repl_scripts/reactive-guard-demo.lisp`

Run:

```bash
./build/dev/muslisp examples/repl_scripts/reactive-guard-demo.lisp
```

## Source

```lisp
--8<-- "examples/repl_scripts/reactive-guard-demo.lisp"
```

## What To Look For

- first tick enters the async subtree and returns `running`
- second tick flips the guard and returns `failure`
- trace contains `node_preempt`, `node_halt`, and scheduler cancellation events
