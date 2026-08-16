# controlled invocation-authority campaign

## what this is

The controlled invocation-authority campaign compares four ways to call a
variable-latency service from one reactive Behaviour Tree task. The campaign
separates authority correctness from proposal quality and robot performance.

Schedule identifiers are internal protocol keys. Reader-facing reports use
plain descriptions such as "observation context changes before result
admission". A paper must not assume that a reader knows the internal numbering.

## when to use it

Use the campaign when changing asynchronous request ownership, result admission,
branch revocation, context identity, terminal handling or capability dispatch.
It is also the source of the controlled results reported in the accompanying
paper.

Do not use the campaign to compare model quality or low-level controllers.

## how it works

Every implementation runs the same Lisp task, proposal schema, host validation
and event ordering. The four implementations differ only in their authority
mechanism: blocking, ordinary asynchronous completion, timeout plus cancellation,
or the full invocation-scoped contract.

`shared_lisp_task_runner` registers the common emergency, model, dispatch,
fallback and safe-stand callbacks. A scheduled submission is queued with
`request_submission()` and is issued only from the common model action. Result
dispatch is deliberately separated from admission by a tick boundary. That
boundary permits the controlled context-after-admission schedule without
changing the Lisp tree.

The timeout-only adapter makes one deadline terminal claim and requests
best-effort cancellation. It deliberately does not check branch epoch,
generation, context identity or dispatch authority. The invocation-scoped
adapter translates the experiment provider response into a production VLA
response. Production `vla-request`, `vla-wait`, the approach-pose validator and
`dispatch_walking_target()` retain responsibility for all B3 authority
decisions.

The semantic lane uses a stepped clock and explicit event barriers. An
independent oracle tracks the branch epoch, request generation, context and
deadline. The oracle decides whether a commit or dispatch was obsolete; the
implementation under test cannot redefine that outcome.

The campaign driver resolves the frozen JSON files into a versioned tabular
plan. The C++ engine reads that plan and executes the common task through the
four real adapters. The driver then validates each canonical event stream and
derives manifests, summaries and paper tables. Derived files never replace the
canonical runtime evidence.

The completion-burst schedule creates 16 independent task instances before it
releases their provider barriers at one logical time. The replay schedule runs
the preceding schedules again with the same deterministic scripted provider
and compares task-decision signatures. Neither schedule substitutes an
analysis-only model for runtime execution.

The timing lane uses the real monotonic clock. Semantic and timing results are
never pooled.

## api and syntax

The frozen C0 files are:

- `experiments/invocation_authority_controlled/configs/protocol.v1.json`;
- `experiments/invocation_authority_controlled/schedules/catalogue.v1.json`;
- `experiments/invocation_authority_controlled/lisp/common_task.lisp`; and
- `schemas/controlled_authority/v1/`.

The C++ foundation is in
`experiments/invocation_authority_controlled/src/common_task.hpp`. It provides:

- the shared task state;
- immutable request records;
- the independent obsolescence oracle; and
- a deterministic coordinator for task-world events.

`effect_recorder.hpp` records request, completion, cancellation, commit,
rejection, accepted or rejected dispatch, fallback and safe-stand effects. It
calls the coordinator-owned oracle again at commit and dispatch, so a weak
variant cannot classify its own effect.

`variant.hpp` defines the experiment-local provider and authority-variant
interfaces. B0 blocks inside provider inference. B1 runs the same inference in
a worker without an authority check. B2 adds a deadline terminal claim and
best-effort cancellation, but no other authority dimension.

`runtime_variant.hpp` provides B3. The adapter does not copy the production
commit rules into the experiment. It runs a nested production VLA subtree and
mirrors the resulting accepted, rejected or revoked outcome into the independent
effect recorder. Its provider cancellation watcher forwards a production
best-effort cancellation request to the experiment provider.

`task_runner.hpp` provides the shared executable seam. `task_events()` returns
the canonical `mbt.evt.v1` stream for the common Lisp task. `variant_events()`
returns the separate production stream owned by B3. The streams are not
concatenated because each has its own canonical sequence and run envelope.

`campaign_plan.hpp` defines the versioned resolved-plan boundary.
`campaign_engine.hpp` executes its run records. The outcome and writer modules
evaluate the frozen expectations and serialise raw trial records plus canonical
streams. `run_campaign.py` is the supported command-line entry point and is
responsible for all derived artefacts.

## example

The common task gives an emergency branch priority over the model-mediated
branch and retains an authored fallback:

```lisp
(defbt controlled-invocation-authority-common-task
  (reactive-sel
    (seq
      (cond controlled-emergency?)
      (act controlled-safe-stand))
    (seq
      (act controlled-model-step)
      (act controlled-dispatch-step))
    (act controlled-fallback)))
```

The runner registers the model and dispatch action names, then delegates their
work through the selected adapter. The task structure does not change between
variants.

A runner queues a scheduled request and ticks the same compiled task for any
adapter:

```cpp
auto variant = std::make_unique<timeout_variant>(
    provider, recorder, [&coordinator] { return coordinator.now(); });
shared_lisp_task_runner runner(
    coordinator, recorder, std::move(variant), common_task_source);

runner.request_submission();
(void)runner.tick();
```

The complete executable examples are in
`experiments/invocation_authority_controlled/tests/task_runner_tests.cpp`.

Build and run one complete engineering seed with:

```sh
cmake -S . -B build -DMUESLI_BT_BUILD_CONTROLLED_AUTHORITY_EXPERIMENT=ON
cmake --build build --target muesli_bt_controlled_authority_campaign -j
python3 experiments/invocation_authority_controlled/run_campaign.py \
  --output /tmp/controlled-authority-seed-0 \
  --seeds 0
```

The standard seed sets are selected with `--seed-set engineering` and
`--seed-set paper`. The output directory must be new or empty.

The artefact tree is:

```text
campaign-manifest.json
resolved-plan.tsv
raw_trials.jsonl
events/*.mbt.evt.v1.jsonl
runs/<internal-schedule>/<variant>/<seed>/manifest.json
summary/trials.csv
summary/schedule-summary.{csv,json}
summary/variant-summary.{csv,json}
paper/controlled-authority-table.{csv,md}
paper/variant-summary.md
```

Run manifests retain internal schedule and variant keys for auditability, and
pair each key with its reader label. Paper tables omit internal schedule keys
and use the reader labels directly. Every canonical stream and frozen input is
identified by a SHA-256 digest in a manifest. The compact paper table reports
obsolete-effect trials and terminal-outcome counts. The terminal count exposes
duplicate completion and blocked-submission controls that an obsolete-effect
column alone would hide.

## gotchas

- Internal schedule identifiers are for manifests and regression fixtures, not
  unexplained prose.
- A completion at the exact deadline is current. It becomes expired only after
  the deadline.
- Best-effort provider cancellation does not change the oracle's authority
  decision.
- B2's terminal claim prevents a late completion from creating a second
  terminal outcome. It does not make a pre-deadline stale result safe.
- B3 evidence comes from production gates. A replacement implementation in the
  campaign harness would invalidate that comparison.
- `task_events()` and `variant_events()` are separate canonical streams. Do not
  concatenate their JSONL lines without rebuilding one envelope and sequence.
- The primary integrity unit is a complete resolved schedule, not an individual
  tick.
- Runtime evidence must remain in `mbt.evt.v1`.
- The driver rejects a non-empty output directory. Choose a new campaign path
  instead of combining runs.
- A partial or engineering campaign does not evaluate the paper gate. Only the
  exact four variants, 16 schedules and frozen 128 paper seeds do so.
- The replay schedule requires every preceding schedule for the same variant
  and seed.

## see also

- [invocation-scoped authority](../bt/invocation-scoped-authority.md)
- [humanoid model-mediated approach](humanoid-model-mediated-approach.md)
- [air-hockey model-mediated defence](air-hockey-model-mediated-defence.md)
