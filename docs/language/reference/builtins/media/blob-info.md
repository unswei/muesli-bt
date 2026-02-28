# `blob.info`

**Signature:** `(blob.info blob_handle) -> map`

## What It Does

Returns metadata for a blob handle.

## Arguments And Return

- Arguments: `blob_handle`
- Return: map with `id`, `size_bytes`, `mime_type`, `timestamp_ms`, `tag`

## Errors And Edge Cases

- unknown handle raises runtime error

## Examples

### Minimal

```lisp
(blob.info (blob.make 99 "text/plain" 111 "note"))
```

### Realistic

```lisp
(map.get (blob.info (blob.make 4096 "application/octet-stream" 321 "packet")) 'size_bytes 0)
```

## Notes

- Intended for metadata inspection, not payload decoding.

## See Also

- [Reference Index](../../index.md)
- [blob.make](blob-make.md)
- [VLA Request/Response Schema](../../../../bt/vla-request-response.md)
