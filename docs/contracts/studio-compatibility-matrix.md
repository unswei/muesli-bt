# studio compatibility matrix

## what this is

This page records `muesli-bt` compatibility expectations for `muesli-studio`
across release tags and `main`.

## when to use it

Use this page when you:

- decide which runtime ref to pin in Studio
- validate whether `main` is safe enough for exploratory integration
- review compatibility impact before a release

## how it works

The matrix tracks compatibility at the contract level:

- runtime contract version
- canonical event schema (`mbt.evt.v1`)
- validation lane used to assert compatibility
- support posture (release support vs rolling best-effort)

## api / syntax

| muesli-bt ref | runtime contract | event schema | studio parser expectation | validation lane | support posture | notes |
| --- | --- | --- | --- | --- | --- | --- |
| `v0.1.0` tag | `1.0.0` | `mbt.evt.v1` | parser for `mbt.evt.v1` enabled | release + CI contract/schema/fixture gates | supported | first contract-facing release; no released ROS2 transport baseline |
| `v0.2.0` tag | `1.0.0` | `mbt.evt.v1` | parser for `mbt.evt.v1` enabled and tolerant of additive runtime-contract events | release + CI contract/schema/fixture gates plus `L0` and `L1` conformance | supported | stable contract/conformance baseline; ROS2 is not yet a released transport surface at this tag |
| `main` branch (rolling) | `1.0.0` until changed by contract versioning | `mbt.evt.v1` until schema bump | parser for active schema required | push/PR CI plus Linux ROS-backed checks and rosbag-backed `L2` replay on Ubuntu 22.04 + Humble | best-effort (non-stable) | first rolling ROS2 thin-adaptor baseline: `Odometry` in, `Twist` out, real reset unsupported, canonical log parity still being completed |

## example

Pinning strategy:

1. Studio production release pins latest supported runtime tag row.
2. Studio development branch may test `main` row when investigating upcoming runtime changes.
3. If runtime contract/schema changes, update this page and the matching page under `docs/releases/` in the same change.

## gotchas

- `main` compatibility is not a stability guarantee.
- Contract/schema bumps require coordinated Studio parser updates.
- A green CI run on one commit does not imply forward compatibility for later `main` commits.

## see also

- [muesli-studio integration contract](muesli-studio-integration.md)
- [runtime contract v1](runtime-contract-v1.md)
- [compatibility policy](compatibility.md)
