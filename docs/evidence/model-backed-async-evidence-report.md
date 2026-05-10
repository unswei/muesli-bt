# model-backed async evidence report

## what this is

This page defines the standard replay report format for model-backed asynchronous evidence.

The current MiniVLA smoke path writes this format as `replay_report.json` with schema `muesli-bt.model_async_evidence_report.v1`.

## when to use it

Use this report for evidence where `muesli-bt` calls an external model capability, records the result, replays from cache, and checks that runtime validation and replay behaviour are stable.

The format is intended for VLA sessions, world-model calls, and later flagship model-mediated runs. It is not MiniVLA-specific.

## how it works

The report has four stable sections:

- `summary`: run counts and aggregate pass/fail booleans
- `gates`: release-relevant checks that should pass before the evidence is considered usable
- `conditions`: one record/replay comparison per task condition
- `artefacts`: request hashes, response hashes, and frame refs used by that condition
- `host_dispatch`: optional validated handoff to a mock host action sink

Each condition records:

- the public capability id
- the model-service operation path, such as `start`, `step`, `close`
- record status, validation result, replay-cache status, and `host_reached`
- replay status, validation result, replay-cache status, and `host_reached`
- parity checks for actions, request hashes, response hashes, and frame refs
- validated mock-host dispatch status, action hash, action dimension, and `host_reached=true` when the handoff succeeds

The release-safe companion report uses schema `muesli-bt.release_safe_model_async_evidence_report.v1`. It keeps the same summary, gates, and condition structure, but replaces raw `frame://` refs with hashes.

## api / syntax

Minimal shape:

```json
{
  "schema": "muesli-bt.model_async_evidence_report.v1",
  "profile": "model_service.vla_action_chunk_smoke.v1",
  "capability": "cap.vla.action_chunk.v1",
  "service_protocol": "MMSP v0.2",
  "evidence_kind": "model_backed_async_replay",
  "summary": {
    "condition_count": 4,
    "all_replay_hits": true,
    "all_actions_match": true,
    "all_record_actions_host_safe": true,
    "all_mock_host_dispatches_host_reached": true
  },
  "gates": [],
  "conditions": []
}
```

Required gates for the current MiniVLA smoke path:

- `record_completed`
- `replay_completed`
- `replay_cache_hit`
- `action_parity`
- `request_hash_parity`
- `response_hash_parity`
- `frame_ref_parity`
- `record_host_reached_zero`
- `replay_host_reached_zero`
- `mock_host_dispatch_completed`
- `mock_host_dispatch_validated`
- `mock_host_dispatch_reached`

## example

One condition looks like this:

```json
{
  "condition_id": "webots-epuck-goal-start",
  "capability": "cap.vla.action_chunk.v1",
  "operation_path": ["start", "step", "close"],
  "record": {
    "status": ":done",
    "final_status": ":ok",
    "validation": {
      "validation_status": "accepted",
      "reason": "host_safe_action_proposal",
      "host_reached": false
    },
    "replay_cache_hit": false,
    "host_reached": false
  },
  "replay": {
    "status": ":done",
    "final_status": ":ok",
    "validation": {
      "validation_status": "accepted",
      "reason": "host_safe_action_proposal",
      "host_reached": false
    },
    "replay_cache_hit": true,
    "host_reached": false
  },
  "parity": {
    "actions_match": true,
    "frame_refs_match": true,
    "request_hashes_match": true,
    "response_hashes_match": true
  },
  "host_dispatch": {
    "host": "mock-host.v1",
    "dispatch_status": "accepted",
    "host_reached": true,
    "action_schema": "mock_host.action_handoff.v1",
    "action_hash": "...",
    "action_dims": 7
  }
}
```

## gotchas

- A replay report proves runtime/replay behaviour for the recorded conditions. It does not prove model quality outside those conditions.
- `record.host_reached=false` and `replay.host_reached=false` mean the model output remained a proposal at the model boundary.
- `host_dispatch.host_reached=true` is only set after the proposal passes validation and is handed to the mock host action sink.
- The mock host proves the dispatch boundary and report shape. It does not claim that a physical robot accepted the action.
- Raw `frame://` refs belong in the private report. Public evidence should use `release_safe_replay_report.json`.
- Request and response hashes are deterministic evidence identifiers, not cryptographic access controls.

## see also

- [MiniVLA smoke evidence](minivla-smoke-evidence.md)
- [release-safe redaction policy](redaction-policy.md)
- [muesli-model-service bridge](../integration/model-service-bridge.md)
