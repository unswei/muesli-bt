"""Fail-closed verification for the packaged muesli native runner."""

from __future__ import annotations

import hashlib
import json
import os
import pathlib
import struct
from dataclasses import dataclass
from typing import Any

SCHEMA_VERSION = "humanoid.booster_native_payload.v1"
PLATFORM = "sim_x86_64"
RUNNER_PATH = pathlib.PurePosixPath("sim_x86_64/bin/humanoid_model_mediated_trial")
EXPECTED_ASSET_PATHS = frozenset(
    {
        "common/configs/common.json",
        "common/configs/t1_normal_full.json",
        "common/configs/t2a_moved_ball_baseline.json",
        "common/configs/t2b_moved_ball_full.json",
        "common/configs/t3_emergency_full.json",
        "common/evidence/manifests/t1_normal_full.json",
        "common/evidence/manifests/t2a_moved_ball_baseline.json",
        "common/evidence/manifests/t2b_moved_ball_full.json",
        "common/evidence/manifests/t3_emergency_full.json",
        "common/lisp/bt_deadline_only.lisp",
        "common/lisp/bt_invocation_scoped.lisp",
    }
)


class PayloadError(RuntimeError):
    """The native payload is missing, incompatible or internally inconsistent."""


@dataclass(frozen=True)
class VerifiedPayload:
    root: pathlib.Path
    runner: pathlib.Path
    source_git_commit: str
    source_git_dirty: bool
    manifest: dict[str, Any]


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return f"sha256:{digest.hexdigest()}"


def read_json(path: pathlib.Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except Exception as exc:
        raise PayloadError(f"failed to read {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise PayloadError(f"expected a JSON object: {path}")
    return value


def _is_sha256(value: object) -> bool:
    return (
        isinstance(value, str)
        and len(value) == 71
        and value.startswith("sha256:")
        and all(character in "0123456789abcdef" for character in value[7:])
    )


def validate_linux_x86_64_elf(path: pathlib.Path) -> None:
    if not path.is_file():
        raise PayloadError(f"native runner not found: {path}")
    header = path.read_bytes()[:20]
    if len(header) < 20 or header[:4] != b"\x7fELF":
        raise PayloadError(f"native runner is not an ELF executable: {path}")
    if header[4] != 2 or header[5] != 1:
        raise PayloadError("native runner must be a little-endian ELF64 executable")
    machine = struct.unpack_from("<H", header, 18)[0]
    if machine != 62:
        raise PayloadError(
            f"native runner targets ELF machine {machine}; sim_x86_64 requires machine 62"
        )


def safe_manifest_path(root: pathlib.Path, value: object, field: str) -> pathlib.Path:
    if (
        not isinstance(value, str)
        or not value
        or pathlib.PurePosixPath(value).is_absolute()
    ):
        raise PayloadError(f"{field} must be a non-empty relative path")
    lexical = pathlib.PurePosixPath(value)
    if any(part in {"", ".", ".."} for part in lexical.parts):
        raise PayloadError(f"{field} contains an unsafe path component")
    resolved = (root / pathlib.Path(*lexical.parts)).resolve()
    try:
        resolved.relative_to(root.resolve())
    except ValueError as exc:
        raise PayloadError(f"{field} escapes the payload root") from exc
    return resolved


def verify_payload(root: pathlib.Path) -> VerifiedPayload:
    root = root.resolve()
    manifest = read_json(root / "manifest.json")
    if manifest.get("schema_version") != SCHEMA_VERSION:
        raise PayloadError("unsupported or missing payload schema version")
    if manifest.get("platform") != PLATFORM:
        raise PayloadError("payload platform is not sim_x86_64")
    source_git_commit = manifest.get("source_git_commit")
    if not isinstance(source_git_commit, str) or not source_git_commit:
        raise PayloadError("payload manifest has no source Git commit")
    source_git_dirty = manifest.get("source_git_dirty")
    if not isinstance(source_git_dirty, bool):
        raise PayloadError("payload manifest has no source-tree state")
    status_digest = manifest.get("source_status_sha256")
    if not _is_sha256(status_digest):
        raise PayloadError("payload manifest has no source-tree status digest")

    runner_record = manifest.get("runner")
    if not isinstance(runner_record, dict):
        raise PayloadError("payload manifest has no runner record")
    if runner_record.get("path") != RUNNER_PATH.as_posix():
        raise PayloadError("payload manifest has the wrong runner path")
    runner = safe_manifest_path(root, runner_record.get("path"), "runner.path")
    validate_linux_x86_64_elf(runner)
    if runner.is_symlink() or not os.access(runner, os.X_OK):
        raise PayloadError(f"native runner is a symlink or is not executable: {runner}")
    if runner_record.get("format") != "elf64-x86-64":
        raise PayloadError("payload manifest has the wrong runner format")
    if runner_record.get("sha256") != sha256_file(runner):
        raise PayloadError("native runner digest does not match payload manifest")
    if runner_record.get("size_bytes") != runner.stat().st_size:
        raise PayloadError("native runner size does not match payload manifest")

    records = manifest.get("assets")
    if not isinstance(records, list) or len(records) != len(EXPECTED_ASSET_PATHS):
        raise PayloadError("payload manifest has an incomplete asset set")
    seen: set[str] = set()
    for index, record in enumerate(records):
        if not isinstance(record, dict):
            raise PayloadError(f"asset record {index} is not an object")
        path_value = record.get("path")
        if not isinstance(path_value, str) or path_value in seen:
            raise PayloadError("payload manifest has a missing or duplicate asset path")
        seen.add(path_value)
        asset = safe_manifest_path(root, path_value, f"assets[{index}].path")
        if not asset.is_file() or asset.is_symlink():
            raise PayloadError(
                f"payload asset is missing or is a symlink: {path_value}"
            )
        if record.get("sha256") != sha256_file(asset):
            raise PayloadError(f"payload asset digest mismatch: {path_value}")
        if record.get("size_bytes") != asset.stat().st_size:
            raise PayloadError(f"payload asset size mismatch: {path_value}")
    if seen != EXPECTED_ASSET_PATHS:
        raise PayloadError(
            "payload manifest asset paths differ from the frozen asset set"
        )
    return VerifiedPayload(root, runner, source_git_commit, source_git_dirty, manifest)
