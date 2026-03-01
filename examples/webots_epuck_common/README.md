# Shared Webots e-puck Controller

This folder contains the shared C++ controller implementation used by:

- `examples/webots_epuck_obstacle/controllers/muesli_epuck/muesli_epuck.cpp`
- `examples/webots_epuck_line/controllers/muesli_epuck/muesli_epuck.cpp`
- `examples/webots_epuck_goal/controllers/muesli_epuck/muesli_epuck.cpp`
- `examples/webots_epuck_foraging/controllers/muesli_epuck/muesli_epuck.cpp`
- `examples/webots_epuck_tag/controllers/muesli_epuck/muesli_epuck.cpp`

The per-demo `muesli_epuck.cpp` files are intentionally small wrappers. This keeps behaviour logic in Lisp while avoiding duplicated controller boilerplate.
The corresponding Lisp entrypoints follow the `.lisp` naming convention.
