"""Pure checks for the frozen WP7 paper protocol and evidence projections."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path
from types import SimpleNamespace

EXAMPLE_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(EXAMPLE_ROOT))

from run_wp7 import (
    _deterministic_action,
    _expected_predicates,
    _trajectory,
    load_protocol,
    select_learned_subset,
)


def _generated(shot_id: str = "shot-a") -> SimpleNamespace:
    return SimpleNamespace(
        shot=SimpleNamespace(shot_id=shot_id),
        target_goal_y=0.1,
    )


def _record(step: int, *, terminal: bool = False) -> dict[str, object]:
    observation = [0.0] * 14 + [0.1, -0.2, 0.3, -0.4, 1.0]
    return {
        "observation_step": step,
        "finished_monotonic_ns": step * 20_000_000,
        "requested_action": [0.3, -0.4],
        "applied_action": [0.3, -0.4],
        "public_state": {
            "episode_id": "episode-000001",
            "defence_context_id": (
                "episode-000001/track-0001"
                if step < 2
                else "episode-000001/track-0002"
            ),
            "puck_visible": True,
            "action_locked": False,
            "episode_active": not terminal,
            "terminated": False,
            "truncated": terminal,
            "observation": observation,
        },
        "privileged": {
            "puck_position_table_xy": [0.2, 0.1],
            "puck_velocity_table_xy": [-1.0, 0.0],
            "contact": terminal,
            "outcome": "timeout" if terminal else "pending",
        },
    }


class GateG7PureTest(unittest.TestCase):
    def test_protocol_is_frozen_before_the_paper_split_is_opened(self) -> None:
        protocol = load_protocol()
        self.assertEqual(protocol["status"], "frozen_before_muesli_test_open")
        self.assertEqual(protocol["paper_split"]["name"], "muesli_test")
        self.assertEqual(protocol["paper_split"]["expected_shots"], 72)
        self.assertEqual(protocol["campaign"]["deterministic_pairs"], 216)
        self.assertEqual(protocol["campaign"]["learned_pairs"], 12)
        self.assertEqual(protocol["campaign"]["total_pairs"], 228)
        self.assertEqual(protocol["campaign"]["total_runs"], 456)
        self.assertEqual(
            [row["delay_ms"] for row in protocol["deterministic_provider"]["delay_schedule"]],
            [50, 80, 110],
        )

    def test_learned_subset_selection_is_order_independent(self) -> None:
        shots = tuple(_generated(f"shot-{index}") for index in range(20))
        forward = select_learned_subset(shots, 12)
        reverse = select_learned_subset(tuple(reversed(shots)), 12)
        self.assertEqual(
            [value.shot.shot_id for value in forward],
            [value.shot.shot_id for value in reverse],
        )

    def test_public_provider_and_trajectory_authority_are_explicit(self) -> None:
        observation = [0.0] * 16 + [1.2, -1.4, 1.0]
        self.assertEqual(_deterministic_action(observation), [1.0, -1.0])
        records = [_record(1), _record(2), _record(3, terminal=True)]
        baseline = _trajectory(
            "baseline", records, _generated(), [0.3, -0.4], "deadline_only", 2
        )
        full = _trajectory(
            "full", records, _generated(), [0.3, -0.4], "invocation_scoped", 2
        )
        self.assertIsNone(baseline[1]["public"]["authorised_target"])
        self.assertEqual(baseline[2]["public"]["authorised_target"], [0.3, -0.4])
        self.assertTrue(all(row["public"]["authorised_target"] is None for row in full))
        self.assertEqual(baseline[-1]["privileged"]["outcome"], "timeout")
        self.assertEqual(
            _expected_predicates("invocation_scoped"),
            {
                "p7_invocation_scoped_context_rejection",
                "p7_provider_mode_matches",
                "p7_task_episode_completed",
            },
        )


if __name__ == "__main__":
    unittest.main()
