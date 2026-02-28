#!/usr/bin/env python3
"""Normalise line endings in tracked text files.

What it does:
- Converts CRLF and CR-only line endings to LF.
- Ensures files end with a single trailing newline.
What it does NOT do:
- It does not change indentation.
- It does not trim trailing whitespace.
- It does not re-encode files (expects UTF-8 or ASCII-compatible text).

Usage:
  python3 scripts/normalise_text_files.py --check
  python3 scripts/normalise_text_files.py --apply
"""

from __future__ import annotations

import argparse
import subprocess
from pathlib import Path


TEXT_EXTS = {
    ".c", ".cc", ".cpp", ".h", ".hpp", ".hh", ".inl",
    ".py", ".md",
    ".yml", ".yaml", ".json", ".toml",
    ".cmake", ".txt",
    ".sh", ".bash", ".zsh",
    ".lisp", ".scm", ".clj",
    ".ini", ".cfg",
}


def git_tracked_files(repo_root: Path) -> list[Path]:
    out = subprocess.check_output(
        ["git", "ls-files"],
        cwd=str(repo_root),
        text=True,
    )
    return [repo_root / line.strip() for line in out.splitlines() if line.strip()]


def looks_binary(data: bytes) -> bool:
    # Heuristic: if there is a NUL byte, treat as binary.
    return b"\x00" in data


def should_consider(path: Path) -> bool:
    if path.is_symlink() or not path.is_file():
        return False
    if path.suffix.lower() in TEXT_EXTS:
        return True
    # Also catch common root files without extensions.
    if path.name in {"CMakeLists.txt", "LICENSE"}:
        return True
    return False


def normalise_bytes(data: bytes) -> bytes:
    # Decode as UTF-8. If this fails, leave the file alone.
    try:
        s = data.decode("utf-8")
    except UnicodeDecodeError:
        return data

    # Normalise CRLF and CR to LF.
    s2 = s.replace("\r\n", "\n").replace("\r", "\n")

    # Ensure exactly one trailing newline.
    s2 = s2.rstrip("\n") + "\n"

    return s2.encode("utf-8")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true", help="report files that would change")
    ap.add_argument("--apply", action="store_true", help="rewrite files in-place")
    args = ap.parse_args()

    if args.check == args.apply:
        ap.error("choose exactly one of --check or --apply")

    repo_root = Path(__file__).resolve().parents[1]

    changed: list[Path] = []
    skipped_binary: list[Path] = []
    skipped_decode: list[Path] = []

    for p in git_tracked_files(repo_root):
        if not should_consider(p):
            continue

        data = p.read_bytes()

        if looks_binary(data):
            skipped_binary.append(p)
            continue

        new = normalise_bytes(data)
        if new == data:
            continue

        # If we failed to decode, normalise_bytes returns original bytes.
        # Distinguish that case so we do not rewrite unknown encodings.
        try:
            data.decode("utf-8")
        except UnicodeDecodeError:
            skipped_decode.append(p)
            continue

        changed.append(p)

        if args.apply:
            p.write_bytes(new)

    if args.check:
        if changed:
            print("Would normalise:")
            for p in changed:
                print(f"  {p.relative_to(repo_root)}")
        else:
            print("No tracked text files need normalisation.")

    if skipped_binary:
        print(f"Skipped {len(skipped_binary)} binary file(s).")
    if skipped_decode:
        print(f"Skipped {len(skipped_decode)} non-UTF-8 file(s).")

    return 1 if (args.check and changed) else 0


if __name__ == "__main__":
    raise SystemExit(main())
