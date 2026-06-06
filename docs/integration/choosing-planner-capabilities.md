# choosing planner and capability surfaces

!!! note "status"
    Status: explanatory guide for experimental and released planner surfaces.

## what this is

This page explains which planning surface to use in `muesli-bt`.

## when to use it

Use this page when choosing between `planner.plan`, `cap.navigation.v1`, `cap.motion.v1`, `cap.tamp.v1`, and generated BT fragments.

## how it works

Use `planner.plan` for bounded in-runtime action selection.

Use `cap.navigation.v1` for host-owned navigation such as Nav2 goals, paths, progress, and cancellation.

Use `cap.motion.v1` for host-owned manipulation motion such as MoveIt target validation and arm execution.

Use `cap.tamp.v1` for symbolic/geometric task-and-motion planning such as PDDLStream/PyBullet.

Use `agent_proposal.v1` and patchable `slot` nodes when a planner proposes task logic rather than only a result value.

## api / syntax

The common Lisp shape is:

```lisp
(cap.call request-map)
```

The request map chooses a generic capability:

```lisp
(map.set! req 'capability "cap.navigation.v1")
```

Adapter names such as Nav2, MoveIt, PDDLStream, and PyBullet belong in configuration or result metadata.

## example

```text
wheeled goal selection          -> planner.plan
wheeled long navigation goal    -> cap.navigation.v1
arm pose feasibility            -> cap.motion.v1
pick-place symbolic plan        -> cap.tamp.v1
runtime task-policy patch       -> agent_proposal.v1
```

## gotchas

Do not put raw ROS messages, MoveIt objects, PDDL files, PyBullet handles, or model output directly into BT control logic.

## see also

- [cap.navigation.v1](cap-navigation-v1.md)
- [cap.motion.v1](cap-motion-v1.md)
- [cap.tamp.v1](cap-tamp-v1.md)
- [planner.plan request/result](../planning/planner-plan.md)
- [agent-proposed task logic](agent-proposed-task-logic.md)
