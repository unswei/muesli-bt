# muesli-model-service bridge

## what this is

The `muesli-model-service` bridge is the optional model-capability boundary between `muesli-bt` and an external model service.

`muesli-bt` keeps the BT runtime, validation, replay, cancellation, fallback, and host dispatch rules. `muesli-model-service` hosts heavyweight model backends and returns proposals.

The bridge targets `MMSP v0.2`, the first service protocol version aligned with the `muesli-bt` runtime contract.

## when to use it

Use the bridge when a BT needs model-mediated capabilities such as:

- `cap.model.world.rollout.v1`
- `cap.model.world.score_trajectory.v1`
- `cap.vla.action_chunk.v1`
- `cap.vla.propose_nav_goal.v1`

Do not use the bridge for direct robot commands. Model outputs are untrusted proposals until `muesli-bt` validates them.

## how it works

The bridge is optional. `muesli-bt` builds and runs without `muesli-model-service` unless a host explicitly configures model capabilities.

The initial C++ surface is a small `model_service_client` abstraction. It mirrors the `MMSP v0.2` operations:

- `describe`
- `invoke`
- `start`
- `step`
- `cancel`
- `status`
- `close`

Stateless world-model calls are intended to flow through `cap.call`. Session-based VLA calls initially reuse `vla.submit`, `vla.poll`, and `vla.cancel` while carrying generic capability names. A later generic async capability API can replace the `vla.*` compatibility surface if that becomes cleaner.

## api / syntax

`MMSP v0.2` requests use top-level capability fields:

```json
{
  "version": "0.2",
  "id": "req-000001",
  "op": "invoke",
  "capability": "cap.model.world.rollout.v1",
  "deadline_ms": 100,
  "input": {},
  "refs": [],
  "replay": {
    "mode": "live"
  }
}
```

Responses use top-level output and session fields:

```json
{
  "version": "0.2",
  "id": "req-000001",
  "status": "success",
  "output": {},
  "session_id": null,
  "error": null,
  "metadata": {
    "service": "muesli-model-service",
    "service_version": "0.2.0",
    "capability": "cap.model.world.rollout.v1",
    "backend": "mock-world-model"
  }
}
```

The optional build switch is:

```bash
cmake -S . -B build/model-service \
  -DMUESLI_BT_BUILD_MODEL_SERVICE_BRIDGE=ON
```

The default is `OFF`, so core users do not inherit a model-service or networking dependency.

The `muslisp` command can also start the external service in the foreground:

```bash
MUESLI_MODEL_SERVICE_DIR=/Users/oliver/Code/2026/muesli-model-service \
  ./build/core-model-service-test/muslisp --model-service-start
```

Equivalent explicit form:

```bash
./build/core-model-service-test/muslisp --model-service-start \
  --model-service-dir /Users/oliver/Code/2026/muesli-model-service \
  --host 127.0.0.1 \
  --port 8765
```

Discovery order is:

- `--model-service-dir`
- `MUESLI_MODEL_SERVICE_DIR`
- sibling checkout at `../muesli-model-service`
- `muesli-model-service` executable already on `PATH`

Use `--dry-run` to print the command without starting the service.

Robot or simulator cameras should publish frames to the service separately from model calls:

```bash
curl -X PUT http://127.0.0.1:8765/v1/frames/camera1 \
  -H 'Content-Type: image/jpeg' \
  -H 'X-MMS-Timestamp-Ns: 1730000000000000000' \
  --data-binary @camera1.jpg
```

The response includes both an immutable ref such as `frame://camera1/1730000000000000000`
and `frame://camera1/latest`. The model call should use refs:

```json
{
  "version": "0.2",
  "id": "vla-1",
  "op": "start",
  "capability": "cap.vla.action_chunk.v1",
  "deadline_ms": 100,
  "input": {
    "instruction": "move forward slowly",
    "observation": {
      "state": [0.0, 0.0, 0.0],
      "images": {
        "camera1": { "ref": "frame://camera1/latest" },
        "camera2": { "ref": "frame://camera2/latest" },
        "camera3": { "ref": "frame://camera3/latest" }
      }
    }
  }
}
```

## example

A bounded world-model request should use a stable capability id:

```lisp
(begin
  (define req (map.make))
  (map.set! req 'schema_version "cap.model.world.rollout.request.v1")
  (map.set! req 'capability "cap.model.world.rollout.v1")
  (map.set! req 'operation "invoke")
  (map.set! req 'deadline_ms 100)
  (cap.call req))
```

The host bridge maps the request to `MMSP v0.2`, validates the response, emits canonical events, and only then allows downstream host execution.

## gotchas

- Missing service configuration means no model-service capability should be required.
- `--model-service-start` is a convenience wrapper. It does not make the Python service part of the C++ runtime.
- Configured but unreachable service must produce fallback-capable unavailable results.
- `frame://.../latest` is a handle in the service frame cache. It does not itself transport bytes.
- Backend metadata is not BT semantics.
- Late, stale, invalid, unsafe, or policy-violating outputs must have `host_reached=false`.
- A `step` timeout does not automatically cancel a service session.

## see also

- [VLA integration](../bt/vla-integration.md)
- [VLA backend integration plan](vla-backend-integration-plan.md)
- [Host capability bundles](host-capability-bundles.md)
- [cap.call](../language/reference/builtins/cap/cap-call.md)
- [v1.0 direction](../project/v1-direction.md)
