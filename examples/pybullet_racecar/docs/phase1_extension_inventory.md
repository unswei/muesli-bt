# Phase 1 Extension Inventory (PyBullet Racecar)

Scope rule: move only language/runtime surface used exclusively by `examples/pybullet_racecar/**` (plus its support files/tests).

| Symbol / Surface | Definition Location | Usage Locations | Decision |
| --- | --- | --- | --- |
| Backend-specific compatibility builtins | `src/builtins.cpp` (before extraction), then the old demo extension module | historical demo path | removed (use canonical `env.api.v1`) |
| Hook-based extension registration callbacks | `include/muslisp/extensions.hpp` (before extraction) | `examples/pybullet_racecar/native/bridge_module.cpp`, `tests/test_main.cpp` | removed (replaced by explicit extension objects) |
| `racecar_state`, `racecar_tick_record`, `racecar_sim_adapter`, `racecar_loop_options`, `racecar_loop_status`, `racecar_loop_result` | `integrations/pybullet/racecar_demo.hpp` | racecar bridge/tests/example only | moved out of core build |
| `racecar_get_state`, `racecar_apply_action`, `racecar_step`, `racecar_reset`, `racecar_debug_draw`, `run_racecar_loop`, `install_racecar_demo_callbacks` | `integrations/pybullet/racecar_demo.cpp` | racecar bridge/tests/example only | moved out of core build |
| Callback names: `goal-reached-racecar`, `collision-imminent`, `avoid-obstacle`, `constant-drive`, `drive-to-goal`, `apply-action` | `integrations/pybullet/racecar_demo.cpp` | racecar BT/example/tests only | moved out of core build |
| Planner model `racecar-kinematic-v1` | `integrations/pybullet/racecar_demo.cpp` | racecar BT/example/tests only | moved out of core build |
| `install_core_builtins` | `src/builtins.cpp` | global runtime | keep |
| Core demo callbacks (`always-true`, `always-false`, `bb-has`, etc.) via `install_demo_callbacks` | `src/bt/runtime_host.cpp` | global runtime/tests | keep |
| Core planner models (`toy-1d`, `ptz-track`) | `src/bt/planner.cpp` | global runtime/tests | keep |
