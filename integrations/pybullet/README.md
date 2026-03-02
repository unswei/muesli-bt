# pybullet integration

This folder contains the optional PyBullet integration library for `muesli-bt`.

- `extension.hpp` / `extension.cpp`: extension object factory + backend registration for `env.api.v1`.
- `racecar_demo.hpp` / `racecar_demo.cpp`: racecar simulation adapter contract and demo callback/model helpers.

Examples should use this integration target rather than compiling integration code from `examples/`.
