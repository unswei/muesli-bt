# contributing

## what this is

This page defines the minimum local workflow for building, testing, and
documenting `muesli-bt` changes before opening a pull request.

## when to use it

Use this guide whenever you modify runtime code, integrations, docs, fixtures,
or CI configuration.

## build

Preset flow:

```bash
cmake --preset dev
cmake --build --preset dev --parallel
```

Runtime boundary check (core only, integrations disabled):

```bash
cmake --preset core-only
cmake --build --preset core-only --parallel
```

## test

```bash
ctest --preset dev
```

Core-only boundary test run:

```bash
ctest --preset core-only
```

Schema + fixture validation:

```bash
python tools/validate_event_log.py \
  --schema schema/mbt.evt.v1.schema.json \
  tests/fixtures/mbt.evt.v1/*.jsonl
```

## docs

Create/update the unified Python environment:

```bash
./scripts/setup-python-env.sh
```

Build docs in strict mode:

```bash
.venv-py311/bin/python -m mkdocs build --strict
```

Serve docs locally:

```bash
.venv-py311/bin/python -m mkdocs serve
```

## formatting tools

### clang-format

Check or format C/C++ sources:

```bash
find include src integrations tests tools -type f \
  \( -name '*.h' -o -name '*.hpp' -o -name '*.cc' -o -name '*.cpp' \) \
  -print0 | xargs -0 clang-format -i
```

### cmake-format

Format CMake files:

```bash
cmake-format -i CMakeLists.txt cmake/muesli_btConfig.cmake.in tools/consumer_smoketest/CMakeLists.txt
```

### line ending normalisation

Use the repo normaliser to keep text files GitHub-readable:

```bash
python3 scripts/normalise_text_files.py --check
python3 scripts/normalise_text_files.py --apply
```

## pull request checklist

- tests added/updated and passing
- docs updated in the same change (no drift)
- event schema and fixtures updated together when contract-relevant behaviour changes
- runtime/integration boundaries preserved (`muesli_bt::runtime` remains embeddable)
