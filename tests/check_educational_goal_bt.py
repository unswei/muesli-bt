#!/usr/bin/env python3
from pathlib import Path
import sys


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(message)


def main() -> None:
    path = Path("examples/webots_epuck_goal/lisp/educational_goal_bt.lisp")
    source = path.read_text(encoding="utf-8")

    require("defbt educational-goal-tree" in source, "expected educational-goal-tree definition")
    require("(plan-action :name \"educational-goal-plan\"" in source, "expected integrated planner branch")
    require("act_reverse_left" in source, "expected explicit reverse-left action")
    require("act_reverse_right" in source, "expected explicit reverse-right action")
    require("act_arc_left" in source, "expected explicit arc-left action")
    require("act_arc_right" in source, "expected explicit arc-right action")
    require("act_goal_direct" in source, "expected explicit direct-goal action")
    require("collision_imminent" not in source, "educational demo should avoid the flagship collision_imminent shortcut")
    require("act_avoid" not in source, "educational demo should not hide avoidance behind act_avoid")


if __name__ == "__main__":
    main()
