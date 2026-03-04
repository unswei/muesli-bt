# Writing A Backend

This guide shows the minimum work needed to connect a new simulator or robot.

A backend is your [host](../terminology.md#host) integration layer for `env.*`.

## Current State

- Implemented: `env.api.v1` core capability built-ins (`env.info`, `env.attach`, `env.configure`, `env.reset`, `env.observe`, `env.act`, `env.step`, `env.run-loop`, `env.debug-draw`)
- Implemented: `pybullet` backend adapter (racecar demo path)
- Implemented: `webots` backend adapter (e-puck demo path, `reset=false`)
- Implemented: ROS2 backend skeleton extension target (`muesli_bt::integration_ros2`)
- Planned: ROS2 topic/action/service transport binding and conformance expansion (scope defined in [ROS2 backend scope](ros2-backend-scope.md))

Validation references:

- env core contract tests: `tests/test_main.cpp` (`test_env_core_interface_unattached`)
- env generic backend contract tests: `tests/test_main.cpp` (`test_env_generic_pybullet_backend_contract`, `test_env_generic_ros2_backend_contract`)
- multi-episode run-loop tests: `tests/test_main.cpp` (`test_env_run_loop_multi_episode_reset_true`, `test_env_run_loop_multi_episode_reset_false`)
- pybullet backend extension: `integrations/pybullet/extension.cpp`
- webots backend extension: `integrations/webots/extension.cpp`

## Stable C++ Attach API

The supported attach path for integrations is:

1. Link `muesli_bt::runtime` and integration target (for example `muesli_bt::integration_pybullet` or `muesli_bt::integration_webots`).
2. Register integration extension through `muslisp::runtime_config::register_extension(...)`.
3. Create environment through `muslisp::create_global_env(std::move(config))`.
4. Attach backend in Lisp via `(env.attach "backend-name")`.
5. Attach simulator/hardware adapter through integration public API where required (for example `bt::set_racecar_sim_adapter(...)`).

Public headers for this flow:

- `muslisp/eval.hpp`
- `muslisp/extensions.hpp`
- `bt/runtime_host.hpp`
- `bt/event_log.hpp`
- integration headers (PyBullet: `pybullet/extension.hpp`, `pybullet/racecar_demo.hpp`; Webots: `webots/extension.hpp`; ROS2 skeleton: `ros2/extension.hpp`)

Event callback interface for inspectors:

- `bt::event_log::set_line_listener(...)`: consume canonical pre-serialised `mbt.evt.v1` JSONL lines.
- `bt::event_log::serialise_event_line(...)`: canonical envelope serialiser for parity checks.

## Minimal Responsibilities

1. expose `env.info` with backend name + schema ids
2. implement `env.observe` and return a stable observation map
3. implement `env.act` with action validation/clamping
4. implement `env.step` for your timing model
5. provide a safe fallback action path
6. optionally implement `env.reset`

## Key Naming Contract

Use one naming scheme across all backends:

- observation schema key: `obs_schema`
- action schema key: `action_schema`
- state schema key (when state vectors/maps are present): `state_schema`

Avoid introducing alternate names for the same concept in backend docs or examples.

## Observation Path (`env.observe`)

- read sensors (camera, lidar, proprioception, state estimator)
- build observation map with explicit `obs_schema` and (when applicable) `state_schema`
- include timestamp/tick metadata where useful
- keep shapes and units stable across runs

Example map shape:

```lisp
(map.make
  'obs_schema "racecar.obs.v1"
  'state_schema "racecar.state.v1"
  'state_vec (list x y yaw speed)
  'rays ray_distances
  't_ms now_ms)
```

## Action Path (`env.act`)

- define canonical action schema (`action.u.v1` or backend-specific id)
- validate dimension and numeric finiteness
- clamp to bounds before applying
- reject malformed actions and apply fallback safely

## Step Path (`env.step`)

Simulation backend:

- advance simulation by N physics steps per control tick

Real-time backend:

- run one cycle at control frequency and enforce pacing

Hardware backend:

- execute one sensor-read -> control-write iteration with watchdogs

## Reset Semantics (`env.reset`, optional)

If reset exists, document exactly what resets:

- world/robot state
- controller integrators/state estimators
- episode counters/logging context

## Safe Fallback

Always define a backend-side safe action (for example zero throttle + neutral steering).

Use it when:

- control branch returns failure and no action was published
- planner status is non-`ok`
- deadlines are missed
- action payload is invalid

## Backend Checklist

Implemented:
- [x] `env.info` advertises backend capabilities
- [x] `env.observe` returns stable schema maps with episode/step metadata
- [x] `env.act` validates payloads and clamps bounds in demo backends
- [x] `env.step` advances exactly one control increment
- [x] fallback/last-good action handling in `env.run-loop`
- [x] tick log records include action and budget timings
- [x] reproducible demo scripts exist (PyBullet racecar, Webots e-puck variants)

Planned:
- [x] dedicated ROS2 backend target and packaging skeleton
- [ ] additional production backends beyond demo-focused adapters

Known limitations:
- [ ] Webots backend currently reports `reset=false`

## Minimal Template

```cpp
struct my_backend {
  obs_map observe();
  void act(const action_vec& u);
  void step();
  void reset(); // optional

  action_vec safe_action() const { return {0.0, 0.0}; }
};

// loop shape
while (running) {
  auto obs = backend.observe();
  blackboard_put("obs", obs);

  auto st = bt_tick(inst);
  auto action = blackboard_get_or("action", backend.safe_action());

  backend.act(action);
  backend.step();
}
```

## See Also

- [Integration Overview](overview.md)
- [Environment API (`env.*`)](env-api.md)
- [ROS2 Backend Scope](ros2-backend-scope.md)
- [Sensing And Blackboard](sensing-and-blackboard.md)
