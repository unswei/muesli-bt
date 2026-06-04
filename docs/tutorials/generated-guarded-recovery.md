# generated guarded recovery

!!! note "status"
    Status: experimental evidence slice. This page covers deterministic generation, validation, canonical event evidence, and replay artefacts. It does not make runtime hot-swapping a released support surface.

## what this is

This tutorial shows a deterministic generated guarded recovery subtree.

A deterministic template reads a blocked-path context and emits a Lisp Behaviour Tree (BT) subtree. The fragment is treated as untrusted data. The validator checks node shape, host callbacks, capability names, planner budget, and fallback policy before the fixture records an install-at-tick-boundary event.

## when to use it

Use this path when you want to inspect the Lisp-as-DSL evidence without requiring Nav2, a physical robot, ROS2, or a model backend.

This is useful for checking that generated BT data can be:

- generated from deterministic context;
- normalised to canonical DSL;
- validated before execution;
- identified by source and canonical hashes;
- represented in `mbt.evt.v1` lifecycle events;
- replay-loaded from a canonical artefact.

## how it works

The generator is `tools/generate_guarded_recovery_subtree.py`.

The checked-in context is `fixtures/dsl/generated_guarded_recovery/context-blocked-path.json`.

The accepted output lives under `fixtures/dsl/generated_guarded_recovery/accepted-blocked-path/`:

- `fragment.lisp`: generated source fragment;
- `canonical_fragment.lisp`: normalised one-line DSL artefact;
- `validation_report.json`: accepted validation metadata;
- `events.jsonl`: canonical lifecycle event stream;
- `replay_report.json`: replay checks for the canonical hash and event set.

Rejected fragments live beside it and prove that unsafe or incomplete generated data still fails before install:

- `rejected-unknown-capability`;
- `rejected-missing-fallback`.

## api / syntax

Regenerate the accepted fixture:

```bash
python3 tools/generate_guarded_recovery_subtree.py
```

Validate all generated guarded recovery fixtures:

```bash
python3 tools/validate_generated_bt_fragment.py fixtures/dsl/generated_guarded_recovery
```

Validate the generated event stream:

```bash
python3 tools/validate_log.py fixtures/dsl/generated_guarded_recovery/accepted-blocked-path/events.jsonl
python3 tools/validate_trace.py check fixtures/dsl/generated_guarded_recovery/accepted-blocked-path/events.jsonl
```

Run the CTest slice:

```bash
ctest --preset core-only -R muesli_bt_generated_guarded_recovery --output-on-failure
```

## example

The generated source is also checked in as `examples/bt/generated_guarded_recovery.lisp`:

```lisp
--8<-- "examples/bt/generated_guarded_recovery.lisp"
```

The fragment has three safety properties:

- the first two children are guards: `blocked-path?` and `observation-fresh?`;
- the long-running `plan-action` has a positive `:budget_ms`;
- the outer `reactive-sel` has a later `(act safe-stop)` fallback branch.

## gotchas

The install events in this tutorial are deterministic fixture evidence. Live C++ runtime hot-swap is not part of the released API.

The generator is deterministic on purpose. A planner or model can fill in recovery parameters, but the subtree structure should still be produced by a constrained compiler and checked by the same validator.

Hashes identify artefacts for replay and comparison. They are not a safety boundary.

## see also

- [generated guarded recovery evidence](../evidence/lisp-dsl-generated-subtree.md)
- [why Lisp as DSL](../getting-oriented/why-lisp-dsl.md)
- [canonical event log](../observability/event-log.md)
- [roadmap to 1.0](../roadmap-to-1.0.md)
