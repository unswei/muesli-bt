"""Fail-closed deterministic and ACRA-export air-hockey providers."""

from __future__ import annotations

import hashlib
import math
import re
from collections.abc import Callable, Sequence
from pathlib import Path
from typing import Any, Protocol

PUBLIC_OBSERVATION_DIM = 19
PUBLIC_ACTION_DIM = 2
ACTION_FRAME = "airhockey.normalised_mallet_target.v1"
CONTROL_PERIOD_MS = 20


class ProviderError(RuntimeError):
    """A provider configuration or inference contract failed."""


class Policy(Protocol):
    """Narrow public surface shared by ACRA principal policy exports."""

    metadata: dict[str, Any]

    def initial_carry(self) -> Any: ...

    def act(self, observation: Sequence[float], carry: Any) -> tuple[Any, Any]: ...


def checkpoint_sha256(path: Path) -> str:
    """Return the immutable digest used to bind an exported checkpoint."""

    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return f"sha256:{digest.hexdigest()}"


def _vector(value: Any, size: int, name: str) -> list[float]:
    if isinstance(value, (str, bytes)):
        raise ProviderError(f"{name} must be a numeric vector")
    try:
        items = list(value)
    except TypeError as error:
        raise ProviderError(f"{name} must be a numeric vector") from error
    if len(items) != size:
        raise ProviderError(f"{name} must contain exactly {size} values")
    checked: list[float] = []
    for item in items:
        if isinstance(item, bool):
            raise ProviderError(f"{name} must not contain booleans")
        try:
            number = float(item)
        except (TypeError, ValueError) as error:
            raise ProviderError(f"{name} must contain only numeric values") from error
        if not math.isfinite(number):
            raise ProviderError(f"{name} must contain only finite values")
        if number < -1.0 or number > 1.0:
            raise ProviderError(f"{name} values must lie in [-1, 1]")
        checked.append(number)
    return checked


def validate_observation(observation: Any) -> list[float]:
    """Validate the complete public 19-value observation."""

    return _vector(observation, PUBLIC_OBSERVATION_DIM, "public observation")


def validate_action(action: Any) -> list[float]:
    """Validate one normalised two-value planar target."""

    return _vector(action, PUBLIC_ACTION_DIM, "provider action")


class FixedProvider:
    """Return one fixed, bounded proposal for every public observation."""

    def __init__(self, target: Sequence[float]) -> None:
        self._target = validate_action(target)

    def reset(self) -> None:
        """Reset the stateless provider at an episode boundary."""

    def infer(self, observation: Sequence[float]) -> list[float]:
        validate_observation(observation)
        return list(self._target)


class AcraExportProvider:
    """Load one hash-bound ACRA NumPy export without choosing a checkpoint."""

    def __init__(
        self,
        family_id: str,
        checkpoint: Path,
        expected_sha256: str,
        *,
        policy_loader: Callable[[str, Path], Policy] | None = None,
        supported_families: Sequence[str] | None = None,
    ) -> None:
        path = checkpoint.resolve()
        if not path.is_file():
            raise ProviderError(f"ACRA checkpoint does not exist: {path}")
        if not re.fullmatch(r"sha256:[0-9a-f]{64}", expected_sha256):
            raise ProviderError("ACRA checkpoint requires a complete SHA-256 digest")
        observed_sha256 = checkpoint_sha256(path)
        if observed_sha256 != expected_sha256:
            raise ProviderError("ACRA checkpoint SHA-256 mismatch")

        if policy_loader is None or supported_families is None:
            try:
                from airhockey_distill.students import (
                    PRINCIPAL_FAMILY_IDS,
                    load_principal_policy,
                )
            except Exception as error:
                raise ProviderError(
                    "the pinned airhockey_distill package is not importable"
                ) from error
            if policy_loader is None:
                policy_loader = load_principal_policy
            if supported_families is None:
                supported_families = PRINCIPAL_FAMILY_IDS

        if family_id not in supported_families:
            raise ProviderError(f"unsupported ACRA export family: {family_id}")
        try:
            policy = policy_loader(family_id, path)
        except Exception as error:
            raise ProviderError(f"failed to load ACRA export: {error}") from error
        metadata = dict(policy.metadata)
        if metadata.get("student_id") != family_id:
            raise ProviderError("ACRA checkpoint student_id does not match its family")

        self.family_id = family_id
        self.checkpoint = path
        self.checkpoint_sha256 = observed_sha256
        self.metadata = metadata
        self._policy = policy
        self._carry: Any = None
        self.reset()

    def reset(self) -> None:
        """Reset policy carry only at an episode/session boundary."""

        try:
            self._carry = self._policy.initial_carry()
        except Exception as error:
            raise ProviderError(
                f"failed to initialise ACRA policy carry: {error}"
            ) from error

    def infer(self, observation: Sequence[float]) -> list[float]:
        public = validate_observation(observation)
        try:
            action, next_carry = self._policy.act(public, self._carry)
        except Exception as error:
            raise ProviderError(f"ACRA export inference failed: {error}") from error
        checked = validate_action(action)
        self._carry = next_carry
        return checked


def provider_response(
    request: dict[str, Any], action: Sequence[float]
) -> dict[str, Any]:
    """Create the exact response shape consumed as an action-chunk proposal."""

    return {
        "schema_version": "airhockey.provider.response.v1",
        "request_id": request["request_id"],
        "captured_context_id": request["captured_context_id"],
        "source_observation_step": request["source_observation_step"],
        "actions": [
            {
                "type": "continuous",
                "frame_id": ACTION_FRAME,
                "values": validate_action(action),
                "dt_ms": CONTROL_PERIOD_MS,
            }
        ],
    }
