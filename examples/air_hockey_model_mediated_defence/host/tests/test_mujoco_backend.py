"""Contract tests for the lazy MuJoCo host adapter using an injected environment."""

from __future__ import annotations

import json
import sys
import unittest
from dataclasses import dataclass
from pathlib import Path
from typing import Any

REPOSITORY_ROOT = Path(__file__).resolve().parents[4]
HOST_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HOST_ROOT / "src"))

from muesli_air_hockey_host import (
    MujocoDirectLaunchHostBackend,
    ProtocolProcessor,
    SchemaRegistry,
)
from muesli_air_hockey_host.backend import HostConfiguration

SCHEMA_DIRECTORY = REPOSITORY_ROOT / "schemas" / "air_hockey_host" / "v1"
PROHIBITED_KEYS = {
    "outcome",
    "privileged",
    "shot_id",
    "true_puck_position",
    "true_puck_velocity",
}


@dataclass(frozen=True)
class _PrivilegedState:
    puck_position_table_xy: tuple[float, float]
    puck_velocity_table_xy: tuple[float, float]


class _InjectedEnvironment:
    def __init__(self, configuration: HostConfiguration) -> None:
        self.configuration = configuration
        self.step_number = 0
        self.mallet = [0.0, 0.0]
        self.closed = False

    def reset(self, *, shot: object, seed: int) -> tuple[list[float], dict[str, Any]]:
        del shot, seed
        self.step_number = 0
        self.mallet = [0.0, 0.0]
        return self._observation(), self._info()

    def step(
        self, action: list[float]
    ) -> tuple[list[float], float, bool, bool, dict[str, Any]]:
        if self.step_number >= self.configuration.action_lock_steps:
            self.mallet = list(action)
        self.step_number += 1
        truncated = self.step_number >= self.configuration.timeout_steps
        return self._observation(), 0.0, False, truncated, self._info()

    def privileged_state(self) -> _PrivilegedState:
        return _PrivilegedState(
            puck_position_table_xy=(0.5 - 0.1 * self.step_number, 0.1),
            puck_velocity_table_xy=(-1.0, 0.0),
        )

    def close(self) -> None:
        self.closed = True

    def _visible(self) -> bool:
        start = self.configuration.blackout_start_step
        end = start + self.configuration.blackout_length_steps
        return not (start <= self.step_number < end)

    def _observation(self) -> list[float]:
        puck = [0.2, -0.1] if self._visible() else [0.0, 0.0]
        return [0.0] * 14 + list(self.mallet) + puck + [float(self._visible())]

    def _info(self) -> dict[str, Any]:
        truncated = self.step_number >= self.configuration.timeout_steps
        result: dict[str, Any] = {
            "shot_id": "prohibited-wire-value",
            "observation_step": self.step_number,
            "puck_visible": self._visible(),
            "action_locked": self.step_number < self.configuration.action_lock_steps,
        }
        if truncated:
            result["outcome"] = "timeout"
        return result


def _request(request_id: str, operation: str, payload: dict[str, Any] | None = None) -> bytes:
    return json.dumps(
        {
            "schema_version": "airhockey.host.request.v1",
            "request_id": request_id,
            "op": operation,
            "payload": payload or {},
        },
        separators=(",", ":"),
        sort_keys=True,
    ).encode("utf-8")


def _all_keys(value: Any) -> set[str]:
    if isinstance(value, dict):
        return set(value).union(*(_all_keys(item) for item in value.values()))
    if isinstance(value, list):
        return set().union(*(_all_keys(item) for item in value))
    return set()


class MujocoBackendContractTest(unittest.TestCase):
    def setUp(self) -> None:
        self.environments: list[_InjectedEnvironment] = []

        def factory(configuration: HostConfiguration) -> _InjectedEnvironment:
            environment = _InjectedEnvironment(configuration)
            self.environments.append(environment)
            return environment

        self.backend = MujocoDirectLaunchHostBackend(
            environment_factory=factory,
            shot_factory=object,
        )
        self.processor = ProtocolProcessor(
            SchemaRegistry(SCHEMA_DIRECTORY), self.backend
        )

    def exchange(
        self,
        request_id: str,
        operation: str,
        payload: dict[str, Any] | None = None,
    ) -> dict[str, Any]:
        return json.loads(
            self.processor.process(_request(request_id, operation, payload))
        )

    def act_and_step(self, sequence: int, action: list[float]) -> dict[str, Any]:
        self.assertTrue(
            self.exchange(f"act-{sequence}", "act", {"action": action})["ok"]
        )
        response = self.exchange(f"step-{sequence}", "step")
        self.assertTrue(response["ok"])
        return response

    def test_simulator_adapter_replays_and_keeps_privileged_state_off_wire(self) -> None:
        info = self.exchange("info", "info")
        self.assertEqual(info["result"]["backend"], "acra_direct_launch")
        self.exchange(
            "configure",
            "configure",
            {
                "blackout_start_step": 1,
                "blackout_length_steps": 1,
                "timeout_steps": 3,
            },
        )
        reset = self.exchange("reset", "reset", {"seed": 6302})
        first_context = reset["result"]["state"]["defence_context_id"]
        blackout = self.act_and_step(1, [0.25, -0.4])
        reacquired = self.act_and_step(2, [0.0, 0.0])
        terminal = self.act_and_step(3, [0.1, 0.2])

        self.assertFalse(blackout["result"]["state"]["puck_visible"])
        self.assertEqual(
            blackout["result"]["state"]["defence_context_id"], first_context
        )
        self.assertEqual(
            reacquired["result"]["state"]["defence_context_id"],
            "episode-000001/track-0002",
        )
        self.assertTrue(terminal["result"]["state"]["truncated"])
        self.assertFalse(PROHIBITED_KEYS & _all_keys(reset))
        self.assertFalse(PROHIBITED_KEYS & _all_keys(blackout))
        self.assertFalse(PROHIBITED_KEYS & _all_keys(terminal))

        records = self.backend.evaluation_records()
        self.assertEqual(len(records), 3)
        self.assertIn("privileged", records[0])
        self.assertEqual(records[-1]["privileged"]["outcome"], "timeout")
        replay = self.backend.direct_replay_report()
        self.assertTrue(replay["passed"])
        self.assertEqual(replay["steps"], 3)
        self.assertEqual(replay["maximum_public_observation_error"], 0.0)
        self.assertEqual(len(self.environments), 2)
        self.assertTrue(self.environments[-1].closed)

        closed = self.exchange("close", "close")
        self.assertTrue(closed["ok"])
        self.assertTrue(self.environments[0].closed)


if __name__ == "__main__":
    unittest.main()
