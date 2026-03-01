# Diagrams

muesli-bt docs use Graphviz DOT sources for architecture/workflow diagrams.

## Layout

- DOT sources: `docs/diagrams/src/*.dot`
- generated SVG: `docs/diagrams/gen/*.svg`

## Rendering

Render all diagrams:

```bash
./scripts/gen_diagrams.sh
```

Alternative Python entrypoint:

```bash
python3 scripts/render-doc-diagrams.py
```

The docs build also runs the Python renderer automatically through an MkDocs pre-build hook.

CI verifies that each committed `docs/diagrams/src/*.dot` has a committed matching
`docs/diagrams/gen/*.svg`.

## Tooling

- required tool: Graphviz (`dot`)
- on macOS: `brew install graphviz`
- on Ubuntu: `sudo apt-get install graphviz`

## Authoring Guidance

- keep diagrams focused on one concept per figure
- prefer stable node names and minimal edge crossings
- avoid mixing too many runtime levels in one graph
