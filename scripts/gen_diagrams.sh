#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC_DIR="$ROOT_DIR/docs/diagrams/src"
GEN_DIR="$ROOT_DIR/docs/diagrams/gen"

if ! command -v dot >/dev/null 2>&1; then
  echo "error: Graphviz 'dot' is required" >&2
  exit 1
fi

mkdir -p "$GEN_DIR"

found=0
for dot_file in "$SRC_DIR"/*.dot; do
  [ -e "$dot_file" ] || continue
  found=1
  base_name="$(basename "$dot_file" .dot)"
  svg_file="$GEN_DIR/$base_name.svg"
  dot -Tsvg "$dot_file" -o "$svg_file"
  echo "rendered: $dot_file -> $svg_file"
done

if [ "$found" -eq 0 ]; then
  echo "no DOT files found in $SRC_DIR"
fi
