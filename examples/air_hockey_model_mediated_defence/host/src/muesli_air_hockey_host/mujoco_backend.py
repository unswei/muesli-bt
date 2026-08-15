"""MuJoCo-backed implementation of the air-hockey host wire contract."""

from __future__ import annotations

import copy
import math
import time
from collections.abc import Callable
from typing import Any

from .backend import FakeDirectLaunchBackend, HostConfiguration, HostOperationError

EnvironmentFactory = Callable[[HostConfiguration], Any]
ShotFactory = Callable[[], Any]


def _default_environment(configuration: HostConfiguration) -> Any:
    from airhockey_distill.envs import (
        BlackoutSchedule,
        DefendShotTrackingLoss,
        MujocoDirectLaunchBackend,
    )

    return DefendShotTrackingLoss(
        MujocoDirectLaunchBackend(),
        blackout=BlackoutSchedule(
            start_observation_step=configuration.blackout_start_step,
            length_steps=configuration.blackout_length_steps,
        ),
        timeout_steps=configuration.timeout_steps,
        action_lock_steps=configuration.action_lock_steps,
    )


def _default_shot() -> Any:
    from airhockey_distill.envs import DEFAULT_DIRECT_LAUNCH_SHOT

    return DEFAULT_DIRECT_LAUNCH_SHOT


def _float_list(values: Any, *, size: int, where: str) -> list[float]:
    if hasattr(values, "tolist"):
        values = values.tolist()
    result = [float(value) for value in values]
    if len(result) != size or not all(math.isfinite(value) for value in result):
        raise RuntimeError(f"{where} must contain {size} finite values")
    return result


class MujocoDirectLaunchHostBackend(FakeDirectLaunchBackend):
    """Adapt ACRA's pinned direct-launch Gym environment to host protocol v1.

    Simulator ground truth is retained only in the evaluation trace returned by
    :meth:`evaluation_records`; it is never included in a protocol response.
    """

    def __init__(
        self,
        *,
        environment_factory: EnvironmentFactory | None = None,
        shot_factory: ShotFactory | None = None,
    ) -> None:
        super().__init__()
        self._environment_factory = environment_factory or _default_environment
        self._shot_factory = shot_factory or _default_shot
        self._environment: Any | None = None
        self._shot: Any | None = None
        self._observation: list[float] | None = None
        self._environment_info: dict[str, Any] = {}
        self._reset_observation: list[float] | None = None
        self._reset_info: dict[str, Any] = {}
        self._evaluation: list[dict[str, Any]] = []

    @staticmethod
    def _info() -> dict[str, Any]:
        result = FakeDirectLaunchBackend._info()
        result["backend"] = "acra_direct_launch"
        return result

    def _dispatch(self, operation: str, payload: dict[str, Any]) -> dict[str, Any]:
        if operation != "close":
            return super()._dispatch(operation, payload)
        self.shutdown()
        self._closed = True
        self._episode_started = False
        self._pending_action = None
        return {"closed": True}

    def _reset(self, payload: dict[str, Any]) -> dict[str, Any]:
        self._close_environment()
        self._environment = self._environment_factory(self._configuration)
        self._shot = self._shot_factory()
        self._seed = int(payload.get("seed", 0))
        observation, info = self._environment.reset(
            shot=self._shot,
            seed=self._seed,
        )
        self._episode_number += 1
        self._track_number = 1
        self._step = 0
        self._pending_action = None
        self._episode_started = True
        self._terminated = False
        self._truncated = False
        self._observation = _float_list(
            observation,
            size=19,
            where="ACRA public observation",
        )
        self._environment_info = dict(info)
        self._reset_observation = list(self._observation)
        self._reset_info = dict(self._environment_info)
        self._previous_visible = self._puck_visible_from_info()
        self._evaluation = []
        self._validate_environment_step()
        return {"state": self._public_state()}

    def _advance(self) -> dict[str, Any]:
        self._require_active_episode()
        if self._pending_action is None:
            raise HostOperationError(
                "action_required",
                "act must be accepted before each step",
            )
        if self._environment is None:
            raise RuntimeError("MuJoCo environment is unavailable after reset")

        requested_action = list(self._pending_action)
        applied_action = (
            list(self._observation[14:16])
            if bool(self._environment_info.get("action_locked", False))
            else list(requested_action)
        )
        started_ns = time.monotonic_ns()
        observation, reward, terminated, truncated, info = self._environment.step(
            requested_action
        )
        finished_ns = time.monotonic_ns()
        self._pending_action = None
        self._observation = _float_list(
            observation,
            size=19,
            where="ACRA public observation",
        )
        self._environment_info = dict(info)
        previous_step = self._step
        self._step = int(self._environment_info.get("observation_step", previous_step + 1))
        if self._step != previous_step + 1:
            raise RuntimeError("ACRA observation steps must advance exactly once")

        visible = self._puck_visible_from_info()
        reacquired = not self._previous_visible and visible
        replaced = self._step in self._configuration.replace_track_steps
        if reacquired or replaced:
            self._track_number += 1
        self._previous_visible = visible

        forced_terminal = self._configuration.terminate_at_step == self._step
        self._terminated = bool(terminated) or forced_terminal
        self._truncated = bool(truncated) and not self._terminated
        self._episode_started = not (self._terminated or self._truncated)
        state = self._public_state()
        self._evaluation.append(
            {
                "observation_step": self._step,
                "started_monotonic_ns": started_ns,
                "finished_monotonic_ns": finished_ns,
                "step_duration_ns": finished_ns - started_ns,
                "requested_action": requested_action,
                "applied_action": applied_action,
                "reward": float(reward),
                "public_state": copy.deepcopy(state),
                "environment_info": self._evaluation_info(),
                "privileged": self._privileged_state(),
            }
        )
        return {"state": state, "reward": float(reward)}

    def _public_state(self) -> dict[str, Any]:
        if self._observation is None:
            raise HostOperationError(
                "episode_not_started",
                "reset must be called first",
            )
        episode_id = f"episode-{self._episode_number:06d}"
        return {
            "observation_schema": "airhockey.public_observation.v1",
            "observation": list(self._observation),
            "observation_step": self._step,
            "puck_visible": self._puck_visible_from_info(),
            "action_locked": bool(self._environment_info.get("action_locked", False)),
            "episode_active": self._episode_started,
            "terminated": self._terminated,
            "truncated": self._truncated,
            "defence_context_id": f"{episode_id}/track-{self._track_number:04d}",
            "episode_id": episode_id,
        }

    def evaluation_records(self) -> list[dict[str, Any]]:
        """Return a copy of evaluation-only records kept outside wire messages."""

        return copy.deepcopy(self._evaluation)

    def direct_replay_report(self) -> dict[str, Any]:
        """Replay host actions through a fresh direct environment and compare."""

        if self._reset_observation is None or self._shot is None:
            raise RuntimeError("cannot replay before a completed host reset")
        if self._configuration.terminate_at_step is not None:
            raise RuntimeError("forced terminal scenarios are not direct-replay eligible")
        environment = self._environment_factory(self._configuration)
        maximum_error = 0.0
        try:
            observation, info = environment.reset(shot=self._shot, seed=self._seed)
            maximum_error = max(
                maximum_error,
                self._observation_error(
                    self._reset_observation,
                    _float_list(observation, size=19, where="direct reset observation"),
                ),
            )
            self._compare_public_info(self._reset_info, dict(info), "reset")
            final_info = dict(info)
            for record in self._evaluation:
                observation, reward, terminated, truncated, info = environment.step(
                    record["requested_action"]
                )
                replayed = _float_list(
                    observation,
                    size=19,
                    where="direct replay observation",
                )
                expected = record["public_state"]["observation"]
                maximum_error = max(
                    maximum_error,
                    self._observation_error(expected, replayed),
                )
                if not math.isclose(
                    float(reward), record["reward"], rel_tol=0.0, abs_tol=1e-9
                ):
                    raise RuntimeError("host/direct reward mismatch")
                if bool(terminated) != record["public_state"]["terminated"]:
                    raise RuntimeError("host/direct terminated mismatch")
                if bool(truncated) != record["public_state"]["truncated"]:
                    raise RuntimeError("host/direct truncated mismatch")
                final_info = dict(info)
                self._compare_public_info(
                    record["environment_info"],
                    final_info,
                    f"step {record['observation_step']}",
                )
            if maximum_error > 1e-6:
                raise RuntimeError(
                    f"host/direct public observation mismatch: {maximum_error}"
                )
            return {
                "control_period_ms": 20,
                "steps": len(self._evaluation),
                "maximum_public_observation_error": maximum_error,
                "outcome": final_info.get("outcome", "pending"),
                "passed": True,
            }
        finally:
            environment.close()

    def shutdown(self) -> None:
        self._close_environment()
        self._episode_started = False
        self._pending_action = None

    def _close_environment(self) -> None:
        if self._environment is not None:
            self._environment.close()
            self._environment = None

    def _puck_visible_from_info(self) -> bool:
        if "puck_visible" not in self._environment_info:
            raise RuntimeError("ACRA public info omitted puck_visible")
        return bool(self._environment_info["puck_visible"])

    def _validate_environment_step(self) -> None:
        if int(self._environment_info.get("observation_step", -1)) != self._step:
            raise RuntimeError("ACRA public info observation_step mismatch")
        if bool(self._environment_info.get("action_locked", False)) != self._action_locked():
            raise RuntimeError("ACRA public info action_locked mismatch")

    def _evaluation_info(self) -> dict[str, Any]:
        return {
            key: self._environment_info[key]
            for key in ("observation_step", "puck_visible", "action_locked", "outcome")
            if key in self._environment_info
        }

    def _privileged_state(self) -> dict[str, Any]:
        if self._environment is None:
            raise RuntimeError("MuJoCo environment is unavailable")
        state = self._environment.privileged_state()
        outcome_tracker = getattr(self._environment, "outcome_tracker", None)
        contact_step = getattr(outcome_tracker, "first_contact_step", None)
        return {
            "puck_position_table_xy": _float_list(
                state.puck_position_table_xy,
                size=2,
                where="privileged puck position",
            ),
            "puck_velocity_table_xy": _float_list(
                state.puck_velocity_table_xy,
                size=2,
                where="privileged puck velocity",
            ),
            "contact": contact_step is not None and int(contact_step) <= self._step,
            "outcome": self._environment_info.get("outcome", "pending"),
        }

    @staticmethod
    def _observation_error(expected: list[float], observed: list[float]) -> float:
        return max(abs(left - right) for left, right in zip(expected, observed, strict=True))

    @staticmethod
    def _compare_public_info(
        expected: dict[str, Any], observed: dict[str, Any], where: str
    ) -> None:
        for key in ("observation_step", "puck_visible", "action_locked", "outcome"):
            if key in expected and expected[key] != observed.get(key):
                raise RuntimeError(f"host/direct {where} {key} mismatch")
