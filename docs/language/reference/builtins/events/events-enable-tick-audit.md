# `events.enable-tick-audit`

**Signature:** `(events.enable-tick-audit enabled?) -> nil`

Enable or disable the per-tick `tick_audit` canonical event.

When enabled, each `bt.tick` emits one `tick_audit` event after `tick_end`.
The event uses the `tick_audit.v1` payload described in [tick audit record](../../../../observability/tick-audit.md).

