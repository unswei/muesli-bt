# agent-proposed task logic

!!! note "status"
    Status: experimental. The current path covers proposal envelopes, manifests, validation results, dry-run reports, semantic diffs, slot metadata, rollback handles, fixture evidence, and the first narrow live C++ tick-boundary install API for `slot` nodes.

## what this is

This page describes the agent-facing task-logic proposal path.

Agents do not send arbitrary robot code to the host. They send a constrained proposal envelope that contains a Lisp Behaviour Tree (BT) fragment, the target slot, the fragment contract, intent metadata, and context identity.

## when to use it

Use this path when an agent, planner, or deterministic generator proposes a recovery or task-logic change before `v1.0.0`.

Do not use it for low-level safety control, direct actuator authority, robot driver replacement, or broad unbounded code generation.

## how it works

The proposal pipeline is:

```text
agent_proposal.v1
  -> fragment validation
  -> fragment contract check
  -> capability and blackboard manifest check
  -> install policy gates
  -> semantic diff
  -> dry-run report
  -> tick-boundary install request
  -> rollback handle
  -> replay artefacts
```

The first built-in fragment contract is `guarded-recovery.v1`.
It requires a guarded subtree, bounded long-running work, a fallback for long-running work, known callbacks or capabilities, and size limits.

## api / syntax

Patchable BT slots use this DSL shape:

```lisp
(slot recovery-policy
  :contract guarded-recovery.v1
  :install at-tick-boundary
  :fallback safe-stop
  (seq
    (cond always-true)
    (act always-success)))
```

The first proposal envelope shape is:

```json
{
  "schema_version": "agent_proposal.v1",
  "proposal_id": "proposal-recovery-001",
  "source": "deterministic-template-v1",
  "intent": "recover from a blocked path",
  "context_hash": "fnv1a64:1111111111111111",
  "slot": "recovery-policy",
  "fragment_contract": "guarded-recovery.v1",
  "previous_subtree_hash": "fnv1a64:9999999999999999",
  "fragment": "(reactive-sel ...)"
}
```

The schema lives at `schemas/agent_task_logic/v1/agent_proposal.v1.schema.json`.

Validate proposal fixtures:

```bash
python3 tools/validate_generated_bt_fragment.py fixtures/dsl/generated_guarded_recovery --json
```

Export default manifests for an agent:

```bash
python3 tools/validate_generated_bt_fragment.py --export-manifests build/agent-manifests
```

The exported manifests are:

- `capability_manifest.v1`
- `blackboard_manifest.v1`
- `install_policy.v1`
- `fragment_contract.v1`

The schema directory is `schemas/agent_task_logic/v1/`.

The first live runtime API is C++-facing and experimental:

- `bt::request_subtree_install(instance&, services&, subtree_install_request)`;
- `bt::request_subtree_rollback(instance&, services&, subtree_rollback_request)`.

The runtime accepts only validated `at-tick-boundary` requests for an existing `slot`. The compiled runtime fragment must also be rooted at a matching `slot` node. The agent proposal envelope can still carry the child fragment text; host tooling is responsible for wrapping or compiling that fragment into the runtime install shape.

## example

The accepted generated guarded recovery proposal lives under:

```text
fixtures/dsl/generated_guarded_recovery/proposal-accepted/
```

The validation output includes:

- `fragment_validation_result.v1`: status, reason code, field path, slot, contract, hashes, and `host_reached=false`;
- `bt_semantic_diff.v1`: slot changed, guards, budgets, fallback status, capabilities, and hash change;
- `agent_proposal_dry_run.v1`: dry-run checks and fixed versus generated recovery summary;
- rollback handle with previous and new subtree hashes.

## gotchas

The current runtime keeps live subtree mutation deliberately narrow. It supports one pending install or rollback per instance, commits only at a tick boundary, preserves non-slot runtime state, and clears replaced-subtree memory, active VLA jobs, halt warnings, and profile state.

Rejected proposals keep `host_reached=false`. A rejected proposal must not reach robot execution.

## see also

- [generated guarded recovery](../tutorials/generated-guarded-recovery.md)
- [Lisp DSL generated subtree evidence](../evidence/lisp-dsl-generated-subtree.md)
- [canonical event log](../observability/event-log.md)
- [roadmap to 1.0](../roadmap-to-1.0.md)
