#!/usr/bin/env python3
"""Check docs snippet freshness for example Lisp sources.

This check keeps docs snippets tied to source files by validating that:
1) every docs include directive pointing at `examples/**` resolves to an existing file
2) every mentioned `examples/*.lisp` path in docs is embedded via `--8<--` include
"""

from __future__ import annotations

import re
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DOCS_DIR = ROOT / "docs"
README_PATH = ROOT / "README.md"

INCLUDE_RE = re.compile(r'--8<--\s+"([^"]+)"')
EXAMPLE_LISP_REF_RE = re.compile(r"\b(examples/[A-Za-z0-9_./-]+\.lisp)\b")


def iter_markdown_files() -> list[Path]:
    files = [README_PATH]
    files.extend(sorted(DOCS_DIR.rglob("*.md")))
    return files


def rel(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


def main() -> int:
    include_refs: dict[str, set[str]] = defaultdict(set)
    lisp_refs: dict[str, set[str]] = defaultdict(set)
    errors: list[str] = []

    for doc_path in iter_markdown_files():
        text = doc_path.read_text(encoding="utf-8")
        doc_rel = rel(doc_path)

        for target in INCLUDE_RE.findall(text):
            if not target.startswith("examples/"):
                continue
            include_refs[target].add(doc_rel)
            if not (ROOT / target).is_file():
                errors.append(f"missing include source: {target} (referenced by {doc_rel})")

        for target in EXAMPLE_LISP_REF_RE.findall(text):
            lisp_refs[target].add(doc_rel)
            if not (ROOT / target).is_file():
                errors.append(f"broken docs example path: {target} (referenced by {doc_rel})")

    missing_embed = sorted(path for path in lisp_refs if path not in include_refs)
    if missing_embed:
        for path in missing_embed:
            refs = ", ".join(sorted(lisp_refs[path]))
            errors.append(
                "docs snippet freshness: example path is referenced but not embedded via include: "
                f"{path} (referenced by {refs})"
            )

    if errors:
        for err in errors:
            print(f"error: {err}")
        print("docs snippet freshness check failed")
        return 1

    print(
        "docs snippet freshness ok "
        f"(includes={len(include_refs)}, example_refs={len(lisp_refs)})"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
