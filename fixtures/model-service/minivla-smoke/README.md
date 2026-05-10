# MiniVLA smoke frames

## what this is

This fixture contains small Webots-generated robot-camera frames for MiniVLA model-service smoke and replay evidence.

## when to use it

Use these frames when a model-service evidence run needs redistributable `camera1` images instead of ad hoc local JPGs.

## how it works

The frames were captured from the checked-in Webots e-puck cluttered-goal world with a plain synthetic sky and a temporary capture controller. The controller saved images from the e-puck `camera` device at `224x224`.

## api / syntax

Use each file as an HTTP frame-ingest body for the model service:

```bash
curl -X PUT \
  -H 'Content-Type: image/jpeg' \
  --data-binary @fixtures/model-service/minivla-smoke/frames/webots-epuck-goal-start.jpg \
  http://127.0.0.1:8765/v1/frames/camera1
```

## example

The returned immutable `frame://camera1/...` ref should be passed into the VLA request observation.

## gotchas

- Do not use `frame://camera1/latest` in release evidence. Use the immutable ref returned by frame ingest.
- Keep `manifest.json` hashes in sync if frames are regenerated.

## see also

- `docs/integration/model-service-bridge.md`
- `docs/integration/vla-backend-integration-plan.md`
