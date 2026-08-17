# air-hockey model-mediated defence

## what this is

The air-hockey example is a staged paper demonstration of
[invocation-scoped authority](../bt/invocation-scoped-authority.md). The WP2
harness proves the request, context, revocation and dispatch behaviour against
a strict local fake host. The WP3 workflow turns raw runtime and task records
into reproducible evidence bundles. WP4 pins the ACRA source and simulator
image, then checks provider packaging without starting MuJoCo.
WP5 validates the pinned MuJoCo vertical slice. WP6 freezes the engineering
protocol and runs the complete authority and learned-provider pilot while the
paper split remains closed. WP7 runs and seals the frozen paper campaign.

The synthetic campaign is an analysis test. It is not MuJoCo task evidence and
is never paper eligible.

## when to use it

Use the local workflow to change or review:

- air-hockey authority scenarios H1--H8;
- event, trajectory, manifest or recorded-provider schemas;
- paired integrity and obsolete-target motion summaries;
- binomial or paired-bootstrap reporting; or
- trace-derived overlay fields;
- the fixed or ACRA-export provider boundary; or
- immutable joint-container build contexts.

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

Run the local WP4 packaging gate against the sibling ACRA checkout:

```bash
uv run \
  --with 'numpy>=1.26,<2' \
  --with 'gymnasium>=1.0,<2' \
  --with 'pyyaml>=6,<7' \
  --with 'jsonschema>=4.20,<5' \
  python examples/air_hockey_model_mediated_defence/run_wp4.py check
```

The gate exports ACRA commit
`1b6bbbbf19743b0042f01eabf0628eba5621cacf` to a temporary directory and
loads an engineering-only NumPy checkpoint through the real committed import
surface. It does not read mutable ACRA source files, select a final checkpoint,
resolve the Marvin-local image or contact Marvin.

After committing WP4, prepare the two immutable Git-archive contexts with:

```bash
uv run --with 'jsonschema>=4.20,<5' \
  python examples/air_hockey_model_mediated_defence/run_wp4.py \
  prepare-context --out build/air-hockey-wp4/context
```

The command prints the digest-bound `docker buildx build` command. Use
`print-mujoco-smoke` only after the ACRA experiment owner confirms that Marvin
is free. That deferred command runs ACRA's unchanged integration tests inside
the joint image before any muesli task trial.

Gate G5 uses `run_wp5.py` after that unchanged suite passes. It starts a fresh
MuJoCo host for the default fixed shot and each of H1, H2a and H2b, drives the
compiled C++ runner over the versioned socket contract, validates the canonical
events and information boundary, and directly replays the exact requested
actions through a second ACRA environment. The pinned successful command is:

```bash
g5_output=$PWD/build/air-hockey-g5
mkdir -p "$g5_output"
docker run --rm --cpus 2 --memory 8g --ipc host \
  --volume "$g5_output:/evidence" \
  local/muesli-air-hockey:65313e4-1b6bbbb \
  python3 /opt/muesli-bt/examples/air_hockey_model_mediated_defence/run_wp5.py \
    --runner /opt/muesli-bt/build/air-hockey-wp4/muesli_bt_air_hockey_scenario_tests \
    --out /evidence
```

No GPU is exposed to this command. It is an engineering Gate G5 run and must
not be pointed at the unopened `muesli_test` split.

Gate G6 is driven by the schema-validated `configs/wp6_protocol.json`. It fixes
the 26-shot engineering manifest, delay calibration, H1--H8 matrix,
`structured_k2` checkpoint identity, source-protocol action lock, timing
limits, save floor and fallback. Check the protocol locally before the Marvin
run:

```bash
uv run --with-requirements \
  examples/air_hockey_model_mediated_defence/container/requirements-wp4.txt \
  python examples/air_hockey_model_mediated_defence/run_wp6.py check-protocol
```

The passing campaign used archive-built image
`local/muesli-air-hockey:8555ffb-1b6bbbb` with 2 CPUs, 8 GB RAM and no exposed
GPU. All 234 deterministic matrix runs passed. Current-result progress was
26/26; the deadline-only baseline exposed 26 obsolete dispatches, while the
invocation-scoped configurations exposed none. Operational BT tick p99 was
9.605 ms with zero budget misses. The learned provider returned all 26 shots,
with 0.130 ms p95 inference latency and no deadline fallback. Build, image,
event, replay, timing and outcome artefacts are checksummed together. The report
records `paper_split_opened: false`; WP6 does not authorise a `muesli_test` run.

Gate G7 is driven by `configs/wp7_protocol.json`. `run_wp7.py check-protocol`
is the last split-safe local check; the `run` command opens the 72-shot
`muesli_test` split and therefore requires explicit authorisation, an empty
output directory, the exact read-only checkpoint and the immutable joint
image. The passing campaign used
`local/muesli-air-hockey:38c8a19-1b6bbbb`, image ID
`sha256:203151fc7bd897851a5cf998d30df62eccbb2c4c6ecc1d6d43136f933c609621`,
2 CPUs, 8 GB RAM and no exposed GPU.

All 228 matched pairs and 456 policy bundles passed per-run validation and
exact replay. The deadline-only baseline dispatched an obsolete proposal in
228/228 pairs, whereas invocation-scoped authority dispatched none. There
were no missing terminal invocations, reason-code failures, replay mismatches,
trace failures or direct-replay failures. BT tick p99 was 9.145 ms; 9/1,824
samples exceeded the 20 ms budget and the maximum was 51.976 ms. Learned
inference p95 was 0.212 ms over the predeclared 12-shot subset.

The task outcome exposes a necessary design trade-off. The full condition's
post-rejection continuation merely holds position, so save rate fell from
0.9079 to 0.0132. Gate G7 therefore supports the authority claim, while also
showing that rejection requires a task-capable fallback; it does not support a
task-performance claim. The campaign is read-only and backed up. Its checksum
manifest has SHA-256
`80af405a8c05cf035525af27c27b532b29f82d0e33cd90e4cfe6b6262f0031f1`,
and the verified backup has SHA-256
`99f70a25e6736a24ddf3f1422d70336d59054919be12a3b60c5d2c7a7c3f903b`.

WP10 adds a narrow post-admission experiment for the second authority gate.
The frozen protocol uses the same deterministic 24-shot subset for two
treatments and three schedules: context change after admission, owner
pre-emption after admission, and a no-change control. `admission_only` trusts
the accepted handle; `two_gate` revalidates at the capability boundary. Both
treatments use current-context recovery after the causal dispatch attempt. Run
the split-safe checks with:

```bash
uv run --with-requirements \
  examples/air_hockey_model_mediated_defence/container/requirements-wp4.txt \
  python examples/air_hockey_model_mediated_defence/run_wp10.py check-protocol

uv run --with-requirements \
  examples/air_hockey_model_mediated_defence/container/requirements-wp4.txt \
  python examples/air_hockey_model_mediated_defence/run_wp10.py check-native \
    --runner build/muesli_bt_air_hockey_scenario_tests
```

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
- A learned-provider configuration requires the exact family, checkpoint
  SHA-256, source-protocol SHA-256 and action-lock semantics. WP6 contains the
  frozen engineering choice and WP7 binds the paper campaign; no checkpoint
  path is committed.
- The pinned base image is available on Marvin, so local WP4 proves the build
  definition and non-MuJoCo startup path rather than claiming an image run.
- `--force` replaces only a safe, marked run directory. It refuses unmarked
  data and path traversal.
- Exact binomial intervals describe observed integrity counts. A zero count is
  reported with an upper confidence bound, not as zero probability.
- WP3 SVG overlays and synthetic outcomes validate tooling only. WP7's figures
  and videos are generated from the validated MuJoCo paper bundles.

## see also

- [air-hockey host protocol](../integration/air-hockey-host-protocol.md)
- [event log](../observability/event-log.md)
- [testing](../testing.md)
- [humanoid model-mediated approach](humanoid-model-mediated-approach.md)
