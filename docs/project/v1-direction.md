# v1.0 direction

## what this is

This page states the product direction for `muesli-bt` from the current baseline to `v1.0.0`.

Use it together with the [roadmap to 1.0](../roadmap-to-1.0.md), [todo](../todo.md), and [known limitations](../known-limitations.md) pages when deciding what belongs in the public `v1.0.0` release.

The purpose of this page is to keep the release focused on one strong runtime story rather than many loosely related features.

## the flagship

The main `v1.0.0` anchor is dependable model-mediated wheeled robot behaviour on a physical robot.

The preferred task family is semantic inspection or semantic navigation on a wheeled platform. The exact robot and environment may vary, but the public release should centre on one reproducible wheeled flagship rather than several equal scenarios.

This direction builds on the existing cross-transport baseline:

- same BT semantics across Webots, PyBullet, and ROS2
- canonical `mbt.evt.v1` logs and replay artefacts
- thin ROS2 transport plus host capability boundaries

## the core product claim

The `v1.0.0` release should support a narrow, testable product claim:

- `muesli-bt` keeps BT semantics stable across transports and host deployments
- model-backed outputs are untrusted until validated against runtime and host policies
- stale, invalid, late, or policy-violating outputs do not silently reach host execution
- canonical logs and replay artefacts make failures reproducible, comparable, and explainable

This is a runtime and product claim. It is not a claim that model outputs are safe by default, and it is not a claim that `muesli-bt` replaces robot drivers, Nav2, MoveIt, or low-level safety systems.

## what is in scope before v1.0.0

The main `v1.0.0` scope should include:

- one real model-backed asynchronous capability path
- one physical wheeled runbook with required artefacts
- one Nav2-backed host capability path for the same task family
- one generated BT fragment demonstration that makes the Lisp DSL argument visible
- one fair BehaviorTree.CPP comparison path for the flagship scenario
- one reproducible evidence bundle contract for logs, manifests, replay reports, and supporting artefacts

## what is not the main v1.0 story

The `v1.0.0` release should not be led by:

- generic robotics IDE positioning
- broad visual-tooling scope
- manipulation-first positioning
- several equal flagship scenarios competing for release priority
- broad ROS2 surface expansion beyond the transport-plus-capability boundary

Supporting lanes such as Isaac Sim, H1, and future manipulation work can remain documented, but they should not displace the wheeled flagship sequence.

## fallback posture

The preferred `v1.0.0` evidence path is a physical wheeled flagship.

If hardware readiness slips, the acceptable fallback is a reproducible Nav2-backed or rosbag-backed ROS2 evidence path for the same task family, with the limitation stated plainly in release docs and artefacts.

That fallback should preserve:

- the same BT semantics
- the same capability and validation story
- the same canonical logging and replay discipline
- the same failure taxonomy and evidence bundle expectations

## how this maps onto the roadmap

This direction changes priority more than it changes architecture.

It means:

- `v0.8.0` should focus on real model-backed capability execution, validation, rejection, replay cache, and deterministic fault injection for the flagship task family
- `v0.9.0` should focus on the physical wheeled bridge, Nav2-backed capability work, fair baselines, and evaluation hardening
- `v1.0.0` should focus on reproducible flagship evidence bundles, release artefacts, and the supported public engine boundary

It also means that early documentation and runbooks for the flagship should land before broader scope expansion.

## release policy

Before `v1.0.0`, new work should be justified by at least one of these:

- it strengthens the flagship task
- it strengthens the runtime contract or host-validation story
- it strengthens reproducible release evidence

If a proposed feature does not improve one of those three areas, it should normally wait until after the release baseline is secure.

## see also

- [roadmap to 1.0](../roadmap-to-1.0.md)
- [todo](../todo.md)
- [known limitations](../known-limitations.md)
- [ROS2 backend scope](../integration/ros2-backend-scope.md)
- [cross-transport flagship](../integration/cross-transport-flagship.md)
