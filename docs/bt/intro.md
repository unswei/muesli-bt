# Brief Behaviour Tree Introduction

Behaviour trees (BTs) are a structured way to express task logic.

They are useful in robotics because they provide:

- explicit sequencing and fallback behaviour
- clear runtime state (`success`, `failure`, `running`)
- natural composition of condition checks and actions

## Tick-Based Execution

A BT is not run once and forgotten. It is ticked repeatedly by a [host](../terminology.md#host) loop.

Each tick evaluates from the root and returns one status:

- `success`
- `failure`
- `running`

`running` means the task is in progress and should be ticked again.

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

- [BT Syntax](syntax.md)
- [BT Semantics](semantics.md)
- [PlanAction Node](plan-action-node.md)
- [VLA Integration In BTs](vla-integration.md)
