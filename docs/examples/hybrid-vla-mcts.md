# Example: Hybrid VLA Prior + MCTS

This script requests a VLA action proposal, then feeds it into `planner.mcts` as a prior mixture sampler.

Script path:

- `examples/repl_scripts/hybrid-vla-mcts-1d.lisp`

Run:

```bash
./build/dev/muslisp examples/repl_scripts/hybrid-vla-mcts-1d.lisp
```

What to look for:

- `vla.submit` + `vla.poll` produce a structured action proposal
- planner request uses `action_sampler = "vla_mixture"`
- prior parameters (`action_prior_mean`, `action_prior_sigma`, `action_prior_mix`) influence root sampling
