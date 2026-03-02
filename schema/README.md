# schema

This directory stores authoritative, versioned schemas for public `muesli-bt` interfaces.

## versioning rules

- `mbt.evt.v1` is additive-first.
- Any breaking change requires a new schema version (for example `mbt.evt.v2`).
- Contract or schema updates must update `CHANGELOG.md` in the same change.
- Deterministic fixtures under `tests/fixtures/mbt.evt.v1/` must be regenerated and committed when schema-relevant behaviour changes.

## validate logs

Use the in-repo validator:

```bash
python3 tools/validate_event_log.py \
  --schema schema/mbt.evt.v1.schema.json \
  tests/fixtures/mbt.evt.v1/*.jsonl
```

The validator fails on:

- malformed JSON lines
- schema mismatches
- unknown `schema` versions in envelope fields
