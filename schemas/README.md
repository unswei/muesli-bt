# schemas

Authoritative schemas for versioned runtime contracts live under this directory.

- Event log schema v1: `schemas/event_log/v1/mbt.evt.v1.schema.json`

Schema validation only checks per-record shape.
For cross-event runtime-contract checks, use `tools/validate_trace.py`.
