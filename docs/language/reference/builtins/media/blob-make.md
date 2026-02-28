# `blob.make`

**Signature:** `(blob.make size_bytes mime_type timestamp_ms tag) -> blob_handle`

## What It Does

Creates an opaque blob handle for binary payload metadata.

## Arguments And Return

- Arguments: byte size, mime type, timestamp, tag
- Return: `blob_handle`

## Errors And Edge Cases

- `size_bytes` must be non-negative

## Examples

### Minimal

```lisp
(blob.make 1024 "application/octet-stream" 1000 "snapshot")
```

### Realistic

```lisp
(begin
  (define b (blob.make 2048 "image/jpeg" 111 "frame"))
  (blob.info b))
```

## Notes

- Useful for non-image observation attachments.

## See Also

- [Reference Index](../../index.md)
- [blob.info](blob-info.md)
- [VLA Request/Response Schema](../../../../bt/vla-request-response.md)
