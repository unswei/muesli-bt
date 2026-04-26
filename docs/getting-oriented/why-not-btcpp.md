# why not just BehaviorTree.CPP?

BehaviorTree.CPP is mature and should be used when you need a mainstream C++ Behaviour Tree library with a broad ecosystem.

`muesli-bt` is different because it treats these as one runtime contract:

- Behaviour Tree execution;
- bounded planning;
- cancellable async/model calls;
- canonical event logs;
- replay/conformance.

## where BehaviorTree.CPP is stronger

- ecosystem maturity;
- adoption;
- XML tooling;
- ROS/Nav2 familiarity;
- production use.

## where muesli-bt is trying to be different

- Lisp-authored runtime task logic;
- explicit event contract;
- tick-budget-oriented planning;
- async job lifecycle as a logged runtime concept;
- deterministic replay as a first-class artefact.

## when to choose each

Use BehaviorTree.CPP if you need the most established C++ BT ecosystem, existing XML tooling, or the common Nav2 BT workflow.

Use `muesli-bt` if you need inspectable runtime task logic, bounded planner/model calls, and canonical execution traces as part of the same contract.

This is not a claim that BehaviorTree.CPP lacks async support. It is a distinction between a broad BT library ecosystem and a smaller runtime that centres logs, budgets, cancellation, and replay in its public contract.
