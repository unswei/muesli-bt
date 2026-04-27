#!/usr/bin/env python3
"""Verify deterministic runtime-contract fixture bundles."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import sys
from typing import Iterable


SCHEMA_PATH = pathlib.Path("schemas/event_log/v1/mbt.evt.v1.schema.json")
FIXTURE_ROOT = pathlib.Path("fixtures")
REQUIRED_FILES = {
    "config.json",
    "seed.json",
    "events.jsonl",
    "expected_metrics.json",
    "manifest.json",
}


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Verify runtime-contract fixture bundles.")
    parser.add_argument(
        "--fixture",
        action="append",
        default=[],
        help="Optional fixture name filter. May be passed multiple times.",
    )
    parser.add_argument(
        "--schema",
        default=str(SCHEMA_PATH),
        help=f"Schema file path (default: {SCHEMA_PATH}).",
    )
    return parser.parse_args(argv)


def load_json(path: pathlib.Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def iter_events(path: pathlib.Path) -> Iterable[dict]:
    with path.open("r", encoding="utf-8") as handle:
        for line_no, line in enumerate(handle, start=1):
            text = line.strip()
            if not text:
                continue
            try:
                yield json.loads(text)
            except json.JSONDecodeError as exc:
                raise RuntimeError(f"{path}:{line_no}: invalid JSON: {exc}") from exc


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(65536), b""):
            digest.update(chunk)
    return digest.hexdigest()


def type_counts(events: list[dict]) -> dict[str, int]:
    out: dict[str, int] = {}
    for ev in events:
        event_type = str(ev.get("type", ""))
        out[event_type] = out.get(event_type, 0) + 1
    return out


def node_status_sequence(events: list[dict]) -> list[str]:
    sequence: list[str] = []
    for ev in events:
        if ev.get("type") != "node_exit":
            continue
        data = ev.get("data", {})
        if not isinstance(data, dict):
            continue
        node_id = data.get("node_id")
        status = data.get("status")
        if node_id is None or status is None:
            continue
        sequence.append(f"{node_id}:{status}")
    return sequence


def verify_fixture_dir(path: pathlib.Path, validator) -> None:
    missing = [name for name in sorted(REQUIRED_FILES) if not (path / name).is_file()]
    if missing:
        raise RuntimeError(f"{path}: missing required files: {', '.join(missing)}")

    manifest = load_json(path / "manifest.json")
    expected = load_json(path / "expected_metrics.json")
    events = list(iter_events(path / "events.jsonl"))

    for idx, event in enumerate(events, start=1):
        errors = sorted(validator.iter_errors(event), key=lambda e: e.path)
        if errors:
            first = errors[0]
            where = ".".join(str(p) for p in first.absolute_path) or "<root>"
            raise RuntimeError(f"{path}/events.jsonl:{idx}: {where}: {first.message}")

    if manifest.get("fixture_name") != path.name:
        raise RuntimeError(f"{path}: manifest fixture_name mismatch")
    if manifest.get("generator") != "tools/fixtures/update_fixture.py":
        raise RuntimeError(f"{path}: manifest generator mismatch")

    expected_count = int(expected.get("event_count", -1))
    if expected_count != len(events):
        raise RuntimeError(f"{path}: expected event_count={expected_count}, got {len(events)}")

    counts = type_counts(events)
    for event_type in expected.get("required_types", []):
        if counts.get(event_type, 0) == 0:
            raise RuntimeError(f"{path}: missing required event type: {event_type}")
    for event_type in expected.get("absent_types", []):
        if counts.get(event_type, 0) != 0:
            raise RuntimeError(f"{path}: unexpected event type present: {event_type}")
    for event_type, expected_count_raw in expected.get("type_counts", {}).items():
        expected_type_count = int(expected_count_raw)
        actual_type_count = counts.get(str(event_type), 0)
        if actual_type_count != expected_type_count:
            raise RuntimeError(
                f"{path}: event type {event_type} count mismatch: expected {expected_type_count}, got {actual_type_count}"
            )

    expected_sequence = expected.get("node_status_sequence")
    if expected_sequence is not None:
        actual_sequence = node_status_sequence(events)
        if actual_sequence != expected_sequence:
            raise RuntimeError(
                f"{path}: node_status_sequence mismatch: expected {expected_sequence}, got {actual_sequence}"
            )

    expected_digest = expected.get("events_sha256")
    if isinstance(expected_digest, str):
        actual_digest = sha256_file(path / "events.jsonl")
        if actual_digest != expected_digest:
            raise RuntimeError(f"{path}: events_sha256 mismatch: expected {expected_digest}, got {actual_digest}")


def is_fixture_bundle(path: pathlib.Path) -> bool:
    return all((path / name).is_file() for name in REQUIRED_FILES)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    schema_path = pathlib.Path(args.schema)
    if not schema_path.is_file():
        print(f"error: schema not found: {schema_path}", file=sys.stderr)
        return 2
    if not FIXTURE_ROOT.is_dir():
        print(f"error: fixture root not found: {FIXTURE_ROOT}", file=sys.stderr)
        return 2

    try:
        from jsonschema import Draft202012Validator
    except ImportError:
        print("error: jsonschema is required. Install with: python3 -m pip install jsonschema", file=sys.stderr)
        return 2

    schema = load_json(schema_path)
    Draft202012Validator.check_schema(schema)
    validator = Draft202012Validator(schema)

    selected = set(args.fixture)
    all_fixture_dirs = sorted(p for p in FIXTURE_ROOT.iterdir() if p.is_dir())
    if selected:
        candidates = [p for p in all_fixture_dirs if p.name in selected]
        unknown = selected - {p.name for p in candidates}
        if unknown:
            print(f"error: unknown fixture(s): {', '.join(sorted(unknown))}", file=sys.stderr)
            return 2
    else:
        candidates = [p for p in all_fixture_dirs if is_fixture_bundle(p)]

    for fixture_dir in candidates:
        try:
            verify_fixture_dir(fixture_dir, validator)
            print(f"verified {fixture_dir}")
        except RuntimeError as exc:
            print(f"error: {exc}", file=sys.stderr)
            return 1

    print("fixture verification passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
