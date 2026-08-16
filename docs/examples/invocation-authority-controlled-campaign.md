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

## see also

- [invocation-scoped authority](../bt/invocation-scoped-authority.md)
- [humanoid model-mediated approach](humanoid-model-mediated-approach.md)
- [air-hockey model-mediated defence](air-hockey-model-mediated-defence.md)
