# `json.encode`

**Signature:** `(json.encode x) -> string`

## What It Does

Serialises Lisp data into JSON text.

## Arguments And Return

- Arguments: serialisable value (`nil`, booleans, numbers, strings/symbols, lists, maps, handles)
- Return: JSON string

## Errors And Edge Cases

- non-finite floats are rejected
- unsupported runtime types raise runtime errors

## Examples

### Minimal

```lisp
(json.encode (list 1 2 3))
```

### Realistic

```lisp
(begin
  (define m (map.make))
  (map.set! m 'task_id "demo")
  (map.set! m 'state (list 0.0 1.0))
  (json.encode m))
```

## Notes

- Map keys are emitted as JSON object string keys.

## See Also

- [Reference Index](../../index.md)
- [json.decode](json-decode.md)
