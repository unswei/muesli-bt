# release-safe redaction policy

## what this is

This page defines the release-safe redaction boundary for model-backed evidence bundles.

The policy applies to `muesli-bt` evidence that uses `muesli-model-service`, including MiniVLA smoke runs and later flagship model-backed runs.

## when to use it

Use this policy before publishing or attaching model-backed evidence outside the development machine.

Use raw artefacts for local reproduction and debugging. Use release-safe artefacts for public release notes, issue discussion, and website evidence.

## how it works

Generated evidence bundles may contain two classes of artefact:

- raw artefacts, kept for local reproducibility
- release-safe summaries, safe to publish after normal review

Raw artefacts may contain prompts, raw `frame://` refs, service-local session ids, backend placement details, worker URLs, raw model-service envelopes, and model outputs.

Release-safe summaries keep stable hashes, counts, statuses, validation outcomes, replay parity, and allowlisted public metadata. They do not copy raw prompts, raw frame refs, raw envelopes, service-local request ids, service-local session ids, or backend placement details.

## api / syntax

The MiniVLA smoke evidence runner writes this boundary explicitly:

```text
redaction_policy.json
release_safe_prompt_summary.json
release_safe_frame_refs.json
release_safe_model_service_describe.json
release_safe_cache_summary.json
release_safe_replay_report.json
mock_host_dispatch_report.json
```

The raw-only artefacts include:

```text
run-record.lisp
run-replay.lisp
record.stdout
record.stderr
replay.stdout
replay.stderr
model-service-cache/*.json
model-service-describe.json
frame_refs.json
request_response_cache_index.json
```

The manifest lists both sets under `release_safety`.

## example

A release-safe frame summary keeps the fixture identity and hashes but redacts the raw service ref:

```json
{
  "source_file": "fixtures/model-service/minivla-smoke/frames/example.jpg",
  "fixture_sha256": "...",
  "ref": {
    "scheme": "frame",
    "sha256": "...",
    "redacted": true
  }
}
```

A release-safe cache summary keeps deterministic request and response hashes but redacts raw request and session ids:

```json
{
  "request_hash": "fnv1a64:...",
  "sha256": "...",
  "id": {
    "sha256": "...",
    "redacted": true
  },
  "session_id": {
    "sha256": "...",
    "redacted": true
  }
}
```

## gotchas

- Release-safe does not mean secret-free in all deployments. Review the generated summaries before publication when a robot, lab, or backend setup is sensitive.
- Raw caches are still required for deterministic replay. Do not delete them from private evidence bundles just because release-safe summaries exist.
- Backend metadata is allowlisted. Public fields such as `backend`, `adapter`, `capability`, `service`, and `service_version` may appear. Placement details such as device names, model paths, and worker URLs are redacted.
- The release-safe replay report keeps the standard model-backed async evidence structure, but replaces raw `frame://` refs with hashes.
- The mock-host dispatch report contains action hashes and dimensions, not raw actuator values.
- Frame images are not redacted by this policy. Only use checked-in, curated, non-sensitive fixture frames for release evidence.

## see also

- [MiniVLA smoke evidence](minivla-smoke-evidence.md)
- [model-backed async evidence report](model-backed-async-evidence-report.md)
- [muesli-model-service bridge](../integration/model-service-bridge.md)
- [event log](../observability/event-log.md)
