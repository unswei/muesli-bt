# generated guarded recovery fixtures

These fixtures cover the first deterministic generated guarded recovery subtree slice and the first agent proposal-envelope validation path.

The accepted fixture is generated from `context-blocked-path.json` by `tools/generate_guarded_recovery_subtree.py`.
The rejected fragment fixtures are hand-written unsafe or incomplete fragments that must fail validation before execution.
The proposal fixtures cover accepted and rejected `agent_proposal.v1` envelopes, including slot, contract, manifest, dry-run, semantic-diff, and rollback-handle checks.

Run:

```bash
python3 tools/generate_guarded_recovery_subtree.py
python3 tools/validate_generated_bt_fragment.py fixtures/dsl/generated_guarded_recovery/accepted-blocked-path
python3 tools/validate_generated_bt_fragment.py fixtures/dsl/generated_guarded_recovery/rejected-unknown-capability
python3 tools/validate_generated_bt_fragment.py fixtures/dsl/generated_guarded_recovery/rejected-missing-fallback
python3 tools/validate_generated_bt_fragment.py fixtures/dsl/generated_guarded_recovery/proposal-accepted --json
python3 tools/validate_log.py fixtures/dsl/generated_guarded_recovery/accepted-blocked-path/events.jsonl
python3 tools/validate_trace.py check fixtures/dsl/generated_guarded_recovery/accepted-blocked-path/events.jsonl
```
