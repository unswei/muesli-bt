#!/usr/bin/env python3
"""Validate and prepare the local air-hockey WP4 integration package."""

from __future__ import annotations

import argparse
import io
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path
from typing import Any

import tomllib
from jsonschema import Draft202012Validator
from provider import (
    AcraExportProvider,
    FixedProvider,
    MockProviderService,
    ProviderError,
    capability_descriptor,
    checkpoint_sha256,
)
from provider.mock_service import validate_capability_descriptor

EXAMPLE_ROOT = Path(__file__).resolve().parent
REPOSITORY_ROOT = EXAMPLE_ROOT.parents[1]
LOCK_PATH = EXAMPLE_ROOT / "container" / "wp4.lock.json"
SCHEMA_ROOT = REPOSITORY_ROOT / "schemas" / "air_hockey_integration" / "v1"
CONTEXT_MARKER = ".air-hockey-wp4-context"
DEFAULT_ACRA_REPOSITORY = REPOSITORY_ROOT.parent / "distillation"


class IntegrationError(RuntimeError):
    """The pinned WP4 integration definition failed validation."""


def read_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except Exception as error:
        raise IntegrationError(f"failed to read JSON {path}: {error}") from error
    if not isinstance(value, dict):
        raise IntegrationError(f"expected a JSON object: {path}")
    return value


def write_json(path: Path, value: object) -> None:
    path.write_text(
        json.dumps(value, indent=2, sort_keys=True, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )


def _validator(schema_name: str) -> Draft202012Validator:
    schema = read_json(SCHEMA_ROOT / schema_name)
    Draft202012Validator.check_schema(schema)
    return Draft202012Validator(schema)


def load_lock() -> dict[str, Any]:
    lock = read_json(LOCK_PATH)
    _validator("airhockey.integration_lock.v1.schema.json").validate(lock)
    return lock


def provider_request(request_id: str = "wp4-provider-0001") -> dict[str, Any]:
    return {
        "schema_version": "airhockey.provider.request.v1",
        "request_id": request_id,
        "captured_context_id": "episode-000001/track-0001",
        "source_observation_step": 0,
        "deadline_ms": 120,
        "action_frame": "airhockey.normalised_mallet_target.v1",
        "observation": [0.0] * 19,
    }


def _run_git(repository: Path, *arguments: str) -> str:
    try:
        result = subprocess.run(
            ["git", *arguments],
            cwd=repository,
            check=True,
            capture_output=True,
            text=True,
        )
    except subprocess.CalledProcessError as error:
        message = error.stderr.strip() or error.stdout.strip() or str(error)
        raise IntegrationError(
            f"git {' '.join(arguments)} failed: {message}"
        ) from error
    return result.stdout.strip()


def validate_definition(lock: dict[str, Any]) -> None:
    base = lock["base_container"]
    if base["immutable_reference"] != f"{base['image']}@{base['digest']}":
        raise IntegrationError("base container reference does not match its digest")

    dockerfile_path = EXAMPLE_ROOT / lock["joint_container"]["dockerfile"]
    dockerfile = dockerfile_path.read_text(encoding="utf-8")
    expected_from = f"FROM {base['immutable_reference']}"
    if expected_from not in dockerfile.splitlines():
        raise IntegrationError("joint Dockerfile does not use the pinned base digest")
    required_fragments = (
        "COPY --from=airhockey_distill / /opt/airhockey-distillation",
        lock["joint_container"]["runner_target"],
        f'test "${{ACRA_REVISION}}" = "{lock["acra"]["revision"]}"',
        ".air-hockey-source-revision",
        ".muesli-bt-source-revision",
        "cmake=3.28.3-1build7",
        "g++=4:13.2.0-7ubuntu1",
        "make=4.3-4.1build2",
        "container-check",
    )
    for fragment in required_fragments:
        if fragment not in dockerfile:
            raise IntegrationError(f"joint Dockerfile is missing: {fragment}")

    requirements_path = EXAMPLE_ROOT / lock["joint_container"]["requirements"]
    requirements = [
        line.strip()
        for line in requirements_path.read_text(encoding="utf-8").splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    ]
    if not requirements or not all(
        re.fullmatch(r"[A-Za-z0-9_.-]+==[A-Za-z0-9_.+-]+", line)
        for line in requirements
    ):
        raise IntegrationError("WP4 Python requirements must use exact versions")

    validate_capability_descriptor(capability_descriptor(), lock)
    if (
        lock["learned_provider"]["checkpoint_selection"]
        != ("unresolved_until_acra_freeze")
        or lock["learned_provider"]["checkpoint_sha256"] is not None
    ):
        raise IntegrationError("WP4 must not select the final ACRA checkpoint")


def check_fixed_provider(lock: dict[str, Any]) -> None:
    service = MockProviderService(
        lambda: FixedProvider((0.25, -0.4)),
        SCHEMA_ROOT,
    )
    descriptor = service.describe()
    if descriptor["version"] != "0.2" or descriptor["status"] != "success":
        raise IntegrationError("mock provider service did not start as MMSP v0.2")
    validate_capability_descriptor(descriptor["capabilities"][0], lock)
    request = provider_request()
    session_id = service.start(request)
    response = service.step(session_id, request)
    if response["actions"][0]["values"] != [0.25, -0.4]:
        raise IntegrationError("fixed provider action changed during mock startup")
    service.close(session_id)
    try:
        service.step(session_id, request)
    except ProviderError:
        pass
    else:
        raise IntegrationError("closed provider session accepted another step")


def validate_acra_repository(repository: Path, lock: dict[str, Any]) -> None:
    if not repository.is_dir():
        raise IntegrationError(f"ACRA repository not found: {repository}")
    expected = lock["acra"]
    revision = _run_git(repository, "rev-parse", f"{expected['revision']}^{{commit}}")
    if revision != expected["revision"]:
        raise IntegrationError("pinned ACRA revision does not resolve exactly")
    remotes = {
        _run_git(repository, "remote", "get-url", name)
        for name in _run_git(repository, "remote").splitlines()
    }
    if expected["repository"] not in remotes:
        raise IntegrationError(
            "local ACRA checkout does not match the pinned repository"
        )
    pyproject = tomllib.loads(
        _run_git(repository, "show", f"{revision}:pyproject.toml")
    )
    project = pyproject.get("project", {})
    if project.get("name") != expected["python_distribution"]:
        raise IntegrationError("pinned ACRA distribution name changed")
    if project.get("version") != expected["package_version"]:
        raise IntegrationError("pinned ACRA package version changed")


def _safe_extract_git_archive(archive: bytes, destination: Path) -> None:
    with tarfile.open(fileobj=io.BytesIO(archive), mode="r:") as tar:
        for member in tar.getmembers():
            target = (destination / member.name).resolve()
            if (
                destination.resolve() not in target.parents
                and target != destination.resolve()
            ):
                raise IntegrationError("git archive member escaped its destination")
            if member.issym() or member.islnk():
                raise IntegrationError("WP4 source archives must not contain links")
        tar.extractall(destination)


def export_revision(repository: Path, revision: str, destination: Path) -> None:
    try:
        result = subprocess.run(
            ["git", "archive", "--format=tar", revision],
            cwd=repository,
            check=True,
            capture_output=True,
        )
    except subprocess.CalledProcessError as error:
        raise IntegrationError(f"failed to export {revision}") from error
    destination.mkdir(parents=True)
    _safe_extract_git_archive(result.stdout, destination)


def _clear_acra_modules() -> None:
    for name in tuple(sys.modules):
        if name == "airhockey_distill" or name.startswith("airhockey_distill."):
            del sys.modules[name]


def check_acra_export(repository: Path, lock: dict[str, Any]) -> None:
    revision = lock["acra"]["revision"]
    with tempfile.TemporaryDirectory(prefix="muesli-air-hockey-acra-") as directory:
        exported = Path(directory) / "airhockey-distillation"
        export_revision(repository, revision, exported)
        source = str(exported / "src")
        sys.path.insert(0, source)
        _clear_acra_modules()
        try:
            import numpy as np
            from airhockey_distill import __version__
            from airhockey_distill.envs.policy_interface import (
                PUBLIC_ACTION_DIM,
                PUBLIC_OBSERVATION_DIM,
            )
            from airhockey_distill.students import (
                PARAMETER_SHAPES,
                PRINCIPAL_FAMILY_IDS,
                save_principal_checkpoint,
            )

            if __version__ != lock["acra"]["package_version"]:
                raise IntegrationError("imported ACRA package version mismatch")
            if (PUBLIC_OBSERVATION_DIM, PUBLIC_ACTION_DIM) != (19, 2):
                raise IntegrationError("pinned ACRA public dimensions changed")
            if (
                list(PRINCIPAL_FAMILY_IDS)
                != lock["learned_provider"]["supported_families"]
            ):
                raise IntegrationError("pinned ACRA export families changed")

            checkpoint = Path(directory) / "engineering-mock-feed-forward.npz"
            parameters = {
                name: np.zeros(shape, dtype=np.float32)
                for name, shape in PARAMETER_SHAPES.items()
            }
            save_principal_checkpoint(
                "feed_forward",
                checkpoint,
                parameters,
                {
                    "student_id": "feed_forward",
                    "training_seed": 0,
                    "training_stage": "wp4_engineering_mock",
                },
            )
            digest = checkpoint_sha256(checkpoint)
            service = MockProviderService(
                lambda: AcraExportProvider(
                    "feed_forward",
                    checkpoint,
                    digest,
                ),
                SCHEMA_ROOT,
            )
            request = provider_request("wp4-acra-export-0001")
            session_id = service.start(request)
            response = service.step(session_id, request)
            service.close(session_id)
            if response["actions"][0]["values"] != [0.0, 0.0]:
                raise IntegrationError("ACRA mock export did not reproduce zero action")
            try:
                AcraExportProvider(
                    "feed_forward",
                    checkpoint,
                    "sha256:" + "0" * 64,
                )
            except ProviderError:
                pass
            else:
                raise IntegrationError(
                    "ACRA wrapper accepted the wrong checkpoint hash"
                )
        finally:
            _clear_acra_modules()
            sys.path.remove(source)


def container_check(lock: dict[str, Any]) -> None:
    validate_definition(lock)
    check_fixed_provider(lock)
    try:
        import airhockey_distill
        from airhockey_distill.envs.policy_interface import (
            PUBLIC_ACTION_DIM,
            PUBLIC_OBSERVATION_DIM,
        )
    except Exception as error:
        raise IntegrationError(
            "joint container cannot import the pinned airhockey_distill package"
        ) from error
    if airhockey_distill.__version__ != lock["acra"]["package_version"]:
        raise IntegrationError("joint container has the wrong ACRA package version")
    if (PUBLIC_OBSERVATION_DIM, PUBLIC_ACTION_DIM) != (19, 2):
        raise IntegrationError("joint container has incompatible public dimensions")


def _guard_context_output(output: Path, force: bool) -> None:
    if output.exists():
        if not force:
            raise IntegrationError(f"build context already exists: {output}")
        if not output.is_dir() or not (output / CONTEXT_MARKER).is_file():
            raise IntegrationError("refuse to replace an unmarked build context")
        shutil.rmtree(output)
    output.mkdir(parents=True)
    (output / CONTEXT_MARKER).write_text(
        "airhockey.integration_lock.v1\n", encoding="utf-8"
    )


def prepare_context(
    acra_repository: Path, output: Path, lock: dict[str, Any], force: bool
) -> dict[str, Any]:
    validate_acra_repository(acra_repository, lock)
    output = output.resolve()
    if output == output.parent:
        raise IntegrationError("build context cannot be a filesystem root")
    _guard_context_output(output, force)
    muesli_revision = _run_git(REPOSITORY_ROOT, "rev-parse", "HEAD")
    acra_revision = lock["acra"]["revision"]
    export_revision(REPOSITORY_ROOT, muesli_revision, output / "muesli_bt")
    export_revision(acra_repository, acra_revision, output / "airhockey_distill")
    (output / "muesli_bt" / ".muesli-bt-source-revision").write_text(
        muesli_revision + "\n", encoding="utf-8"
    )
    (output / "airhockey_distill" / ".air-hockey-source-revision").write_text(
        acra_revision + "\n", encoding="utf-8"
    )
    exported_dockerfile = (
        output
        / "muesli_bt"
        / "examples"
        / "air_hockey_model_mediated_defence"
        / lock["joint_container"]["dockerfile"]
    )
    if not exported_dockerfile.is_file():
        raise IntegrationError(
            "muesli HEAD does not contain WP4; commit it before preparing a build context"
        )
    manifest = {
        "schema_version": "airhockey.build_context.v1",
        "muesli_bt_revision": muesli_revision,
        "muesli_bt_tree": _run_git(REPOSITORY_ROOT, "rev-parse", "HEAD^{tree}"),
        "acra_revision": acra_revision,
        "acra_tree": _run_git(
            acra_repository, "rev-parse", f"{acra_revision}^{{tree}}"
        ),
        "base_container": lock["base_container"]["immutable_reference"],
    }
    write_json(output / "build-context.json", manifest)
    return manifest


def build_command(context: Path, manifest: dict[str, Any], lock: dict[str, Any]) -> str:
    context = context.resolve()
    muesli = context / "muesli_bt"
    acra = context / "airhockey_distill"
    dockerfile = (
        muesli
        / "examples"
        / "air_hockey_model_mediated_defence"
        / lock["joint_container"]["dockerfile"]
    )
    arguments = [
        "docker",
        "buildx",
        "build",
        "--load",
        "--file",
        str(dockerfile),
        "--build-context",
        f"airhockey_distill={acra}",
        "--build-arg",
        f"ACRA_REVISION={manifest['acra_revision']}",
        "--build-arg",
        f"MUESLI_BT_REVISION={manifest['muesli_bt_revision']}",
        "--tag",
        lock["joint_container"]["output_image"],
        str(muesli),
    ]
    return shlex.join(arguments)


def resolve_acra_repository(value: Path | None) -> Path:
    if value is not None:
        return value.resolve()
    configured = os.environ.get("AIRHOCKEY_DISTILL_REPO")
    if configured:
        return Path(configured).resolve()
    return DEFAULT_ACRA_REPOSITORY.resolve()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    check = subparsers.add_parser("check", help="run the complete local Gate G4")
    check.add_argument("--acra-repo", type=Path)
    subparsers.add_parser(
        "container-check", help="run non-MuJoCo checks inside the joint image"
    )
    prepare = subparsers.add_parser(
        "prepare-context", help="export immutable local Docker build contexts"
    )
    prepare.add_argument("--acra-repo", type=Path)
    prepare.add_argument("--out", required=True, type=Path)
    prepare.add_argument("--force", action="store_true")
    subparsers.add_parser(
        "print-mujoco-smoke", help="print the deferred Marvin smoke command"
    )
    arguments = parser.parse_args()

    lock = load_lock()
    if arguments.command == "check":
        acra = resolve_acra_repository(arguments.acra_repo)
        validate_definition(lock)
        check_fixed_provider(lock)
        validate_acra_repository(acra, lock)
        check_acra_export(acra, lock)
        print(
            "air-hockey Gate G4 local package passed: pinned ACRA archive, "
            "fixed provider, frozen-export wrapper and mock compatibility"
        )
        return 0
    if arguments.command == "container-check":
        container_check(lock)
        print("air-hockey WP4 joint-container startup checks passed")
        return 0
    if arguments.command == "prepare-context":
        acra = resolve_acra_repository(arguments.acra_repo)
        manifest = prepare_context(acra, arguments.out, lock, arguments.force)
        print(build_command(arguments.out, manifest, lock))
        return 0
    if arguments.command == "print-mujoco-smoke":
        print(lock["mujoco_smoke"]["command"])
        return 0
    raise IntegrationError(f"unsupported command: {arguments.command}")


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (IntegrationError, ProviderError) as error:
        raise SystemExit(f"error: {error}") from error
