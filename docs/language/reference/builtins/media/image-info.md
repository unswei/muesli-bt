# `image.info`

**Signature:** `(image.info image_handle) -> map`

## What It Does

Returns metadata for an image handle.

## Arguments And Return

- Arguments: `image_handle`
- Return: map with `id`, `w`, `h`, `channels`, `encoding`, `timestamp_ms`, `frame_id`

## Errors And Edge Cases

- unknown handle raises runtime error

## Examples

### Minimal

```lisp
(begin
  (define img (image.make 10 20 3 "rgb8" 100 "cam"))
  (image.info img))
```

### Realistic

```lisp
(map.get (image.info (image.make 640 480 3 "rgb8" 1234 "front")) 'encoding "")
```

## Notes

- Metadata is small and safe to log.

## See Also

- [Reference Index](../../index.md)
- [image.make](image-make.md)
- [VLA Request/Response Schema](../../../../bt/vla-request-response.md)
