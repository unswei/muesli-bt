# `vla.submit`

**Signature:** `(vla.submit request-map) -> int`

## What It Does

Submits an async VLA job and returns a `job_id`.

## Arguments And Return

- Arguments: structured request map (task, instruction, observation, action space, constraints, model, deadline)
- Return: positive integer `job_id`

## Errors And Edge Cases

- missing required fields raise runtime errors
- invalid bounds/dims/schema types raise runtime errors
- unknown handles in observation raise runtime errors

## Examples

### Minimal

```lisp
(begin
  (define req (map.make))
  (map.set! req 'task_id "task")
  (map.set! req 'instruction "move")
  (let ((obs (map.make)))
    (map.set! obs 'state (list 0.0))
    (map.set! obs 'timestamp_ms 100)
    (map.set! obs 'frame_id "base")
    (map.set! req 'observation obs))
  (let ((space (map.make)))
    (map.set! space 'type ':continuous)
    (map.set! space 'dims 1)
    (map.set! space 'bounds (list (list -1.0 1.0)))
    (map.set! req 'action_space space))
  (let ((c (map.make)))
    (map.set! c 'max_abs_value 1.0)
    (map.set! c 'max_delta 1.0)
    (map.set! req 'constraints c))
  (let ((m (map.make)))
    (map.set! m 'name "rt2-stub")
    (map.set! m 'version "stub-1")
    (map.set! req 'model m))
  (vla.submit req))
```

### Realistic

```lisp
(begin
  (define req (map.make))
  (map.set! req 'task_id "task-with-image")
  (map.set! req 'instruction "track target")
  (map.set! req 'deadline_ms 30)
  (map.set! req 'seed 42)
  (let ((obs (map.make)))
    (map.set! obs 'image (image.make 320 240 3 "rgb8" 1000 "cam0"))
    (map.set! obs 'state (list 0.0 0.0))
    (map.set! obs 'timestamp_ms 1000)
    (map.set! obs 'frame_id "cam0")
    (map.set! req 'observation obs))
  (let ((space (map.make)))
    (map.set! space 'type ':continuous)
    (map.set! space 'dims 2)
    (map.set! space 'bounds (list (list -0.5 0.5) (list -0.5 0.5)))
    (map.set! req 'action_space space))
  (let ((c (map.make)))
    (map.set! c 'max_abs_value 0.5)
    (map.set! c 'max_delta 0.5)
    (map.set! req 'constraints c))
  (let ((m (map.make)))
    (map.set! m 'name "rt2-stub")
    (map.set! m 'version "stub-1")
    (map.set! req 'model m))
  (vla.submit req))
```

## Notes

- Use `vla.poll` to observe status and result.

## See Also

- [Reference Index](../../index.md)
- [vla.poll](vla-poll.md)
- [VLA Request/Response Schema](../../../../bt/vla-request-response.md)
