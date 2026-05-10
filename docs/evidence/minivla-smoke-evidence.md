# minivla smoke evidence

## what this is

This page describes the curated MiniVLA smoke evidence path for the optional `muesli-model-service` bridge.

The run proves that `muesli-bt` can submit `cap.vla.action_chunk.v1` VLA sessions through the model-service bridge, carry `frame://camera1/...` refs, record request-hash keyed model-service responses, replay the same sessions from cache, and report host-safe action proposals.

## when to use it

Use this evidence path when checking the real MiniVLA backend or preparing a release evidence bundle for model-backed VLA behaviour.

This is a gated integration run. It requires a reachable `muesli-model-service` with the MiniVLA backend loaded. It is not part of the default unit test suite.

## how it works

The runner uses checked-in Webots robot-camera frames from `fixtures/model-service/minivla-smoke/frames/`.

For each frame, the runner:

- verifies the fixture SHA-256 hash
- uploads the JPEG to `muesli-model-service` with HTTP frame ingest
- records the immutable `frame://camera1/...` ref returned by the service
- runs `vla.submit` through `muesli-bt` and the optional model-service bridge
- writes canonical `events.jsonl`
- writes request-hash keyed response cache files under `model-service-cache/`
- runs a replay pass using the recorded cache
- writes `replay_report.json` with request hash, response hash, frame ref, replay hit, validation, and action-parity checks
- writes release-safe sidecars that redact prompts, raw frame refs, backend placement metadata, request ids, session ids, and raw cache envelopes

The model output remains a proposal. The evidence report marks accepted outputs as host-safe proposals and keeps `host_reached=false`.

## api / syntax

Build the bridge-enabled executable first:

```bash
cmake -S . -B build/model-service-bridge-test \
  -DMUESLI_BT_BUILD_MODEL_SERVICE_BRIDGE=ON
cmake --build build/model-service-bridge-test --target muslisp
```

Start or tunnel to a MiniVLA-backed model service. For the current remote service pattern:

```bash
ssh -L 8765:127.0.0.1:8765 magrathea
curl http://127.0.0.1:8765/health
```

Run the curated evidence path:

```bash
python3 tools/evidence/run_minivla_smoke.py \
  --http-endpoint http://127.0.0.1:8765 \
  --ws-endpoint ws://127.0.0.1:8765/v1/ws
```

If port `8765` is already in use locally, use another tunnel port and pass matching endpoints:

```bash
ssh -L 18765:127.0.0.1:8765 magrathea
python3 tools/evidence/run_minivla_smoke.py \
  --http-endpoint http://127.0.0.1:18765 \
  --ws-endpoint ws://127.0.0.1:18765/v1/ws
```

## example

The runner writes a timestamped directory under `build/evidence/`, for example:

```text
build/evidence/minivla-smoke-YYYYMMDDTHHMMSSZ/
```

Expected artefacts:

- `events.jsonl`
- `replay-events.jsonl`
- `manifest.json`
- `replay_report.json`
- `frame_refs.json`
- `record-results.jsonl`
- `replay-results.jsonl`
- `request_response_cache_index.json`
- `model-service-cache/*.json`
- `redaction_policy.json`
- `release_safe_prompt_summary.json`
- `release_safe_frame_refs.json`
- `release_safe_model_service_describe.json`
- `release_safe_cache_summary.json`
- `release_safe_replay_report.json`
- copied input frames under `frames/`

The fixture images are in:

```text
fixtures/model-service/minivla-smoke/
```

## gotchas

- Do not use ad hoc local images for release evidence. Use the checked-in Webots frames or regenerate a documented replacement.
- Do not use `frame://camera1/latest` in release evidence. The bundle must use immutable frame refs returned by HTTP ingest.
- Use the `release_safe_*` artefacts for public evidence. Keep raw model-service caches, raw `frame://` refs, generated scripts, and stdout/stderr private unless they have been reviewed.
- `events.jsonl` from this smoke path is canonical event JSONL, but it is a VLA API smoke run rather than a full BT tick trace.
- The evidence directory under `build/` is an artefact. Commit the fixture and runner, not the generated cache bundle.

## see also

- [muesli-model-service bridge](../integration/model-service-bridge.md)
- [release-safe redaction policy](redaction-policy.md)
- [VLA backend integration plan](../integration/vla-backend-integration-plan.md)
- [VLA logging](../observability/vla-logging.md)
