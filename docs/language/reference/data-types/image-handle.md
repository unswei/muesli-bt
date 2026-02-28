# `image_handle`

**Signature:** `image_handle handle`

## What It Does

Opaque handle for host-managed image observations.

## Arguments And Return

- Return: `image_handle` value from `image.make`

## Errors And Edge Cases

- Handle is opaque; use `image.info` for metadata.

## Examples

### Minimal

```lisp
(image.make 320 240 3 "rgb8" 1000 "cam0")
```

### Realistic

```lisp
(begin
  (define img (image.make 640 480 3 "rgb8" 1234 "front"))
  (image.info img))
```

## Notes

- Designed to avoid copying large image buffers into Lisp lists.

## See Also

- [Reference Index](../index.md)
- [VLA Request/Response Schema](../../../bt/vla-request-response.md)
