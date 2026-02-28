# VLA BT Nodes Reference

## `vla-request`

**Signature:** `(vla-request key value key value ...)`

Submits a VLA job when no in-flight job id is present at `:job_key`.

Common keys:

- `:name` (string)
- `:job_key` (symbol/string)
- `:instruction` or `:instruction_key`
- `:task_id` or `:task_key`
- `:state_key`
- `:image_key` (optional)
- `:blob_key` (optional)
- `:deadline_ms`
- `:dims`, `:bound_lo`, `:bound_hi`
- `:max_abs`, `:max_delta`
- `:seed` or `:seed_key`
- `:model_name`, `:model_version`
- `:capability`

Return semantics:

- `running` after submit
- `running` while job id already exists
- `failure` on invalid inputs/config

## `vla-wait`

**Signature:** `(vla-wait key value key value ...)`

Polls a submitted VLA job and optionally commits early streaming candidates.

Common keys:

- `:name`
- `:job_key`
- `:action_key`
- `:meta_key` (optional JSON summary)
- `:early_commit` (bool)
- `:early_confidence` (float)
- `:cancel_on_early_commit` (bool)
- `:clear_job` (bool)

Return semantics:

- `success` when a valid final action is committed
- `success` on valid early-commit candidate (if enabled)
- `running` while job is queued/running/streaming
- `failure` on timeout/error/cancel/invalid

## `vla-cancel`

**Signature:** `(vla-cancel key value key value ...)`

Cancels an in-flight job id at `:job_key` and clears that key.

Common keys:

- `:name`
- `:job_key`

Return semantics:

- `success` when cancelled or nothing to cancel
- `failure` only when service/config is invalid

## See Also

- [VLA Integration In BTs](vla-integration.md)
- [BT Semantics](semantics.md)
- [VLA Logging Schema](../observability/vla-logging.md)
