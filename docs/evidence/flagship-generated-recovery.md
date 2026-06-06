# flagship generated-recovery evidence

!!! note "status"
    Status: experimental evidence slice. The current artefacts prove the wheeled flagship recovery branch can be represented as a patchable generated-recovery slot. They do not promote the generated variant into the shared simulator or ROS2 wrappers.

## what this is

This page records the release-style evidence bundle for `wheeled-goal-flagship-generated-recovery`.

The variant keeps the flagship branch order intact. The recovery branch is a `recovery-policy` slot with contract `guarded-recovery.v1`, install mode `at-tick-boundary`, and fallback `safe-stop`. The slot default child remains the fixed collision recovery branch.

## when to use it

Use this evidence when reviewing the pre-`v1.0.0` bridge between the wheeled flagship and agent-proposed recovery logic.

Use the original shared flagship for cross-transport comparison, PyBullet, Webots, ROS2, Nav2, and physical robot work until a later slice deliberately promotes this variant.

## how it works

The fixture bundle lives under `fixtures/dsl/generated_guarded_recovery/flagship-recovery-accepted/`.

The bundle proves these claims:

- the accepted proposal targets slot `recovery-policy`;
- the proposal uses `guarded-recovery.v1`;
- validation accepts the generated subtree before any host callback is reachable;
- a rejected contract proposal remains rejected with `host_reached=false`;
- canonical events record install, rollback, and replay lifecycle steps;
- rollback restores the fixed recovery subtree hash;
- fixed-versus-generated comparison remains deterministic.

The release-style manifest is:

```text
fixtures/dsl/generated_guarded_recovery/flagship-recovery-accepted/evidence_manifest.json
```

The manifest records proposal ids, slot, contract, source hash, canonical DSL hash, previous and restored subtree hashes, event log path, replay report path, comparison report path, and artefact hashes.

## api / syntax

Check the flagship evidence manifest and artefacts:

```bash
python3 tools/run_flagship_generated_recovery_evidence.py --check
```

Write a copy of the manifest to a build directory:

```bash
python3 tools/run_flagship_generated_recovery_evidence.py \
  --write-manifest build/flagship-generated-recovery/evidence_manifest.json
```

Run the full generated guarded recovery fixture check:

```bash
python3 tests/check_generated_guarded_recovery.py
```

Validate the flagship event log directly:

```bash
python3 tools/validate_log.py \
  fixtures/dsl/generated_guarded_recovery/flagship-recovery-accepted/events.jsonl
```

## example

The experimental flagship BT source is checked in here:

```lisp
--8<-- "examples/flagship_wheeled/lisp/bt_goal_flagship_generated_recovery.lisp"
```

The manifest identifies the accepted generated subtree:

```json
{
  "variant": "wheeled-goal-flagship-generated-recovery",
  "slot": "recovery-policy",
  "fragment_contract": "guarded-recovery.v1",
  "install_mode": "at_tick_boundary",
  "canonical_baseline_promoted": false,
  "wrappers_promoted": false
}
```

## gotchas

The fixture is not Nav2 evidence and is not physical robot evidence.

The event log is canonical `mbt.evt.v1`, but the install and rollback sequence is a deterministic fixture artefact. Live C++ tick-boundary install and rollback are covered by core runtime tests.

The generated recovery strategy is intentionally small. The evidence is about constrained proposal handling, validation, install boundary behaviour, rollback, and replay.

## see also

- [Lisp DSL generated subtree evidence](lisp-dsl-generated-subtree.md)
- [generated guarded recovery tutorial](../tutorials/generated-guarded-recovery.md)
- [agent-proposed task logic](../integration/agent-proposed-task-logic.md)
- [flagship task and evidence contract](../project/flagship-task-contract.md)
