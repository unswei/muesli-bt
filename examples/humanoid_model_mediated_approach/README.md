# humanoid model-mediated approach video experiment

Status: experimental software experiment harness with an offline-tested Booster
Studio host-adapter scaffold. The live C++ bridge client, Studio package build,
virtual K1 run and physical video capture remain integration work.

## what this is

This directory contains the executable experiment for the humanoid video
trials. It compares a timeout-only baseline with invocation-scoped authority
while a deterministic approach-pose service waits for 2.5 seconds.

The external service returns one bounded pose in `ball_context`. It never
returns joint, balance or footstep commands. The checked-in runner uses a
recording walking adapter and refuses to enable physical motion.

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
| T2a | `deadline_only` | move A to B | Accept the stale proposal; the independent host envelope rejects dispatch. |
| T2b | `invocation_scoped` | move A to B | Reject with `context_changed`, enter `rejected_safe_wait` and do not attempt dispatch. |
| T3 | `invocation_scoped` | software emergency | Enter `safe_stand`, revoke authority and drop the delayed completion. |

T2a deliberately demonstrates an unsafe commit decision. It does not weaken
the host dispatch boundary. The physical robot therefore cannot be driven
towards the obsolete target by this example.

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
- decision and reason from `vla_result` or `async_authority_revoked`; and
- accepted or dispatch-blocked targets from `walking_target_dispatch`;
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
stability/emergency state, operating-area validation and a bounded pose
follower. Motion is disabled by default. Before enabling a robot trial:

1. implement the C++ client for the adapter's snapshot and synchronous dispatch
   socket operations;
2. feed each snapshot into the existing muesli state sync and use the dispatch
   response as the walking-target callback result;
3. build and package the C++ runtime for the Booster agent environment; and
4. retain the runtime commit gate and both dispatch-time safety checks.

The fake runner rejects `--physical-motion-enabled true`. Do not bypass that
guard. A physical push is outside this example and should follow a successful
software-emergency trial and the robot safety review.

`--force` replaces only a basename-safe, experiment-marked run directory. It
refuses unmarked directories and paths outside the selected output root. A
replacement is captured and validated in a sibling staging directory first, so
a validation or runner failure leaves the previous evidence bundle intact. For
a multi-trial command, every selected trial is validated before any bundle is
published. Publication then uses sequential directory renames; it is not a
crash-atomic transaction across the matrix. If an old backup cannot be removed,
the command succeeds with a warning and retains that backup for manual recovery.
