# cap.perception.scene.v1

## what this is

`cap.perception.scene.v1` is the host capability bundle contract for host-owned scene perception.

It exposes normalised scene state to BT logic.
The contract is for objects, facts, confidence, timestamps, and frames after the host has processed sensor data.

Detector outputs are adapter internals.
A YOLO-compatible detector, segmentation model, simulator oracle, fiducial tracker, or robot SDK may sit behind this contract, but BT logic should not depend on detector-specific classes, tensors, bounding boxes, topics, or message types.

## when to use it

Use `cap.perception.scene.v1` when BT logic needs higher-level scene state rather than raw observations.

Good uses:

- query known objects in the current scene
- fetch a normalised pose for a named or typed object
- inspect symbolic facts such as `on`, `clear`, `held`, or `visible`
- check confidence and freshness before acting on perception
- ask the host to refresh scene state under a bounded timeout

Do not use `cap.perception.scene.v1` for:

- direct camera, lidar, odometry, or proprioception reads; use `env.observe`
- low-level action or control output; use `env.act`
- planner action selection; use `planner.plan`
- raw detector outputs such as class logits, mask tensors, bounding boxes, or middleware messages

## how it works

A perception adapter owns the platform-specific sensing and model pipeline.
The adapter may call a detector, a simulator state service, a tracking stack, or a hand-written test fixture.

The adapter returns normalised scene state with:

- object records
- symbolic facts
- confidence values
- timestamps and freshness metadata
- coordinate frames
- adapter metadata that BT logic can ignore

The host is responsible for converting raw detections into stable object ids, labels, poses, facts, and confidence values.
BT logic should branch on the normalised contract, not on detector internals.

## api / syntax

This page specifies the contract shape for `cap.perception.scene.v1`.
It does not add a released Lisp built-in by itself.

Future APIs should pass these maps through the host capability bundle mechanism.
Until that mechanism exists, examples on this page are contract examples rather than executable snippets.

### common request fields

Required:

- `schema_version`: `"cap.perception.scene.request.v1"`
- `capability`: `"cap.perception.scene.v1"`
- `operation`: one of the operations listed below
- `request_id`: stable string chosen by the caller or host wrapper

Common optional:

- `timeout_ms`: wall-clock timeout for host-owned work
- `frame`: preferred output frame
- `at_time_ms`: requested scene time when the adapter supports history
- `max_age_ms`: maximum acceptable age for cached scene state
- `query`: operation-specific filter map
- `metadata`: map for caller-owned annotations

### common result fields

Required:

- `schema_version`: `"cap.perception.scene.result.v1"`
- `capability`: `"cap.perception.scene.v1"`
- `operation`: operation handled by the adapter
- `request_id`: request id from the matching request
- `status`: stable status keyword

Common optional:

- `scene`: scene-state map
- `time_used_ms`: elapsed host time
- `adapter`: concrete adapter name, such as `"yolo-scene-normaliser"`, `"sim-scene"`, or `"mock"`
- `adapter_schema`: adapter-specific metadata schema id
- `source_time_ms`: timestamp of the sensor/model input used for the scene
- `scene_time_ms`: timestamp assigned to the normalised scene state
- `error`: short diagnostic string for human debugging

### status values

Common host capability status values:

- `:ok`: operation completed successfully
- `:accepted`: asynchronous operation was accepted and returned a `job_id`
- `:running`: asynchronous operation is still in progress
- `:cancelled`: operation was cancelled before completion
- `:timeout`: operation exceeded its timeout, budget, or freshness bound
- `:rejected`: adapter rejected the request before execution
- `:error`: adapter failed for an implementation-specific reason
- `:unavailable`: no suitable adapter is available

Perception-specific status values:

- `:stale`: scene state exists but is older than `max_age_ms`
- `:not-found`: requested object or fact is absent
- `:low-confidence`: scene state exists but confidence is below the requested threshold

Adapters may include extra diagnostic fields, but BT logic should branch only on stable status values.

## scene state

A scene-state map should contain:

- `schema_version`: `"cap.perception.scene.state.v1"`
- `scene_id`: host-defined id for this scene snapshot
- `frame`: default coordinate frame for poses
- `scene_time_ms`: timestamp assigned to the normalised scene state
- `source_time_ms`: timestamp of the sensor/model input used for the scene
- `objects`: list of normalised object maps
- `facts`: list of normalised fact maps
- `confidence`: confidence for the scene as a whole, from `0.0` to `1.0`

Timestamps are milliseconds.
The host must document whether timestamps are wall time, simulation time, or another clock.

## normalised objects

An object map should contain:

- `object_id`: stable id within the current scene or tracking context
- `label`: semantic label such as `"disc"`, `"peg"`, `"cup"`, or `"target"`
- `confidence`: object confidence from `0.0` to `1.0`
- `frame`: frame for the pose
- `pose`: pose map when pose is known
- `observed_time_ms`: timestamp of the observation used for this object

Optional fields:

- `track_id`: stable id across scene snapshots when tracking is available
- `attributes`: map of normalised attributes such as colour, size class, or state
- `extent`: normalised 3D size or footprint map when available
- `source`: short source label such as `"camera-front"` or `"sim-state"`

Pose map fields:

- `position`: map with `x`, `y`, and `z`
- `orientation`: map with `qx`, `qy`, `qz`, and `qw`

If an adapter can only provide 2D image-space evidence, it should either normalise that evidence into a documented scene attribute or omit pose.
It should not expose raw detector boxes as the core object contract.

## normalised facts

A fact map should contain:

- `predicate`: symbolic predicate name
- `args`: list of object ids or literal values
- `confidence`: fact confidence from `0.0` to `1.0`
- `observed_time_ms`: timestamp of the evidence used for this fact

Examples:

- `predicate`: `"on"`, `args`: `("disc-red" "peg-a")`
- `predicate`: `"clear"`, `args`: `("peg-b")`
- `predicate`: `"held"`, `args`: `("disc-blue")`
- `predicate`: `"visible"`, `args`: `("target-1")`

Facts should be normalised enough that BT logic does not need detector-specific post-processing.

## operations

### `get-scene`

Return the current normalised scene state.

Common optional request fields:

- `frame`
- `max_age_ms`
- `timeout_ms`

### `query-objects`

Return objects that match a query.

Common query fields:

- `label`
- `object_id`
- `track_id`
- `min_confidence`
- `attributes`

The result should return `:not-found` when no object matches.

### `query-facts`

Return facts that match a query.

Common query fields:

- `predicate`
- `args`
- `min_confidence`

The result should return `:not-found` when no fact matches.

### `refresh`

Ask the host to refresh scene state under a bounded timeout.

The host may run a detector, pull simulator state, update tracking, or reuse a cache if the cache satisfies `max_age_ms`.
The result should include `scene_time_ms` and `source_time_ms` so consumers can judge freshness.

## example

Illustrative `get-scene` request:

```lisp
(map.make
  'schema_version "cap.perception.scene.request.v1"
  'capability "cap.perception.scene.v1"
  'operation "get-scene"
  'request_id "scene-001"
  'frame "world"
  'max_age_ms 100
  'timeout_ms 80)
```

Illustrative result:

```lisp
(map.make
  'schema_version "cap.perception.scene.result.v1"
  'capability "cap.perception.scene.v1"
  'operation "get-scene"
  'request_id "scene-001"
  'status :ok
  'adapter "yolo-scene-normaliser"
  'adapter_schema "cap.perception.scene.yolo.adapter.v1"
  'scene
    (map.make
      'schema_version "cap.perception.scene.state.v1"
      'scene_id "scene-001"
      'frame "world"
      'scene_time_ms 1711000000123
      'source_time_ms 1711000000105
      'confidence 0.91
      'objects
        (list
          (map.make
            'object_id "disc-red"
            'label "disc"
            'confidence 0.94
            'frame "world"
            'pose
              (map.make
                'position (map.make 'x 0.25 'y 0.10 'z 0.03)
                'orientation (map.make 'qx 0.0 'qy 0.0 'qz 0.0 'qw 1.0))
            'observed_time_ms 1711000000105
            'attributes (map.make 'colour "red")))
      'facts
        (list
          (map.make
            'predicate "on"
            'args (list "disc-red" "peg-a")
            'confidence 0.88
            'observed_time_ms 1711000000105))))
  'time_used_ms 32)
```

## gotchas

- `cap.perception.scene.v1` does not guarantee that any specific detector is present.
- Detector labels, logits, boxes, masks, and middleware messages are adapter internals.
- Adapter names are metadata, not BT semantics.
- Confidence must use `0.0` to `1.0` so BT thresholds stay portable.
- Frames and timestamp sources must be explicit.
- Cached scene state must report freshness through `scene_time_ms`, `source_time_ms`, and status such as `:stale`.
- Hidden simulator oracle state is acceptable for tests only when the adapter reports that source clearly.

## see also

- [host capability bundles](host-capability-bundles.md)
- [cap.motion.v1](cap-motion-v1.md)
- [environment api (`env.*`)](env-api.md)
- [planner.plan request/result](../planning/planner-plan.md)
- [ROS2 backend scope](ros2-backend-scope.md)
- [roadmap to 1.0](../roadmap-to-1.0.md)
