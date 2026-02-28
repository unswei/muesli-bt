# Example: Async VLA Stub BT

This script shows non-blocking VLA integration in a behaviour tree.

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
- `vla.logs.dump` returns JSON lines records
