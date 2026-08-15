# air-hockey model-mediated defence

This example is the staged integration for the muesli paper's dynamic
air-hockey demonstration. WP1 provides the versioned local protocol and pure
fake host. WP2 adds the C++ `env_backend`, matched Behaviour Trees, proposal
validator, dispatch gate and deterministic H1--H8 harness. WP3 adds the local
evidence-bundle, paired-analysis, replay and overlay tooling. WP4 pins the ACRA
source and container, defines the joint image and packages provider adapters.

The fake host is useful for protocol, lifecycle and information-boundary tests.
It is not a physics simulator and its observations must not be used as task
evidence.

## wp2 matrix

The two checked-in BTs differ only at `:acceptance_policy`. Each waiting path
authors the current mallet position as fallback before polling the provider. A
proposal can replace that pending fallback only after the runtime commit gate
and the example dispatch gate accept it.

| Trial | Intervention | Expected result |
| --- | --- | --- |
| H1 | Timely current result | Commit and dispatch once. |
| H2a | Context change, deadline-only baseline | Admit and dispatch one bounded obsolete action in the fake host. |
| H2b | Same context change, invocation-scoped | Reject with `context_changed`; no obsolete dispatch. |
| H3 | Replacement request | Revoke generation one; dispatch generation two. |
| H4 | Context change after commit | Reject at the dispatch gate before any host capability call. |
| H5 | Defence branch exit | Revoke authority and drop the late completion. |
| H6 | Completion after 120 ms, both policies | Reject with `deadline_expired`; retain fallback. |
| H7 | Duplicate terminal polling and dispatch | Record one accepted decision and one host call; reject both duplicates. |
| H8 | Recorded-provider replay | Consume the cached response and reproduce the decision, dispatch and applied mallet-state projection without live inference. |

H2a is a deliberate research baseline and runs only against the bounded fake
host. Every invocation-scoped row records zero accepted obsolete dispatches.

Build and run Gate G2 from the repository root:

```bash
cmake -S . -B build/dev
cmake --build build/dev --target muesli_bt_air_hockey_scenario_tests -j
uv run --with 'jsonschema>=4.20,<5' \
  python examples/air_hockey_model_mediated_defence/run_g2.py \
  --runner build/dev/muesli_bt_air_hockey_scenario_tests
```

The runner starts a fresh mode-`0600` fake-host socket for every scenario. It
checks every predicate declared in `evidence/g2_predicates.json` and verifies
that the matched BT sources have no structural drift.

## wp3 analysis and reproducibility

Run the complete local Gate G3 check with:

```bash
uv run --with 'jsonschema>=4.20,<5' \
  python examples/air_hockey_model_mediated_defence/run_wp3.py check
```

The check generates eight synthetic runs in four matched pairs inside a
temporary directory. It validates immutable raw artefact hashes, the public
and privileged field boundary, manifests, canonical events, cross-event traces
and recorded-provider replay. It then regenerates integrity counts,
obsolete-target motion, exact binomial intervals, paired bootstrap intervals,
table rows, plot fields, overlay timelines and SVG overlays.

To exercise the two-stage workflow explicitly:

```bash
uv run --with 'jsonschema>=4.20,<5' \
  python examples/air_hockey_model_mediated_defence/run_wp3.py \
  generate-synthetic --out build/air-hockey-wp3/runs
uv run --with 'jsonschema>=4.20,<5' \
  python examples/air_hockey_model_mediated_defence/run_wp3.py \
  analyse --runs build/air-hockey-wp3/runs \
  --out build/air-hockey-wp3/analysis
```

`--force` replaces only basename-safe run directories carrying the
`.air-hockey-evidence-run` marker. It refuses an unmarked destination. The
synthetic outcomes exercise analysis code only and are never paper evidence.

## wp4 integration packaging

`container/wp4.lock.json` pins ACRA commit
`1b6bbbbf19743b0042f01eabf0628eba5621cacf` and the audited base-image digest.
The learned-provider checkpoint remains explicitly unresolved until the ACRA
freeze. The joint Dockerfile consumes Git archives, not mutable working trees.
It also installs the exact Ubuntu CMake, C++ and Make package versions required
to build the muesli runner because the simulator base image has no compiler.

Run every non-MuJoCo startup, schema and provider check locally:

```bash
uv run \
  --with 'numpy>=1.26,<2' \
  --with 'gymnasium>=1.0,<2' \
  --with 'pyyaml>=6,<7' \
  --with 'jsonschema>=4.20,<5' \
  python examples/air_hockey_model_mediated_defence/run_wp4.py check
```

After WP4 is committed, export immutable build contexts. The command prints
the exact `docker buildx build` invocation:

```bash
uv run --with 'jsonschema>=4.20,<5' \
  python examples/air_hockey_model_mediated_defence/run_wp4.py \
  prepare-context --out build/air-hockey-wp4/context
```

The pinned base image is Marvin-local, so WP4 validates the build definition
without resolving or running that image. Once the ACRA experiment owner has
released Marvin, print the single deferred MuJoCo smoke command with:

```bash
uv run --with 'jsonschema>=4.20,<5' \
  python examples/air_hockey_model_mediated_defence/run_wp4.py \
  print-mujoco-smoke
```

Do not run the printed command while the ACRA experiments are active.

Running the joint image without overriding its command executes the H1 fake-host
scenario through the complete G2 harness. This is a CPU-only packaging check;
it is not MuJoCo task evidence.

## run the contract tests

From the repository root:

```bash
uv run --with 'jsonschema>=4.20,<5' \
  python -m unittest discover \
  -s examples/air_hockey_model_mediated_defence/host/tests \
  -p 'test_*.py' -v
```

These tests require no GPU, MuJoCo installation or remote machine.

## run the fake host

```bash
uv run --with 'jsonschema>=4.20,<5' \
  python examples/air_hockey_model_mediated_defence/host/run_fake_host.py \
  --socket /tmp/muesli-air-hockey.sock
```

The process creates a mode `0600` Unix-domain socket and refuses to replace a
non-socket path. Each connection carries one bounded JSON request and one JSON
reply.

The authoritative request and response shapes are in
`schemas/air_hockey_host/v1/`. See the
[air-hockey host protocol](../../docs/integration/air-hockey-host-protocol.md)
for the lifecycle and field boundary.

## layout

- `host/`: strict Python protocol host and WP1 tests;
- `src/`: C++ socket client, `env_backend`, commit validator and dispatch gate;
- `lisp/`: matched deadline-only and invocation-scoped task BTs;
- `configs/`: frozen deterministic H1--H8 scenario configurations;
- `evidence/`: named Gate G2 evidence predicates;
- `analysis/`: WP3 validation, replay, statistics and overlay modules;
- `provider/`: fixed and hash-bound ACRA-export provider adapters;
- `container/`: the pinned WP4 lock, requirements and joint Dockerfile;
- `tests/`: the gate-controlled C++ scenario harness.
