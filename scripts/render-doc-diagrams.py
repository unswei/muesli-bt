#!/usr/bin/env python3
from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
SRC_DIR = ROOT / "docs" / "diagrams" / "src"
OUT_DIR = ROOT / "docs" / "diagrams" / "gen"


def render_dot_to_svg(dot_path: Path, svg_path: Path) -> None:
    svg_path.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        ["dot", "-Tsvg", str(dot_path), "-o", str(svg_path)],
        check=True,
        capture_output=True,
        text=True,
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="Render docs DOT diagrams to SVG.")
    parser.add_argument("--force", action="store_true", help="Render all diagrams even if outputs look up-to-date.")
    args = parser.parse_args()

    dot_bin = shutil.which("dot")
    dot_files = sorted(SRC_DIR.glob("*.dot"))
    if not dot_files:
        print(f"No DOT files found in {SRC_DIR}")
        return 0

    if dot_bin is None:
        missing = []
        for dot_path in dot_files:
            svg_path = OUT_DIR / f"{dot_path.stem}.svg"
            if not svg_path.exists():
                missing.append(svg_path)
        if missing:
            print("error: Graphviz 'dot' not found and generated SVGs are missing:")
            for path in missing:
                print(f"  - {path}")
            print("Install Graphviz (for example: brew install graphviz or apt-get install graphviz).")
            return 1
        print("warning: Graphviz 'dot' not found; using existing generated SVGs.")
        return 0

    rendered = 0
    for dot_path in dot_files:
        svg_path = OUT_DIR / f"{dot_path.stem}.svg"
        if not args.force and svg_path.exists() and svg_path.stat().st_mtime >= dot_path.stat().st_mtime:
            continue
        render_dot_to_svg(dot_path, svg_path)
        rendered += 1
        print(f"rendered: {dot_path} -> {svg_path}")

    if rendered == 0:
        print("diagrams up to date")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
