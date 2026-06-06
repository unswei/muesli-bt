# Lisp DSL generated subtree evidence

!!! note "status"
    Status: experimental evidence slice. The current artefacts prove deterministic generation, rejection, canonical hashing, and event-log shape. They do not claim production runtime subtree generation support.

## what this is

This page records the evidence for the generated guarded recovery subtree slice and the experimental wheeled flagship recovery-slot variant.

The slice demonstrates that Lisp BT fragments can be generated as structured data, wrapped in an agent proposal envelope, normalised, validated, hashed, represented in canonical events, and replay-loaded from a canonical artefact.

## when to use it

Use this page when reviewing whether the Lisp DSL argument has concrete test coverage rather than only prose.

Use the fixture when you need a reproducible generated-subtree example that runs without simulator, ROS2, model-service, or physical robot dependencies.

## how it works

The accepted fixture is generated from a blocked-path context:

```bash
python3 tools/generate_guarded_recovery_subtree.py
```

The flagship recovery fixture is generated from the matching wheeled blocked-path context:

```bash
python3 tools/generate_guarded_recovery_subtree.py \
  --context fixtures/dsl/generated_guarded_recovery/context-flagship-blocked-recovery.json \
  --out-dir fixtures/dsl/generated_guarded_recovery/flagship-recovery-accepted
```

The validator accepts the generated fragment only when:

- all BT node types are known;
- all host callbacks are known;
- long-running planner/model nodes have positive budgets or deadlines;
- a later fallback branch exists for long-running work;
- unsupported capabilities are rejected.

The accepted fixture emits these generated-subtree lifecycle events:

- `dsl_fragment_generated`;
- `dsl_fragment_normalised`;
- `dsl_fragment_validation_ok`;
- `dsl_fragment_compiled`;
- `subtree_install_requested`;
- `subtree_installed`;
- `subtree_rollback_requested`;
- `subtree_rolled_back`;
- `subtree_replay_loaded`.

The rejected fixtures cover:

- unsupported generated capability request;
- missing fallback around `plan-action`.
- malformed proposal envelope;
- missing slot;
- unknown fragment contract;
- denied capability;
- stale blackboard input;
- invalid budget;
- excessive depth;
- unknown callback.

## api / syntax

Run the verification slice:

```bash
python3 tests/check_generated_guarded_recovery.py
```

Or through CTest:

```bash
ctest --preset core-only -R muesli_bt_generated_guarded_recovery --output-on-failure
```

The checked-in accepted artefacts are:

```text
fixtures/dsl/generated_guarded_recovery/accepted-blocked-path/fragment.lisp
fixtures/dsl/generated_guarded_recovery/accepted-blocked-path/canonical_fragment.lisp
fixtures/dsl/generated_guarded_recovery/accepted-blocked-path/validation_report.json
fixtures/dsl/generated_guarded_recovery/accepted-blocked-path/events.jsonl
fixtures/dsl/generated_guarded_recovery/accepted-blocked-path/replay_report.json
fixtures/dsl/generated_guarded_recovery/proposal-accepted/proposal.json
```

The experimental flagship artefacts are:

```text
examples/flagship_wheeled/lisp/bt_goal_flagship_generated_recovery.lisp
fixtures/dsl/generated_guarded_recovery/flagship-recovery-accepted/fragment.lisp
fixtures/dsl/generated_guarded_recovery/flagship-recovery-accepted/fixed_vs_generated_report.json
fixtures/dsl/generated_guarded_recovery/proposal-flagship-accepted/proposal.json
fixtures/dsl/generated_guarded_recovery/proposal-flagship-rejected-contract/proposal.json
```

The experimental flagship BT source is:

```lisp
--8<-- "examples/flagship_wheeled/lisp/bt_goal_flagship_generated_recovery.lisp"
```

## example

The accepted validation report records:

```json
{
  "canonical_dsl_hash": "fnv1a64:7bb0dd1bf850f536",
  "fallback_policy": "required_and_present",
  "long_running_nodes": ["plan-action"],
  "node_count": 8
}
```

The replay report requires the same canonical DSL hash to appear in generation, validation, installation, and replay-loaded events.

The accepted proposal validation result also includes a semantic diff, dry-run report, and rollback handle. All rejected proposal fixtures report `host_reached=false`.

The flagship comparison report proves that the fixed recovery branch remains the rollback target for slot `recovery-policy`, while the generated recovery proposal is validated before host reach.

## gotchas

The fixture evidence remains deterministic. The C++ runtime now also has an experimental live tick-boundary install and rollback path for matching `slot` fragments, covered by core unit tests. The generated guarded recovery fixture still stays as the reproducible artefact path for proposal validation, replay, and comparison.

The wheeled flagship generated-recovery BT is not the canonical cross-transport baseline yet. Existing PyBullet, Webots, and ROS2 wrappers still load the original shared flagship BT.

The generated subtree is intentionally small. The value is the validated lifecycle and replay evidence, not a clever recovery strategy.

## see also

- [agent-proposed task logic](../integration/agent-proposed-task-logic.md)
- [generated guarded recovery tutorial](../tutorials/generated-guarded-recovery.md)
- [why Lisp as DSL](../getting-oriented/why-lisp-dsl.md)
- [canonical event log](../observability/event-log.md)
- [known limitations](../known-limitations.md)
