# Example: Hello BT

Path: `examples/bt/hello_bt.lisp`

This is the smallest BT example that still shows a state change across ticks.
Use it after [Example: muslisp Basics](lisp-basics.md) if you want to see how `defbt`, instance creation, and repeated ticking fit together.

Run it with:

```bash
./build/dev/muslisp examples/bt/hello_bt.lisp
```

The tree is a two-child `seq`.
The condition leaf must succeed first, then the action leaf is allowed to run.
Because the action returns `running` once before it returns `success`, the whole tree changes behaviour between the first and second tick.

## Where These Leaf Names Come From

`always-true` and `running-then-success` are demo callbacks installed by the default runtime host.
They are useful for teaching because they make the BT shape obvious without requiring a simulator or robot backend first.
In a real integration you register your own condition and action callbacks, then refer to them through the same `cond` and `act` forms.

```lisp
--8<-- "examples/bt/hello_bt.lisp"
```

## What Happens On Tick 1

- `(cond always-true)` succeeds immediately
- `(act running-then-success 1)` returns `running` on its first tick
- the enclosing `seq` therefore returns `running`

## What Happens On Tick 2

- the sequence starts again from the first child
- `always-true` succeeds again
- `running-then-success` reaches its configured completion point and returns `success`
- the whole tree returns `success`

## What `events.dump` Is Showing

This example ends with `(events.dump 20)`.
That call returns the most recent canonical event-log lines from the in-memory event ring.
The default runtime host has event capture enabled, so this script can inspect recent events without calling `events.enable` first.
Because the script does not set an event-log path, it is inspecting the in-memory log rather than writing a file.

## What This Shows

- BT authoring sugar with `defbt`
- compile + instance creation
- `running` transition to `success`
- trace inspection after ticks

Read next:

- [Brief Behaviour Tree Introduction](../bt/intro.md)
- [Logging](../observability/logging.md)
