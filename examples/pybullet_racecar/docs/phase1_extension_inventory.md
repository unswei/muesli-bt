# Phase 1 Extension Inventory (PyBullet Racecar)

Scope rule: move only language/runtime surface used exclusively by `examples/pybullet_racecar/**` (plus its support files/tests).

| Symbol / Surface | Definition Location | Usage Locations | Decision |
| --- | --- | --- | --- |
| `env.pybullet.get-state`, `env.pybullet.apply-action`, `env.pybullet.step`, `env.pybullet.reset`, `env.pybullet.debug-draw`, `env.pybullet.run-loop` (bridged by canonical `env.*`) | `src/builtins.cpp` (before extraction), now `examples/pybullet_racecar_common/extension.cpp` | `tests/test_main.cpp`, racecar docs/example support | move |
| `install_racecar_demo_builtins` | `include/muslisp/builtins.hpp`, `src/builtins.cpp` (before extraction) | `examples/pybullet_racecar/native/bridge_module.cpp`, `tests/test_main.cpp` | move (replaced by generic hook registrar) |
| `racecar_state`, `racecar_tick_record`, `racecar_sim_adapter`, `racecar_loop_options`, `racecar_loop_status`, `racecar_loop_result` | `examples/pybullet_racecar/native/racecar_demo.hpp` | racecar bridge/tests/example only | move out of core build (remain in extension target) |
| `racecar_get_state`, `racecar_apply_action`, `racecar_step`, `racecar_reset`, `racecar_debug_draw`, `run_racecar_loop`, `install_racecar_demo_callbacks` | `examples/pybullet_racecar/native/racecar_demo.cpp` | racecar bridge/tests/example only | move out of core build (remain in extension target) |
| Callback names: `goal-reached-racecar`, `collision-imminent`, `avoid-obstacle`, `constant-drive`, `drive-to-goal`, `apply-action` | `examples/pybullet_racecar/native/racecar_demo.cpp` | racecar BT/example/tests only | move out of core build (remain in extension target) |
| Planner model `racecar-kinematic-v1` | `examples/pybullet_racecar/native/racecar_demo.cpp` | racecar BT/example/tests only | move out of core build (remain in extension target) |
| `install_core_builtins` | `src/builtins.cpp` | global runtime | keep |
| Core demo callbacks (`always-true`, `always-false`, `bb-has`, etc.) via `install_demo_callbacks` | `src/bt/runtime_host.cpp` | global runtime/tests | keep |
| Core planner models (`toy-1d`, `ptz-track`) | `src/bt/planner.cpp` | global runtime/tests | keep |
