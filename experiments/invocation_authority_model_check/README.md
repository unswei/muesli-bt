# finite invocation-authority model

This directory contains a small TLA+ model of one asynchronous invocation.
It explores the complete reachable state graph of a finite abstraction
involving branch exit and re-entry,
generation supersession, context change, timeout, cancellation, completion,
admission, and dispatch.

The model is intentionally narrow. It checks the safety shape of the
execution contract; it does not prove the C++ implementation correct for all
thread interleavings, nor does it decide whether a host selected an
appropriate context-equivalence relation.

## finite domains and property

The model has one invocation and one result. Its explicit finite domains are:

- five request lifecycle states: `idle`, `issued`, `admitted`, `rejected`, and
  `dispatched`;
- Boolean owner, deadline, cancellation, readiness, admission, consumption,
  and safety-marker variables;
- two values, `{0, 1}`, for each current and captured entry epoch, generation,
  and context token; and
- completion-claim count `{0, 1, 2}`, saturated at two because two is the
  first unsafe value.

Each current token can diverge from its captured value once. This represents
the one relevant branch re-entry, supersession, or context change for the
modelled invocation while retaining an ABA-style reused logical slot. The
model does not approximate unbounded production counters with wraparound.

`Safety` is the conjunction

```tla
/\ ~badAdmission
/\ ~badDispatch
/\ completionClaims <= 1
/\ (IF UseConsumeLatch THEN consumed => requestState = "dispatched" ELSE TRUE)
```

`badAdmission` records acceptance without the full owner, cancellation,
deadline, epoch, generation, and context authority relation. `badDispatch`
records dispatch without that relation, or repeated dispatch when the consume
latch is removed. `TypeOK` separately checks every finite domain.

## running TLC

Download `tla2tools.jar` from the TLA+ release distribution, then run from
this directory:

```sh
python3 run_tlc.py --jar /path/to/tla2tools.jar
```

`full.cfg` should reach a fixed point and report that `Safety` holds. Each
weakened configuration should produce a short counterexample: obsolete
admission for the deadline, entry-epoch, generation, and context variants;
obsolete dispatch when dispatch revalidation is removed; and more than one
completion/consume effect when the terminal and consume latches are removed.

With TLC 2.19, the full contract reaches a fixed point after generating 436
states and finding 186 distinct reachable states, with zero states left on the
queue. The complete state graph has depth 11. TLC reports no `TypeOK` or
`Safety` violation. The fingerprint-collision probability estimate for this
run was $2.5\times10^{-15}$.

TLC uses breadth-first search, so the reported mutant traces are shortest
counterexamples. Length counts transitions from the initial state:

| Configuration | Transitions | Shortest witness |
|---|---:|---|
| `deadline_only.cfg` | 4 | `Start`, `BranchExit`, `Complete`, `AdmitAccepted` |
| `missing_entry_epoch.cfg` | 5 | `Start`, `BranchExit`, `Reenter`, `Complete`, `AdmitAccepted` |
| `missing_generation.cfg` | 4 | `Start`, `Supersede`, `Complete`, `AdmitAccepted` |
| `missing_context.cfg` | 4 | `Start`, `ContextChange`, `Complete`, `AdmitAccepted` |
| `missing_dispatch_revalidation.cfg` | 5 | `Start`, `Complete`, `AdmitAccepted`, `BranchExit`, `Dispatch` |
| `missing_terminal_latch.cfg` | 3 | `Start`, `Complete`, `DuplicateComplete` |

The runner asserts the fixed-point counts and every action sequence. It also
checks the final generation-mismatch state explicitly, so an unrelated
`Safety` violation cannot satisfy that mutation. The `-deadlock` command-line
option disables deadlock reporting because rejected and dispatched lifecycle
states are intentionally terminal; TLC still explores those reachable states.

For an individual TLC run, pass the configuration explicitly:

```sh
java -cp /path/to/tla2tools.jar tlc2.TLC -deadlock -config full.cfg InvocationAuthority
```
