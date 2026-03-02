# `events.snapshot-bb`

**Signature:** `(events.snapshot-bb [full?]) -> nil`

Request a `bb_snapshot` event at the next tick boundary.

- default: bounded snapshot
- with `#t`: full snapshot
