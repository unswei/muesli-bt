# host capability BT example

## what this is

This page shows the intended BT boundary for host capability bundles.

The example is Lisp-shaped pseudocode.
It is not a released runtime API and it is not expected to run today.

The important flow is:

1. ask perception for normalised scene state
2. choose a symbolic next step from that scene state
3. ask motion to execute the step

## when to use it

Use this page when you want to review whether a future host capability API keeps BT logic separate from adapter internals.

The BT should see stable scene facts and motion requests.
It should not see detector tensors, MoveIt messages, ROS actions, simulator oracles, or robot SDK handles.

## how it works

The host owns two capability adapters:

- `cap.perception.scene.v1` provides normalised objects and facts
- `cap.motion.v1` executes or validates a generic motion request

The BT owns the task policy:

- decide whether the scene is fresh enough
- choose the next symbolic task step
- request motion for that step
- branch on stable status values

## api / syntax

The pseudocode below uses a placeholder `(cap.call request-map)` form.
That form is illustrative only.
A later implementation may use a different built-in name or host callback shape.

```lisp
(define (get-current-scene)
  (cap.call
    (map.make
      'schema_version "cap.perception.scene.request.v1"
      'capability "cap.perception.scene.v1"
      'operation "get-scene"
      'request_id "scene-for-next-step"
      'frame "world"
      'max_age_ms 100
      'timeout_ms 80)))

(define (choose-next-step scene)
  ;; Task policy reads normalised facts and object ids.
  ;; It does not inspect detector boxes, logits, masks, or middleware messages.
  (cond
    ((scene-fact? scene "clear" (list "peg-b"))
      (map.make
        'kind "move-object"
        'object_id "disc-red"
        'target_pose
          (map.make
            'frame "world"
            'position (map.make 'x 0.40 'y 0.10 'z 0.25)
            'orientation (map.make 'qx 0.0 'qy 0.0 'qz 0.0 'qw 1.0))))
    (else
      (map.make 'kind "wait"))))

(define (execute-step step)
  (if (eq? (map.get step 'kind) "move-object")
    (cap.call
      (map.make
        'schema_version "cap.motion.request.v1"
        'capability "cap.motion.v1"
        'operation "move-to-pose"
        'request_id "execute-next-step"
        'group "arm"
        'link "tool0"
        'target (map.get step 'target_pose)
        'timeout_ms 500))
    (map.make
      'schema_version "cap.motion.result.v1"
      'capability "cap.motion.v1"
      'operation "noop"
      'request_id "execute-next-step"
      'status :ok)))

(defbt capability-boundary-demo
  (seq
    (act refresh-scene get-current-scene scene-result)
    (cond scene-ok scene-result)
    (act choose-step choose-next-step scene-result next-step)
    (act execute-symbolic-step execute-step next-step motion-result)
    (cond motion-ok motion-result)))
```

## example

In this shape, perception and motion are host services.
The BT still owns the decision boundary.

The BT reads:

- `status` values such as `:ok`, `:stale`, `:not-found`, `:timeout`, or `:unavailable`
- normalised object ids and facts
- normalised poses, frames, and confidence values

The BT does not read:

- YOLO class logits
- image-space detector boxes
- segmentation masks
- MoveIt action payloads
- ROS message objects
- simulator-private state handles

## gotchas

- This is not a commitment to the built-in name `(cap.call ...)`.
- A real implementation must define how request ids, cancellation, and event logging work.
- A real implementation must emit relevant canonical events if host capability calls affect runtime behaviour.
- Keep task policy in the BT and adapter mechanics in the host.

## see also

- [host capability bundles](host-capability-bundles.md)
- [cap.perception.scene.v1](cap-perception-scene-v1.md)
- [cap.motion.v1](cap-motion-v1.md)
- [planner.plan request/result](../planning/planner-plan.md)
