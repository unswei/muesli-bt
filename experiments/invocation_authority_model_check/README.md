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
entry-epoch, and context variants; obsolete dispatch when dispatch
revalidation is removed; and more than one completion/consume effect when
the terminal and consume latches are removed.

`MaxSteps = 8` is a deliberately small bound. The configurations use TLC's
state constraint to exhaustively explore all schedules up to that bound.

For an individual TLC run, pass the configuration explicitly:

```sh
java -cp /path/to/tla2tools.jar tlc2.TLC -deadlock -config full.cfg InvocationAuthority
```
