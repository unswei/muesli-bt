# `vla.cancel`

**Signature:** `(vla.cancel job_id) -> bool`

## What It Does

Requests cancellation of a submitted VLA job.

## Arguments And Return

- Arguments: positive integer `job_id`
- Return: boolean indicating whether cancellation was accepted

## Errors And Edge Cases

- invalid job id type/value raises runtime error
- already-terminal jobs typically return `#f`

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
  (define job (vla.submit req))
  (vla.cancel job))
```

### Realistic

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
  (define job (vla.submit req))
  (vla.cancel job)
  (vla.poll job))
```

## Notes

- Cancellation is best-effort.

## See Also

- [Reference Index](../../index.md)
- [vla.poll](vla-poll.md)
- [VLA BT Nodes](../../../../bt/vla-nodes.md)
