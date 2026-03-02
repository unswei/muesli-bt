# consume as a package

## what this is

This page shows how to install `muesli-bt` from source and consume it from a
separate CMake project using `find_package(muesli_bt CONFIG REQUIRED)`.

## when to use it

Use this flow when you are:

- embedding the runtime in another application
- building inspector/tooling consumers (for example Studio adapters)
- needing a clean runtime-only package boundary without demo sources

## how it works

`muesli-bt` installs exported CMake targets and public headers under an install
prefix. Downstream projects point `CMAKE_PREFIX_PATH` at that prefix and link
against imported targets.

Core runtime target:

- `muesli_bt::runtime`

Optional integration targets (only when built and exported):

- `muesli_bt::integration_pybullet`
- `muesli_bt::integration_webots`

## api / syntax

Install from source (runtime-only boundary):

```bash
cmake -S . -B build/install-core -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PWD/build/install-root" \
  -DMUESLI_BT_BUILD_INTEGRATION_PYBULLET=OFF \
  -DMUESLI_BT_BUILD_INTEGRATION_WEBOTS=OFF \
  -DMUESLI_BT_BUILD_PYTHON_BRIDGE=OFF \
  -DMUESLI_BT_BUILD_WEBOTS_EXAMPLES=OFF
cmake --build build/install-core --parallel
cmake --install build/install-core
```

Consume from another project:

```cmake
find_package(muesli_bt CONFIG REQUIRED)

add_executable(app main.cpp)
target_link_libraries(app PRIVATE muesli_bt::runtime)
```

Optional integration probe pattern:

```cmake
if(TARGET muesli_bt::integration_pybullet)
  target_link_libraries(app PRIVATE muesli_bt::integration_pybullet)
endif()

if(TARGET muesli_bt::integration_webots)
  target_link_libraries(app PRIVATE muesli_bt::integration_webots)
endif()
```

Recommended consumer preset shape:

```json
{
  "version": 6,
  "configurePresets": [
    {
      "name": "dev",
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/build/dev",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Debug",
        "CMAKE_PREFIX_PATH": "/abs/path/to/muesli-bt/build/install-root"
      }
    }
  ]
}
```

## example

Minimal configure + build in a downstream consumer:

```bash
cmake -S . -B build/dev -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH="/abs/path/to/muesli-bt/build/install-root"
cmake --build build/dev --parallel
```

Studio/tooling contract assets are installed with the package and exposed via
`muesli_bt_SHARE_DIR` in `muesli_btConfig.cmake`.

## gotchas

- `muesli_bt::integration_webots` is omitted when Webots SDK is unavailable.
- enabling `MUESLI_BT_BUILD_PYTHON_BRIDGE` requires PyBullet integration.
- do not link against non-exported internal targets; only use imported
  `muesli_bt::*` targets.

## see also

- [getting started](getting-started.md)
- [muesli-studio integration contract](contracts/muesli-studio-integration.md)
- [contracts index](contracts/README.md)
