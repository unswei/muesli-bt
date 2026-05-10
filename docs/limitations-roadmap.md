# Roadmap

This page tracks likely next areas of work. It is intentionally short and grouped by theme.

For the release-oriented path from the current baseline to `v1.0.0`, see [roadmap to 1.0](roadmap-to-1.0.md).
For the flagship release direction, see [v1.0 direction](project/v1-direction.md).

The `v1.0.0` effort is centred on dependable model-mediated wheeled robot behaviour with replayable evidence. The earlier “same BT, different IO transport” result remains foundational, but it is no longer the whole release story.

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
- one physical wheeled flagship path with matching Nav2-backed or rosbag-backed evidence
- host capability bundles for richer external services such as manipulation, navigation, and perception
- optional `muesli-model-service` bridge for capability-native world-model and VLA calls, with live images staged through frame ingest and model requests carrying `frame://` refs
- curated model-backed async evidence showing MiniVLA action-chunk proposals, validation, replay-cache hits, release-safe summaries, and a mock-host handoff
- cross-transport evidence for the existing ROS2 thin adaptor as a foundation, not the whole `v1.0.0` headline
- keep the existing Isaac Sim / ROS2 H1 showcase documented and reproducible
- add one documented Isaac Sim wheeled showcase that reuses the released `Odometry -> Twist` path and yields website-ready capture artefacts
- production VLA backends beyond the experimental model-service evidence path
- consumer-driven expansion of the thin ROS2 adaptor only after parity and evidence needs are clear
- external telemetry/export integrations

## Tooling

- stronger docs and link validation in CI
- benchmark and profiling workflows for common BT patterns
- improved packaging of runnable examples

## Contributing

Contributions are welcome. Prefer small, focused changes with tests and matching docs updates, especially when adding or changing language/BT primitives.
