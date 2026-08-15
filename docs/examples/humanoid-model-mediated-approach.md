# humanoid model-mediated approach video experiment

!!! note "status"

    Status: experimental software experiment harness with an offline-tested
    Booster Studio host bridge, verified native-runner supervisor and
    canonical-event overlay path. The four-trial virtual K1 matrix and a
    motion-enabled T1 video have passed. Physical video capture remains
    integration work.

## what this is

This example runs the four humanoid video trials defined by the
[experiment contract](../project/humanoid-model-mediated-approach-contract.md).
It uses the real asynchronous VLA nodes, invocation authority, host validation,
walking-target dispatch and canonical `mbt.evt.v1` stream.

A deterministic fake service returns one three-dimensional approach pose after
2.5 seconds. The Behaviour Tree continues ticking during the delay.

## when to use it

Use this example to:

- rehearse the video sequence without a Booster SDK;
- compare `deadline_only` and `invocation_scoped` with the same BT shape;
- verify moved-ball and emergency evidence before robot integration; and
- generate provenance-rich runtime bundles for later video reconciliation.

Do not use the included recording dispatcher for physical motion. Physical
motion is available only through an explicit Booster bridge whose reported
motion state matches the runner option.

The Booster Studio subproject implements the platform-owned context, safety,
dispatch and bounded velocity policy. Motion is disabled by default. The muesli
C++ bridge client supplies live state and receives the synchronous host
decision.

The Studio agent can supervise one frozen trial after fresh ball, robot pose and
stability data arrive. It verifies a digest-bound Linux x86-64 payload before
launch and fails closed on missing files, changed files, wrong architecture or
an unexpected runner exit. Each live run derives `overlay.ass` directly from
`mbt.evt.v1`.

## how it works

The root is a reactive sequence. It first copies the host snapshot into the
blackboard, then re-evaluates the priority branches:

```text
safe stand > search > wait/commit/dispatch > submit > fallback
```

The delayed service deliberately completes even after cancellation. This makes
logical revocation visible in T3. The full validator checks frame, bounds,
context and stability. The research baseline validator deliberately omits
context identity at commit time.

The host dispatch boundary still checks context in both modes. T2a therefore
shows a stale proposal accepted by the timeout-only commit policy and then
blocked before the walking adapter. This preserves the safety envelope while
exposing the baseline defect.

T2b writes the rejected candidate to overlay-only blackboard keys and remains
in `rejected_safe_wait`. The candidate is never written to the live walking
target or sent to the dispatcher.

## api / syntax

The deadline-only BT is:

```lisp
--8<-- "examples/humanoid_model_mediated_approach/lisp/bt_deadline_only.lisp"
```

The invocation-scoped BT is:

```lisp
--8<-- "examples/humanoid_model_mediated_approach/lisp/bt_invocation_scoped.lisp"
```

The shared configuration freezes a 2.5-second delay, 3.5-second deadline,
20 Hz tick rate, fixed seed, frames, request action space, host pose bounds and
context thresholds. The
four per-trial files select only the BT, acceptance policy, intervention and
expected evidence. The runner rejects common-configuration or matrix drift and
evaluates every named evidence predicate in the per-trial protocol.

## example

Build the native helper and run the shortened matrix:

```bash
python3 -m pip install jsonschema
cmake --preset dev
cmake --build --preset dev --target humanoid_model_mediated_trial
python3 examples/humanoid_model_mediated_approach/run_trials.py \
  --runner build/dev/humanoid_model_mediated_trial \
  --check
```

Expected output ends with:

```text
PASS T1: ...
PASS T2a: ...
PASS T2b: ...
PASS T3: ...
humanoid video experiment matrix passed (4 trial(s))
```

Run the Booster host policy tests without BoosterOS or ROS 2:

```bash
python3 -m unittest discover \
  -s examples/humanoid_model_mediated_approach/booster_studio/tests \
  -p 'test_*.py' -v
```

Run all three local bridge layers, including the native runner:

```bash
ctest --test-dir build/dev --output-on-failure \
  -R '^muesli_bt_booster_(bridge|bridge_runner|studio_adapter)$'
```

Build the virtual K1 native payload:

```bash
python3 examples/humanoid_model_mediated_approach/booster_studio/tools/build_native_payload.py
```

The command requires Docker Buildx. It uses a pinned Linux x86-64 container;
local macOS binaries are rejected. The Booster adapter README contains the
Studio activation and video-finalisation runbook. The Agent starts with motion
disabled. In Studio, invoke `motion_arm` before invoking one of `trial_t1`,
`trial_t2a`, `trial_t2b` or `trial_t3`. The `software_emergency` action supplies
the controlled T3 interruption. In the `football3v3` match runner, use the
equivalent `/muesli/motion_arm`, `/muesli/trial_command` and
`/muesli/emergency` ROS 2 topics because Studio does not route active-Agent UI
clicks to team processes.

Run a real-time clip for the full moved-ball trial:

```bash
python3 examples/humanoid_model_mediated_approach/run_trials.py \
  --runner build/dev/humanoid_model_mediated_trial \
  --config T2b \
  --run-id-prefix paper-run-01
```

The generated bundle is under
`examples/humanoid_model_mediated_approach/runs/`. Its manifest marks the raw
and overlay videos, cross-event validation, and recorded-result replay as
pending. Per-record JSON Schema validation is fail-closed and must pass. Add the
pending artefacts before treating the run as paper evidence.

## gotchas

- `--check` shortens the delay and is never paper-eligible.
- Time scaling is rejected without `--check`; normal bundles always use the
  frozen real-time delay.
- T2a proves stale commit acceptance, not unsafe physical execution. The host
  envelope blocks the stale dispatch.
- The automatic intervention is suitable for rehearsal and deterministic
  evidence. A robot adapter must record the actual perception or operator
  intervention through the same canonical blackboard writes.
- The software stub enforces the configured movement threshold when changing
  ball context. It deliberately does not simulate observation age. The Booster
  perception adapter must supply and enforce observation timestamps.
- `events.jsonl` is the only event log. Do not add a separate overlay log.
- T2b's red candidate comes from `candidate-walking-target`; its job ID and
  generation are recorded in adjacent canonical blackboard writes.
- A completed runtime bundle is still missing raw video, overlay video and
  replay comparison until the manifest says otherwise.
- The native runner and adapter pass a local socket round trip. Signed Agent
  `0.2.5` also completed the four-trial virtual K1 matrix. The motion-enabled
  T1 run accepted one current target and the simulated robot walked towards
  it. This result does not replace device validation or physical video.
- The Booster host defaults to the standard `default` walking gait. Override
  `MUESLI_BOOSTER_GAIT` only after confirming that the selected simulator or
  device gait translates bounded velocity commands as expected.
- `build.toml` currently advertises only `sim_x86_64`. Do not add a device or
  ARM target until its native payload has a separate build and test result.
- A forced rerun is staged and validated before it replaces an existing marked
  evidence bundle. A selected matrix is fully validated before publication
  starts.
- Multi-directory publication is not crash-atomic. Preserve any `.previous-*`
  directory if the capture process or host stops during the final rename step.

## see also

- [experiment directory README](https://github.com/unswei/muesli-bt/blob/main/examples/humanoid_model_mediated_approach/README.md)
- [humanoid experiment contract](../project/humanoid-model-mediated-approach-contract.md)
- [approach pose validation](../bt/approach-pose-validation.md)
- [VLA logging](../observability/vla-logging.md)
- [testing and verification](../testing.md)
- [Booster Studio adapter README](https://github.com/unswei/muesli-bt/blob/main/examples/humanoid_model_mediated_approach/booster_studio/README.md)
- [Booster Studio bridge](../integration/booster-studio-bridge.md)
