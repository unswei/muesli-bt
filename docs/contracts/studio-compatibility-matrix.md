# studio compatibility matrix

## what this is

This page records `muesli-bt` release compatibility expectations for `muesli-studio`.

## when to use it

Use this page when you:

- decide which runtime release to pin in Studio
- review compatibility impact before a release

## how it works

The matrix tracks compatibility at the contract level:

- runtime contract version
- canonical event schema (`mbt.evt.v1`)
- validation lane used to assert compatibility
- support posture

## api / syntax

| muesli-bt ref | runtime contract | event schema | studio parser expectation | validation lane | support posture | notes |
| --- | --- | --- | --- | --- | --- | --- |
| `v0.1.0` tag | `1.0.0` | `mbt.evt.v1` | parser for `mbt.evt.v1` enabled | release + CI contract/schema/fixture gates | supported | first contract-facing release; no released ROS2 transport baseline |
| `v0.2.0` tag | `1.0.0` | `mbt.evt.v1` | parser for `mbt.evt.v1` enabled and tolerant of additive runtime-contract events | release + CI contract/schema/fixture gates plus `L0` and `L1` conformance; local `muesli-studio` contract/schema/fixture equality check on March 14, 2026 | supported | stable contract/conformance baseline; ROS2 is not yet a released transport surface at this tag; local check found byte-identical Studio copies of `mbt.evt.v1`, `muesli-studio-integration.md`, and the `minimal` / `planner` / `scheduler` fixture logs |
| `v0.3.0` tag | `1.0.0` | `mbt.evt.v1` | parser for `mbt.evt.v1` enabled and tolerant of additive runtime-contract events | release + CI contract/schema/fixture gates plus Ubuntu 22.04 + Humble ROS-backed tests, live runner validation, and rosbag-backed `L2` replay verification | supported | first released ROS2 thin-adaptor baseline: `Odometry` in, `Twist` out, real reset unsupported, install/export plus consumer smoke validated, direct canonical ROS-backed event parity still deferred; attached Ubuntu binary archive remained generic non-ROS |
| `v0.3.1` tag | `1.0.0` | `mbt.evt.v1` | parser for `mbt.evt.v1` enabled and tolerant of additive runtime-contract events | release + CI contract/schema/fixture gates plus a dedicated Ubuntu 22.04 + Humble ROS release job covering contract tests, rosbag-backed `L2`, install/export, and ROS consumer smoke | supported | same released ROS2 thin-adaptor baseline as `v0.3.0`, but now with a dedicated ROS-enabled Ubuntu release artefact; target hosts still require a matching ROS 2 Humble runtime |
| `v0.4.0` tag | `1.0.0` | `mbt.evt.v1` | parser for `mbt.evt.v1` enabled and tolerant of additive runtime-contract events | release + CI contract/schema/fixture gates plus Ubuntu 22.04 + Humble ROS release validation, rosbag-backed `L2`, canonical ROS artefact verification through `events.jsonl`, and trace-level replay validation tooling | supported | same released ROS2 thin-adaptor baseline, now with direct canonical ROS-backed event-log parity, explicit lifecycle anchors (`episode_begin`, `episode_end`, `run_end`), and released trace-level validation (`tools/validate_trace.py`) |
| `v0.5.0` tag | `1.0.0` | `mbt.evt.v1` | parser for `mbt.evt.v1` enabled and tolerant of additive runtime-contract events | release + CI contract/schema/fixture gates plus cross-transport flagship comparison, same-robot strict comparison, and ROS2 `L2` replay on Ubuntu 22.04 + Humble | supported | same BT, different IO transport baseline across Webots, PyBullet, and ROS2, with the ROS surface still limited to `Odometry` in and `Twist` out |
| `v0.6.0` tag | `1.0.0` | `mbt.evt.v1` | parser for `mbt.evt.v1` enabled and tolerant of additive runtime-contract events | release + CI contract/schema/fixture gates plus host capability registry smoke coverage and unchanged ROS2 `L2` replay on Ubuntu 22.04 + Humble | supported | contract and boundary release: host capability bundle docs, `cap.motion.v1`, `cap.perception.scene.v1`, `cap.call`, and deterministic `cap.echo.v1`; no event schema bump and no new robot adapter surface |

## example

Pinning strategy:

1. Studio release branches pin a supported runtime release row.
2. If runtime contract/schema changes, update this page and the matching page under `docs/releases/` in the same change.

## gotchas

- Contract/schema bumps require coordinated Studio parser updates.

## see also

- [muesli-studio integration contract](muesli-studio-integration.md)
- [runtime contract v1](runtime-contract-v1.md)
- [compatibility policy](compatibility.md)
