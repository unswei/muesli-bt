# compatibility policy

## what this is

This page defines compatibility and versioning rules for runtime contract artefacts.

## when to use it

Use this policy when changing:

- runtime contract docs
- event schema
- event envelope fields
- fixture formats or conformance checks
- release notes/changelog contract statements

## how it works

### version surfaces

Public compatibility surfaces:

- runtime contract id/version (`runtime-contract-v1.0.0`, `1.0.0`)
- event schema name (`mbt.evt.v1`)
- conformance fixture bundle format (`fixtures/<name>/manifest.json` contract field)

### compatibility levels

Additive change:

- new optional fields in event payloads
- new event types accepted by existing consumers

Breaking change:

- removing or renaming existing required fields
- changing existing field meaning or type
- removing previously guaranteed event ordering semantics

Breaking changes require:

- new schema version (for example `mbt.evt.v2`)
- new contract version
- matching docs + conformance + fixture updates in the same change

### release notes and changelog conventions

Contract-affecting changes must update:

- `CHANGELOG.md` under `Unreleased`
- release notes for the next tagged release

Contract-affecting entries must explicitly state:

- previous version
- new version
- compatibility impact (`additive` or `breaking`)
- migration notes for consumers

## api / syntax

Runtime and schema artefacts:

- contract docs: `docs/contracts/runtime-contract-v1.md`
- event schema: `schemas/event_log/v1/mbt.evt.v1.schema.json`
- validator: `tools/validate_log.py`
- fixture verifier: `tools/fixtures/verify_fixture.py`

## example

Additive example:

- add new event type `budget_warning` with optional payload fields
- keep `mbt.evt.v1` schema name
- update fixtures and changelog

Breaking example:

- change `tick` from integer to string
- publish `mbt.evt.v2`
- update Studio/parser compatibility matrix and release notes

## gotchas

- Changing docs only is still a contract change if it alters guarantees.
- Fixture drift without manifest updates is treated as a compatibility failure.
- Contract version and schema version are related but not identical surfaces.

## see also

- [runtime contract v1](runtime-contract-v1.md)
- [conformance levels](conformance.md)
- [contracts index](README.md)
