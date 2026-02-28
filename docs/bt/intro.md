# Brief Behaviour Tree Introduction

Behaviour trees (BTs) are a structured way to express task logic.

They are useful in robotics because they provide:

- explicit sequencing and fallback behaviour
- clear runtime state (`success`, `failure`, `running`)
- natural composition of condition checks and actions

## Tick-Based Execution

A BT is not run once and forgotten. It is ticked repeatedly by a host loop.

Each tick evaluates from the root and returns one status:

- `success`
- `failure`
- `running`

`running` means the task is in progress and should be ticked again.

## Node Categories

- composites: control child flow (`seq`, `sel`)
- decorators: transform child outcomes (`invert`, `repeat`, `retry`)
- leaves: host callbacks (`cond`, `act`) and bounded-time planning (`plan-action`)

This prepares you for the project BT DSL (domain-specific language) in [BT Syntax](syntax.md).

Authoring helpers are available:

- `(bt ...)` compiles one DSL form
- `(defbt name ...)` defines a compiled tree
