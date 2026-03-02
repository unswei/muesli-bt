# contracts

This directory contains public integration contracts maintained by `muesli-bt`.

## available contracts

- [runtime contract v1](runtime-contract-v1.md)
- [muesli-studio integration contract](muesli-studio-integration.md)
- [compatibility policy](compatibility.md)
- [conformance levels](conformance.md)
- [canonical event schema (`mbt.evt.v1`)](https://github.com/unswei/muesli-bt/blob/main/schemas/event_log/v1/mbt.evt.v1.schema.json)

## compatibility matrix

| contract profile | muesli-bt tag | schema version | runtime contract version | deterministic mode |
| --- | --- | --- | --- | --- |
| supports runtime contract v1 + studio integration requirements (`mbt.evt.v1` parser enabled) | `v0.1.0+` | `mbt.evt.v1` | `1.0.0` | supported (`bt::runtime_host::enable_deterministic_test_mode(...)`) |

## related artefacts

- [deterministic fixtures (`tests/fixtures/mbt.evt.v1/`)](https://github.com/unswei/muesli-bt/tree/main/tests/fixtures/mbt.evt.v1)
- [contract fixture bundles (`fixtures/`)](https://github.com/unswei/muesli-bt/tree/main/fixtures)
- [event log validation tool (`tools/validate_log.py`)](https://github.com/unswei/muesli-bt/blob/main/tools/validate_log.py)
