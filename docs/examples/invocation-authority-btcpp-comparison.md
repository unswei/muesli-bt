# BehaviorTree.CPP invocation-authority comparison

## what this is

This experiment compares the same model-mediated Behaviour Tree task across
muesli-bt and BehaviorTree.CPP. Each runtime has an ordinary asynchronous
implementation and a full invocation-scoped implementation. The comparison
tests portability of the authority contract. It does not assume that one
runtime is incapable of implementing the other runtime's safety mechanism.

The protocol was frozen as `controlled-authority.btcpp-comparison.e1.v1`
before the BehaviorTree.CPP authority adapters were implemented. Schedule
identifiers are internal audit keys. Paper prose and tables use plain-language
schedule descriptions.

## when to use it

Use this comparison when evaluating whether result ownership, pre-emption,
context changes, deadline handling, terminal claims and dispatch revalidation
behave equivalently across the two runtimes.

Use the separate generic performance lane for execution-cost measurements.
Do not infer authority correctness from tick speed.

## how it works

All four implementations receive the same task observations, proposal values,
provider completions and logical event order. The shared C0 oracle, not the
runtime under test, decides whether a commit or dispatch is obsolete.

The BehaviorTree.CPP ordinary implementation must use its documented
`BT::StatefulActionNode` lifecycle: `onStart()`, `onRunning()` and
`onHalted()`. It uses a worker/provider boundary and reasonable best-effort
cancellation. It is a competent asynchronous baseline, not a deliberately
blocking or incomplete strawman.

The BehaviorTree.CPP full implementation uses the same lifecycle and adds an
invocation record, generation and entry epoch, context identity, deadline,
exactly-once terminal claim, logical revocation, host validation, central
admission and dispatch-time revalidation. Framework lifecycle support and
custom contract code are reported separately.

Every C0 schedule is in scope, including validation, provider failure,
completion bursts, key reuse, reset and replay. The deterministic semantic lane
is separate from the generic runtime-cost lane.

## api and syntax

The frozen protocol is:

`experiments/invocation_authority_btcpp/configs/protocol.v1.json`

It references, without modifying, these C0 inputs:

- `experiments/invocation_authority_controlled/configs/protocol.v1.json`;
- `experiments/invocation_authority_controlled/configs/fault-matrix.v1.json`;
- `experiments/invocation_authority_controlled/schedules/catalogue.v1.json`; and
- `experiments/invocation_authority_controlled/lisp/common_task.lisp`.

BehaviorTree.CPP is pinned to version 4.9.0 at commit
`3ff6a32ba0497a08519c77a1436e3b81eff1bcd6` and is built in the benchmark
build directory through CMake `FetchContent`. No system installation is
required.

The manifest labels are `MBT-ordinary`, `BTCPP-ordinary`, `MBT-full` and
`BTCPP-full`. A paper must define these implementations before using shortened
labels.

`btcpp_task_runner.hpp` exposes the comparison runner. Scheduled submissions
are queued with `request_submission()`, `tick()` advances the BehaviorTree.CPP
tree, `cancel_request()` exposes the explicit cancellation race and `reset()`
halts the tree before rebuilding its node state. `task_events()` and
`variant_events()` return separate canonical streams.

`btcpp_variant.hpp` exposes `btcpp_asynchronous_variant` and
`btcpp_invocation_scoped_variant`. The ordinary variant requests best-effort
cancellation from `onHalted()` but does not convert an acknowledgement into
logical authority. The full variant owns the terminal claim, invocation
identity, authority checks and committed target until dispatch.

## example

The equivalent task shape in both runtimes is:

```text
reactive fallback
├── emergency? → safe stand
├── model request/wait → dispatch accepted target
└── safe wait
```

The model action remains running while the provider works. A higher-priority
emergency branch halts it. The ordinary implementation requests cancellation
but does not gain generation, context or dispatch authority. The full
implementation logically revokes the invocation and rejects any later result.

Configure the optional comparison build with:

```sh
cmake --preset bench-release-btcpp
cmake --build --preset bench-release-btcpp -j
ctest --preset bench-release-btcpp \
  -R muesli_bt_controlled_authority_btcpp --output-on-failure
```

Run one complete engineering seed with:

```sh
python3 experiments/invocation_authority_btcpp/run_campaign.py \
  --output /path/to/new-comparison-directory \
  --seeds 0
```

Use `--seed-set engineering` for the frozen 32-seed engineering lane and
`--seed-set paper` for the 128-seed paper lane. The driver writes raw trials,
one manifest and canonical stream set per experimental unit, schedule and
variant summaries, a code-location inventory and reader-facing CSV and
Markdown tables. It records one paired-input digest for each schedule and seed;
all four implementations must have the same digest.

The paper gate judges both full ports against zero obsolete effects, zero false
rejections, valid evidence and deterministic replay. Ordinary asynchronous
results retain the C0 profile as a predeclared reference, but a competent
framework lifecycle is allowed to outperform that reference. Such differences
are reported as reference deviations rather than converted into failures.

## gotchas

- The C0 protocol and matrix remain unchanged. Version E1 if this comparison
  contract changes.
- Best-effort cancellation is not authority. A completion may race with or
  ignore physical cancellation.
- The two full implementations may place custom state in different locations.
  Compare observable outcomes and disclose that custom machinery.
- Canonical runtime evidence is `mbt.evt.v1`. CSV and Markdown tables are
  derived artefacts.
- Partial and engineering runs cannot pass the paper gate.
- The deterministic semantic lane records maximum tick duration for diagnosis,
  but it is not the generic runtime-cost comparison.
- Report results even when they do not favour muesli-bt.

## see also

- [controlled invocation-authority campaign](invocation-authority-controlled-campaign.md)
- [humanoid model-mediated approach](humanoid-model-mediated-approach.md)
- [known limitations](../known-limitations.md)
