# event log schema v1

This directory contains the canonical `mbt.evt.v1` JSON Schema files used by runtime-contract v1.

## files

- `mbt.evt.v1.schema.json`: canonical envelope and payload schema.

## validation

```bash
python3 tools/validate_log.py \
  --schema schemas/event_log/v1/mbt.evt.v1.schema.json \
  tests/fixtures/mbt.evt.v1/*.jsonl
```
