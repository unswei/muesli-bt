# muesli-bt

muesli-bt is a compact Lisp runtime with an integrated behaviour tree runtime for robotics and control.

It keeps behaviour logic scriptable while the [host](terminology.md#host) (backend) handles sensors, actuators, timing, and platform integration.

## Start Here

- [Getting oriented](getting-oriented/what-is-muesli-bt.md)
- [Getting started](getting-started.md)
- [Examples overview](examples/index.md)
- [Terminology](terminology.md)

## Core Docs

- [Language](language/syntax.md)
- [Behaviour trees](bt/intro.md)
- [Planning](planning/overview.md)
- [Integration](integration/overview.md)
- [Observability](observability/logging.md)

## What muesli-bt Is Good At

- Lisp-first task logic with explicit BT execution semantics
- bounded-time planning during ticks (`planner.plan`, `plan-action`)
- backend-agnostic environment API (`env.*`)
- reproducible logs/traces for debugging and evaluation
