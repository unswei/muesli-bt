# `image.make`

**Signature:** `(image.make width height channels encoding timestamp_ms frame_id) -> image_handle`

## What It Does

Creates an opaque image observation handle managed by the host runtime.

## Arguments And Return

- Arguments: integer dimensions, encoding text, timestamp, frame id
- Return: `image_handle`

## Errors And Edge Cases

- width/height/channels must be positive

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

- Handle values are GC-traced while underlying image data stays host-managed.

## See Also

- [Reference Index](../../index.md)
- [image.info](image-info.md)
- [VLA Request/Response Schema](../../../../bt/vla-request-response.md)
