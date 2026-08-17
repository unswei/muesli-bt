"""Pure checks for the frozen WP9 context-token sensitivity study."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

EXAMPLE_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(EXAMPLE_ROOT))

from run_wp9 import _load_protocol, context_invalidates


class GateG9PureTest(unittest.TestCase):
    def test_protocol_freezes_public_only_policy_inputs_and_cardinality(self) -> None:
        protocol, _ = _load_protocol()

        self.assertEqual(protocol["context_observation"]["target_indices"], [16, 17])
        self.assertEqual(protocol["context_observation"]["visible_flag_index"], 18)
        self.assertFalse(protocol["context_observation"]["privileged_state"])
        self.assertEqual(protocol["paper_split"]["selected_shots"], 24)
        self.assertEqual(protocol["campaign"]["matched_cases"], 72)
        self.assertEqual(protocol["campaign"]["policy_runs"], 216)

    def test_reacquisition_identity_always_invalidates(self) -> None:
        policy = {
            "kind": "always_new_context_on_reacquisition",
            "displacement_threshold": None,
        }

        self.assertTrue(context_invalidates(policy, 0.0))

    def test_displacement_policy_keeps_the_declared_boundary_equivalent(self) -> None:
        policy = {
            "kind": "new_context_above_public_displacement",
            "displacement_threshold": 0.1,
        }

        self.assertFalse(context_invalidates(policy, 0.1))
        self.assertTrue(context_invalidates(policy, 0.1000001))


if __name__ == "__main__":
    unittest.main()
