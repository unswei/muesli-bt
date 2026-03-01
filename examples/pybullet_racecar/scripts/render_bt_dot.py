#!/usr/bin/env python3
from __future__ import annotations

import argparse
import shutil
import subprocess
import tempfile
from pathlib import Path


def lisp_string_literal(text: str) -> str:
    escaped = text.replace("\\", "\\\\").replace('"', '\\"')
    return f"\"{escaped}\""


def parse_args() -> argparse.Namespace:
    here = Path(__file__).resolve().parent
    demo_root = here.parent
    repo_root = demo_root.parent.parent
    parser = argparse.ArgumentParser(description="Export racecar BT to DOT and render with Graphviz.")
    parser.add_argument("--bt", type=Path, default=demo_root / "bt" / "racecar_bt.lisp")
    parser.add_argument("--muslisp", type=Path, default=repo_root / "build" / "dev" / "muslisp")
    parser.add_argument("--out", type=Path, default=demo_root / "out" / "bt.svg")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    bt_path = args.bt.resolve()
    muslisp_path = args.muslisp.resolve()
    out_path = args.out.resolve()
    out_path.parent.mkdir(parents=True, exist_ok=True)

    if not bt_path.exists():
        raise FileNotFoundError(f"BT file not found: {bt_path}")
    if not muslisp_path.exists():
        raise FileNotFoundError(f"muslisp binary not found: {muslisp_path}")

    dot_path = out_path if out_path.suffix.lower() == ".dot" else out_path.with_suffix(".dot")

    script = (
        "(begin "
        f"(define tree (bt.load-dsl {lisp_string_literal(str(bt_path))})) "
        f"(bt.export-dot tree {lisp_string_literal(str(dot_path))}))"
    )

    with tempfile.NamedTemporaryFile(mode="w", suffix=".lisp", delete=False, encoding="utf-8") as temp_file:
        temp_file.write(script)
        script_path = Path(temp_file.name)

    try:
        subprocess.run([str(muslisp_path), str(script_path)], check=True)
    finally:
        script_path.unlink(missing_ok=True)

    if out_path.suffix.lower() == ".dot":
        print(f"DOT exported: {dot_path}")
        return 0

    dot_bin = shutil.which("dot")
    if dot_bin is None:
        print(f"Graphviz `dot` not found. DOT file is available at: {dot_path}")
        return 0

    format_name = out_path.suffix.lstrip(".").lower()
    subprocess.run([dot_bin, f"-T{format_name}", str(dot_path), "-o", str(out_path)], check=True)
    print(f"Rendered: {out_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
