# contracts

This directory contains public integration contracts maintained by `muesli-bt`.

## available contracts

- [muesli-studio integration contract](muesli-studio-integration.md)
- [canonical event schema (`mbt.evt.v1`)](https://github.com/unswei/muesli-bt/blob/main/schema/mbt.evt.v1.schema.json)

## compatibility matrix

| muesli-studio compatibility profile | muesli-bt tag | schema version | deterministic mode |
| --- | --- | --- | --- |
| supports `muesli-studio` contract requirements 1-12 (`mbt.evt.v1` parser enabled) | `v0.1.0` | `mbt.evt.v1` | supported (`bt::runtime_host::enable_deterministic_test_mode(...)`) |

## related artefacts

- [deterministic fixtures (`tests/fixtures/mbt.evt.v1/`)](https://github.com/unswei/muesli-bt/tree/main/tests/fixtures/mbt.evt.v1)
- [event log validation tool (`tools/validate_event_log.py`)](https://github.com/unswei/muesli-bt/blob/main/tools/validate_event_log.py)
