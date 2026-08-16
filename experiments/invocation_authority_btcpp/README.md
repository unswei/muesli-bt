# BehaviorTree.CPP invocation-authority comparison

## status

External-comparison gate E1 is frozen as protocol
`controlled-authority.btcpp-comparison.e1.v1`. The protocol was frozen before
the BehaviorTree.CPP authority adapters were implemented.

## purpose

This experiment tests whether ordinary asynchronous lifecycle behaviour and
the full invocation-scoped authority contract produce the same task effects in
muesli-bt and BehaviorTree.CPP. It also freezes a separate generic performance
lane. Performance results must not be presented as authority results.

The comparison reuses the C0 task, schedules, scripted proposal boundary, host
validation and independent oracle. Internal schedule identifiers remain
manifest keys only. Reader-facing tables use the schedule descriptions.

See [the public experiment contract](../../docs/examples/invocation-authority-btcpp-comparison.md).
