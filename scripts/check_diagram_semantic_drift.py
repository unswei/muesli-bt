#!/usr/bin/env python3
from __future__ import annotations

import argparse
import shutil
import subprocess
import tempfile
import xml.etree.ElementTree as ET
from collections import Counter
from dataclasses import dataclass
from pathlib import Path


ROOT_DIR = Path(__file__).resolve().parent.parent
SRC_DIR = ROOT_DIR / "docs" / "diagrams" / "src"
GEN_DIR = ROOT_DIR / "docs" / "diagrams" / "gen"
SVG_NS = "{http://www.w3.org/2000/svg}"


@dataclass(frozen=True)
class SvgSemantics:
    titles: tuple[tuple[str, int], ...]
    texts: tuple[tuple[str, int], ...]


def _normalise_text(text: str | None) -> str:
    if not text:
        return ""
    return " ".join(text.split())


def _extract_semantics(svg_path: Path) -> SvgSemantics:
    tree = ET.parse(svg_path)
    root = tree.getroot()

    titles: Counter[str] = Counter()
    texts: Counter[str] = Counter()

    for title in root.iter(f"{SVG_NS}title"):
        norm = _normalise_text(title.text)
        if norm:
            titles[norm] += 1

    for text in root.iter(f"{SVG_NS}text"):
        norm = _normalise_text("".join(text.itertext()))
        if norm:
            texts[norm] += 1

    return SvgSemantics(
        titles=tuple(sorted(titles.items())),
        texts=tuple(sorted(texts.items())),
    )


def _render_dot(dot_path: Path, output_svg: Path) -> None:
    subprocess.run(["dot", "-Tsvg", str(dot_path), "-o", str(output_svg)], check=True)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Check semantic drift between DOT sources and committed SVG diagrams."
    )
    parser.add_argument("--src-dir", type=Path, default=SRC_DIR)
    parser.add_argument("--gen-dir", type=Path, default=GEN_DIR)
    args = parser.parse_args()

    if shutil.which("dot") is None:
        print("error: Graphviz 'dot' is required")
        return 1

    dot_files = sorted(args.src_dir.glob("*.dot"))
    if not dot_files:
        print(f"error: no DOT files found in {args.src_dir}")
        return 1

    failed = False
    with tempfile.TemporaryDirectory(prefix="mbt-diagram-check-") as tmp_dir:
        tmp_root = Path(tmp_dir)
        for dot_path in dot_files:
            svg_name = f"{dot_path.stem}.svg"
            committed_svg = args.gen_dir / svg_name
            rendered_svg = tmp_root / svg_name

            if not committed_svg.exists():
                print(f"error: missing committed SVG for {dot_path.name}: {committed_svg}")
                failed = True
                continue

            _render_dot(dot_path, rendered_svg)
            committed_semantics = _extract_semantics(committed_svg)
            rendered_semantics = _extract_semantics(rendered_svg)

            if committed_semantics != rendered_semantics:
                print(f"error: semantic drift detected for {svg_name}")
                failed = True
            else:
                print(f"ok: {svg_name}")

    if failed:
        print("diagram semantic drift check failed")
        return 1

    print("all diagram semantics are in sync")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
