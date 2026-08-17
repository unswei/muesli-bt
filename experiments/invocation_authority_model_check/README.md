# bounded invocation-authority model

This directory contains a small TLA+ model of one asynchronous invocation.
It explores bounded event schedules involving branch exit and re-entry,
generation supersession, context change, timeout, cancellation, completion,
admission, and dispatch.

The model is intentionally narrow. It checks the safety shape of the
execution contract; it does not prove the C++ implementation correct for all
thread interleavings, nor does it decide whether a host selected an
appropriate context-equivalence relation.

## running TLC

Download `tla2tools.jar` from the TLA+ release distribution, then run from
this directory:

```sh
python3 run_tlc.py --jar /path/to/tla2tools.jar
```

`full.cfg` should report that `Safety` holds. Each weakened configuration
should produce a short counterexample: obsolete admission for the deadline,
entry-epoch, generation, and context variants; obsolete dispatch when dispatch
revalidation is removed; and more than one completion/consume effect when
the terminal and consume latches are removed.

The runner additionally checks that `missing_generation.cfg` reaches the
supersession witness: generation zero is captured, the live slot advances to
generation one, and the old result is nevertheless admitted. This makes the
generation mutation evidence specific rather than accepting an unrelated
`Safety` violation.

With TLC 2.19, the shortest observed witness is `Start`, `Supersede`,
`Complete`, `AdmitAccepted`. At step 4, `capturedGeneration = 0`,
`currentGeneration = 1`, `requestState = "admitted"`, and
`badAdmission = TRUE`. TLC reports the invariant violation at search depth 5
after generating 105 states and retaining 60 distinct states.

`MaxSteps = 8` is a deliberately small bound. The configurations use TLC's
state constraint to exhaustively explore all schedules up to that bound.

For an individual TLC run, pass the configuration explicitly:

```sh
java -cp /path/to/tla2tools.jar tlc2.TLC -deadlock -config full.cfg InvocationAuthority
```
