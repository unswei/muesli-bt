# Example: Async VLA Stub BT

This script shows non-blocking VLA integration in a behaviour tree.
It demonstrates the current repository scope: **stub integration + contract hooks** (not production model transport).

Script path:

- `examples/repl_scripts/vla-stub-bt.lisp`

Run:

```bash
./build/dev/muslisp examples/repl_scripts/vla-stub-bt.lisp
```

What to look for:

- BT keeps returning `running` while VLA job is in flight
- `flow-action`/`vla-action` is written by `vla-wait` when ready
- `vla-meta` contains structured poll metadata
- `events.dump` returns canonical event records

What is real vs placeholder:

- real now: async API shape, cancellation path, BT orchestration, event logging
- placeholder: provider auth/network integration (implemented by host backends)

## Source

```lisp
--8<-- "examples/repl_scripts/vla-stub-bt.lisp"
```
