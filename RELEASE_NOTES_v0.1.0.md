# muesli-bt v0.1.0 release notes

Release date: 2026-03-02

## what is stable in v0.1.0

- CMake package consumption:
  - `find_package(muesli_bt CONFIG REQUIRED)`
  - `muesli_bt::runtime` exported target
  - optional exported targets:
    - `muesli_bt::integration_pybullet`
    - `muesli_bt::integration_webots` (when built with SDK available)
- canonical event stream contract:
  - schema name `mbt.evt.v1`
  - schema file `schema/mbt.evt.v1.schema.json`
  - deterministic fixture suite under `tests/fixtures/mbt.evt.v1/`
- Studio integration contract:
  - `docs/contracts/muesli-studio-integration.md`
  - installed share assets via `muesli_bt_SHARE_DIR`

## what is experimental

- demo-level controller tuning and simulator-specific behaviour shaping
- bridge/demo workflows that depend on optional simulator/toolchain setup
- optional Webots integration path on platforms with differing SDK/runtime layouts

## how studio consumes this release

1. install `muesli-bt` to a prefix (`cmake --install ...`)
2. consume runtime from CMake via `muesli_bt::runtime`
3. read installed contract/schema assets from `muesli_bt_SHARE_DIR`
4. validate event logs against `mbt.evt.v1`
5. use deterministic fixtures for compatibility regression checks

## upgrade notes

- use `INSTALL.txt` and `docs/getting-started-consume.md` for the supported package flow
- use `CONTRIBUTING.md` for local verification and CI-aligned checks
