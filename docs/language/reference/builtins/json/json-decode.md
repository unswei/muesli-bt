# `json.decode`

**Signature:** `(json.decode s) -> any`

## What It Does

Parses JSON text into Lisp values.

## Arguments And Return

- Arguments: JSON string
- Return: decoded value (`map`, list, string, number, boolean, or `nil`)

## Errors And Edge Cases

- invalid JSON raises runtime errors
- decoded object keys are stored as string map keys

## Examples

### Minimal

```lisp
(json.decode "[1,2,3]")
```

### Realistic

```lisp
(begin
  (define payload "{\"task\":\"demo\",\"ok\":true}")
  (map.get (json.decode payload) "task" ""))
```

## Notes

- Arrays decode to proper Lisp lists.

## See Also

- [Reference Index](../../index.md)
- [json.encode](json-encode.md)
