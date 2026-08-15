"""Unit tests for local air-hockey WP4 integration packaging."""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
EXAMPLE_ROOT = REPOSITORY_ROOT / "examples" / "air_hockey_model_mediated_defence"
sys.path.insert(0, str(EXAMPLE_ROOT))

from provider import (
    AcraExportProvider,
    FixedProvider,
    MockProviderService,
    ProviderError,
    capability_descriptor,
    checkpoint_sha256,
)
from provider.mock_service import validate_capability_descriptor
from run_wp4 import (
    CONTEXT_MARKER,
    SCHEMA_ROOT,
    IntegrationError,
    _guard_context_output,
    load_lock,
    provider_request,
    validate_definition,
)


class _FakePolicy:
    def __init__(self) -> None:
        self.metadata = {"student_id": "feed_forward"}

    def initial_carry(self) -> int:
        return 0

    def act(self, observation: list[float], carry: int) -> tuple[list[float], int]:
        if len(observation) != 19:
            raise ValueError("wrong observation")
        return [0.1, -0.2], carry + 1


class AirHockeyWp4Tests(unittest.TestCase):
    def test_lock_and_dockerfile_are_immutable(self) -> None:
        lock = load_lock()
        validate_definition(lock)
        self.assertEqual(len(lock["acra"]["revision"]), 40)
        self.assertIsNone(lock["learned_provider"]["checkpoint_sha256"])
        self.assertIn("@sha256:", lock["base_container"]["immutable_reference"])

    def test_fixed_provider_mock_lifecycle_is_schema_valid(self) -> None:
        service = MockProviderService(lambda: FixedProvider([0.3, -0.1]), SCHEMA_ROOT)
        request = provider_request()
        session = service.start(request)
        response = service.step(session, request)
        self.assertEqual(response["actions"][0]["values"], [0.3, -0.1])
        service.cancel(session)
        with self.assertRaisesRegex(ProviderError, "not active"):
            service.step(session, request)

    def test_acra_export_wrapper_binds_family_and_hash(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            checkpoint = Path(directory) / "engineering.npz"
            checkpoint.write_bytes(b"engineering-only-checkpoint")
            digest = checkpoint_sha256(checkpoint)

            def loader(family_id: str, path: Path) -> _FakePolicy:
                self.assertEqual(family_id, "feed_forward")
                self.assertEqual(path, checkpoint.resolve())
                return _FakePolicy()

            provider = AcraExportProvider(
                "feed_forward",
                checkpoint,
                digest,
                policy_loader=loader,
                supported_families=("feed_forward",),
            )
            self.assertEqual(provider.infer([0.0] * 19), [0.1, -0.2])
            with self.assertRaisesRegex(ProviderError, "SHA-256 mismatch"):
                AcraExportProvider(
                    "feed_forward",
                    checkpoint,
                    "sha256:" + "0" * 64,
                    policy_loader=loader,
                    supported_families=("feed_forward",),
                )

    def test_capability_descriptor_fails_closed(self) -> None:
        lock = load_lock()
        descriptor = capability_descriptor()
        descriptor["supports_cancel"] = False
        with self.assertRaisesRegex(ProviderError, "cancellation"):
            validate_capability_descriptor(descriptor, lock)

    def test_guarded_context_replacement_refuses_unmarked_data(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "context"
            output.mkdir()
            keep = output / "keep.txt"
            keep.write_text("keep\n", encoding="utf-8")
            with self.assertRaisesRegex(IntegrationError, "unmarked"):
                _guard_context_output(output, force=True)
            self.assertTrue(keep.is_file())

            output.rename(Path(directory) / "unmarked")
            marked = Path(directory) / "marked"
            marked.mkdir()
            (marked / CONTEXT_MARKER).write_text("marked\n", encoding="utf-8")
            _guard_context_output(marked, force=True)
            self.assertTrue((marked / CONTEXT_MARKER).is_file())


if __name__ == "__main__":
    unittest.main()
