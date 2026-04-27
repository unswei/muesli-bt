# muesli-bt

[![Linux CI](https://github.com/unswei/muesli-bt/actions/workflows/linux-ci.yml/badge.svg)](https://github.com/unswei/muesli-bt/actions/workflows/linux-ci.yml)
[![Docs Pages](https://github.com/unswei/muesli-bt/actions/workflows/docs-pages.yml/badge.svg)](https://github.com/unswei/muesli-bt/actions/workflows/docs-pages.yml)
[![Docs](https://img.shields.io/badge/docs-online-brightgreen)](https://unswei.github.io/muesli-bt/)
[![Latest Release](https://img.shields.io/github/v/release/unswei/muesli-bt?display_name=tag)](https://github.com/unswei/muesli-bt/releases)
[![Licence: MIT](https://img.shields.io/badge/licence-MIT-blue.svg)](LICENSE)

`muesli-bt` is a compact Lisp-authored Behaviour Tree runtime for robotics, with bounded-time planning, cancellable asynchronous jobs, and canonical event logs.

## what it is

`muesli-bt` combines task-level Behaviour Tree (BT) execution, bounded-time planning, cancellable asynchronous jobs, and canonical event logs in one runtime contract.

It is intended for task-level robot control where decisions must be inspectable, replayable, and robust to planner or model latency. It is not a hard real-time servo controller and does not replace ROS2, Nav2, MoveIt, or robot drivers.

`muslisp` is the executable. `muesli-bt` is the runtime and project.

## why it exists

Robotics task logic often needs more than a plain BT tick:

- planner work must respect tick budgets;
- model or VLA jobs can return late and need cancellation;
- execution traces must be good enough for debugging, conformance, and publication;
- simulator and ROS2 backends should not redefine the BT semantics.

`muesli-bt` keeps these concerns explicit through the Lisp BT DSL, the runtime contract, and the canonical `mbt.evt.v1` event stream.

## what makes it different

- Lisp-authored Behaviour Trees with explicit tick semantics.
- Bounded-time planning inside ticks.
- Cancellable async/VLA jobs.
- Canonical event logs and replay/conformance tooling.

## who this is for

Use `muesli-bt` if you need task-level robot control where:

- Behaviour Tree logic should remain editable and inspectable;
- planners or model calls must respect tick budgets;
- async jobs must be cancellable and replayable;
- execution traces matter for debugging, evaluation, or publication.

Do not use it for:

- hard real-time servo control;
- replacing ROS2, Nav2, MoveIt, or robot drivers;
- hiding arbitrary Python/C++ code behind opaque BT leaves;
- production VLA control without a host-side validation layer.

## current status

Status vocabulary used in this repository:

- `released`: documented, tested, and part of the supported release surface.
- `experimental`: implemented but not yet stable enough for strong external dependence.
- `contract-only`: documented boundary or schema, but not yet a complete executable implementation.
- `planned`: roadmap item, not released functionality.

| Area | Status | Where to start |
| --- | --- | --- |
| Core Lisp runtime | released | [language syntax](docs/language/syntax.md) |
| Behaviour Trees | released | [BT introduction](docs/bt/intro.md) |
| Bounded planning | released | [planning overview](docs/planning/overview.md) |
| Canonical event logs | released | [event log](docs/observability/event-log.md) |
| Conformance L0/L1/L2 | released | [conformance levels](docs/contracts/conformance.md) |
| PyBullet/Webots examples | released | [examples overview](docs/examples/index.md) |
| ROS2 thin transport | released | [ROS2 tutorial](docs/integration/ros2-tutorial.md) |
| Async cancellation lifecycle | released | [event log](docs/observability/event-log.md) |
| Lisp DSL round-trip and hash evidence | experimental | [why Lisp as DSL?](docs/getting-oriented/why-lisp-dsl.md) |
| Generated-fragment rejection fixtures | experimental | [why Lisp as DSL?](docs/getting-oriented/why-lisp-dsl.md) |
| Host capability bundles | contract-only | [host capability bundles](docs/integration/host-capability-bundles.md) |
| `cap.echo.v1` registry smoke path | released | [cap.call](docs/language/reference/builtins/cap/cap-call.md) |
| VLA lifecycle hooks and stubs | experimental | [VLA integration](docs/bt/vla-integration.md) |
| Production VLA providers | planned | [roadmap to 1.0](docs/roadmap-to-1.0.md) |
| Nav2/MoveIt adapters | planned | [roadmap to 1.0](docs/roadmap-to-1.0.md) |

## at a glance

![muesli-bt architecture](docs/assets/architecture-at-a-glance.svg)

`muesli-bt` runs task-level BT logic in a compact Lisp runtime. Host backends provide robot or simulator IO. Long-running planners and model calls are submitted, polled, cancelled, and logged through the runtime contract. Each run can emit canonical `mbt.evt.v1` traces for replay and inspection.

Small canonical event-log excerpt:

```json
{"schema":"mbt.evt.v1","type":"tick_begin","tick":1}
{"schema":"mbt.evt.v1","type":"node_exit","tick":1,"data":{"node_id":1,"status":"running"}}
{"schema":"mbt.evt.v1","type":"tick_end","tick":1,"data":{"root_status":"running"}}
```

## quickstart

Core runtime path:

```bash
cmake --preset dev
cmake --build --preset dev -j
./build/dev/muslisp examples/bt/hello_bt.lisp
./scripts/setup-python-env.sh
make verify-install
```

`make verify-install` writes and validates `logs/verify-install.mbt.evt.v1.jsonl`.

For the shortest walkthrough, use [first 10 minutes](docs/getting-started-10min.md). For fuller setup options, use [getting started](docs/getting-started.md).

## first examples

- [Hello BT](docs/examples/hello-bt.md): smallest BT with a visible `running` to `success` transition.
- [Runtime contract in practice](docs/tutorials/runtime-contract-in-practice.md): run, log, validate, and inspect canonical events.
- [PyBullet e-puck-style goal seeking](docs/examples/pybullet-epuck-goal.md): first simulator-backed flagship path.

## documentation

- New users: [choose your path](docs/getting-oriented/choose-your-path.md), [what is muesli-bt](docs/getting-oriented/what-is-muesli-bt.md), [feature inventory](docs/getting-oriented/feature-inventory.md), [first 10 minutes](docs/getting-started-10min.md).
- BT users: [BT introduction](docs/bt/intro.md), [BT syntax](docs/bt/syntax.md), [why not just BehaviorTree.CPP?](docs/getting-oriented/why-not-btcpp.md).
- Robot and simulator integration: [integration overview](docs/integration/overview.md), [env API](docs/integration/env-api.md), [ROS2 tutorial](docs/integration/ros2-tutorial.md).
- Tool builders and reviewers: [runtime contract v1](docs/contracts/runtime-contract-v1.md), [conformance levels](docs/contracts/conformance.md), [evidence index](docs/evidence/index.md).
- Limits and roadmap: [known limitations](docs/known-limitations.md), [roadmap to 1.0](docs/roadmap-to-1.0.md), [release notes](docs/releases/index.md).

## benchmarks and evidence

Benchmark numbers are reported for curated evidence bundles. The README links to the current harness and interpretation, but ad hoc local runs are not treated as release evidence.

- [benchmark harness](bench/README.md)
- [runtime performance notes](docs/internals/runtime-performance.md)
- [evidence index](docs/evidence/index.md)
- [canonical event log](docs/observability/event-log.md)

## ROS2 and integrations

ROS2 support is a thin host integration layer. The released baseline is Humble-focused and keeps the core transport surface intentionally narrow: `nav_msgs/msg/Odometry` in, `geometry_msgs/msg/Twist` out, canonical logs through `mbt.evt.v1`.

Start with the [ROS2 tutorial](docs/integration/ros2-tutorial.md), then read [ROS2 backend scope](docs/integration/ros2-backend-scope.md) and [host capability bundles](docs/integration/host-capability-bundles.md). Nav2 and MoveIt adapters are roadmap work unless a release note says otherwise.

## VLA status

VLA/model support is currently experimental lifecycle infrastructure: submit, poll, cancel, timeout handling, BT node semantics, and canonical logging. Production provider transport, credentials, and safety validation are planned work behind host-side capability layers unless a concrete backend is documented and tested in release notes.

See [VLA integration](docs/bt/vla-integration.md), [VLA nodes](docs/bt/vla-nodes.md), and [known limitations](docs/known-limitations.md).

## citation / paper status

Paper artefacts are in preparation. Until a preprint or accepted paper exists, cite the repository version you used. Citation metadata is provided in [CITATION.cff](CITATION.cff).

## contributing

Contributions should keep code, docs, tests, and event-contract evidence aligned. Start with [CONTRIBUTING.md](CONTRIBUTING.md), the [docs style guide](docs/contributing/docs-style-guide.md), and the [support policy](SUPPORT.md).
