# feature inventory

This page keeps the compact feature inventory that used to live in the root README. Use it when you want a broad map of the released runtime surface before reading individual reference pages.

## language runtime

- small Lisp core with closures and lexical scoping;
- non-moving mark/sweep GC;
- mutable `vec` and `map` containers;
- deterministic RNG (`rng.make`, `rng.uniform`, `rng.normal`, `rng.int`);
- numeric helpers (`sqrt`, `log`, `exp`, `atan2`, `abs`, `clamp`);
- monotonic time (`time.now-ms`);
- JSON built-ins (`json.encode`, `json.decode`).

## behaviour tree runtime

- memoryless composites: `seq`, `sel`;
- memoryful composites: `mem-seq`, `mem-sel`;
- yielding/reactive composites: `async-seq`, `reactive-seq`, `reactive-sel`;
- decorators and leaves: `invert`, `repeat`, `retry`, `cond`, `act`, `succeed`, `fail`, `running`;
- compile/load/save paths: `bt.compile`, `bt.to-dsl`, `bt.save`, `bt.load`, `bt.save-dsl`, `bt.load-dsl`;
- per-instance blackboard with typed values and write metadata;
- scheduler-backed async actions and reactive pre-emption tracing.

## bounded-time planning

- `plan-action` node for strict per-tick budgeted planning;
- `planner.plan` service for direct scripted experiments;
- backend selection across MCTS, MPPI, and iLQR;
- structured planner stats and logs.

## capability and VLA layer

- capability registry: `cap.list`, `cap.describe`, `cap.call`;
- deterministic capability fixture: `cap.echo.v1`;
- async job API: `vla.submit`, `vla.poll`, `vla.cancel`;
- stream-aware polling statuses: `:queued`, `:running`, `:streaming`, `:done`, `:error`, `:timeout`, `:cancelled`;
- opaque media handles plus metadata accessors;
- structured per-job logs.

Production VLA provider transport, credentials, and robot safety validation are host-side work unless a concrete backend is documented and tested.

## environment capability layer

The canonical backend-agnostic control surface is:

- `env.info`, `env.attach`, `env.configure`;
- `env.reset`, `env.observe`, `env.act`, `env.step`;
- `env.run-loop`, `env.debug-draw`.

Backends are registered through explicit integration extensions. Avoid legacy backend-specific Lisp namespaces for new integrations.

## observability and control

- canonical `mbt.evt.v1` event logs;
- trace/log rings and dump APIs;
- per-tree and per-node profiling stats;
- scheduler stats and tick budget controls;
- schema and trace validation tools.

## package and tooling integration

`muesli-bt` installs a CMake package for runtime consumers and tooling such as [`muesli-studio`](https://github.com/unswei/muesli-studio).

Start with:

- [consume as a package](../getting-started-consume.md);
- [runtime contract v1](../contracts/runtime-contract-v1.md);
- [muesli-studio integration contract](../contracts/muesli-studio-integration.md).

Optional integration targets may also be exported when enabled at build time:

- `muesli_bt::integration_pybullet`;
- `muesli_bt::integration_webots`;
- `muesli_bt::integration_ros2`.

## repository layout

- `src/`, `include/`: core runtime, evaluator, BT engine, planner, VLA integration;
- `integrations/`: optional backend integrations such as PyBullet, Webots, and ROS2;
- `examples/bt/`: compact BT scripts;
- `examples/repl_scripts/`: end-to-end experiments and demos;
- `examples/pybullet_racecar/`: racecar demo package;
- `examples/pybullet_epuck_goal/`: differential-drive PyBullet surrogate for the shared wheeled flagship BT;
- `tests/`: unit, integration, and conformance coverage;
- `docs/`: user, evidence, and internals documentation.

## see also

- [language reference index](../language/reference/index.md)
- [BT introduction](../bt/intro.md)
- [planning overview](../planning/overview.md)
- [integration overview](../integration/overview.md)
- [observability first](observability-first.md)
