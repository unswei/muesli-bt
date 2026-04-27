# release notes

## what this is

This section keeps per-tag release notes that summarise stable surfaces, contract compatibility, and migration notes for released versions of `muesli-bt`.

The `v0.7.0` page is currently marked as in preparation on `main`; treat it as release-candidate documentation until the tag is cut.

## when to use it

Use these pages when you:

- pin a released runtime version
- check compatibility notes for tool builders and downstream consumers
- review what was intentionally stable at a given tag

## how it works

Each tagged release note should record:

- release date
- stable surfaces for that tag
- compatibility notes for runtime contract and event schema
- migration notes for consumers where relevant

The changelog stays cumulative. Release notes stay tag-specific.

## api / syntax

Available release notes:

- [v0.7.0](v0.7.0.md)
- [v0.6.0](v0.6.0.md)
- [v0.5.0](v0.5.0.md)
- [v0.4.0](v0.4.0.md)
- [v0.3.1](v0.3.1.md)
- [v0.3.0](v0.3.0.md)
- [v0.2.0](v0.2.0.md)
- [v0.1.0](v0.1.0.md)

## example

If you are deciding which runtime tag to pin in `muesli-studio`, use the release note for that tag together with the [studio compatibility matrix](../contracts/studio-compatibility-matrix.md).

## gotchas

- Release notes do not replace the changelog.
- Rolling `main` changes belong in `CHANGELOG.md`, not in a future release note page.
- Pages marked "in preparation" describe release-candidate state, not a tagged release.
- Contract-affecting release notes should stay aligned with the compatibility policy.

## see also

- [changelog](../changelog.md)
- [compatibility policy](../contracts/compatibility.md)
- [studio compatibility matrix](../contracts/studio-compatibility-matrix.md)
