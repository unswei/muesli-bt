#!/usr/bin/env python3
"""Build or verify the frozen Booster Studio ``sim_x86_64`` payload."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import shutil
import subprocess
import sys
import tempfile
from typing import Any

STUDIO_ROOT = pathlib.Path(__file__).resolve().parents[1]
EXPERIMENT_ROOT = STUDIO_ROOT.parent
REPO_ROOT = EXPERIMENT_ROOT.parents[1]
PAYLOAD_ROOT = STUDIO_ROOT / "res" / "native_payload"
sys.path.insert(0, str(STUDIO_ROOT / "src"))

from muesli_booster.native_payload import (
    PLATFORM,
    SCHEMA_VERSION,
    PayloadError,
    sha256_file,
    validate_linux_x86_64_elf,
)
from muesli_booster.native_payload import (
    verify_payload as verify_native_payload,
)

RUNNER_NAME = "humanoid_model_mediated_trial"
UBUNTU_IMAGE = (
    "ubuntu:22.04@sha256:"
    "3b06811b2afd352be909dd088a004166d665dc76d38b13eada33522a9d915c6f"
)
SOURCE_ASSETS = (
    pathlib.Path("configs/common.json"),
    pathlib.Path("configs/t1_normal_full.json"),
    pathlib.Path("configs/t2a_moved_ball_baseline.json"),
    pathlib.Path("configs/t2b_moved_ball_full.json"),
    pathlib.Path("configs/t3_emergency_full.json"),
    pathlib.Path("evidence/manifests/t1_normal_full.json"),
    pathlib.Path("evidence/manifests/t2a_moved_ball_baseline.json"),
    pathlib.Path("evidence/manifests/t2b_moved_ball_full.json"),
    pathlib.Path("evidence/manifests/t3_emergency_full.json"),
    pathlib.Path("lisp/bt_deadline_only.lisp"),
    pathlib.Path("lisp/bt_invocation_scoped.lisp"),
)


def write_json(path: pathlib.Path, value: object) -> None:
    path.write_text(
        json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )


def git_provenance() -> dict[str, object]:
    completed = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=REPO_ROOT,
        check=False,
        capture_output=True,
        text=True,
    )
    commit = completed.stdout.strip() if completed.returncode == 0 else "unknown"
    status = subprocess.run(
        [
            "git",
            "status",
            "--porcelain",
            "--untracked-files=all",
            "--",
            ".",
            ":(exclude).agents",
            ":(exclude).codex",
        ],
        cwd=REPO_ROOT,
        check=False,
        capture_output=True,
        text=True,
    )
    status_text = (
        status.stdout.strip() if status.returncode == 0 else "status-unavailable"
    )
    return {
        "commit": commit,
        "dirty": bool(status_text),
        "status_sha256": f"sha256:{hashlib.sha256(status_text.encode('utf-8')).hexdigest()}",
    }


def validate_sources() -> None:
    dockerfile = STUDIO_ROOT / "native" / "Dockerfile.sim_x86_64"
    if not dockerfile.is_file():
        raise PayloadError(f"missing native build definition: {dockerfile}")
    docker_text = dockerfile.read_text(encoding="utf-8")
    if UBUNTU_IMAGE not in docker_text:
        raise PayloadError(
            "native build definition is not pinned to the frozen Ubuntu image"
        )
    for relative in SOURCE_ASSETS:
        source = EXPERIMENT_ROOT / relative
        if not source.is_file():
            raise PayloadError(f"missing frozen experiment asset: {source}")


def build_runner(destination: pathlib.Path) -> pathlib.Path:
    command = [
        "docker",
        "buildx",
        "build",
        "--platform",
        "linux/amd64",
        "--file",
        str(STUDIO_ROOT / "native" / "Dockerfile.sim_x86_64"),
        "--target",
        "export",
        "--output",
        f"type=local,dest={destination}",
        str(REPO_ROOT),
    ]
    try:
        completed = subprocess.run(command, cwd=REPO_ROOT, check=False)
    except FileNotFoundError as exc:
        raise PayloadError(
            "Docker with Buildx is required to build the native payload"
        ) from exc
    if completed.returncode != 0:
        raise PayloadError(
            "the pinned linux/amd64 build failed; start a Docker-compatible builder or use "
            "--binary with an existing Linux x86_64 build"
        )
    runner = destination / RUNNER_NAME
    validate_linux_x86_64_elf(runner)
    return runner


def stage_payload(binary: pathlib.Path, output: pathlib.Path) -> dict[str, Any]:
    validate_sources()
    validate_linux_x86_64_elf(binary)
    output.mkdir(parents=True, exist_ok=True)
    staged_runner = output / PLATFORM / "bin" / RUNNER_NAME
    staged_runner.parent.mkdir(parents=True)
    shutil.copy2(binary, staged_runner)
    staged_runner.chmod(0o755)

    assets: list[dict[str, object]] = []
    for relative in SOURCE_ASSETS:
        source = EXPERIMENT_ROOT / relative
        destination = output / "common" / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)
        assets.append(
            {
                "path": destination.relative_to(output).as_posix(),
                "sha256": sha256_file(destination),
                "size_bytes": destination.stat().st_size,
                "source": relative.as_posix(),
            }
        )

    provenance = git_provenance()
    manifest: dict[str, Any] = {
        "schema_version": SCHEMA_VERSION,
        "platform": PLATFORM,
        "source_git_commit": provenance["commit"],
        "source_git_dirty": provenance["dirty"],
        "source_status_sha256": provenance["status_sha256"],
        "build": {
            "container_image": UBUNTU_IMAGE,
            "configuration": "Release",
            "portable_gnu_runtime": True,
        },
        "runner": {
            "path": staged_runner.relative_to(output).as_posix(),
            "sha256": sha256_file(staged_runner),
            "size_bytes": staged_runner.stat().st_size,
            "format": "elf64-x86-64",
        },
        "assets": assets,
    }
    write_json(output / "manifest.json", manifest)
    return manifest


def verify_payload(root: pathlib.Path) -> dict[str, Any]:
    return verify_native_payload(root).manifest


def publish_payload(staged: pathlib.Path, destination: pathlib.Path) -> None:
    destination.mkdir(parents=True, exist_ok=True)
    try:
        # ``staged`` may live on a different filesystem (for example, /tmp is
        # commonly a tmpfs). Copy it beside the destination before using
        # same-filesystem renames to publish each managed payload entry.
        with tempfile.TemporaryDirectory(
            prefix=f".{destination.name}.publish-", dir=destination.parent
        ) as temporary:
            adjacent = pathlib.Path(temporary) / "staged"
            shutil.copytree(staged, adjacent)
            for name in ("common", PLATFORM, "manifest.json"):
                target = destination / name
                if target.is_dir() and not target.is_symlink():
                    shutil.rmtree(target)
                elif target.exists() or target.is_symlink():
                    target.unlink()
            for source in adjacent.iterdir():
                source.replace(destination / source.name)
    except OSError as exc:
        raise PayloadError(f"could not publish native payload: {exc}") from exc


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    action = parser.add_mutually_exclusive_group()
    action.add_argument(
        "--binary",
        type=pathlib.Path,
        help="Stage an existing Linux x86_64 runner instead of invoking Docker.",
    )
    action.add_argument(
        "--check-only",
        action="store_true",
        help="Verify the existing payload and exit.",
    )
    action.add_argument(
        "--source-check",
        action="store_true",
        help="Verify build inputs without Docker or a generated payload.",
    )
    parser.add_argument("--output", type=pathlib.Path, default=PAYLOAD_ROOT)
    parser.add_argument(
        "--allow-dirty",
        action="store_true",
        help="Permit a development payload whose manifest records a dirty source tree.",
    )
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    try:
        if args.source_check:
            validate_sources()
            print("PASS Booster Studio native payload sources")
            return 0
        output = args.output.resolve()
        if args.check_only:
            manifest = verify_payload(output)
            print(
                f"PASS {manifest['platform']} payload from {manifest['source_git_commit']}"
            )
            return 0

        provenance = git_provenance()
        if provenance["dirty"] and not args.allow_dirty:
            raise PayloadError(
                "refusing a release payload from a dirty source tree; commit the source or "
                "pass --allow-dirty for a development-only payload"
            )

        with tempfile.TemporaryDirectory(prefix="muesli-booster-payload-") as temporary:
            workspace = pathlib.Path(temporary)
            if args.binary is not None:
                binary = args.binary.resolve()
            else:
                binary = build_runner(workspace / "docker-export")
            staged = workspace / "staged"
            stage_payload(binary, staged)
            verify_payload(staged)
            publish_payload(staged, output)
            manifest = verify_payload(output)
        print(f"PASS wrote {manifest['platform']} payload to {output}")
        return 0
    except PayloadError as exc:
        print(f"payload error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
