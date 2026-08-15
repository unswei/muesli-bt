"""Booster Studio agent entry point.

The platform validator requires ``AgentBase`` to be a direct base of the entry
class. Platform-specific imports stay in :mod:`muesli_booster.runtime` so the
adapter policy remains testable without BoosterOS or ROS 2.
"""

from booster_agent_framework import (
    AgentBase,
    AgentFeatures,
    Component,
    DefaultStateIconComponent,
    LocaleString,
)

from .muesli_booster.runtime import BoosterRuntime
from .muesli_booster.trial_runner import TrialError


class MuesliHumanoidAgent(AgentBase):
    """Own the ROS, BoosterOS and local muesli bridge lifecycles."""

    def __init__(self) -> None:
        super().__init__(AgentFeatures())
        self._runtime = BoosterRuntime(logger=self.logger)
        icon = "res/logo.png"
        initial_motion_state = self._runtime.motion_enabled
        self.component_manager.add_components(
            [
                DefaultStateIconComponent(
                    "motion_arm",
                    LocaleString("Arm motion", "Arm motion"),
                    icon,
                    initial_motion_state,
                    self.on_motion_arm,
                ),
                DefaultStateIconComponent(
                    "trial_t1",
                    LocaleString("Run T1 normal", "Run T1 normal"),
                    icon,
                    False,
                    self.on_trial_t1,
                ),
                DefaultStateIconComponent(
                    "trial_t2a",
                    LocaleString("Run T2a baseline", "Run T2a baseline"),
                    icon,
                    False,
                    self.on_trial_t2a,
                ),
                DefaultStateIconComponent(
                    "trial_t2b",
                    LocaleString("Run T2b full", "Run T2b full"),
                    icon,
                    False,
                    self.on_trial_t2b,
                ),
                DefaultStateIconComponent(
                    "trial_t3",
                    LocaleString("Run T3 emergency", "Run T3 emergency"),
                    icon,
                    False,
                    self.on_trial_t3,
                ),
                DefaultStateIconComponent(
                    "software_emergency",
                    LocaleString("Software emergency", "Software emergency"),
                    icon,
                    False,
                    self.on_software_emergency,
                ),
            ]
        )

    def on_motion_arm(self, component: Component) -> LocaleString:
        try:
            if self._runtime.motion_enabled:
                self._runtime.disarm_motion()
                component.state = False
                message = "Motion disarmed"
            else:
                self._runtime.arm_motion()
                component.state = True
                message = "Motion armed"
            self.component_manager.update_component(component)
            return LocaleString(message, message)
        except Exception as exc:  # noqa: BLE001 - Booster SDK callback boundary
            self.logger.error(f"motion arming action failed: {exc}")
            return LocaleString("Motion arming failed", "Motion arming failed")

    def _start_trial(self, trial_id: str) -> LocaleString:
        try:
            run_dir = self._runtime.start_trial(trial_id)
            self.logger.info(f"muesli trial evidence directory: {run_dir}")
            message = f"{trial_id} started"
            return LocaleString(message, message)
        except TrialError as exc:
            self.logger.error(f"{trial_id} did not start: {exc}")
            message = f"{trial_id} not ready"
            return LocaleString(message, message)

    def on_trial_t1(self, component: Component) -> LocaleString:
        del component
        return self._start_trial("T1")

    def on_trial_t2a(self, component: Component) -> LocaleString:
        del component
        return self._start_trial("T2a")

    def on_trial_t2b(self, component: Component) -> LocaleString:
        del component
        return self._start_trial("T2b")

    def on_trial_t3(self, component: Component) -> LocaleString:
        del component
        return self._start_trial("T3")

    def on_software_emergency(self, component: Component) -> LocaleString:
        component.state = not component.state
        self._runtime.set_emergency(component.state)
        self.component_manager.update_component(component)
        message = "Emergency active" if component.state else "Emergency cleared"
        return LocaleString(message, message)

    def on_agent_activated(self) -> None:
        self._runtime.start()

    def on_agent_close(self) -> None:
        self._runtime.stop()
