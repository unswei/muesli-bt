# Roadmap

This page tracks likely next areas of work. It is intentionally short and grouped by theme.

## Language

- richer macro support beyond quasiquote templates
- additional data-structure ergonomics around `vec`/`map`
- broader persistence and interchange helpers where needed

## Behaviour Tree Runtime

- explicit leaf `halt` lifecycle for long-running actions
- memoryful composites (`mem-seq`, `mem-sel`)
- stronger blackboard schema validation and typed contracts for complex leaves

## Integrations

- richer host service adapters for production robotics systems
- additional VLA backends beyond stub/replay capability adapters
- dedicated ROS2 adapter surface and packaging
- external telemetry/export integrations

## Tooling

- stronger docs and link validation in CI
- benchmark and profiling workflows for common BT patterns
- improved packaging of runnable examples

## Contributing

Contributions are welcome. Prefer small, focused changes with tests and matching docs updates, especially when adding or changing language/BT primitives.
