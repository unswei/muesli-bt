# Brief Behaviour Tree Introduction

Behaviour trees (BTs) are a structured way to express task logic.

They are useful in robotics because they provide:

- explicit sequencing and fallback behaviour
- clear runtime state (`success`, `failure`, `running`)
- natural composition of condition checks and actions

If you are new to muesli-bt specifically, start by learning four forms: `seq`, `sel`, `cond`, and `act`.
Most of the other composites on this page are refinements of those basic ideas.

## First Tree

This tiny tree is enough to explain the basic shape:

```lisp
(defbt hello-tree
  (seq
    (cond always-true)
    (act running-then-success 1)))
```

How to read it:

- `defbt` defines a compiled tree named `hello-tree`
- `seq` means both children must succeed in order
- `cond always-true` checks a host condition callback
- `act running-then-success 1` calls a host action callback that returns `running` once, then `success`

On the first tick, the tree returns `running`.
On the second tick, it returns `success`.
That is the core repeated-tick model in one small example.

## Tick-Based Execution

A BT is not run once and forgotten. It is ticked repeatedly by a [host](../terminology.md#host) loop.

Each tick evaluates from the root and returns one status:

- `success`
- `failure`
- `running`

`running` means the task is in progress and should be ticked again.

## What `cond` And `act` Refer To

`cond` and `act` are BT leaf forms.
The names that follow them are not special Lisp keywords. They are callback names registered by the host or backend.

For example:

- `(cond target-visible)` asks the host to evaluate a condition callback named `target-visible`
- `(act approach-target)` asks the host to run an action callback named `approach-target`

The BT decides which leaf should run.
The host decides how that condition or action is actually implemented on a simulator or robot.

## Node Categories

- composites:

    - memoryless: `seq`, `sel`
    - memoryful: `mem-seq`, `mem-sel`
    - yielding/reactive: `async-seq`, `reactive-seq`, `reactive-sel`

- decorators: transform child outcomes (`invert`, `repeat`, `retry`)
- leaves: host callbacks (`cond`, `act`), bounded-time planning (`plan-action`), and async VLA orchestration (`vla-request`, `vla-wait`, `vla-cancel`)

This prepares you for the project BT language (DSL: a small purpose-built language for behaviour trees) in [BT Syntax](syntax.md).

Authoring helpers are available:

- `(bt ...)` compiles one DSL form
- `(defbt name ...)` defines a compiled tree

Most readers should learn the memoryless composites first:

- `seq`
- `sel`
- `cond`
- `act`

## Composite Quick Reference

| Composite | Category | Core intent |
| --- | --- | --- |
| `seq` | memoryless | restart from child 0 each tick |
| `sel` | memoryless | retry higher-priority children each tick |
| `mem-seq` | memoryful | resume from last running/failing child |
| `mem-sel` | memoryful | stay on selected running child; skip earlier failed children |
| `async-seq` | yielding | return `running` between child boundaries |
| `reactive-seq` | reactive | continuously re-check earlier guards and pre-empt stale running work |
| `reactive-sel` | reactive | continuously re-check priorities and pre-empt lower running work |

## See Also

- [Example: Hello BT](../examples/hello-bt.md)
- [Memoryful Sequence Demo](../examples/memoryful-sequence-demo.md)
- [Reactive Guard Demo](../examples/reactive-guard-demo.md)
- [BT Syntax](syntax.md)
- [BT Semantics](semantics.md)
- [PlanAction Node](plan-action-node.md)
- [VLA Integration In BTs](vla-integration.md)
