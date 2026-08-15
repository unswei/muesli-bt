# humanoid model-mediated approach video experiment

Status: experimental software experiment harness with a locally integrated,
offline-tested Booster Studio host bridge, native supervisor and overlay path.
The signed Linux package and all four live trials ran on a virtual K1 at source
commit `e88bdaa`. Polished, canonical-event-validated T1 and matched T2
simulation prototypes are reproducible from clean Booster Studio captures.
The T2 comparison supports a simulation-only unsafe baseline that visibly
walks towards an obsolete target, beside current-context recovery and visible
walking towards the current target. Physical video capture remains integration
work.

## what this is

This directory contains the executable experiment for the humanoid video
trials. It compares a timeout-only baseline with invocation-scoped authority
while a deterministic approach-pose service waits for 2.5 seconds.

The external service returns one bounded pose in `ball_context`. It never
returns joint, balance or footstep commands. The checked-in runner uses a
recording walking adapter by default. An explicit local bridge option replaces
that adapter with the Booster host gate.

## contents

```text
humanoid_model_mediated_approach/
├── booster_studio/           fail-closed Booster K1 host adapter scaffold
├── configs/                  frozen common and per-trial configuration
├── evidence/manifests/       required evidence and video actions
├── lisp/                     deadline-only and invocation-scoped BTs
├── runs/                     ignored generated evidence bundles
├── src/                      delayed fake service and native runtime helper
└── run_trials.py             trial-matrix and evidence-bundle runner
```

The two BTs differ only in their tree name and `:acceptance_policy` value. The
matrix checker enforces that property. The native runner also inspects the
compiled `vla-request` and binds its model, keys, dimensions, bounds,
`max_delta`, frames, seed and deadline to `common.json`.

## trial matrix

| Trial | Policy | Intervention | Required runtime outcome |
| --- | --- | --- | --- |
| T1 | `invocation_scoped` | none | Accept and dispatch once. |
| T2a | `deadline_only` | move the ball | Accept the stale proposal; the independent host envelope rejects dispatch. |
| T2b | `invocation_scoped` | move the ball | Reject with `context_changed`, enter `rejected_safe_wait` and do not attempt dispatch. |
| T3 | `invocation_scoped` | software emergency | Enter `safe_stand`, revoke authority and drop the delayed completion. |

T2a deliberately demonstrates an unsafe commit decision. It does not weaken
the host dispatch boundary. The physical robot therefore cannot be driven
towards the obsolete target by this example.

The polished simulation video has a separate, explicit T2a-only override. It
disables the host context check so the baseline failure becomes physically
visible as motion towards obsolete target A. The override is false by default,
cannot cross loss of the ball track and must never be enabled on hardware. See
the [T2 video workflow](booster_studio/video/t2-comparison.md).

## build and check

From the repository root:

```bash
python3 -m pip install jsonschema
cmake --preset dev
cmake --build --preset dev --target humanoid_model_mediated_trial
python3 examples/humanoid_model_mediated_approach/run_trials.py \
  --runner build/dev/humanoid_model_mediated_trial \
  --check
```

`--check` scales only the artificial delay and intervention time. It runs all
four trials in a temporary directory and is not paper evidence. Time scaling is
rejected outside `--check`, so a normal run always uses the frozen 2.5-second
service delay.

## run the video protocol

Run the complete frozen matrix at real time:

```bash
python3 examples/humanoid_model_mediated_approach/run_trials.py \
  --runner build/dev/humanoid_model_mediated_trial
```

Run one trial when recording separate clips:

```bash
python3 examples/humanoid_model_mediated_approach/run_trials.py \
  --runner build/dev/humanoid_model_mediated_trial \
  --config T2b \
  --run-id-prefix paper-run-01
```

The native helper prints `REQUEST_SUBMITTED` when the fake backend starts.
Move the ball or assert the software emergency flag after that cue. The default
runner injects the configured intervention automatically after 1 second. A
Booster host adapter may instead map the same state boundary to operator or
perception input.

## evidence bundles

Each trial writes:

```text
runs/<run-id>/
├── manifest.json
├── events.jsonl
├── trial-summary.json
├── event-validation.json
└── replay-report.json
```

`manifest.json` records source hashes, Git state, timing, frames, bounds,
contexts, intervention time and artefact hashes. It lists `raw-video.mp4` and
`overlay-video.mp4` as pending until they are added by the recording workflow.
`event-validation.json` must report full JSON Schema validation as passed.
The evidence protocols use named, structured predicates. The runner evaluates
every predicate against the canonical stream and records the results in the
run manifest. Missing `jsonschema` is a hard failure. `replay-report.json`
remains explicit
that cross-event validation and recorded-result replay are pending. A bundle is
not paper-eligible until those steps are complete.

`events.jsonl` is the only event log. The overlay should read:

- active branch from `bb_write` on `active-branch`;
- request identity and generation from `vla_submit`;
- context from `bb_write` on `ball-context-id`;
- request state from `vla_poll` and `request-state`;
- runtime decision and reason from `vla_result` or
  `async_authority_revoked`;
- the separate host dispatch decision and reason from
  `walking_target_dispatch`;
- a rejected or dispatch-blocked candidate from `candidate-walking-target`, correlated
  by `candidate-target-job-id` and `candidate-target-generation`; and
- target colour from `walking-target-state` (`current` is green and `obsolete`
  is red).

The SDK-independent perception stub enforces the configured Euclidean movement
threshold when changing the context ID. It does not simulate observation age;
`common.json` records that policy explicitly. The Booster perception adapter
must supply and enforce observation timestamps before a physical trial.

## booster integration boundary

The `booster_studio/` project implements ball context tracking, observation age,
stability/emergency state, operating-area validation, a bounded pose follower,
a manifest-verified native runner supervisor and event-derived overlays. Motion
is disabled by default. Studio exposes an explicit `motion_arm` action plus one
action per trial and a controlled `software_emergency` action. The native
runner polls adapter snapshots and uses the synchronous bridge response as the
walking-target callback result. Before enabling a robot trial:

1. build the pinned `sim_x86_64` native payload and verify its manifest;
2. run the snapshot/dispatch round trip with motion disabled;
3. invoke `motion_arm` in the virtual K1 scene;
4. run the software-emergency trial; and
5. retain the runtime commit gate and both dispatch-time safety checks.

See `booster_studio/README.md` for the exact payload, trial and video finalising
commands, including the equivalent ROS operator topics required by the
`football3v3` match runner. The current Studio package advertises only
`sim_x86_64`; device targets remain deliberately unavailable until separately
built and tested.

Physical motion requires an absolute `--booster-bridge-socket`, an explicit
`--physical-motion-enabled true` and a host snapshot that also reports motion
enabled. A physical push remains outside this example until the software trial
and robot safety review pass.

`--force` replaces only a basename-safe, experiment-marked run directory. It
refuses unmarked directories and paths outside the selected output root. A
replacement is captured and validated in a sibling staging directory first, so
a validation or runner failure leaves the previous evidence bundle intact. For
a multi-trial command, every selected trial is validated before any bundle is
published. Publication then uses sequential directory renames; it is not a
crash-atomic transaction across the matrix. If an old backup cannot be removed,
the command succeeds with a warning and retains that backup for manual recovery.
