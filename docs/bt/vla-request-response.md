# VLA Request/Response Schema

This page documents the structured maps used by `vla.submit` and `vla.poll`.

## Request Map (`vla.submit`)

Required fields:

- `task_id` (string)
- `instruction` (string)
- `observation` (map)
- `action_space` (map)
- `constraints` (map)
- `deadline_ms` (int)
- `model` (map)

Optional fields:

- `capability` (string)
- `seed` (int/float/string/symbol)
- `run_id` (string)
- `tick_index` (int)
- `node_name` (string)

Observation fields:

- `image` (`image_handle`, optional)
- `blob` (`blob_handle`, optional)
- `state` (number or numeric list)
- `timestamp_ms` (int)
- `frame_id` (string)

Action space fields:

- `type` (`:continuous` or string)
- `dims` (int)
- `bounds` (list of `(lo hi)`)
- `units` (optional list)
- `semantic` (optional list)

Constraints fields:

- `max_abs_value` (float)
- `max_delta` (float)
- `forbidden_ranges` (optional list of `(lo hi)`)

## Poll Map (`vla.poll`)

Top-level fields:

- `status` (`:queued :running :streaming :done :error :timeout :cancelled`)
- `partial` (optional map)
- `final` (optional map)
- `stats` (map)

`partial` fields:

- `sequence` (int)
- `confidence` (float)
- `text_chunk` (optional string)
- `action_candidate` (optional action map)

`final` fields:

- `status` (`:ok :timeout :error :cancelled :invalid`)
- `action` (action map)
- `confidence` (float)
- `explanation` (optional string)
- `model` (map)
- `stats` (map)

Action map forms:

- `{:type :continuous, :u (...)}`
- `{:type :discrete, :id "..."}`
- `{:type :sequence, :steps (...)}`

## Validation Rules

- action bounds length must match `dims`
- non-finite action values are rejected
- unsupported/missing required fields return error status
- cancelled/timeout/error states do not auto-commit actions in `vla-wait`

## See Also

- [VLA Integration In BTs](vla-integration.md)
- [VLA BT Nodes Reference](vla-nodes.md)
- [Language Reference Index](../language/reference/index.md)
