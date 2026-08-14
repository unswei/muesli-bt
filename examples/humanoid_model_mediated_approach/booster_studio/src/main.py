"""Booster Studio agent entry point.

The platform validator requires ``AgentBase`` to be a direct base of the entry
class. Platform-specific imports stay in :mod:`muesli_booster.runtime` so the
adapter policy remains testable without BoosterOS or ROS 2.
"""

from booster_agent_framework import AgentBase, AgentFeatures

from .muesli_booster.runtime import BoosterRuntime


class MuesliHumanoidAgent(AgentBase):
    """Own the ROS, BoosterOS and local muesli bridge lifecycles."""

    def __init__(self) -> None:
        super().__init__(AgentFeatures())
        self._runtime = BoosterRuntime(logger=self.logger)

    def on_agent_activated(self) -> None:
        self._runtime.start()

    def on_agent_close(self) -> None:
        self._runtime.stop()
