# Example: Async VLA Stub BT

This script shows non-blocking VLA integration in a behaviour tree.
It demonstrates the deterministic stub path. The optional model-service path is covered separately in the VLA and evidence docs.

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

- real now: async API shape, cancellation path, BT orchestration, event logging, and optional model-service sessions when configured
- placeholder here: provider auth/network integration, because this example intentionally uses the deterministic stub backend

## Source

```lisp
--8<-- "examples/repl_scripts/vla-stub-bt.lisp"
```
