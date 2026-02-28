# `vla.poll`

**Signature:** `(vla.poll job_id) -> map`

## What It Does

Polls async VLA job state and returns structured partial/final payloads.

## Arguments And Return

- Arguments: positive integer `job_id`
- Return: map with `status`, optional `partial`, optional `final`, and `stats`

## Errors And Edge Cases

- invalid job id type/value raises runtime error
- unknown job ids return an error-like status payload

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
  (vla.poll job))
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
  (define (wait-final id n)
    (if (= n 0)
        (vla.poll id)
        (let ((p (vla.poll id)))
          (if (eq? (map.get p 'status ':none) ':done)
              p
              (wait-final id (- n 1))))))
  (wait-final job 32))
```

## Notes

- `status` values: `:queued`, `:running`, `:streaming`, `:done`, `:error`, `:timeout`, `:cancelled`.

## See Also

- [Reference Index](../../index.md)
- [vla.submit](vla-submit.md)
- [VLA Request/Response Schema](../../../../bt/vla-request-response.md)
