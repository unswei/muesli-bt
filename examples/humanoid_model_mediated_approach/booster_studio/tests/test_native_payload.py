from __future__ import annotations

import json
import pathlib
import struct
import sys
import tempfile
import unittest

try:
    import jsonschema
except (
    ImportError
):  # pragma: no cover - optional outside the repository test environment
    jsonschema = None


PROJECT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT / "tools"))

import build_native_payload as payload


def write_elf(path: pathlib.Path, machine: int = 62) -> None:
    header = bytearray(64)
    header[:4] = b"\x7fELF"
    header[4] = 2  # ELFCLASS64
    header[5] = 1  # ELFDATA2LSB
    header[6] = 1  # EV_CURRENT
    struct.pack_into("<H", header, 16, 2)  # ET_EXEC
    struct.pack_into("<H", header, 18, machine)
    path.write_bytes(header + b"deterministic-test-runner")
    path.chmod(0o755)


class NativePayloadTests(unittest.TestCase):
    def test_stage_and_verify_frozen_payload(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            runner = root / "runner"
            write_elf(runner)
            staged = root / "payload"

            manifest = payload.stage_payload(runner, staged)
            verified = payload.verify_payload(staged)

            self.assertEqual(verified, manifest)
            self.assertEqual(verified["platform"], "sim_x86_64")
            self.assertEqual(len(verified["assets"]), len(payload.SOURCE_ASSETS))
            self.assertTrue(
                (staged / "sim_x86_64/bin/humanoid_model_mediated_trial").is_file()
            )

    def test_tampered_asset_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            runner = root / "runner"
            write_elf(runner)
            staged = root / "payload"
            payload.stage_payload(runner, staged)
            (staged / "common/configs/common.json").write_text("{}\n", encoding="utf-8")

            with self.assertRaisesRegex(payload.PayloadError, "digest mismatch"):
                payload.verify_payload(staged)

    def test_wrong_architecture_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            runner = pathlib.Path(directory) / "runner"
            write_elf(runner, machine=183)  # AArch64
            with self.assertRaisesRegex(payload.PayloadError, "requires machine 62"):
                payload.validate_linux_x86_64_elf(runner)

    def test_manifest_path_escape_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            runner = root / "runner"
            write_elf(runner)
            staged = root / "payload"
            payload.stage_payload(runner, staged)
            manifest_path = staged / "manifest.json"
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            manifest["runner"]["path"] = "../runner"
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")

            with self.assertRaisesRegex(payload.PayloadError, "wrong runner path"):
                payload.verify_payload(staged)

    def test_publish_preserves_tracked_readme(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            runner = root / "runner"
            write_elf(runner)
            staged = root / "staged"
            payload.stage_payload(runner, staged)
            destination = root / "published"
            destination.mkdir()
            (destination / "README.md").write_text("keep me\n", encoding="utf-8")

            payload.publish_payload(staged, destination)

            self.assertEqual(
                (destination / "README.md").read_text(encoding="utf-8"), "keep me\n"
            )
            payload.verify_payload(destination)

    @unittest.skipIf(jsonschema is None, "jsonschema is unavailable")
    def test_generated_manifest_matches_public_schema(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            runner = root / "runner"
            write_elf(runner)
            manifest = payload.stage_payload(runner, root / "payload")
            schema_path = (
                PROJECT.parents[2]
                / "schemas/humanoid_booster/v1/native-payload.schema.json"
            )
            schema = json.loads(schema_path.read_text(encoding="utf-8"))
            jsonschema.Draft202012Validator(schema).validate(manifest)


if __name__ == "__main__":
    unittest.main()
