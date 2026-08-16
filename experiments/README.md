# experiments

This directory contains research campaigns that are more structured than the
runnable examples under `examples/`.

- `invocation_authority_controlled/` contains the provider-independent
  comparison of blocking, asynchronous, timeout-only and invocation-scoped
  execution.

Each campaign keeps its frozen protocol separate from generated runs. Runtime
evidence uses `mbt.evt.v1`; paper-facing summaries are derived artefacts.
