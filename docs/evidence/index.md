# evidence index

This page groups the evidence behind the main `muesli-bt` claims. It distinguishes raw artefacts from interpretation pages.

## runtime contract

- [runtime contract v1](../contracts/runtime-contract-v1.md)
- [compatibility policy](../contracts/compatibility.md)
- [canonical event log](../observability/event-log.md)
- [conformance levels](../contracts/conformance.md)

## benchmarks

- [benchmark harness](https://github.com/unswei/muesli-bt/blob/main/bench/README.md)
- [runtime performance interpretation](../internals/runtime-performance.md)
- [tick audit record](../observability/tick-audit.md)

Benchmark pages should include hardware, commit, compiler, build flags, commands, raw output location, and interpretation limits before numbers are treated as release evidence.

## cross-transport evidence

- [cross-transport flagship comparison](../examples/cross-transport-flagship-comparison.md)
- [cross-transport flagship for v0.5](../integration/cross-transport-flagship.md)
- [same-robot strict comparison](../integration/same-robot-strict-comparison.md)
- [PyBullet e-puck-style goal seeking](../examples/pybullet-epuck-goal.md)
- [Webots e-puck goal seeking](../examples/webots-epuck-goal-seeking.md)

## replay evidence

- [event log validation tools](../observability/event-log.md#validation)
- [deterministic fixtures](https://github.com/unswei/muesli-bt/tree/main/tests/fixtures/mbt.evt.v1)
- [fixture bundles](https://github.com/unswei/muesli-bt/tree/main/fixtures)

Reproduction commands live in the linked contract and conformance pages.

## model-backed VLA evidence

- [release-safe redaction policy](redaction-policy.md)
- [MiniVLA smoke evidence](minivla-smoke-evidence.md)
- [muesli-model-service bridge](../integration/model-service-bridge.md)
- [VLA backend integration plan](../integration/vla-backend-integration-plan.md)

## ROS2 evidence

- [ROS2 tutorial](../integration/ros2-tutorial.md)
- [ROS2 backend scope](../integration/ros2-backend-scope.md)
- [conformance L2](../contracts/conformance.md#how-it-works)
- [Isaac Sim / ROS2 TurtleBot3 showcase](../examples/isaac-wheeled-ros2-showcase.md)

ROS2 remains a thin host integration layer. It does not redefine core BT semantics.

## known limitations

- [known limitations](../known-limitations.md)
- [roadmap to 1.0](../roadmap-to-1.0.md)
- [release notes](../releases/index.md)
