# Roadmap

This page tracks likely next areas of work. It is intentionally short and grouped by theme.

For the release-oriented path from the current baseline to `v1.0.0`, see [roadmap to 1.0](roadmap-to-1.0.md).

## Language

- richer macro support beyond quasiquote templates
- additional data-structure ergonomics around `vec`/`map`
- broader persistence and interchange helpers where needed

## Behaviour Tree Runtime

- explicit leaf `halt` lifecycle for long-running actions
- async fallback composite (`async-sel`, BT.CPP-style `AsyncFallback`) in a future version
- stronger blackboard schema validation and typed contracts for complex leaves

## Integrations

- richer [host](terminology.md#host) service adapters for production robotics systems
- host capability bundles for richer external services such as manipulation, navigation, and perception
- cross-transport evidence for the existing ROS2 thin adaptor
- keep the existing Isaac Sim / ROS2 H1 showcase documented and reproducible
- additional VLA backends beyond stub/replay capability adapters
- consumer-driven expansion of the thin ROS2 adaptor only after parity and evidence needs are clear
- external telemetry/export integrations

## Tooling

- stronger docs and link validation in CI
- benchmark and profiling workflows for common BT patterns
- improved packaging of runnable examples

## Contributing

Contributions are welcome. Prefer small, focused changes with tests and matching docs updates, especially when adding or changing language/BT primitives.
