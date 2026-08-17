# air-hockey model-mediated defence

This example is the staged integration for the muesli paper's dynamic
air-hockey demonstration. WP1 provides the versioned local protocol and pure
fake host. WP2 adds the C++ `env_backend`, matched Behaviour Trees, proposal
validator, dispatch gate and deterministic H1--H8 harness. WP3 adds the local
evidence-bundle, paired-analysis, replay and overlay tooling. WP4 pins the ACRA
source and container, defines the joint image and packages provider adapters.
WP5 validates the MuJoCo vertical slice, WP6 freezes and runs the complete
engineering pilot without opening the paper split, and WP7 runs and seals the
frozen paper campaign.

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
The runner is configured explicitly with `CMAKE_BUILD_TYPE=Release`; the
container-definition check rejects an image recipe that omits this setting.

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

## wp5 MuJoCo vertical slice

On Marvin, run ACRA's unchanged integration tests from the WP4 lock before any
Muesli trial. Gate G5 then runs the fixed shot and H1/H2a/H2b through fresh
MuJoCo hosts, validates each canonical event stream and replays every requested
action through a fresh direct ACRA environment:

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

This deterministic gate does not request a GPU. The successful Marvin run used
the default ACRA shot only; it did not generate or open the `muesli_test` paper
split. Evaluation-only MuJoCo state is written separately from public host
states and is rejected if it crosses into canonical events.

## wp6 calibration and pilot campaign

The frozen protocol is `configs/wp6_protocol.json`. Validate its schema,
engineering-manifest hashes, calibrated delays and closed paper split locally:

```bash
uv run --with-requirements \
  examples/air_hockey_model_mediated_defence/container/requirements-wp4.txt \
  python examples/air_hockey_model_mediated_defence/run_wp6.py check-protocol
```

Gate G6 must run from an empty evidence directory with the exact checkpoint
mounted read-only. The passing Marvin command used the archive-built joint
image below and exposed no GPU:

```bash
g6_output=$PWD/build/air-hockey-g6
checkpoint=/absolute/path/to/structured_k2-14303.npz
mkdir -p "$g6_output"
docker run --rm --cpus 2 --memory 8g --ipc host \
  --env OMP_NUM_THREADS=1 --env OPENBLAS_NUM_THREADS=1 \
  --env MKL_NUM_THREADS=1 --env NUMEXPR_NUM_THREADS=1 \
  --mount type=bind,src="$checkpoint",dst=/checkpoint/structured_k2-14303.npz,readonly \
  --mount type=bind,src="$g6_output",dst=/evidence \
  local/muesli-air-hockey:8555ffb-1b6bbbb \
  python3 /opt/muesli-bt/examples/air_hockey_model_mediated_defence/run_wp6.py run \
    --runner /opt/muesli-bt/build/air-hockey-wp4/muesli_bt_air_hockey_scenario_tests \
    --checkpoint /checkpoint/structured_k2-14303.npz \
    --out /evidence/campaign
```

The passing campaign ran H1--H8 once for each of 26 engineering shots (234
runs), plus timely, boundary and stale delay calibrations. It recorded 26/26
current results, 26 context-change rejections, exactly 26 deadline-only
obsolete dispatches, zero invocation-scoped obsolete dispatches and 26 fallback
checks. Operational BT tick p99 was 9.605 ms with no 20 ms budget misses. The
hash-bound `structured_k2` provider saved 26/26 shots, had 0.130 ms p95
inference latency and required no deadline fallback. Its five-step action lock
is bound to the checkpoint source-protocol hash rather than inferred from pilot
outcomes. The `muesli_test` split remained unopened.

## wp7 frozen paper campaign

WP7 is governed by `configs/wp7_protocol.json`. The local protocol check
validates all frozen hashes and campaign counts without generating or reading
the paper split:

```bash
uv run --with-requirements \
  examples/air_hockey_model_mediated_defence/container/requirements-wp4.txt \
  python examples/air_hockey_model_mediated_defence/run_wp7.py check-protocol
```

The authorised campaign ran from an empty output directory inside the
archive-built image `local/muesli-air-hockey:38c8a19-1b6bbbb` (image ID
`sha256:203151fc7bd897851a5cf998d30df62eccbb2c4c6ecc1d6d43136f933c609621`)
with 2 CPUs, 8 GB RAM and no exposed GPU:

```bash
g7_root=$PWD/build/air-hockey-g7
checkpoint=/absolute/path/to/structured_k2-14303.npz
mkdir -p "$g7_root/evidence"
docker run --rm --cpus 2 --memory 8g --ipc host \
  --env OMP_NUM_THREADS=1 --env OPENBLAS_NUM_THREADS=1 \
  --env MKL_NUM_THREADS=1 --env NUMEXPR_NUM_THREADS=1 \
  --mount type=bind,src="$checkpoint",dst=/checkpoint/structured_k2-14303.npz,readonly \
  --mount type=bind,src="$g7_root",dst=/wp7 \
  local/muesli-air-hockey:38c8a19-1b6bbbb \
  python3 /opt/muesli-bt/examples/air_hockey_model_mediated_defence/run_wp7.py run \
    --runner /opt/muesli-bt/build/air-hockey-wp4/muesli_bt_air_hockey_scenario_tests \
    --checkpoint /checkpoint/structured_k2-14303.npz \
    --out /wp7/evidence/campaign \
    --image local/muesli-air-hockey:38c8a19-1b6bbbb \
    --image-digest sha256:203151fc7bd897851a5cf998d30df62eccbb2c4c6ecc1d6d43136f933c609621
```

After Gate G7 passes, seal it in a separate container invocation:

```bash
docker run --rm \
  --mount type=bind,src="$g7_root",dst=/wp7 \
  local/muesli-air-hockey:38c8a19-1b6bbbb \
  python3 /opt/muesli-bt/examples/air_hockey_model_mediated_defence/run_wp7.py seal \
    --campaign /wp7/evidence/campaign \
    --backup /wp7/backups/wp7-g7-38c8a19-1b6bbbb.tar.gz \
    --seal-report /wp7/evidence/g7-seal.json
```

The frozen 72-shot `muesli_test` manifest produced 216 deterministic pairs
across three delay seeds and 12 predeclared learned-provider pairs. All 228
pairs, or 456 policy bundles including exact replay, passed. The deadline-only
baseline dispatched an obsolete proposal in 228/228 pairs; invocation-scoped
authority dispatched none. There were no missing terminal decisions,
reason-code failures, replay mismatches, trace failures or direct-replay
failures. BT tick p99 was 9.145 ms, with 9/1,824 samples above 20 ms and a
51.976 ms maximum. Learned inference p95 was 0.212 ms over 12 samples.

This campaign demonstrates authority and evidence integrity, not improved task
success. The current invocation-scoped continuation holds position after
rejection and has no capable defensive fallback: its save rate was 0.0132,
compared with 0.9079 for the deliberately unsafe deadline-only baseline. Any
paper use must report this fallback limitation rather than interpreting the
result as a policy-performance gain.

The sealed campaign is retained on Marvin under
`/home/oliver/experiments/muesli-air-hockey/wp7-gate-g7-38c8a19-1b6bbbb/`.
Its campaign checksum-manifest SHA-256 is
`80af405a8c05cf035525af27c27b532b29f82d0e33cd90e4cfe6b6262f0031f1`;
the verified backup SHA-256 is
`99f70a25e6736a24ddf3f1422d70336d59054919be12a3b60c5d2c7a7c3f903b`.
`g7-report.json` is part of the checksummed campaign and necessarily records
pre-seal flags; the external `g7-seal.json` is the authoritative seal record.

## wp8 current-context recovery arm

WP8 freezes a third, non-paper arm for the recovery question raised by the
WP7 result. It keeps the invocation-scoped admission and dispatch contract,
but replaces hold-position fallback with `current_context_recovery.v1`.
After an obsolete model result is rejected, the policy uses only the current
public observation: a visible puck target when available, otherwise the
current public mallet position. It has no privileged-state access and no
external inference.

The frozen protocol is `configs/wp8_recovery_protocol.json`. It is paired
with the existing deadline-only and invocation-scoped hold treatments, but
the WP7 two-arm seal is not modified. The local check exercises the new tree,
policy callback, event evidence and zero-obsolete-dispatch predicate:

```bash
cmake --build build/dev --target muesli_bt_air_hockey_scenario_tests -j
uv run --with 'jsonschema>=4.20,<5' \
  python examples/air_hockey_model_mediated_defence/run_recovery.py \
  --runner build/dev/muesli_bt_air_hockey_scenario_tests
```

A successful WP8 campaign should report integrity and recovery separately:
the rejected proposal must never be dispatched, while the recovery policy
must produce at least one current-context action and complete the episode.

After a WP8 campaign passes, seal it as a separate fail-closed operation. The
seal writes a complete SHA-256 manifest, creates and verifies a compressed
backup, then changes campaign files to mode `0444` and directories to `0555`:

```bash
python examples/air_hockey_model_mediated_defence/run_wp8.py seal \
  --campaign /path/to/evidence/campaign \
  --backup /path/to/evidence/campaign.tar.gz \
  --seal-report /path/to/evidence/wp8-seal.json
```

The backup and seal report must be outside the campaign. The command refuses
unmarked or non-passing campaigns, changed protocols or summaries, symlinks,
and existing output targets.

The first three-arm Marvin run completed all 684 treatments but is not valid
paper evidence. It exposed two harness defects: retained terminal jobs emitted
repeated `vla_result` decisions, and the image used CMake's unoptimised default.
The failed run remains unchanged at
`/home/oliver/experiments/muesli-air-hockey/wp8-recovery-d3c7be6-1b6bbbb/evidence/campaign`.

After rebuilding the pushed revision, rerun only the recovery treatment into a
new evidence root and revalidate the unchanged deadline-only and hold raw
bundles from the failed campaign:

```bash
failed_g8=/home/oliver/experiments/muesli-air-hockey/wp8-recovery-d3c7be6-1b6bbbb/evidence/campaign
g8_root=/home/oliver/experiments/muesli-air-hockey/wp8-recovery-latched-<muesli-revision>-1b6bbbb
checkpoint=/home/oliver/experiments/airhockey-memory-distillation/principal-sweep-v1-2026-08-14-v3/final/structured_k2/14303/checkpoint.npz
docker run --rm --cpus 2 --memory 8g --ipc host \
  --env OMP_NUM_THREADS=1 --env OPENBLAS_NUM_THREADS=1 \
  --env MKL_NUM_THREADS=1 --env NUMEXPR_NUM_THREADS=1 \
  --mount type=bind,src="$checkpoint",dst=/checkpoint/structured_k2-14303.npz,readonly \
  --mount type=bind,src="$failed_g8",dst=/wp8-source,readonly \
  --mount type=bind,src="$g8_root",dst=/wp8 \
  local/muesli-air-hockey:wp8-<muesli-revision>-1b6bbbb \
  python3 /opt/muesli-bt/examples/air_hockey_model_mediated_defence/run_wp8.py run \
    --runner /opt/muesli-bt/build/air-hockey-wp4/muesli_bt_air_hockey_scenario_tests \
    --checkpoint /checkpoint/structured_k2-14303.npz \
    --out /wp8/evidence/campaign \
    --image local/muesli-air-hockey:wp8-<muesli-revision>-1b6bbbb \
    --image-digest <image-id-without-sha256-prefix> \
    --reuse-baselines-from /wp8-source
```

The new root contains 228 freshly run recovery bundles and copied raw bundles
for the 456 preserved baselines. Every copied bundle is hash-checked and
reanalysed with the corrected strict checker. The report records revisions and
container digests by treatment. Because the preserved baselines came from the
old unoptimised image, the latency gate applies only to the fresh Release
recovery arm; the combined timing distribution remains diagnostic and is not
presented as a homogeneous Release benchmark.

The WP8 runner fails before opening an output root unless all four thread
limits are `1`. Without those limits, numerical-library worker pools contend
with the control process under Docker's two-CPU quota and create periodic
50--60 ms scheduler stalls. Those stalls are infrastructure timing, not BT
execution work, but they invalidate the operational latency gate.

The passing campaign used image
`local/muesli-air-hockey:wp8-c0ec89e-1b6bbbb` (image ID
`sha256:d7058dcb76c5e7a347d3d03481fc84908ac15aa1052676e03e1b24433c4fa3a4`).
All 228 recovery pairs and all 684 reaggregated bundles passed. Recovery
dispatched no obsolete action and emitted no duplicate terminal decision. It
saved 150/228 shots (0.6579), compared with 207/228 (0.9079) for the unsafe
deadline-only baseline and 3/228 (0.0132) for hold-position fallback. Recovery
BT tick p99 was 5.879 ms over 20,228 samples; 26 samples exceeded 20 ms and the
maximum was 105.456 ms. Learned inference p95 was 0.249 ms over 12 samples.

The campaign is retained on Marvin at
`/home/oliver/experiments/muesli-air-hockey/wp8-recovery-thread-limited-c0ec89e-1b6bbbb/evidence/campaign`.
Its `wp8-report.json` SHA-256 is
`4c1d52bc18c837ca3488cb3885f548fb15ee1483cf1ac20795e93849f496c673`.
The external seal report is stored beside the campaign. The complete checksum
manifest contains 12,318 entries and has SHA-256
`149c0b71906c6675217e2a97e50b3d54059a68d4bc812ad6364c3179b1a0aa73`;
an independent post-seal check reported zero failures. The verified compressed
backup has SHA-256
`3a81bc515f74b39c9ba819ae51e1448d897a40f33b510bb7ed29667d1c7486cb`.
Campaign files are mode `0444` and directories are mode `0555`.

## wp9 context-token sensitivity

WP9 is a small matched sensitivity study over the host-owned context
equivalence relation. Its frozen protocol is
`configs/wp9_context_sensitivity_protocol.json`. It compares three policies at
the same tracking reacquisition: always assign a new context, assign a new
context above 0.10 normalised public-target displacement, or assign one above
0.20 displacement. The policy sees only public observation indices 16--18.

The study predeclares 0.10 as the downstream usefulness tolerance. It reports
context invalidations, obsolete dispatches above that reference tolerance,
rejections at or below it, and save rate. Twenty-four shots are selected by
SHA-256 shot-identifier order and crossed with the existing 50, 80 and 110 ms
delays, giving 72 matched cases and 216 policy runs. Thresholds and selection
were frozen before campaign outcomes were inspected.

Check the frozen protocol and both native authority branches from the
repository root:

```bash
uv run --with 'jsonschema>=4.20,<5' \
  python examples/air_hockey_model_mediated_defence/run_wp9.py check-protocol
uv run --with 'jsonschema>=4.20,<5' \
  python examples/air_hockey_model_mediated_defence/run_wp9.py check-native \
  --runner build/dev/muesli_bt_air_hockey_scenario_tests
```

The full `run` command additionally requires the sealed WP8 campaign and its
external seal report. It refuses a non-empty output directory, verifies every
WP8 checksum before starting, requires single-threaded numerical-library
settings, and records live/replay evidence for every policy run.

After a passing campaign, seal and back it up outside the campaign directory:

```bash
python examples/air_hockey_model_mediated_defence/run_wp9.py seal \
  --campaign /path/to/wp9/campaign \
  --backup /path/to/wp9-campaign-backup.tar.gz \
  --seal-report /path/to/wp9-seal.json
```

Sealing verifies the report and semantic summary hash, writes a complete
checksum manifest, verifies the compressed backup, and changes the raw files
and directories to modes `0444` and `0555` respectively.

## wp10 post-admission authority

WP10 isolates the race between result admission and capability use. The frozen
protocol crosses two dispatch policies with context change, reactive owner
pre-emption and a no-change control over the same deterministic 24-shot subset.
The admission-only mutant trusts a handle after admission. The two-gate runtime
revalidates the handle immediately before the effect. Both arms then use the
current-context recovery tree.

The local checks do not open the paper split:

```bash
uv run --with 'jsonschema>=4.20,<5' \
  python examples/air_hockey_model_mediated_defence/run_wp10.py check-protocol
uv run --with 'jsonschema>=4.20,<5' \
  python examples/air_hockey_model_mediated_defence/run_wp10.py check-native \
  --runner build/dev/muesli_bt_air_hockey_scenario_tests
```

The 144-run campaign records obsolete capability calls, save outcome,
valid-handle control rejections, exact replay and strict trace validation. Its
physical metric pairs the two treatments at each observation step and projects
their mallet-position separation towards the obsolete target. This isolates the
trajectory effect of the stale capability call from later motion that both
treatments may produce under recovery. `run_wp10.py seal` writes and verifies a
compressed backup before making a passing campaign read-only.

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
- `configs/`: frozen deterministic scenarios and WP6/WP7 protocols;
- `evidence/`: named Gate G2 evidence predicates;
- `analysis/`: WP3 validation, replay, statistics and overlay modules;
- `provider/`: fixed and hash-bound ACRA-export provider adapters;
- `container/`: the pinned WP4 lock, requirements and joint Dockerfile;
- `tests/`: the gate-controlled C++ scenario harness.
