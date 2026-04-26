# Example: Reactive Guard Demo

This script demonstrates guard re-check + pre-emption using `reactive-seq` and `async-seq`.
Use it after [Memoryful Sequence Demo](memoryful-sequence-demo.md), not as the first BT example.
It combines three ideas at once: a blackboard-backed guard, an async subtree, and cancellation when the guard flips.

Script path:

- `examples/repl_scripts/reactive-guard-demo.lisp`

## run it

```bash
./build/dev/muslisp examples/repl_scripts/reactive-guard-demo.lisp
```

## expected output

You should see approximately:

```text
reactive-guard-demo
(tick-1 running)
(tick-2-stop failure)
("...sched_submit..." "...sched_cancel..." "...tick_end...")
nil
```

The event records include run ids and timings, so they will not match byte for byte.

## expected artefacts

This example prints event excerpts from memory and does not write files.

## source

```lisp
--8<-- "examples/repl_scripts/reactive-guard-demo.lisp"
```

## What The Tree Is Doing

The root is a `reactive-seq`.
Its first child is `(invert (cond bb-has stop))`, which means "keep going only while the `stop` key is absent".
Its second child is an `async-seq` with two long-running sleeps, so the tree has work that can still be in progress when the guard is checked again on the next tick.

## What Happens On Tick 1

- the blackboard does not contain `stop`
- the inverted guard succeeds
- the async subtree starts and returns `running`
- the whole tree returns `running`

## What Happens On Tick 2

- tick input seeds the blackboard with `stop`
- `bb-has stop` now succeeds, so the surrounding `invert` fails
- `reactive-seq` re-checks that earlier guard before letting the running async branch continue
- the async work is cancelled and the whole tree returns `failure`

## What To Look For

- first tick enters the async subtree and returns `running`
- second tick flips the guard and returns `failure`
- the event stream should show the guard result change and async cancellation behaviour

In practice, inspect:

- `node_status` to see the branch outcome change between the two ticks
- `bb_write` or tick-input state to confirm that `stop` was seeded
- `sched_cancel` and related scheduler events to confirm that the async work was cancelled

> Maintainer note: this page previously named `node_preempt` and `node_halt`, but [Canonical Event Log](../observability/event-log.md) does not currently list those event types. Reconcile the example wording with the canonical event reference before making the event-name claim more specific.

## next

- [Brief Behaviour Tree Introduction](../bt/intro.md)
- [Logging](../observability/logging.md)
