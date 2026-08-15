# air-hockey model-mediated defence

## what this is

The air-hockey example is a staged paper demonstration of
[invocation-scoped authority](../bt/invocation-scoped-authority.md). The WP2
harness proves the request, context, revocation and dispatch behaviour against
a strict local fake host. The WP3 workflow turns raw runtime and task records
into reproducible evidence bundles.

The synthetic campaign is an analysis test. It is not MuJoCo task evidence and
is never paper eligible.

## when to use it

Use the local workflow to change or review:

- air-hockey authority scenarios H1--H8;
- event, trajectory, manifest or recorded-provider schemas;
- paired integrity and obsolete-target motion summaries;
- binomial or paired-bootstrap reporting; or
- trace-derived overlay fields.

The local workflow requires no GPU, checkpoint, ACRA import or Marvin access.

## how it works

Each marked run directory contains immutable raw artefacts:

```text
manifest.json
events.jsonl
task-trajectory.jsonl
recorded-provider.jsonl
replay-events.jsonl
replay-task-trajectory.jsonl
```

The analyser checks every raw SHA-256 digest before producing validation,
summary, replay and overlay artefacts. `events.jsonl` remains the only runtime
event stream. `task-trajectory.jsonl` stores public control observations and
privileged evaluation fields in separate, schema-closed objects.

Matched pairs must share the provider response, delay schedule, seed, shot
entry and provider configuration. The acceptance policy and matched Behaviour
Tree are the treatment difference.

## api and commands

Run the self-contained local gate:

```bash
uv run --with 'jsonschema>=4.20,<5' \
  python examples/air_hockey_model_mediated_defence/run_wp3.py check
```

Generate raw synthetic bundles, then analyse them independently:

```bash
uv run --with 'jsonschema>=4.20,<5' \
  python examples/air_hockey_model_mediated_defence/run_wp3.py \
  generate-synthetic --out build/air-hockey-wp3/runs

uv run --with 'jsonschema>=4.20,<5' \
  python examples/air_hockey_model_mediated_defence/run_wp3.py \
  analyse --runs build/air-hockey-wp3/runs \
  --out build/air-hockey-wp3/analysis
```

The versioned schemas are under `schemas/air_hockey_evidence/v1/`. The primary
implementation is in
`examples/air_hockey_model_mediated_defence/analysis/evidence.py`.

## example behaviour tree

The deadline-only and invocation-scoped trees are structurally identical. The
full invocation-scoped source is embedded below.

```lisp
--8<-- "examples/air_hockey_model_mediated_defence/lisp/bt_invocation_scoped.lisp"
```

## gotchas

- Privileged scoring values may appear only inside the trajectory record's
  `privileged` object. They must not enter events or provider records.
- A recorded-provider replay requires an exact request SHA-256 match.
- `--force` replaces only a safe, marked run directory. It refuses unmarked
  data and path traversal.
- Exact binomial intervals describe observed integrity counts. A zero count is
  reported with an upper confidence bound, not as zero probability.
- SVG overlays and synthetic outcomes validate tooling only. Paper videos and
  task claims require the later MuJoCo gates.

## see also

- [air-hockey host protocol](../integration/air-hockey-host-protocol.md)
- [event log](../observability/event-log.md)
- [testing](../testing.md)
- [humanoid model-mediated approach](humanoid-model-mediated-approach.md)
