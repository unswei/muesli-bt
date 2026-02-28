# `planner.mcts`

**Signature:** `(planner.mcts request-map) -> result-map`

## What It Does

Runs bounded-time MCTS using a host planner model and returns action plus diagnostics.

## Arguments And Return

- Arguments: map with at least `state`; optional planner/model fields (for example `model_service`, `budget_ms`, `iters_max`, `seed`)
- Return: map containing `action`, `status`, `confidence`, `stats` (and optional `error`)

## Errors And Edge Cases

- request must be a map
- `state` is required
- invalid field types raise runtime errors
- unknown model name returns `status :error`

## Examples

### Minimal

```lisp
(begin
  (define req (map.make))
  (map.set! req 'state 0.0)
  (planner.mcts req))
```

### Realistic

```lisp
(begin
  (define req (map.make))
  (map.set! req 'model_service "toy-1d")
  (map.set! req 'state 0.0)
  (map.set! req 'budget_ms 8)
  (map.set! req 'iters_max 600)
  (map.set! req 'seed 42)
  (planner.mcts req))
```

## Notes

- Deterministic for same request + seed.
- `status` is one of `:ok`, `:timeout`, `:noaction`, `:error`.

## See Also

- [Reference Index](../../index.md)
- [Planner Configuration](../../../../bt/planner-configuration.md)
