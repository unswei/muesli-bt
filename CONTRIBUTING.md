# contributing

Keep changes small, testable, and documented in the same pull request.

## build

```bash
cmake --preset dev
cmake --build --preset dev --parallel
```

Core/runtime boundary check (no integrations):

```bash
cmake --preset core-only
cmake --build --preset core-only --parallel
```

## test

```bash
ctest --preset dev
ctest --preset core-only
```

Validate canonical event fixtures:

```bash
python tools/validate_event_log.py \
  --schema schema/mbt.evt.v1.schema.json \
  tests/fixtures/mbt.evt.v1/*.jsonl
```

## docs

```bash
./scripts/setup-python-env.sh
.venv-py311/bin/python -m mkdocs build --strict
```

## formatting

Format C/C++:

```bash
find include src integrations tests tools -type f \
  \( -name '*.h' -o -name '*.hpp' -o -name '*.cc' -o -name '*.cpp' \) \
  -print0 | xargs -0 clang-format -i
```

Format CMake:

```bash
uvx --with pyyaml --from cmakelang cmake-format -i \
  CMakeLists.txt \
  cmake/muesli_btConfig.cmake.in \
  tools/consumer_smoketest/CMakeLists.txt
```

Normalise tracked text files:

```bash
python3 scripts/normalise_text_files.py --check
python3 scripts/normalise_text_files.py --apply
```

## boundaries

- keep `muesli_bt::runtime` embeddable and free of demo/example source dependencies
- keep integrations optional (`muesli_bt::integration_pybullet`, `muesli_bt::integration_webots`)
- keep demo-specific wiring out of core runtime registration paths

## pull request checklist

- tests added or updated and passing
- docs updated in the same change (no drift)
- event schema and fixtures updated together when runtime behaviour changes
