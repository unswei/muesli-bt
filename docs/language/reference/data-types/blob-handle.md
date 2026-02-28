# `blob_handle`

**Signature:** `blob_handle handle`

## What It Does

Opaque handle for host-managed binary payloads.

## Arguments And Return

- Return: `blob_handle` value from `blob.make`

## Errors And Edge Cases

- Handle is opaque; use `blob.info` for metadata.

## Examples

### Minimal

```lisp
(blob.make 1024 "application/octet-stream" 1000 "snapshot")
```

### Realistic

```lisp
(begin
  (define b (blob.make 2048 "image/jpeg" 555 "frame"))
  (blob.info b))
```

## Notes

- Useful for large payload transport through capability requests.

## See Also

- [Reference Index](../index.md)
- [VLA Request/Response Schema](../../../bt/vla-request-response.md)
